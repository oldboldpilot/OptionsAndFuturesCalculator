module;
#include <cerrno>

#include <poll.h>
#include <libpq-fe.h>

export module pg;
import std;

import logger;
import sgee.runtime.resilience;

/**
 * @author Olumuyiwa Oluwasanmi
 *
 * A small, purpose-built libpq wrapper: RAII connections, PQexecParams-only
 * query execution, a bounded connection pool, and a LISTEN/NOTIFY pump. This
 * exists as the substrate for inference_queue.cppm's shared job queue and is
 * deliberately NOT a general-purpose database layer.
 *
 * Why this instead of SGEE's own libpq driver
 * (backend/external/SGEE/src/runtime/database_adapters.cppm ~958-1232):
 *
 *   1. That driver's real body is gated behind SGEE_USE_POSTGRES, which is
 *      OFF by default, and the DEFAULT-BUILD STUB SILENTLY SUCCEEDS
 *      (~1238-1260): connect() returns OK and execute() returns an empty
 *      result set. A queue built on it would pass every test that forgot the
 *      flag while persisting nothing.
 *   2. It calls PQexec with string-built SQL (~1013, ~1062). This module's
 *      payload is an arbitrary user utterance; that is unacceptable.
 *   3. SGEE's own CMakeLists (~684-687) says its driver's CI is still
 *      pending.
 *
 * libpq is a C API, so there is no libc++/libstdc++ ABI concern linking it
 * into this otherwise-libc++ binary (see backend/CMakeLists.txt ~439-444,
 * which anticipates exactly this case).
 *
 * PQexecParams-only, no exceptions:
 *
 *   Every statement in this module -- including the fixed, compile-time
 *   constant strings used for `SET statement_timeout`, `LISTEN <channel>`,
 *   `BEGIN`, `COMMIT` and `ROLLBACK` -- goes through Connection::exec() /
 *   exec_params(), both of which call PQexecParams internally, never
 *   PQexec. PQexecParams is used even where there is nothing to
 *   parameterize (a bare LISTEN channel name is a SQL identifier, not a
 *   value position, so libpq cannot bind it as a $-parameter at all) so
 *   that there is exactly one code path in this module that ever calls into
 *   libpq's query-execution API, and it is the parameterized one. None of
 *   those fixed strings are ever built by concatenating untrusted input --
 *   the one value in this whole substrate that IS untrusted end-to-end (the
 *   caller's JSON payload) always travels as a bound parameter, never as
 *   text spliced into a command string.
 */
namespace options_calculator::pg {

// ---------------------------------------------------------------------------
// Errors
// ---------------------------------------------------------------------------

export enum class ErrorCode : std::uint8_t {
    ConnectFailed,
    QueryFailed,
    Timeout,
    PoolExhausted,
    CircuitOpen,
    NotConnected,
};

export [[nodiscard]] constexpr auto to_string(ErrorCode code) noexcept -> std::string_view {
    switch (code) {
        case ErrorCode::ConnectFailed:  return "ConnectFailed";
        case ErrorCode::QueryFailed:    return "QueryFailed";
        case ErrorCode::Timeout:        return "Timeout";
        case ErrorCode::PoolExhausted:  return "PoolExhausted";
        case ErrorCode::CircuitOpen:    return "CircuitOpen";
        case ErrorCode::NotConnected:   return "NotConnected";
    }
    return "Unknown";
}

export struct Error {
    ErrorCode code{ErrorCode::QueryFailed};
    std::string message;
};

// ---------------------------------------------------------------------------
// RAII handles -- rule 3/4 of config/cpp_details.txt: no raw OWNING pointers.
// libpq's PGconn/PGresult are opaque C handles freed by PQfinish/PQclear;
// wrapping each in std::unique_ptr with a stateless deleter makes every
// connection and every result exception- and early-return-safe without a
// single manual PQfinish/PQclear call anywhere else in this module.
// ---------------------------------------------------------------------------

export struct ConnDeleter {
    auto operator()(PGconn* conn) const noexcept -> void {
        if (conn != nullptr) PQfinish(conn);
    }
};

export struct ResultDeleter {
    auto operator()(PGresult* result) const noexcept -> void {
        if (result != nullptr) PQclear(result);
    }
};

export using ConnHandle = std::unique_ptr<PGconn, ConnDeleter>;
export using ResultHandle = std::unique_ptr<PGresult, ResultDeleter>;

// ---------------------------------------------------------------------------
// Result
// ---------------------------------------------------------------------------

/** A completed query's result set. Read-only, text-format fields (libpq's
 *  default) -- the queue's own columns are all text-representable (bigint,
 *  text, jsonb, timestamptz), so there is no reason to pay for binary format
 *  parsing here. */
export class Result {
  public:
    explicit Result(ResultHandle handle) noexcept : handle_(std::move(handle)) {}

    [[nodiscard]] auto rows() const noexcept -> int { return PQntuples(handle_.get()); }
    [[nodiscard]] auto cols() const noexcept -> int { return PQnfields(handle_.get()); }

    [[nodiscard]] auto is_null(int row, int col) const noexcept -> bool {
        return PQgetisnull(handle_.get(), row, col) != 0;
    }

    /** Empty string_view for a SQL NULL -- callers that care about the
     *  distinction between NULL and an empty string must check is_null()
     *  first, exactly as libpq itself requires. */
    [[nodiscard]] auto text(int row, int col) const -> std::string_view {
        if (is_null(row, col)) return {};
        return {PQgetvalue(handle_.get(), row, col),
                static_cast<std::size_t>(PQgetlength(handle_.get(), row, col))};
    }

    /** Rows affected by an INSERT/UPDATE/DELETE with no RETURNING. For a
     *  RETURNING statement, use rows() instead -- PQcmdTuples() is documented
     *  by libpq to report the RETURNING row count too, but every call site in
     *  this module that cares about "did my fenced UPDATE hit a row" already
     *  has a RETURNING clause and reads rows(), so this exists for
     *  completeness rather than because the queue protocol needs it. */
    [[nodiscard]] auto command_tuples() const -> long long {
        const char* raw = PQcmdTuples(handle_.get());
        if (raw == nullptr || raw[0] == '\0') return 0;
        return std::strtoll(raw, nullptr, 10);
    }

  private:
    ResultHandle handle_;
};

// ---------------------------------------------------------------------------
// Connection
// ---------------------------------------------------------------------------

/** One live libpq connection. Move-only (holds a unique_ptr), constructed
 *  only through connect() so every live Connection has already had its
 *  statement_timeout pinned. */
export class Connection {
  public:
    /**
     * Opens a connection and pins its statement_timeout.
     *
     * `conninfo` is passed through PQconnectdbParams via the `dbname`
     * keyword with expand_dbname=1, which is libpq's own documented
     * mechanism for accepting either a URI (`postgres://...`) or a
     * keyword/value string there and merging in additional keywords
     * afterwards -- here, `connect_timeout` and `sslmode`, which then take
     * precedence over anything the same keyword already named inside
     * `conninfo`. This is what "every connection carries connect_timeout=2"
     * and "TLS is enforced on every connection" mean in practice: they are
     * enforced by this one call, not by asking every caller to remember to
     * append them to their DATABASE_URL.
     *
     * TLS enforcement and `sslmode`:
     *
     *   libpq's default is `prefer`, which negotiates TLS opportunistically
     *   and silently falls back to plaintext if TLS negotiation fails --
     *   so "we observed TLS" in production is not the same as "TLS is
     *   enforced".
     *
     *   Merging `sslmode=require` guarantees that plaintext is rejected.
     *   `require` encrypts traffic on the wire but does NOT verify the
     *   server certificate, so it stops passive eavesdropping and not an
     *   active MITM. The stronger levels (`verify-ca` and `verify-full`)
     *   are NOT used because no root CA bundle is shipped in the runtime
     *   image; tested against production, both fail with: "Either provide
     *   the file or change sslmode to disable server certificate
     *   verification". `require` is therefore the strongest level that
     *   works today.
     *
     *   An operator can override this default for local socket-only dev
     *   databases without editing code by setting `PGSSLMODE_OVERRIDE`
     *   (e.g. to `disable` or `prefer`), but the default remains `require`.
     *
     * `statement_timeout` is a SESSION GUC set via `SET statement_timeout`
     * as this connection's first statement, before the connection is
     * returned to the caller -- so "every statement carries
     * statement_timeout=2000" holds for every statement run on this
     * Connection afterwards without each call site having to set it again.
     */
    [[nodiscard]] static auto connect(std::string_view conninfo,
                                       std::chrono::milliseconds connect_timeout,
                                       std::chrono::milliseconds statement_timeout)
        -> std::expected<Connection, Error>;

    /** PQexecParams with zero parameters -- for fixed, compile-time-constant
     *  statements (SET, LISTEN, BEGIN/COMMIT/ROLLBACK). See the module
     *  banner for why this still goes through PQexecParams. */
    [[nodiscard]] auto exec(std::string_view sql) -> std::expected<Result, Error>;

    /** PQexecParams with bound parameters. `params[i] == std::nullopt` binds
     *  SQL NULL for that position; every other value is sent as text. This
     *  is the ONLY way untrusted data (a caller's JSON payload, an error
     *  message, anything not a literal in this module's own source) may
     *  reach a SQL statement in this codebase's inference-queue substrate. */
    [[nodiscard]] auto exec_params(std::string_view sql,
                                    std::span<const std::optional<std::string>> params)
        -> std::expected<Result, Error>;

    /** Cheap liveness check (PQstatus, no round trip). The pool additionally
     *  treats a failed exec/exec_params as evidence of death, since a
     *  connection can go bad between two consecutive alive() checks. */
    [[nodiscard]] auto alive() const noexcept -> bool;

    /** Exposes the raw handle for the LISTEN pump's PQsocket/PQconsumeInput/
     *  PQnotifies loop, which libpq has no non-blocking wrapper for. Not
     *  exported outside this module's own Pool -- ordinary callers use
     *  exec()/exec_params() exclusively. */
    [[nodiscard]] auto native() noexcept -> PGconn*;

    Connection(Connection&&) noexcept = default;
    auto operator=(Connection&&) noexcept -> Connection& = default;
    Connection(const Connection&) = delete;
    auto operator=(const Connection&) -> Connection& = delete;
    ~Connection() = default;

  private:
    explicit Connection(ConnHandle handle) noexcept : handle_(std::move(handle)) {}

    ConnHandle handle_;
};

// ---------------------------------------------------------------------------
// Pool
// ---------------------------------------------------------------------------

export struct PoolConfig {
    std::string conninfo;
    std::size_t size = 4;
    /** Bounded-wait acquisition: acquire() waits at most this long for a
     *  connection to free up, then fails rather than blocking a thread
     *  indefinitely. */
    std::chrono::milliseconds acquire_timeout{250};
    std::chrono::milliseconds connect_timeout{2000};
    std::chrono::milliseconds statement_timeout{2000};
    sgee::resilience::CircuitBreakerConfig breaker{
        .failure_threshold = 5, .open_cooldown_ms = 5000, .half_open_max_trials = 1};
};

export class Pool;

/** An RAII lease on one pooled connection. Returns its slot to the pool on
 *  destruction (or on being overwritten by move-assignment), so a caller
 *  that early-returns, throws, or simply falls out of scope cannot leak a
 *  connection out of the pool -- the same RAII discipline as ConnHandle/
 *  ResultHandle above, just for a pool slot instead of a libpq handle. */
export class Lease {
  public:
    Lease() noexcept = default;
    ~Lease();

    Lease(Lease&& other) noexcept;
    auto operator=(Lease&& other) noexcept -> Lease&;
    Lease(const Lease&) = delete;
    auto operator=(const Lease&) -> Lease& = delete;

    [[nodiscard]] auto get() noexcept -> Connection*;
    [[nodiscard]] auto operator->() noexcept -> Connection*;
    [[nodiscard]] auto valid() const noexcept -> bool { return pool_ != nullptr; }

  private:
    friend class Pool;
    Lease(Pool& pool, std::size_t slot) noexcept : pool_(&pool), slot_(slot) {}

    // Non-owning: the Pool owns every Connection for its entire lifetime.
    // This pointer only identifies WHICH slot to return on release(), the
    // same role an index would play -- it never outlives the Pool it points
    // at, because stop_listen_pump()/~Pool() are the only things that could
    // invalidate it, and neither runs while a Lease taken from this Pool is
    // still alive in correctly-written caller code (the same lifetime
    // contract every connection pool in every language places on its
    // callers).
    Pool* pool_ = nullptr;
    std::size_t slot_ = 0;
};

/** A small, bounded libpq connection pool with a circuit breaker over
 *  Postgres reachability (sgee::resilience::CircuitBreaker, the same
 *  primitive market_data.cppm ~226 uses for Alpaca/Treasury -- see that
 *  module's "Resilience" section for why a breaker rather than a hand-rolled
 *  retry loop) and a LISTEN/NOTIFY pump on its own dedicated connection and
 *  thread. */
export class Pool {
  public:
    explicit Pool(PoolConfig config);
    ~Pool();

    Pool(const Pool&) = delete;
    auto operator=(const Pool&) -> Pool& = delete;
    Pool(Pool&&) = delete;
    auto operator=(Pool&&) -> Pool& = delete;

    /**
     * Acquires a connection, waiting at most `acquire_timeout` (default
     * 250ms) for one to free up. NEVER blocks indefinitely: a pool that is
     * fully checked out for longer than the bound fails the caller with
     * PoolExhausted rather than queuing it forever.
     *
     * Consults the circuit breaker FIRST, before even joining the wait
     * queue: when Postgres has been failing repeatedly, acquire() fails
     * immediately with CircuitOpen instead of making every caller separately
     * discover the outage by waiting out the full 250ms.
     */
    [[nodiscard]] auto acquire() -> std::expected<Lease, Error>;

    using NotifyHandler = std::function<void(std::string_view channel, std::string_view payload)>;

    /**
     * Starts the LISTEN pump: opens one dedicated connection (separate from
     * the pool's own connections, so a slow or misbehaving notification
     * handler can never starve query traffic of a connection), issues
     * LISTEN for each channel, and runs a poll()-driven loop on its own
     * std::jthread that calls `handler` for every notification received.
     *
     * This is a WAKEUP HINT ONLY. Nothing in this module -- and nothing in
     * inference_queue.cppm built on top of it -- may assume a notification
     * is ever delivered: a dropped connection, a missed reconnect window, or
     * simply calling submit_remote()/complete() before start_listen_pump()
     * runs all mean zero notifications for a real completion. Every caller
     * that wants to know when a job finishes must poll as the correctness
     * backstop; this pump exists purely to make that poll loop return sooner
     * on the common path.
     */
    [[nodiscard]] auto start_listen_pump(std::vector<std::string> channels, NotifyHandler handler)
        -> std::expected<void, Error>;

    /** Stops and joins the pump thread. Safe to call when no pump is
     *  running (idempotent) and is called from the destructor, so a Pool
     *  going out of scope never leaves a detached thread behind. */
    auto stop_listen_pump() noexcept -> void;

    [[nodiscard]] auto circuit_state() const -> sgee::resilience::State;

  private:
    friend class Lease;
    auto release(std::size_t slot) noexcept -> void;
    [[nodiscard]] auto try_reconnect(std::size_t slot) -> bool;
    auto pump_loop(std::stop_token stop, std::shared_ptr<Connection> conn, NotifyHandler handler)
        -> void;

    PoolConfig config_;

    std::mutex mu_;
    std::condition_variable cv_;
    std::vector<std::unique_ptr<Connection>> conns_;
    std::deque<std::size_t> available_;

    mutable std::mutex breaker_mu_;
    sgee::resilience::CircuitBreaker breaker_;

    std::jthread pump_thread_;
};

}  // namespace options_calculator::pg

// =============================================================================
// Implementation
// =============================================================================

namespace options_calculator::pg {

namespace {

[[nodiscard]] auto steady_ms() -> std::uint64_t {
    static const auto epoch = std::chrono::steady_clock::now();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                epoch)
            .count());
}

}  // namespace

// --- Connection --------------------------------------------------------

auto Connection::connect(std::string_view conninfo, std::chrono::milliseconds connect_timeout,
                          std::chrono::milliseconds statement_timeout)
    -> std::expected<Connection, Error> {
    const std::string dbname{conninfo};
    // PQconnectdbParams wants whole seconds, minimum 1 -- 0 means "no
    // timeout" to libpq, the opposite of what a caller asking for a bounded
    // connect_timeout wants.
    const auto seconds = std::max<std::int64_t>(
        1, std::chrono::duration_cast<std::chrono::seconds>(connect_timeout).count());
    const std::string timeout_str = std::to_string(seconds);

    const char* const sslmode_env = std::getenv("PGSSLMODE_OVERRIDE");
    const char* const sslmode =
        (sslmode_env != nullptr && sslmode_env[0] != '\0') ? sslmode_env : "require";

    const char* keywords[] = {"dbname", "connect_timeout", "sslmode", nullptr};
    const char* values[] = {dbname.c_str(), timeout_str.c_str(), sslmode, nullptr};

    ConnHandle handle{PQconnectdbParams(keywords, values, /*expand_dbname=*/1)};
    if (!handle) {
        return std::unexpected(
            Error{ErrorCode::ConnectFailed, "PQconnectdbParams returned null (out of memory)"});
    }
    if (PQstatus(handle.get()) != CONNECTION_OK) {
        Error e{ErrorCode::ConnectFailed, PQerrorMessage(handle.get())};
        return std::unexpected(std::move(e));
    }

    Connection conn{std::move(handle)};
    const std::string stmt =
        "SET statement_timeout = " + std::to_string(statement_timeout.count());
    if (auto pin = conn.exec(stmt); !pin) {
        return std::unexpected(pin.error());
    }
    return conn;
}

auto Connection::exec(std::string_view sql) -> std::expected<Result, Error> {
    return exec_params(sql, {});
}

auto Connection::exec_params(std::string_view sql,
                              std::span<const std::optional<std::string>> params)
    -> std::expected<Result, Error> {
    if (!handle_) {
        return std::unexpected(
            Error{ErrorCode::NotConnected, "exec_params on a closed/moved-from connection"});
    }

    std::vector<const char*> values;
    values.reserve(params.size());
    for (const auto& p : params) {
        values.push_back(p.has_value() ? p->c_str() : nullptr);
    }

    const std::string command{sql};
    ResultHandle result{PQexecParams(handle_.get(), command.c_str(),
                                      static_cast<int>(params.size()), /*paramTypes=*/nullptr,
                                      values.data(), /*paramLengths=*/nullptr,
                                      /*paramFormats=*/nullptr, /*resultFormat=*/0)};
    if (!result) {
        return std::unexpected(Error{ErrorCode::QueryFailed, PQerrorMessage(handle_.get())});
    }

    const auto status = PQresultStatus(result.get());
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
        Error e{ErrorCode::QueryFailed, PQresultErrorMessage(result.get())};
        return std::unexpected(std::move(e));
    }
    return Result{std::move(result)};
}

auto Connection::alive() const noexcept -> bool {
    return handle_ && PQstatus(handle_.get()) == CONNECTION_OK;
}

auto Connection::native() noexcept -> PGconn* { return handle_.get(); }

// --- Lease ---------------------------------------------------------------

Lease::~Lease() {
    if (pool_ != nullptr) pool_->release(slot_);
}

Lease::Lease(Lease&& other) noexcept : pool_(other.pool_), slot_(other.slot_) {
    other.pool_ = nullptr;
}

auto Lease::operator=(Lease&& other) noexcept -> Lease& {
    if (this != &other) {
        if (pool_ != nullptr) pool_->release(slot_);
        pool_ = other.pool_;
        slot_ = other.slot_;
        other.pool_ = nullptr;
    }
    return *this;
}

auto Lease::get() noexcept -> Connection* {
    return (pool_ != nullptr) ? pool_->conns_[slot_].get() : nullptr;
}

auto Lease::operator->() noexcept -> Connection* { return get(); }

// --- Pool ------------------------------------------------------------------

Pool::Pool(PoolConfig config) : config_(std::move(config)), breaker_(config_.breaker) {
    auto& log = logger::Logger::getInstance();
    conns_.resize(config_.size);
    for (std::size_t i = 0; i < config_.size; ++i) {
        auto attempt = Connection::connect(config_.conninfo, config_.connect_timeout,
                                            config_.statement_timeout);
        if (attempt) {
            conns_[i] = std::make_unique<Connection>(std::move(*attempt));
        } else {
            // Not fatal: the pool comes up with a dead slot rather than
            // refusing to construct at all, and acquire() retries the
            // reconnect on first use. A queue substrate that could not start
            // because Postgres happened to be mid-restart when the process
            // booted would be worse than one that starts degraded and heals.
            log.error("pg::Pool: initial connection {} failed: {}", i, attempt.error().message);
        }
        available_.push_back(i);
    }
}

Pool::~Pool() { stop_listen_pump(); }

auto Pool::try_reconnect(std::size_t slot) -> bool {
    auto attempt =
        Connection::connect(config_.conninfo, config_.connect_timeout, config_.statement_timeout);
    if (!attempt) {
        logger::Logger::getInstance().error("pg::Pool: reconnect of slot {} failed: {}", slot,
                                             attempt.error().message);
        return false;
    }
    conns_[slot] = std::make_unique<Connection>(std::move(*attempt));
    return true;
}

auto Pool::acquire() -> std::expected<Lease, Error> {
    {
        const std::lock_guard lock{breaker_mu_};
        if (!breaker_.allow(steady_ms())) {
            return std::unexpected(
                Error{ErrorCode::CircuitOpen, "postgres circuit breaker is open"});
        }
    }

    std::unique_lock lock{mu_};
    const bool got =
        cv_.wait_for(lock, config_.acquire_timeout, [this] { return !available_.empty(); });
    if (!got) {
        return std::unexpected(Error{
            ErrorCode::PoolExhausted,
            "no connection became available within the bounded acquire_timeout"});
    }
    const std::size_t slot = available_.front();
    available_.pop_front();
    lock.unlock();

    const bool needs_reconnect = !conns_[slot] || !conns_[slot]->alive();
    if (needs_reconnect && !try_reconnect(slot)) {
        {
            const std::lock_guard relock{mu_};
            available_.push_back(slot);  // don't shrink the pool permanently
        }
        cv_.notify_one();
        const std::lock_guard breaker_lock{breaker_mu_};
        breaker_.on_failure(steady_ms());
        return std::unexpected(
            Error{ErrorCode::ConnectFailed, "pooled connection was dead and reconnect failed"});
    }

    {
        const std::lock_guard breaker_lock{breaker_mu_};
        breaker_.on_success(steady_ms());
    }
    return Lease{*this, slot};
}

auto Pool::release(std::size_t slot) noexcept -> void {
    {
        const std::lock_guard lock{mu_};
        available_.push_back(slot);
    }
    cv_.notify_one();
}

auto Pool::circuit_state() const -> sgee::resilience::State {
    const std::lock_guard lock{breaker_mu_};
    return breaker_.state();
}

auto Pool::start_listen_pump(std::vector<std::string> channels, NotifyHandler handler)
    -> std::expected<void, Error> {
    stop_listen_pump();

    auto attempt =
        Connection::connect(config_.conninfo, config_.connect_timeout, config_.statement_timeout);
    if (!attempt) return std::unexpected(attempt.error());

    // shared_ptr, not unique_ptr: ownership genuinely straddles this
    // function (which opened the connection and issues LISTEN on it) and the
    // jthread's lambda (which owns it for the rest of the pump's life). A
    // unique_ptr would have to be released into the thread with no way for
    // this function to keep using it to issue the LISTEN statements first.
    auto conn = std::make_shared<Connection>(std::move(*attempt));
    for (const auto& channel : channels) {
        // `channel` is one of a small, fixed set of internal constants
        // (see inference_queue.cppm's kNotifyChannel) -- never derived from
        // caller input -- so splicing it into this LISTEN text carries none
        // of the risk PQexecParams-only exists to close off. LISTEN's target
        // is a SQL identifier, a syntactic position libpq cannot bind a
        // $-parameter into at all. This still calls exec(), i.e. still goes
        // through PQexecParams and never PQexec -- see the module banner.
        if (auto res = conn->exec("LISTEN " + channel); !res) {
            return std::unexpected(res.error());
        }
    }

    pump_thread_ = std::jthread([this, conn, handler = std::move(handler)](std::stop_token stop) {
        pump_loop(stop, conn, handler);
    });
    return {};
}

auto Pool::stop_listen_pump() noexcept -> void {
    if (pump_thread_.joinable()) {
        pump_thread_.request_stop();
        pump_thread_.join();
    }
}

auto Pool::pump_loop(std::stop_token stop, std::shared_ptr<Connection> conn, NotifyHandler handler)
    -> void {
    auto& log = logger::Logger::getInstance();
    PGconn* raw = conn->native();
    const int sock = PQsocket(raw);
    if (sock < 0) {
        log.error("pg::Pool: LISTEN pump could not obtain a socket; pump exiting");
        return;
    }

    // 200ms poll tick: short enough that request_stop() is honored promptly
    // on shutdown, and unrelated to (much shorter than) the 250ms poll
    // backstop inference_queue.cppm's callers run while awaiting a result --
    // this loop's only job is to wake THAT poll early, not to replace it.
    constexpr int kPollTickMs = 200;

    while (!stop.stop_requested()) {
        pollfd pfd{.fd = sock, .events = POLLIN, .revents = 0};
        const int rc = ::poll(&pfd, 1, kPollTickMs);
        if (rc < 0) {
            if (errno == EINTR) continue;
            log.warn("pg::Pool: LISTEN pump poll() failed: {}", std::strerror(errno));
            break;
        }
        if (rc > 0 && (pfd.revents & POLLIN) != 0) {
            if (PQconsumeInput(raw) == 0) {
                // The connection is gone. This pump thread exits rather than
                // trying to reconnect+re-LISTEN itself -- every consumer of
                // notifications already treats them as a hint on top of a
                // mandatory poll backstop, so a pump that silently stops
                // delivering hints degrades to "always poll", not to
                // incorrectness.
                log.warn("pg::Pool: LISTEN pump lost its connection: {}", PQerrorMessage(raw));
                break;
            }
        }
        while (PGnotify* n = PQnotifies(raw)) {
            if (handler) handler(n->relname, n->extra);
            PQfreemem(n);
        }
    }
}

}  // namespace options_calculator::pg
