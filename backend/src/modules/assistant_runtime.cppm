module;

#include <unistd.h>

export module assistant_runtime;

import std;
import encoder_queue;
import inference_admission;
import inference_queue;
import logger;
import pg;
import sgee_queue_client;

/**
 * @author Olumuyiwa Oluwasanmi
 *
 * What an assistant needs to EXECUTE work and to SHARE it, in one place.
 *
 * The strategy and the mortgage assistants each carried the same seven members and the same
 * hundred and fifty lines around them: the decoder that executes on this replica, the encoder that
 * may execute instead, the Postgres pool and queue, the lease source, the stand-in backend of a
 * replica with no weights, and the admission object that submits. Measured by diffing the two
 * Worker classes after substituting the surface's name: `configure_sgee_queue` differed by ONE
 * integer (the worker-id parity) and one comment, `configure_inference_queue` by comments only.
 * That is a single piece of knowledge -- how an assistant's Worker joins the shared queue -- and
 * it lived twice, then a third time as each of five helpers (`local_executor`, `remote_deadline`,
 * `install_lease_source`, `encoder_answer`, `encoder_enabled`) added with the encoder.
 *
 * What stays in a Worker is what really differs between the assistants: how its model is chosen
 * and loaded, which environment variables name it, what its prompt and grammar are, and how its
 * answer is rendered. Everything below this line is the same for both.
 *
 * ORDER IS LOAD-BEARING AND IS THE CALLER'S. `configure_queue()` installs the lease source, and
 * `QueuedBackend::start()` documents that as part of construction: a decoder whose owner thread
 * starts before it leaves a worker leasing nothing while jobs pile up on a queue it is not yet
 * reading. So a Worker adopts its decoder, calls `configure_queue()`, and only then starts it.
 */
export namespace options_calculator::assistant_runtime {

class AssistantRuntime {
  public:
    /** @param label the prefix of every log line: "Strategy assistant", "Mortgage assistant". */
    AssistantRuntime(inference_queue::Surface surface, std::string label)
        : surface_(surface), label_(std::move(label)) {}

    AssistantRuntime(const AssistantRuntime&) = delete;
    auto operator=(const AssistantRuntime&) -> AssistantRuntime& = delete;
    AssistantRuntime(AssistantRuntime&&) = delete;
    auto operator=(AssistantRuntime&&) -> AssistantRuntime& = delete;
    ~AssistantRuntime() = default;

    // ---- what executes on this replica -----------------------------------------------------

    /** The decoder: one owner thread feeding a fused batch. A null pointer is ignored. */
    auto adopt(std::unique_ptr<inference_admission::QueuedBackend> decoder) -> void {
        decoder_ = std::move(decoder);
    }

    /** The encoder: no owner thread, any thread may call it. */
    auto adopt(std::unique_ptr<encoder_queue::EncoderService> encoder) -> void {
        encoder_ = std::move(encoder);
    }

    /** A view of the decoder this runtime OWNS, or nothing. The view is valid for as long as the
     *  runtime is, which is the life of the process: every Worker holds its runtime as a member of a
     *  function-local static. */
    [[nodiscard]] auto decoder() const noexcept
        -> std::optional<std::reference_wrapper<inference_admission::QueuedBackend>> {
        if (decoder_ == nullptr) return std::nullopt;
        return std::ref(*decoder_);
    }

    /** Non-null only when the small encoder was selected and loaded. Asked for one engine and
     *  unable to provide it, an assistant is UNAVAILABLE rather than quietly served by the other
     *  (the `ASSISTANT_BACKEND=llamacpp` rule), so a caller tests this rather than assuming. */
    [[nodiscard]] auto encoder() const noexcept
        -> std::optional<std::reference_wrapper<encoder_queue::EncoderService>> {
        if (encoder_ == nullptr) return std::nullopt;
        return std::ref(*encoder_);
    }

    // ---- answering --------------------------------------------------------------------------

    /**
     * Whether THIS process holds the weights, as opposed to being able to reach something that
     * does. Distinct from `available()` on purpose: the startup banner and the documented cutover
     * check (`grep -c 'model is LOADED'`, one line per replica per assistant) ask where the model
     * physically IS, and a submit-only replica answering "LOADED" would make that count describe
     * a fleet that does not exist -- the class of wrong-layer health signal this project has been
     * bitten by before.
     */
    [[nodiscard]] auto holds_model() const noexcept -> bool {
        return decoder_ != nullptr || encoder_ != nullptr;
    }

    /**
     * Whether the RPC can be served at all: this replica executes (the decoder or the encoder), or
     * it can submit to a shared queue that something else executes on (an admission object in
     * submit-only mode). Immutable after construction, so no synchronization is needed.
     *
     * The encoder HAD TO BE TAUGHT TO BOTH this and `holds_model()` separately, and forgetting one
     * is a measured defect: with only `holds_model()` updated, the mortgage service's boot banner
     * printed "model is LOADED" while every single RPC answered "not available right now" -- a
     * health signal from the wrong layer, across all 600 holdout rows.
     */
    [[nodiscard]] auto available() const noexcept -> bool {
        return holds_model() || admission_ != nullptr;
    }

    /** Whether a shared queue is configured (`INFERENCE_QUEUE=postgres|sgee` and it came up). */
    [[nodiscard]] auto queued() const noexcept -> bool { return admission_ != nullptr; }

    /** One decoder prompt: through the shared queue when there is one, which already falls back
     *  to the local decoder on any failure of that path, else straight to the decoder -- exactly
     *  as before the queue existed. */
    [[nodiscard]] auto submit(std::string prompt)
        -> std::optional<inference_admission::InferenceOutcome> {
        if (decoder_ == nullptr && admission_ == nullptr) {
            // Defense in depth: the RPC handler is expected to check available() first, but if
            // this is ever reached anyway there is no owner thread to fulfil a queued job's
            // promise and no queue to hand it to -- returning a populated failure here, rather
            // than enqueueing, is what stands between this and a permanent hang.
            return inference_admission::InferenceOutcome{
                .ok = false, .text = {}, .error = "model not loaded"};
        }
        if (admission_ != nullptr) return admission_->submit(std::move(prompt));
        return decoder_->submit(std::move(prompt));
    }

    /** One encoder exchange: through the shared queue when configured and a slot is free, in this
     *  process otherwise. Either way `EncoderBackend::answer()` decides what the chain's outcome
     *  means, so the two cannot disagree. Thread-safe. Only valid when an encoder was adopted. */
    [[nodiscard]] auto answer(const encoder_queue::EncoderRequest& request) -> encoder_queue::Answer {
        if (admission_ != nullptr) return encoder_->answer(*admission_, request);
        return encoder_->answer(request);
    }

    // ---- joining the shared queue -----------------------------------------------------------

    /**
     * `INFERENCE_QUEUE` selects `local` (the default; also anything unrecognised, which degrades
     * quietly rather than crashing), `postgres` or `sgee`.
     *
     * Any failure here leaves the assistant on local-only inference rather than half-configured:
     * a missing DATABASE_URL or an SGEE client that cannot be built is logged and this returns
     * with no admission object, which is indistinguishable from `local` at every call site. That
     * is the degrade-never-hang contract restated at configuration time -- a cluster that cannot
     * be reached must cost nothing more than the shared queue it would have provided.
     *
     * A replica that executes nothing (no decoder, no encoder) gets a SUBMIT-ONLY admission over
     * `NoLocalBackend` and installs no lease source: leasing is what commits a replica to
     * executing, and it has nothing to execute with.
     */
    auto configure_queue() -> void {
        const std::string mode = inference_admission::environment_text("INFERENCE_QUEUE").value_or("local");
        if (mode == "sgee") {
            configure_sgee();
        } else if (mode == "postgres") {
            configure_postgres();
        } else if (mode != "local") {
            logger::Logger::getInstance().warn(
                "INFERENCE_QUEUE=\"{}\" is not \"local\", \"postgres\" or \"sgee\" -- the {} stays "
                "on local-only inference.",
                mode, noun());
        }
    }

  private:
    /** "the strategy assistant" for the middle of a sentence. */
    [[nodiscard]] auto noun() const -> std::string {
        std::string out{label_};
        if (!out.empty()) out.front() = static_cast<char>(std::tolower(static_cast<unsigned char>(out.front())));
        return out;
    }

    /** What this replica's tasks are routed on: its surface, and -- for the encoder, whose answer
     *  depends on the build that computes it -- that build. A decoder carries none and routes on
     *  the surface alone, exactly as it always has. */
    [[nodiscard]] auto route_tag() const -> inference_admission::RouteTag {
        return {surface_, encoder_ != nullptr ? encoder_->fingerprint() : std::string{}};
    }

    /** The backend that EXECUTES here, or null on a submit-only replica. */
    [[nodiscard]] auto local_executor() noexcept
        -> std::optional<std::reference_wrapper<inference_admission::InferenceBackend>> {
        if (decoder_ != nullptr) return std::ref<inference_admission::InferenceBackend>(*decoder_);
        if (encoder_ != nullptr) return std::ref<inference_admission::InferenceBackend>(encoder_->backend());
        return std::nullopt;
    }

    /** How long a submitter waits on the shared queue before answering for itself: 90 s for a
     *  decode, `encoder_queue::kRemoteDeadline` for an encoder parse. It is a CEILING on a
     *  genuinely stuck request, not a target: the poll returns the instant the task is terminal. */
    [[nodiscard]] auto remote_deadline() const noexcept -> std::chrono::milliseconds {
        return encoder_ != nullptr ? encoder_queue::kRemoteDeadline : std::chrono::milliseconds(90000);
    }

    /** Hands shared work to whatever executes here. A decoder's owner thread draws it in its own
     *  loop; an encoder has no owner thread, so a runner drains the source into it. */
    auto install_lease_source(std::shared_ptr<inference_admission::LeaseSource> source) -> void {
        if (decoder_ != nullptr) {
            decoder_->set_lease_source(source);
        } else {
            encoder_->serve(source);
        }
        lease_source_ = std::move(source);
    }

    /** What an admission object degrades to. Executing replicas hand back their own executor;
     *  a replica with none gets the stand-in, which fails honestly instead of hanging. */
    [[nodiscard]] auto local_or_stand_in() -> inference_admission::InferenceBackend& {
        if (const auto local = local_executor(); local.has_value()) return local->get();
        no_local_ = std::make_unique<inference_admission::NoLocalBackend>();
        return *no_local_;
    }

    /** The worker id only has to be unique among live leaseholders; the pid is what both queues
     *  use. The surface is folded in so the two assistants of ONE process never collide on it. */
    [[nodiscard]] auto worker_id() const -> std::uint64_t {
        return static_cast<std::uint64_t>(::getpid()) * 2ULL + static_cast<std::uint64_t>(surface_);
    }

    auto configure_sgee() -> void {
        auto client = SgeeQueueClient::create_for_admission();
        if (!client.has_value()) {
            // create_for_admission() has already logged which variable was missing or unusable.
            logger::Logger::getInstance().warn(
                "INFERENCE_QUEUE=sgee was requested but no SGEE client could be built -- the {} "
                "degrades to local-only inference.",
                noun());
            return;
        }

        // Ordering matters: the lease source is installed BEFORE anything can submit. 90 s
        // visibility, matching the admission deadline for a decode: a shorter window would let the
        // cluster reclaim a task this worker is still decoding and hand it to someone else,
        // paying for the same inference twice and fencing out the answer that arrives first.
        const bool executes = local_executor().has_value();
        if (executes) {
            install_lease_source(std::make_shared<inference_admission::SgeeLeaseSource>(
                *client, route_tag(), worker_id(), /*visibility_ms=*/90000));
        }
        admission_ = std::make_unique<inference_admission::SgeeAdmission>(
            *client, route_tag(), local_or_stand_in(), remote_deadline());

        logger::Logger::getInstance().info(
            "{}: INFERENCE_QUEUE=sgee -- {} through the SGEE queue cluster (worker_id={})", label_,
            executes ? "submitting and leasing, with the local backend as fallback"
                     : "SUBMIT-ONLY (no local weights; never leases)",
            worker_id());
    }

    auto configure_postgres() -> void {
        const auto url = inference_admission::environment_text("DATABASE_URL");
        if (!url.has_value()) {
            logger::Logger::getInstance().warn(
                "INFERENCE_QUEUE=postgres was requested but DATABASE_URL is unset -- the {} "
                "degrades to local-only inference (its own decode loop, no shared queue).",
                noun());
            return;
        }

        // connect_timeout and statement_timeout are pg::PoolConfig's own defaults already --
        // restated, not overridden, so this is self-documenting against the mandated bounds
        // rather than a silent reliance on a default that could drift later.
        pg::PoolConfig pool_config;
        pool_config.conninfo = *url;
        pool_config.connect_timeout = std::chrono::milliseconds(2000);
        pool_config.statement_timeout = std::chrono::milliseconds(2000);
        // 16, not PoolConfig's own default of 4: this ONE pool is shared by every submitter's
        // submit_remote()/await_result() polling AND the worker's own lease()/complete() calls.
        // At max_concurrent=4 the worst-case simultaneous need is roughly 4 submitters + up to 4
        // write-back helpers + the lease loop -- comfortably under 16. Leaving it at 4 lets
        // Pool::acquire()'s bounded acquire_timeout start silently queuing requests for a
        // connection under ordinary load, which shows up as added latency, not as an error.
        // Revisit if MAX_CONCURRENT is raised well beyond 4.
        pool_config.size = 16;
        pool_ = std::make_shared<pg::Pool>(std::move(pool_config));
        queue_ = std::make_shared<inference_queue::Queue>(pool_);
        // LISTEN/NOTIFY is a wakeup hint only (see inference_queue.cppm's banner): a failure to
        // start it is logged and otherwise ignored, and await_result()'s poll loop stays correct,
        // just not sped up.
        if (auto pump = queue_->start_notify_pump(); !pump.has_value()) {
            logger::Logger::getInstance().warn(
                "inference_admission: {}'s LISTEN pump failed to start ({}) -- await_result() will "
                "still work correctly via its poll backstop, just not as promptly.",
                noun(), inference_queue::to_string(pump.error()));
        }
        // Without this an abandoned lease or a job that timed out while still pending sits until
        // some OTHER replica's ticker (or an operator) reaps it. Safe to start unconditionally even
        // though the other assistant starts its own: sweep_once()'s pg_try_advisory_lock makes
        // every ticker but one a no-op on any given tick, cluster-wide.
        queue_->start_sweep_ticker();

        const std::string worker = std::string(inference_queue::to_string(surface_)) + "-" +
                                   std::to_string(::getpid());
        const bool executes = local_executor().has_value();
        if (executes) {
            install_lease_source(std::make_shared<inference_admission::PostgresLeaseSource>(
                queue_, route_tag(), worker));
        }
        admission_ = std::make_unique<inference_admission::PostgresAdmission>(
            queue_, route_tag(), local_or_stand_in(), remote_deadline());

        logger::Logger::getInstance().info(
            "{}: INFERENCE_QUEUE=postgres -- {} the shared queue (worker_id={})", label_,
            executes ? "submitting through and leasing from, with the local backend as fallback"
                     : "SUBMIT-ONLY through (no local weights; never leases)",
            worker);
    }

    inference_queue::Surface surface_;
    std::string label_;

    // Declaration order is destruction order in reverse, and it matters: the admission object
    // references the executor (or the stand-in), the runner thread inside the encoder reads the
    // lease source, and the lease source holds the queue -- so the admission goes first and the
    // executor last.
    std::unique_ptr<inference_admission::QueuedBackend> decoder_;
    std::unique_ptr<encoder_queue::EncoderService> encoder_;
    std::shared_ptr<pg::Pool> pool_;
    std::shared_ptr<inference_queue::Queue> queue_;
    std::shared_ptr<inference_admission::LeaseSource> lease_source_;
    std::unique_ptr<inference_admission::InferenceBackend> no_local_;
    std::unique_ptr<inference_admission::InferenceBackend> admission_;
};

}  // namespace options_calculator::assistant_runtime
