module;
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <exception>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

#include <grpcpp/grpcpp.h>
#include "assistant.pb.h"
#include "assistant.grpc.pb.h"

module assistant_service;

import sensen.llm_pipeline;
import fastjson;
import logger;
import quota;
import api_key;
import strategy_catalogue;
import market_data;

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
// The single dedicated inference worker
// ---------------------------------------------------------------------------

/** What the worker hands back for one generation request. */
struct InferenceOutcome {
    bool ok = false;
    std::string text;   // the model's raw decoded output, valid iff ok
    std::string error;  // human-readable failure detail, valid iff !ok
};

/**
 * Owns the one `sensen::LLMPipeline` instance this process will ever create,
 * on the one thread that will ever touch it.
 *
 * WHY A SINGLE DEDICATED WORKER THREAD, NOT A POOL:
 *
 * The pipeline is NOT thread-safe (nothing in sensen.llm_pipeline's contract
 * claims otherwise for concurrent `generate()` calls sharing one KV cache),
 * and the model holds roughly 2.63 GB resident (weights plus KV cache, per
 * the measured ground truth). One instance, one thread, for the entire
 * process lifetime is therefore not a convenience -- it is the only safe
 * shape: a second instance would double that footprint for a CPU-bound
 * ~34 tok/s model that gains nothing from running two generations at once
 * (there is no shared GPU to time-slice; the bottleneck is one CPU decode
 * loop either way), and sharing one instance across threads without owning
 * its concurrency contract would be a data race waiting to be measured.
 *
 * WHY A BOUNDED QUEUE OF DEPTH 4, NOT AN UNBOUNDED ONE:
 *
 * Every accepted request holds a gRPC handler thread blocked on `submit()`
 * for roughly 1.1 seconds (the measured wall-clock cost of one extraction).
 * An unbounded queue would let load pile up invisibly -- the caller-facing
 * symptom would be requests taking longer and longer to answer, with no
 * signal distinguishing "briefly busy" from "about to wait minutes", and no
 * way for a client to make an informed retry decision. A queue capped at 4
 * (roughly 4-5 seconds of backlog at the measured decode rate) makes the
 * choice explicit instead: once it is full, a request fails FAST with
 * RESOURCE_EXHAUSTED rather than joining an invisible line. A fast, honest
 * refusal beats an unbounded latency cliff -- the caller can retry with
 * backoff immediately, instead of discovering minutes later that its request
 * was one of dozens silently queued behind it.
 *
 * The queue lives on the calling gRPC threads' side: `submit()` never blocks
 * to make room. If the queue is already at depth 4 when a new request
 * arrives, it is turned away immediately, full stop -- the gRPC thread is
 * never parked waiting for space to open up.
 */
class AssistantWorker {
  public:
    [[nodiscard]] static auto instance() -> AssistantWorker& {
        static AssistantWorker worker;
        return worker;
    }

    /** True iff the model loaded successfully at process start. Immutable
     * after construction, so no synchronization is needed to read it. */
    [[nodiscard]] auto available() const noexcept -> bool { return available_; }

    /**
     * Submits one prompt for generation and blocks the CALLING thread (an
     * accepted gRPC handler thread) until the worker has produced a result.
     *
     * Returns `std::nullopt` iff the queue was already at capacity when this
     * call arrived -- the caller must map that, and only that, to
     * RESOURCE_EXHAUSTED. Any other outcome (including a failed generation)
     * comes back as an `InferenceOutcome` with `ok == false`.
     *
     * Blocking the calling thread on the RESULT of its own accepted job is
     * not the thing kMaxQueueDepth exists to prevent -- gRPC's synchronous
     * service model already dedicates one thread per in-flight call (see
     * every other handler in this codebase, e.g. FinanceServiceImpl), and
     * that thread doing nothing else while ITS OWN request is served is
     * exactly how a synchronous unary RPC is supposed to behave. What must
     * never happen is a thread blocking to make room for a request that was
     * never going to be accepted -- that is the queue-full case, handled by
     * returning immediately instead.
     */
    [[nodiscard]] auto submit(std::string prompt) -> std::optional<InferenceOutcome> {
        if (!available_) {
            // Defense in depth: RegisterAssistantService's caller is expected
            // to check available() first, but if this is ever reached anyway
            // there is no worker thread running to fulfil a queued job's
            // promise -- returning a populated failure here, rather than
            // enqueueing, is what stands between this and a permanent hang.
            return InferenceOutcome{.ok = false, .text = {}, .error = "model not loaded"};
        }

        // The promise/future pair is only constructed once there is
        // confirmed room in the queue -- the full-queue rejection path stays
        // as cheap as the "immediately" in the design brief implies, rather
        // than paying for setup work whose result is about to be discarded.
        std::future<InferenceOutcome> future;
        {
            const std::lock_guard lock{mutex_};
            if (queue_.size() >= kMaxQueueDepth) {
                return std::nullopt;
            }
            Job job;
            job.prompt = std::move(prompt);
            future = job.promise.get_future();
            queue_.push_back(std::move(job));
        }
        cv_.notify_one();
        return future.get();
    }

    // Owns a live worker thread and a loaded model; neither is sensible to
    // copy or move, and there is exactly one instance for the process
    // lifetime regardless.
    AssistantWorker(const AssistantWorker&) = delete;
    auto operator=(const AssistantWorker&) -> AssistantWorker& = delete;
    AssistantWorker(AssistantWorker&&) = delete;
    auto operator=(AssistantWorker&&) -> AssistantWorker& = delete;

    // `jthread`'s destructor requests a stop and joins automatically; `run()`
    // below reacts to that by draining and failing any still-queued jobs
    // before returning, so no promise is ever left unfulfilled. Declared
    // implicitly via the defaulted destructor, relying on `worker_` being
    // the LAST data member so every member it touches (`pipeline_`,
    // `queue_`, `mutex_`, `cv_`) is destroyed only after it has stopped.
    ~AssistantWorker() = default;

  private:
    struct Job {
        std::string prompt;
        std::promise<InferenceOutcome> promise;
    };

    AssistantWorker() {
        const auto path = env_string("MODEL_PATH");
        if (!path.has_value()) {
            logger::Logger::getInstance().warn(
                "MODEL_PATH is not set -- the strategy assistant will return a Refusal on "
                "every call. The calculator and finance services are unaffected.");
            return;
        }

        // Thread count for the pipeline's own inference thread pool -- NOT
        // the same axis as "one dedicated worker thread" above. That is
        // about how many OS threads may ever call into the pipeline at
        // once (exactly one, forever); this is how many threads the
        // pipeline itself is allowed to fan a single generation's matmuls
        // across internally. Both are real and independent.
        const int threads = env_positive_int("ASSISTANT_INFERENCE_THREADS", 4);

        try {
            pipeline_ = sensen::LLMPipeline::fromGGUF(*path)
                            .maxAgents(1)
                            .kvCacheMaxSeqLen(1024)
                            .numThreads(static_cast<std::size_t>(threads))
                            .build();
        } catch (const std::exception& e) {
            logger::Logger::getInstance().error(
                "Failed to load the strategy-assistant model from MODEL_PATH ({}): {} -- the "
                "strategy assistant will return a Refusal on every call. The calculator and "
                "finance services are unaffected.",
                *path, e.what());
            pipeline_.reset();
            return;
        } catch (...) {
            logger::Logger::getInstance().error(
                "Failed to load the strategy-assistant model from MODEL_PATH ({}): unknown "
                "error -- the strategy assistant will return a Refusal on every call.",
                *path);
            pipeline_.reset();
            return;
        }

        if (pipeline_ == nullptr) {
            logger::Logger::getInstance().error(
                "LLMPipeline::Builder::build() returned null for MODEL_PATH ({}) -- the "
                "strategy assistant will return a Refusal on every call.",
                *path);
            return;
        }

        available_ = true;
        worker_ = std::jthread([this](std::stop_token stoken) { run(stoken); });
        logger::Logger::getInstance().info(
            "Strategy-assistant model loaded from {} ({} inference threads); one dedicated "
            "worker thread started.",
            *path, threads);
    }

    auto run(std::stop_token stoken) -> void {
        while (!stoken.stop_requested()) {
            Job job;
            {
                std::unique_lock lock{mutex_};
                // The stop_token-aware overload of condition_variable_any::wait
                // registers a stop callback that notifies this condition
                // variable the moment a stop is requested, so this wakes
                // promptly on shutdown rather than only on the next enqueue.
                const bool has_job = cv_.wait(lock, stoken, [this] { return !queue_.empty(); });
                if (!has_job) {
                    // Woken by a stop request with nothing queued -- loop
                    // back to the top, where stop_requested() ends the loop.
                    continue;
                }
                job = std::move(queue_.front());
                queue_.pop_front();
            }

            InferenceOutcome outcome;
            try {
                sensen::GenerationConfig config;
                // Greedy, not sampled: this is structured extraction, not
                // creative generation. The training system prompt asks for
                // one JSON object or one question -- there is no case where
                // sampling diversity across candidates is wanted here, and
                // greedy decode is the more reproducible, more honest choice
                // for a tool that must never invent variation the trader
                // did not ask for.
                config.strategy = sensen::SamplingStrategy::GREEDY;
                config.max_new_tokens = kMaxNewTokens;
                config.deterministic = true;

                const auto result = pipeline_->generate(job.prompt, config);
                outcome.ok = true;
                outcome.text = result.text;
            } catch (const std::exception& e) {
                outcome.ok = false;
                outcome.error = e.what();
            } catch (...) {
                outcome.ok = false;
                outcome.error = "unknown inference failure";
            }
            job.promise.set_value(std::move(outcome));
        }

        // Shutdown: fail every job still queued so no caller blocked in
        // submit()'s future.get() hangs forever waiting on a promise this
        // worker will never fulfil.
        const std::lock_guard lock{mutex_};
        while (!queue_.empty()) {
            Job job = std::move(queue_.front());
            queue_.pop_front();
            job.promise.set_value(
                InferenceOutcome{.ok = false, .text = {}, .error = "assistant worker shutting down"});
        }
    }

    std::unique_ptr<sensen::LLMPipeline> pipeline_;
    bool available_ = false;

    std::mutex mutex_;
    std::condition_variable_any cv_;
    std::deque<Job> queue_;

    // Declared LAST: member destruction order is the reverse of declaration
    // order, so `worker_`'s destructor (request_stop + join) runs BEFORE
    // pipeline_/queue_/mutex_/cv_ are torn down, guaranteeing run() has
    // fully exited -- and therefore touched none of them -- by the time
    // they are destroyed.
    std::jthread worker_;
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

    // Every check above this line is free (string comparisons, catalogue
    // lookups); this one costs a network round trip, so it runs LAST -- a
    // request that was always going to be refused for its strategy, its
    // expiration or its quantity should not also pay for a live quote it
    // will never use.
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
            // ability to check them right now. MODEL_UNAVAILABLE is reused
            // rather than a new reason invented for it because its
            // documented contract already fits exactly: "the RPC to THIS
            // service still completed correctly... the caller needs that
            // distinction to decide whether retrying is even worthwhile" --
            // true here for the market-data backend precisely as it is for
            // the LLM backend.
            populate_refusal(response, calculator::assistant::Refusal::MODEL_UNAVAILABLE,
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
    // A `<think>` block is the tell-tale sign the system prompt did not take
    // effect (see kSystemPrompt's own comment) -- the model has reverted to
    // stock reasoning-then-refuse behaviour rather than the trained
    // params/question format. Surfacing that raw reasoning trace to a
    // trader would be both useless and a quality regression worse than an
    // honest refusal, so it is refused explicitly rather than accidentally
    // accepted as a "clarifying question".
    if (raw_text.find("<think>") != std::string::npos) {
        populate_refusal(response, calculator::assistant::Refusal::OUT_OF_SCOPE,
                         "The assistant could not produce a usable response for this request.");
        return;
    }

    if (const auto block = extract_params_block(raw_text); block.has_value()) {
        validate_and_populate_params(trim(*block), response);
        return;
    }

    const std::string question = trim(raw_text);
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
