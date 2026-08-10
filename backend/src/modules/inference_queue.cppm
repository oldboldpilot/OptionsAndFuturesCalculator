module;
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

export module inference_queue;

import pg;

/**
 * @author Olumuyiwa Oluwasanmi
 *
 * The shared inference queue's client- and worker-facing protocol, built on
 * pg.cppm's connection pool. This module is Postgres-backed persistence and
 * protocol ONLY -- it does not run inference, does not know what a
 * "strategy" or a "mortgage operation" IS beyond the two literal strings
 * that name them, and is not imported by assistant_service.cpp or
 * mortgage_assistant_service.cpp. Wiring an actual caller and an actual
 * worker onto this substrate is a later, separate change; see this module's
 * own file banner in the task that produced it for why that separation is
 * deliberate.
 *
 * Table: public.inference_jobs (backend/migrations/02_inference_queue.sql).
 * Fencing: public.inference_fence, a single cluster-wide sequence -- see
 * that migration's own comment for why a sequence and not a per-row default.
 *
 * The five operations below map directly onto the five job states
 * (`pending` -> `leased` -> `done`/`failed`/`dead`):
 *
 *   submit_remote   caller side.   pending row appears (or the surface's
 *                                  queue is full and nothing is inserted).
 *   lease           worker side.   claims the oldest eligible pending row
 *                                  with `FOR UPDATE SKIP LOCKED`, stamps a
 *                                  fresh fencing_token from inference_fence,
 *                                  and returns it as `leased`.
 *   heartbeat       worker side.   extends a held lease's deadline, FENCED.
 *   complete/fail   worker side.   terminal transition, FENCED, and sends
 *                                  pg_notify in the same transaction.
 *   sweep_once      any replica,   requeues/kills expired leases, fails
 *                   single-flight. pending rows past their submit_deadline,
 *                                  and reaps old terminal rows. Nothing
 *                                  calls this on a timer by itself --
 *                                  start_sweep_ticker() is the optional
 *                                  scheduler below; without it, sweep_once()
 *                                  only ever runs when some caller invokes
 *                                  it directly (a test, or an operator).
 *
 * FENCING is the one property every worker-side write depends on. A lease
 * hands out a fencing_token; every later write from that lease (heartbeat,
 * complete, fail) is a conditional UPDATE requiring `fencing_token = $N AND
 * state = 'leased'`. Zero rows updated means a NEWER lease already exists for
 * that job -- this worker's lease expired, the sweeper (or a racing second
 * lease() call) already moved the row on, and this write MUST be discarded.
 * Every method below that can be fenced out returns that outcome as `false`
 * (heartbeat) or as `SubmitError::Fenced` (complete/fail) specifically so a
 * caller cannot mistake "my write didn't land because I'm stale" for success
 * -- see this module's own .cpp for the double-execution bug that results
 * from getting this wrong.
 *
 * LISTEN/NOTIFY (via pg::Pool::start_listen_pump on channel kNotifyChannel)
 * is a WAKEUP HINT ONLY, never load-bearing for correctness: await_result()
 * always polls on config().poll_interval regardless of whether a pump is
 * running, and only additionally wakes early when a notification arrives.
 */
namespace options_calculator::inference_queue {

// ---------------------------------------------------------------------------
// Vocabulary
// ---------------------------------------------------------------------------

/** The two assistants this queue is shared between. Mirrors the migration's
 *  `surface IN ('strategy', 'mortgage')` CHECK constraint exactly -- adding a
 *  third assistant means updating both together. */
export enum class Surface : std::uint8_t {
    Strategy,
    Mortgage,
};

export [[nodiscard]] constexpr auto to_string(Surface s) noexcept -> std::string_view {
    switch (s) {
        case Surface::Strategy: return "strategy";
        case Surface::Mortgage: return "mortgage";
    }
    return "unknown";
}

export [[nodiscard]] auto surface_from_string(std::string_view s) noexcept
    -> std::optional<Surface>;

export enum class JobState : std::uint8_t {
    Pending,
    Leased,
    Done,
    Failed,
    Dead,
};

export [[nodiscard]] constexpr auto to_string(JobState s) noexcept -> std::string_view {
    switch (s) {
        case JobState::Pending: return "pending";
        case JobState::Leased:  return "leased";
        case JobState::Done:    return "done";
        case JobState::Failed:  return "failed";
        case JobState::Dead:    return "dead";
    }
    return "unknown";
}

export [[nodiscard]] auto job_state_from_string(std::string_view s) noexcept
    -> std::optional<JobState>;

/** One row of public.inference_jobs, decoded. `payload`/`result`/`error` stay
 *  as JSON/plain text -- this module does not parse them; that is the
 *  caller's and the worker's business, not the queue's. */
export struct Job {
    std::int64_t id = 0;
    Surface surface = Surface::Strategy;
    std::string payload;
    JobState state = JobState::Pending;
    int attempts = 0;
    int max_attempts = 2;
    std::optional<std::int64_t> fencing_token;
    std::optional<std::string> result;
    std::optional<std::string> error;
};

export enum class SubmitError : std::uint8_t {
    QueueFull,
    Fenced,
    NotFound,
    InvalidSurface,
    PoolExhausted,
    ConnectFailed,
    CircuitOpen,
    DatabaseError,
    Timeout,
};

export [[nodiscard]] constexpr auto to_string(SubmitError e) noexcept -> std::string_view {
    switch (e) {
        case SubmitError::QueueFull:      return "QueueFull";
        case SubmitError::Fenced:         return "Fenced";
        case SubmitError::NotFound:       return "NotFound";
        case SubmitError::InvalidSurface: return "InvalidSurface";
        case SubmitError::PoolExhausted:  return "PoolExhausted";
        case SubmitError::ConnectFailed:  return "ConnectFailed";
        case SubmitError::CircuitOpen:    return "CircuitOpen";
        case SubmitError::DatabaseError:  return "DatabaseError";
        case SubmitError::Timeout:        return "Timeout";
    }
    return "Unknown";
}

/** What submit_remote() hands back: enough to lease(), heartbeat(),
 *  complete()/fail() against, and to poll or await_result() on. */
export struct Result {
    std::int64_t job_id = 0;
};

/** What sweep_once() did, in numbers a caller/test can assert on directly
 *  rather than having to re-query the table. `ran = false` means another
 *  replica held the advisory lock and this call was a no-op -- the
 *  single-flight-across-replicas property, not a failure. */
export struct SweepReport {
    bool ran = false;
    std::int64_t requeued = 0;
    std::int64_t dead = 0;
    std::int64_t expired_pending_failed = 0;
    std::int64_t reaped = 0;
};

// ---------------------------------------------------------------------------
// Queue
// ---------------------------------------------------------------------------

/**
 * Queue::Config's settings, factored out to namespace scope (rather than a
 * struct nested inside Queue) because a NESTED class's own default member
 * initializers cannot be used to satisfy a default ARGUMENT of the enclosing
 * class's own constructor -- `explicit Queue(..., Config config = {})` sitting
 * inside Queue while Config is also a member of Queue hits exactly that
 * ordering trap (Config's `= 200` etc. are only usable in a "complete-class
 * context", and a default argument of Queue's own constructor is evaluated
 * before Queue itself -- and therefore its nested Config member -- is
 * considered complete). Declaring the struct here sidesteps the trap
 * entirely: it is complete at its own closing brace, independent of Queue.
 * `Queue::Config` remains valid via the alias just inside the class below.
 */
export struct QueueConfig {
    /** Bound on pending rows PER SURFACE, enforced by enqueue's own
     *  `INSERT ... SELECT ... WHERE (SELECT count(*) ...) < bound` -- the
     *  check and the write are already ONE SQL statement, not two
     *  application-level round trips, so the only remaining race is
     *  Postgres's own per-statement MVCC snapshot under two genuinely
     *  concurrent transactions, not an app-level check-then-act bug. This
     *  is a soft bound under concurrent submitters -- two racing
     *  submit_remote() calls can each observe a count just under the
     *  bound and both insert, so the true pending count can briefly
     *  exceed it by up to (concurrent submitters - 1).
     *
     *  KEPT AS A SOFT BOUND, DELIBERATELY, after quantifying the overshoot
     *  rather than assuming it away: "concurrent submitters" here means
     *  concurrent submit_remote() calls racing this exact statement for
     *  the SAME surface, which is hard-capped by each process's own
     *  `pg::PoolConfig::size` (4, unmodified by either
     *  assistant_service.cpp's or mortgage_assistant_service.cpp's
     *  `configure_inference_queue()`) -- submit_remote() must first
     *  acquire a pooled connection, so no more than `pool_size` callers
     *  can even be mid-INSERT at once per process. Worst case, with the
     *  current single-container Railway deployment (one replica, one pool
     *  per surface), that is AT MOST 3 extra rows beyond a 200-row bound
     *  -- 1.5% burst, self-limiting (nothing about it compounds: the next
     *  submit_remote() sees the true, now-updated count and is refused
     *  normally), and still bounded in time by `submit_deadline`/the
     *  sweeper regardless. A future multi-replica deployment scales this
     *  to `(pool_size * replica_count) - 1`, still a small, known number,
     *  not unbounded growth.
     *
     *  A per-surface advisory lock would make this exact instead --
     *  serializing EVERY enqueue to that surface, not just concurrent
     *  ones, adding latency and a new shared point of contention between
     *  both assistants' submitters -- to buy a guarantee this queue does
     *  not need: "zero rows back means full" already holds exactly as
     *  specified either way, and the bound exists to protect the worker
     *  pool from UNBOUNDED backlog, not to be exact to the row. Revisit
     *  only if `pool_size` or replica count grows enough to make that
     *  handful-out-of-200 figure worth re-deriving -- see this comment for
     *  the arithmetic, not just the conclusion. */
    std::size_t pending_bound_per_surface = 200;
    std::chrono::seconds lease_duration{30};
    std::chrono::milliseconds poll_interval{250};
    std::chrono::hours done_retention{24};
    std::chrono::hours dead_retention{24 * 7};

    /** How often start_sweep_ticker()'s background thread calls sweep_once().
     *  Independent of lease_duration/submit_deadline -- this only bounds how
     *  PROMPTLY an abandoned lease or an unleased pending row past its
     *  submit_deadline gets noticed, not how long either window itself is.
     *  15s comfortably undercuts both the default 30s lease_duration and a
     *  90s submit window, so a lease that just expired or a job that just
     *  timed out is swept well within one more lease_duration/submit window,
     *  not left to accumulate until some other caller happens to sweep. */
    std::chrono::seconds sweep_interval{15};
};

export class Queue {
  public:
    using Config = QueueConfig;

    explicit Queue(std::shared_ptr<pg::Pool> pool, Config config = {});

    /** Joins the sweep ticker thread, if one was started -- see
     *  stop_sweep_ticker()'s own doc. User-declared only because a
     *  std::thread member needs an explicit join before it is destroyed;
     *  nothing else about Queue's destruction requires this class to give up
     *  the implicit special members it would otherwise get for free (Queue
     *  is already non-copyable/non-movable via its std::mutex/
     *  std::condition_variable members, so this changes nothing about that). */
    ~Queue();

    /** Starts the LISTEN pump on the shared pool (channel kNotifyChannel).
     *  Optional -- await_result() works correctly, just less promptly,
     *  without ever calling this. See the module banner. */
    [[nodiscard]] auto start_notify_pump() -> std::expected<void, SubmitError>;
    auto stop_notify_pump() noexcept -> void;

    /** Starts a background thread that calls sweep_once() every
     *  config().sweep_interval, until stop_sweep_ticker() or destruction.
     *  Optional, exactly like start_notify_pump() -- no correctness property
     *  of lease()/heartbeat()/complete()/fail() depends on a ticker running
     *  anywhere; without one, an abandoned lease or a job that timed out
     *  while still pending simply waits for SOME caller (a ticker on another
     *  Queue instance, an operator, a test) to call sweep_once(), rather than
     *  being noticed within sweep_interval. Safe to call from every replica,
     *  and from more than one Queue instance in the same process (both
     *  assistant surfaces each own a Queue against the same table) --
     *  sweep_once()'s own pg_try_advisory_lock makes every ticker but one a
     *  no-op on any given tick. Calling this twice on the same Queue without
     *  an intervening stop_sweep_ticker() replaces the running thread (the
     *  old one is stopped first). */
    auto start_sweep_ticker() -> void;

    /** Stops the ticker started by start_sweep_ticker(), if any, and joins
     *  its thread. Safe to call when no ticker is running (a no-op). Also
     *  called from the destructor so a Queue never outlives its own ticker
     *  thread -- see this class's own destructor. */
    auto stop_sweep_ticker() noexcept -> void;

    // --- caller side ---------------------------------------------------

    /** Enqueues one job, bounded per-surface. `payload_json` must already be
     *  valid JSON text -- it is bound as a parameter and cast `::jsonb` by
     *  the INSERT, so malformed JSON fails the query rather than being
     *  silently stored as an opaque string (see this module's file banner
     *  and pg.cppm's exec_params doc for why it is a bound parameter and
     *  never spliced into SQL text). */
    [[nodiscard]] auto submit_remote(Surface surface, std::string_view payload_json,
                                      std::chrono::system_clock::time_point deadline)
        -> std::expected<Result, SubmitError>;

    /** One row lookup by id, for polling. */
    [[nodiscard]] auto get_job(std::int64_t job_id) -> std::expected<std::optional<Job>, SubmitError>;

    /** Polls get_job() every `poll_interval` (the correctness backstop, run
     *  unconditionally) until the job reaches a terminal state or
     *  `deadline` passes, whichever comes first. If start_notify_pump() has
     *  been called and a completion notification for this job arrives, the
     *  next poll happens immediately rather than waiting out the interval --
     *  purely an optimization; suppressing notifications entirely (never
     *  calling start_notify_pump(), or having the pump silently die) leaves
     *  this method correct, just no faster than the poll interval. */
    [[nodiscard]] auto await_result(std::int64_t job_id,
                                     std::chrono::system_clock::time_point deadline)
        -> std::expected<Job, SubmitError>;

    // --- worker side -----------------------------------------------------

    /** Claims the oldest eligible pending row for `surface` via
     *  `FOR UPDATE SKIP LOCKED`, stamping a fresh fencing_token. Returns
     *  std::nullopt (not an error) when there is nothing to lease -- an
     *  empty queue is the ordinary case, not a failure. */
    [[nodiscard]] auto lease(Surface surface, std::string_view worker_id)
        -> std::expected<std::optional<Job>, SubmitError>;

    /** Extends a held lease's deadline. Returns `false` (not an error) when
     *  the write was fenced out -- the caller no longer owns this job and
     *  MUST stop working on it; see the module banner. */
    [[nodiscard]] auto heartbeat(std::int64_t job_id, std::int64_t fencing_token)
        -> std::expected<bool, SubmitError>;

    /** Fenced terminal transition to `done`, storing `result_json` (must be
     *  valid JSON text; see submit_remote's doc) and sending pg_notify on
     *  kNotifyChannel in the same transaction. Returns `false` when fenced
     *  out; the caller MUST discard its own result in that case rather than
     *  treat the completed work as delivered -- see the module banner. */
    [[nodiscard]] auto complete(std::int64_t job_id, std::int64_t fencing_token,
                                 std::string_view result_json) -> std::expected<bool, SubmitError>;

    /** Fenced terminal transition to `failed` (a worker-reported, permanent
     *  failure -- e.g. an unparseable request -- NOT a crash; crash recovery
     *  is the sweeper's job, see sweep_once()). Returns `false` when fenced
     *  out, same discipline as complete(). */
    [[nodiscard]] auto fail(std::int64_t job_id, std::int64_t fencing_token,
                             std::string_view error_message) -> std::expected<bool, SubmitError>;

    // --- maintenance -------------------------------------------------------

    /** Single-flight across replicas via pg_try_advisory_lock: expired
     *  leases go back to `pending` (attempts remaining and still inside
     *  submit_deadline) or to `dead` (the DLQ) otherwise; pending rows past
     *  submit_deadline become `failed`; terminal rows older than the
     *  configured retention are deleted outright. Safe to call from every
     *  replica on a timer -- at most one call across the whole cluster does
     *  real work at a time, the rest observe `ran = false` and return. */
    [[nodiscard]] auto sweep_once() -> std::expected<SweepReport, SubmitError>;

    [[nodiscard]] auto config() const noexcept -> const Config& { return config_; }

    /** The channel every completion notification is sent on
     *  (pg_notify('inference_jobs', <job id>)) -- exported so a test can
     *  assert against it without duplicating the literal. */
    static constexpr std::string_view kNotifyChannel = "inference_jobs";

  private:
    /** Handles one raw notification from pg::Pool's pump: parses `payload`
     *  as a job id and records it as "recently notified" so a blocked
     *  await_result() wakes early instead of waiting out its poll_interval.
     *  Never the sole path to progress -- see the module banner. */
    auto on_notify(std::string_view channel, std::string_view payload) -> void;

    std::shared_ptr<pg::Pool> pool_;
    Config config_;

    std::mutex notify_mu_;
    std::condition_variable notify_cv_;
    std::unordered_set<std::int64_t> notified_ids_;

    /** The sweep ticker's own thread/mutex/condition_variable/stop-flag --
     *  deliberately a PLAIN std::thread/std::condition_variable rather than
     *  std::jthread/std::condition_variable_any + std::stop_token. This is
     *  not a style preference: inference_admission.cppm's own
     *  force_take_jobs_symbol_emission documents a confirmed, unresolved
     *  upstream Clang bug where an out-of-line module member function that
     *  calls condition_variable_any::wait(lock, stop_token, pred) fails to
     *  LINK when the calling TU also independently #includes <stop_token>
     *  (as this module's own callers do, transitively). start_sweep_ticker()/
     *  stop_sweep_ticker() are defined out-of-line in inference_queue.cpp --
     *  a module IMPLEMENTATION unit, the exact shape that bug requires -- so
     *  this sidesteps it entirely by never naming stop_token or
     *  condition_variable_any at all, rather than depending on a fix that,
     *  as of this writing, has not landed. */
    std::thread sweep_thread_;
    std::mutex sweep_mu_;
    std::condition_variable sweep_cv_;
    bool sweep_stop_ = false;
};

}  // namespace options_calculator::inference_queue
