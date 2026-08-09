module;
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <expected>
#include <format>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

module inference_queue;

import pg;
import logger;

/**
 * @author Olumuyiwa Oluwasanmi
 *
 * See inference_queue.cppm's module banner for the protocol. This file is
 * the SQL.
 *
 * FENCING, worked through concretely: complete()/fail()/heartbeat() all
 * share one shape --
 *
 *     UPDATE inference_jobs SET ... WHERE id = $1 AND fencing_token = $2
 *                                         AND state = 'leased'
 *     RETURNING id
 *
 * -- and every one of them reports success as `rows() > 0`, NEVER as "the
 * query executed without error". A worker whose lease already expired (the
 * sweeper moved the row back to 'pending', or a second worker already
 * leased and completed it) issues exactly the same UPDATE, gets back an
 * error-free result with zero rows, and MUST treat that as "discard my
 * work" -- not as "my write raced something and technically didn't
 * happen, but the call itself succeeded so I'm done". Returning `true` for
 * an error-free zero-row UPDATE is precisely the double-execution bug this
 * whole column exists to prevent; see the task's own discriminating test
 * for what that looks like when it is deliberately reintroduced.
 */
namespace options_calculator::inference_queue {

namespace {

[[nodiscard]] auto map_pg_error(const pg::Error& e) noexcept -> SubmitError {
    switch (e.code) {
        case pg::ErrorCode::ConnectFailed:  return SubmitError::ConnectFailed;
        case pg::ErrorCode::PoolExhausted:  return SubmitError::PoolExhausted;
        case pg::ErrorCode::CircuitOpen:    return SubmitError::CircuitOpen;
        case pg::ErrorCode::Timeout:        return SubmitError::Timeout;
        case pg::ErrorCode::NotConnected:   return SubmitError::ConnectFailed;
        case pg::ErrorCode::QueryFailed:    return SubmitError::DatabaseError;
    }
    return SubmitError::DatabaseError;
}

[[nodiscard]] auto parse_i64(std::string_view s) noexcept -> std::optional<std::int64_t> {
    if (s.empty()) return std::nullopt;
    std::int64_t value = 0;
    const auto res = std::from_chars(s.data(), s.data() + s.size(), value);
    if (res.ec != std::errc{}) return std::nullopt;
    return value;
}

[[nodiscard]] auto parse_int(std::string_view s) noexcept -> std::optional<int> {
    const auto v = parse_i64(s);
    if (!v) return std::nullopt;
    return static_cast<int>(*v);
}

/** system_clock::time_point formats as UTC calendar time (no timezone math
 *  applied), matching market_data.cppm's own iso8601-with-Z convention --
 *  see that module's fetch_risk_free_rate() for the identical pattern. */
[[nodiscard]] auto iso8601(std::chrono::system_clock::time_point tp) -> std::string {
    return std::format("{:%Y-%m-%dT%H:%M:%S}Z", std::chrono::floor<std::chrono::microseconds>(tp));
}

/** Fixed 64-bit key for the sweeper's cluster-wide advisory lock. Session-
 *  scoped and unique to this queue's sweeper so it can never collide with an
 *  advisory lock some other part of this database might take for an
 *  unrelated purpose. The literal is arbitrary; what matters is that it
 *  never changes across replicas or across a redeploy, since two different
 *  keys would mean two different locks and lose the single-flight property
 *  entirely. */
constexpr std::int64_t kSweeperLockId = 0x494e4645525f5157;  // "INFE_QW" as bytes, unremarkable

}  // namespace

auto surface_from_string(std::string_view s) noexcept -> std::optional<Surface> {
    if (s == "strategy") return Surface::Strategy;
    if (s == "mortgage") return Surface::Mortgage;
    return std::nullopt;
}

auto job_state_from_string(std::string_view s) noexcept -> std::optional<JobState> {
    if (s == "pending") return JobState::Pending;
    if (s == "leased") return JobState::Leased;
    if (s == "done") return JobState::Done;
    if (s == "failed") return JobState::Failed;
    if (s == "dead") return JobState::Dead;
    return std::nullopt;
}

Queue::Queue(std::shared_ptr<pg::Pool> pool, Config config)
    : pool_(std::move(pool)), config_(std::move(config)) {}

auto Queue::start_notify_pump() -> std::expected<void, SubmitError> {
    auto result = pool_->start_listen_pump(
        {std::string(kNotifyChannel)},
        [this](std::string_view channel, std::string_view payload) { on_notify(channel, payload); });
    if (!result) return std::unexpected(map_pg_error(result.error()));
    return {};
}

auto Queue::stop_notify_pump() noexcept -> void { pool_->stop_listen_pump(); }

auto Queue::on_notify(std::string_view channel, std::string_view payload) -> void {
    if (channel != kNotifyChannel) return;
    const auto id = parse_i64(payload);
    if (!id) return;
    {
        const std::lock_guard lock{notify_mu_};
        notified_ids_.insert(*id);
    }
    notify_cv_.notify_all();
}

// ---------------------------------------------------------------------------
// Caller side
// ---------------------------------------------------------------------------

auto Queue::submit_remote(Surface surface, std::string_view payload_json,
                           std::chrono::system_clock::time_point deadline)
    -> std::expected<Result, SubmitError> {
    auto slot = pool_->acquire();
    if (!slot) return std::unexpected(map_pg_error(slot.error()));
    auto& conn = *slot;

    const std::string surface_str{to_string(surface)};
    const std::string payload{payload_json};
    const std::string deadline_str = iso8601(deadline);
    const std::string bound_str = std::to_string(config_.pending_bound_per_surface);

    // Bounded enqueue: the WHERE clause's own subquery counts this surface's
    // current pending rows and the INSERT...SELECT only produces a row (and
    // therefore only RETURNs one) when that count is still under the bound.
    // Zero rows back means the shared queue is full for this surface -- see
    // Config::pending_bound_per_surface's doc for why this is a soft bound
    // under concurrent submitters, and why that is an accepted tradeoff.
    std::array<std::optional<std::string>, 4> params{surface_str, payload, deadline_str, bound_str};
    auto res = conn->exec_params(
        "INSERT INTO inference_jobs (surface, payload, submit_deadline) "
        "SELECT $1, $2::jsonb, $3::timestamptz "
        "WHERE (SELECT count(*) FROM inference_jobs "
        "         WHERE surface = $1 AND state = 'pending') < $4::bigint "
        "RETURNING id",
        params);
    if (!res) return std::unexpected(map_pg_error(res.error()));
    if (res->rows() == 0) return std::unexpected(SubmitError::QueueFull);

    const auto id = parse_i64(res->text(0, 0));
    if (!id) return std::unexpected(SubmitError::DatabaseError);
    return Result{*id};
}

auto Queue::get_job(std::int64_t job_id) -> std::expected<std::optional<Job>, SubmitError> {
    auto slot = pool_->acquire();
    if (!slot) return std::unexpected(map_pg_error(slot.error()));
    auto& conn = *slot;

    const std::string id_str = std::to_string(job_id);
    std::array<std::optional<std::string>, 1> params{id_str};
    auto res = conn->exec_params(
        "SELECT id, surface, payload::text, state, attempts, max_attempts, "
        "       fencing_token, result::text, error "
        "FROM inference_jobs WHERE id = $1::bigint",
        params);
    if (!res) return std::unexpected(map_pg_error(res.error()));
    if (res->rows() == 0) return std::optional<Job>{};

    Job job;
    job.id = parse_i64(res->text(0, 0)).value_or(job_id);
    job.surface = surface_from_string(res->text(0, 1)).value_or(Surface::Strategy);
    job.payload = std::string(res->text(0, 2));
    job.state = job_state_from_string(res->text(0, 3)).value_or(JobState::Pending);
    job.attempts = parse_int(res->text(0, 4)).value_or(0);
    job.max_attempts = parse_int(res->text(0, 5)).value_or(2);
    job.fencing_token = parse_i64(res->text(0, 6));
    if (!res->is_null(0, 7)) job.result = std::string(res->text(0, 7));
    if (!res->is_null(0, 8)) job.error = std::string(res->text(0, 8));
    return std::optional<Job>{std::move(job)};
}

auto Queue::await_result(std::int64_t job_id, std::chrono::system_clock::time_point deadline)
    -> std::expected<Job, SubmitError> {
    while (true) {
        auto job = get_job(job_id);
        if (!job) return std::unexpected(job.error());
        if (!job->has_value()) return std::unexpected(SubmitError::NotFound);

        const auto state = (*job)->state;
        if (state == JobState::Done || state == JobState::Failed || state == JobState::Dead) {
            return std::move(**job);
        }

        const auto now = std::chrono::system_clock::now();
        if (now >= deadline) return std::unexpected(SubmitError::Timeout);

        // The correctness backstop: this wait NEVER exceeds poll_interval,
        // regardless of whether a LISTEN pump is running, has ever run, or
        // died silently mid-flight. A notification (see on_notify()) merely
        // makes the predicate below true sooner, which returns from
        // wait_for() early and re-polls immediately -- it does not change
        // the upper bound on how long a poll-only caller waits between
        // checks.
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        const auto tick = std::min(config_.poll_interval, remaining);

        std::unique_lock lock{notify_mu_};
        notify_cv_.wait_for(lock, tick,
                             [this, job_id] { return notified_ids_.contains(job_id); });
        notified_ids_.erase(job_id);
    }
}

// ---------------------------------------------------------------------------
// Worker side
// ---------------------------------------------------------------------------

auto Queue::lease(Surface surface, std::string_view worker_id)
    -> std::expected<std::optional<Job>, SubmitError> {
    auto slot = pool_->acquire();
    if (!slot) return std::unexpected(map_pg_error(slot.error()));
    auto& conn = *slot;

    const std::string surface_str{to_string(surface)};
    const std::string worker{worker_id};
    const std::string lease_secs = std::to_string(config_.lease_duration.count());

    // FOR UPDATE SKIP LOCKED on the inner subquery is what makes concurrent
    // lease() calls against the same surface never double-lease: a row
    // already locked by another in-flight lease() (or by the sweeper) is
    // invisible to this subquery rather than blocking on it, so N concurrent
    // callers against a queue with fewer than N eligible rows each get a
    // DISTINCT row (or nothing), never the same one.
    std::array<std::optional<std::string>, 3> params{surface_str, worker, lease_secs};
    auto res = conn->exec_params(
        "UPDATE inference_jobs j "
        "SET state = 'leased', "
        "    fencing_token = nextval('inference_fence'), "
        "    lease_owner = $2, "
        "    lease_deadline = now() + make_interval(secs => $3::double precision), "
        "    attempts = attempts + 1, "
        "    started_at = now() "
        "WHERE j.id = ( "
        "  SELECT id FROM inference_jobs "
        "  WHERE surface = $1 AND state = 'pending' AND submit_deadline > now() "
        "  ORDER BY id "
        "  FOR UPDATE SKIP LOCKED "
        "  LIMIT 1 "
        ") "
        "RETURNING j.id, j.payload::text, j.attempts, j.max_attempts, j.fencing_token",
        params);
    if (!res) return std::unexpected(map_pg_error(res.error()));
    if (res->rows() == 0) return std::optional<Job>{};  // nothing eligible -- not an error

    Job job;
    job.id = parse_i64(res->text(0, 0)).value_or(0);
    job.surface = surface;
    job.payload = std::string(res->text(0, 1));
    job.attempts = parse_int(res->text(0, 2)).value_or(0);
    job.max_attempts = parse_int(res->text(0, 3)).value_or(2);
    job.fencing_token = parse_i64(res->text(0, 4));
    job.state = JobState::Leased;
    return std::optional<Job>{std::move(job)};
}

auto Queue::heartbeat(std::int64_t job_id, std::int64_t fencing_token)
    -> std::expected<bool, SubmitError> {
    auto slot = pool_->acquire();
    if (!slot) return std::unexpected(map_pg_error(slot.error()));
    auto& conn = *slot;

    const std::string id_str = std::to_string(job_id);
    const std::string token_str = std::to_string(fencing_token);
    const std::string lease_secs = std::to_string(config_.lease_duration.count());
    std::array<std::optional<std::string>, 3> params{id_str, token_str, lease_secs};
    auto res = conn->exec_params(
        "UPDATE inference_jobs "
        "SET lease_deadline = now() + make_interval(secs => $3::double precision) "
        "WHERE id = $1::bigint AND fencing_token = $2::bigint AND state = 'leased' "
        "RETURNING id",
        params);
    if (!res) return std::unexpected(map_pg_error(res.error()));
    return res->rows() > 0;  // false = fenced out; caller MUST stop working on this job
}

auto Queue::complete(std::int64_t job_id, std::int64_t fencing_token,
                      std::string_view result_json) -> std::expected<bool, SubmitError> {
    auto slot = pool_->acquire();
    if (!slot) return std::unexpected(map_pg_error(slot.error()));
    auto& conn = *slot;

    if (auto begin = conn->exec("BEGIN"); !begin) {
        return std::unexpected(map_pg_error(begin.error()));
    }

    const std::string id_str = std::to_string(job_id);
    const std::string token_str = std::to_string(fencing_token);
    const std::string result_str{result_json};
    std::array<std::optional<std::string>, 3> params{id_str, token_str, result_str};
    auto update = conn->exec_params(
        "UPDATE inference_jobs "
        "SET state = 'done', result = $3::jsonb, finished_at = now() "
        "WHERE id = $1::bigint AND fencing_token = $2::bigint AND state = 'leased' "
        "RETURNING id",
        params);
    if (!update) {
        static_cast<void>(conn->exec("ROLLBACK"));
        return std::unexpected(map_pg_error(update.error()));
    }

    const bool fenced_in = update->rows() > 0;
    if (fenced_in) {
        // pg_notify in the SAME transaction as the UPDATE: it only takes
        // effect on COMMIT below, and only ever fires when this write
        // actually landed -- a fenced-out (stale) complete() sends no
        // notification at all, matching "discard this result" all the way
        // through.
        std::array<std::optional<std::string>, 2> notify_params{std::string(kNotifyChannel),
                                                                  id_str};
        auto notify = conn->exec_params("SELECT pg_notify($1::text, $2::text)", notify_params);
        if (!notify) {
            static_cast<void>(conn->exec("ROLLBACK"));
            return std::unexpected(map_pg_error(notify.error()));
        }
    }

    if (auto commit = conn->exec("COMMIT"); !commit) {
        return std::unexpected(map_pg_error(commit.error()));
    }
    return fenced_in;
}

auto Queue::fail(std::int64_t job_id, std::int64_t fencing_token, std::string_view error_message)
    -> std::expected<bool, SubmitError> {
    auto slot = pool_->acquire();
    if (!slot) return std::unexpected(map_pg_error(slot.error()));
    auto& conn = *slot;

    if (auto begin = conn->exec("BEGIN"); !begin) {
        return std::unexpected(map_pg_error(begin.error()));
    }

    const std::string id_str = std::to_string(job_id);
    const std::string token_str = std::to_string(fencing_token);
    const std::string err_str{error_message};
    std::array<std::optional<std::string>, 3> params{id_str, token_str, err_str};
    auto update = conn->exec_params(
        "UPDATE inference_jobs "
        "SET state = 'failed', error = $3, finished_at = now() "
        "WHERE id = $1::bigint AND fencing_token = $2::bigint AND state = 'leased' "
        "RETURNING id",
        params);
    if (!update) {
        static_cast<void>(conn->exec("ROLLBACK"));
        return std::unexpected(map_pg_error(update.error()));
    }

    const bool fenced_in = update->rows() > 0;
    if (fenced_in) {
        std::array<std::optional<std::string>, 2> notify_params{std::string(kNotifyChannel),
                                                                  id_str};
        auto notify = conn->exec_params("SELECT pg_notify($1::text, $2::text)", notify_params);
        if (!notify) {
            static_cast<void>(conn->exec("ROLLBACK"));
            return std::unexpected(map_pg_error(notify.error()));
        }
    }

    if (auto commit = conn->exec("COMMIT"); !commit) {
        return std::unexpected(map_pg_error(commit.error()));
    }
    return fenced_in;
}

// ---------------------------------------------------------------------------
// Maintenance
// ---------------------------------------------------------------------------

auto Queue::sweep_once() -> std::expected<SweepReport, SubmitError> {
    auto slot = pool_->acquire();
    if (!slot) return std::unexpected(map_pg_error(slot.error()));
    auto& conn = *slot;

    const std::string lock_id_str = std::to_string(kSweeperLockId);
    std::array<std::optional<std::string>, 1> lock_params{lock_id_str};
    auto locked = conn->exec_params("SELECT pg_try_advisory_lock($1::bigint)", lock_params);
    if (!locked) return std::unexpected(map_pg_error(locked.error()));
    if (locked->rows() == 0 || locked->text(0, 0) != "t") {
        // Another replica already holds the sweeper's advisory lock -- this
        // is the single-flight property working as designed, not a failure.
        return SweepReport{.ran = false};
    }

    // Always release the advisory lock before returning, on every path --
    // it is session-scoped, so leaving it held on an early return would
    // block every other replica's sweeper (and this one's own next sweep,
    // if the pool hands this same connection back out) until that pooled
    // connection happens to be closed and reopened.
    const auto unlock = [&conn, &lock_params] {
        static_cast<void>(conn->exec_params("SELECT pg_advisory_unlock($1::bigint)", lock_params));
    };

    SweepReport report;
    report.ran = true;

    // Step 1: leases past their deadline go back to 'pending' (a retry
    // remains) or to 'dead' (the DLQ) -- see the migration's own comment on
    // why max_attempts defaults to 2.
    auto expired = conn->exec(
        "WITH expired AS ( "
        "  SELECT id, "
        "         CASE WHEN attempts < max_attempts AND submit_deadline > now() "
        "              THEN 'pending' ELSE 'dead' END AS new_state "
        "  FROM inference_jobs "
        "  WHERE state = 'leased' AND lease_deadline < now() "
        "  FOR UPDATE SKIP LOCKED "
        ") "
        "UPDATE inference_jobs j "
        "SET state = e.new_state, "
        "    lease_owner = NULL, "
        "    lease_deadline = NULL, "
        "    finished_at = CASE WHEN e.new_state = 'dead' THEN now() ELSE j.finished_at END "
        "FROM expired e "
        "WHERE j.id = e.id "
        "RETURNING e.new_state");
    if (!expired) {
        unlock();
        return std::unexpected(map_pg_error(expired.error()));
    }
    for (int i = 0; i < expired->rows(); ++i) {
        if (expired->text(i, 0) == "pending") {
            ++report.requeued;
        } else {
            ++report.dead;
        }
    }

    // Step 2: pending rows whose caller has already given up (submit_deadline
    // passed while nobody ever leased them) fail outright rather than sitting
    // in the queue forever.
    auto timed_out = conn->exec(
        "UPDATE inference_jobs "
        "SET state = 'failed', finished_at = now(), "
        "    error = 'submit_deadline exceeded while pending' "
        "WHERE state = 'pending' AND submit_deadline <= now() "
        "RETURNING id");
    if (!timed_out) {
        unlock();
        return std::unexpected(map_pg_error(timed_out.error()));
    }
    report.expired_pending_failed = timed_out->rows();

    // Step 3: reap old terminal rows so the table does not grow without
    // bound. Two different retentions in one statement because 'dead' rows
    // are the ones worth keeping around longer to investigate (see
    // Config::dead_retention's default of 7 days vs done/failed's 24h).
    const std::string done_hours = std::to_string(
        std::chrono::duration_cast<std::chrono::hours>(config_.done_retention).count());
    const std::string dead_hours = std::to_string(
        std::chrono::duration_cast<std::chrono::hours>(config_.dead_retention).count());
    std::array<std::optional<std::string>, 2> reap_params{done_hours, dead_hours};
    auto reaped = conn->exec_params(
        "DELETE FROM inference_jobs "
        "WHERE (state IN ('done', 'failed') "
        "         AND finished_at < now() - make_interval(hours => $1::int)) "
        "   OR (state = 'dead' "
        "         AND finished_at < now() - make_interval(hours => $2::int)) "
        "RETURNING id",
        reap_params);
    if (!reaped) {
        unlock();
        return std::unexpected(map_pg_error(reaped.error()));
    }
    report.reaped = reaped->rows();

    unlock();
    return report;
}

}  // namespace options_calculator::inference_queue
