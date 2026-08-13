export module sgee_queue_client;
import std;

import sgee.runtime.resilience;

/**
 * @author Olumuyiwa Oluwasanmi
 *
 * Serving-tier mirror client for the Stochastic Graph Execution Engine (SGEE) task queue.
 *
 * Provides non-blocking, mirror-mode enqueue capability parallel to Postgres inference_jobs.
 * Mirror writes land on a bounded in-process ring buffer drained by a background worker thread.
 * Payload cap is 256 KB. If the queue is full or unreachable, mirror payloads are dropped
 * without blocking or erroring the calling RPC. Circuit breaker and exponential backoff with jitter
 * protect downstream nodes from cascading failures.
 */

export enum class SgeeQueueMode : std::uint8_t { Off, Mirror };

export struct MirrorStats {
    std::uint64_t enqueued{0};
    std::uint64_t dropped{0};
    std::uint64_t redirects{0};
    std::string breaker_state;
};

/**
 * What a submitter learns by reading a task back. `terminal` false means the
 * task exists and has not finished; the other two fields are meaningless then.
 *
 * `found` false is a THIRD state and is deliberately not folded into the other
 * two: the queue reclaims terminal tasks past their retention window, so a task
 * that has disappeared is indistinguishable from one that never existed. A
 * submitter must treat that as a lost answer and fall back, never as a failure
 * the queue reported.
 */
export struct SgeeTaskOutcome {
    bool found{false};
    bool terminal{false};
    bool ok{false};       // Completed vs Dead; meaningless unless terminal
    std::string result;   // opaque bytes the worker recorded; empty is legitimate
};

/** One leased task, with the fencing token that must accompany its completion. */
export struct SgeeLeasedJob {
    std::uint64_t task_id{0};
    std::uint64_t local_token{0};
    std::uint64_t raft_term{0};
    std::uint64_t raft_index{0};
    std::string payload;
};

export class SgeeQueueClient {
  public:
    class Impl;

    SgeeQueueClient() = default;
    explicit SgeeQueueClient(std::shared_ptr<Impl> impl);

    static auto create_from_env() -> std::optional<SgeeQueueClient>;

    /**
     * The client the ADMISSION path uses (`INFERENCE_QUEUE=sgee`), as opposed to
     * the mirror path's `create_from_env()`.
     *
     * Separate because the two are gated on different variables and one must not
     * imply the other: `SGEE_QUEUE=mirror` asks for best-effort shadow writes,
     * while routing real inference through the cluster is a far larger promise.
     * A deployment can want either, both, or neither.
     */
    static auto create_for_admission() -> std::optional<SgeeQueueClient>;

    auto enqueue_mirror(std::string_view payload_json) noexcept -> void;
    [[nodiscard]] auto stats() const -> MirrorStats;

    // --- Synchronous surface (the admission path) ---------------------------
    //
    // Every one of these returns nullopt/false on ANY failure rather than an
    // error code, and that is the contract the caller needs: the admission path
    // answers every failure the same way, by degrading to the local backend. An
    // error channel here would only invite a caller to branch on faults that all
    // mean "the cluster did not answer; serve it yourself".
    //
    // All of them retry across peers and follow a NotLeader hint, exactly as the
    // mirror path does, and all are bounded in time -- nothing here can hang an
    // RPC handler.

    [[nodiscard]] auto submit_blocking(std::string_view payload) -> std::optional<std::uint64_t>;
    [[nodiscard]] auto poll_task(std::uint64_t task_id) -> std::optional<SgeeTaskOutcome>;
    [[nodiscard]] auto lease_blocking(std::uint64_t worker_id, std::uint64_t visibility_ms)
        -> std::optional<SgeeLeasedJob>;
    auto complete_blocking(SgeeLeasedJob const& job, std::string_view result) -> bool;
    auto fail_blocking(SgeeLeasedJob const& job) -> bool;

  private:
    std::shared_ptr<Impl> impl_;
};
