// @author Olumuyiwa Oluwasanmi
//
// Tests pg.cppm + inference_queue.cppm against a REAL, reachable Postgres --
// DATABASE_URL must be a libpq keyword/value connection string (not a URI;
// this file appends `application_name=...` to it directly, which only the
// keyword/value form tolerates as plain text concatenation -- pg.cppm's own
// Connection::connect() supports either form for real callers via
// PQconnectdbParams's expand_dbname). Example:
//
//   DATABASE_URL="host=/tmp/iqpg_sock port=55432 dbname=inference_queue_test user=postgres" \
//     ./test_inference_queue_pg
//
// Not registered with add_test()/ctest -- see the CMakeLists.txt comment
// next to this target for why. Every section TRUNCATEs the table first, so
// sections are independent and order does not matter for correctness (it
// does matter for readability, so they still run in a deliberate order).
//
// Plain hand-rolled check()/section() harness, matching every other test in
// this tree -- not gtest (sensen's cpp_details.txt rule 39 forbids external
// test frameworks project-wide).
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

import pg;
import inference_queue;

using namespace std::chrono_literals;
using options_calculator::inference_queue::Job;
using options_calculator::inference_queue::JobState;
using options_calculator::inference_queue::Queue;
using options_calculator::inference_queue::Result;
using options_calculator::inference_queue::SubmitError;
using options_calculator::inference_queue::Surface;
using options_calculator::inference_queue::SweepReport;
namespace pg = options_calculator::pg;

namespace {

int g_checks = 0;
int g_failures = 0;

auto check(bool condition, const std::string& what) -> void {
    ++g_checks;
    if (condition) {
        std::printf("  PASS: %s\n", what.c_str());
    } else {
        ++g_failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

auto section(const char* title) -> void { std::printf("\n=== %s ===\n", title); }

// --- Test scaffolding: a raw admin connection outside the module's own
// pool, used only to set up/inspect database state directly (force-expire a
// lease, count rows, read back a column) -- never to exercise the protocol
// under test, which always goes through Queue/Pool.
[[nodiscard]] auto base_conninfo() -> std::string {
    const char* raw = std::getenv("DATABASE_URL");
    return (raw != nullptr) ? std::string{raw} : std::string{};
}

[[nodiscard]] auto admin_connect(const std::string& app_name) -> pg::Connection {
    auto conn = pg::Connection::connect(base_conninfo() + " application_name=" + app_name, 2000ms,
                                         2000ms);
    if (!conn) {
        std::printf("FATAL: admin connection failed: %s\n", conn.error().message.c_str());
        std::exit(1);
    }
    return std::move(*conn);
}

auto reset_db(pg::Connection& admin) -> void {
    auto res = admin.exec("TRUNCATE TABLE inference_jobs RESTART IDENTITY");
    if (!res) {
        std::printf("FATAL: TRUNCATE failed: %s\n", res.error().message.c_str());
        std::exit(1);
    }
}

[[nodiscard]] auto make_pool(const std::string& app_name, std::size_t size = 6)
    -> std::shared_ptr<pg::Pool> {
    pg::PoolConfig cfg;
    cfg.conninfo = base_conninfo() + " application_name=" + app_name;
    cfg.size = size;
    return std::make_shared<pg::Pool>(std::move(cfg));
}

[[nodiscard]] auto row_state(pg::Connection& admin, std::int64_t id) -> std::string {
    std::array<std::optional<std::string>, 1> params{std::to_string(id)};
    auto res = admin.exec_params("SELECT state FROM inference_jobs WHERE id = $1::bigint", params);
    if (!res || res->rows() == 0) return "<missing>";
    return std::string{res->text(0, 0)};
}

[[nodiscard]] auto row_result(pg::Connection& admin, std::int64_t id) -> std::optional<std::string> {
    std::array<std::optional<std::string>, 1> params{std::to_string(id)};
    auto res =
        admin.exec_params("SELECT result::text FROM inference_jobs WHERE id = $1::bigint", params);
    if (!res || res->rows() == 0 || res->is_null(0, 0)) return std::nullopt;
    return std::string{res->text(0, 0)};
}

auto force_expire_lease(pg::Connection& admin, std::int64_t id) -> void {
    std::array<std::optional<std::string>, 1> params{std::to_string(id)};
    auto res = admin.exec_params(
        "UPDATE inference_jobs SET lease_deadline = now() - interval '1 second' "
        "WHERE id = $1::bigint",
        params);
    if (!res) {
        std::printf("FATAL: force_expire_lease failed: %s\n", res.error().message.c_str());
        std::exit(1);
    }
}

constexpr auto kFarFuture = []() { return std::chrono::system_clock::now() + 24h; };

}  // namespace

auto main() -> int {
    if (base_conninfo().empty()) {
        std::printf(
            "FATAL: DATABASE_URL is not set. This suite needs a real, reachable Postgres -- "
            "see this file's own banner for how to point it at one. NO tests below were run.\n");
        return 1;
    }

    auto admin = admin_connect("iq_test_admin");

    // -----------------------------------------------------------------
    section("8 concurrent lessors against a populated queue -- no double-lease");
    {
        reset_db(admin);
        auto pool = make_pool("iq_test_concurrency", /*size=*/10);
        Queue queue{pool};

        constexpr int kJobs = 40;
        for (int i = 0; i < kJobs; ++i) {
            auto submitted =
                queue.submit_remote(Surface::Strategy, R"({"n":)" + std::to_string(i) + "}",
                                     kFarFuture());
            check(submitted.has_value(), "seed job " + std::to_string(i) + " submitted");
        }

        std::mutex leased_mu;
        std::vector<std::int64_t> leased_ids;
        std::atomic<int> lease_calls{0};

        auto worker = [&](int worker_no) {
            while (true) {
                auto job = queue.lease(Surface::Strategy, "worker-" + std::to_string(worker_no));
                lease_calls.fetch_add(1);
                if (!job) {
                    std::printf("  worker %d: lease() error: %s\n", worker_no,
                                std::string(options_calculator::inference_queue::to_string(
                                                job.error()))
                                    .c_str());
                    return;
                }
                if (!job->has_value()) return;  // queue drained for this worker
                const std::lock_guard lock{leased_mu};
                leased_ids.push_back((*job)->id);
            }
        };

        std::vector<std::thread> threads;
        threads.reserve(8);
        for (int i = 0; i < 8; ++i) threads.emplace_back(worker, i);
        for (auto& t : threads) t.join();

        check(static_cast<int>(leased_ids.size()) == kJobs,
              "all " + std::to_string(kJobs) + " seeded jobs were leased exactly once in total "
              "(got " + std::to_string(leased_ids.size()) + ")");

        std::unordered_set<std::int64_t> distinct{leased_ids.begin(), leased_ids.end()};
        check(distinct.size() == leased_ids.size(),
              "NO job id was leased more than once across all 8 threads (distinct=" +
                  std::to_string(distinct.size()) + ", total=" + std::to_string(leased_ids.size()) +
                  ") -- FOR UPDATE SKIP LOCKED held under real concurrency");

        check(lease_calls.load() >= kJobs,
              "lease() was called at least once per job (plus one draining call per worker), "
              "proving the workers actually raced each other rather than partitioning work "
              "some other way");
    }

    // -----------------------------------------------------------------
    section("Expired lease -> re-leased -> STALE worker's fenced complete() is discarded");
    {
        reset_db(admin);
        auto pool = make_pool("iq_test_fencing");
        Queue queue{pool};

        auto submitted = queue.submit_remote(Surface::Strategy, R"({"q":1})", kFarFuture());
        check(submitted.has_value(), "job submitted");
        const auto id = submitted->job_id;

        auto first = queue.lease(Surface::Strategy, "worker-A");
        check(first.has_value() && first->has_value(), "worker A leases the job");
        const auto token_a = (*first)->fencing_token.value_or(-1);
        check(token_a >= 0, "worker A's lease carries a fencing token");

        force_expire_lease(admin, id);
        auto swept = queue.sweep_once();
        check(swept.has_value() && swept->ran, "sweep_once() ran (held the advisory lock)");
        check(swept.has_value() && swept->requeued == 1,
              "sweep_once() requeued exactly the one expired lease (attempts=1 < max_attempts=2)");
        check(row_state(admin, id) == "pending", "the row is back to 'pending' after the sweep");

        auto second = queue.lease(Surface::Strategy, "worker-B");
        check(second.has_value() && second->has_value(), "worker B leases the SAME job");
        check(second.has_value() && second->has_value() && (*second)->id == id,
              "worker B's lease is for the same job id worker A originally held");
        const auto token_b = (*second)->fencing_token.value_or(-1);
        check(token_b != token_a,
              "worker B's fencing token (" + std::to_string(token_b) +
                  ") differs from worker A's stale one (" + std::to_string(token_a) + ")");

        // THE discriminating check: worker A -- unaware its lease expired --
        // now tries to report success with its OLD token.
        auto stale_complete = queue.complete(id, token_a, R"({"from":"stale-worker-A"})");
        check(stale_complete.has_value() && stale_complete.value() == false,
              "worker A's fenced complete() with the STALE token updates ZERO rows and reports "
              "false -- MUST be discarded by the caller, not treated as success");
        check(row_state(admin, id) == "leased",
              "the row is still 'leased' (by worker B) after worker A's stale write -- worker "
              "A's result did NOT land");

        auto real_complete = queue.complete(id, token_b, R"({"from":"worker-B"})");
        check(real_complete.has_value() && real_complete.value() == true,
              "worker B's complete() with the CURRENT token updates the row and reports true");
        check(row_state(admin, id) == "done", "the row is 'done' after worker B's real complete()");
        const auto result = row_result(admin, id);
        check(result.has_value() && result->find("worker-B") != std::string::npos,
              "the stored result is worker B's, never worker A's stale payload -- no "
              "double-execution occurred");
    }

    // -----------------------------------------------------------------
    section("A job past its submit_deadline is never leased");
    {
        reset_db(admin);
        auto pool = make_pool("iq_test_deadline");
        Queue queue{pool};

        const auto past = std::chrono::system_clock::now() - 1h;
        auto submitted = queue.submit_remote(Surface::Strategy, R"({"late":true})", past);
        check(submitted.has_value(), "a job with an already-past submit_deadline still enqueues "
                                      "(submission and eligibility are independent checks)");

        auto leased = queue.lease(Surface::Strategy, "worker-X");
        check(leased.has_value() && !leased->has_value(),
              "lease() finds nothing to claim -- the row exists but submit_deadline > now() "
              "fails, exactly as the LEASE query requires");

        auto swept = queue.sweep_once();
        check(swept.has_value() && swept->expired_pending_failed == 1,
              "sweep_once() instead fails the row outright (still pending, deadline passed)");
        check(row_state(admin, submitted->job_id) == "failed",
              "the row is 'failed', never 'leased' -- it was never eligible");
    }

    // -----------------------------------------------------------------
    section("LISTEN/NOTIFY wakes await_result() early; suppressing it falls back to the poll");
    {
        reset_db(admin);
        auto pool = make_pool("iq_test_notify");
        Queue queue{pool};

        // --- with the pump running ---
        auto pump_started = queue.start_notify_pump();
        check(pump_started.has_value(), "start_notify_pump() succeeds");

        auto submitted = queue.submit_remote(Surface::Strategy, R"({"a":1})", kFarFuture());
        check(submitted.has_value(), "job submitted for the notify-pump case");
        const auto id1 = submitted->job_id;

        auto leased = queue.lease(Surface::Strategy, "worker-notify");
        check(leased.has_value() && leased->has_value(), "job leased for the notify-pump case");
        const auto token1 = (*leased)->fencing_token.value_or(-1);

        std::thread completer1([&] {
            std::this_thread::sleep_for(60ms);
            auto ok = queue.complete(id1, token1, R"({"ok":true})");
            if (!ok || !ok.value()) std::printf("  (completer1 failed to complete)\n");
        });

        const auto t0 = std::chrono::steady_clock::now();
        auto awaited1 = queue.await_result(id1, std::chrono::system_clock::now() + 5s);
        const auto elapsed1 = std::chrono::steady_clock::now() - t0;
        completer1.join();

        check(awaited1.has_value() && awaited1->state == JobState::Done,
              "await_result() observes the completion with the pump running");
        check(elapsed1 < 200ms,
              "await_result() returned in " +
                  std::to_string(
                      std::chrono::duration_cast<std::chrono::milliseconds>(elapsed1).count()) +
                  "ms, well under one 250ms poll_interval -- the NOTIFY hint woke it early "
                  "rather than it merely surviving to the next poll tick");

        queue.stop_notify_pump();

        // --- with notifications fully suppressed (pump never (re)started) ---
        auto submitted2 = queue.submit_remote(Surface::Strategy, R"({"a":2})", kFarFuture());
        check(submitted2.has_value(), "job submitted for the suppressed-notify case");
        const auto id2 = submitted2->job_id;

        auto leased2 = queue.lease(Surface::Strategy, "worker-notify");
        check(leased2.has_value() && leased2->has_value(), "job leased for the suppressed case");
        const auto token2 = (*leased2)->fencing_token.value_or(-1);

        std::thread completer2([&] {
            std::this_thread::sleep_for(60ms);
            auto ok = queue.complete(id2, token2, R"({"ok":true})");
            if (!ok || !ok.value()) std::printf("  (completer2 failed to complete)\n");
        });

        const auto t1 = std::chrono::steady_clock::now();
        auto awaited2 = queue.await_result(id2, std::chrono::system_clock::now() + 5s);
        const auto elapsed2 = std::chrono::steady_clock::now() - t1;
        completer2.join();

        check(awaited2.has_value() && awaited2->state == JobState::Done,
              "await_result() STILL observes the completion with NO pump running at all -- the "
              "250ms poll backstop alone is sufficient for correctness");
        check(elapsed2 >= 60ms,
              "this run took at least as long as the completer's own delay (sanity: it did not "
              "return instantly by accident)");
    }

    // -----------------------------------------------------------------
    section("A pool connection is killed mid-flight -- the pool detects it and recovers");
    {
        reset_db(admin);
        auto pool = make_pool("iq_test_kill_conn", /*size=*/2);

        // Force every pooled connection to actually open before we go hunting
        // for its backend pid in pg_stat_activity.
        {
            auto a = pool->acquire();
            auto b = pool->acquire();
            check(a.has_value() && b.has_value(), "both pool connections opened");
        }

        auto victim = admin.exec(
            "SELECT pid FROM pg_stat_activity WHERE application_name = 'iq_test_kill_conn' "
            "LIMIT 1");
        check(victim.has_value() && victim->rows() == 1,
              "found exactly one live backend for the pool's application_name");
        if (victim.has_value() && victim->rows() == 1) {
            const std::string pid_str{victim->text(0, 0)};
            std::array<std::optional<std::string>, 1> kill_params{pid_str};
            auto killed = admin.exec_params("SELECT pg_terminate_backend($1::int)", kill_params);
            check(killed.has_value(), "pg_terminate_backend() issued against a live pool "
                                       "connection's backend pid " +
                                           pid_str);
        }

        // Give the kernel/postmaster a moment to actually tear the socket
        // down, then prove the pool is still usable: acquire() must either
        // hand back a healthy connection (having reconnected the dead slot)
        // or, at worst, fail ONE bounded acquire() -- it must never hang.
        std::this_thread::sleep_for(200ms);

        bool recovered = false;
        for (int attempt = 0; attempt < 5 && !recovered; ++attempt) {
            auto slot = pool->acquire();
            if (slot.has_value()) {
                auto probe = (*slot)->exec("SELECT 1");
                recovered = probe.has_value();
            }
            if (!recovered) std::this_thread::sleep_for(100ms);
        }
        check(recovered,
              "the pool served a healthy, working connection again after one of its two "
              "connections was killed out from under it -- acquire() reconnected rather than "
              "handing back (or hanging on) the dead one");
    }

    // -----------------------------------------------------------------
    section("Abandoned lease (simulated worker crash) -- sweeper requeues, second attempt "
            "completes");
    {
        reset_db(admin);
        pg::PoolConfig cfg;
        cfg.conninfo = base_conninfo() + " application_name=iq_test_crash";
        cfg.size = 4;
        auto pool = std::make_shared<pg::Pool>(std::move(cfg));
        Queue::Config qcfg;
        qcfg.lease_duration = 1s;  // short, so the test does not need to sleep 30s
        Queue queue{pool, qcfg};

        auto submitted = queue.submit_remote(Surface::Mortgage, R"({"m":1})", kFarFuture());
        check(submitted.has_value(), "job submitted");
        const auto id = submitted->job_id;

        auto crashed_lease = queue.lease(Surface::Mortgage, "worker-doomed");
        check(crashed_lease.has_value() && crashed_lease->has_value(),
              "the doomed worker leases the job");
        // The worker now vanishes: no heartbeat, no complete(), no fail() --
        // exactly what a crash looks like from the queue's point of view.
        // (The pooled TCP connection it used is already back in the pool,
        // unharmed, by the time lease() returned -- the previous section
        // already proves the pool tolerates a genuinely killed connection.
        // What matters HERE is that lease ownership lives in the ROW, not in
        // any connection, so nothing about that connection's fate is even
        // relevant to whether this job recovers.)

        std::this_thread::sleep_for(1200ms);  // past the 1s lease_duration

        auto swept = queue.sweep_once();
        check(swept.has_value() && swept->ran, "sweep_once() ran");
        check(swept.has_value() && swept->requeued == 1,
              "the abandoned lease was requeued to pending (attempts=1 < max_attempts=2)");

        auto second_lease = queue.lease(Surface::Mortgage, "worker-recovers");
        check(second_lease.has_value() && second_lease->has_value(),
              "a second worker leases the SAME job after the sweep");
        check(second_lease.has_value() && second_lease->has_value() &&
                  (*second_lease)->id == id,
              "it is the same job id");
        check(second_lease.has_value() && second_lease->has_value() &&
                  (*second_lease)->attempts == 2,
              "attempts is now 2 (one from the doomed lease, one from this one)");

        const auto token = (*second_lease)->fencing_token.value_or(-1);
        auto completed = queue.complete(id, token, R"({"recovered":true})");
        check(completed.has_value() && completed.value() == true,
              "the second attempt completes successfully");
        check(row_state(admin, id) == "done", "the row is 'done' -- the job was NOT lost");
    }

    // -----------------------------------------------------------------
    section("Crash/restart mid-everything: no job lost, no job double-completed");
    {
        reset_db(admin);
        pg::PoolConfig cfg;
        cfg.conninfo = base_conninfo() + " application_name=iq_test_restart_gen1";
        cfg.size = 4;
        auto pool_gen1 = std::make_shared<pg::Pool>(std::move(cfg));
        Queue::Config qcfg;
        qcfg.lease_duration = 1s;
        Queue queue_gen1{pool_gen1, qcfg};

        constexpr int kTotal = 10;
        std::vector<std::int64_t> ids;
        for (int i = 0; i < kTotal; ++i) {
            auto submitted = queue_gen1.submit_remote(Surface::Strategy,
                                                        R"({"i":)" + std::to_string(i) + "}",
                                                        kFarFuture());
            check(submitted.has_value(), "restart-test job " + std::to_string(i) + " submitted");
            ids.push_back(submitted->job_id);
        }

        // Half get leased and then abandoned (the "crash"); half are left
        // untouched (still pending when the "process" goes away).
        std::int64_t stale_id = -1;
        std::int64_t stale_token = -1;
        for (int i = 0; i < kTotal / 2; ++i) {
            auto leased = queue_gen1.lease(Surface::Strategy, "worker-pre-crash");
            check(leased.has_value() && leased->has_value(),
                  "pre-crash lease " + std::to_string(i));
            if (i == 0 && leased.has_value() && leased->has_value()) {
                stale_id = (*leased)->id;
                stale_token = (*leased)->fencing_token.value_or(-1);
            }
        }
        // gen1's Pool/Queue now go out of scope below -- simulating the
        // process exiting without ever completing anything. Nothing about
        // job ownership lived in that process; it all lived in the rows.

        std::this_thread::sleep_for(1200ms);  // past the abandoned leases' 1s deadline

        // "Restart": a brand new Pool and a brand new Queue, as a fresh
        // process would construct on boot.
        pg::PoolConfig cfg2;
        cfg2.conninfo = base_conninfo() + " application_name=iq_test_restart_gen2";
        cfg2.size = 4;
        auto pool_gen2 = std::make_shared<pg::Pool>(std::move(cfg2));
        Queue queue_gen2{pool_gen2, qcfg};

        auto swept = queue_gen2.sweep_once();
        check(swept.has_value() && swept->ran, "the new instance's sweep_once() runs");
        check(swept.has_value() && swept->requeued == kTotal / 2,
              "every one of the " + std::to_string(kTotal / 2) +
                  " abandoned leases was requeued, not lost");

        std::unordered_set<std::int64_t> completed_ids;
        for (int i = 0; i < kTotal; ++i) {
            auto leased = queue_gen2.lease(Surface::Strategy, "worker-post-restart");
            check(leased.has_value() && leased->has_value(),
                  "post-restart lease " + std::to_string(i));
            if (!leased.has_value() || !leased->has_value()) continue;
            const auto job_id = (*leased)->id;
            const auto token = (*leased)->fencing_token.value_or(-1);
            auto ok = queue_gen2.complete(job_id, token,
                                           R"({"restarted":)" + std::to_string(job_id) + "}");
            check(ok.has_value() && ok.value(), "post-restart complete for job id " +
                                                     std::to_string(job_id));
            if (ok.has_value() && ok.value()) completed_ids.insert(job_id);
        }

        check(completed_ids.size() == static_cast<std::size_t>(kTotal),
              "exactly " + std::to_string(kTotal) +
                  " DISTINCT jobs ended up completed -- every originally-submitted job is "
                  "accounted for exactly once (got " + std::to_string(completed_ids.size()) + ")");

        for (const auto id : ids) {
            check(completed_ids.contains(id),
                  "originally-submitted job id " + std::to_string(id) + " is among the completed "
                  "set");
        }

        // The stale pre-crash lease's own token must still be fenced out even
        // now, long after its job has already been completed by someone else
        // entirely -- this is the same double-execution guard as the
        // dedicated fencing section above, checked again here in the more
        // realistic multi-job restart scenario.
        if (stale_id >= 0) {
            auto stale_attempt =
                queue_gen2.complete(stale_id, stale_token, R"({"from":"pre-crash-worker"})");
            check(stale_attempt.has_value() && stale_attempt.value() == false,
                  "the pre-crash worker's stale complete() (job " + std::to_string(stale_id) +
                      ") is still discarded after the restart, even though the job is long since "
                      "done");
            check(row_result(admin, stale_id).value_or("").find("pre-crash-worker") ==
                      std::string::npos,
                  "the stale pre-crash payload never overwrote the real result");
        }

        auto remaining = admin.exec("SELECT count(*) FROM inference_jobs WHERE state NOT IN "
                                     "('done','failed','dead')");
        check(remaining.has_value() && remaining->rows() == 1 &&
                  remaining->text(0, 0) == "0",
              "no row is left pending or leased -- every job reached a terminal state");
    }

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
