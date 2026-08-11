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
#include <expected>
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

#include <unistd.h>

#include <grpcpp/grpcpp.h>
#include "assistant.pb.h"
#include "assistant.grpc.pb.h"
// The calculator contract, for the StrategyParams -> calculator.Leg averaging
// handoff at the foot of this file. Included here as well as in the module
// interface because a module implementation unit gets no header from its
// interface's global module fragment.
#include "calculator.pb.h"

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
// ComputeBackend enum values and CudaBackend::query()/is_available(), the
// runtime probes AssistantWorker's own ASSISTANT_DEVICE resolution reads --
// see that call site for why this file cannot use `#ifdef SENSEN_HAS_CUDA`
// itself. sensen_slim's own FILE_SET already includes cuda_backend.cppm
// regardless of ENABLE_CUDA, so this import needs no build-file change.
import sensen.cuda_backend;
import fastjson;
import logger;
import quota;
import api_key;
import strategy_catalogue;
import market_data;
import assistant_verification;
import inference_admission;
import inference_queue;
import pg;

// SGEE: ParseStrategy's admission/model-availability/generate/parse-and-verify
// sequence is expressed as a real workflow graph rather than a chain of
// hand-rolled `if (...) return ...;` checks -- see calculator_service.cpp's
// OptionsWorkflow (studied before writing this) for the sibling shape and,
// critically, for the GetActionId trap documented at its "Bind each action by
// the ID the builder assigned, not by name" comment: ActionRegistry::Register
// hashes the name while Builder::Execute assigns sequential IDs from its own
// table, and the two schemes never agree, so an action bound by name compiles
// and runs while silently never executing. This file's own binding below
// follows the same GetActionId()-only discipline for exactly that reason.
import sgee.builder.fluent;
import sgee.runtime.context;
import sgee.runtime.interpreter;
import sgee.runtime.action_registry;
import sgee.core.blueprint;
import sgee.core.types;

namespace options_calculator::assistant {

using grpc::ServerContext;
using grpc::Status;

// InferenceOutcome, PendingJob, InferenceBackend, QueuedBackend, and the
// Postgres-backed PostgresLeaseSource/PostgresAdmission extension of that
// same admission layer all live in inference_admission.cppm now -- shared
// with mortgage_assistant_service.cpp rather than duplicated. See that
// module's own banner for the full contract.
using namespace options_calculator::inference_admission;

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
 * A `<params>` JSON blob (six short fields, one of them optional) or a one-sentence clarifying
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
 * THIS SHAPE WAS WRONG, AND IT WAS THE ROOT CAUSE OF A PRODUCTION DEFECT:
 * a trader asks "Long ES, 30 days, 1 contract.", correctly gets asked
 * futures-or-equity, answers "futures", and the SAME request that produces
 * `futures_long` when phrased with explicit futures wording instead produces
 * the near-miss `long_futures` and gets refused. The model can clearly
 * produce the right token; feeding it `prior_clarification` was what
 * perturbed it.
 *
 * The previous version of this function put `prior_clarification` in an
 * `assistant` turn, BEFORE the `user` turn carrying `utterance`:
 *
 *     assistant: futures
 *     user: Long ES, 30 days, 1 contract.
 *
 * That is backwards on both axes at once against what this model actually
 * saw in training (agent/dataset/build_dataset.py's `make_clarification`,
 * which is 16% of the 28,500-row fine-tuning set): every clarification
 * example there is `user(full request) -> assistant(one short question) ->
 * user(one short reply) -> assistant(final answer)`. Putting the TRADER's
 * own one-word reply in the ASSISTANT's mouth, and putting it BEFORE the
 * request it is answering rather than after, is a shape this fine-tune never
 * saw -- and Qwen3 has no other prior to fall back on, so it produced a
 * degraded near-miss instead of the trained one.
 *
 * This version restores both: `utterance` (the trader's original request,
 * which is what every observed caller -- including scripts/probe_live_
 * assistant.py -- resends unchanged on the answering turn) is the FIRST user
 * turn, and `prior_clarification` (the trader's short reply, e.g. "futures")
 * is a LATER user turn, never an assistant one. A short, neutral placeholder
 * fills the assistant slot in between so the turns keep strictly alternating
 * roles, matching the training shape structurally -- its exact wording is
 * not load-bearing: ParseRequest deliberately does not carry the actual
 * question this service asked last turn (see assistant.proto's own comment
 * on why: only the last two turns of the true three-turn exchange survive
 * the round trip, not the first), so the real question text is not
 * reconstructable here, and this specific ambiguous-root clarification was
 * never itself a training example either way (the model was never taught
 * the ES/Eversource distinction; see CLAUDE.md's own note that asset-class
 * disambiguation is resolved by this file's static heuristics, never by the
 * model). What the placeholder buys is ROLE and POSITION, not content: the
 * trader's words stay attributed to the trader, in the position a genuine
 * answer-to-a-question occupies.
 *
 * When `prior_clarification` is empty (every first-turn call, and every
 * currently-passing case: SPY iron condor, NVDA bull call spread, explicit
 * E-mini wording, explicit shares wording, CL+equity) this function emits
 * EXACTLY the same three-turn prompt it always has -- system, user,
 * assistant -- so none of those are touched by this change.
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
        prompt += "<|im_start|>assistant\nWhich did you mean?<|im_end|>\n";
        prompt += "<|im_start|>user\n";
        prompt += prior_clarification;
        prompt += "<|im_end|>\n";
    }
    prompt += "<|im_start|>assistant\n";
    return prompt;
}

// ---------------------------------------------------------------------------
// Inference backends
// ---------------------------------------------------------------------------
//
// InferenceOutcome, PendingJob, InferenceBackend and QueuedBackend used to be
// defined here. They now live in inference_admission.cppm (imported above)
// -- see this file's own `using namespace options_calculator::inference_admission;`
// and that module's banner for the full contract. SensenBackend and
// LlamaCppBackend below derive from the imported QueuedBackend exactly as they
// derived from the local one before this extraction.


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
     * Loads the model. Returns nullptr -- never a half-constructed object --
     * if anything about the load fails, so the caller's only decision is
     * "did I get a backend or not".
     *
     * Deliberately does NOT start the owner thread -- that used to happen
     * here, and starting it before the caller has had a chance to call
     * set_lease_source() is exactly the startup race documented on
     * QueuedBackend::start() itself (inference_admission.cppm), which left a
     * live INFERENCE_QUEUE=postgres deployment leasing nothing, forever. The
     * caller MUST call start() itself, and -- if it is going to install a
     * lease source at all -- only after doing so; see AssistantWorker's own
     * constructor for the enforced order.
     */
    [[nodiscard]] static auto create(const std::string& model_path, std::size_t max_concurrent,
                                     std::size_t queue_depth, std::size_t kv_max_seq_len,
                                     int threads, Device device) -> std::unique_ptr<SensenBackend> {
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

        return std::unique_ptr<SensenBackend>(
            new SensenBackend(std::move(pipeline), max_concurrent, queue_depth, device));  // NOLINT(cppcoreguidelines-owning-memory) -- the allocation is owned by the std::unique_ptr constructed on the previous line; the private constructor is why make_unique cannot be used here.
    }

    [[nodiscard]] auto name() const noexcept -> std::string_view override { return "sensen"; }

    /** The device actually decoding here -- see InferenceBackend::device()'s
     *  own doc for why this reports ground truth, not the ASSISTANT_DEVICE
     *  request that may have been degraded away from at construction time. */
    [[nodiscard]] auto device() const noexcept -> std::string_view override {
        return device_name(device_);
    }

    /** Public (moved out of `private:` below) because the OWNING Worker --
     *  a different class -- must be able to call it after create() returns,
     *  once it has decided whether to install a lease source first. See
     *  QueuedBackend::start()'s own doc for why the ordering matters. */
    auto start() -> void override {
        worker_ = std::jthread([this](std::stop_token stoken) { run(stoken); });
    }

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

        // Set in admit(); read in finish() to split the request's wall time
        // into prefill vs decode. See InferenceOutcome's own doc comment for
        // why this exists.
        std::chrono::steady_clock::time_point admit_start{};
        std::chrono::steady_clock::time_point first_token{};
        double prefill_ms = 0.0;
    };

    SensenBackend(std::unique_ptr<sensen::LLMPipeline> pipeline, std::size_t max_concurrent,
                  std::size_t queue_depth, Device device)
        : pipeline_{std::move(pipeline)}, device_{device} {
        max_concurrent_ = max_concurrent;
        max_queue_depth_ = queue_depth;
    }

    /**
     * The sampling configuration every request in this process is generated
     * with. Built fresh per call rather than cached because it is three field
     * writes (four on a `Device::Cuda` instance) and sharing mutable state
     * with the decode loop buys nothing.
     *
     * No longer `static`: the `Device::Cuda` branch below needs both
     * `device_` and `pipeline_->getModel().num_layers()`, neither of which a
     * static function can see. This changes nothing about the `Device::Cpu`
     * path -- see that branch's own comment.
     */
    [[nodiscard]] auto generation_config(std::size_t max_new_tokens) const
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

        // Device offload. `device_` is `Device::Cpu` on every process that
        // existed before ASSISTANT_DEVICE did (the default, and every build
        // without genuine CUDA support -- see AssistantWorker's own
        // resolve_device() call site), so this block is a no-op there:
        // `n_gpu_layers` stays the 0 pinned two comments up, and
        // `compute_backend` is left at GenerationConfig's own default (AUTO)
        // exactly as that comment already explains is safe -- AUTO never
        // survives the `n_gpu_layers=0` conjunction it depends on. This is
        // the "cpu (or anything unrecognised) -> byte-identical to today"
        // branch of this task's brief, and it is enforced by this being the
        // only place `device_` is read: nothing above this line changed.
        //
        // Only a genuinely resolved `Device::Cuda` (real CUDA support
        // compiled in AND a ready device found at runtime -- never merely
        // requested) touches either field, and when it does:
        //
        //   - `compute_backend` is set EXPLICITLY to CUDA, never left at
        //     AUTO. Leaving it AUTO is the exact defect this project's
        //     CLAUDE.md documents: GenerationConfig defaults compute_backend
        //     to AUTO, sensen counted that as a GPU request on its own, and a
        //     build that could not honour it then cast a raw float logit to
        //     a token id -- garbage tokens, no crash. Setting it explicitly
        //     here removes AUTO from the decision entirely.
        //   - `n_gpu_layers` is set to every layer this model has
        //     (`pipeline_->getModel().num_layers()`), never a split. Hybrid
        //     decode is unimplemented in sensen and hybrid prefill is broken
        //     for Q8_0 (the format both fine-tuned assistants ship), so
        //     there is no partial value this service may offer -- all or
        //     zero, matching `Device`'s own two-value contract.
        if (device_ == Device::Cuda) {
            config.n_gpu_layers = pipeline_->getModel().num_layers();
            config.compute_backend = sensen::cuda::ComputeBackend::CUDA;
        }

        return config;
    }

    /**
     * The fixed prefix a prefill can potentially reuse: the mandatory system
     * turn plus the opening of the user turn, byte-for-byte the string
     * `build_prompt` emits before it appends `utterance`. Rebuilt on every
     * call rather than cached as a std::string member because it is four
     * concatenations and this runs exactly once, from `ensure_prefix_cache`.
     */
    [[nodiscard]] static auto fixed_prefix_text() -> std::string {
        std::string prefix;
        prefix += "<|im_start|>system\n";
        prefix += kSystemPrompt;
        prefix += "<|im_end|>\n";
        prefix += "<|im_start|>user\n";
        return prefix;
    }

    /**
     * Builds the cached system-prompt prefix once, on the owner thread,
     * before the first request is ever admitted.
     *
     * WHY floor(P/16)*16, NOT THE FULL NATURAL PREFIX LENGTH P:
     *
     * sensen's Q8 KV cache (the default on this CPU-only build -- see
     * kv_half.cppm's parseKvDtypeEnv, which resolves an unset
     * SENSEN_KV_DTYPE to Q8, not fp32/fp16) quantises a 16-token KV block
     * only once its 16th token has landed (PagedKVCache::update(),
     * kv_cache.cppm, "deferred quantisation"). A chunked forward
     * (schedulerPrefill(K) + schedulerPrefillContinue(rest)) and an
     * equivalent one-shot schedulerPrefill(all) read DIFFERENT
     * quantisation states of the SAME tokens whenever the split K does not
     * land on a 16-token boundary -- confirmed on this exact checkpoint by
     * prefix_cache_verify_probe's --split sweep: the natural system-prompt
     * prefix length (not a multiple of 16) DIFFERS at every case tested,
     * while a 16-aligned split is bit-identical at every case tested.
     * Snapshotting the cache at floor(P/16)*16 rather than at the natural
     * P tokens sidesteps that mechanism entirely: the cached blocks are
     * always whole, quantised, closed blocks, identical to what a one-shot
     * forward over the same prefix would have produced. The remaining
     * (P - floor(P/16)*16) tail tokens of the fixed prefix -- at most 15 of
     * them -- are recomputed fresh on every request, folded into the same
     * schedulerPrefillContinue call that also carries the caller's own
     * turn, exactly like any other non-cached tail.
     *
     * A failure here (no model loaded far enough to prefill, or a system
     * prompt shorter than one block) disables the cache, not the service:
     * admit() falls back to a full schedulerPrefill on every request, which
     * is exactly today's behaviour without this feature.
     */
    auto ensure_prefix_cache(const sensen::GenerationConfig& params) -> void {
        if (prefix_cache_ready_) return;
        prefix_cache_ready_ = true;  // set first: a failure must not retry per request

        prefix_match_tokens_ = pipeline_->schedulerEncode(fixed_prefix_text());
        prefix_aligned_len_ = (prefix_match_tokens_.size() / 16) * 16;
        if (prefix_aligned_len_ == 0) {
            logger::Logger::getInstance().warn(
                "assistant: system-prompt prefix is only {} tokens -- shorter than one "
                "16-token KV block, so the prefix cache is disabled (nothing block-aligned to "
                "share). Every request will prefill from scratch, as before this feature.",
                prefix_match_tokens_.size());
            return;
        }

        auto agent = pipeline_->schedulerMakeAgent(kPrefixAgentId);
        if (agent == nullptr) {
            logger::Logger::getInstance().warn(
                "assistant: could not allocate the prefix-cache agent -- the prefix cache is "
                "disabled. Every request will prefill from scratch, as before this feature.");
            return;
        }
        const std::span<const std::uint32_t> aligned_span(prefix_match_tokens_.data(),
                                                           prefix_aligned_len_);
        const auto logits = pipeline_->schedulerPrefill(*agent, aligned_span, params);
        if (logits.empty()) {
            logger::Logger::getInstance().warn(
                "assistant: prefix-cache prefill produced no logits -- the prefix cache is "
                "disabled. Every request will prefill from scratch, as before this feature.");
            return;
        }

        prefix_agent_ = std::move(agent);
        logger::Logger::getInstance().info(
            "assistant: prefix cache ready -- {} of {} system-prompt tokens cached "
            "(block-aligned); {} trailing token(s) plus the caller's own turn are recomputed "
            "on every request.",
            prefix_aligned_len_, prefix_match_tokens_.size(),
            prefix_match_tokens_.size() - prefix_aligned_len_);
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
        ensure_prefix_cache(params);

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
        seq.admit_start = std::chrono::steady_clock::now();
        try {
            const auto prompt_tokens = pipeline_->schedulerEncode(prompt);
            seq.agent = pipeline_->schedulerMakeAgent(agent_id);
            if (seq.agent == nullptr) {
                seq.promise.set_value(InferenceOutcome{
                    .ok = false, .text = {}, .error = "could not allocate an inference session"});
                return false;
            }

            // Reuse the cached system-prompt prefix's KV when this request's
            // own tokens genuinely open with it -- VERIFIED by comparing
            // actual token ids below, never assumed from the source strings
            // matching (the tokenizer can merge differently across the
            // prefix/utterance boundary, and a request that arrived without
            // the system prompt at all -- which should not happen, but must
            // not be trusted blindly -- must fall through to a full prefill
            // rather than silently reusing a stale prefix).
            std::vector<float> logits;
            if (prefix_agent_ != nullptr && prompt_tokens.size() > prefix_match_tokens_.size() &&
                std::equal(prefix_match_tokens_.begin(), prefix_match_tokens_.end(),
                          prompt_tokens.begin())) {
                auto& context = seq.agent->getContext();
                context.assign(prompt_tokens.begin(),
                               prompt_tokens.begin() +
                                   static_cast<std::ptrdiff_t>(prefix_aligned_len_));
                // COW block sharing (MultiLayerKVCache::share_from): no KV
                // bytes are copied here, only the shared block pointers --
                // sensen's own PagedKVCache copies a block on the first
                // WRITE to it (use_count() > 1 gate), and this agent never
                // writes to a block it shares with prefix_agent_, since its
                // own new tokens all land at or after prefix_aligned_len_,
                // strictly past every shared (block-aligned) block.
                seq.agent->kv_cache->share_from(*prefix_agent_->kv_cache);
                logits = pipeline_->schedulerPrefillContinue(
                    *seq.agent, std::span<const std::uint32_t>(prompt_tokens).subspan(
                                    prefix_aligned_len_),
                    params);
            } else {
                logits = pipeline_->schedulerPrefill(*seq.agent, prompt_tokens, params);
            }
            if (logits.empty()) {
                pipeline_->schedulerEvict(*seq.agent);
                seq.promise.set_value(InferenceOutcome{
                    .ok = false, .text = {}, .error = "prompt prefill produced no logits"});
                return false;
            }
            seq.next_token = pipeline_->schedulerSample(logits, params, seq.agent->getContext());
            seq.first_token = std::chrono::steady_clock::now();
            seq.prefill_ms =
                std::chrono::duration<double, std::milli>(seq.first_token - seq.admit_start).count();
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
        outcome.prefill_ms = seq.prefill_ms;
        outcome.tokens_generated = seq.generated.size();
        // seq.first_token stays default-constructed (epoch) if admit() never
        // reached it -- can't happen here, since finish() is only reachable
        // for a sequence that made it into `active`, which requires admit()
        // to have succeeded and set it.
        const double decode_ms = std::chrono::duration<double, std::milli>(
                                     std::chrono::steady_clock::now() - seq.first_token)
                                     .count();
        outcome.decode_ms = decode_ms;
        logger::Logger::getInstance().info(
            "[assistant] timing: prefill={:.1f}ms decode={:.1f}ms tokens={} decode_tok_s={:.1f} "
            "total={:.1f}ms",
            outcome.prefill_ms, decode_ms, outcome.tokens_generated,
            decode_ms > 0.0 ? (static_cast<double>(outcome.tokens_generated) * 1000.0 / decode_ms)
                            : 0.0,
            outcome.prefill_ms + decode_ms);
        seq.promise.set_value(std::move(outcome));
        pipeline_->schedulerEvict(*seq.agent);
    }

    std::unique_ptr<sensen::LLMPipeline> pipeline_;

    // The device generation_config() offloads onto -- Device::Cpu unless
    // AssistantWorker resolved a genuinely ready Device::Cuda. See
    // create()'s own parameter and generation_config()'s own comment on the
    // one place this is read.
    Device device_ = Device::Cpu;

    // System-prompt KV prefix cache (see ensure_prefix_cache()/admit()
    // above). prefix_match_tokens_ is the FULL (natural, not block-aligned)
    // tokenized prefix -- used only to verify a request's own tokens
    // genuinely open with it, never to decide how much KV is shared.
    // prefix_aligned_len_ (<= prefix_match_tokens_.size(), always a
    // multiple of 16) is how much of it prefix_agent_'s KV cache actually
    // holds. prefix_agent_ stays null, and every request falls back to a
    // full prefill exactly as before this feature existed, until
    // ensure_prefix_cache() successfully builds it on the owner thread --
    // and forever if it never does.
    std::vector<std::uint32_t> prefix_match_tokens_;
    std::size_t prefix_aligned_len_ = 0;
    std::unique_ptr<sensen::AgentSession> prefix_agent_;
    bool prefix_cache_ready_ = false;

    // Reserved agent id for prefix_agent_, far outside the range
    // next_agent_id (0, 1, 2, ... in run()) will ever reach on a live
    // process, so the two id spaces can never collide.
    static constexpr std::size_t kPrefixAgentId = std::numeric_limits<std::size_t>::max() - 1;

    // Declared LAST: member destruction order is the reverse of declaration
    // order, so `worker_`'s destructor (request_stop + join) runs BEFORE
    // pipeline_, prefix_agent_ and the queue in the base are torn down,
    // guaranteeing run() has fully exited -- and therefore touched none of
    // them -- by the time they are destroyed.
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
    /**
     * Deliberately does NOT start the owner thread -- see SensenBackend::
     * create()'s own doc, right above this class, for the startup race that
     * used to cause when it did. The caller MUST call start() itself, after
     * deciding whether to install a lease source; see AssistantWorker's
     * constructor for the enforced order.
     */
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

        return std::unique_ptr<LlamaCppBackend>(
            new LlamaCppBackend(model, ctx, max_concurrent, queue_depth, n_ctx_per_seq));  // NOLINT(cppcoreguidelines-owning-memory) -- the allocation is owned by the std::unique_ptr constructed on the previous line; the private constructor is why make_unique cannot be used here.
    }

    [[nodiscard]] auto name() const noexcept -> std::string_view override { return "llamacpp"; }

    /** Public (moved out of `private:` below) -- see SensenBackend::start()'s
     *  own doc, right above this class, for why. */
    auto start() -> void override {
        worker_ = std::jthread([this](std::stop_token stoken) { run(stoken); });
    }

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
        // `admission_` is non-null only in INFERENCE_QUEUE=postgres mode, and
        // it already falls back to `*backend_` (today's exact in-process
        // path) on any Postgres-path failure -- see PostgresAdmission::submit.
        // In `local` mode (the default) `admission_` stays null and this call
        // goes to `backend_` directly, exactly as before this feature existed.
        if (admission_ != nullptr) {
            return admission_->submit(std::move(prompt));
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

        // Device selection: `cpu` (default, byte-identical to every build
        // before this selector existed) or `cuda` (only ever real on a build
        // this project's CMakeLists.txt compiled with genuine CUDA support
        // AND a process that finds a usable device at runtime). See
        // resolve_device's own doc (inference_admission.cppm) for exactly
        // what each of "cuda on a non-CUDA build", "cuda on a CUDA build
        // with no ready device", and "cuda selected and usable" resolves to.
        //
        // Resolved BEFORE the ASSISTANT_BACKEND selector just below, not
        // after: ASSISTANT_DEVICE names a property of the whole build and
        // process, not of whichever decode engine ASSISTANT_BACKEND names,
        // so a cuda request this build cannot honour refuses the whole
        // assistant rather than quietly landing on a CPU-only llama.cpp
        // backend nobody asked for.
        const std::string requested_device = env_string("ASSISTANT_DEVICE").value_or("cpu");

        // Whether THIS compiled binary has genuine CUDA support at all --
        // established at RUNTIME, not via `#ifdef SENSEN_HAS_CUDA` in this
        // file. That macro cannot answer the question here: backend/
        // CMakeLists.txt defines it PRIVATE to the sensen_slim target only
        // (see that file's own comment on the block that adds it -- C++20/23
        // modules do not leak preprocessor macros across the import
        // boundary), so calculator_engine's own translation units, this one
        // included, never see it, on ANY build, CUDA-enabled or not.
        //
        // sensen.cuda_backend's CudaBackend::query() is the one call in the
        // module whose FAILURE MESSAGE distinguishes "never compiled in"
        // from "compiled in, nothing usable right now" -- its own source
        // (read-only from this task) returns the literal string "CUDA not
        // compiled (ENABLE_CUDA=OFF)" from its non-CUDA branch and a
        // DIFFERENT message ("CUDA device query failed (error N)") from its
        // CUDA branch when a real device probe fails. Should that exact
        // wording ever change upstream, this degrades soft, not silent: it
        // simply stops recognising the build-configuration case and always
        // takes the runtime-degrade branch below instead -- still an honest
        // error log, still CPU service, never a silent substitution of a
        // working GPU path for one that was never asked for.
        const auto cuda_query = sensen::cuda::CudaBackend::query();
        const bool cuda_build =
            cuda_query.has_value() || cuda_query.error() != "CUDA not compiled (ENABLE_CUDA=OFF)";
        const bool cuda_device_ready = cuda_query.has_value();
        const auto device_resolution =
            resolve_device(requested_device, cuda_build, cuda_device_ready);
        if (device_resolution.refuse) {
            logger::Logger::getInstance().error(
                "ASSISTANT_DEVICE=cuda was requested, but this binary was not built with CUDA "
                "support (-DENABLE_CUDA=ON was not set at configure time) -- the strategy "
                "assistant will return a Refusal on every call rather than silently serving on "
                "CPU; rebuild with CUDA enabled or set ASSISTANT_DEVICE=cpu.");
            return;
        }
        if (requested_device == "cuda" && device_resolution.device == Device::Cpu) {
            logger::Logger::getInstance().error(
                "ASSISTANT_DEVICE=cuda was requested and this binary was built with CUDA "
                "support, but no usable CUDA device was found at runtime -- the strategy "
                "assistant is serving on CPU instead of failing outright, since a missing or "
                "broken device is not the build-configuration mistake the branch above guards "
                "against.");
        }
        const Device device = device_resolution.device;

        const std::string requested = env_string("ASSISTANT_BACKEND").value_or("sensen");

        if (requested == "sensen") {
            backend_ = SensenBackend::create(*path, max_concurrent, queue_depth, kv_max_seq_len,
                                             threads, device);
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
            "Strategy assistant ready: backend={} device={} model={} inference_threads={} "
            "max_concurrent={} queue_depth={} context_tokens={}",
            backend_->name(), backend_->device(), *path, threads, max_concurrent, queue_depth,
            kv_max_seq_len);

        // configure_inference_queue() MUST run before backend_->start(): it is
        // what calls backend_->set_lease_source() in INFERENCE_QUEUE=postgres
        // mode, and start() must never be called before that decision is
        // made -- see QueuedBackend::start()'s own doc (inference_admission.
        // cppm) for the startup race this order exists to make impossible.
        // In `local` mode configure_inference_queue() is a fast no-op (no
        // lease source is ever installed), so this reordering changes
        // nothing observable about that path.
        configure_inference_queue();
        backend_->start();
    }

    /**
     * `INFERENCE_QUEUE` selects `local` (default, or anything unrecognized --
     * degrades quietly rather than crashing) or `postgres`. This runs AFTER
     * `backend_` is confirmed non-null above, since postgres mode still needs
     * a real local backend both as the lease source's decode target and as
     * PostgresAdmission's fallback.
     *
     * Any failure here degrades to `local` behaviour rather than leaving the
     * process half-configured: a missing DATABASE_URL is logged and this
     * function simply returns with `admission_`/lease source left null, which
     * is indistinguishable from INFERENCE_QUEUE=local at every call site.
     * pg::Pool itself never throws or hard-fails at construction either (see
     * its own header comment) -- a dead initial connection heals on first
     * use, or surfaces as a bounded submit_remote()/await_result() failure
     * that PostgresAdmission already treats as "fall back to local".
     */
    auto configure_inference_queue() -> void {
        const std::string mode = env_string("INFERENCE_QUEUE").value_or("local");
        if (mode != "postgres") {
            if (mode != "local") {
                logger::Logger::getInstance().warn(
                    "INFERENCE_QUEUE=\"{}\" is not \"local\" or \"postgres\" -- the strategy "
                    "assistant stays on local-only inference.",
                    mode);
            }
            return;
        }

        const auto database_url = env_string("DATABASE_URL");
        if (!database_url.has_value()) {
            logger::Logger::getInstance().warn(
                "INFERENCE_QUEUE=postgres was requested but DATABASE_URL is unset -- the "
                "strategy assistant degrades to local-only inference (its own decode loop, no "
                "shared queue).");
            return;
        }

        // connect_timeout=2000ms/statement_timeout=2000ms are pg::PoolConfig's
        // own defaults already -- restated here, not overridden, so this is
        // self-documenting against the brief's mandated bounds rather than a
        // silent reliance on a default that could drift later.
        pg::PoolConfig pool_config;
        pool_config.conninfo = *database_url;
        pool_config.connect_timeout = std::chrono::milliseconds(2000);
        pool_config.statement_timeout = std::chrono::milliseconds(2000);
        // 16, not PoolConfig's own default of 4: this ONE pool is shared by
        // every submitter's submit_remote()/await_result() polling AND the
        // worker's own lease()/complete() calls (this Worker's admission_ and
        // its backend_'s lease source both hold the SAME queue_/pool_). At
        // max_concurrent=4 (the default), worst-case simultaneous need is
        // roughly 4 submitters + up to 4 write-back helper threads + the
        // worker's own lease loop -- comfortably under 16, with headroom.
        // Leaving this at 4 lets Pool::acquire()'s own bounded
        // acquire_timeout (250ms) start silently queuing requests for a
        // connection under ordinary load, which shows up as added latency,
        // not as an error -- see this task's own latency breakdown for why
        // that mattered enough to size explicitly rather than accept the
        // default. Revisit if MAX_CONCURRENT is raised well beyond 4.
        pool_config.size = 16;
        pool_ = std::make_shared<pg::Pool>(std::move(pool_config));
        queue_ = std::make_shared<inference_queue::Queue>(pool_);
        // LISTEN/NOTIFY is a wakeup hint only (see inference_queue.cppm's own
        // banner) -- a failure to start it is logged and otherwise ignored;
        // await_result()'s poll loop remains correct, just not sped up.
        if (auto pump = queue_->start_notify_pump(); !pump.has_value()) {
            logger::Logger::getInstance().warn(
                "inference_admission: strategy assistant's LISTEN pump failed to start ({}) -- "
                "await_result() will still work correctly via its poll backstop, just not as "
                "promptly.",
                inference_queue::to_string(pump.error()));
        }
        // Without this, an abandoned lease or a job that timed out while
        // still pending sits until some OTHER replica's ticker (or an
        // operator's manual sweep_once()) happens to reap it -- see
        // Queue::start_sweep_ticker()'s own doc. Safe to start unconditionally
        // here even though the mortgage assistant's Worker starts its own
        // ticker too: sweep_once()'s pg_try_advisory_lock makes every ticker
        // but one a no-op on any given tick, cluster-wide.
        queue_->start_sweep_ticker();

        const std::string worker_id = "strategy-" + std::to_string(::getpid());
        auto lease_source = std::make_shared<inference_admission::PostgresLeaseSource>(
            queue_, inference_queue::Surface::Strategy, worker_id);
        backend_->set_lease_source(lease_source);
        lease_source_ = std::move(lease_source);

        // 90s, matching this queue's own design (was 20s here, a drift from
        // that design this task's own measurement caught: a 20s window gave
        // a healthy worker under transient load far less room than intended
        // before the caller gave up and fell back). This is a CEILING, not a
        // target latency: await_result() returns the instant the job reaches
        // a terminal state, so a healthy worker (one that actually leases --
        // see the startup-race fix on QueuedBackend::start()) answers in
        // roughly one decode's worth of time (kMaxNewTokens at ~34 tok/s is
        // roughly a second), and this bound is only ever fully paid by a
        // genuinely stuck request, at which point falling back late is still
        // strictly better than an anonymous MODEL_UNAVAILABLE.
        admission_ = std::make_unique<inference_admission::PostgresAdmission>(
            queue_, inference_queue::Surface::Strategy, *backend_, std::chrono::milliseconds(90000));

        logger::Logger::getInstance().info(
            "Strategy assistant: INFERENCE_QUEUE=postgres -- submitting through the shared "
            "queue (worker_id={}), with the local backend as fallback and lease source",
            worker_id);
    }

    std::unique_ptr<QueuedBackend> backend_;
    std::shared_ptr<pg::Pool> pool_;
    std::shared_ptr<inference_queue::Queue> queue_;
    std::shared_ptr<inference_admission::PostgresLeaseSource> lease_source_;
    std::unique_ptr<InferenceBackend> admission_;
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
 * An unterminated `<think>` USUALLY means the model hit the token ceiling
 * mid-reasoning, leaving nothing to interpret -- everything is dropped and the
 * caller falls through to its refusal path rather than showing a trader a
 * severed reasoning trace. Text with no `<think>` at all is returned unchanged.
 *
 * But "unterminated" does not always mean "truncated", and assuming it did was
 * silently discarding correct answers. The deployed model answers
 *
 *     <think>\n\n<params>{"symbol":"NVDA",...}</params>
 *
 * -- 130 bytes, `</params>` present and balanced, simply missing `</think>`.
 * That is a COMPLETE answer, not a severed trace, and dropping it turned a
 * correct parse into "the assistant could not produce structured parameters".
 * Measured 2026-08-03: it is why the `bull call spread on NVDA` baseline failed
 * in production while the model itself had gotten it right.
 *
 * So an unterminated block is dropped only up to a complete `<params>` block, if
 * one follows. With no such block the original all-or-nothing behaviour stands,
 * because then there really is nothing safe to show. */
[[nodiscard]] auto strip_think_block(std::string_view text) -> std::string {
    constexpr std::string_view kOpen = "<think>";
    constexpr std::string_view kClose = "</think>";

    const auto open = text.find(kOpen);
    if (open == std::string_view::npos) return std::string{text};

    const auto close = text.find(kClose, open + kOpen.size());
    if (close == std::string_view::npos) {
        // Rescue a complete params block that a missing </think> would otherwise
        // swallow. Both tags must be present, in order, or this is a genuine
        // truncation and the caller should refuse.
        constexpr std::string_view kParamsOpen = "<params>";
        constexpr std::string_view kParamsClose = "</params>";
        const auto p_open = text.find(kParamsOpen, open + kOpen.size());
        if (p_open == std::string_view::npos) return {};
        if (text.find(kParamsClose, p_open + kParamsOpen.size()) == std::string_view::npos) return {};
        std::string rescued{text.substr(0, open)};
        rescued += text.substr(p_open);
        return rescued;
    }

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

/**
 * The graph-visible verdict of turning the model's raw text into a response:
 * did the RPC end up with something other than a refusal (a `<params>` block
 * that survived every check including the mandatory GP-ARA gate, OR a short
 * clarifying question -- both are documented-OK outcomes per assistant.proto's
 * own file banner), or a refusal (for any of the many reasons validate_and_
 * populate_params/interpret_model_output can produce one, including but not
 * limited to a bad ticker, an unsupported strategy, or a live-quote miss)?
 *
 * This is deliberately coarse -- two values, not one per Refusal::Reason --
 * because the graph only needs to know WHICH EDGE to take out of the
 * ParseAndVerify node (Next("Done") vs OnError("Refused")); the specific
 * reason is still fully preserved in `response.refusal().reason()` either
 * way, exactly as it always was. See StrategyAssistantImpl's constructor for
 * where this drives real interpreter routing rather than staying a
 * documentation-only distinction.
 */
enum class ModelOutputOutcome : std::uint8_t { Success, Refused };

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
 *
 * `already_disambiguated_ambiguous_root` is true iff `symbol` is one of
 * `assistant_verification::kAmbiguousRoots` (ES/CL) -- which, by the time
 * this function is ever called, `validate_and_populate_params` has already
 * resolved via the trader's own words (see its own comment on
 * `detect_asset_class_signal`) or refused to proceed past with a
 * Clarification instead. For a FUTURES/CRYPTO claim on one of those roots,
 * the live equity quote WILL resolve (that resolving quote is the ambiguity
 * itself: "ES" really is Eversource's live ticker), so running the ordinary
 * AssistantParamsMismatch branch below would re-discover the exact same
 * ambiguity the trader just answered and ask about it again (the ordinary
 * branch is named `SymbolProbeOutcome::AssetClassMismatch` below) -- the
 * infinite-loop failure mode the round-trip is specifically built to avoid.
 * So for this one case the live quote is skipped entirely and catalogue
 * membership (`kKnownFuturesRoots`/`kKnownCryptoSymbols`) is trusted
 * directly, exactly as already happens below when the quote fails to
 * resolve at all -- the resolving-quote case is simply never reached for an
 * already-disambiguated ambiguous root.
 */
[[nodiscard]] auto probe_symbol(const std::string& symbol, const std::string& asset_class,
                                 bool already_disambiguated_ambiguous_root) -> SymbolProbeOutcome {
    if (already_disambiguated_ambiguous_root && asset_class != "EQUITY") {
        if (asset_class == "FUTURES") {
            return in_catalogue(kKnownFuturesRoots, symbol) ? SymbolProbeOutcome::Resolved
                                                             : SymbolProbeOutcome::Unknown;
        }
        return in_catalogue(kKnownCryptoSymbols, symbol) ? SymbolProbeOutcome::Resolved
                                                          : SymbolProbeOutcome::Unknown;
    }

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
 *
 * `utterance`/`prior_clarification` exist on this signature for exactly one
 * purpose: resolving a genuinely ambiguous futures/equity root (ES, CL) from
 * the trader's own words, immediately below, before either GP-ARA or
 * `probe_symbol` ever see the symbol. Everything else in this function is
 * unaffected by them.
 */
auto validate_and_populate_params(std::string_view json_text, std::string_view utterance,
                                   std::string_view prior_clarification,
                                   calculator::assistant::ParseResponse& response)
    -> ModelOutputOutcome {
    auto parsed = fastjson::parse(json_text);
    if (!parsed.has_value() || !parsed->is_object()) {
        populate_refusal(response, calculator::assistant::Refusal::OUT_OF_SCOPE,
                         "The assistant's structured output could not be parsed as a JSON object.");
        return ModelOutputOutcome::Refused;
    }
    const auto& obj = parsed.value();

    if (!obj.contains("symbol") || !obj["symbol"].is_string()) {
        populate_refusal(response, calculator::assistant::Refusal::UNKNOWN_SYMBOL,
                         "The assistant did not name a symbol for this request.");
        return ModelOutputOutcome::Refused;
    }
    const std::string symbol{obj["symbol"].as_string()};
    if (!looks_like_a_ticker(symbol)) {
        populate_refusal(response, calculator::assistant::Refusal::UNKNOWN_SYMBOL,
                         "\"" + symbol + "\" does not look like a real ticker.");
        return ModelOutputOutcome::Refused;
    }

    if (!obj.contains("asset_class") || !obj["asset_class"].is_string()) {
        populate_refusal(response, calculator::assistant::Refusal::UNKNOWN_SYMBOL,
                         "The assistant did not resolve an asset class for \"" + symbol + "\".");
        return ModelOutputOutcome::Refused;
    }
    std::string asset_class{obj["asset_class"].as_string()};
    if (!is_known_asset_class(asset_class)) {
        populate_refusal(response, calculator::assistant::Refusal::UNKNOWN_SYMBOL,
                         "\"" + asset_class + "\" is not a supported asset class.");
        return ModelOutputOutcome::Refused;
    }

    if (!obj.contains("strategy") || !obj["strategy"].is_string()) {
        populate_refusal(response, calculator::assistant::Refusal::UNSUPPORTED_STRATEGY,
                         "The assistant did not name a strategy for this request.");
        return ModelOutputOutcome::Refused;
    }
    std::string strategy{obj["strategy"].as_string()};
    if (!::options_calculator::strategy::is_known(strategy)) {
        // A token-order near miss (e.g. "long_futures" for the catalogue's
        // "futures_long") is a general failure mode of a 0.6B fine-tune, not
        // a one-off -- see assistant_verification.cppm's own doc comment for
        // the derivation and the ambiguity guard that keeps this from ever
        // silently redirecting to the WRONG strategy. This is a guard on top
        // of the prompt fix in `build_prompt`, not a substitute for it: it
        // catches whatever the prompt fix does not.
        if (const auto alias =
                ::options_calculator::assistant::verify::normalize_strategy_alias(strategy);
            alias.has_value()) {
            strategy = *alias;
        } else {
            populate_refusal(
                response, calculator::assistant::Refusal::UNSUPPORTED_STRATEGY,
                "\"" + strategy + "\" is not one of the strategies this calculator prices.");
            return ModelOutputOutcome::Refused;
        }
    }

    if (!obj.contains("expiration_days") || !obj["expiration_days"].is_number()) {
        populate_refusal(response, calculator::assistant::Refusal::OUT_OF_SCOPE,
                         "The assistant did not give an expiration for this request.");
        return ModelOutputOutcome::Refused;
    }
    const std::int64_t expiration_days = obj["expiration_days"].as_int64();
    if (expiration_days < kMinExpirationDays || expiration_days > kMaxExpirationDays) {
        populate_refusal(response, calculator::assistant::Refusal::OUT_OF_SCOPE,
                         "The assistant's expiration (" + std::to_string(expiration_days) +
                             " days) is out of a sane range.");
        return ModelOutputOutcome::Refused;
    }

    // Optional far leg, for the five two-expiry strategies. Absent is the common
    // case and means "the trader gave only the near leg" -- the UI completes it
    // from a second chain. Only a present-but-nonsense value is refused here;
    // the near/far ORDER is the verifier's business, since that is a cross-field
    // rule and this loop is per-field.
    std::int64_t far_expiration_days = 0;
    if (obj.contains("far_expiration_days") && obj["far_expiration_days"].is_number()) {
        far_expiration_days = obj["far_expiration_days"].as_int64();
        if (far_expiration_days != 0 &&
            (far_expiration_days < kMinExpirationDays || far_expiration_days > kMaxExpirationDays)) {
            populate_refusal(response, calculator::assistant::Refusal::OUT_OF_SCOPE,
                             "The assistant's far expiration (" +
                                 std::to_string(far_expiration_days) +
                                 " days) is out of a sane range.");
            return ModelOutputOutcome::Refused;
        }
    }

    if (!obj.contains("quantity") || !obj["quantity"].is_number()) {
        populate_refusal(response, calculator::assistant::Refusal::OUT_OF_SCOPE,
                         "The assistant did not give a quantity for this request.");
        return ModelOutputOutcome::Refused;
    }
    const std::int64_t quantity = obj["quantity"].as_int64();
    if (quantity < kMinQuantity || quantity > kMaxQuantity) {
        populate_refusal(response, calculator::assistant::Refusal::OUT_OF_SCOPE,
                         "The assistant's quantity (" + std::to_string(quantity) +
                             ") is out of a sane range.");
        return ModelOutputOutcome::Refused;
    }

    // ------------------------------------------------------------------
    // Ambiguous-root disambiguation (ES/Eversource, CL/Colgate-Palmolive).
    //
    // Runs before GP-ARA and before the live probe, deliberately: it is free
    // (no network), and it can change `asset_class` -- so GP-ARA's own
    // cross-field checks below (strategy category vs asset_class) see the
    // TRADER'S resolved asset class rather than whatever the model happened
    // to guess when it had no real basis to pick one. `symbol` is only ever
    // one of these two roots today (`kAmbiguousRoots`, checked against a real
    // equity-ticker lookup -- see that array's own doc comment for which of
    // the five supported futures roots genuinely collide with a live equity
    // and which do not); every other symbol gets `nullptr` here and this
    // whole block is a no-op, so SPY/NVDA/QQQ/etc. are provably unaffected.
    //
    // Resolution is deterministic keyword matching over the trader's own
    // words -- the utterance THIS turn plus the clarification question ASKED
    // last turn, concatenated, so a one-word reply on a second turn ("futures",
    // "the stock", "Eversource") resolves exactly like a first-turn utterance
    // that already answered the question would (see
    // `detect_asset_class_signal`'s own comment) -- never the model's own
    // asset_class guess. The model was never trained to arbitrate this
    // specific ambiguity (CLAUDE.md's own documented note that the training
    // set restricts futures roots to ES/NQ, making anything about them
    // out-of-distribution), so its guess is evidence of nothing here.
    //
    // A decisive signal OVERWRITES `asset_class` to match, so a stale or
    // simply-guessed model value can never leak through once the trader's own
    // words already answer the question -- this is also what keeps the
    // round trip from looping: `probe_symbol` below is told
    // (`already_disambiguated_ambiguous_root`) that this symbol's asset class
    // was resolved right here, so it skips the live-quote check that would
    // otherwise rediscover the very ambiguity just resolved and ask about it
    // again. No signal at all short-circuits straight to a Clarification
    // naming both concrete readings (never "please clarify"), skipping GP-ARA
    // and the network probe entirely for a request that is about to be asked
    // a question anyway.
    //
    // Built once, here, rather than only inside the `ambiguous` branch below:
    // `is_unsupported_bare_direction_guess` immediately after this block needs
    // the identical (utterance + prior_clarification) context for EVERY
    // request, not only ambiguous-root ones, so it is computed unconditionally
    // to serve both call sites from one string.
    std::string disambiguation_context;
    disambiguation_context.reserve(utterance.size() + prior_clarification.size() + 1);
    disambiguation_context += utterance;
    disambiguation_context += ' ';
    disambiguation_context += prior_clarification;

    if (const auto* ambiguous = ::options_calculator::assistant::verify::find_ambiguous_root_info(symbol);
        ambiguous != nullptr) {
        const auto signal =
            ::options_calculator::assistant::verify::detect_asset_class_signal(symbol, disambiguation_context);
        if (signal == ::options_calculator::assistant::verify::AssetClassSignal::None) {
            // STEP 2 of the layering (utterance keywords -> GP-ARA constraint
            // propagation -> ask): the trader's own words did not decide it,
            // so before asking, see whether the STRATEGY does. A
            // Futures-category strategy on an ambiguous root pins the asset
            // class by elimination through the exact rule table
            // `AssistantParamsDomain::translate()` already enforces
            // everywhere else -- see `infer_ambiguous_root_asset_class`'s own
            // doc comment (assistant_verification.cppm) for why this
            // direction is sound, why the mirror-image EQUITY inference is
            // deliberately NOT attempted (that is the exact "long_put" from
            // "Long ES, 30 days, 1 contract." production defect this task
            // shipped against), and why lexical support is required on top
            // of the category constraint, not instead of it.
            if (const auto inferred = ::options_calculator::assistant::verify::infer_ambiguous_root_asset_class(
                    symbol, strategy, expiration_days, quantity, disambiguation_context);
                inferred.has_value()) {
                asset_class = *inferred;
            } else {
                // STEP 3: neither the trader's words nor the reasoner could
                // settle it -- ask. Covers a genuine Unsafe (wrong category),
                // an Indeterminate (uncategorised strategy), and missing
                // lexical support alike; see that function's own comment for
                // why collapsing all three to "ask" is correct specifically
                // on THIS path (unlike the mandatory GP-ARA gate below, whose
                // Indeterminate stays a flat refusal).
                const auto question =
                    ::options_calculator::assistant::verify::build_ambiguity_clarification(symbol, strategy);
                populate_clarification(response, question.has_value()
                                                      ? *question
                                                      : "\"" + symbol + "\" is ambiguous between more than one "
                                                            "asset class -- which did you mean?");
                return ModelOutputOutcome::Success;
            }
        } else {
            asset_class = (signal == ::options_calculator::assistant::verify::AssetClassSignal::Futures)
                              ? "FUTURES"
                              : "EQUITY";
        }
    }

    // ------------------------------------------------------------------
    // General strategy-credibility gate: independent of whether `symbol` is
    // an ambiguous root at all.
    //
    // Two live-observed defects share one shape: the model names one of the
    // two "bare direction" ids it falls back to when it cannot actually tell
    // what structure the trader described -- `long_call` or `long_put` --
    // while the one word that distinguishes that id from every sibling
    // ("call"/"put") appears nowhere in what the trader typed:
    //
    //   - "Long ES, 30 days, 1 contract." -> long_put. Nothing says "put".
    //   - "Buy 100 shares of ES stock, Eversource, 30 days." -> long_call,
    //     quantity=100. Buying shares is not a long call, and this
    //     calculator prices no equity outright -- a share-purchase
    //     utterance names no strategy in this catalogue at all.
    //
    // Both pass every per-field check above AND the mandatory GP-ARA gate
    // below (each is internally consistent -- a real strategy, a real
    // symbol, sane bounds) while being, in the ordinary sense, simply wrong.
    // `is_unsupported_bare_direction_guess` is the narrow, general check for
    // exactly this shape -- narrow (two of forty-seven ids, not a blanket
    // lexical-support requirement over the whole catalogue, which would risk
    // refusing genuine requests phrased descriptively rather than by name;
    // see that function's own doc comment) and general (every symbol, not
    // only the ambiguous-root pair) at once. Runs after the ambiguous-root
    // block so that block's own, more specific clarification (naming both
    // concrete asset-class readings) still wins for an ambiguous root that
    // has not yet been resolved -- this check only ever fires once
    // `asset_class` is already settled, one way or another.
    if (::options_calculator::assistant::verify::is_unsupported_bare_direction_guess(strategy,
                                                                                      disambiguation_context)) {
        const std::string_view distinguishing_word = strategy == "long_call" ? "call" : "put";
        populate_clarification(
            response, "I read this as \"" + strategy + "\" for \"" + symbol +
                          "\", but nothing in your request mentions a \"" + std::string{distinguishing_word} +
                          "\" or any option at all -- could you say what you'd like to do (this calculator "
                          "prices options and futures, not buying the underlying outright)?");
        return ModelOutputOutcome::Success;
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
         .quantity = quantity,
         .far_expiration_days = far_expiration_days});
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
        return ModelOutputOutcome::Refused;
    }

    // Every check above this line is free (string comparisons, catalogue
    // lookups, and the GP-ARA cross-field verification above); this one
    // costs a network round trip, so it runs LAST -- a request that was
    // always going to be refused for its strategy, its expiration, its
    // quantity, or a cross-field contradiction should not also pay for a
    // live quote it will never use.
    switch (probe_symbol(symbol, asset_class,
                          ::options_calculator::assistant::verify::find_ambiguous_root_info(symbol) != nullptr)) {
        case SymbolProbeOutcome::Unknown:
            populate_refusal(response, calculator::assistant::Refusal::UNKNOWN_SYMBOL,
                             "I could not find a tradeable instrument for '" + symbol +
                                 "'. Which symbol did you mean?");
            return ModelOutputOutcome::Refused;
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
            return ModelOutputOutcome::Success;
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
            return ModelOutputOutcome::Refused;
        case SymbolProbeOutcome::Resolved:
            break;
    }

    // Exercise style (European/American/Bermudan) and Asian averaging.
    // Deterministic extraction over the trader's OWN utterance, not model
    // output -- the fine-tuned model was never trained on either concept
    // (agent/dataset/, 11,400 rows, zero instances of either) and cannot be
    // asked to emit them. `extract_exercise_and_asian` mirrors
    // sensen.finance.ExerciseType/AsianType name-for-name and value-for-value
    // as its own plain enum (that module links no protobuf headers -- see its
    // own doc comment), so the two `static_cast`s below are a same-numbered-
    // value reinterpretation, never a translation that could drift.
    const auto exercise_asian =
        ::options_calculator::assistant::verify::extract_exercise_and_asian(utterance);

    auto* params = response.mutable_params();
    params->set_symbol(symbol);
    params->set_asset_class(asset_class);
    params->set_strategy(strategy);
    params->set_expiration_days(static_cast<std::int32_t>(expiration_days));
    params->set_quantity(static_cast<std::int32_t>(quantity));
    params->set_far_expiration_days(static_cast<std::int32_t>(far_expiration_days));
    params->set_exercise_type(static_cast<sensen::finance::ExerciseType>(exercise_asian.exercise_type));
    params->set_asian_type(static_cast<sensen::finance::AsianType>(exercise_asian.asian_type));
    return ModelOutputOutcome::Success;
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
auto interpret_model_output(const std::string& raw_text, std::string_view utterance,
                             std::string_view prior_clarification,
                             calculator::assistant::ParseResponse& response) -> ModelOutputOutcome {
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
        return validate_and_populate_params(trim(*block), utterance, prior_clarification, response);
    }

    // The model emitted no params. Before treating its prose as a clarifying
    // question, try the one shape it is known to decline wrongly: a bare
    // directional futures order ("Long NQ, 45 days, 2 contracts."), which it
    // reads as a request to PLACE a trade and answers with "I can't price a
    // position directly". Measured 2026-08-03 that is 3 of 16 defect-holdout
    // rows, while the identically-shaped ES row succeeds -- a gap in what the
    // model learned, not a missing capability.
    //
    // This is a recovery, not an override: it runs ONLY when there is no params
    // block, and its result goes through `validate_and_populate_params` -- the
    // same per-field validation and the same mandatory GP-ARA gate the model's
    // own output faces. A recovered guess that cannot be proven safe is refused
    // exactly like a hallucinated one, so this can turn a wrong refusal into a
    // correct answer but never a refusal into a wrong answer.
    if (const auto recovered =
            ::options_calculator::assistant::verify::recover_bare_futures_directive(utterance);
        recovered.has_value()) {
        std::fprintf(stderr, "[assistant] recovered bare futures directive: %s\n", recovered->c_str());
        std::fflush(stderr);
        return validate_and_populate_params(*recovered, utterance, prior_clarification, response);
    }

    const std::string question = trim(visible);
    if (question.empty() || question.size() > kMaxClarificationLength) {
        // Neither a valid params block nor something that looks like one
        // short clarifying question -- per the design brief, that is a
        // Refusal, never a crash and never an invented answer.
        populate_refusal(response, calculator::assistant::Refusal::OUT_OF_SCOPE,
                         "The assistant could not produce structured parameters or a short "
                         "clarifying question for this request.");
        return ModelOutputOutcome::Refused;
    }
    response.mutable_clarification()->set_question(question);
    return ModelOutputOutcome::Success;
}

// ---------------------------------------------------------------------------
// SGEE execution context and graph actions
//
// ParseStrategy used to be a straight-line chain of `if (...) return ...;`
// checks (charge, bound-check, injection/advice guards, Pro gate, model
// availability, generate, interpret). It is now a real SGEE workflow graph --
// see calculator_service.cpp's OptionsWorkflow, studied before writing this,
// for the sibling shape. Two differences from that graph, both deliberate:
//
//   1. calculator_service.cpp's graph is purely linear (Next chains only,
//      short-circuiting internally on ctx->status) because its actions never
//      need to distinguish MORE than "did an earlier stage already fail" --
//      every failure there is the same INVALID_ARGUMENT/INTERNAL shape. This
//      graph uses real `.OnError(...)` routing instead: a refusal (populated
//      response, or a hard grpc::Status) takes a DIFFERENT edge out of its
//      node than success does, landing on a distinct "Refused" terminal
//      rather than falling through the same Done path every other outcome
//      does. That is what makes the routing provable from outside the
//      function bodies (see ParseStrategy's postcondition and
//      RegisterAssistantServiceForTest below), not merely asserted in a
//      comment.
//   2. Two kinds of failure exist and both take the OnError edge, but they
//      are NOT the same thing: a hard gRPC error (quota exhausted, malformed
//      request, Pro gate) is recorded in `status` and returned AS a
//      grpc::Status; a Refusal/Clarification is a SUCCESSFUL RPC whose
//      response already carries the reason. ParseStrategy's postcondition
//      below disambiguates the two exactly the way CalculateStrategy's own
//      postcondition reads ctx->status after Run() -- see that comment in
//      calculator_service.cpp for the shared rationale.
// ---------------------------------------------------------------------------

struct AssistantCtx {
    // Copied out of the request up front (never a raw ParseRequest*): every
    // node action below runs on its own turn of the interpreter loop, well
    // after ParseStrategy's own `request` parameter would have gone out of
    // scope in a differently-shaped implementation, so the fields this graph
    // actually needs travel by value -- mirroring ComputeContext's own
    // `calculator::StrategyRequest request` member in calculator_service.cpp.
    std::string utterance;
    std::string prior_clarification;

    // Non-owning, and the one deliberate exception to "no raw pointer
    // travels" among this graph's context fields. grpc::ServerContext is
    // move/copy-disabled by design (it is gRPC's own per-call state), so it
    // cannot be captured by value the way `utterance` is above. The pointer
    // is safe here specifically because AssistantEngineCtx is constructed,
    // driven through Interpreter<>::Run() synchronously, and destroyed
    // entirely within ParseStrategy's own stack frame -- it never escapes to
    // another thread or another call, so it cannot outlive the `context` the
    // RPC handler was given.
    grpc::ServerContext* context = nullptr;

    calculator::assistant::ParseResponse response;
    // OK unless a node set it to a hard gRPC error (quota/auth/bound-check/
    // Pro gate). A populated Refusal/Clarification is NOT represented here --
    // it lives in `response` and is still a Status::OK outcome, exactly as
    // it always was before this graph existed.
    grpc::Status status{grpc::Status::OK};
    ::options_calculator::auth::Identity identity;
    std::string model_text;
};
using Ctx = std::shared_ptr<AssistantCtx>;
using ActionRegistry = sgee::runtime::ActionRegistry<Ctx>;

using sgee::ExecutionResult;

/** The four action names the StrategyAssistantWorkflow graph defines, in
 * graph order. Shared between the production constructor (binds all four)
 * and the test-only constructor overload, exactly mirroring kAllActionNames
 * in calculator_service.cpp and for the identical reason: one definition of
 * "the full set" that cannot drift between the two callers. */
inline constexpr std::array<std::string_view, 4> kAllActionNames{
    "Admission", "CheckModel", "Generate", "ParseAndVerify"};

/**
 * Authenticate, charge, bound-check, and gate -- everything ParseStrategy did
 * before ever touching the model, now as one node's action. Manually inlines
 * what the removed CHARGE macro did (KeyRegistry::authenticate +
 * QuotaEnforcer::admit_identity) because a macro built around `return`
 * cannot early-exit an RPC from inside a graph action; the equivalent here
 * sets `ctx.status` and signals the interpreter via ExecutionResult instead,
 * exactly as action_initialize does in calculator_service.cpp.
 */
[[nodiscard]] auto action_admission(Ctx& ctx) -> ExecutionResult<> {
    if (auto s = ::options_calculator::auth::KeyRegistry::instance().authenticate(
            *ctx->context, "assistant", "ParseStrategy", ctx->identity);
        !s.ok()) {
        ctx->status = s;
        return std::unexpected(sgee::ExecutionError::ActionFailed);
    }
    ::options_calculator::quota::TierLimits lim{ctx->identity.requests_per_minute,
                                                ctx->identity.compute_units_per_hour};
    if (auto q = ::options_calculator::quota::QuotaEnforcer::instance().admit_identity(
            ctx->identity.id, ctx->identity.tier, "ParseStrategy",
            ::options_calculator::quota::cost_llm_generate(1, static_cast<int>(kMaxNewTokens)),
            ctx->identity.has_limits ? &lim : nullptr);
        !q.ok()) {
        ctx->status = q;
        return std::unexpected(sgee::ExecutionError::ActionFailed);
    }

    if (ctx->utterance.size() > kMaxUtteranceLength) {
        ctx->status = grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                                   "utterance exceeds the " + std::to_string(kMaxUtteranceLength) +
                                       "-character limit for this RPC.");
        return std::unexpected(sgee::ExecutionError::ActionFailed);
    }
    if (ctx->prior_clarification.size() > kMaxPriorClarificationLength) {
        ctx->status = grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                                   "prior_clarification exceeds the " +
                                       std::to_string(kMaxPriorClarificationLength) +
                                       "-character limit for this RPC.");
        return std::unexpected(sgee::ExecutionError::ActionFailed);
    }

    if (::options_calculator::assistant::verify::looks_like_prompt_injection(ctx->utterance) ||
        ::options_calculator::assistant::verify::looks_like_prompt_injection(
            ctx->prior_clarification)) {
        populate_refusal(
            ctx->response, calculator::assistant::Refusal::OUT_OF_SCOPE,
            "This reads like an instruction to the assistant rather than a trade description. "
            "Describe the strategy you want priced (e.g. \"bull call spread on NVDA, 30 days, "
            "2 contracts\") without instructions aimed at the assistant itself.");
        return std::unexpected(sgee::ExecutionError::ActionFailed);
    }
    if (::options_calculator::assistant::verify::looks_like_advice_request(ctx->utterance)) {
        populate_refusal(
            ctx->response, calculator::assistant::Refusal::OUT_OF_SCOPE,
            "I don't give trading advice, predictions, or recommendations -- describe a "
            "specific strategy (e.g. \"bull call spread on NVDA, 30 days, 2 contracts\") and "
            "I will price it.");
        return std::unexpected(sgee::ExecutionError::ActionFailed);
    }

    // The Pro gate, server-side and before any inference work -- same
    // placement rationale as CalculateStrategy's own gate in
    // calculator_service.cpp. With PRO_GATE_MODE unset (Off) this is inert
    // and ParseStrategy stays free, matching today's behaviour.
    if (auto s = ::options_calculator::auth::check_assistant_entitlement(
            ctx->identity, ::options_calculator::auth::kStrategySurface);
        !s.ok()) {
        ctx->status = s;
        return std::unexpected(sgee::ExecutionError::ActionFailed);
    }
    return {};
}

/** Model-availability gate. Degrades honestly when MODEL_PATH was never set
 * or the model failed to load -- see Refusal::MODEL_UNAVAILABLE's own proto
 * doc comment for why this is a populated response (Status::OK), not a gRPC
 * error, and therefore an OnError edge whose destination still carries
 * ctx->status == OK. */
[[nodiscard]] auto action_check_model(Ctx& ctx) -> ExecutionResult<> {
    if (!AssistantWorker::instance().available()) {
        populate_refusal(ctx->response, calculator::assistant::Refusal::MODEL_UNAVAILABLE,
                         "The strategy assistant is not available right now.");
        return std::unexpected(sgee::ExecutionError::ActionFailed);
    }
    return {};
}

/** Builds the prompt and submits it to the worker. A full queue is the ONE
 * genuine gRPC error past admission (RESOURCE_EXHAUSTED, immediate -- see
 * submit()'s own contract); a backend exception is a Refusal, matching
 * MODEL_UNAVAILABLE's "the RPC to THIS service completed correctly"
 * reasoning. */
[[nodiscard]] auto action_generate(Ctx& ctx) -> ExecutionResult<> {
    const std::string prompt = build_prompt(ctx->utterance, ctx->prior_clarification);

    auto outcome = AssistantWorker::instance().submit(prompt);
    if (!outcome.has_value()) {
        ctx->status = grpc::Status(grpc::StatusCode::RESOURCE_EXHAUSTED,
                                   "The strategy assistant is at capacity; please retry shortly.");
        return std::unexpected(sgee::ExecutionError::ActionFailed);
    }
    if (!outcome->ok) {
        populate_refusal(ctx->response, calculator::assistant::Refusal::MODEL_UNAVAILABLE,
                         "The strategy assistant failed to produce a response: " + outcome->error);
        return std::unexpected(sgee::ExecutionError::ActionFailed);
    }
    ctx->model_text = std::move(outcome->text);
    return {};
}

/**
 * Parse the model's raw text, run symbol verification, and populate the
 * final response -- interpret_model_output is untouched in substance (same
 * messages, same GP-ARA-equivalent field checks, same live-quote probe); it
 * now RETURNS the Success/Refused verdict instead of silently writing only
 * into `response`, which is what lets this node's OnError edge actually fire
 * on a refusal instead of the graph reaching Done regardless of outcome.
 */
[[nodiscard]] auto action_parse_and_verify(Ctx& ctx) -> ExecutionResult<> {
    const auto outcome =
        interpret_model_output(ctx->model_text, ctx->utterance, ctx->prior_clarification, ctx->response);
    if (outcome == ModelOutputOutcome::Refused) {
        return std::unexpected(sgee::ExecutionError::ActionFailed);
    }
    return {};
}

// ---------------------------------------------------------------------------
// Service
// ---------------------------------------------------------------------------

class StrategyAssistantImpl final : public calculator::assistant::StrategyAssistant::Service {
  private:
    std::shared_ptr<ActionRegistry> actions_;
    std::shared_ptr<const sgee::GraphBlueprint> graph_;
    // Resolved once at construction, exactly like calculator_service.cpp's
    // done_node_id_ -- ParseStrategy is on the hot path (well, the
    // model-latency path) and must not look these up by name per call.
    std::uint16_t done_node_id_{0};
    std::uint16_t refused_node_id_{0};

  public:
    /**
     * `bound_action_names` defaults to every action the graph defines --
     * see calculator_service.cpp's CalculatorServiceImpl constructor for why
     * the parameter exists at all (RegisterAssistantServiceForTest below is
     * its only other caller, and only from tests).
     */
    explicit StrategyAssistantImpl(
        std::span<const std::string_view> bound_action_names = kAllActionNames)
        : actions_{std::make_shared<ActionRegistry>()} {
        auto& log = logger::Logger::getInstance();

        auto graph_result = sgee::Builder<Ctx>("StrategyAssistantWorkflow")
            .Node("Admission")
                .Execute("Admission")
                .Next("CheckModel")
                .OnError("Refused")
            .Node("CheckModel")
                .Execute("CheckModel")
                .Next("Generate")
                .OnError("Refused")
            .Node("Generate")
                .Execute("Generate")
                .Next("ParseAndVerify")
                .OnError("Refused")
            .Node("ParseAndVerify")
                .Execute("ParseAndVerify")
                .Next("Done")
                .OnError("Refused")
            .Node("Done")
                .IsTerminal()
            .Node("Refused")
                .IsTerminal()
            .Build();

        if (!graph_result) {
            log.error("Failed to build SGEE graph: {}", graph_result.error());
            return;
        }
        graph_ = graph_result.value();
        done_node_id_ = graph_->GetNodeId("Done");
        refused_node_id_ = graph_->GetNodeId("Refused");

        // Bind each action by the ID the builder assigned, NEVER by name --
        // see this file's own import-block comment and calculator_service.cpp's
        // constructor for the GetActionId trap this avoids.
        const std::array<std::pair<std::string_view, ActionRegistry::ActionFunction>, 4> bindings{{
            {"Admission", action_admission},
            {"CheckModel", action_check_model},
            {"Generate", action_generate},
            {"ParseAndVerify", action_parse_and_verify},
        }};

        const auto is_bound = [&](std::string_view name) {
            return std::ranges::find(bound_action_names, name) != bound_action_names.end();
        };

        for (const auto& [name, fn] : bindings) {
            if (!is_bound(name)) {
                // Test-only path -- see calculator_service.cpp's identical
                // branch. Production always binds the full set.
                log.warn("Action '{}' intentionally left unbound (test configuration); "
                         "any entity reaching its node will halt without a status",
                         name);
                continue;
            }
            const auto id = graph_->GetActionId(name);
            if (!id) {
                log.error("Action '{}' is not present in the graph; refusing to start", name);
                graph_.reset();
                return;
            }
            actions_->RegisterById(*id, fn);
        }

        log.info("SGEE graph initialized: {} registered actions for the strategy assistant",
                 bindings.size());
    }

    ~StrategyAssistantImpl() override = default;

    auto ParseStrategy(ServerContext* context, const calculator::assistant::ParseRequest* request,
                       calculator::assistant::ParseResponse* response) -> Status override {
        if (context == nullptr || request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        if (!graph_) {
            return Status(grpc::StatusCode::INTERNAL, "Execution graph not initialized");
        }

        auto ctx = std::make_shared<AssistantCtx>();
        ctx->utterance = request->utterance();
        ctx->prior_clarification = request->prior_clarification();
        ctx->context = context;

        // Inline, on this RPC's own thread -- same rationale as
        // CalculateStrategy's identical comment in calculator_service.cpp:
        // this service is built with no callback API and no
        // SetSyncServerOption, so gRPC already gives every in-flight RPC its
        // own thread.
        sgee::runtime::EngineContext<Ctx> engine;
        std::vector<Ctx> entities{ctx};
        engine.Load(entities);

        sgee::runtime::Interpreter<Ctx> interpreter(
            graph_, sgee::runtime::ParallelismLevel::Sequential, actions_.get());
        interpreter.Run(engine);

        // --- Postconditions --------------------------------------------
        //
        // TERMINAL-STATE: did the one entity in this run reach EITHER
        // recognised terminal? If it stalled anywhere else, an action failed
        // without setting ctx->status/returning unexpected, an action id was
        // not registered, or the registry was never wired up -- the same
        // three failure modes calculator_service.cpp's postcondition
        // documents, reproduced here through RegisterAssistantServiceForTest
        // below rather than merely asserted.
        const auto& state_ids = engine.GetStateIds();
        const bool reached_terminal = !state_ids.empty() &&
            (state_ids[0] == done_node_id_ || state_ids[0] == refused_node_id_);
        if (!reached_terminal) {
            const std::string stalled_at =
                state_ids.empty() ? std::string{"<no entity>"}
                                  : std::string{graph_->GetNodeName(state_ids[0])};
            logger::Logger::getInstance().error(
                "ParseStrategy: graph halted at '{}' instead of reaching Done or Refused -- an "
                "action failed without setting status, an action id was not registered, or the "
                "action registry was never wired up",
                stalled_at);
            return Status(grpc::StatusCode::INTERNAL,
                          "Strategy-assistant parse halted before completion (stalled at '" +
                              stalled_at +
                              "'); this is a server-side defect, not a problem with the request");
        }

        // DID-COMPUTE: reaching a terminal proves every state was walked, not
        // that the action which landed there actually ran. Every node's
        // OnError target is "Refused" -- the SAME edge Execute() takes for a
        // genuinely UNREGISTERED action id (action_registry.cppm's Execute()
        // returns ExecutionError::ActionFailed for a miss, identically to a
        // real failure) -- so an unbound CheckModel/Generate/ParseAndVerify
        // action would otherwise land on "Refused" with ctx->status still OK
        // and ctx->response still default-constructed (no oneof set at all),
        // silently satisfying the TERMINAL-STATE check above while never
        // having run at all. This assertion is what actually catches that:
        // "Refused" must carry EITHER a hard status error OR a populated
        // Refusal; "Done" must carry a Params or a Clarification. A terminal
        // reached with neither is the silent-unbound-action bug wearing a
        // valid-looking state id.
        const bool refused_is_real =
            state_ids[0] != refused_node_id_ || !ctx->status.ok() || ctx->response.has_refusal();
        const bool done_is_real = state_ids[0] != done_node_id_ ||
            ctx->response.has_params() || ctx->response.has_clarification();
        if (!refused_is_real || !done_is_real) {
            logger::Logger::getInstance().error(
                "ParseStrategy: graph reached '{}' but produced no payload -- an action id "
                "landed on this terminal via its OnError edge without ever running (the same "
                "id-miss path a genuinely unregistered action takes)",
                std::string{graph_->GetNodeName(state_ids[0])});
            return Status(grpc::StatusCode::INTERNAL,
                          "Strategy-assistant parse reached completion without producing a "
                          "params, clarification, or refusal payload; this is a server-side "
                          "defect, not a problem with the request");
        }

        // Whichever terminal was reached, ctx->status is the single source of
        // truth for "was this a hard gRPC error" -- a Refusal/Clarification
        // leaves it OK and carries its reason in ctx->response instead,
        // exactly as ParseStrategy always returned Status::OK alongside a
        // populated Refusal before this graph existed.
        if (!ctx->status.ok()) return ctx->status;
        *response = ctx->response;
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

auto RegisterAssistantServiceForTest(grpc::ServerBuilder& builder,
                                     std::span<const std::string_view> bound_action_names) -> void {
    // Deliberately leaked, WITHOUT `static` -- see calculator_service.cpp's
    // RegisterCalculatorServiceForTest for the exact trap this avoids: a
    // function-local static here would fix the bound action set at the
    // FIRST call for the rest of the process, which is wrong for this hook.
    // A discriminating test needs to register services with DIFFERENT
    // subsets of actions in the same test binary (one missing an action, one
    // with the full set as a control), and `static` would silently make the
    // second registration reuse the first call's (possibly broken) instance.
    // Each caller is a short-lived test process that builds one in-process
    // grpc::Server per case and exits soon after, so the leak is bounded by
    // the test's own lifetime.
    auto* service = new StrategyAssistantImpl(bound_action_names);  // NOLINT(cppcoreguidelines-owning-memory) -- see comment above: gRPC's RegisterService does NOT take ownership and the service must outlive the server, while a function-local static would freeze the bound action set at the first call and break the discriminating tests that register different action subsets in one binary.
    builder.RegisterService(service);
}

// ---------------------------------------------------------------------------
// StrategyParams -> calculator.Leg averaging handoff. See the declarations in
// assistant_service.cppm for why these exist, why the two enums are separate,
// and for the training-set caveat that bounds how much any of this can be
// trusted.
// ---------------------------------------------------------------------------

auto calculator_asian_type(sensen::finance::AsianType parsed) -> calculator::Leg::AsianType {
    // Written out rather than static_cast even though the values coincide.
    // A cast here would keep compiling if either enum gained a value or
    // reordered, and would then reinterpret one style as another -- silently,
    // in the one field whose whole purpose is to say which payoff applies.
    // The default is NOT_ASIAN, which is also the only safe wrong answer:
    // it makes the calculator price a vanilla and say so, rather than price a
    // vanilla while claiming an Asian.
    switch (parsed) {
        case sensen::finance::AVERAGE_PRICE:
            return calculator::Leg::AVERAGE_PRICE;
        case sensen::finance::AVERAGE_STRIKE:
            return calculator::Leg::AVERAGE_STRIKE;
        case sensen::finance::NOT_ASIAN:
            return calculator::Leg::NOT_ASIAN;
        default:
            return calculator::Leg::NOT_ASIAN;
    }
}

[[nodiscard]] auto apply_averaging_to_legs(const calculator::assistant::StrategyParams& params,
                                           calculator::StrategyRequest& request) -> int {
    const auto style = calculator_asian_type(params.asian_type());
    if (style == calculator::Leg::NOT_ASIAN) {
        // Return before touching the request at all. Setting the field to its
        // own zero value would be harmless on the wire and is still avoided,
        // so that "a vanilla parse leaves the request untouched" is a property
        // of the code and not an accident of proto3's defaults.
        return 0;
    }

    int stamped = 0;
    for (int i = 0; i < request.legs_size(); ++i) {
        auto* leg = request.mutable_legs(i);
        if (leg->type() != calculator::Leg::CALL && leg->type() != calculator::Leg::PUT) {
            // A future or a share has no averaging window. Left alone, not
            // refused: the leg is valid, it is the style that does not apply.
            continue;
        }
        leg->set_asian_type(style);
        ++stamped;
    }
    return stamped;
}

}  // namespace options_calculator::assistant
