export module encoder_queue;

import std;
import fastjson;
import logger;
import inference_admission;
import sensen.encoder_assistant;

/**
 * @author Olumuyiwa Oluwasanmi
 *
 * The small ENCODER assistants, expressed as an `InferenceBackend` so they ride the same shared
 * queue the decoders do.
 *
 * WHAT WAS MISSING. With `INFERENCE_QUEUE=sgee` the encoders never touched the queue: the
 * service called `EncoderAssistant::parse()` in-process, and `admission_` was consulted only for
 * decoder prompts. Measured in production on 2026-10-07, ~120 assistant calls advanced the three
 * nodes' `last_applied` by 10 -- housekeeping. The owner's decision is that the queue is how the
 * service scales across backends, so an encoder parse can be a task like any other.
 *
 * WHEN A REQUEST GOES THERE (decided 2026-10-07, after the first routing was measured): a request
 * is answered IN-PROCESS and spills to the queue only when the replica is BUSY -- its own chain
 * already has `kDefaultLocalMaxInFlight` executions in flight. The first routing sent every request
 * to the queue first; at the deployed 300 ms Raft heartbeat that was ~570 ms per request against
 * ~1 ms in-process, for sharing no caller was waiting on. See `EncoderService::answer`.
 *
 * ONE RULE, ONE PLACE. `EncoderBackend::answer()` is the only code that runs the chain and
 * decides what its outcome means. Local mode calls it directly; the queue's worker calls it
 * through `submit()`; the queue's own degrade path calls it through `submit()` too. There is no
 * second implementation for the answers to drift between, which is what makes "the same answer
 * from the queue as from the process" a property of the design rather than something to test for
 * after the fact (it is tested for anyway).
 *
 * THE WIRE. A request travels as the `prompt` string the queue already carries; an answer comes
 * back as the `text` string it already returns. Both are small JSON objects owned by this module,
 * so the surface tag, the lease filter, the fencing and the write-back are untouched.
 *
 * A CHAIN REFUSAL IS AN ANSWER, NOT A FAILURE. `EncoderAssistant::parse()` refuses
 * deterministically -- a literal that straddles a token, an operation the schema cannot name --
 * and the same request refuses the same way on every replica. Reporting that to the cluster as a
 * failed task would burn the task's attempts on re-leases that cannot change the outcome and
 * leave the submitter waiting on a Dead task; the queue's own lesson (a deterministic rejection
 * is an answer, retrying it froze the cluster) applies unchanged. So all three outcomes --
 * params, `<NONE>`, and a refusal with its reason -- complete the task successfully.
 */
export namespace options_calculator::encoder_queue {

/**
 * How long a submitter waits on the shared queue for an encoder answer before computing it
 * itself. A ceiling for a genuinely stuck request, not a target: a healthy replica answers in
 * tens of milliseconds. The decoders keep 90 s because a decode takes seconds; an encoder parse
 * takes about a millisecond, so waiting a minute and a half on a cluster that has stopped
 * answering would cost the visitor far more than the answer it is waiting to avoid.
 */
inline constexpr std::chrono::milliseconds kRemoteDeadline{2000};

/** One exchange, owning its strings. `Turns` holds views, which cannot cross a queue. */
struct EncoderRequest {
    std::string utterance;
    std::string prior_question;
    std::string prior_clarification;

    [[nodiscard]] auto turns() const noexcept -> sensen::encoder_assistant::Turns {
        return {.utterance = utterance,
                .prior_question = prior_question,
                .prior_clarification = prior_clarification};
    }

    [[nodiscard]] auto operator==(const EncoderRequest&) const -> bool = default;
};

/** What the chain concluded. Three outcomes, because the services render each differently. */
enum class Verdict : std::uint8_t {
    Params,   ///< `text` is the model-text block the decoder path also produces: `<params>{...}</params>`
    None,     ///< the model named `<NONE>`: no calculation recognised; `text` is empty
    Refused,  ///< the chain declined this request; `text` is its reason, for the log or the caller
};

struct Answer {
    Verdict verdict{Verdict::None};
    std::string text;

    [[nodiscard]] auto operator==(const Answer&) const -> bool = default;
};

namespace detail {

[[nodiscard]] constexpr auto verdict_name(Verdict v) noexcept -> std::string_view {
    switch (v) {
        case Verdict::Params: return "params";
        case Verdict::None: return "none";
        case Verdict::Refused: return "refused";
    }
    return "none";
}

[[nodiscard]] constexpr auto verdict_from_name(std::string_view name) noexcept
    -> std::optional<Verdict> {
    for (const auto v : {Verdict::Params, Verdict::None, Verdict::Refused}) {
        if (verdict_name(v) == name) return v;
    }
    return std::nullopt;
}

/** The string member `key` of a parsed object, or an error naming what was wrong with it. */
[[nodiscard]] inline auto string_member(const fastjson::json_value& obj, const char* key)
    -> std::expected<std::string, std::string> {
    if (!obj.contains(key) || !obj[key].is_string()) {
        return std::unexpected(std::format("missing string member \"{}\"", key));
    }
    return std::string(obj[key].as_string());
}

[[nodiscard]] inline auto parse_object(std::string_view text, std::string_view what)
    -> std::expected<fastjson::json_value, std::string> {
    auto parsed = fastjson::parse(text);
    if (!parsed.has_value() || !parsed->is_object()) {
        return std::unexpected(std::format("{} is not a JSON object", what));
    }
    return std::move(parsed.value());
}

}  // namespace detail

/** The `prompt` string an encoder task carries. */
[[nodiscard]] inline auto encode_request(const EncoderRequest& request) -> std::string {
    fastjson::json_object obj;
    obj["utterance"] = fastjson::json_value(request.utterance);
    obj["prior_question"] = fastjson::json_value(request.prior_question);
    obj["prior_clarification"] = fastjson::json_value(request.prior_clarification);
    return fastjson::json_value(std::move(obj)).to_string();
}

/** The inverse of `encode_request`. Refuses anything else -- in particular a DECODER's prompt,
 *  so a worker handed the wrong kind of task fails it by name instead of parsing nonsense. */
[[nodiscard]] inline auto decode_request(std::string_view prompt)
    -> std::expected<EncoderRequest, std::string> {
    return detail::parse_object(prompt, "an encoder task prompt")
        .and_then([](const fastjson::json_value& obj) -> std::expected<EncoderRequest, std::string> {
            auto utterance = detail::string_member(obj, "utterance");
            auto question = detail::string_member(obj, "prior_question");
            auto reply = detail::string_member(obj, "prior_clarification");
            if (!utterance) return std::unexpected(utterance.error());
            if (!question) return std::unexpected(question.error());
            if (!reply) return std::unexpected(reply.error());
            return EncoderRequest{.utterance = std::move(*utterance),
                                  .prior_question = std::move(*question),
                                  .prior_clarification = std::move(*reply)};
        });
}

/** The `text` string an encoder task's result carries. */
[[nodiscard]] inline auto encode_answer(const Answer& answer) -> std::string {
    fastjson::json_object obj;
    obj["verdict"] = fastjson::json_value(std::string(detail::verdict_name(answer.verdict)));
    obj["text"] = fastjson::json_value(answer.text);
    return fastjson::json_value(std::move(obj)).to_string();
}

[[nodiscard]] inline auto decode_answer(std::string_view text)
    -> std::expected<Answer, std::string> {
    return detail::parse_object(text, "an encoder task result")
        .and_then([](const fastjson::json_value& obj) -> std::expected<Answer, std::string> {
            auto name = detail::string_member(obj, "verdict");
            auto body = detail::string_member(obj, "text");
            if (!name) return std::unexpected(name.error());
            if (!body) return std::unexpected(body.error());
            const auto verdict = detail::verdict_from_name(*name);
            if (!verdict) return std::unexpected(std::format("unknown verdict \"{}\"", *name));
            return Answer{.verdict = *verdict, .text = std::move(*body)};
        });
}

/**
 * How many things are executing right now, with an atomic "enter only if there is room".
 *
 * ONE counter type for the two bounds this module keeps, because each is the same question -- "is
 * this many already in flight?" -- asked of a different thing:
 *
 *  - the CHAIN on this replica (`EncoderBackend`): how many parses are executing in this process,
 *    whether for this replica's own requests or for work leased from the shared queue. That count
 *    is what "busy" means, and it is measured rather than configured: it is the thing that rises
 *    when the replica is loaded.
 *  - the SHARED QUEUE (`EncoderService`): how many of this assistant's requests are outstanding on
 *    it. See `kDefaultMaxInFlight` for why that one is bounded at all.
 *
 * `try_enter(limit)` is exact (a compare-exchange loop), so a bound of N is never exceeded by a
 * race, and a limit of 0 admits nothing.
 */
class InFlightCount {
  public:
    [[nodiscard]] auto try_enter(std::size_t limit) noexcept -> bool {
        auto current = count_.load(std::memory_order_relaxed);
        while (current < limit) {
            if (count_.compare_exchange_weak(current, current + 1, std::memory_order_acquire,
                                             std::memory_order_relaxed)) {
                return true;
            }
        }
        return false;
    }

    /** Enters unconditionally: work that has to run whatever the bound says still has to be COUNTED,
     *  or the next arrival would read an idle replica that is not. */
    auto enter() noexcept -> void { count_.fetch_add(1, std::memory_order_acquire); }

    auto leave() noexcept -> void { count_.fetch_sub(1, std::memory_order_release); }

    [[nodiscard]] auto load() const noexcept -> std::size_t {
        return count_.load(std::memory_order_relaxed);
    }

  private:
    std::atomic<std::size_t> count_{0};
};

namespace detail {

/** Leaves a count it has entered, on every path out of the scope. */
struct LeaveOnExit {
    InFlightCount& count;
    ~LeaveOnExit() { count.leave(); }
};

}  // namespace detail

/**
 * THE QUEUE-SIDE BOUND. Every queue operation is a replicated write and the leader serialises
 * them, so the queue has a ceiling -- measured at 30 / 10 / 3 requests per second for Raft
 * heartbeats of 10 / 50 / 300 ms (the deployed value is 300), against several thousand per
 * second for one replica answering for itself. Past that ceiling the queue does not merely
 * stop helping, it collapses: each request waits out the whole deadline, is answered locally
 * anyway, and leaves an ORPHAN task that a worker executes later for nobody -- three more
 * writes spent on an answer no one is waiting for, in the one resource that was already
 * saturated, which delays the next request further. Measured at 300 ms with 24 callers: 85
 * deadline expiries in 96 requests. A bound on what each assistant may have outstanding keeps the
 * backlog -- and so the wait -- below the deadline at every timing measured.
 *
 * Overridable with `ENCODER_QUEUE_MAX_IN_FLIGHT`; 0 means this assistant never submits.
 */
inline constexpr std::size_t kDefaultMaxInFlight{1};

/**
 * THE LOCAL BOUND, and what "busy" means. A replica answers an encoder request in-process while
 * fewer than this many chain executions are in flight on it; at this many it is busy and the
 * request spills to the shared queue (if this assistant has a queue slot free).
 *
 * MEASURED, not chosen: `scripts/encoder_local_sweep.sh`, one engine in `local` mode, three rounds
 * (the order of the concurrencies reversed on alternate rounds), 32-CPU host, medians. Client
 * concurrency 1 / 2 / 4 / 8 / 16 / 32 / 64:
 *
 *     mortgage   p50 0.87 / 0.95 / 1.03 / 1.24 / 2.08 / 4.79 / 8.81 ms    1134 / 2029 / 3636 / 5870 / 6887 / 6284 / 6498 rps
 *     strategy   p50 0.30 / 0.35 / 0.45 / 0.64 / 1.57 / 3.29 / 7.70 ms    3151 / 5331 / 8102 / 10761 / 9376 / 8847 / 7617 rps
 *
 * Throughput stops growing between 8 and 16 (strategy peaks AT 8, mortgage at 16) and latency then
 * doubles with every doubling of callers, which is a replica with nothing left to give. 8 is the
 * highest concurrency at which p50 is still about twice its idle value on both surfaces (1.4x and
 * 2.1x) while throughput is already 85-100% of peak; at 16 the strategy p50 is 5x idle for 87% of
 * its peak. So below 8 a request is answered in about a millisecond.
 *
 * The bound counts chain executions, which is fewer than client concurrency (gRPC framing and the
 * service's own work run outside the chain), so it is reached rarely: the benchmark with 24 callers
 * spilled 0 or 1 request in 243 at the default.
 *
 * It is NOT lower, because the queue is never faster: even at 64 callers the in-process p50 is
 * 7-9 ms against 75-150 ms (50 ms heartbeat) to 570 ms (300 ms heartbeat) for a queued parse. A
 * spill helps a saturated replica by taking work OFF it, not by finishing sooner, so it should
 * start where the replica stops scaling and no earlier. Re-measure on the real host before trusting
 * the number there; `ENCODER_LOCAL_MAX_IN_FLIGHT` overrides it, and 0 means every request is busy
 * (the pre-2026-10-07 routing, queue first): the lever for measuring the queue or forcing a request
 * through it.
 */
inline constexpr std::size_t kDefaultLocalMaxInFlight{8};

/** The two bounds an `EncoderService` routes on. */
struct Bounds {
    std::size_t local{kDefaultLocalMaxInFlight};
    std::size_t queue{kDefaultMaxInFlight};
};

/**
 * What an operator's `<variable>` says. Unset or empty is `fallback`. Anything else must be a whole
 * number in base 10 and NOTHING else -- no sign, no space, no suffix -- and ZERO IS A VALUE
 * (`zero_means` says what it does for this variable).
 *
 * An unusable value is an ERROR rather than a fallback. This tree's rule for an operator switch
 * (`MORTGAGE_WEIGHT_STORE`, `MORTGAGE_RESTRICTED_PROJECTION`) is that a typo must not silently
 * serve the configuration the operator meant to leave: coercing `=abc` or `=0` to 1 left a replica
 * on a ~0.6 s request path with no line anywhere saying why.
 */
[[nodiscard]] inline auto parse_in_flight_bound(std::string_view variable,
                                                const std::optional<std::string>& raw,
                                                std::size_t fallback, std::string_view zero_means)
    -> std::expected<std::size_t, std::string> {
    if (!raw.has_value() || raw->empty()) return fallback;
    const std::string_view text{*raw};
    std::size_t parsed = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (ec != std::errc{} || end != text.data() + text.size()) {
        return std::unexpected(std::format(
            "{}=\"{}\" is not a whole number (0 means {}, unset means {})", variable, text,
            zero_means, fallback));
    }
    return parsed;
}

namespace detail {

/** A 64-bit digest of a file's bytes, or nullopt when it cannot be read. `std::hash` is enough:
 *  this identifies a build for routing, it is not a defence against anyone forging one. */
[[nodiscard]] inline auto file_digest(const std::filesystem::path& path)
    -> std::optional<std::uint64_t> {
    std::ifstream in{path, std::ios::binary};
    if (!in) return std::nullopt;
    std::string bytes{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    if (in.bad() || bytes.empty()) return std::nullopt;
    return std::hash<std::string_view>{}(bytes);
}

}  // namespace detail

/**
 * What identifies the build that will answer an encoder request, for the queue to route on.
 *
 * Two digests: the model file the chain loaded (weights, schema, tokenizer, normaliser -- one GGUF),
 * and the running executable (the chain's code, the lexer, the renderer, every rule between the
 * model's output and the text a caller reads). Neither alone is enough: a renderer change leaves
 * the GGUF untouched, and a retrained model leaves the code untouched. And NEITHER IS A NUMBER
 * SOMEONE BUMPS -- both are DERIVED from the bytes, so a change cannot ship without moving
 * them, which is why a hand-maintained "renderer version" constant was not used.
 *
 * Replicas of one deployment run one image and so share a fingerprint and share work. Across a
 * blue/green overlap the old and new engines carry different ones and never lease each other's
 * tasks, which is the point (see `RouteTag`).
 *
 * If either file cannot be read the build's identity cannot be established, and the safe answer is
 * ISOLATION: a fingerprint nobody else can have, so this replica only ever executes what it
 * submitted itself. It still answers every request; it just stops sharing, and says so.
 */
[[nodiscard]] inline auto build_fingerprint(const std::filesystem::path& model,
                                            const std::filesystem::path& executable = "/proc/self/exe")
    -> std::string {
    const auto model_digest = detail::file_digest(model);
    const auto executable_digest = detail::file_digest(executable);
    if (model_digest.has_value() && executable_digest.has_value()) {
        return std::format("{:016x}.{:016x}", *model_digest, *executable_digest);
    }
    std::random_device entropy;
    const auto token = (static_cast<std::uint64_t>(entropy()) << 32) | entropy();
    logger::Logger::getInstance().warn(
        "encoder_queue: cannot read {} -- this replica's build cannot be fingerprinted, so it is "
        "ISOLATED on the shared queue: it answers its own requests and executes no other replica's",
        model_digest.has_value() ? executable.string() : model.string());
    return std::format("unshared.{:016x}", token);
}

/**
 * Runs the encoder chain for one surface and speaks the queue's `InferenceBackend` contract.
 *
 * The chain and the renderer are INJECTED, not named: the chain is `EncoderAssistant::parse` in
 * production and a stub in the test that has no model, and the renderer is the SERVICE's
 * (`encoder_params_to_json`), because the JSON type of a field is a fact about the proto that
 * the service owns -- the same split `encoder_assistant` itself documents.
 */
class EncoderBackend final : public inference_admission::InferenceBackend {
  public:
    using Chain = std::function<std::expected<std::optional<sensen::encoder_assistant::Parsed>,
                                              std::string>(const sensen::encoder_assistant::Turns&)>;
    using Render = std::function<std::string(const sensen::encoder_assistant::Parsed&)>;

    /** @param label what the logs call this backend: "mortgage encoder", "strategy encoder". */
    EncoderBackend(Chain chain, Render render, std::string label)
        : chain_(std::move(chain)), render_(std::move(render)), label_(std::move(label)) {}

    /** The ONE place the chain runs and its outcome acquires a meaning, for work that must run
     *  whatever the load: a leased job, a degrade, an overflow. Thread-safe: the chain is const
     *  (`EncoderAssistant` documents it) and the only state here is the atomic in-flight count. */
    [[nodiscard]] auto answer(const EncoderRequest& request) const -> Answer {
        in_flight_.enter();
        const detail::LeaveOnExit leave{in_flight_};
        return run(request);
    }

    /**
     * The same, but only while fewer than `limit` chain executions are in flight on this replica;
     * `nullopt` means BUSY and nothing ran. The count includes work leased from the shared queue,
     * because a core spent on another replica's parse is a core this replica's own request does
     * not have -- so "busy" is what the replica is actually doing, measured here, rather than a
     * rate someone configured. Exact: a race cannot admit one more than `limit`.
     */
    [[nodiscard]] auto answer_below(std::size_t limit, const EncoderRequest& request) const
        -> std::optional<Answer> {
        if (!in_flight_.try_enter(limit)) return std::nullopt;
        const detail::LeaveOnExit leave{in_flight_};
        return run(request);
    }

    /** Chain executions in flight right now: the measured signal "busy" is read from. */
    [[nodiscard]] auto in_flight() const noexcept -> std::size_t { return in_flight_.load(); }

    /** The queue's entry point: wire request in, wire answer out. Never `nullopt` -- an encoder
     *  has no admission queue to be full -- and `ok == false` only for a prompt that is not an
     *  encoder task at all. */
    [[nodiscard]] auto submit(std::string prompt)
        -> std::optional<inference_admission::InferenceOutcome> override {
        auto request = decode_request(prompt);
        if (!request.has_value()) {
            return inference_admission::InferenceOutcome{
                .ok = false, .text = {}, .error = std::move(request.error())};
        }
        return inference_admission::InferenceOutcome{
            .ok = true, .text = encode_answer(answer(*request)), .error = {}};
    }

    [[nodiscard]] auto name() const noexcept -> std::string_view override { return label_; }

  private:
    /** The chain and its meaning. The caller has already entered `in_flight_`. */
    [[nodiscard]] auto run(const EncoderRequest& request) const -> Answer {
        auto parsed = chain_(request.turns());
        if (!parsed.has_value()) {
            return {.verdict = Verdict::Refused, .text = std::move(parsed.error())};
        }
        if (!parsed->has_value()) return {.verdict = Verdict::None, .text = {}};
        return {.verdict = Verdict::Params,
                .text = "<params>" + render_(**parsed) + "</params>"};
    }

    Chain chain_;
    Render render_;
    std::string label_;
    mutable InFlightCount in_flight_;
};

/**
 * Everything one assistant needs on the encoder side, in one object: the chain as an
 * `InferenceBackend`, the bounds that route a request, and the runner that executes work leased from
 * the shared queue.
 *
 * ONE OBJECT BECAUSE THEIR LIFETIMES ARE ONE LIFETIME. The runner's thread reads the backend, and
 * the backend's chain reads the encoder; declaration order here is the destruction order that makes
 * that safe, and it used to be restated in each assistant's worker. The assistant itself is owned by
 * the chain's closure (a `shared_ptr`), so no caller has to keep it alive beside this.
 */
class EncoderService {
  public:
    /** @param fingerprint what the queue routes this service's tasks on (`build_fingerprint`);
     *         empty for a service that is never put on a queue. */
    EncoderService(EncoderBackend::Chain chain, EncoderBackend::Render render, std::string label,
                   Bounds bounds, std::string fingerprint = {})
        : backend_(std::move(chain), std::move(render), std::move(label)), bounds_(bounds),
          fingerprint_(std::move(fingerprint)) {}

    /**
     * The service the engine builds: its bounds come from `ENCODER_LOCAL_MAX_IN_FLIGHT` and
     * `ENCODER_QUEUE_MAX_IN_FLIGHT`, the effective values are LOGGED, and an unusable one stops the
     * process. Both assistants build theirs here, so the rule is written once.
     */
    [[nodiscard]] static auto from_environment(EncoderBackend::Chain chain, EncoderBackend::Render render,
                                               std::string label, const std::filesystem::path& model)
        -> std::unique_ptr<EncoderService> {
        const auto raw_local = inference_admission::environment_text("ENCODER_LOCAL_MAX_IN_FLIGHT");
        const auto raw_queue = inference_admission::environment_text("ENCODER_QUEUE_MAX_IN_FLIGHT");
        const auto local = parse_in_flight_bound("ENCODER_LOCAL_MAX_IN_FLIGHT", raw_local,
                                                 kDefaultLocalMaxInFlight,
                                                 "every request spills to the queue first");
        const auto queue = parse_in_flight_bound("ENCODER_QUEUE_MAX_IN_FLIGHT", raw_queue,
                                                 kDefaultMaxInFlight, "never submit");
        const auto refuse_if_unusable = [](const std::expected<std::size_t, std::string>& bound) {
            if (bound.has_value()) return;
            logger::Logger::getInstance().error(
                "{} -- refusing to start rather than guessing which bound was meant.", bound.error());
            std::exit(1);
        };
        refuse_if_unusable(local);
        refuse_if_unusable(queue);
        const auto source = [](const std::optional<std::string>& raw) {
            return raw.has_value() ? "set" : "unset: the default";
        };
        logger::Logger::getInstance().info(
            "encoder_queue: {} answers in-process, and spills to the shared queue only when {} are "
            "already executing here -- at most {} at a time (ENCODER_LOCAL_MAX_IN_FLIGHT {}, "
            "ENCODER_QUEUE_MAX_IN_FLIGHT {})",
            label, *local, *queue, source(raw_local), source(raw_queue));
        return std::make_unique<EncoderService>(std::move(chain), std::move(render), std::move(label),
                                                Bounds{.local = *local, .queue = *queue},
                                                build_fingerprint(model));
    }

    /** What the shared queue routes this service's tasks on. */
    [[nodiscard]] auto fingerprint() const noexcept -> const std::string& { return fingerprint_; }

    /** The local executor: what an admission object degrades to and what a lease runner executes. */
    [[nodiscard]] auto backend() noexcept -> EncoderBackend& { return backend_; }

    /** From now on this replica executes encoder work leased from `source`. */
    auto serve(std::shared_ptr<inference_admission::LeaseSource> source) -> void {
        runner_ = std::make_unique<inference_admission::LeaseRunner>(std::move(source), backend_,
                                                                     std::string(backend_.name()));
    }

    /**
     * Answers one request: IN THIS PROCESS, unless the replica is busy, in which case it spills to
     * the shared queue.
     *
     * WHY IN-PROCESS FIRST. A parse costs about a millisecond here and a queued one costs about a
     * Raft heartbeat per replicated write (three of them): 75-150 ms at 50 ms, ~570 ms at the
     * deployed 300 ms, and the queue saturates near 3 requests a second against thousands for a
     * replica answering for itself. Routing every request through it first made the one-visitor case
     * ~600x slower, to share work nobody was waiting on. The queue's value is taking work
     * OFF a replica that has none to give, so a request goes there only when the replica is BUSY:
     * `bounds.local` chain executions already in flight (see `kDefaultLocalMaxInFlight`).
     *
     * A busy request is submitted when this assistant has a queue slot free (`bounds.queue`), and
     * answered in-process anyway when it does not -- the overflow is the replica doing the work it
     * was always able to do. The submitted request is surface-tagged, build-routed and leasable by
     * any replica of the same build; the admission object degrades to `backend().submit()` on any
     * submit or poll failure, and this adds the one failure it cannot see, an answer that comes back
     * undecodable, which is also answered locally rather than surfaced to a caller who asked a
     * question the process could answer in a millisecond.
     *
     * `shared` is the service's admission object (`SgeeAdmission` / `PostgresAdmission`); in `local`
     * mode there is none and the other overload is used.
     *
     * ONE of three lines is written per request, because "how was this answered" is a count an
     * operator reads, not an inference: `answered in-process`, `spilled to the queue ...` (the queue
     * answered) and `answered locally after the shared queue degraded` (it was tried and failed, and
     * the admission object answered for itself). The last two look identical to the caller -- a
     * degrade is ok=true with a decodable answer -- so only the layer that knows says. (A queue that
     * returns NOTHING usable writes a WARN instead of the second or third, and answers locally.)
     *
     * Thread-safe: the chain is const and the counts are atomic.
     */
    [[nodiscard]] auto answer(inference_admission::InferenceBackend& shared,
                              const EncoderRequest& request) -> Answer {
        if (auto local = backend_.answer_below(bounds_.local, request); local.has_value()) {
            logger::Logger::getInstance().info("encoder_queue: {} answered in-process",
                                               backend_.name());
            return std::move(*local);
        }
        return spill(shared, request);
    }

    /** `local` mode: no queue exists, so the answer is always in-process, and nothing is logged. */
    [[nodiscard]] auto answer(const EncoderRequest& request) -> Answer {
        return backend_.answer(request);
    }

  private:
    /** The replica is busy: send the request to the shared queue if this assistant may. */
    [[nodiscard]] auto spill(inference_admission::InferenceBackend& shared,
                             const EncoderRequest& request) -> Answer {
        // The measured signal the decision was made on, in the line that reports the decision: an
        // operator reading "spilled" can see how busy the replica was without a profiler.
        const auto busy_at = backend_.in_flight();
        if (!queue_in_flight_.try_enter(bounds_.queue)) {
            logger::Logger::getInstance().info(
                "encoder_queue: {} answered in-process: busy here ({} in flight), and this assistant's "
                "share of the shared queue is in use",
                backend_.name(), busy_at);
            return backend_.answer(request);
        }
        const detail::LeaveOnExit leave{queue_in_flight_};

        const auto queued = shared.submit(encode_request(request));
        if (queued.has_value() && queued->ok) {
            if (auto decoded = decode_answer(queued->text); decoded.has_value()) {
                // The admission object degrades to this replica's own backend on any submit or
                // poll failure, and that outcome is ok == true like a queue success. Which one this
                // was is the admission's to say (`degraded`), and the line must say it.
                if (queued->degraded) {
                    logger::Logger::getInstance().info(
                        "encoder_queue: {} answered locally after the shared queue degraded",
                        backend_.name());
                } else {
                    logger::Logger::getInstance().info(
                        "encoder_queue: {} spilled to the queue and was answered by it (busy: {} in "
                        "flight here)",
                        backend_.name(), busy_at);
                }
                return std::move(*decoded);
            } else {
                logger::Logger::getInstance().warn(
                    "encoder_queue: the shared queue returned an undecodable answer ({}) -- "
                    "answering locally instead",
                    decoded.error());
            }
        } else {
            logger::Logger::getInstance().warn(
                "encoder_queue: the shared queue produced no usable answer ({}) -- answering "
                "locally instead",
                queued.has_value() ? queued->error : std::string("admission refused"));
        }
        return backend_.answer(request);
    }

    EncoderBackend backend_;
    Bounds bounds_;
    InFlightCount queue_in_flight_;
    std::string fingerprint_;
    /** Declared last: destroyed first, so the thread stops before anything it reads goes. */
    std::unique_ptr<inference_admission::LeaseRunner> runner_;
};

}  // namespace options_calculator::encoder_queue
