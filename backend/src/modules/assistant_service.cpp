module;
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <deque>
#include <exception>
#include <future>
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
#include "assistant.pb.h"
#include "assistant.grpc.pb.h"

// The selectable llama.cpp backend. Included in the GLOBAL MODULE FRAGMENT,
// like every other header this translation unit uses, because `llama.h` is a
// plain C header and a module purview cannot include one. The whole backend is
// compiled out when the build was configured with ENABLE_LLAMACPP_BACKEND=OFF,
// in which case asking for it at runtime is reported as the configuration
// mistake it is rather than silently answered by the other engine.
#ifdef ASSISTANT_HAVE_LLAMACPP
#include "llama.h"
#endif

module assistant_service;

import sensen.llm_pipeline;
import fastjson;
import logger;
import quota;
import api_key;
import strategy_catalogue;
import market_data;
import assistant_verification;

namespace options_calculator::assistant {

using grpc::ServerContext;
using grpc::Status;

namespace {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

/**
 * THE MANDATORY TRAINING SYSTEM PROMPT.
 *
 * Verbatim, character for character, per the executor brief that shipped this
 * file: the fine-tuned Qwen3-0.6B extraction model was trained against this
 * exact string as its system turn. Without it, the ground truth is explicit
 * that the model reverts to stock Qwen3 behaviour -- it emits `<think>`
 * blocks and refusals and NEVER produces a `<params>` block. This is not a
 * tunable default; changing so much as a comma here silently converts a
 * working extraction model back into an uninstructed base model, and nothing
 * downstream (parsing, validation, tests) would tell you why every call
 * suddenly started producing garbage. It is injected on EVERY call, with no
 * opt-out, for exactly that reason.
 */
constexpr std::string_view kSystemPrompt =
    "You turn a trader's request into parameters for the Options & Futures "
    "Calculator. Reply with a single JSON object inside <params></params> "
    "when you have enough to act, or ask exactly one short question when you "
    "do not. You do not give trading advice.";

/**
 * Upper bound on generated tokens per call.
 *
 * A `<params>` JSON blob (five short fields) or a one-sentence clarifying
 * question both fit comfortably inside this many tokens; it exists mainly to
 * bound the worst case -- a model that fails to emit a closing tag and just
 * keeps going -- so one bad call cannot hold the single worker thread for an
 * unbounded stretch. It is also the exact figure `cost_llm_generate` charges
 * against, so raising it changes both the ceiling and the price together.
 */
constexpr std::size_t kMaxNewTokens = 256;

/** Depth of the bounded request queue feeding the inference worker. */
constexpr std::size_t kMaxQueueDepth = 4;

/** A clarifying question longer than this does not look like "one short
 * question" any more -- either the model rambled or the system prompt did
 * not take, and passing it through as-is would be a worse experience than a
 * refusal that says so plainly. */
constexpr std::size_t kMaxClarificationLength = 400;

/**
 * Server-side caps on the two caller-supplied strings, checked BEFORE the
 * prompt is assembled or the worker is touched.
 *
 * These exist because nothing else in this file bounds them. `kMaxNewTokens`
 * bounds the OUTPUT the worker is asked to generate, and `cost_llm_generate`
 * prices the call from that same constant -- but the call's actual cost also
 * scales with how much the worker has to PREFILL, and prefill cost is a
 * function of `utterance`/`prior_clarification` length, which a caller fully
 * controls and which the price never accounts for. The one existing guard
 * against an oversized prompt (`LlamaCppBackend::tokenise`'s
 * `tokens.size() + kMaxNewTokens > n_ctx_per_seq_` check) belongs to the
 * SELECTABLE llama.cpp backend, not the production default (`sensen`,
 * `SensenBackend::admit`) -- which has no equivalent check at all, so an
 * unbounded prompt there is free to consume the worker for however long
 * prefill over that many tokens takes, still billed at the SAME
 * `cost_llm_generate(1, kMaxNewTokens)` a one-line utterance costs.
 *
 * `kMaxUtteranceLength` is generous for what this RPC actually does --
 * turning one trade description into five structured fields -- while still
 * rejecting a multi-kilobyte payload that could only be padding, not a trade
 * description. `kMaxPriorClarificationLength` reuses `kMaxClarificationLength`
 * deliberately rather than picking its own number: `prior_clarification` is
 * documented (assistant.proto) as always being a question THIS SERVICE itself
 * asked on a prior turn, and this service never emits one longer than
 * `kMaxClarificationLength` (see `interpret_model_output`) -- so any honest
 * client can never legitimately send more than that, and a value beyond it is
 * itself evidence the field is being misused (echoing the original utterance,
 * or something adversarial) rather than a genuine reply to a prior question.
 */
constexpr std::size_t kMaxUtteranceLength = 1000;
constexpr std::size_t kMaxPriorClarificationLength = kMaxClarificationLength;

/** A ticker beyond this length is not a real symbol this product trades. */
constexpr std::size_t kMaxSymbolLength = 15;

/**
 * Sane bounds on the two numeric fields.
 *
 * `expiration_days` is calendar days out, as the trader stated it: 0 admits
 * same-day (0DTE) expiries, which are real and heavily traded on index
 * options; ~10 years comfortably covers LEAPS and any futures term structure
 * this product models, while still rejecting an obviously hallucinated
 * figure (e.g. a four- or five-digit typo the model invented). `quantity` is
 * contracts or lots, not shares or notional; zero or negative is never a
 * real order, and the upper bound is generous for any retail-to-small-desk
 * size this calculator is aimed at while still catching a runaway number.
 */
constexpr std::int64_t kMinExpirationDays = 0;
constexpr std::int64_t kMaxExpirationDays = 3650;
constexpr std::int64_t kMinQuantity = 1;
constexpr std::int64_t kMaxQuantity = 100'000;

// ---------------------------------------------------------------------------
// Environment helpers
// ---------------------------------------------------------------------------

[[nodiscard]] auto env_string(const char* name) -> std::optional<std::string> {
    const char* raw = std::getenv(name);
    if (raw == nullptr || *raw == '\0') return std::nullopt;
    return std::string{raw};
}

/** Parses a positive thread count from an env var, falling back on anything
 * unset, empty, non-numeric, or non-positive -- a malformed override should
 * degrade to the documented default, never to zero threads or a crash. */
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
 * Builds the ChatML prompt this codebase already speaks everywhere else
 * (see `build_anthropic_chatml_prompt` in sensen/server/serve_http.cppm) --
 * reused here as a small local function rather than an import, because that
 * function lives inside the general-purpose HTTP proxy module and is not
 * exported for reuse; duplicating four lines of string concatenation is
 * cheaper and safer than pulling in the whole HTTP surface for it.
 *
 * `prior_clarification`, when present, is represented as the assistant's own
 * preceding turn. This is a judgement call, not something the proto or the
 * training data specifies: ParseRequest deliberately does not carry the
 * ORIGINAL utterance that provoked the clarification (see assistant.proto's
 * own comment on why), so the true three-turn exchange (user, assistant
 * question, user answer) cannot be fully reconstructed -- only its last two
 * turns can. Presenting those two turns in the correct roles is the most
 * faithful representation available from what the proto actually carries,
 * and leans on the fine-tuned model's own robustness to recover the missing
 * first turn from context the way a human reading the transcript would.
 */
[[nodiscard]] auto build_prompt(std::string_view utterance, std::string_view prior_clarification)
    -> std::string {
    std::string prompt;
    prompt += "<|im_start|>system\n";
    prompt += kSystemPrompt;
    prompt += "<|im_end|>\n";
    if (!prior_clarification.empty()) {
        prompt += "<|im_start|>assistant\n";
        prompt += prior_clarification;
        prompt += "<|im_end|>\n";
    }
    prompt += "<|im_start|>user\n";
    prompt += utterance;
    prompt += "<|im_end|>\n";
    prompt += "<|im_start|>assistant\n";
    return prompt;
}

// ---------------------------------------------------------------------------
// Inference backends
// ---------------------------------------------------------------------------

/** What a backend hands back for one generation request. */
struct InferenceOutcome {
    bool ok = false;
    std::string text;   // the model's raw decoded output, valid iff ok
    std::string error;  // human-readable failure detail, valid iff !ok
};

/**
 * One accepted request, in flight from the gRPC handler thread that accepted
 * it to the backend owner thread that will serve it.
 *
 * The promise is fulfilled exactly once, on every path out of the owner
 * thread including shutdown -- a promise destroyed unfulfilled would make the
 * handler thread's `future.get()` throw `std::future_error` instead of
 * returning, which is a strictly worse failure than an honest error string.
 */
struct PendingJob {
    std::string prompt;
    std::promise<InferenceOutcome> promise;
};

/**
 * The narrow contract the RPC layer depends on, and the ONLY thing it knows
 * about inference.
 *
 * There are two implementations -- sensen's in-process `LLMPipeline` and
 * upstream llama.cpp -- and the RPC handler cannot tell them apart. That is
 * the point: the sensen path is the production default because it is this
 * project's own stack and needs no third-party runtime, but it is a large,
 * fast-moving library whose CPU inference path this service has already been
 * bitten by once (see the `n_gpu_layers` comment in the decode loop below).
 * Having a second, independently-implemented engine behind the same interface
 * turns "the model backend is misbehaving" from an outage into a one
 * environment-variable change, and -- more valuable during diagnosis -- gives
 * a second opinion on what the same GGUF, given the same prompt, ought to
 * produce.
 */
class InferenceBackend {
  public:
    InferenceBackend() = default;
    virtual ~InferenceBackend() = default;
    InferenceBackend(const InferenceBackend&) = delete;
    auto operator=(const InferenceBackend&) -> InferenceBackend& = delete;
    InferenceBackend(InferenceBackend&&) = delete;
    auto operator=(InferenceBackend&&) -> InferenceBackend& = delete;

    /**
     * Submits one prompt and blocks the CALLING thread (an accepted gRPC
     * handler thread) until the backend has produced a result.
     *
     * Returns `std::nullopt` iff the admission queue was already full when
     * this call arrived -- the caller must map that, and only that, to
     * RESOURCE_EXHAUSTED. Any other outcome, including a failed generation,
     * comes back as an `InferenceOutcome` with `ok == false`.
     */
    [[nodiscard]] virtual auto submit(std::string prompt) -> std::optional<InferenceOutcome> = 0;

    /** Identifier for logs and for the startup banner. Never user-facing. */
    [[nodiscard]] virtual auto name() const noexcept -> std::string_view = 0;
};

/**
 * The admission queue and back-pressure policy both backends share.
 *
 * WHY A BOUNDED QUEUE AT ALL:
 *
 * Every accepted request holds a gRPC handler thread blocked on `submit()`
 * for the duration of one extraction. An unbounded queue would let load pile
 * up invisibly -- the caller-facing symptom would be requests taking longer
 * and longer to answer, with no signal distinguishing "briefly busy" from
 * "about to wait minutes", and no way for a client to make an informed retry
 * decision. A bounded queue makes the choice explicit instead: once it is
 * full, a request fails FAST with RESOURCE_EXHAUSTED rather than joining an
 * invisible line. A fast, honest refusal beats an unbounded latency cliff --
 * the caller can retry with backoff immediately, instead of discovering
 * minutes later that its request was one of dozens silently queued behind it.
 *
 * The queue lives on the calling gRPC threads' side: `submit()` never blocks
 * to make room. If the queue is already full when a new request arrives it is
 * turned away immediately, full stop -- the gRPC thread is never parked
 * waiting for space to open up.
 *
 * WHAT CHANGED FROM THE ORIGINAL ONE-AT-A-TIME DESIGN:
 *
 * This class used to serve exactly one request at a time behind a queue of
 * depth four, so a fifth concurrent user was refused outright and the fourth
 * waited roughly four extractions. That is no longer the shape. Both backends
 * now run CONTINUOUS BATCHING: a single owner thread holds a set of in-flight
 * sequences and advances all of them by one token per iteration in one fused
 * forward pass, admitting newly-queued requests into free slots as earlier
 * ones finish. The queue below is therefore the WAITING room in front of that
 * set, not the whole capacity.
 */
class QueuedBackend : public InferenceBackend {
  public:
    [[nodiscard]] auto submit(std::string prompt) -> std::optional<InferenceOutcome> final {
        // The promise/future pair is only constructed once there is confirmed
        // room -- the full-queue rejection path stays as cheap as an
        // immediate refusal should be, rather than paying for setup work
        // whose result is about to be discarded.
        std::future<InferenceOutcome> future;
        {
            const std::lock_guard lock{mutex_};
            if (shutting_down_) {
                return InferenceOutcome{
                    .ok = false, .text = {}, .error = "assistant backend is shutting down"};
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
     * `want` is how many free in-flight slots the caller has right now, so a
     * backend never dequeues work it has no slot to run -- a dequeued job is
     * one whose promise this thread has taken responsibility for, and holding
     * un-runnable ones would only make the shutdown drain longer.
     *
     * `block` is false when the caller already has sequences in flight: it
     * must not park on the condition variable while there is decoding to do,
     * it just wants whatever happens to be waiting.
     */
    [[nodiscard]] auto take_jobs(std::stop_token stoken, std::size_t want, bool block)
        -> std::vector<PendingJob> {
        std::vector<PendingJob> taken;
        if (want == 0) return taken;

        std::unique_lock lock{mutex_};
        if (block) {
            // The stop_token-aware overload registers a stop callback that
            // notifies this condition variable the moment a stop is
            // requested, so this wakes promptly on shutdown rather than only
            // on the next enqueue.
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
     * `future.get()` hangs forever waiting on a promise this backend will
     * never fulfil. Called from the owner thread as it exits, and again --
     * harmlessly, the queue is empty by then -- is not needed, so it is
     * called exactly once.
     *
     * `shutting_down_` is set under the same lock so a request that races the
     * shutdown gets an honest error rather than being enqueued onto a queue
     * nobody will ever drain again.
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

// ---------------------------------------------------------------------------
// Backend 1: sensen's in-process LLMPipeline (the production default)
// ---------------------------------------------------------------------------

/**
 * Drives `sensen::LLMPipeline` through its ITERATION-LEVEL SCHEDULER API from
 * one owner thread, batching every in-flight request into a single fused
 * decode step per iteration.
 *
 * WHY THE SCHEDULER API AND NOT `generate()` FROM A THREAD POOL:
 *
 * `LLMPipeline::generate()` is not safe to call concurrently on one pipeline,
 * and the reason is specific rather than a general disclaimer. sensen's
 * `FeedForwardNetwork` (sensen/src/transformer_block.cppm) keeps its per-call
 * working buffers as `mutable std::vector<float> scratch_up_/scratch_gate_`
 * MEMBERS -- one set per layer object, not `thread_local` -- and the
 * single-token decode entry point `forward_swiglu_single` writes straight into
 * them. The layer objects belong to the one `LlamaModel` the whole pipeline
 * shares, so two threads decoding different requests would interleave writes
 * into the same scratch and silently corrupt each other's hidden states. That
 * is a wrong-answer race, not a crash, which is the worst kind to ship. The
 * same hazard rules out the library's own `generateBatch`/`generateConcurrent`
 * convenience wrappers, which fan `forward()` across the pipeline's internal
 * thread pool and hit exactly this.
 *
 * The scheduler API is the path sensen's own server (sensen/server/
 * serve_reactor.cppm) uses in production, and it is built on the opposite
 * premise: ONE owner thread is the sole caller of the forward path, and
 * concurrency comes from batching N sequences into a single `forwardBatch`
 * step rather than from N threads. That is what this class does. Its
 * multi-token batched forward uses genuinely `thread_local` scratch, so the
 * race above does not apply to it.
 *
 * WHY NOT A POOL OF PIPELINES INSTEAD:
 *
 * That was the obvious alternative and it is far more expensive. Weights are
 * held once per PIPELINE and shared by every sequence in it; only the KV cache
 * is per-sequence. A second pipeline therefore duplicates the entire resident
 * model -- measured at roughly 3.3 GB for this checkpoint -- to buy
 * concurrency that one pipeline already provides for the cost of one more KV
 * cache. Four independent pipelines would need on the order of thirteen
 * gigabytes to serve four users; one pipeline with four sequences was measured
 * flat against one sequence at these prompt lengths.
 *
 * WHAT BATCHING BUYS AND WHAT IT COSTS (measured on this checkpoint, CPU,
 * 96 new tokens, against a serial baseline through the same loop):
 *
 *     concurrent | batched wall | serial wall | aggregate tok/s | speed-up
 *              1 |      1.03 s  |     1.04 s  |          35.8   |  1.00x
 *              2 |      2.02 s  |     2.14 s  |          36.6   |  1.06x
 *              4 |      3.45 s  |     4.47 s  |          43.7   |  1.29x
 *              8 |      5.72 s  |     8.57 s  |          52.3   |  1.50x
 *
 * The honest cost is tail latency: an individual request inside a batch of
 * eight took about 4.6 s versus about 1.0 s alone, because it now shares the
 * decode loop. Aggregate throughput rises, per-request latency rises with it.
 * `ASSISTANT_MAX_CONCURRENT` is where that trade is set, and it defaults low
 * (four) because this is an interactive assistant where a trader waiting on
 * one answer cares more about their own latency than about the server's
 * aggregate token rate. Raising it trades the former for the latter.
 *
 * Batching does NOT change any single request's ANSWER: the same prompt was
 * verified to decode byte-identically alone and inside a batch of eight, at
 * two different thread counts.
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
                "sensen backend: failed to load the strategy-assistant model from {}: {}",
                model_path, e.what());
            return nullptr;
        } catch (...) {
            logger::Logger::getInstance().error(
                "sensen backend: failed to load the strategy-assistant model from {}: unknown "
                "error",
                model_path);
            return nullptr;
        }
        if (pipeline == nullptr) {
            logger::Logger::getInstance().error(
                "sensen backend: LLMPipeline::Builder::build() returned null for {}", model_path);
            return nullptr;
        }

        auto backend = std::unique_ptr<SensenBackend>(
            new SensenBackend(std::move(pipeline), max_concurrent, queue_depth));
        backend->start();
        return backend;
    }

    [[nodiscard]] auto name() const noexcept -> std::string_view override { return "sensen"; }

    ~SensenBackend() override = default;

  private:
    /**
     * One in-flight request as the decode loop sees it: the sensen session
     * that owns its KV cache, the token it is about to consume, the ids it has
     * produced so far, and the promise waiting on the other end.
     */
    struct Sequence {
        std::unique_ptr<sensen::AgentSession> agent;
        std::promise<InferenceOutcome> promise;
        std::uint32_t next_token = 0;
        std::vector<std::uint32_t> generated;
    };

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
     * The sampling configuration every request in this process is generated
     * with. Built fresh per call rather than cached because it is three field
     * writes and sharing mutable state with the decode loop buys nothing.
     */
    [[nodiscard]] static auto generation_config(std::size_t max_new_tokens)
        -> sensen::GenerationConfig {
        sensen::GenerationConfig config;

        // Greedy, not sampled: this is structured extraction, not creative
        // generation. The training system prompt asks for one JSON object or
        // one question -- there is no case where sampling diversity across
        // candidates is wanted here, and greedy decode is the more
        // reproducible, more honest choice for a tool that must never invent
        // variation the trader did not ask for.
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
        // generated token onward, reads the next token id out of `logits[0]`
        // -- because the only code that honours that flag by returning a
        // one-element vector holding the sampled id lives inside
        // `#ifdef SENSEN_HAS_CUDA`, which this CPU-only build never defines.
        // The CPU forward path returns the full vocabulary-sized logit vector
        // and never consults the flag, so `logits[0]` is a raw float score,
        // and casting it to a token id lands in the low single digits, which
        // in Qwen's vocabulary are the punctuation characters `&'()*+,-`.
        //
        // The symptom was 262 bytes of punctuation noise, byte-identical
        // across runs (nothing random is involved once the sampler is
        // bypassed) and completely unaffected by `deterministic`, with only
        // the FIRST token correct (index zero still goes through the real
        // sampler). Setting n_gpu_layers to zero breaks the conjunction, so
        // the flag is never set and every token is sampled properly.
        //
        // The underlying defect has also been fixed in sensen itself -- the
        // GPU-ness test is now compiled out when SENSEN_HAS_CUDA is undefined
        // -- but this line stays regardless. It states what this service
        // actually wants (run entirely on the CPU; there is no GPU in the
        // deployment target) instead of relying on a library default, and it
        // keeps the service correct against any sensen build, fixed or not.
        config.n_gpu_layers = 0;

        return config;
    }

    /**
     * The owner thread: admit, decode one fused step across everything in
     * flight, retire whatever finished, repeat.
     *
     * Admission runs a prefill, which is a forward pass, on this same thread.
     * That briefly stalls the sequences already decoding -- sensen's own
     * server splits long prefills into chunks co-scheduled with the decode
     * batch to avoid exactly that. It is not worth doing here: this service's
     * prompts are one short system turn plus one short user turn, tens of
     * tokens, so a prefill costs a small fraction of the ~96-token decode it
     * is joining, and chunked prefill would add a scheduler's worth of state
     * to save it.
     */
    auto run(std::stop_token stoken) -> void {
        const auto params = generation_config(kMaxNewTokens);
        const std::uint32_t eos_id = pipeline_->schedulerEosToken();

        // `<|im_end|>` is how ChatML ends an assistant turn, and on this
        // fine-tune it is what actually terminates a response; the model's
        // configured EOS may or may not be the same id, so both are treated
        // as stop tokens rather than assuming. Resolved once, here, because
        // it is a tokenizer lookup that cannot change afterwards.
        std::uint32_t im_end_id = eos_id;
        if (const auto encoded = pipeline_->schedulerEncode("<|im_end|>"); encoded.size() == 1) {
            im_end_id = encoded.front();
        }

        std::vector<Sequence> active;
        std::size_t next_agent_id = 0;

        while (!stoken.stop_requested()) {
            // Admit whatever is waiting into free slots. Block only when
            // there is nothing in flight -- with sequences decoding, this
            // thread must return to the decode step promptly rather than
            // parking on the queue.
            const std::size_t free_slots = max_concurrent_ - active.size();
            auto jobs = take_jobs(stoken, free_slots, active.empty());
            for (auto& job : jobs) {
                Sequence seq;
                seq.promise = std::move(job.promise);
                if (!admit(job.prompt, seq, params, next_agent_id++)) {
                    // Admission is the one place a per-request failure is
                    // isolated: this sequence's promise is completed with the
                    // error and it simply never enters the active set, so one
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

        // Shutdown. Everything still decoding is failed, then the waiting
        // room is drained, so no caller is left blocked on a promise that
        // will never be fulfilled.
        for (auto& seq : active) {
            seq.promise.set_value(InferenceOutcome{
                .ok = false, .text = {}, .error = "assistant worker shutting down"});
            pipeline_->schedulerEvict(*seq.agent);
        }
        active.clear();
        drain_and_fail("assistant worker shutting down");
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
     * The cadence here is `schedulerDecodeStep`'s documented precondition and
     * mirrors sensen's own reactor: the token sampled on the previous
     * iteration is fed in, THEN each agent's position is advanced for the
     * token just consumed, THEN that token is appended to the context and
     * checked for a stop, and only then is the next one sampled.
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
            logger::Logger::getInstance().error("sensen backend: batched decode step threw: {}",
                                                e.what());
            return false;
        } catch (...) {
            logger::Logger::getInstance().error(
                "sensen backend: batched decode step threw an unknown exception");
            return false;
        }
        if (logits.size() != active.size()) {
            logger::Logger::getInstance().error(
                "sensen backend: batched decode step returned {} logit vectors for {} active "
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
                if (seq.generated.size() >= params.max_new_tokens) stop = true;
            }
            if (!stop && logits[i].empty()) {
                // A per-sequence empty logit vector from a step that
                // otherwise succeeded is not something to paper over: finish
                // this one honestly and let the rest of the batch continue.
                stop = true;
            }

            if (stop) {
                finish(seq);
                continue;
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

    std::unique_ptr<sensen::LLMPipeline> pipeline_;

    // Declared LAST: member destruction order is the reverse of declaration
    // order, so `worker_`'s destructor (request_stop + join) runs BEFORE
    // pipeline_ and the queue in the base are torn down, guaranteeing run()
    // has fully exited -- and therefore touched none of them -- by the time
    // they are destroyed.
    std::jthread worker_;
};

#ifdef ASSISTANT_HAVE_LLAMACPP

// ---------------------------------------------------------------------------
// Backend 2: upstream llama.cpp (the selectable fallback)
// ---------------------------------------------------------------------------

/**
 * The same continuous-batching shape, implemented against upstream
 * llama.cpp's plain C API.
 *
 * WHY THIS EXISTS: it is a second, entirely independent implementation of
 * "run this GGUF and give me the tokens", built from a different codebase by
 * different people. When the primary engine produced garbage for this exact
 * model, what established that the model, the quantisation and the prompt
 * were all fine -- and therefore that the defect was in the caller -- was
 * llama.cpp producing the correct answer from the same file. Keeping that
 * second opinion permanently available, behind the same interface and one
 * environment variable, is worth the dependency.
 *
 * WHY ONE CONTEXT WITH MANY SEQUENCES, NOT ONE CONTEXT PER USER:
 *
 * A `llama_context` sized with `n_seq_max = N` holds N independent KV
 * sequences over ONE copy of the weights, and `llama_decode` accepts a batch
 * carrying tokens for several sequence ids at once. N contexts would instead
 * duplicate the ~640 MB of weights N times for the same concurrency. Measured
 * on this checkpoint: resident memory went 740 MB / 803 MB / 926 MB / 1176 MB
 * at one, two, four and eight parallel sequences -- about 56 MiB of KV cache
 * per additional concurrent user on a fixed base, which is the cost this
 * design is choosing to pay.
 *
 * WHY A SINGLE OWNER THREAD RATHER THAN A MUTEX AROUND `generate()`:
 *
 * `llama_decode` on one context is not thread-safe, so some serialisation is
 * mandatory either way. A mutex would serialise whole REQUESTS; an owner
 * thread gathering every in-flight sequence into one batch per step
 * serialises only the STEP, which is what makes batching possible at all. The
 * difference was measured: eight requests took 5.26 s serialised versus
 * 1.19 s batched at sixteen threads, a 4.4x improvement, with aggregate
 * throughput rising from ~56 tok/s to ~250 tok/s. This is also the shape
 * llama.cpp's own reference server uses, which is the strongest available
 * evidence that it is the supported way to do this.
 */
class LlamaCppBackend final : public QueuedBackend {
  public:
    [[nodiscard]] static auto create(const std::string& model_path, std::size_t max_concurrent,
                                     std::size_t queue_depth, std::size_t n_ctx_per_seq,
                                     int threads) -> std::unique_ptr<LlamaCppBackend> {
        // ggml's load banner and per-tensor detail already go to stderr,
        // which is where this service's own raw-output logging goes and where
        // the container collects logs from, so no llama_log_set call is made
        // to redirect it. Leaving llama.cpp's default in place also means a
        // load failure's real diagnostic reaches the same stream as the
        // failure message logged below it, in order, rather than being split
        // across two sinks.
        llama_backend_init();

        auto model_params = llama_model_default_params();
        // There is no GPU in the deployment target, and asking for layers on
        // one that does not exist is how the sensen path got into trouble.
        // Say CPU explicitly.
        model_params.n_gpu_layers = 0;

        llama_model* model = llama_model_load_from_file(model_path.c_str(), model_params);
        if (model == nullptr) {
            logger::Logger::getInstance().error(
                "llamacpp backend: llama_model_load_from_file failed for {}", model_path);
            llama_backend_free();
            return nullptr;
        }

        auto ctx_params = llama_context_default_params();
        // n_ctx is the TOTAL across sequences: llama.cpp divides it by
        // n_seq_max to get each sequence's window, so a per-sequence budget
        // has to be multiplied up here or every slot silently gets a
        // fraction of the intended context.
        ctx_params.n_ctx = static_cast<std::uint32_t>(n_ctx_per_seq * max_concurrent);
        ctx_params.n_seq_max = static_cast<std::uint32_t>(max_concurrent);
        // n_batch is how many tokens ONE llama_decode call may carry. An
        // admission group can be every waiting request's whole prompt at
        // once, so it is sized for the worst case rather than for a single
        // prompt -- see admit_batch() for why admissions are grouped.
        ctx_params.n_batch = ctx_params.n_ctx;
        // n_ubatch is the micro-batch llama.cpp actually splits that work
        // into, and it is what the compute buffer is sized against, so it is
        // deliberately NOT raised to match n_batch: doing so was measured to
        // double the compute buffer (300 MB to 600 MB) for no throughput.
        // llama.cpp splits an n_batch submission into n_ubatch chunks itself.
        ctx_params.n_ubatch = 512;
        ctx_params.n_threads = threads;
        ctx_params.n_threads_batch = threads;

        llama_context* ctx = llama_init_from_model(model, ctx_params);
        if (ctx == nullptr) {
            logger::Logger::getInstance().error(
                "llamacpp backend: llama_init_from_model failed for {}", model_path);
            llama_model_free(model);
            llama_backend_free();
            return nullptr;
        }

        auto backend = std::unique_ptr<LlamaCppBackend>(
            new LlamaCppBackend(model, ctx, max_concurrent, queue_depth, n_ctx_per_seq));
        backend->start();
        return backend;
    }

    [[nodiscard]] auto name() const noexcept -> std::string_view override { return "llamacpp"; }

    ~LlamaCppBackend() override {
        // worker_ is declared last, so by the time this body runs the owner
        // thread has already been stopped and joined and nothing else can be
        // touching the context or the model.
        if (batch_.token != nullptr) llama_batch_free(batch_);
        if (ctx_ != nullptr) llama_free(ctx_);
        if (model_ != nullptr) llama_model_free(model_);
        llama_backend_free();
    }

  private:
    /** One in-flight request: which KV sequence id it owns, where its cursor
     * is, what it has produced, and who is waiting. */
    struct Sequence {
        std::promise<InferenceOutcome> promise;
        llama_seq_id seq_id = 0;
        llama_pos pos = 0;
        llama_token next_token = 0;
        std::string text;
        std::size_t produced = 0;
        llama_sampler* sampler = nullptr;
    };

    LlamaCppBackend(llama_model* model, llama_context* ctx, std::size_t max_concurrent,
                    std::size_t queue_depth, std::size_t n_ctx_per_seq)
        : model_{model}, ctx_{ctx}, vocab_{llama_model_get_vocab(model)},
          n_ctx_per_seq_{n_ctx_per_seq} {
        max_concurrent_ = max_concurrent;
        max_queue_depth_ = queue_depth;
        // One shared batch, allocated once and refilled in place every step,
        // sized for the largest single submission (a whole prompt) rather
        // than reallocated per iteration.
        batch_ = llama_batch_init(static_cast<std::int32_t>(n_ctx_per_seq * max_concurrent), 0,
                                  static_cast<std::int32_t>(max_concurrent));
        for (std::size_t i = 0; i < max_concurrent; ++i) {
            free_slots_.push_back(static_cast<llama_seq_id>(i));
        }
    }

    auto start() -> void {
        worker_ = std::jthread([this](std::stop_token stoken) { run(stoken); });
    }

    /** Clears the shared batch without freeing it. */
    auto batch_clear() -> void { batch_.n_tokens = 0; }

    /** Appends one token for one sequence to the shared batch. */
    auto batch_add(llama_token token, llama_pos pos, llama_seq_id seq_id, bool want_logits)
        -> void {
        const std::int32_t i = batch_.n_tokens;
        batch_.token[i] = token;
        batch_.pos[i] = pos;
        batch_.n_seq_id[i] = 1;
        batch_.seq_id[i][0] = seq_id;
        batch_.logits[i] = want_logits ? 1 : 0;
        batch_.n_tokens = i + 1;
    }

    auto run(std::stop_token stoken) -> void {
        std::vector<Sequence> active;

        while (!stoken.stop_requested()) {
            const std::size_t free_slots = max_concurrent_ - active.size();
            auto jobs = take_jobs(stoken, free_slots, active.empty());
            admit_batch(jobs, active);

            if (active.empty()) continue;

            if (!decode_step(active)) {
                for (auto& seq : active) {
                    seq.promise.set_value(InferenceOutcome{
                        .ok = false,
                        .text = {},
                        .error = "batched decode step failed inside llama.cpp"});
                    release(seq);
                }
                active.clear();
            }
        }

        for (auto& seq : active) {
            seq.promise.set_value(InferenceOutcome{
                .ok = false, .text = {}, .error = "assistant worker shutting down"});
            release(seq);
        }
        active.clear();
        drain_and_fail("assistant worker shutting down");
    }

    /**
     * Tokenises one prompt, or completes its promise with an error and
     * returns nullopt. Split out from admission so a malformed prompt is
     * rejected before it consumes a KV slot.
     *
     * `add_special` is left to the vocabulary's own answer rather than
     * hard-coded: this fine-tune's GGUF reports add_bos == false because
     * ChatML's `<|im_start|>` already plays that role, and prepending a
     * second BOS would shift every position by one against what the model was
     * trained on. `parse_special` must be true or the ChatML control tokens
     * the prompt is built from would be tokenised as literal text.
     */
    [[nodiscard]] auto tokenise(const std::string& prompt, std::promise<InferenceOutcome>& promise)
        -> std::optional<std::vector<llama_token>> {
        const bool add_special = llama_vocab_get_add_bos(vocab_);
        std::vector<llama_token> tokens(prompt.size() + 8);
        std::int32_t n =
            llama_tokenize(vocab_, prompt.data(), static_cast<std::int32_t>(prompt.size()),
                           tokens.data(), static_cast<std::int32_t>(tokens.size()), add_special,
                           true);
        if (n < 0) {
            tokens.resize(static_cast<std::size_t>(-n));
            n = llama_tokenize(vocab_, prompt.data(), static_cast<std::int32_t>(prompt.size()),
                               tokens.data(), static_cast<std::int32_t>(tokens.size()), add_special,
                               true);
        }
        if (n <= 0) {
            promise.set_value(
                InferenceOutcome{.ok = false, .text = {}, .error = "prompt tokenisation failed"});
            return std::nullopt;
        }
        tokens.resize(static_cast<std::size_t>(n));

        // Refusing an over-long prompt is better than letting llama.cpp
        // silently drop the beginning of it -- which, for a prompt whose
        // first turn is the mandatory system prompt, would quietly turn this
        // fine-tune back into an uninstructed base model.
        if (tokens.size() + kMaxNewTokens > n_ctx_per_seq_) {
            promise.set_value(InferenceOutcome{
                .ok = false,
                .text = {},
                .error = "prompt is too long for the assistant's context window"});
            return std::nullopt;
        }
        return tokens;
    }

    /**
     * Prefills EVERY newly-accepted prompt in ONE `llama_decode` and appends
     * the admitted sequences to `active`.
     *
     * WHY GROUPED RATHER THAN ONE PROMPT AT A TIME:
     *
     * This was measured, not assumed. Admitting eight requests with eight
     * separate prefill decodes cost 1471 ms of the 5716 ms it took to serve
     * eight concurrent users -- roughly 184 ms per prompt, over a quarter of
     * the total, spent re-entering the model eight times for work that is one
     * ragged forward pass. `llama_batch` carries a sequence id per token
     * precisely so several sequences' prompts can share one submission, and
     * the decode loop below already relies on that for the token-by-token
     * phase; there is no reason for admission to be the one place that does
     * not.
     *
     * The group is capped by the batch's own capacity rather than assumed to
     * fit: `n_batch` is sized for every slot's full context, so in practice a
     * group of short chat prompts never comes close, but a caller sending
     * prompts near the per-sequence limit must not silently overflow the
     * batch. Anything that does not fit stays for the next iteration, where
     * it is admitted against a freshly emptied batch.
     */
    auto admit_batch(std::vector<PendingJob>& jobs, std::vector<Sequence>& active) -> void {
        if (jobs.empty()) return;

        struct Admission {
            Sequence seq;
            std::vector<llama_token> tokens;
            std::int32_t logits_row = 0;
        };
        std::vector<Admission> admissions;
        admissions.reserve(jobs.size());

        batch_clear();
        const std::int32_t capacity = static_cast<std::int32_t>(n_ctx_per_seq_ * max_concurrent_);

        for (auto& job : jobs) {
            if (free_slots_.empty()) {
                // Cannot happen while take_jobs() is told how many slots are
                // free, but a promise must never be dropped on the floor, so
                // this is completed rather than assumed unreachable.
                job.promise.set_value(InferenceOutcome{
                    .ok = false, .text = {}, .error = "no free inference slot"});
                continue;
            }
            auto tokens = tokenise(job.prompt, job.promise);
            if (!tokens.has_value()) continue;
            if (batch_.n_tokens + static_cast<std::int32_t>(tokens->size()) > capacity) {
                // Does not fit this group. Re-queueing is not possible from
                // here without reordering ahead of requests that arrived
                // earlier, so this is refused with the same signal a full
                // queue gives -- the caller retries and is admitted into an
                // empty batch.
                job.promise.set_value(InferenceOutcome{
                    .ok = false,
                    .text = {},
                    .error = "the assistant is at capacity; please retry shortly"});
                continue;
            }

            Admission adm;
            adm.seq.promise = std::move(job.promise);
            adm.seq.seq_id = free_slots_.back();
            free_slots_.pop_back();
            // Whatever a previous request left in this slot must go before
            // the new one starts at position zero.
            llama_memory_seq_rm(llama_get_memory(ctx_), adm.seq.seq_id, -1, -1);

            for (std::size_t i = 0; i < tokens->size(); ++i) {
                batch_add((*tokens)[i], static_cast<llama_pos>(i), adm.seq.seq_id,
                          i + 1 == tokens->size());
            }
            // The row this sequence's next-token logits will land in: the
            // last token it contributed, which is the only one flagged for
            // logits above.
            adm.logits_row = batch_.n_tokens - 1;
            adm.seq.pos = static_cast<llama_pos>(tokens->size());
            adm.tokens = std::move(*tokens);
            admissions.push_back(std::move(adm));
        }

        if (admissions.empty()) return;

        if (llama_decode(ctx_, batch_) != 0) {
            logger::Logger::getInstance().error("llamacpp backend: batched prefill failed");
            for (auto& adm : admissions) {
                free_slots_.push_back(adm.seq.seq_id);
                adm.seq.promise.set_value(InferenceOutcome{
                    .ok = false, .text = {}, .error = "prompt prefill failed"});
            }
            return;
        }

        for (auto& adm : admissions) {
            // Greedy for the same reason the sensen path is greedy:
            // structured extraction, not creative generation. One sampler per
            // sequence because a sampler carries per-sequence accepted-token
            // state.
            adm.seq.sampler = llama_sampler_init_greedy();
            adm.seq.next_token = llama_sampler_sample(adm.seq.sampler, ctx_, adm.logits_row);
            llama_sampler_accept(adm.seq.sampler, adm.seq.next_token);
            active.push_back(std::move(adm.seq));
        }
    }

    /** Advances every in-flight sequence by one token in one shared batch. */
    [[nodiscard]] auto decode_step(std::vector<Sequence>& active) -> bool {
        batch_clear();
        for (auto& seq : active) {
            batch_add(seq.next_token, seq.pos, seq.seq_id, true);
        }
        if (llama_decode(ctx_, batch_) != 0) {
            logger::Logger::getInstance().error("llamacpp backend: llama_decode failed");
            return false;
        }

        std::vector<Sequence> still_active;
        still_active.reserve(active.size());
        for (std::size_t i = 0; i < active.size(); ++i) {
            Sequence& seq = active[i];
            const llama_token consumed = seq.next_token;
            seq.pos += 1;

            bool stop = llama_vocab_is_eog(vocab_, consumed);
            if (!stop) {
                seq.text += piece(consumed);
                if (++seq.produced >= kMaxNewTokens) stop = true;
            }
            if (stop) {
                finish(seq);
                continue;
            }
            seq.next_token = llama_sampler_sample(seq.sampler, ctx_, static_cast<std::int32_t>(i));
            llama_sampler_accept(seq.sampler, seq.next_token);
            still_active.push_back(std::move(seq));
        }
        active = std::move(still_active);
        return true;
    }

    /** Detokenises one token id to its UTF-8 piece. */
    [[nodiscard]] auto piece(llama_token token) const -> std::string {
        std::string out(16, '\0');
        std::int32_t n = llama_token_to_piece(vocab_, token, out.data(),
                                              static_cast<std::int32_t>(out.size()), 0, true);
        if (n < 0) {
            out.resize(static_cast<std::size_t>(-n));
            n = llama_token_to_piece(vocab_, token, out.data(),
                                     static_cast<std::int32_t>(out.size()), 0, true);
        }
        if (n < 0) return {};
        out.resize(static_cast<std::size_t>(n));
        return out;
    }

    auto finish(Sequence& seq) -> void {
        seq.promise.set_value(
            InferenceOutcome{.ok = true, .text = std::move(seq.text), .error = {}});
        release(seq);
    }

    /** Returns a sequence's KV window and slot so a waiting request can use it. */
    auto release(Sequence& seq) -> void {
        if (seq.sampler != nullptr) {
            llama_sampler_free(seq.sampler);
            seq.sampler = nullptr;
        }
        llama_memory_seq_rm(llama_get_memory(ctx_), seq.seq_id, -1, -1);
        free_slots_.push_back(seq.seq_id);
    }

    llama_model* model_ = nullptr;
    llama_context* ctx_ = nullptr;
    const llama_vocab* vocab_ = nullptr;
    std::size_t n_ctx_per_seq_ = 0;
    llama_batch batch_{};
    // Touched only by the owner thread, so it needs no lock.
    std::vector<llama_seq_id> free_slots_;

    // Declared LAST, for the same destruction-ordering reason as the sensen
    // backend: the thread must be stopped and joined before the context,
    // model or batch it uses are freed.
    std::jthread worker_;
};

#endif  // ASSISTANT_HAVE_LLAMACPP

// ---------------------------------------------------------------------------
// Backend selection
// ---------------------------------------------------------------------------

/**
 * Chooses a backend at startup and owns it for the process lifetime.
 *
 * `ASSISTANT_BACKEND` selects: `sensen` (the default) or `llamacpp`. The
 * selection is deliberately NOT silently corrective. If a backend is asked for
 * by name and cannot be initialised, this does not quietly start the other
 * one: a service that answers with a different engine than the operator asked
 * for, without saying so, is how a "fixed" incident turns out days later to
 * have been running the wrong thing all along. It logs the failure and leaves
 * the assistant unavailable, which the RPC layer reports as
 * MODEL_UNAVAILABLE -- an honest refusal, which this product prefers to a
 * guess in every other place it makes this choice too.
 *
 * The one automatic fallback is narrow and announced: asking for `llamacpp` in
 * a build that was compiled without it is a configuration mistake rather than
 * a runtime failure, and it is reported as such.
 */
class AssistantWorker {
  public:
    [[nodiscard]] static auto instance() -> AssistantWorker& {
        static AssistantWorker worker;
        return worker;
    }

    /** True iff a backend initialised successfully at process start.
     * Immutable after construction, so no synchronization is needed. */
    [[nodiscard]] auto available() const noexcept -> bool { return backend_ != nullptr; }

    [[nodiscard]] auto submit(std::string prompt) -> std::optional<InferenceOutcome> {
        if (backend_ == nullptr) {
            // Defense in depth: the RPC handler is expected to check
            // available() first, but if this is ever reached anyway there is
            // no owner thread to fulfil a queued job's promise -- returning a
            // populated failure here, rather than enqueueing, is what stands
            // between this and a permanent hang.
            return InferenceOutcome{.ok = false, .text = {}, .error = "model not loaded"};
        }
        return backend_->submit(std::move(prompt));
    }

    AssistantWorker(const AssistantWorker&) = delete;
    auto operator=(const AssistantWorker&) -> AssistantWorker& = delete;
    AssistantWorker(AssistantWorker&&) = delete;
    auto operator=(AssistantWorker&&) -> AssistantWorker& = delete;
    ~AssistantWorker() = default;

  private:
    AssistantWorker() {
        const auto path = env_string("MODEL_PATH");
        if (!path.has_value()) {
            logger::Logger::getInstance().warn(
                "MODEL_PATH is not set -- the strategy assistant will return a Refusal on "
                "every call. The calculator and finance services are unaffected.");
            return;
        }

        // How many threads the ENGINE may fan a single forward pass across
        // internally. This is a different axis from how many requests are in
        // flight: one owner thread drives the batch, and this is how wide
        // each of its steps is allowed to go.
        const int threads = env_positive_int("ASSISTANT_INFERENCE_THREADS", 4);

        // How many requests may decode simultaneously, and how many may wait
        // for a slot. See SensenBackend's measured throughput/latency table
        // for why the default is deliberately modest.
        const auto max_concurrent =
            static_cast<std::size_t>(env_positive_int("ASSISTANT_MAX_CONCURRENT", 4));
        const auto queue_depth =
            static_cast<std::size_t>(env_positive_int("ASSISTANT_QUEUE_DEPTH", 8));

        // Per-sequence context window. The original 1024 was a guess that
        // happened to hold; this model's GGUF declares a trained context of
        // 40960, so 1024 was never a model limit, only an allocation choice.
        // It was then raised to 2048 against an F32 KV cache.
        //
        // The KV cache is now F16 (SENSEN_KV_DTYPE, default fp16), which halves
        // the per-token cost from 224 KiB to 112 KiB:
        //
        //     2 (K and V) * 28 layers * 8 kv_heads * 128 head_dim * 2 bytes
        //
        // so 4096 tokens at F16 costs exactly what 2048 cost at F32 -- 448 MiB
        // per slot, 1.75 GiB across the four concurrent slots. This raise is
        // therefore free against a budget that was already accepted, which is
        // the whole reason the KV width was worth halving.
        //
        // It stops at 4096 rather than the trained 40960 because the cache is
        // sized per concurrent slot and nothing this service sends approaches
        // even 2048 -- the system prompt plus a user turn plus kMaxNewTokens,
        // with room for a prior clarification turn. 4096 is headroom, not a
        // requirement; the measured cost of going further is linear and the
        // justification for paying it does not exist yet.
        //
        // Note the ceiling is a cap, not an allocation: production runs the
        // PAGED cache, which commits 16-token blocks on demand, so a short
        // conversation's resident KV tracks its real length, not this number.
        const auto kv_max_seq_len =
            static_cast<std::size_t>(env_positive_int("ASSISTANT_CONTEXT_TOKENS", 4096));

        const std::string requested = env_string("ASSISTANT_BACKEND").value_or("sensen");

        if (requested == "sensen") {
            backend_ = SensenBackend::create(*path, max_concurrent, queue_depth, kv_max_seq_len,
                                             threads);
        } else if (requested == "llamacpp") {
#ifdef ASSISTANT_HAVE_LLAMACPP
            backend_ = LlamaCppBackend::create(*path, max_concurrent, queue_depth, kv_max_seq_len,
                                               threads);
#else
            logger::Logger::getInstance().error(
                "ASSISTANT_BACKEND=llamacpp was requested, but this binary was built without "
                "the llama.cpp backend (ENABLE_LLAMACPP_BACKEND was OFF at configure time). "
                "The strategy assistant will return a Refusal on every call; rebuild with the "
                "backend enabled or set ASSISTANT_BACKEND=sensen.");
            return;
#endif
        } else {
            logger::Logger::getInstance().error(
                "ASSISTANT_BACKEND=\"{}\" is not a backend this build knows about (expected "
                "\"sensen\" or \"llamacpp\") -- the strategy assistant will return a Refusal on "
                "every call rather than silently guessing which engine was meant.",
                requested);
            return;
        }

        if (backend_ == nullptr) {
            logger::Logger::getInstance().error(
                "The \"{}\" assistant backend failed to initialise from MODEL_PATH ({}) -- the "
                "strategy assistant will return a Refusal on every call. The calculator and "
                "finance services are unaffected.",
                requested, *path);
            return;
        }

        logger::Logger::getInstance().info(
            "Strategy assistant ready: backend={} model={} inference_threads={} "
            "max_concurrent={} queue_depth={} context_tokens={}",
            backend_->name(), *path, threads, max_concurrent, queue_depth, kv_max_seq_len);
    }

    std::unique_ptr<InferenceBackend> backend_;
};

// ---------------------------------------------------------------------------
// Output interpretation: <params> / clarifying question / refusal
// ---------------------------------------------------------------------------

[[nodiscard]] auto trim(std::string_view s) -> std::string {
    std::size_t start = 0;
    std::size_t end = s.size();
    while (start < end && std::isspace(static_cast<unsigned char>(s[start])) != 0) ++start;
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])) != 0) --end;
    return std::string{s.substr(start, end - start)};
}

/** Removes a leading `<think>...</think>` block, returning whatever follows.
 *
 * Qwen3 emits one on every response. On this fine-tune a correct answer is
 * `<think>\n\n</think>\n\n<params>{...}</params>` -- the block is present and
 * empty -- so its presence carries no information and cannot be used to detect
 * anything. Only what comes AFTER it is the model's actual answer.
 *
 * An unterminated `<think>` (the model hit the token ceiling mid-reasoning)
 * leaves nothing to interpret, so everything is dropped and the caller falls
 * through to its refusal path rather than showing a trader a severed reasoning
 * trace. Text with no `<think>` at all is returned unchanged. */
[[nodiscard]] auto strip_think_block(std::string_view text) -> std::string {
    constexpr std::string_view kOpen = "<think>";
    constexpr std::string_view kClose = "</think>";

    const auto open = text.find(kOpen);
    if (open == std::string_view::npos) return std::string{text};

    const auto close = text.find(kClose, open + kOpen.size());
    if (close == std::string_view::npos) return {};

    std::string out{text.substr(0, open)};
    out += text.substr(close + kClose.size());
    return out;
}

/** Extracts the text between `<params>` and `</params>`, if both are present
 * in that order. A missing or unclosed tag (e.g. the model was cut off by
 * kMaxNewTokens mid-object) yields nullopt, which the caller treats as "this
 * was not a params response" rather than attempting to parse a fragment. */
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

/** A symbol this check can vouch for the SHAPE of only -- that it looks like
 * a ticker rather than a sentence fragment the model failed to extract
 * cleanly. It says nothing about whether the symbol names a real,
 * currently-listed instrument; that is `probe_symbol`'s job, below, which
 * runs against live market data once a candidate has passed this shape check
 * and every other free (non-network) validation in
 * `validate_and_populate_params`. Splitting the two matters for cost: a
 * malformed fragment should never spend a network round trip finding that
 * out. */
[[nodiscard]] auto looks_like_a_ticker(std::string_view s) noexcept -> bool {
    if (s.empty() || s.size() > kMaxSymbolLength) return false;
    return std::ranges::all_of(s, [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-';
    });
}

[[nodiscard]] auto is_known_asset_class(std::string_view s) noexcept -> bool {
    return s == "EQUITY" || s == "FUTURES" || s == "CRYPTO";
}

auto populate_refusal(calculator::assistant::ParseResponse& response,
                       calculator::assistant::Refusal_Reason reason, std::string message) -> void {
    auto* refusal = response.mutable_refusal();
    refusal->set_reason(reason);
    refusal->set_message(std::move(message));
}

/** Mirrors `populate_refusal` for the other successful-but-not-params
 * outcome. Kept as its own function, rather than inlined at each call site,
 * for the same reason `populate_refusal` is: one place that knows which
 * field of the `oneof` a clarifying question belongs in. */
auto populate_clarification(calculator::assistant::ParseResponse& response, std::string question) -> void {
    response.mutable_clarification()->set_question(std::move(question));
}

// ---------------------------------------------------------------------------
// GP-ARA mandatory verification stage
// ---------------------------------------------------------------------------
//
// assistant_verification.cppm's `verify::ReasonCode` deliberately picks among
// the SAME `Refusal::Reason` values the per-field checks above already use --
// it invents nothing new on the wire. `ReasonCode::None` only ever
// accompanies `Outcome::Proven`, which this function is never called for
// (see the switch below); reaching it here would be this file's own bug, not
// a verification-layer question, so it default-denies to OUT_OF_SCOPE rather
// than emitting an unspecified reason.
[[nodiscard]] auto map_verification_reason(::options_calculator::assistant::verify::ReasonCode reason)
    -> calculator::assistant::Refusal_Reason {
    using ::options_calculator::assistant::verify::ReasonCode;
    switch (reason) {
        case ReasonCode::UnsupportedStrategy:
            return calculator::assistant::Refusal::UNSUPPORTED_STRATEGY;
        case ReasonCode::UnknownSymbol:
            return calculator::assistant::Refusal::UNKNOWN_SYMBOL;
        case ReasonCode::OutOfScope:
        case ReasonCode::None:
            return calculator::assistant::Refusal::OUT_OF_SCOPE;
    }
    return calculator::assistant::Refusal::OUT_OF_SCOPE;
}

// ---------------------------------------------------------------------------
// Live symbol validation
// ---------------------------------------------------------------------------
//
// Everything above this point validates the model's OWN output against
// itself -- its shape, its catalogue membership. Nothing above it asks
// whether the world agrees. This section does: it is what stands between a
// symbol the model invented (measured ground truth: "321 crack spread on
// crude 45 days" produced "CND", which is not a real instrument) and that
// symbol reaching the calculator as a confident wrong answer.

/**
 * Futures roots this product's OWN catalogue recognizes -- see the root
 * README's Interactive Symbol Selector feature list: "Futures (ES, NQ, CL,
 * GC, ZB)". This list exists because `market_data` has no futures quote path
 * at all: `AlpacaProvider::quote` only ever calls Alpaca's EQUITY snapshot
 * endpoint (`/v2/stocks/{symbol}/snapshot`), so there is no live feed this
 * function could probe to confirm "NQ" is a real futures root the way it can
 * confirm "NVDA" is a real equity. Catalogue membership is the only
 * available signal, so it is used deliberately -- not as a stand-in for a
 * live check this function is too lazy to do.
 *
 * This is the product's full catalogue, not the fine-tuned model's narrower
 * training distribution (measured ground truth: the training data covered
 * only ES and NQ, which is WHY every other commodity root is
 * out-of-distribution and prone to exactly the CND-style hallucination this
 * change exists to catch). Using the broader, checked-in catalogue rather
 * than hardcoding just {ES, NQ} means a future retraining that teaches the
 * model CL, GC or ZB does not also require a change here, and a model that
 * already happens to emit a correct "CL" today (the measured example's own
 * stated correct answer) is accepted rather than refused on a technicality.
 */
constexpr std::array<std::string_view, 5> kKnownFuturesRoots{"ES", "NQ", "CL", "GC", "ZB"};

/** Crypto symbols this product's catalogue recognizes ("Crypto (BTC, ETH)").
 * Same rationale as `kKnownFuturesRoots`: `market_data` has no crypto feed
 * either, so catalogue membership is the only signal this function has. */
constexpr std::array<std::string_view, 2> kKnownCryptoSymbols{"BTC", "ETH"};

template <std::size_t N>
[[nodiscard]] auto in_catalogue(const std::array<std::string_view, N>& catalogue,
                                 std::string_view symbol) noexcept -> bool {
    return std::ranges::any_of(catalogue, [symbol](std::string_view s) { return s == symbol; });
}

/**
 * What a `fetch_quote` failure means for THIS purpose: is the symbol simply
 * not a real instrument, or did the check itself never get a good answer?
 *
 * `HttpError` and `MissingData` are what a genuinely unknown ticker looks
 * like -- Alpaca answered (its snapshot endpoint 404s an unlisted symbol,
 * mapped to `HttpError` by `get_text` in market_data.cppm; or 200s with an
 * empty book, mapped to `MissingData` by `AlpacaProvider::quote`), and the
 * answer is "no, this is not a real equity". Every other code
 * `fetch_quote`/`AlpacaProvider::quote` can return -- `NetworkError` (could
 * not reach Alpaca inside the shared client's own connect/read timeout, see
 * `client_for` in market_data.cppm), `NotConfigured` (ALPACA_API_KEY/SECRET
 * unset), `ParseError` (Alpaca answered with something this engine could not
 * read) -- is THIS SERVICE failing to ask the question, not the market
 * answering "no". Conflating the two would turn a transient provider outage
 * into a false claim that the trader's symbol does not exist, which is
 * exactly the class of confident wrong answer this whole change exists to
 * prevent -- just aimed at the validator itself instead of at the model.
 *
 * Compares by category name and raw value rather than writing
 * `ec == market_data::MarketDataError::HttpError`, the usual `std::error_code`
 * idiom: that idiom needs `market_data::make_error_code` and the
 * `std::is_error_code_enum` specialization market_data.cppm defines for it to
 * be reachable from OUTSIDE that module, and neither one is declared
 * `export` there. Whether an unexported specialization stays reachable
 * across a module boundary is exactly the kind of standard's-corner-case
 * this file should not gamble a compile (or worse, a silently-disabled
 * comparison) on. `MarketDataError` itself IS exported, so comparing its
 * underlying value against `std::error_code`'s own plain, non-modular
 * accessors (`value()`, `category().name()`) is correct without depending on
 * that question at all.
 */
[[nodiscard]] auto is_market_data_error(const std::error_code& ec,
                                         market_data::MarketDataError e) noexcept -> bool {
    return ec.value() == static_cast<int>(e) &&
           std::string_view{ec.category().name()} == "market_data";
}

[[nodiscard]] auto symbol_definitely_absent(const std::error_code& ec) noexcept -> bool {
    return is_market_data_error(ec, market_data::MarketDataError::HttpError) ||
           is_market_data_error(ec, market_data::MarketDataError::MissingData);
}

/** The four ways probing a (symbol, asset_class) pair against live market
 * data can come out. See `probe_symbol`'s own doc comment for what each one
 * means and why it is not just a two-way resolves/doesn't-resolve split. */
enum class SymbolProbeOutcome { Resolved, Unknown, AssetClassMismatch, ProviderUnavailable };

/**
 * Probes the model-extracted (symbol, asset_class) pair against the live
 * market-data layer. This is the network call the design brief calls out by
 * name: it must not fabricate an answer on failure, and it must not block
 * indefinitely.
 *
 * ON THE TIMEOUT: this deliberately makes ONE call to the existing
 * `market_data::fetch_quote`, which is already bounded by `client_for`'s
 * explicit `set_connection_timeout(3, 0)` / `set_read_timeout(10, 0)` (see
 * market_data.cppm) -- the same bound every other live quote in this engine
 * (GetMarketQuote, the option chain, expirations) already trusts. No second,
 * shorter timeout is layered on top here. A `std::async` + `wait_for` racing
 * that same call would not actually cancel it -- httplib's blocking `Get`
 * has no cooperative cancellation once the request is in flight -- so
 * "timing out" sooner would only stop THIS call from waiting; the socket and
 * the thread behind it would keep running for up to the real ~13s bound
 * regardless, now leaked rather than joined. That trade -- a shorter-looking
 * timeout that is actually a leak -- is worse than the one real, honest
 * bound market_data.cppm already establishes, so this function relies on it
 * instead of reinventing it.
 *
 * ON WHY A RESOLVING QUOTE IS NOT ALWAYS A PASS: `market_data`'s only live
 * feed is Alpaca's EQUITY snapshot -- there is no futures or crypto quote
 * path anywhere in this engine (see market_data.cppm's file banner and
 * `kKnownFuturesRoots`'s doc comment above). That is a hard boundary this
 * function has to reason around, not a gap it papers over with a new HTTP
 * client (explicitly out of scope for this change):
 *
 *   - An EQUITY claim is checked directly: a resolving quote for the literal
 *     symbol IS the confirmation this claim needs.
 *   - A FUTURES or CRYPTO claim can never be CONFIRMED by this feed --
 *     Alpaca has an opinion on whether "ES" is a real stock, never on
 *     whether it is a real futures root. So a RESOLVING quote under a
 *     non-EQUITY claim is not a pass; it means the literal symbol names a
 *     different, CONFIRMED-real instrument than the one the model claimed
 *     ("ES" is Eversource Energy's listed stock right now, whatever else it
 *     might also be) -- the ambiguity this product has hit before (see the
 *     memory note on the ES collision) -- and it is surfaced as a
 *     Clarification rather than a silent pick either way. A NON-resolving
 *     quote under a non-EQUITY claim falls back to catalogue membership
 *     (`kKnownFuturesRoots` / `kKnownCryptoSymbols`) -- the only remaining
 *     signal available -- because the alternative is accepting every string
 *     the model ever emits for those two classes, which is precisely the
 *     hole "CND" fell through.
 */
[[nodiscard]] auto probe_symbol(const std::string& symbol, const std::string& asset_class)
    -> SymbolProbeOutcome {
    const auto quote = market_data::fetch_quote(symbol);
    if (quote.has_value()) {
        return (asset_class == "EQUITY") ? SymbolProbeOutcome::Resolved
                                          : SymbolProbeOutcome::AssetClassMismatch;
    }

    if (!symbol_definitely_absent(quote.error())) {
        return SymbolProbeOutcome::ProviderUnavailable;
    }

    if (asset_class == "EQUITY") return SymbolProbeOutcome::Unknown;
    if (asset_class == "FUTURES") {
        return in_catalogue(kKnownFuturesRoots, symbol) ? SymbolProbeOutcome::Resolved
                                                         : SymbolProbeOutcome::Unknown;
    }
    // The only value `is_known_asset_class` still allows here is "CRYPTO".
    return in_catalogue(kKnownCryptoSymbols, symbol) ? SymbolProbeOutcome::Resolved
                                                      : SymbolProbeOutcome::Unknown;
}

/** "FUTURES" / "CRYPTO" as the noun a trader would recognize in a sentence,
 * for the Clarification message below. `probe_symbol` only ever returns
 * `AssetClassMismatch` for these two asset classes (a mismatch against
 * EQUITY is impossible: EQUITY is confirmed by the exact same live lookup
 * that would produce the mismatch), so the EQUITY branch here is unreachable
 * in practice and exists only so this function is total. */
[[nodiscard]] auto asset_class_noun(std::string_view asset_class) noexcept -> std::string_view {
    if (asset_class == "FUTURES") return "futures";
    if (asset_class == "CRYPTO") return "crypto";
    return "equity";
}

/**
 * Validates a decoded `<params>` JSON object against the proto's own field
 * contract, against the 47-entry catalogue this backend actually knows how
 * to price, and -- last, because it is the only step that costs a network
 * round trip -- against live market data via `probe_symbol`. Populates
 * EXACTLY ONE of `response.params` (every field present, well-typed, in
 * bounds, AND the symbol independently confirmed real), `response.refusal`,
 * or `response.clarification`: there is no partial-success case, because a
 * strategy the pricing engine cannot fully resolve, or a symbol this RPC
 * cannot confirm is real, is not a request it should hand downstream.
 *
 * Reason-code mapping, since Refusal::Reason has exactly four non-zero
 * values and several distinct failure shapes below must share them: a
 * missing or malformed symbol/asset_class field, or a symbol the live probe
 * could not find at all, maps to UNKNOWN_SYMBOL (its own doc comment already
 * covers "does not resolve... including genuinely ambiguous"); an
 * unrecognised strategy id maps to UNSUPPORTED_STRATEGY, exactly as
 * specified; everything else that makes the JSON block unusable as a trade
 * request -- a parse failure, a missing numeric field, or a numeric field
 * outside sane bounds -- maps to OUT_OF_SCOPE, the closest fit among the
 * four for "this could not become a valid options/futures strategy
 * request." The live probe's own two failure shapes are handled separately,
 * below, because neither one is a shape this mapping already covers: an
 * asset-class mismatch is a Clarification, not a Refusal (see
 * `probe_symbol`'s doc comment for why silently picking one side would be
 * fabricating market identity), and a provider outage reuses
 * MODEL_UNAVAILABLE rather than OUT_OF_SCOPE or UNKNOWN_SYMBOL, because
 * unlike those two it says nothing false about the trader's own request. In
 * every case the specific reason is still fully legible in `message`, which
 * is exactly what the message/reason split in the proto exists for.
 *
 * NOTE for a future reader of assistant.proto: `Refusal::UNKNOWN_SYMBOL`'s
 * own doc comment still cites the ES ambiguity as an example of THIS reason.
 * That comment now describes stale behaviour -- this function deliberately
 * routes that exact case to Clarification instead (see `probe_symbol`).
 * Updating the proto comment was left out of this change because it was
 * scoped to this file; whoever next touches assistant.proto should reconcile
 * the two.
 *
 * Between the per-field checks above and `probe_symbol` below sits one more
 * MANDATORY stage: `assistant_verification::verify_assistant_params`
 * (GP-ARA). The per-field checks that precede it each look at one field in
 * isolation and cannot catch a hallucination that is plausible field-by-
 * field but self-contradictory as a whole; `probe_symbol` after it confirms
 * a symbol is real against live data but has no opinion on, say, whether a
 * futures-only strategy paired with a crypto symbol makes sense. See that
 * call site's own comment for why it runs where it does, and
 * assistant_verification.cppm's file banner for the full rule set.
 */
auto validate_and_populate_params(std::string_view json_text,
                                   calculator::assistant::ParseResponse& response) -> void {
    auto parsed = fastjson::parse(json_text);
    if (!parsed.has_value() || !parsed->is_object()) {
        populate_refusal(response, calculator::assistant::Refusal::OUT_OF_SCOPE,
                         "The assistant's structured output could not be parsed as a JSON object.");
        return;
    }
    const auto& obj = parsed.value();

    if (!obj.contains("symbol") || !obj["symbol"].is_string()) {
        populate_refusal(response, calculator::assistant::Refusal::UNKNOWN_SYMBOL,
                         "The assistant did not name a symbol for this request.");
        return;
    }
    const std::string symbol{obj["symbol"].as_string()};
    if (!looks_like_a_ticker(symbol)) {
        populate_refusal(response, calculator::assistant::Refusal::UNKNOWN_SYMBOL,
                         "\"" + symbol + "\" does not look like a real ticker.");
        return;
    }

    if (!obj.contains("asset_class") || !obj["asset_class"].is_string()) {
        populate_refusal(response, calculator::assistant::Refusal::UNKNOWN_SYMBOL,
                         "The assistant did not resolve an asset class for \"" + symbol + "\".");
        return;
    }
    const std::string asset_class{obj["asset_class"].as_string()};
    if (!is_known_asset_class(asset_class)) {
        populate_refusal(response, calculator::assistant::Refusal::UNKNOWN_SYMBOL,
                         "\"" + asset_class + "\" is not a supported asset class.");
        return;
    }

    if (!obj.contains("strategy") || !obj["strategy"].is_string()) {
        populate_refusal(response, calculator::assistant::Refusal::UNSUPPORTED_STRATEGY,
                         "The assistant did not name a strategy for this request.");
        return;
    }
    const std::string strategy{obj["strategy"].as_string()};
    if (!::options_calculator::strategy::is_known(strategy)) {
        populate_refusal(response, calculator::assistant::Refusal::UNSUPPORTED_STRATEGY,
                         "\"" + strategy + "\" is not one of the strategies this calculator prices.");
        return;
    }

    if (!obj.contains("expiration_days") || !obj["expiration_days"].is_number()) {
        populate_refusal(response, calculator::assistant::Refusal::OUT_OF_SCOPE,
                         "The assistant did not give an expiration for this request.");
        return;
    }
    const std::int64_t expiration_days = obj["expiration_days"].as_int64();
    if (expiration_days < kMinExpirationDays || expiration_days > kMaxExpirationDays) {
        populate_refusal(response, calculator::assistant::Refusal::OUT_OF_SCOPE,
                         "The assistant's expiration (" + std::to_string(expiration_days) +
                             " days) is out of a sane range.");
        return;
    }

    if (!obj.contains("quantity") || !obj["quantity"].is_number()) {
        populate_refusal(response, calculator::assistant::Refusal::OUT_OF_SCOPE,
                         "The assistant did not give a quantity for this request.");
        return;
    }
    const std::int64_t quantity = obj["quantity"].as_int64();
    if (quantity < kMinQuantity || quantity > kMaxQuantity) {
        populate_refusal(response, calculator::assistant::Refusal::OUT_OF_SCOPE,
                         "The assistant's quantity (" + std::to_string(quantity) +
                             ") is out of a sane range.");
        return;
    }

    // ------------------------------------------------------------------
    // MANDATORY GP-ARA verification. Every field above this point was
    // checked in ISOLATION -- well-formed on its own, never against each
    // other. This is the gate that catches a hallucination that is
    // plausible field-by-field but self-contradictory as a whole (a known
    // futures root tagged asset_class=EQUITY, a futures-only strategy
    // paired with a crypto symbol, a calendar spread asked to live inside
    // one expiration_days field -- see assistant_verification.cppm's file
    // banner for the full rule set and why it needs no SMT solver to
    // decide them). It runs BEFORE the live market-data probe below,
    // deliberately: a symbol/asset_class contradiction that a live equity
    // lookup could paper over (crude oil's "CL" root also happens to be
    // Colgate-Palmolive's real NYSE ticker) must never get the chance to
    // resolve against the wrong market. Nothing computed here is network-
    // dependent -- see the measured latency in test_assistant_verification.cpp,
    // negligible next to the ~1.1s the model call itself costs -- so paying
    // it before the network probe costs nothing extra on the refused path
    // and saves a wasted round trip on it.
    //
    // The tri-state verdict is preserved end to end, never collapsed to a
    // boolean: Proven falls through to the live probe below (the only path
    // that can still reach `response.mutable_params()`); Unsafe and
    // Indeterminate both refuse immediately, via the exact same
    // `map_verification_reason()` -> `populate_refusal()` call, because
    // treating "definitely contradictory" and "this verifier could not
    // decide" identically at the response boundary is the correct,
    // fail-closed default (see assistant_verification.cppm's own comment on
    // why Indeterminate maps to a refusal here rather than a clarification:
    // the one case that can produce it is a gap between this file's rule
    // table and strategy_catalogue.cppm, not an ambiguity in what the
    // trader said that a follow-up question could resolve).
    const auto verification_start = std::chrono::steady_clock::now();
    const auto verdict = ::options_calculator::assistant::verify::verify_assistant_params(
        {.symbol = symbol,
         .asset_class = asset_class,
         .strategy = strategy,
         .expiration_days = expiration_days,
         .quantity = quantity});
    const auto verification_us = std::chrono::duration_cast<std::chrono::microseconds>(
                                      std::chrono::steady_clock::now() - verification_start)
                                      .count();
    logger::Logger::getInstance().debug(
        "assistant: GP-ARA verification took {}us, outcome={}", verification_us,
        verdict.outcome == ::options_calculator::assistant::verify::Outcome::Proven ? "Proven"
        : verdict.outcome == ::options_calculator::assistant::verify::Outcome::Unsafe ? "Unsafe"
                                                                                        : "Indeterminate");

    if (verdict.outcome != ::options_calculator::assistant::verify::Outcome::Proven) {
        populate_refusal(response, map_verification_reason(verdict.reason),
                         verdict.message.empty()
                             ? "The assistant's answer could not be verified."
                             : verdict.message);
        return;
    }

    // Every check above this line is free (string comparisons, catalogue
    // lookups, and the GP-ARA cross-field verification above); this one
    // costs a network round trip, so it runs LAST -- a request that was
    // always going to be refused for its strategy, its expiration, its
    // quantity, or a cross-field contradiction should not also pay for a
    // live quote it will never use.
    switch (probe_symbol(symbol, asset_class)) {
        case SymbolProbeOutcome::Unknown:
            populate_refusal(response, calculator::assistant::Refusal::UNKNOWN_SYMBOL,
                             "I could not find a tradeable instrument for '" + symbol +
                                 "'. Which symbol did you mean?");
            return;
        case SymbolProbeOutcome::AssetClassMismatch:
            // Never picked silently: see probe_symbol's own doc comment for
            // why a resolving quote under a FUTURES/CRYPTO claim is
            // evidence of a DIFFERENT real instrument, not a pass.
            populate_clarification(
                response, "\"" + symbol +
                              "\" is a real, listed equity ticker, and also a name traders use "
                              "for a " + std::string{asset_class_noun(asset_class)} +
                              " instrument -- I can't tell which you meant. Did you mean the "
                              "equity, or the " + std::string{asset_class_noun(asset_class)} + "?");
            return;
        case SymbolProbeOutcome::ProviderUnavailable:
            // A Refusal, not a Clarification -- deliberately. The trader
            // said nothing ambiguous here; the market-data backend could
            // not be reached, or answered with something this engine could
            // not read, inside its own timeout (see probe_symbol's doc
            // comment). No question this RPC could ask would let the trader
            // resolve THAT -- "which symbol did you mean" only makes sense
            // when the ambiguity is in their words, not in this service's
            // ability to check them right now.
            //
            // DATA_UNAVAILABLE, not MODEL_UNAVAILABLE. This originally reused
            // MODEL_UNAVAILABLE on the reasoning that its contract -- the RPC
            // succeeded, a backend behind it did not -- fits the market-data
            // backend as well as the LLM one. That is true and still cost real
            // time, because the two demand different responses: one says check
            // the model, the other says check the market-data credentials.
            // With ALPACA_API_KEY unset, a fully loaded and correctly
            // generating assistant answered MODEL_UNAVAILABLE, and the smoke
            // gate -- which reads that code as "this environment ships no
            // model" and skips -- reported the model absent while it was
            // working. A code that cannot distinguish those two states is not
            // carrying information; it is losing it.
            populate_refusal(response, calculator::assistant::Refusal::DATA_UNAVAILABLE,
                             "Live market data is temporarily unavailable, so \"" + symbol +
                                 "\" could not be verified. Please try again shortly.");
            return;
        case SymbolProbeOutcome::Resolved:
            break;
    }

    auto* params = response.mutable_params();
    params->set_symbol(symbol);
    params->set_asset_class(asset_class);
    params->set_strategy(strategy);
    params->set_expiration_days(static_cast<std::int32_t>(expiration_days));
    params->set_quantity(static_cast<std::int32_t>(quantity));
}

/**
 * Classifies the model's raw decoded text into one of the three proto
 * outcomes. This is where "clarification is an OK status, not an error" is
 * actually decided: a short plain-text question is exactly the model doing
 * its job (see assistant.proto's file banner), so it is written straight
 * into `response.clarification` with no error path taken at all -- the RPC
 * succeeded, it just succeeded by asking something back instead of finishing
 * the parse.
 */
auto interpret_model_output(const std::string& raw_text,
                             calculator::assistant::ParseResponse& response) -> void {
    // Qwen3 emits a `<think>` block on EVERY response, including the ones that
    // are exactly right. Measured against this fine-tune, a correct answer looks
    // like:
    //
    //     <think>\n\n</think>\n\n<params>{"symbol":"SPY",...}</params>
    //
    // so the block being present says nothing, and the block being EMPTY is the
    // signal the system prompt took. This guard previously refused on the mere
    // presence of the tag, which rejected every correct answer the model
    // produced -- the same prompts that parse cleanly through sensen's own
    // server were refused here with "could not produce a usable response".
    // Caught by running a real request through this RPC rather than through the
    // model directly; nothing short of that would have shown it.
    //
    // The guard's actual intent is still served, and still worth serving: a
    // reasoning trace must never reach a trader. So the block is stripped and
    // whatever follows is interpreted normally. If the model genuinely did
    // revert, what remains is prose rather than a params block or a short
    // question, and it falls through to the refusal at the bottom of this
    // function on its own merits.
    // The model's raw output is otherwise invisible: nothing else logs it and
    // there is no field on ParseResponse carrying it, so when this function
    // refuses, the only thing an operator sees is the refusal itself with no
    // way to tell a model that answered badly from one that answered fine into
    // a parser that mishandled it. That distinction is the whole diagnosis, and
    // its absence already cost one debugging cycle here.
    std::fprintf(stderr, "[assistant] raw model output (%zu bytes): %s\n", raw_text.size(),
                 raw_text.c_str());
    std::fflush(stderr);

    const std::string visible = strip_think_block(raw_text);

    if (const auto block = extract_params_block(visible); block.has_value()) {
        validate_and_populate_params(trim(*block), response);
        return;
    }

    const std::string question = trim(visible);
    if (question.empty() || question.size() > kMaxClarificationLength) {
        // Neither a valid params block nor something that looks like one
        // short clarifying question -- per the design brief, that is a
        // Refusal, never a crash and never an invented answer.
        populate_refusal(response, calculator::assistant::Refusal::OUT_OF_SCOPE,
                         "The assistant could not produce structured parameters or a short "
                         "clarifying question for this request.");
        return;
    }
    response.mutable_clarification()->set_question(question);
}

// ---------------------------------------------------------------------------
// Admission: authenticate, then charge
// ---------------------------------------------------------------------------

/**
 * The same admission guard finance_service.cpp documents and uses, scoped to
 * "assistant" instead of "finance". Duplicated rather than shared from a
 * common header because CHARGE is not exposed anywhere shareable today (only
 * finance_service.cpp defines it) -- one visible macro per service file is
 * exactly the debuggable, greppable shape quota.cppm's own doc comment
 * argues for, and inventing a shared header for a four-line macro used by
 * two files is not warranted.
 */
#define CHARGE(method_name, cost)                                                \
    ::options_calculator::auth::Identity _id;                                    \
    do {                                                                         \
        if (auto _a = ::options_calculator::auth::KeyRegistry::instance()         \
                          .authenticate(*context, "assistant", (method_name), _id); \
            !_a.ok()) {                                                          \
            return _a;                                                           \
        }                                                                        \
        ::options_calculator::quota::TierLimits _lim{_id.requests_per_minute,     \
                                                    _id.compute_units_per_hour}; \
        if (auto _q = ::options_calculator::quota::QuotaEnforcer::instance()      \
                          .admit_identity(_id.id, _id.tier, (method_name),        \
                                          (cost),                                 \
                                          _id.has_limits ? &_lim : nullptr);      \
            !_q.ok()) {                                                          \
            return _q;                                                           \
        }                                                                        \
    } while (false)

// ---------------------------------------------------------------------------
// Service
// ---------------------------------------------------------------------------

class StrategyAssistantImpl final : public calculator::assistant::StrategyAssistant::Service {
  public:
    auto ParseStrategy(ServerContext* context, const calculator::assistant::ParseRequest* request,
                       calculator::assistant::ParseResponse* response) -> Status override {
        if (context == nullptr || request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }

        // CHARGE at the top, before any inference work, per quota.cppm's own
        // rationale: a refused call should cost a hash lookup, not a
        // generation. cost_llm_generate prices this call from the same
        // kMaxNewTokens bound the worker actually generates with below, so
        // the price and the real worst-case cost never drift apart.
        CHARGE("ParseStrategy", ::options_calculator::quota::cost_llm_generate(
                                     1, static_cast<int>(kMaxNewTokens)));

        // Bound the two caller-controlled prompt inputs BEFORE they are
        // concatenated into a prompt or reach the worker. Cheap string-length
        // checks, charged the same as everything else per the CHARGE above --
        // this is request-shape validation, the same class of failure
        // finance_service.cpp reports as INVALID_ARGUMENT (e.g. "paths and
        // steps must be positive"), not a Refusal: the request itself is
        // malformed, independent of whether the trade it describes makes
        // sense. See kMaxUtteranceLength's own doc comment for why this
        // exists at all.
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

        // The Pro gate, server-side and before any inference work -- same
        // placement rationale as CalculateStrategy's own gate in
        // calculator_service.cpp. `_id` is the identity CHARGE already
        // resolved above; reusing it here means authentication runs exactly
        // once per call. With PRO_GATE_MODE unset (Off) this is inert and
        // ParseStrategy stays free, matching today's behaviour.
        if (auto s = ::options_calculator::auth::check_assistant_entitlement(_id); !s.ok()) {
            return s;
        }

        auto& worker = AssistantWorker::instance();
        if (!worker.available()) {
            // Degrade honestly: MODEL_PATH was never set, or the model
            // failed to load at startup. This is genuine infrastructure
            // unavailability, but per the proto's own Refusal::MODEL_UNAVAILABLE
            // doc comment -- "not a gRPC error because the RPC to THIS
            // service still completed correctly" -- it is expressed as a
            // successful RPC carrying that reason, not a gRPC error status.
            // This also satisfies the design brief's own explicit allowance
            // ("a Refusal, or an appropriate gRPC error") by choosing the
            // richer, more branchable outcome the proto was written for.
            populate_refusal(*response, calculator::assistant::Refusal::MODEL_UNAVAILABLE,
                             "The strategy assistant is not available right now.");
            return Status::OK;
        }

        const std::string prompt =
            build_prompt(request->utterance(), request->prior_clarification());

        auto outcome = worker.submit(prompt);
        if (!outcome.has_value()) {
            // The ONE case that is a genuine gRPC error rather than a
            // Refusal: the bounded queue was already full. Per the design
            // brief, this must be immediate -- no blocking, no growing the
            // queue -- and it is: submit() never waited to make room.
            return Status(grpc::StatusCode::RESOURCE_EXHAUSTED,
                          "The strategy assistant is at capacity; please retry shortly.");
        }

        if (!outcome->ok) {
            // The inference backend accepted the job but failed to complete
            // it (an exception inside generate()). Same reasoning as the
            // unavailable-at-startup case above: the RPC to THIS service
            // completed correctly, so this is a Refusal, not a gRPC error.
            populate_refusal(*response, calculator::assistant::Refusal::MODEL_UNAVAILABLE,
                             "The strategy assistant failed to produce a response: " + outcome->error);
            return Status::OK;
        }

        interpret_model_output(outcome->text, *response);
        return Status::OK;
    }
};

}  // namespace

auto RegisterAssistantService(grpc::ServerBuilder& builder) -> void {
    // Static storage duration for the same reason the calculator and finance
    // services use it: gRPC's RegisterService takes the address and does not
    // take ownership, so the service must outlive both the builder and the
    // server.
    static StrategyAssistantImpl service;
    builder.RegisterService(&service);
    logger::Logger::getInstance().info("Registered {} on the same port as the calculator",
                                       calculator::assistant::StrategyAssistant::service_full_name());

    // Force AssistantWorker to construct (and attempt to load MODEL_PATH) NOW
    // rather than on the first RPC, mirroring RegisterFinanceService's own
    // eager-init rationale for the quota enforcer: whether the assistant is
    // usable should be a fact this process establishes and logs at startup,
    // not something the first caller discovers by accident. A failed load
    // here is logged and swallowed, never propagated -- per the design
    // brief, a missing or broken model must degrade this ONE service, not
    // take down the calculator and finance services sharing this port.
    const bool loaded = AssistantWorker::instance().available();
    logger::Logger::getInstance().info(
        "Strategy assistant model is {}", loaded ? "LOADED" : "UNAVAILABLE (set MODEL_PATH to enable)");
}

#undef CHARGE

}  // namespace options_calculator::assistant
