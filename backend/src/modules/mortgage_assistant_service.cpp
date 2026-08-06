module;
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <exception>
#include <future>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#include <grpcpp/grpcpp.h>
#include "mortgage_assistant.pb.h"
#include "mortgage_assistant.grpc.pb.h"

module mortgage_assistant_service;

import sensen.llm_pipeline;
import sensen.tokenizer;  // Tokenizer, for the grammar's id -> text vocabulary
import fastjson;
import logger;
import quota;
import api_key;
import assistant_verification;
import mortgage_verification;
import mortgage_grammar;

// ============================================================================
// The mortgage / time-value-of-money assistant.
//
// @author Olumuyiwa Oluwasanmi
//
// The sibling of assistant_service.cpp, serving a DIFFERENT fine-tuned
// Qwen3-0.6B against a different label space: the 26 in-scope RPCs of
// sensen.finance.Finance (mortgages, HELOC, time value of money, cash-flow
// analysis, depreciation, real estate) rather than the 48 option/futures
// strategies. See proto/mortgage_assistant.proto's banner for the contract and
// for why a clarification and a refusal are both gRPC OK.
//
// ---------------------------------------------------------------------------
// ON THE DUPLICATED INFERENCE MACHINERY BELOW -- read before "fixing" it.
//
// `QueuedBackend` and `SensenBackend` here are a deliberate near-copy of
// assistant_service.cpp's, not an oversight. Both of those classes live in that
// file's ANONYMOUS namespace, so there is nothing to import; making them
// shareable means lifting them into a new module and rewriting the strategy
// assistant to consume it -- a change to a service that is in production,
// gated, and measured, in order to add a second one beside it. The honest
// trade was made in that direction on purpose: this file carries the
// duplication and says so, rather than putting the working assistant at risk to
// avoid it. Extracting a shared `assistant_inference` module, with BOTH
// services moved onto it in one change and both re-measured, is the follow-up
// -- and it is a refactor, not a bug fix.
//
// What is NOT duplicated: the llama.cpp backend. CLAUDE.md is explicit that
// llama.cpp is a debugging and cross-checking tool, that `ASSISTANT_BACKEND`
// defaults to sensen, that production is built with the llama.cpp backend
// compiled OUT, and that every gate in this repo is defined on the sensen path.
// A second selectable engine here would be a second serving path no gate
// covers, for a model no probe has ever been pointed at. sensen is the only
// backend this service has, and asking for another one is not expressible
// rather than silently ignored.
//
// ---------------------------------------------------------------------------
// WHY A SECOND PIPELINE AND A SECOND OWNER THREAD, and not a shared one.
//
// This is a SECOND model in the same process. It has its own weights, so it
// needs its own `sensen::LLMPipeline` -- weights are held per pipeline, and one
// pipeline cannot hold two models. It also needs its own owner thread, and that
// is the load-bearing half: sensen's `FeedForwardNetwork` keeps its per-call
// working buffers as `mutable` MEMBERS of each layer object rather than
// `thread_local`, so the forward path of one pipeline is safe only when exactly
// one thread ever enters it. Two pipelines are two independent sets of layer
// objects, so two owner threads -- one each -- is correct and sharing a single
// thread between them would only serialise two engines that have no reason to
// wait on each other.
//
// The cost is stated rather than hidden: this process now holds two Qwen3-0.6B
// checkpoints resident (roughly 640 MB of Q8_0 weights each, plus each
// pipeline's own KV cache across its concurrent slots). The two are configured
// independently -- `MORTGAGE_ASSISTANT_*` versus `ASSISTANT_*` -- so a
// deployment that wants only one of them can leave the other's model path unset
// and pay for neither.
// ============================================================================

namespace options_calculator::mortgage_assistant {

// The grammar module lives in the mortgage calculator's own namespace, not this
// service's. Aliased once here for the same reason `mv` is aliased further down.
namespace mg = ::mortgage_calculator::assistant::grammar;

using grpc::ServerContext;
using grpc::Status;

namespace {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

/**
 * THE MANDATORY TRAINING SYSTEM PROMPT.
 *
 * Verbatim, character for character, from the `system` turn of every row in
 * agent/dataset/data_mortgage/train.jsonl -- which is itself emitted from the
 * `SYSTEM` constant in agent/dataset/build_mortgage_dataset.py, whose own
 * comment states the requirement from the other side ("this string must end up
 * byte-identical to the mortgage assistant's serving-side system prompt ...
 * Whoever wires the serving prompt builder for this assistant must copy this
 * constant verbatim, not paraphrase it").
 *
 * Without it the model reverts to stock Qwen3: it emits a `<think>` block and
 * prose, and NEVER a `<params>` block. That is not a degradation this file
 * could detect and report -- every request would simply fall through to the
 * clarification/refusal path with no indication of why -- so the prompt is
 * injected on EVERY call with no opt-out. Changing so much as a comma here
 * silently converts a working extraction model back into an uninstructed base
 * model.
 */
constexpr std::string_view kSystemPrompt =
    "You turn a homeowner's or investor's request into parameters for the "
    "Finance service's mortgage, time-value-of-money and cash-flow tools. "
    "Reply with a single JSON object inside <params></params> when you have "
    "enough to act, or ask exactly one short question when you do not. You "
    "do not give financial, tax or legal advice.";

/**
 * Upper bound on generated tokens per call.
 *
 * Sized from the training set rather than guessed: the longest assistant turn
 * in agent/dataset/data_mortgage/train.jsonl is 473 characters -- a
 * `ComputeHomeNpv` block, fourteen fields, mostly digits -- which is the
 * worst case this label space can produce, since every operation emits exactly
 * its request message's fields and `HomeNpvRequest`/`RefinanceRequest` are the
 * widest at fourteen. Digit-dense JSON tokenizes at roughly two to three
 * characters per token on Qwen3's vocabulary, so that block plus the empty
 * `<think>` wrapper lands near 200 tokens; 384 is headroom over the widest
 * real answer, not a shot in the dark.
 *
 * It is deliberately LARGER than the strategy assistant's 256, because that
 * model's widest answer is six short fields and this one's is fourteen. It
 * exists mainly to bound the worst case -- a model that fails to close its tag
 * and just keeps going -- so one bad call cannot hold the owner thread for an
 * unbounded stretch. It is also the exact figure `cost_llm_generate` charges
 * against, so raising it changes the ceiling and the price together.
 */
constexpr std::size_t kMaxNewTokens = 384;

/** A clarifying question longer than this does not look like "one short
 * question" any more -- either the model rambled or the system prompt did not
 * take, and passing it through as-is would be a worse experience than a refusal
 * that says so plainly. */
constexpr std::size_t kMaxClarificationLength = 400;

/**
 * Server-side caps on the two caller-supplied strings, checked BEFORE the
 * prompt is assembled or the worker is touched.
 *
 * Same rationale as the strategy assistant's: `kMaxNewTokens` bounds the OUTPUT
 * and prices the call, but the real cost also scales with PREFILL, which is a
 * function of these two caller-controlled strings and which the price never
 * accounts for. `kMaxPriorClarificationLength` reuses `kMaxClarificationLength`
 * deliberately: `prior_clarification` is documented as always being a question
 * THIS service asked on a prior turn, and this service never emits one longer
 * than that -- so a longer value is itself evidence the field is being misused.
 */
constexpr std::size_t kMaxUtteranceLength = 1000;
constexpr std::size_t kMaxPriorClarificationLength = kMaxClarificationLength;

/**
 * Bounds on the parameter values themselves.
 *
 * `kMaxDecimalTextLength` is generous against sensen's BigDecimal (eighteen
 * decimal places on an __int128) while still rejecting a run-on string that
 * could only be a decode failure. `kMaxArrayLength` bounds the one nested shape
 * these requests have -- a cash-flow series -- at a length no chat message
 * plausibly dictates, and keeps a single map value from growing without limit.
 * `kMaxAbsMagnitude` rejects a figure no loan, cash flow or property value
 * reaches; it is a hallucination guard, not a business rule, which is why it is
 * set far above any real input rather than at a plausible ceiling.
 */
constexpr std::size_t kMaxDecimalTextLength = 48;
constexpr std::size_t kMaxArrayLength = 512;
constexpr double kMaxAbsMagnitude = 1e15;

// ---------------------------------------------------------------------------
// Environment helpers
// ---------------------------------------------------------------------------

[[nodiscard]] auto env_string(const char* name) -> std::optional<std::string> {
    const char* raw = std::getenv(name);
    if (raw == nullptr || *raw == '\0') return std::nullopt;
    return std::string{raw};
}

/** Parses a positive integer from an env var, falling back on anything unset,
 * empty, non-numeric or non-positive -- a malformed override should degrade to
 * the documented default, never to zero threads or a crash. */
[[nodiscard]] auto env_positive_int(const char* name, int fallback) -> int {
    const char* raw = std::getenv(name);
    if (raw == nullptr || *raw == '\0') return fallback;
    const std::string_view view{raw};
    int value = 0;
    const auto result = std::from_chars(view.data(), view.data() + view.size(), value);
    if (result.ec != std::errc{} || value <= 0) return fallback;
    return value;
}

// ---------------------------------------------------------------------------
// Prompt construction
// ---------------------------------------------------------------------------

/**
 * Builds the ChatML prompt this model was fine-tuned on.
 *
 * The turn SHAPE is load-bearing and is copied from the training data rather
 * than invented here. Every clarification example in
 * agent/dataset/build_mortgage_dataset.py is
 *
 *     user(full request) -> assistant(one short question)
 *     -> user(one short reply) -> assistant(<params> block)
 *
 * so on an answering turn the user's original request stays the FIRST user
 * turn and their short reply ("37 years") is a LATER USER turn -- never an
 * assistant turn, and never placed before the request it answers. Getting this
 * backwards on the sibling service was a real production defect: the same
 * request that produced the correct label when phrased in one turn produced a
 * degraded near-miss when the reply was put in the assistant's mouth ahead of
 * it (see `build_prompt` in assistant_service.cpp). This builder was written
 * from that conclusion, not from first principles.
 *
 * A short neutral placeholder fills the assistant slot in between so the roles
 * keep strictly alternating, matching the training shape structurally. Its
 * wording is not load-bearing: ParseRequest deliberately does not carry the
 * question this service asked last turn (only the last two turns of the true
 * three-turn exchange survive the round trip), so the real question text is not
 * reconstructable here. What the placeholder buys is ROLE and POSITION, not
 * content.
 *
 * When `prior_clarification` is empty -- every first-turn call -- this emits
 * exactly the three-turn system/user/assistant prompt the non-clarification
 * training rows use.
 */
[[nodiscard]] auto build_prompt(std::string_view utterance, std::string_view prior_clarification)
    -> std::string {
    std::string prompt;
    prompt += "<|im_start|>system\n";
    prompt += kSystemPrompt;
    prompt += "<|im_end|>\n";
    prompt += "<|im_start|>user\n";
    prompt += utterance;
    prompt += "<|im_end|>\n";
    if (!prior_clarification.empty()) {
        prompt += "<|im_start|>assistant\nCould you clarify?<|im_end|>\n";
        prompt += "<|im_start|>user\n";
        prompt += prior_clarification;
        prompt += "<|im_end|>\n";
    }
    prompt += "<|im_start|>assistant\n";
    return prompt;
}

// ---------------------------------------------------------------------------
// Inference backend
// ---------------------------------------------------------------------------

/** What the backend hands back for one generation request. */
struct InferenceOutcome {
    bool ok = false;
    std::string text;   // the model's raw decoded output, valid iff ok
    std::string error;  // human-readable failure detail, valid iff !ok
};

/**
 * One accepted request, in flight from the gRPC handler thread that accepted it
 * to the owner thread that will serve it.
 *
 * The promise is fulfilled exactly once, on every path out of the owner thread
 * including shutdown -- a promise destroyed unfulfilled would make the handler
 * thread's `future.get()` throw `std::future_error` instead of returning, which
 * is a strictly worse failure than an honest error string.
 */
struct PendingJob {
    std::string prompt;
    std::promise<InferenceOutcome> promise;
};

/**
 * The admission queue and back-pressure policy.
 *
 * Every accepted request holds a gRPC handler thread blocked on `submit()` for
 * the duration of one extraction. An unbounded queue would let load pile up
 * invisibly -- the caller-facing symptom would be requests taking longer and
 * longer, with no signal distinguishing "briefly busy" from "about to wait
 * minutes". A bounded queue makes the choice explicit: once full, a request
 * fails FAST with RESOURCE_EXHAUSTED rather than joining an invisible line.
 *
 * The queue is the WAITING ROOM in front of the in-flight set, not the whole
 * capacity: the owner thread runs continuous batching, holding several
 * sequences and advancing all of them by one token per fused forward pass,
 * admitting newly-queued requests into slots as earlier ones finish.
 */
class QueuedBackend {
  public:
    QueuedBackend() = default;
    virtual ~QueuedBackend() = default;
    QueuedBackend(const QueuedBackend&) = delete;
    auto operator=(const QueuedBackend&) -> QueuedBackend& = delete;
    QueuedBackend(QueuedBackend&&) = delete;
    auto operator=(QueuedBackend&&) -> QueuedBackend& = delete;

    /**
     * Submits one prompt and blocks the CALLING thread until the backend has
     * produced a result.
     *
     * Returns `std::nullopt` iff the admission queue was already full when this
     * call arrived -- the caller must map that, and only that, to
     * RESOURCE_EXHAUSTED. Any other outcome, including a failed generation,
     * comes back as an `InferenceOutcome` with `ok == false`.
     */
    [[nodiscard]] auto submit(std::string prompt) -> std::optional<InferenceOutcome> {
        // The promise/future pair is only constructed once there is confirmed
        // room -- the full-queue rejection path stays as cheap as an immediate
        // refusal should be.
        std::future<InferenceOutcome> future;
        {
            const std::lock_guard lock{mutex_};
            if (shutting_down_) {
                return InferenceOutcome{
                    .ok = false, .text = {}, .error = "mortgage assistant backend is shutting down"};
            }
            if (queue_.size() >= max_queue_depth_) {
                return std::nullopt;
            }
            PendingJob job;
            job.prompt = std::move(prompt);
            future = job.promise.get_future();
            queue_.push_back(std::move(job));
        }
        cv_.notify_one();
        return future.get();
    }

  protected:
    /**
     * Blocks the owner thread until at least one job is queued or a stop is
     * requested, then moves up to `want` jobs out of the queue.
     *
     * `want` is how many free in-flight slots the caller has right now, so the
     * backend never dequeues work it has no slot to run. `block` is false when
     * the caller already has sequences in flight: it must not park on the
     * condition variable while there is decoding to do.
     */
    [[nodiscard]] auto take_jobs(std::stop_token stoken, std::size_t want, bool block)
        -> std::vector<PendingJob> {
        std::vector<PendingJob> taken;
        if (want == 0) return taken;

        std::unique_lock lock{mutex_};
        if (block) {
            // The stop_token-aware overload registers a stop callback that
            // notifies this condition variable the moment a stop is requested,
            // so this wakes promptly on shutdown rather than only on the next
            // enqueue.
            const bool has_job = cv_.wait(lock, stoken, [this] { return !queue_.empty(); });
            if (!has_job) return taken;
        }
        while (!queue_.empty() && taken.size() < want) {
            taken.push_back(std::move(queue_.front()));
            queue_.pop_front();
        }
        return taken;
    }

    /**
     * Fails every job still queued so no caller blocked in `submit()`'s
     * `future.get()` hangs forever waiting on a promise this backend will never
     * fulfil. `shutting_down_` is set under the same lock so a request that
     * races the shutdown gets an honest error rather than being enqueued onto a
     * queue nobody will ever drain again.
     */
    auto drain_and_fail(std::string_view reason) -> void {
        std::deque<PendingJob> leftovers;
        {
            const std::lock_guard lock{mutex_};
            shutting_down_ = true;
            leftovers.swap(queue_);
        }
        for (auto& job : leftovers) {
            job.promise.set_value(
                InferenceOutcome{.ok = false, .text = {}, .error = std::string{reason}});
        }
    }

    /** How many requests may be decoding simultaneously in one fused batch. */
    std::size_t max_concurrent_ = 1;
    /** How many may WAIT for a slot before further arrivals are refused. */
    std::size_t max_queue_depth_ = 1;

  private:
    std::mutex mutex_;
    std::condition_variable_any cv_;
    std::deque<PendingJob> queue_;
    bool shutting_down_ = false;
};

/**
 * Drives this service's OWN `sensen::LLMPipeline` through its iteration-level
 * scheduler API from ONE owner thread, batching every in-flight request into a
 * single fused decode step per iteration.
 *
 * WHY THE SCHEDULER API AND NOT `generate()` FROM A THREAD POOL:
 *
 * `LLMPipeline::generate()` is not safe to call concurrently on one pipeline,
 * and the reason is specific rather than a general disclaimer. sensen's
 * `FeedForwardNetwork` (sensen/src/transformer_block.cppm) keeps its per-call
 * working buffers as `mutable std::vector<float> scratch_up_/scratch_gate_`
 * MEMBERS -- one set per layer object, not `thread_local` -- and the
 * single-token decode entry point writes straight into them. The layer objects
 * belong to the one model the pipeline shares, so two threads decoding
 * different requests would interleave writes into the same scratch and silently
 * corrupt each other's hidden states. That is a WRONG-ANSWER race, not a crash,
 * which is the worst kind to ship. The same hazard rules out the library's own
 * `generateBatch`/`generateConcurrent` wrappers, which fan `forward()` across an
 * internal thread pool and hit exactly this.
 *
 * The scheduler API is built on the opposite premise: one owner thread is the
 * sole caller of the forward path, and concurrency comes from batching N
 * sequences into a single `forwardBatch` step rather than from N threads. Its
 * multi-token batched forward uses genuinely `thread_local` scratch, so the race
 * above does not apply to it.
 *
 * This instance is entirely separate from the strategy assistant's: separate
 * pipeline, separate weights, separate owner thread. Nothing about the two
 * interacts, which is the point -- one owner thread per pipeline is the
 * invariant, and two pipelines sharing one thread would satisfy the safety rule
 * while needlessly serialising two independent models.
 */
class SensenBackend final : public QueuedBackend {
  public:
    /**
     * Loads the model and starts the owner thread. Returns nullptr -- never a
     * half-constructed object -- if anything about the load fails, so the
     * caller's only decision is "did I get a backend or not".
     */
    [[nodiscard]] static auto create(const std::string& model_path, std::size_t max_concurrent,
                                     std::size_t queue_depth, std::size_t kv_max_seq_len,
                                     int threads) -> std::unique_ptr<SensenBackend> {
        std::unique_ptr<sensen::LLMPipeline> pipeline;
        try {
            pipeline = sensen::LLMPipeline::fromGGUF(model_path)
                           .maxAgents(max_concurrent)
                           .kvCacheMaxSeqLen(kv_max_seq_len)
                           .numThreads(static_cast<std::size_t>(threads))
                           .build();
        } catch (const std::exception& e) {
            logger::Logger::getInstance().error(
                "mortgage assistant: failed to load the model from {}: {}", model_path, e.what());
            return nullptr;
        } catch (...) {
            logger::Logger::getInstance().error(
                "mortgage assistant: failed to load the model from {}: unknown error", model_path);
            return nullptr;
        }
        if (pipeline == nullptr) {
            logger::Logger::getInstance().error(
                "mortgage assistant: LLMPipeline::Builder::build() returned null for {}",
                model_path);
            return nullptr;
        }

        auto backend = std::unique_ptr<SensenBackend>(
            new SensenBackend(std::move(pipeline), max_concurrent, queue_depth));
        backend->start();
        return backend;
    }

    [[nodiscard]] static auto name() noexcept -> std::string_view { return "sensen"; }

    ~SensenBackend() override = default;

  private:
    /**
     * One in-flight request as the decode loop sees it: the sensen session that
     * owns its KV cache, the token it is about to consume, the ids it has
     * produced so far, and the promise waiting on the other end.
     */
    struct Sequence {
        std::unique_ptr<sensen::AgentSession> agent;
        std::promise<InferenceOutcome> promise;
        std::uint32_t next_token = 0;
        std::vector<std::uint32_t> generated;

        /**
         * Everything this sequence has emitted, detokenised as it goes.
         *
         * Accumulated incrementally rather than re-decoding `generated` each
         * step, which would be quadratic in the response length. Its only
         * purpose is to spot the grammar's activation marker; the finished text
         * still comes from `schedulerDecode` so nothing downstream depends on
         * this being byte-identical to a whole-sequence decode.
         */
        std::string emitted;

        /**
         * The grammar constraining this sequence, or nullptr while it is
         * decoding unconstrained.
         *
         * Null for the whole of the `<think>` block and null forever on a
         * sequence that answers with a clarifying question -- 46 of 600 gold
         * rows do. See `arm_grammar` for why the constraint is switched on
         * partway through rather than at the first token.
         */
        mg::MortgageParamsGrammar* grammar = nullptr;
        std::size_t grammar_slot = kNoSlot;
    };

    static constexpr std::size_t kNoSlot = static_cast<std::size_t>(-1);

    SensenBackend(std::unique_ptr<sensen::LLMPipeline> pipeline, std::size_t max_concurrent,
                  std::size_t queue_depth)
        : pipeline_{std::move(pipeline)} {
        max_concurrent_ = max_concurrent;
        max_queue_depth_ = queue_depth;
    }

    auto start() -> void {
        worker_ = std::jthread([this](std::stop_token stoken) { run(stoken); });
    }

    /**
     * The sampling configuration every request through this service is
     * generated with.
     */
    [[nodiscard]] static auto generation_config(std::size_t max_new_tokens)
        -> sensen::GenerationConfig {
        sensen::GenerationConfig config;

        // Greedy, not sampled: this is structured extraction, not creative
        // generation. The training system prompt asks for one JSON object or
        // one question -- there is no case where sampling diversity is wanted,
        // and greedy decode is the more reproducible, more honest choice for a
        // tool that must never invent variation the user did not ask for.
        config.strategy = sensen::SamplingStrategy::GREEDY;
        config.max_new_tokens = max_new_tokens;
        config.deterministic = true;

        // THIS LINE IS LOAD-BEARING AND MUST NOT BE REMOVED.
        //
        // A default-constructed sensen::GenerationConfig arrives with
        // `n_gpu_layers = SIZE_MAX` and `compute_backend = AUTO`. Those two
        // defaults together satisfy every conjunct of an "on-device greedy
        // sampling" fast path inside LLMPipeline: greedy strategy, no
        // penalties, no grammar, no logprobs, a backend that counts as GPU
        // (AUTO did), and all layers on the GPU (min(SIZE_MAX, L) == L). The
        // pipeline then sets `on_device_sampling` and, from the SECOND
        // generated token onward, reads the next token id out of `logits[0]` --
        // because the only code that honours that flag by returning a
        // one-element vector holding the sampled id lives inside
        // `#ifdef SENSEN_HAS_CUDA`, which this CPU-only build never defines.
        // The CPU forward path returns the full vocabulary-sized logit vector
        // and never consults the flag, so `logits[0]` is a raw float score, and
        // casting it to a token id lands in the low single digits -- which in
        // Qwen's vocabulary are the punctuation characters `&'()*+,-`.
        //
        // The symptom on the sibling service was 262 bytes of punctuation
        // noise, byte-identical across runs (nothing random is involved once
        // the sampler is bypassed) and completely unaffected by `deterministic`,
        // with only the FIRST token correct. Setting n_gpu_layers to zero breaks
        // the conjunction, so the flag is never set and every token is sampled
        // properly.
        //
        // The underlying defect has also been fixed in sensen itself, but this
        // line stays regardless. It states what this service actually wants
        // (run entirely on the CPU; there is no GPU in the deployment target)
        // instead of relying on a library default, and it keeps the service
        // correct against any sensen build, fixed or not.
        config.n_gpu_layers = 0;

        // MUST be 1.0, and this line is as load-bearing as n_gpu_layers=0 above.
        //
        // sensen::Sampler::sample (llm_pipeline.cppm) applies repetition_penalty
        // to the logits BEFORE the greedy argmax -- so it changes which token
        // "greedy" picks, despite greedy decode having no business being
        // penalised at all. GenerationConfig's default is 1.1 over a 64-token
        // window (llm_interfaces.cppm), and the penalty divides a logit by 1.1
        // ONCE PER OCCURRENCE in that window, i.e. exponentially in repeat count.
        //
        // Structured JSON is digit-dense and Qwen3 tokenises one digit per
        // token, so '0' lands in the window 8-12 times in a normal params block
        // and its logit collapses by 14-18 points. Measured A/B on 90 identical
        // held-out rows: 0/90 exact at 1.1, 25/90 (27.8%) at 1.0. The whole
        // failure was this field.
        //
        // It also produced the fullwidth-zero symptom: U+FF10 is a DIFFERENT
        // token id, so it escapes the penalty entirely while ASCII '0' is being
        // crushed, and greedy then prefers it -- 2/100 rows carried a homoglyph
        // at 1.1, 0/100 at 1.0. Nothing was wrong with the decode path or the
        // checkpoint; an independent llama.cpp rollout on the same GGUF agreed
        // with sensen at 8/30 both ways.
        config.repetition_penalty = 1.0F;

        return config;
    }

    /**
     * The owner thread: admit, decode one fused step across everything in
     * flight, retire whatever finished, repeat. This thread is the SOLE caller
     * of `pipeline_`'s forward path for this model's lifetime.
     */
    auto run(std::stop_token stoken) -> void {
        const auto params = generation_config(kMaxNewTokens);
        const std::uint32_t eos_id = pipeline_->schedulerEosToken();

        // `<|im_end|>` is how ChatML ends an assistant turn, and on this
        // fine-tune it is what actually terminates a response; the model's
        // configured EOS may or may not be the same id, so both are treated as
        // stop tokens rather than assuming. Resolved once, here, because it is
        // a tokenizer lookup that cannot change afterwards.
        std::uint32_t im_end_id = eos_id;
        if (const auto encoded = pipeline_->schedulerEncode("<|im_end|>"); encoded.size() == 1) {
            im_end_id = encoded.front();
        }

        // Built here rather than lazily on the first token: this is the owner
        // thread, it runs exactly once, and the outcome is logged at startup
        // where an operator can see whether constrained decoding is actually on
        // instead of having to infer it from the shape of a response.
        ensure_grammar_pool();

        std::vector<Sequence> active;
        std::size_t next_agent_id = 0;

        while (!stoken.stop_requested()) {
            // Admit whatever is waiting into free slots. Block only when there
            // is nothing in flight -- with sequences decoding, this thread must
            // return to the decode step promptly rather than parking.
            const std::size_t free_slots = max_concurrent_ - active.size();
            auto jobs = take_jobs(stoken, free_slots, active.empty());
            for (auto& job : jobs) {
                Sequence seq;
                seq.promise = std::move(job.promise);
                if (!admit(job.prompt, seq, params, next_agent_id++)) {
                    // Admission is the one place a per-request failure is
                    // isolated: this sequence's promise is completed with the
                    // error and it never enters the active set, so one
                    // malformed prompt cannot take down the batch.
                    continue;
                }
                active.push_back(std::move(seq));
            }

            if (active.empty()) {
                continue;
            }

            if (!decode_step(active, params, eos_id, im_end_id)) {
                // A failed fused step is not attributable to any single
                // sequence -- the whole batch shared the forward pass -- so
                // every in-flight request is failed honestly rather than
                // silently retried or, worse, completed with whatever partial
                // text happened to be buffered.
                for (auto& seq : active) {
                    seq.promise.set_value(InferenceOutcome{
                        .ok = false,
                        .text = {},
                        .error = "batched decode step failed inside the inference pipeline"});
                    pipeline_->schedulerEvict(*seq.agent);
                }
                active.clear();
            }
        }

        // Shutdown. Everything still decoding is failed, then the waiting room
        // is drained, so no caller is left blocked on a promise that will never
        // be fulfilled.
        for (auto& seq : active) {
            seq.promise.set_value(InferenceOutcome{
                .ok = false, .text = {}, .error = "mortgage assistant worker shutting down"});
            pipeline_->schedulerEvict(*seq.agent);
        }
        active.clear();
        drain_and_fail("mortgage assistant worker shutting down");
    }

    /**
     * Prefills one newly-accepted prompt and samples its first token, leaving
     * `seq` ready to be advanced by the decode loop. Returns false, having
     * already completed the promise with an error, if the prompt could not be
     * admitted.
     */
    [[nodiscard]] auto admit(const std::string& prompt, Sequence& seq,
                             const sensen::GenerationConfig& params, std::size_t agent_id) -> bool {
        try {
            const auto prompt_tokens = pipeline_->schedulerEncode(prompt);
            seq.agent = pipeline_->schedulerMakeAgent(agent_id);
            if (seq.agent == nullptr) {
                seq.promise.set_value(InferenceOutcome{
                    .ok = false, .text = {}, .error = "could not allocate an inference session"});
                return false;
            }
            const auto logits = pipeline_->schedulerPrefill(*seq.agent, prompt_tokens, params);
            if (logits.empty()) {
                pipeline_->schedulerEvict(*seq.agent);
                seq.promise.set_value(InferenceOutcome{
                    .ok = false, .text = {}, .error = "prompt prefill produced no logits"});
                return false;
            }
            seq.next_token = pipeline_->schedulerSample(logits, params, seq.agent->getContext());
            return true;
        } catch (const std::exception& e) {
            seq.promise.set_value(InferenceOutcome{.ok = false, .text = {}, .error = e.what()});
            return false;
        } catch (...) {
            seq.promise.set_value(InferenceOutcome{
                .ok = false, .text = {}, .error = "unknown failure admitting the prompt"});
            return false;
        }
    }

    /**
     * Advances every in-flight sequence by exactly one token in ONE fused
     * forward pass, retires the ones that finished, and returns false only if
     * the shared step itself failed.
     *
     * The cadence is `schedulerDecodeStep`'s documented precondition: the token
     * sampled on the previous iteration is fed in, THEN each agent's position is
     * advanced for the token just consumed, THEN that token is appended to the
     * context and checked for a stop, and only then is the next one sampled.
     */
    [[nodiscard]] auto decode_step(std::vector<Sequence>& active,
                                   const sensen::GenerationConfig& params, std::uint32_t eos_id,
                                   std::uint32_t im_end_id) -> bool {
        std::vector<std::uint32_t> tokens;
        std::vector<sensen::AgentSession*> agents;
        tokens.reserve(active.size());
        agents.reserve(active.size());
        for (auto& seq : active) {
            tokens.push_back(seq.next_token);
            agents.push_back(seq.agent.get());
        }

        std::vector<std::vector<float>> logits;
        try {
            logits = pipeline_->schedulerDecodeStep(std::span<const std::uint32_t>(tokens),
                                                    std::span<sensen::AgentSession* const>(agents));
        } catch (const std::exception& e) {
            logger::Logger::getInstance().error(
                "mortgage assistant: batched decode step threw: {}", e.what());
            return false;
        } catch (...) {
            logger::Logger::getInstance().error(
                "mortgage assistant: batched decode step threw an unknown exception");
            return false;
        }
        if (logits.size() != active.size()) {
            logger::Logger::getInstance().error(
                "mortgage assistant: batched decode step returned {} logit vectors for {} active "
                "sequences",
                logits.size(), active.size());
            return false;
        }

        std::vector<Sequence> still_active;
        still_active.reserve(active.size());
        for (std::size_t i = 0; i < active.size(); ++i) {
            Sequence& seq = active[i];
            seq.agent->incrementPosition();

            const std::uint32_t consumed = tokens[i];
            bool stop = (consumed == eos_id) || (consumed == im_end_id);
            if (!stop) {
                seq.agent->getContext().push_back(consumed);
                seq.generated.push_back(consumed);

                // Advance the constraint by the token actually taken, and only
                // then look for the activation marker -- in that order, so the
                // token that COMPLETES the marker arms the grammar rather than
                // being fed to an automaton that has not been primed with it.
                if (seq.grammar != nullptr && !seq.grammar->accept(consumed)) {
                    // A mask/automaton disagreement. sensen's IGrammar contract
                    // is explicit that a false return must not be ignored: the
                    // constraint has desynced from the text, so every mask after
                    // this one describes a state the sequence is not in.
                    // Dropping the constraint keeps the answer coming and leaves
                    // the verifier -- which is what actually decides safety -- in
                    // charge, which is strictly better than masking against a lie.
                    logger::Logger::getInstance().error(
                        "mortgage assistant: grammar rejected token {} it had allowed -- "
                        "constraint DROPPED for this sequence, verifier still gates the answer",
                        consumed);
                    release_grammar(seq);
                }
                if (!grammar_pool_.empty()) {
                    seq.emitted += pipeline_->getTokenizer().decodeToken(consumed);
                    arm_grammar(seq);
                }

                if (seq.generated.size() >= params.max_new_tokens) stop = true;
            }
            if (!stop && logits[i].empty()) {
                // A per-sequence empty logit vector from a step that otherwise
                // succeeded is not something to paper over: finish this one
                // honestly and let the rest of the batch continue.
                stop = true;
            }

            if (stop) {
                release_grammar(seq);
                finish(seq);
                continue;
            }

            // The IGrammar contract, applied exactly as sensen's own
            // `sampleGuided` applies it: mask the disallowed logits to -inf,
            // then sample normally. Done here rather than by asking sensen for a
            // guided scheduler call because the batched path has no guided
            // variant -- and because masking in front of the SAME
            // `schedulerSample` keeps the unconstrained path byte-for-byte what
            // it was, which is what makes an A/B on this meaningful.
            if (seq.grammar != nullptr) {
                const std::vector<bool>& allowed = seq.grammar->allowedMask();
                if (allowed.size() == logits[i].size()) {
                    for (std::size_t t = 0; t < allowed.size(); ++t) {
                        if (!allowed[t]) logits[i][t] = -std::numeric_limits<float>::infinity();
                    }
                } else {
                    // A vocabulary/logit width mismatch would silently mask the
                    // wrong tokens. Refuse to guess which end is wrong.
                    logger::Logger::getInstance().error(
                        "mortgage assistant: grammar mask is {} wide but the logit vector is {} -- "
                        "constraint DROPPED for this sequence",
                        allowed.size(), logits[i].size());
                    release_grammar(seq);
                }
            }

            seq.next_token = pipeline_->schedulerSample(logits[i], params, seq.agent->getContext());
            still_active.push_back(std::move(seq));
        }
        active = std::move(still_active);
        return true;
    }

    /** Detokenises a finished sequence, completes its promise, and returns its
     * KV blocks to the pool so a waiting request can take the slot. */
    auto finish(Sequence& seq) -> void {
        InferenceOutcome outcome;
        try {
            outcome.text = pipeline_->schedulerDecode(seq.generated);
            outcome.ok = true;
        } catch (const std::exception& e) {
            outcome.ok = false;
            outcome.error = e.what();
        } catch (...) {
            outcome.ok = false;
            outcome.error = "unknown failure decoding the generated tokens";
        }
        seq.promise.set_value(std::move(outcome));
        pipeline_->schedulerEvict(*seq.agent);
    }

    /**
     * Builds the grammar pool once, on the owner thread, the first time a
     * sequence needs it.
     *
     * Lazy rather than eager because it materialises the whole id -> text
     * vocabulary: 151,936 `std::string`s for Qwen3, which every
     * `MortgageParamsGrammar` holds a copy of. One instance per concurrent slot
     * is allocated and then RE-USED across requests -- `reset()` + `prime()`
     * returns an instance to its start state, so a per-request construction
     * (and a per-request vocabulary copy) never happens.
     *
     * A failure here is not fatal. `grammar_pool_` stays empty, `arm_grammar`
     * finds no slot, and every sequence decodes exactly as it did before this
     * existed. Constrained decoding is an improvement to the output, not a
     * precondition for producing any -- and the verifier behind it is what
     * actually guarantees safety.
     */
    auto ensure_grammar_pool() -> void {
        if (grammar_pool_ready_) return;
        grammar_pool_ready_ = true;  // set first: a failure must not retry per token

        logger::Logger::getInstance().info(
            "mortgage assistant: MORTGAGE_GRAMMAR={} -- constrained decoding {}",
            env_string("MORTGAGE_GRAMMAR").value_or("(unset, defaults on)"),
            grammar_enabled_ ? "REQUESTED" : "OFF");
        if (!grammar_enabled_) return;

        auto schema = mg::Schema::build();
        if (!schema.has_value()) {
            logger::Logger::getInstance().error(
                "mortgage assistant: could not build the params grammar schema ({}) -- decoding "
                "UNCONSTRAINED. The verifier still gates every answer.",
                schema.error());
            return;
        }
        schema_ = std::move(*schema);

        try {
            const sensen::Tokenizer& tok = pipeline_->getTokenizer();
            const std::size_t vsz = tok.getVocabSize();
            std::vector<std::string> vocab_text(vsz);
            for (std::size_t id = 0; id < vsz; ++id) {
                vocab_text[id] = tok.decodeToken(static_cast<std::uint32_t>(id));
            }
            const std::optional<std::uint32_t> eos = tok.getSpecialTokens().eos;

            grammar_pool_.reserve(max_concurrent_);
            for (std::size_t i = 0; i < max_concurrent_; ++i) {
                // The last one moves the table; the earlier ones must copy,
                // because each instance masks against its own state.
                grammar_pool_.push_back(std::make_unique<mg::MortgageParamsGrammar>(
                    *schema_, (i + 1 == max_concurrent_) ? std::move(vocab_text) : vocab_text, eos));
            }
            grammar_free_.assign(max_concurrent_, true);
            logger::Logger::getInstance().info(
                "mortgage assistant: constrained decoding armed on '{}' -- {} grammar slots over a "
                "{}-entry vocabulary",
                schema_->activation_marker(), max_concurrent_, vsz);
        } catch (const std::exception& e) {
            grammar_pool_.clear();
            logger::Logger::getInstance().error(
                "mortgage assistant: could not build the grammar pool ({}) -- decoding "
                "UNCONSTRAINED",
                e.what());
        }
    }

    /**
     * Switches the constraint on for one sequence, at the moment its emitted
     * text ends with the activation marker.
     *
     * Deliberately NOT at the first token. Qwen3 emits a `<think>` block on
     * every response, and a legitimate answer may be a clarifying question
     * rather than params at all -- a grammar forced from token zero would
     * forbid both, and would turn the 46-in-600 clarification rows into
     * malformed params. `prime()` replays the marker into a fresh automaton so
     * arming late is not a desync.
     */
    auto arm_grammar(Sequence& seq) -> void {
        if (seq.grammar != nullptr || grammar_pool_.empty()) return;
        const std::string_view marker = schema_->activation_marker();
        if (marker.empty()) return;

        // CONTAINS, not ends-with, and then replay everything from the marker
        // onward. A token boundary does not respect the marker: the token that
        // completes `<params>` also carries the `{"` after it, so the emitted
        // text never ENDS with the marker at any single step -- it goes from
        // "...<param" straight to "...<params>{\"". An ends_with test therefore
        // never fires, which is exactly how this failed the first time: the
        // pool built, the log said armed, and not one sequence was constrained.
        const auto at = seq.emitted.find(marker);
        if (at == std::string::npos) return;
        const std::string_view replay = std::string_view{seq.emitted}.substr(at);

        for (std::size_t s = 0; s < grammar_pool_.size(); ++s) {
            if (!grammar_free_[s]) continue;
            auto* g = grammar_pool_[s].get();
            g->reset();
            if (!g->prime(replay)) {
                // The automaton rejected its own activation marker. That is a
                // contradiction rather than a bad response, so say so once and
                // leave this sequence unconstrained instead of masking against
                // a state that does not correspond to what was emitted.
                logger::Logger::getInstance().error(
                    "mortgage assistant: grammar refused to prime on the emitted prefix '{}' -- "
                    "this sequence decodes UNCONSTRAINED",
                    replay);
                return;
            }
            grammar_free_[s] = false;
            seq.grammar = g;
            seq.grammar_slot = s;
            return;
        }
        // Every slot busy. Possible only if the pool were smaller than
        // max_concurrent_; it is not, so this is unreachable rather than a
        // capacity policy. Unconstrained is the honest fallback either way.
    }

    /** Returns a sequence's grammar slot to the pool. Safe to call on a
     * sequence that never armed one. */
    auto release_grammar(Sequence& seq) -> void {
        if (seq.grammar_slot != kNoSlot && seq.grammar_slot < grammar_free_.size()) {
            grammar_free_[seq.grammar_slot] = true;
        }
        seq.grammar = nullptr;
        seq.grammar_slot = kNoSlot;
    }

    std::unique_ptr<sensen::LLMPipeline> pipeline_;

    // Constrained decoding. All of this is touched ONLY by the owner thread
    // (`run` and the functions it calls), which is the same reason the rest of
    // the decode state needs no lock.
    bool grammar_enabled_ = env_string("MORTGAGE_GRAMMAR").value_or("on") != "off";
    bool grammar_pool_ready_ = false;
    std::optional<mg::Schema> schema_;
    std::vector<std::unique_ptr<mg::MortgageParamsGrammar>> grammar_pool_;
    std::vector<bool> grammar_free_;

    // Declared LAST: member destruction order is the reverse of declaration
    // order, so `worker_`'s destructor (request_stop + join) runs BEFORE
    // pipeline_ and the queue in the base are torn down, guaranteeing run() has
    // fully exited -- and therefore touched none of them -- by the time they are
    // destroyed.
    std::jthread worker_;
};

// ---------------------------------------------------------------------------
// The process-wide worker
// ---------------------------------------------------------------------------

/**
 * Owns this service's single backend, constructed once at registration.
 *
 * Every environment variable it reads is prefixed `MORTGAGE_` so that tuning
 * this assistant cannot move the strategy assistant, and vice versa. In
 * particular `MORTGAGE_MODEL_PATH` deliberately does NOT fall back to
 * `MODEL_PATH`: a deployment that sets only `MODEL_PATH` has provided the
 * STRATEGY model, and quietly loading it here would put a model trained on
 * option strategies behind a mortgage contract -- which would answer, in the
 * wrong schema, and every answer would be refused by the validator downstream
 * for reasons that describe the parameters rather than the mistake.
 */
class MortgageAssistantWorker {
  public:
    [[nodiscard]] static auto instance() -> MortgageAssistantWorker& {
        static MortgageAssistantWorker worker;
        return worker;
    }

    /** True iff the backend initialised successfully at process start.
     * Immutable after construction, so no synchronization is needed. */
    [[nodiscard]] auto available() const noexcept -> bool { return backend_ != nullptr; }

    [[nodiscard]] auto submit(std::string prompt) -> std::optional<InferenceOutcome> {
        if (backend_ == nullptr) {
            // Defense in depth: the RPC handler is expected to check
            // available() first, but if this is ever reached anyway there is no
            // owner thread to fulfil a queued job's promise -- returning a
            // populated failure here, rather than enqueueing, is what stands
            // between this and a permanent hang.
            return InferenceOutcome{.ok = false, .text = {}, .error = "model not loaded"};
        }
        return backend_->submit(std::move(prompt));
    }

    MortgageAssistantWorker(const MortgageAssistantWorker&) = delete;
    auto operator=(const MortgageAssistantWorker&) -> MortgageAssistantWorker& = delete;
    MortgageAssistantWorker(MortgageAssistantWorker&&) = delete;
    auto operator=(MortgageAssistantWorker&&) -> MortgageAssistantWorker& = delete;
    ~MortgageAssistantWorker() = default;

  private:
    MortgageAssistantWorker() {
        const auto path = env_string("MORTGAGE_MODEL_PATH");
        if (!path.has_value()) {
            logger::Logger::getInstance().warn(
                "MORTGAGE_MODEL_PATH is not set -- the mortgage assistant will return a Refusal on "
                "every call. The calculator, finance and strategy-assistant services are "
                "unaffected.");
            return;
        }

        // How many threads the ENGINE may fan a single forward pass across
        // internally. A different axis from how many requests are in flight:
        // one owner thread drives the batch, and this is how wide each of its
        // steps is allowed to go. Defaults lower than the strategy assistant's
        // is not the point -- it defaults to the SAME 4, and the two are
        // separately settable precisely because a host serving both is now
        // running two engines that can each be told how much of the machine to
        // take.
        const int threads = env_positive_int("MORTGAGE_ASSISTANT_INFERENCE_THREADS", 4);

        // How many requests may decode simultaneously, and how many may wait
        // for a slot. Modest on purpose: batching raises aggregate throughput
        // and raises per-request tail latency with it, and this is an
        // interactive assistant where the person waiting on one answer cares
        // more about their own latency than the server's token rate.
        const auto max_concurrent =
            static_cast<std::size_t>(env_positive_int("MORTGAGE_ASSISTANT_MAX_CONCURRENT", 4));
        const auto queue_depth =
            static_cast<std::size_t>(env_positive_int("MORTGAGE_ASSISTANT_QUEUE_DEPTH", 8));

        // Per-sequence context window, capped rather than allocated: production
        // runs the PAGED KV cache, which commits blocks on demand, so a short
        // exchange's resident KV tracks its real length rather than this number.
        // Nothing this service sends approaches it -- one system turn, one user
        // turn, optionally one more, plus kMaxNewTokens.
        const auto kv_max_seq_len =
            static_cast<std::size_t>(env_positive_int("MORTGAGE_ASSISTANT_CONTEXT_TOKENS", 4096));

        backend_ =
            SensenBackend::create(*path, max_concurrent, queue_depth, kv_max_seq_len, threads);
        if (backend_ == nullptr) {
            logger::Logger::getInstance().error(
                "The mortgage assistant backend failed to initialise from MORTGAGE_MODEL_PATH ({}) "
                "-- it will return a Refusal on every call. The calculator, finance and "
                "strategy-assistant services are unaffected.",
                *path);
            return;
        }

        logger::Logger::getInstance().info(
            "Mortgage assistant ready: backend={} model={} inference_threads={} max_concurrent={} "
            "queue_depth={} context_tokens={}",
            SensenBackend::name(), *path, threads, max_concurrent, queue_depth, kv_max_seq_len);
    }

    std::unique_ptr<QueuedBackend> backend_;
};

// ---------------------------------------------------------------------------
// The label space: 26 operations and their fields, mirrored from finance.proto
// ---------------------------------------------------------------------------
//
// THIS TABLE IS THE SERVING-SIDE HALF OF THE DATASET'S LABEL SPACE, AND THE TWO
// MUST NOT DRIFT.
//
// agent/dataset/build_mortgage_dataset.py derives what the model is TAUGHT to
// emit by parsing backend/proto/finance.proto directly -- its `parse_finance_
// proto()` reads every RPC name and every field of its request message out of
// the .proto text, selects the in-scope ones by the service block's own section
// banners (Time value of money / Mortgages, HELOC / Cash-flow analysis /
// Depreciation / Real estate) minus two rate-theory utilities nobody phrases as
// a chat message, and asserts that each generated `<params>` object's key set
// is EXACTLY that request message's field set. This table is the same 26
// operations and the same 160 fields, so what the model was taught to emit and
// what this service will accept are the same thing by construction rather than
// by coincidence.
//
// It was GENERATED from finance.proto by running that same parser, not typed by
// hand -- 160 field names and their types is well past the size where a
// transcription error hides. If finance.proto's in-scope surface changes, the
// dataset picks it up on its next run and this table must be regenerated the
// same way; a field added on one side only is exactly the silent
// misinterpretation that makes a service compile cleanly and answer wrongly.

/** How a field's JSON value must look, and how it is encoded into the
 * `map<string, string>` the contract carries. Named after the PROTO type in
 * finance.proto, because that is the authority: `Decimal` is a proto `string`
 * holding a sensen::BigDecimal, `Double` is a proto `double` sensen genuinely
 * computes in double. See proto/mortgage_assistant.proto's FinanceParams banner
 * and finance.proto's own numeric-contract banner. */
enum class Kind : std::uint8_t {
    Decimal,         // proto string / BigDecimal -- JSON string, passed through verbatim
    Int,             // proto int32 -- JSON integral number
    Double,          // proto double -- JSON number
    Bool,            // proto bool -- JSON true/false
    Enum,            // proto enum -- JSON string naming one of the enum's constants
    RepeatedDouble,  // proto repeated double -- JSON array of numbers
    RepeatedInt,     // proto repeated int32 -- JSON array of integral numbers
};

struct Field {
    std::string_view name;
    Kind kind;
    /** Non-empty iff `kind == Kind::Enum`: that enum's constants, spelled as
     * finance.proto spells them. */
    std::span<const std::string_view> enum_values;
};

struct Operation {
    /** The method name exactly as `service Finance` declares it. */
    std::string_view id;
    /** Its request message, carried for diagnostics -- a refusal that names
     * `RefinanceRequest` tells an operator where to look without them having to
     * map the method name to a message first. */
    std::string_view request_message;
    std::span<const Field> fields;
};

constexpr std::array<std::string_view, 2> kAnnuityTimingValues{{"END_OF_PERIOD", "BEGINNING_OF_PERIOD"}};
constexpr std::array<std::string_view, 2> kClosingCostTypeValues{{"PAID_IN_CASH", "ROLLED_INTO_LOAN"}};
constexpr std::array<std::string_view, 2> kComponentValues{{"INTEREST", "PRINCIPAL"}};
constexpr std::array<std::string_view, 4> kMethodValues{{"STRAIGHT_LINE", "SUM_OF_YEARS_DIGITS", "DECLINING_BALANCE", "MACRS"}};

constexpr std::array<Field, 6> kFields_ComputeAmortization{{
    {"loan_amount", Kind::Decimal, {}},
    {"annual_rate", Kind::Decimal, {}},
    {"term_months", Kind::Int, {}},
    {"monthly_overpayment", Kind::Decimal, {}},
    {"pmi_annual_rate", Kind::Decimal, {}},
    {"original_home_value", Kind::Decimal, {}},
}};

constexpr std::array<Field, 6> kFields_ComputeAmortizationBatch{{
    {"loan_amounts", Kind::RepeatedDouble, {}},
    {"annual_rates", Kind::RepeatedDouble, {}},
    {"term_months", Kind::RepeatedInt, {}},
    {"extra_payments", Kind::RepeatedDouble, {}},
    {"pmi_rates", Kind::RepeatedDouble, {}},
    {"home_values", Kind::RepeatedDouble, {}},
}};

constexpr std::array<Field, 7> kFields_ComputeCumulative{{
    {"component", Kind::Enum, kComponentValues},
    {"rate", Kind::Double, {}},
    {"periods", Kind::Int, {}},
    {"present_value", Kind::Double, {}},
    {"start_period", Kind::Int, {}},
    {"end_period", Kind::Int, {}},
    {"timing", Kind::Enum, kAnnuityTimingValues},
}};

constexpr std::array<Field, 8> kFields_ComputeDepreciation{{
    {"method", Kind::Enum, kMethodValues},
    {"cost", Kind::Double, {}},
    {"salvage", Kind::Double, {}},
    {"life", Kind::Double, {}},
    {"period", Kind::Double, {}},
    {"factor", Kind::Double, {}},
    {"recovery_period", Kind::Int, {}},
    {"year", Kind::Int, {}},
}};

constexpr std::array<Field, 7> kFields_ComputeDetailedAmortization{{
    {"loan_amount", Kind::Decimal, {}},
    {"annual_rate", Kind::Decimal, {}},
    {"term_months", Kind::Int, {}},
    {"monthly_overpayment", Kind::Decimal, {}},
    {"pmi_annual_rate", Kind::Decimal, {}},
    {"original_home_value", Kind::Decimal, {}},
    {"annual_tax_rate", Kind::Decimal, {}},
}};

constexpr std::array<Field, 5> kFields_ComputeFutureValue{{
    {"rate", Kind::Decimal, {}},
    {"periods", Kind::Int, {}},
    {"payment", Kind::Decimal, {}},
    {"present_value", Kind::Decimal, {}},
    {"timing", Kind::Enum, kAnnuityTimingValues},
}};

constexpr std::array<Field, 6> kFields_ComputeFutureValueDetailed{{
    {"annual_rate", Kind::Decimal, {}},
    {"years", Kind::Int, {}},
    {"annual_contribution", Kind::Decimal, {}},
    {"current_principal", Kind::Decimal, {}},
    {"annual_inflation_rate", Kind::Decimal, {}},
    {"compound_frequency", Kind::Int, {}},
}};

constexpr std::array<Field, 7> kFields_ComputeHeloc{{
    {"home_value", Kind::Decimal, {}},
    {"current_mortgage_balance", Kind::Decimal, {}},
    {"max_ltv_rate", Kind::Decimal, {}},
    {"drawn_amount", Kind::Decimal, {}},
    {"annual_rate", Kind::Decimal, {}},
    {"repayment_term_years", Kind::Int, {}},
    {"payments_per_year", Kind::Int, {}},
}};

constexpr std::array<Field, 7> kFields_ComputeHomeFutureValue{{
    {"current_property_value", Kind::Decimal, {}},
    {"annual_appreciation_rate", Kind::Decimal, {}},
    {"current_loan_balance", Kind::Decimal, {}},
    {"annual_mortgage_rate", Kind::Decimal, {}},
    {"current_monthly_payment", Kind::Decimal, {}},
    {"target_years", Kind::Int, {}},
    {"payments_per_year", Kind::Int, {}},
}};

constexpr std::array<Field, 14> kFields_ComputeHomeNpv{{
    {"property_price", Kind::Decimal, {}},
    {"down_payment", Kind::Decimal, {}},
    {"closing_costs_buy", Kind::Decimal, {}},
    {"loan_amount", Kind::Decimal, {}},
    {"loan_annual_rate", Kind::Decimal, {}},
    {"loan_term_years", Kind::Int, {}},
    {"monthly_taxes_ins_hoa", Kind::Decimal, {}},
    {"monthly_maintenance", Kind::Decimal, {}},
    {"annual_appreciation_rate", Kind::Decimal, {}},
    {"selling_closing_cost_percent", Kind::Decimal, {}},
    {"monthly_rent_saved", Kind::Decimal, {}},
    {"annual_rent_increase", Kind::Decimal, {}},
    {"annual_discount_rate", Kind::Decimal, {}},
    {"holding_period_years", Kind::Int, {}},
}};

constexpr std::array<Field, 6> kFields_ComputeInterestPayment{{
    {"rate", Kind::Decimal, {}},
    {"period", Kind::Int, {}},
    {"periods", Kind::Int, {}},
    {"present_value", Kind::Decimal, {}},
    {"future_value", Kind::Decimal, {}},
    {"timing", Kind::Enum, kAnnuityTimingValues},
}};

constexpr std::array<Field, 2> kFields_ComputeIrr{{
    {"values", Kind::RepeatedDouble, {}},
    {"guess", Kind::Double, {}},
}};

constexpr std::array<Field, 6> kFields_ComputeMortgageRecast{{
    {"current_loan_balance", Kind::Decimal, {}},
    {"current_monthly_payment", Kind::Decimal, {}},
    {"lump_sum_payment", Kind::Decimal, {}},
    {"annual_rate", Kind::Decimal, {}},
    {"remaining_months", Kind::Int, {}},
    {"payments_per_year", Kind::Int, {}},
}};

constexpr std::array<Field, 2> kFields_ComputeNpv{{
    {"rate", Kind::Double, {}},
    {"values", Kind::RepeatedDouble, {}},
}};

constexpr std::array<Field, 3> kFields_ComputePaybackPeriod{{
    {"values", Kind::RepeatedDouble, {}},
    {"discounted", Kind::Bool, {}},
    {"rate", Kind::Double, {}},
}};

constexpr std::array<Field, 5> kFields_ComputePayment{{
    {"rate", Kind::Decimal, {}},
    {"periods", Kind::Int, {}},
    {"present_value", Kind::Decimal, {}},
    {"future_value", Kind::Decimal, {}},
    {"timing", Kind::Enum, kAnnuityTimingValues},
}};

constexpr std::array<Field, 5> kFields_ComputePayoffTiming{{
    {"current_loan_balance", Kind::Decimal, {}},
    {"annual_rate", Kind::Decimal, {}},
    {"current_monthly_payment", Kind::Decimal, {}},
    {"extra_monthly_payment", Kind::Decimal, {}},
    {"payments_per_year", Kind::Int, {}},
}};

constexpr std::array<Field, 5> kFields_ComputePeriods{{
    {"rate", Kind::Decimal, {}},
    {"payment", Kind::Decimal, {}},
    {"present_value", Kind::Decimal, {}},
    {"future_value", Kind::Decimal, {}},
    {"timing", Kind::Enum, kAnnuityTimingValues},
}};

constexpr std::array<Field, 5> kFields_ComputePresentValue{{
    {"rate", Kind::Decimal, {}},
    {"periods", Kind::Int, {}},
    {"payment", Kind::Decimal, {}},
    {"future_value", Kind::Decimal, {}},
    {"timing", Kind::Enum, kAnnuityTimingValues},
}};

constexpr std::array<Field, 6> kFields_ComputePrincipalPayment{{
    {"rate", Kind::Decimal, {}},
    {"period", Kind::Int, {}},
    {"periods", Kind::Int, {}},
    {"present_value", Kind::Decimal, {}},
    {"future_value", Kind::Decimal, {}},
    {"timing", Kind::Enum, kAnnuityTimingValues},
}};

constexpr std::array<Field, 6> kFields_ComputeRate{{
    {"periods", Kind::Int, {}},
    {"payment", Kind::Decimal, {}},
    {"present_value", Kind::Decimal, {}},
    {"future_value", Kind::Decimal, {}},
    {"timing", Kind::Enum, kAnnuityTimingValues},
    {"guess", Kind::Decimal, {}},
}};

constexpr std::array<Field, 14> kFields_ComputeRefinance{{
    {"current_loan_balance", Kind::Decimal, {}},
    {"current_monthly_payment", Kind::Decimal, {}},
    {"current_annual_rate", Kind::Decimal, {}},
    {"current_remaining_months", Kind::Int, {}},
    {"property_value", Kind::Decimal, {}},
    {"new_annual_rate", Kind::Decimal, {}},
    {"new_term_years", Kind::Int, {}},
    {"closing_costs", Kind::Decimal, {}},
    {"closing_cost_type", Kind::Enum, kClosingCostTypeValues},
    {"cash_out_amount", Kind::Decimal, {}},
    {"current_pmi_monthly", Kind::Decimal, {}},
    {"new_pmi_monthly", Kind::Decimal, {}},
    {"pmi_drop_off_ltv", Kind::Decimal, {}},
    {"payments_per_year", Kind::Int, {}},
}};

constexpr std::array<Field, 8> kFields_ComputeRentVsBuy{{
    {"property_price", Kind::Decimal, {}},
    {"down_payment", Kind::Decimal, {}},
    {"monthly_piti_and_maintenance", Kind::Decimal, {}},
    {"annual_home_appreciation", Kind::Decimal, {}},
    {"current_monthly_rent", Kind::Decimal, {}},
    {"annual_rent_increase", Kind::Decimal, {}},
    {"annual_investment_return", Kind::Decimal, {}},
    {"years", Kind::Int, {}},
}};

constexpr std::array<Field, 6> kFields_ComputeRentalRoi{{
    {"property_value", Kind::Decimal, {}},
    {"total_cash_invested", Kind::Decimal, {}},
    {"periodic_gross_rent", Kind::Decimal, {}},
    {"periodic_operating_expenses", Kind::Decimal, {}},
    {"periodic_mortgage_payment", Kind::Decimal, {}},
    {"periods_per_year", Kind::Int, {}},
}};

constexpr std::array<Field, 4> kFields_ComputeXirr{{
    {"rate", Kind::Double, {}},
    {"values", Kind::RepeatedDouble, {}},
    {"dates", Kind::RepeatedDouble, {}},
    {"guess", Kind::Double, {}},
}};

constexpr std::array<Field, 4> kFields_ComputeXnpv{{
    {"rate", Kind::Double, {}},
    {"values", Kind::RepeatedDouble, {}},
    {"dates", Kind::RepeatedDouble, {}},
    {"guess", Kind::Double, {}},
}};

constexpr std::array<Operation, 26> kOperations{{
    {"ComputeAmortization", "AmortizationRequest", kFields_ComputeAmortization},
    {"ComputeAmortizationBatch", "AmortizationBatchRequest", kFields_ComputeAmortizationBatch},
    {"ComputeCumulative", "CumulativeRequest", kFields_ComputeCumulative},
    {"ComputeDepreciation", "DepreciationRequest", kFields_ComputeDepreciation},
    {"ComputeDetailedAmortization", "DetailedAmortizationRequest", kFields_ComputeDetailedAmortization},
    {"ComputeFutureValue", "FutureValueRequest", kFields_ComputeFutureValue},
    {"ComputeFutureValueDetailed", "FutureValueDetailedRequest", kFields_ComputeFutureValueDetailed},
    {"ComputeHeloc", "HelocRequest", kFields_ComputeHeloc},
    {"ComputeHomeFutureValue", "HomeFutureValueRequest", kFields_ComputeHomeFutureValue},
    {"ComputeHomeNpv", "HomeNpvRequest", kFields_ComputeHomeNpv},
    {"ComputeInterestPayment", "PeriodPaymentRequest", kFields_ComputeInterestPayment},
    {"ComputeIrr", "IrrRequest", kFields_ComputeIrr},
    {"ComputeMortgageRecast", "MortgageRecastRequest", kFields_ComputeMortgageRecast},
    {"ComputeNpv", "NpvRequest", kFields_ComputeNpv},
    {"ComputePaybackPeriod", "PaybackRequest", kFields_ComputePaybackPeriod},
    {"ComputePayment", "PaymentRequest", kFields_ComputePayment},
    {"ComputePayoffTiming", "PayoffTimingRequest", kFields_ComputePayoffTiming},
    {"ComputePeriods", "PeriodsRequest", kFields_ComputePeriods},
    {"ComputePresentValue", "PresentValueRequest", kFields_ComputePresentValue},
    {"ComputePrincipalPayment", "PeriodPaymentRequest", kFields_ComputePrincipalPayment},
    {"ComputeRate", "RateRequest", kFields_ComputeRate},
    {"ComputeRefinance", "RefinanceRequest", kFields_ComputeRefinance},
    {"ComputeRentVsBuy", "RentVsBuyRequest", kFields_ComputeRentVsBuy},
    {"ComputeRentalRoi", "RentalRoiRequest", kFields_ComputeRentalRoi},
    {"ComputeXirr", "DatedCashFlowRequest", kFields_ComputeXirr},
    {"ComputeXnpv", "DatedCashFlowRequest", kFields_ComputeXnpv},
}};

[[nodiscard]] auto find_operation(std::string_view id) noexcept -> const Operation* {
    const auto it = std::ranges::find(kOperations, id, &Operation::id);
    return it == kOperations.end() ? nullptr : &*it;
}

[[nodiscard]] auto find_field(const Operation& op, std::string_view name) noexcept -> const Field* {
    const auto it = std::ranges::find(op.fields, name, &Field::name);
    return it == op.fields.end() ? nullptr : &*it;
}

// ---------------------------------------------------------------------------
// Output interpretation helpers
// ---------------------------------------------------------------------------

[[nodiscard]] auto trim(std::string_view s) -> std::string {
    std::size_t start = 0;
    std::size_t end = s.size();
    while (start < end && std::isspace(static_cast<unsigned char>(s[start])) != 0) ++start;
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])) != 0) --end;
    return std::string{s.substr(start, end - start)};
}

/**
 * Removes a leading `<think>...</think>` block, returning whatever follows.
 *
 * QWEN3 EMITS ONE ON EVERY RESPONSE, INCLUDING THE CORRECT ONES. On this
 * fine-tune a correct answer looks like
 * `<think>\n\n</think>\n\n<params>{...}</params>` -- the block is present and
 * EMPTY, and the emptiness is the signal that the training system prompt took.
 * Treating the block's PRESENCE as a failure would reject every valid answer;
 * that exact mistake shipped once on the sibling service and rejected every
 * correct parse the model produced.
 *
 * An unterminated `<think>` usually means the model hit the token ceiling
 * mid-reasoning, leaving nothing to interpret -- everything is dropped and the
 * caller falls through to its refusal path rather than showing a reasoning
 * trace. But "unterminated" does not always mean "truncated": the sibling model
 * was measured emitting `<think>\n\n<params>{...}</params>` -- a COMPLETE
 * answer simply missing `</think>` -- and dropping it turned a correct parse
 * into a refusal. So an unterminated block is dropped only up to a complete
 * `<params>` block, if one follows. With no such block the all-or-nothing
 * behaviour stands, because then there really is nothing safe to show.
 */
[[nodiscard]] auto strip_think_block(std::string_view text) -> std::string {
    constexpr std::string_view kOpen = "<think>";
    constexpr std::string_view kClose = "</think>";

    const auto open = text.find(kOpen);
    if (open == std::string_view::npos) return std::string{text};

    const auto close = text.find(kClose, open + kOpen.size());
    if (close == std::string_view::npos) {
        constexpr std::string_view kParamsOpen = "<params>";
        constexpr std::string_view kParamsClose = "</params>";
        const auto p_open = text.find(kParamsOpen, open + kOpen.size());
        if (p_open == std::string_view::npos) return {};
        if (text.find(kParamsClose, p_open + kParamsOpen.size()) == std::string_view::npos) {
            return {};
        }
        std::string rescued{text.substr(0, open)};
        rescued += text.substr(p_open);
        return rescued;
    }

    std::string out{text.substr(0, open)};
    out += text.substr(close + kClose.size());
    return out;
}

/** Extracts the text between `<params>` and `</params>`, if both are present in
 * that order. A missing or unclosed tag (e.g. the model was cut off by
 * kMaxNewTokens mid-object) yields nullopt, which the caller treats as "this was
 * not a params response" rather than attempting to parse a fragment. */
[[nodiscard]] auto extract_params_block(std::string_view text) -> std::optional<std::string_view> {
    constexpr std::string_view kOpen = "<params>";
    constexpr std::string_view kClose = "</params>";
    const auto open_pos = text.find(kOpen);
    if (open_pos == std::string_view::npos) return std::nullopt;
    const auto content_start = open_pos + kOpen.size();
    const auto close_pos = text.find(kClose, content_start);
    if (close_pos == std::string_view::npos) return std::nullopt;
    return text.substr(content_start, close_pos - content_start);
}

auto populate_refusal(::mortgage::assistant::ParseResponse& response,
                      ::mortgage::assistant::Refusal_Reason reason, std::string message) -> void {
    auto* refusal = response.mutable_refusal();
    refusal->set_reason(reason);
    refusal->set_message(std::move(message));
}

/** Mirrors `populate_refusal` for the other successful-but-not-params outcome.
 * Its own function, rather than inlined at each call site, for the same reason:
 * one place that knows which field of the `oneof` a clarifying question belongs
 * in. */
auto populate_clarification(::mortgage::assistant::ParseResponse& response, std::string question)
    -> void {
    response.mutable_clarification()->set_question(std::move(question));
}

// ---------------------------------------------------------------------------
// GP-ARA mandatory verification stage
// ---------------------------------------------------------------------------
//
// `mortgage_verification.cppm` is the sibling of `assistant_verification.cppm`,
// and it is wired in here the same way and at the same point in the pipeline the
// strategy assistant wires its own: after every per-field check has run, and
// BEFORE anything reaches `response.mutable_params()`. Everything above that
// point inspects the model's output against ITSELF -- is this a real operation,
// are these its declared fields, does each value parse and sit inside a sane
// magnitude. None of it can ask the one question that matters, because none of
// it can see the utterance:
//
//   `current_monthly_payment: "5379.00"` when the user said `5378.63` is a real
//   operation, in a real field of that operation, parsing as a real decimal,
//   inside every bound this file enforces -- and it prices a DIFFERENT LOAN.
//
// The validator's own test proves that gap directly: the structural gates alone
// return Proven on exactly that row. Only `verify_mortgage_output`, which takes
// the user's own words as an explicit argument, can falsify it.

namespace mv = ::mortgage_calculator::assistant::verify;

/**
 * Collapses the verifier's twelve reason codes onto this contract's four.
 *
 * The verifier is deliberately finer-grained than the wire enum (see its own
 * `ReasonCode` doc comment), and collapsing is expected -- but the collapse must
 * not lose the DISTINCTION the proto's Reason values draw. It does not:
 *
 *   UnknownOperation      -> UNSUPPORTED_OPERATION. The model named a method
 *                            that is not one of the 26, which is precisely what
 *                            that value is for.
 *   everything else       -> INVALID_PARAMETERS. The proto's own doc comment for
 *                            it reads "the guard that stands between a
 *                            hallucinated figure and an exact wrong answer
 *                            computed from it" -- which is `UngroundedValue`
 *                            stated in the proto's words, and covers the field,
 *                            shape, enum, magnitude and malformed-number codes
 *                            it already enumerates.
 *
 * There is no OUT_OF_SCOPE branch: every code below reaches this function with a
 * params block in hand, so the request WAS about one of these calculations; what
 * failed is the answer, not the question. And no MODEL_UNAVAILABLE branch: the
 * model answered, it just answered something that could not be verified.
 *
 * `None` only ever accompanies `Outcome::Proven`, which this function is never
 * called for, and `NoParamsEmitted` is unreachable from the one call site
 * (`params_emitted` is set true there). Both still land on INVALID_PARAMETERS
 * rather than REASON_UNSPECIFIED: reaching either would be this file's own bug,
 * and a fail-closed refusal is the right answer to a bug on the serving path.
 */
[[nodiscard]] auto map_verification_reason(mv::ReasonCode reason)
    -> ::mortgage::assistant::Refusal_Reason {
    switch (reason) {
        case mv::ReasonCode::UnknownOperation:
            return ::mortgage::assistant::Refusal::UNSUPPORTED_OPERATION;
        case mv::ReasonCode::NoParamsEmitted:
        case mv::ReasonCode::UnknownField:
        case mv::ReasonCode::MissingField:
        case mv::ReasonCode::DuplicateField:
        case mv::ReasonCode::ShapeMismatch:
        case mv::ReasonCode::InvalidEnumValue:
        case mv::ReasonCode::MalformedNumber:
        case mv::ReasonCode::UngroundedValue:
        case mv::ReasonCode::OutOfRange:
        case mv::ReasonCode::Unclassified:
        case mv::ReasonCode::None:
            return ::mortgage::assistant::Refusal::INVALID_PARAMETERS;
    }
    return ::mortgage::assistant::Refusal::INVALID_PARAMETERS;
}

/**
 * Splits the `[a,b,c]` wire encoding of a repeated field back into its elements.
 *
 * The verifier's `EmittedField` carries one entry per element so that a scalar
 * and a series share one shape, while this service's map value is the joined
 * text. Splitting the ALREADY-ENCODED string rather than re-walking the JSON is
 * deliberate: what gets verified is then byte-for-byte what would have gone onto
 * the wire, so there is no second encoding path that could differ from the one
 * the caller receives. Elements are `std::to_string`/`std::to_chars` output and
 * so never contain a comma, which is what makes the split exact rather than a
 * parse.
 */
[[nodiscard]] auto split_array_encoding(std::string_view encoded) -> std::vector<std::string> {
    if (encoded.size() >= 2 && encoded.front() == '[' && encoded.back() == ']') {
        encoded = encoded.substr(1, encoded.size() - 2);
    }
    std::vector<std::string> out;
    if (encoded.empty()) return out;
    std::size_t start = 0;
    while (true) {
        const auto comma = encoded.find(',', start);
        if (comma == std::string_view::npos) {
            out.emplace_back(encoded.substr(start));
            break;
        }
        out.emplace_back(encoded.substr(start, comma - start));
        start = comma + 1;
    }
    return out;
}

/** The text G3 grounds against: the user's own words, exactly as `build_prompt`
 * handed them to the model, and in the same order. A figure the user supplied in
 * answer to this service's clarifying question is as much their own word as one
 * they supplied first time, so `prior_clarification` is concatenated rather than
 * dropped -- dropping it would refuse "37 years" for the one reason the verifier
 * must never refuse anything: that nobody showed it the utterance. */
[[nodiscard]] auto grounding_text(std::string_view utterance, std::string_view prior_clarification)
    -> std::string {
    std::string text{utterance};
    if (!prior_clarification.empty()) {
        text += '\n';
        text += prior_clarification;
    }
    return text;
}

// ---------------------------------------------------------------------------
// Value validation and encoding
// ---------------------------------------------------------------------------

/** fastjson stores an ordinary JSON number as a double and an over-wide one as
 * its extended 128-bit type; both are numbers as far as this contract is
 * concerned, and `as_float64`/`as_int64` read either. Asking only `is_number()`
 * would refuse a perfectly ordinary figure purely for how the parser chose to
 * store it. */
[[nodiscard]] auto is_numeric(const fastjson::json_value& v) noexcept -> bool {
    return v.is_number() || v.is_number_128();
}

/** Shortest round-trip decimal text for a double.
 *
 * `std::to_chars`'s no-format overload is specified to produce the shortest
 * representation that reads back as the SAME double -- so encoding a value into
 * the map and decoding it on the other side is lossless. Fixed-precision
 * formatting would not be: `%f` on 0.0385 either pads it with zeros or, at low
 * precision, silently changes it. */
[[nodiscard]] auto shortest_double_text(double v) -> std::string {
    std::array<char, 32> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), v);
    if (result.ec != std::errc{}) return {};
    return std::string{buffer.data(), result.ptr};
}

/**
 * Whether a string is a decimal literal sensen's BigDecimal will accept.
 *
 * Deliberately strict and deliberately SHAPE-ONLY. It admits an optional sign,
 * digits, and at most one fractional part -- which is exactly what the model was
 * trained to emit (`money_str`/`rate_str` in build_mortgage_dataset.py format
 * with `%.2f` and `%.6f`) -- and it does NOT admit exponent notation, currency
 * symbols, thousands separators, or anything else. It says nothing about whether
 * the figure is PLAUSIBLE; that is `kMaxAbsMagnitude`'s job below, checked
 * separately so the two failures can be told apart in the refusal message.
 */
[[nodiscard]] auto is_decimal_literal(std::string_view s) noexcept -> bool {
    if (s.empty() || s.size() > kMaxDecimalTextLength) return false;
    std::size_t i = 0;
    if (s[i] == '-' || s[i] == '+') ++i;
    std::size_t digits = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        ++i;
        ++digits;
    }
    if (i < s.size() && s[i] == '.') {
        ++i;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            ++i;
            ++digits;
        }
    }
    return i == s.size() && digits > 0;
}

/** A JSON number that is finite, within the hallucination guard, and -- when
 * `integral` -- actually a whole number inside int32's range. proto3 has no
 * int64 here: every integer field in these 26 requests is `int32`, so a value
 * outside that range would be silently truncated by the caller building the
 * Finance request. */
[[nodiscard]] auto number_is_sane(double v, bool integral) noexcept -> bool {
    if (!std::isfinite(v)) return false;
    if (std::abs(v) > kMaxAbsMagnitude) return false;
    if (!integral) return true;
    if (v != std::trunc(v)) return false;
    return v >= static_cast<double>(std::numeric_limits<std::int32_t>::min()) &&
           v <= static_cast<double>(std::numeric_limits<std::int32_t>::max());
}

/**
 * Validates one field's JSON value against its declared Kind and, on success,
 * writes its wire encoding into `out`.
 *
 * Returns an empty optional on success, or the human-readable reason it was
 * rejected. Returning the reason rather than a bool is what lets the refusal
 * name the actual problem ("term_months: expected a whole number") instead of a
 * generic "invalid parameters" the operator then has to reproduce to
 * understand.
 */
[[nodiscard]] auto validate_and_encode(const Field& field, const fastjson::json_value& value,
                                       std::string& out) -> std::optional<std::string> {
    switch (field.kind) {
        case Kind::Decimal: {
            if (!value.is_string()) {
                return std::string{field.name} +
                       ": expected a decimal STRING (this field is a BigDecimal on the Finance "
                       "service; a JSON number would already have lost precision)";
            }
            const std::string text{value.as_string()};
            if (!is_decimal_literal(text)) {
                return std::string{field.name} + ": \"" + text + "\" is not a plain decimal value";
            }
            // Re-read it as a double for the magnitude check only. The value
            // written to the wire is the ORIGINAL text, never this parse, so
            // no precision is lost by looking at it -- the double is used to
            // answer "is this figure absurd", not to represent the figure.
            double magnitude = 0.0;
            if (std::from_chars(text.data(), text.data() + text.size(), magnitude).ec ==
                    std::errc{} &&
                !number_is_sane(magnitude, /*integral=*/false)) {
                return std::string{field.name} + ": " + text + " is outside any plausible range";
            }
            out = text;
            return std::nullopt;
        }
        case Kind::Int: {
            if (!is_numeric(value)) return std::string{field.name} + ": expected a whole number";
            const double raw = value.as_float64();
            if (!number_is_sane(raw, /*integral=*/true)) {
                return std::string{field.name} +
                       ": expected a whole number within the int32 range this field has on the "
                       "Finance service";
            }
            out = std::to_string(value.as_int64());
            return std::nullopt;
        }
        case Kind::Double: {
            if (!is_numeric(value)) return std::string{field.name} + ": expected a number";
            const double raw = value.as_float64();
            if (!number_is_sane(raw, /*integral=*/false)) {
                return std::string{field.name} + ": the value is not a finite, plausible number";
            }
            out = shortest_double_text(raw);
            return std::nullopt;
        }
        case Kind::Bool: {
            if (!value.is_boolean()) return std::string{field.name} + ": expected true or false";
            out = value.as_boolean() ? "true" : "false";
            return std::nullopt;
        }
        case Kind::Enum: {
            if (!value.is_string()) {
                return std::string{field.name} + ": expected one of this enum's constant names";
            }
            const std::string text{value.as_string()};
            if (std::ranges::find(field.enum_values, text) == field.enum_values.end()) {
                std::string allowed;
                for (const auto v : field.enum_values) {
                    if (!allowed.empty()) allowed += ", ";
                    allowed += v;
                }
                return std::string{field.name} + ": \"" + text + "\" is not one of {" + allowed +
                       "}";
            }
            out = text;
            return std::nullopt;
        }
        case Kind::RepeatedDouble:
        case Kind::RepeatedInt: {
            const bool integral = field.kind == Kind::RepeatedInt;
            if (!value.is_array()) return std::string{field.name} + ": expected an array";
            const std::size_t count = value.size();
            if (count == 0) {
                return std::string{field.name} +
                       ": the array is empty, so the calculation has no inputs";
            }
            if (count > kMaxArrayLength) {
                return std::string{field.name} + ": the array has " + std::to_string(count) +
                       " entries, beyond this service's limit of " +
                       std::to_string(kMaxArrayLength);
            }
            std::string encoded = "[";
            for (std::size_t i = 0; i < count; ++i) {
                const auto& element = value[i];
                if (!is_numeric(element)) {
                    return std::string{field.name} + ": entry " + std::to_string(i) +
                           " is not a number";
                }
                const double raw = element.as_float64();
                if (!number_is_sane(raw, integral)) {
                    return std::string{field.name} + ": entry " + std::to_string(i) +
                           " is not a finite, plausible" + (integral ? " whole" : "") + " number";
                }
                if (i != 0) encoded += ',';
                encoded += integral ? std::to_string(element.as_int64()) : shortest_double_text(raw);
            }
            encoded += ']';
            out = std::move(encoded);
            return std::nullopt;
        }
    }
    // Unreachable for any Kind this file declares. Refusing rather than
    // falling through to success matters: a Kind added without a case here
    // would otherwise pass an UNVALIDATED value straight to the Finance
    // service.
    return std::string{field.name} + ": unrecognised field kind";
}

/**
 * Turns the model's `<params>` JSON into a FinanceParams, or into the refusal
 * that says why it could not be.
 *
 * The contract enforced here, in order:
 *
 *   1. It parses as a JSON object.
 *   2. `operation` is a string naming one of the 26 in-scope Finance RPCs.
 *   3. EVERY field of that RPC's request message is present.
 *   4. NO field that is not one of them is present.
 *   5. Each value validates against its declared type, enum constants and
 *      magnitude bounds.
 *
 * (3) and (4) are the two that look over-strict and are not. proto3 has no
 * "absent": a field this service drops arrives at the Finance service as zero,
 * and ComputeAmortization will amortize a principal of nothing, or a term of
 * nothing, and return an exact, auditable, completely wrong schedule. Nothing
 * downstream can catch that, because a zero is a perfectly valid int32. And
 * neither costs anything on-distribution: the training set asserts that each
 * emitted object's key set is EXACTLY the request message's field set, on every
 * one of its rows, so a model answering as it was taught passes both trivially
 * and only a truncated or drifting answer fails.
 *
 * ...AND THEN (6), WHICH IS THE ONE NONE OF THE ABOVE CAN DO. Steps 1-5 read the
 * model's output and nothing else, so the strongest thing they can conclude is
 * "internally consistent". `user_text` is threaded down to here for the sixth
 * step -- the mandatory GP-ARA gate at the bottom of this function -- because
 * grounding a value needs the utterance the value was supposed to come from, and
 * this is the first point in the pipeline where both exist at once.
 */
auto validate_and_populate_params(std::string_view json_text, std::string_view user_text,
                                  ::mortgage::assistant::ParseResponse& response) -> void {
    auto parsed = fastjson::parse(json_text);
    if (!parsed.has_value() || !parsed->is_object()) {
        populate_refusal(response, ::mortgage::assistant::Refusal::INVALID_PARAMETERS,
                         "The assistant's structured output could not be parsed as a JSON object.");
        return;
    }
    const auto& obj = parsed.value();

    if (!obj.contains("operation") || !obj["operation"].is_string()) {
        populate_refusal(response, ::mortgage::assistant::Refusal::UNSUPPORTED_OPERATION,
                         "The assistant did not name a calculation for this request.");
        return;
    }
    const std::string operation{obj["operation"].as_string()};
    const Operation* op = find_operation(operation);
    if (op == nullptr) {
        // Deliberately NOT normalised to a nearest match. Among these
        // particular operations the near neighbours are not benign:
        // ComputeNpv and ComputeXnpv take different arguments and answer
        // different questions, and ComputeInterestPayment and
        // ComputePrincipalPayment share a request message while returning
        // opposite halves of the same payment. Silently redirecting would
        // produce a confident answer to a question nobody asked.
        populate_refusal(response, ::mortgage::assistant::Refusal::UNSUPPORTED_OPERATION,
                         "\"" + operation +
                             "\" is not one of the finance operations this assistant covers.");
        return;
    }

    // (4) -- reject unknown keys BEFORE filling anything in, so a response
    // carrying a field from a DIFFERENT operation's schema is refused as the
    // schema confusion it is rather than silently half-accepted.
    for (const auto& [key, value] : obj.as_object()) {
        if (key == "operation") continue;
        if (find_field(*op, key) == nullptr) {
            populate_refusal(response, ::mortgage::assistant::Refusal::INVALID_PARAMETERS,
                             "\"" + key + "\" is not a parameter of " + operation + " (" +
                                 std::string{op->request_message} + ").");
            return;
        }
    }

    // (3) and (5) -- every declared field must be present and must validate.
    //
    // The verifier's view of the same object is assembled in this one pass
    // alongside the wire message, from the ENCODED value rather than from the
    // JSON a second time, so what the gate below judges is byte-for-byte what
    // this response would carry.
    ::mortgage::assistant::FinanceParams params;
    params.set_operation(operation);
    mv::MortgageParamsInput verifiable;
    verifiable.params_emitted = true;
    verifiable.operation = operation;
    for (const auto& field : op->fields) {
        const std::string key{field.name};
        if (!obj.contains(key)) {
            populate_refusal(response, ::mortgage::assistant::Refusal::INVALID_PARAMETERS,
                             "The assistant left out \"" + key + "\", which " + operation +
                                 " needs. Filling it in with a default would compute an exact "
                                 "answer to a question nobody asked.");
            return;
        }
        std::string encoded;
        if (const auto problem = validate_and_encode(field, obj[key], encoded);
            problem.has_value()) {
            populate_refusal(response, ::mortgage::assistant::Refusal::INVALID_PARAMETERS,
                             "The assistant's " + *problem + ".");
            return;
        }
        const bool repeated =
            field.kind == Kind::RepeatedDouble || field.kind == Kind::RepeatedInt;
        verifiable.fields.push_back(mv::EmittedField{
            .name = key,
            .values = repeated ? split_array_encoding(encoded)
                               : std::vector<std::string>{encoded},
            .repeated = repeated});

        (*params.mutable_params())[key] = std::move(encoded);
    }

    // ------------------------------------------------------------------
    // (6) MANDATORY GP-ARA VERIFICATION. Nothing reaches
    // `response.mutable_params()` except through this gate.
    //
    // It runs LAST, on purpose and in this order, because `ground_emitted_values`
    // documents that it ASSUMES the structural gates already passed -- it needs a
    // real operation and real field names to know what kind of quantity each
    // value holds. Running it here means the checks above have already settled
    // that, and `verify_mortgage_output` re-runs them itself as G1/G2/G5 anyway,
    // so the two are belt-and-braces rather than an ordering hazard.
    //
    // The tri-state is preserved end to end and never collapsed to a boolean.
    // Proven is the ONLY path to a FinanceParams. Unsafe and Indeterminate take
    // the identical refusal branch, which is the fail-closed default the module's
    // banner argues for at length: Indeterminate is by definition the catalogue
    // of that rule table's blind spots, so serving on it would reduce an
    // attacker's job to finding one. `VerificationVerdict` even
    // default-constructs to Indeterminate for the same reason.
    //
    // A REFUSAL, NOT A gRPC ERROR. Per this proto's own banner, both are
    // successful outcomes of ParseOperation: the RPC did its job -- it declined
    // to hand back numbers the user never said. Encoding it as an error would
    // make every correctly-refused hallucination indistinguishable from the model
    // being down, in gRPC metrics, in Envoy's access log and in the browser
    // client.
    const auto verification_start = std::chrono::steady_clock::now();
    const auto verdict = mv::verify_mortgage_output(verifiable, user_text);
    const auto verification_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                     std::chrono::steady_clock::now() - verification_start)
                                     .count();
    logger::Logger::getInstance().debug(
        "mortgage assistant: GP-ARA verification of {} took {}us, outcome={} reason={}", operation,
        verification_us, mv::to_string(verdict.outcome), mv::to_string(verdict.reason));

    if (verdict.outcome != mv::Outcome::Proven) {
        populate_refusal(response, map_verification_reason(verdict.reason),
                         verdict.message.empty()
                             ? "The assistant's parameters could not be verified against your "
                               "request."
                             : verdict.message);
        return;
    }

    *response.mutable_params() = std::move(params);
}

// ---------------------------------------------------------------------------
// Input-side guards
// ---------------------------------------------------------------------------

/**
 * Advice phrasings specific to THIS domain, on top of the shared table.
 *
 * `assistant_verification`'s `looks_like_advice_request` is reused rather than
 * reimplemented -- "should i buy", "worth it", "do you recommend", "what do you
 * think" are domain-neutral asks for a judgment and catch the common shapes here
 * too. What it cannot catch is the mortgage-specific vocabulary, because it was
 * written against an options-trading utterance ("will it go up this week"). This
 * table is the addendum, not a replacement, and it is deliberately limited to
 * phrasings that ask this service to JUDGE or PREDICT rather than to compute:
 * "should I refinance" is a request for advice, "what would refinancing at 5.5%
 * cost me" is a calculation, and only the first is refused.
 *
 * Same conservative reading the sibling service applies: a false positive costs
 * the user a rephrase, while a false negative is a 0.6B model improvising
 * financial advice on a page that computes real loan numbers. The model's own
 * training system prompt already says it does not give financial, tax or legal
 * advice; this is a deterministic floor under that, checked before the model is
 * ever asked, so the refusal does not depend on the model having learned it.
 */
constexpr std::array<std::string_view, 16> kMortgageAdviceSignals{{
    "should i refinance",   "should i buy a house",   "should i buy a home",
    "should i pay off",     "should i pay down",      "should i rent",
    "is it a good time",    "is now a good time",     "good time to buy",
    "good time to refi",    "can i afford",           "how much house can i",
    "am i better off",      "what would you do",      "is it smart to",
    "is it worth refinancing",
}};

[[nodiscard]] auto looks_like_domain_advice_request(std::string_view utterance) -> bool {
    std::string lower;
    lower.reserve(utterance.size());
    for (const char c : utterance) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return std::ranges::any_of(kMortgageAdviceSignals, [&lower](std::string_view signal) {
        return lower.find(signal) != std::string::npos;
    });
}

// ---------------------------------------------------------------------------
// Output interpretation
// ---------------------------------------------------------------------------

auto interpret_model_output(const std::string& raw_text, std::string_view utterance,
                            std::string_view prior_clarification,
                            ::mortgage::assistant::ParseResponse& response) -> void {
    // LOG THE RAW OUTPUT FIRST, BEFORE ANY VERIFICATION RUNS.
    //
    // The model's raw output is otherwise invisible: nothing else logs it and
    // there is no field on ParseResponse carrying it, so when this function
    // refuses, the only thing an operator sees is the refusal -- with no way to
    // tell a model that answered badly from one that answered fine into a
    // validator that mishandled it. That distinction is the whole diagnosis, and
    // its absence already cost a debugging cycle on the sibling service. It is
    // printed here, unconditionally, ahead of every check below.
    std::fprintf(stderr, "[mortgage-assistant] raw model output (%zu bytes): %s\n", raw_text.size(),
                 raw_text.c_str());
    std::fflush(stderr);

    const std::string visible = strip_think_block(raw_text);

    if (const auto block = extract_params_block(visible); block.has_value()) {
        // The utterance travels with the params block from here down. It is the
        // ONLY object that can falsify a structurally perfect answer, so the
        // params path is the one path that must never be walked without it.
        validate_and_populate_params(trim(*block),
                                     grounding_text(utterance, prior_clarification), response);
        return;
    }

    // NO PARAMS BLOCK IS NOT ROUTED THROUGH THE VERIFIER, DELIBERATELY.
    //
    // `verify_mortgage_output`'s G4 answers Indeterminate for an absent params
    // block, which at this boundary would mean a refusal -- and that is right for
    // a caller about to DISPATCH something, which is what G4 is written for.
    // Here there is nothing to dispatch: the model asked a question instead, and
    // this proto's banner is explicit that a clarifying question is the model
    // doing its job correctly. Feeding the absent case to the gate would convert
    // all 824 clarification shapes in the training set into refusals.

    // No params block. Whatever prose remains is the model's clarifying
    // question -- which is a CORRECT outcome, not a failure, when it is short
    // and specific ("What's the time horizon?"). The 824 clarification rows in
    // the training set are exactly this shape.
    const std::string question = trim(visible);
    if (question.empty() || question.size() > kMaxClarificationLength) {
        // Neither a valid params block nor something that looks like one short
        // clarifying question. That is a Refusal -- never a crash, and never an
        // invented answer.
        populate_refusal(response, ::mortgage::assistant::Refusal::OUT_OF_SCOPE,
                         "The assistant could not produce structured parameters or a short "
                         "clarifying question for this request.");
        return;
    }
    populate_clarification(response, question);
}

// ---------------------------------------------------------------------------
// Admission: authenticate, then charge
// ---------------------------------------------------------------------------

/**
 * The same admission guard finance_service.cpp and assistant_service.cpp
 * document and use.
 *
 * The SERVICE SCOPE is deliberately "assistant", the same string the strategy
 * assistant authenticates under, rather than a new "mortgage_assistant". Key
 * scopes are matched exactly (see `KeyRegistry::check`), so a new scope name
 * would reject every key already issued for assistant access until each was
 * re-minted -- a silent, confusing PERMISSION_DENIED for callers who were
 * granted assistant access and reasonably believe they have it. The two
 * assistants have the same cost profile and the same entitlement, so granting
 * one and not the other is not a distinction anyone has asked to draw. Splitting
 * the scope later is a deliberate change with a migration, not a default.
 */
#define CHARGE(method_name, cost)                                                    \
    ::options_calculator::auth::Identity _id;                                        \
    do {                                                                             \
        if (auto _a = ::options_calculator::auth::KeyRegistry::instance().authenticate( \
                *context, "assistant", (method_name), _id);                          \
            !_a.ok()) {                                                              \
            return _a;                                                               \
        }                                                                            \
        ::options_calculator::quota::TierLimits _lim{_id.requests_per_minute,          \
                                                    _id.compute_units_per_hour};      \
        if (auto _q = ::options_calculator::quota::QuotaEnforcer::instance().admit_identity( \
                _id.id, _id.tier, (method_name), (cost), _id.has_limits ? &_lim : nullptr); \
            !_q.ok()) {                                                              \
            return _q;                                                               \
        }                                                                            \
    } while (false)

// ---------------------------------------------------------------------------
// Service
// ---------------------------------------------------------------------------

class MortgageAssistantImpl final : public ::mortgage::assistant::MortgageAssistant::Service {
  public:
    auto ParseOperation(ServerContext* context,
                        const ::mortgage::assistant::ParseRequest* request,
                        ::mortgage::assistant::ParseResponse* response) -> Status override {
        if (context == nullptr || request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }

        // CHARGE at the top, before any inference work, per quota.cppm's own
        // rationale: a refused call should cost a hash lookup, not a
        // generation. `cost_llm_generate` prices this call from the same
        // kMaxNewTokens bound the worker actually generates with, so the price
        // and the real worst case never drift apart.
        CHARGE("ParseOperation",
               ::options_calculator::quota::cost_llm_generate(1, static_cast<int>(kMaxNewTokens)));

        // Bound the two caller-controlled prompt inputs BEFORE they are
        // concatenated into a prompt or reach the worker. This is
        // request-shape validation -- the request itself is malformed,
        // independent of whether the calculation it describes makes sense -- so
        // it is INVALID_ARGUMENT, not a Refusal.
        if (request->utterance().size() > kMaxUtteranceLength) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "utterance exceeds the " + std::to_string(kMaxUtteranceLength) +
                              "-character limit for this RPC.");
        }
        if (request->prior_clarification().size() > kMaxPriorClarificationLength) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "prior_clarification exceeds the " +
                              std::to_string(kMaxPriorClarificationLength) +
                              "-character limit for this RPC.");
        }

        // Two input-side guards, run BEFORE the model is ever asked to
        // generate. No check on the model's OUTPUT can substitute: both shapes
        // below can produce a structurally perfect `<params>` block that every
        // rule in `validate_and_populate_params` accepts, because that
        // validator only ever sees the final parameters, never the utterance
        // that produced them. Checked on `utterance` AND `prior_clarification`,
        // because `build_prompt` feeds both to the model verbatim and a
        // second-turn reply carries either signal just as well as a first-turn
        // request.
        if (::options_calculator::assistant::verify::looks_like_prompt_injection(
                request->utterance()) ||
            ::options_calculator::assistant::verify::looks_like_prompt_injection(
                request->prior_clarification())) {
            populate_refusal(
                *response, ::mortgage::assistant::Refusal::OUT_OF_SCOPE,
                "This reads like an instruction to the assistant rather than a calculation. "
                "Describe the numbers you want worked out (e.g. \"$420,000 at 6.25% over 30 "
                "years\") without instructions aimed at the assistant itself.");
            return Status::OK;
        }
        if (::options_calculator::assistant::verify::looks_like_advice_request(
                request->utterance()) ||
            looks_like_domain_advice_request(request->utterance())) {
            populate_refusal(
                *response, ::mortgage::assistant::Refusal::OUT_OF_SCOPE,
                "I don't give financial, tax or legal advice -- describe a specific calculation "
                "(e.g. \"what would a $420,000 loan at 6.25% over 30 years cost per month\") and "
                "I will work it out.");
            return Status::OK;
        }

        // The Pro gate, server-side and before any inference work. `_id` is the
        // identity CHARGE already resolved, so authentication runs exactly once
        // per call. `check_assistant_entitlement` is shared with the strategy
        // assistant deliberately: its rationale is cost asymmetry -- one
        // `cost_llm_generate` call against the shared anonymous budget -- which
        // is a property of running a 0.6B model at all, not of which model it
        // is, and this RPC has exactly the same profile.
        if (auto s = ::options_calculator::auth::check_assistant_entitlement(
                _id, ::options_calculator::auth::kMortgageSurface); !s.ok()) {
            return s;
        }

        auto& worker = MortgageAssistantWorker::instance();
        if (!worker.available()) {
            // Degrade honestly: MORTGAGE_MODEL_PATH was never set, or the model
            // failed to load at startup. Genuine infrastructure unavailability,
            // but per the proto's own MODEL_UNAVAILABLE doc comment it is
            // expressed as a successful RPC carrying that reason rather than a
            // gRPC error -- the RPC to THIS service completed correctly.
            populate_refusal(*response, ::mortgage::assistant::Refusal::MODEL_UNAVAILABLE,
                             "The mortgage assistant is not available right now.");
            return Status::OK;
        }

        const std::string prompt =
            build_prompt(request->utterance(), request->prior_clarification());

        auto outcome = worker.submit(prompt);
        if (!outcome.has_value()) {
            // The ONE case that is a genuine gRPC error rather than a Refusal:
            // the bounded queue was already full. Immediate -- submit() never
            // waited to make room.
            return Status(grpc::StatusCode::RESOURCE_EXHAUSTED,
                          "The mortgage assistant is at capacity; please retry shortly.");
        }

        if (!outcome->ok) {
            // The backend accepted the job but failed to complete it. Same
            // reasoning as the unavailable-at-startup case: the RPC to THIS
            // service completed correctly, so this is a Refusal.
            populate_refusal(*response, ::mortgage::assistant::Refusal::MODEL_UNAVAILABLE,
                             "The mortgage assistant failed to produce a response: " +
                                 outcome->error);
            return Status::OK;
        }

        interpret_model_output(outcome->text, request->utterance(), request->prior_clarification(),
                               *response);
        return Status::OK;
    }
};

}  // namespace

auto RegisterMortgageAssistantService(grpc::ServerBuilder& builder) -> void {
    // Static storage duration for the same reason every other service here uses
    // it: gRPC's RegisterService takes the address and does not take ownership,
    // so the service must outlive both the builder and the server.
    static MortgageAssistantImpl service;
    builder.RegisterService(&service);
    logger::Logger::getInstance().info(
        "Registered {} on the same port as the calculator",
        ::mortgage::assistant::MortgageAssistant::service_full_name());

    // Force the worker to construct (and attempt to load MORTGAGE_MODEL_PATH)
    // NOW rather than on the first RPC: whether the assistant is usable should
    // be a fact this process establishes and logs at startup, not something the
    // first caller discovers by accident. A failed load is logged and swallowed,
    // never propagated -- a missing or broken model must degrade this ONE
    // service, not take down the three sharing this port.
    const bool loaded = MortgageAssistantWorker::instance().available();
    logger::Logger::getInstance().info("Mortgage assistant model is {}",
                                       loaded ? "LOADED"
                                              : "UNAVAILABLE (set MORTGAGE_MODEL_PATH to enable)");
}

#undef CHARGE

}  // namespace options_calculator::mortgage_assistant
