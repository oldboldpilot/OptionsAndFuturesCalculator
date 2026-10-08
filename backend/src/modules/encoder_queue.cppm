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
 * service scales across backends, so an encoder parse is now a task like any other.
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
 * Bounds how many of THIS assistant's requests (on this replica) are on the shared queue at once;
 * the rest are answered in-process.
 *
 * WHY A BOUND AT ALL. Every queue operation is a replicated write and the leader serialises
 * them, so the queue has a ceiling -- measured at 30 / 10 / 3 requests per second for Raft
 * heartbeats of 10 / 50 / 300 ms (the deployed value is 300), against several thousand per
 * second for one replica answering for itself. Past that ceiling the queue does not merely
 * stop helping, it collapses: each request waits out the whole deadline, is answered locally
 * anyway, and leaves an ORPHAN task that a worker executes later for nobody -- three more
 * writes spent on an answer no one is waiting for, in the one resource that was already
 * saturated, which delays the next request further. Measured at 300 ms with 24 callers: 85
 * deadline expiries in 96 requests. A bound on what each replica may have outstanding keeps the
 * backlog -- and so the wait -- below the deadline at every timing measured.
 *
 * THE OVERFLOW IS NOT A FAILURE and is not logged per request: it is the replica doing the work
 * it was always able to do. Under the bound the queue still carries and shares every request it
 * can; above it the replica answers itself.
 */
class QueueSlots {
  public:
    explicit QueueSlots(std::size_t limit) noexcept : limit_(limit) {}

    [[nodiscard]] auto try_acquire() noexcept -> bool {
        auto current = in_flight_.load(std::memory_order_relaxed);
        while (current < limit_) {
            if (in_flight_.compare_exchange_weak(current, current + 1, std::memory_order_acquire,
                                                 std::memory_order_relaxed)) {
                return true;
            }
        }
        return false;
    }

    auto release() noexcept -> void { in_flight_.fetch_sub(1, std::memory_order_release); }

  private:
    std::size_t limit_;
    std::atomic<std::size_t> in_flight_{0};
};

/** The default bound, overridable with `ENCODER_QUEUE_MAX_IN_FLIGHT` for a cluster whose commit
 *  latency is known to be lower. */
inline constexpr std::size_t kDefaultMaxInFlight{1};

/**
 * What `ENCODER_QUEUE_MAX_IN_FLIGHT` says. Unset or empty is the default. Anything else must be a
 * whole number in base 10 and NOTHING else -- no sign, no space, no suffix -- and ZERO IS A VALUE:
 * `QueueSlots{0}` admits no request, so this replica never submits and answers every request
 * itself, while its runner still executes work other replicas submit. That is the one setting that
 * routes in-process without unwiring the queue, so an operator who reaches for it must get it.
 *
 * An unusable value is an ERROR rather than a fallback. This tree's rule for an operator switch
 * (`MORTGAGE_WEIGHT_STORE`, `MORTGAGE_RESTRICTED_PROJECTION`) is that a typo must not silently
 * serve the configuration the operator meant to leave: coercing `=abc` or `=0` to 1 left a replica
 * on a ~0.6 s request path with no line anywhere saying why.
 */
[[nodiscard]] inline auto parse_max_in_flight(const char* raw)
    -> std::expected<std::size_t, std::string> {
    if (raw == nullptr || *raw == '\0') return kDefaultMaxInFlight;
    const std::string_view text{raw};
    std::size_t parsed = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (ec != std::errc{} || end != text.data() + text.size()) {
        return std::unexpected(std::format(
            "ENCODER_QUEUE_MAX_IN_FLIGHT=\"{}\" is not a whole number (0 means never submit, "
            "unset means {})",
            text, kDefaultMaxInFlight));
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

    /** The ONE place the chain runs and its outcome acquires a meaning. Thread-safe: the chain is
     *  const (`EncoderAssistant` documents it) and nothing here is mutable. */
    [[nodiscard]] auto answer(const EncoderRequest& request) const -> Answer {
        auto parsed = chain_(request.turns());
        if (!parsed.has_value()) {
            return {.verdict = Verdict::Refused, .text = std::move(parsed.error())};
        }
        if (!parsed->has_value()) return {.verdict = Verdict::None, .text = {}};
        return {.verdict = Verdict::Params,
                .text = "<params>" + render_(**parsed) + "</params>"};
    }

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
    Chain chain_;
    Render render_;
    std::string label_;
};

/**
 * Everything one assistant needs on the encoder side, in one object: the chain as an
 * `InferenceBackend`, the bound on its use of the shared queue, and the runner that executes work
 * leased from that queue.
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
                   std::size_t max_in_flight, std::string fingerprint = {})
        : backend_(std::move(chain), std::move(render), std::move(label)), slots_(max_in_flight),
          fingerprint_(std::move(fingerprint)) {}

    /**
     * The service the engine builds: its bound comes from `ENCODER_QUEUE_MAX_IN_FLIGHT`, the
     * effective value is LOGGED, and an unusable one stops the process. Both assistants build
     * theirs here, so the rule is written once.
     */
    [[nodiscard]] static auto from_environment(EncoderBackend::Chain chain, EncoderBackend::Render render,
                                               std::string label, const std::filesystem::path& model)
        -> std::unique_ptr<EncoderService> {
        const char* const raw = std::getenv("ENCODER_QUEUE_MAX_IN_FLIGHT");
        const auto bound = parse_max_in_flight(raw);
        if (!bound.has_value()) {
            logger::Logger::getInstance().error(
                "{} -- refusing to start rather than guessing which bound was meant.", bound.error());
            std::exit(1);
        }
        logger::Logger::getInstance().info(
            "encoder_queue: {} keeps at most {} request(s) at a time on the shared queue and "
            "answers the rest in-process (ENCODER_QUEUE_MAX_IN_FLIGHT {})",
            label, *bound, (raw != nullptr && *raw != '\0') ? "set" : "unset: the default");
        return std::make_unique<EncoderService>(std::move(chain), std::move(render), std::move(label),
                                                *bound, build_fingerprint(model));
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
     * Answers one request: through the shared queue when there is one and this replica has a free
     * slot on it, in this process otherwise.
     *
     * `shared` is the service's admission object (`SgeeAdmission` / `PostgresAdmission`), or null in
     * `local` mode. When it is used, the request is SUBMITTED -- surface-tagged, leasable by any
     * replica -- and that admission object already degrades to `backend().submit()` on any submit
     * or poll failure. This adds the one failure it cannot see: an answer that comes back
     * undecodable. That is also answered locally, and logged, rather than surfacing as an error to a
     * caller who asked a question the process could answer in a millisecond.
     *
     * Thread-safe: the chain is const and the slot count is atomic.
     */
    [[nodiscard]] auto answer(inference_admission::InferenceBackend* shared,
                              const EncoderRequest& request) -> Answer {
        if (shared == nullptr) return backend_.answer(request);
        if (!slots_.try_acquire()) {
            // One line per request, like the service's own raw-output line, because this is the
            // line an operator reads to see HOW a request was answered; it is what makes "the
            // queue carried N of M" a count rather than an inference.
            logger::Logger::getInstance().info(
                "encoder_queue: {} answered in-process: this replica already has its share of "
                "requests on the shared queue",
                backend_.name());
            return backend_.answer(request);
        }
        struct Release {
            QueueSlots& slots;
            ~Release() { slots.release(); }
        } const release{slots_};

        const auto queued = shared->submit(encode_request(request));
        if (queued.has_value() && queued->ok) {
            if (auto decoded = decode_answer(queued->text); decoded.has_value()) {
                // The admission object degrades to this replica's own backend on any submit or
                // poll failure, and that outcome is ok == true like a queue success. Which one this
                // was is the admission's to say (`degraded`), and the line must say it: the
                // operator's `queued == executed` accounting is built from these.
                if (queued->degraded) {
                    logger::Logger::getInstance().info(
                        "encoder_queue: {} answered locally after the shared queue degraded",
                        backend_.name());
                } else {
                    logger::Logger::getInstance().info(
                        "encoder_queue: {} answered through the shared queue", backend_.name());
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

  private:
    EncoderBackend backend_;
    QueueSlots slots_;
    std::string fingerprint_;
    /** Declared last: destroyed first, so the thread stops before anything it reads goes. */
    std::unique_ptr<inference_admission::LeaseRunner> runner_;
};

}  // namespace options_calculator::encoder_queue
