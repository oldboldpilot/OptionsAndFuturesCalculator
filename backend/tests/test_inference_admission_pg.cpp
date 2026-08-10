// @author Olumuyiwa Oluwasanmi
//
// Tests the Postgres-backed half of inference_admission.cppm --
// PostgresLeaseSource and PostgresAdmission -- against a REAL, reachable
// Postgres. DATABASE_URL must be a libpq keyword/value connection string
// (not a URI; this file appends `application_name=...` to it directly, same
// convention as test_inference_queue_pg.cpp). Example:
//
//   DATABASE_URL="host=/tmp/iqpg_sock port=55432 dbname=inference_queue_test user=postgres" \
//     ./test_inference_admission_pg
//
// Not registered with add_test()/ctest -- same reasoning as
// test_inference_queue_pg (needs a real reachable Postgres and exercises
// real concurrency/timing). Every section TRUNCATEs the table first.
//
// Plain hand-rolled check()/section() harness -- not gtest
// (config/cpp_details.txt rule 39).
//
// WHAT THIS SUITE PROVES THAT test_inference_queue_pg DOES NOT: everything
// here goes through inference_admission.cppm's PostgresLeaseSource/
// PostgresAdmission -- the NEW code this task adds -- rather than calling
// Queue's raw lease()/complete()/fail() directly. In particular:
//
//   1. A job submitted through PostgresAdmission on one Queue/Pool instance
//      ("replica A", submit-only) is served by a SECOND, independent
//      Queue/Pool instance's QueuedBackend with a PostgresLeaseSource
//      installed ("replica B", lease-only) -- proving the cross-replica
//      hand-off works through the admission layer, not just at the Queue
//      API level.
//   2. PostgresAdmission::submit() falls back to the wrapped local backend,
//      quickly and without hanging, when Postgres is unreachable.
//   3. A fenced-out stale complete() is discarded even when the REAL
//      completion was written back by PostgresLeaseSource's own spawned
//      helper thread (spawn_writeback), not by a raw Queue::complete() call
//      in the test itself.
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <sys/wait.h>
#include <unistd.h>

import pg;
import inference_queue;
import inference_admission;

using namespace std::chrono_literals;
using options_calculator::inference_admission::InferenceBackend;
using options_calculator::inference_admission::InferenceOutcome;
using options_calculator::inference_admission::PostgresAdmission;
using options_calculator::inference_admission::PostgresLeaseSource;
using options_calculator::inference_admission::QueuedBackend;
namespace inference_queue = options_calculator::inference_queue;
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

[[nodiscard]] auto make_queue(const std::string& app_name) -> std::shared_ptr<inference_queue::Queue> {
    pg::PoolConfig cfg;
    cfg.conninfo = base_conninfo() + " application_name=" + app_name;
    cfg.size = 4;
    auto pool = std::make_shared<pg::Pool>(std::move(cfg));
    return std::make_shared<inference_queue::Queue>(std::move(pool));
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

/** `lease_owner` is set ONLY by Queue::lease()'s own UPDATE -- never by
 *  PostgresAdmission's fallback path, which calls the wrapped local
 *  backend directly and never touches inference_jobs at all. A non-null
 *  lease_owner on a `done` row is therefore direct, unambiguous proof that
 *  a real lease() call served the request, independent of anything the
 *  decode text itself says (which two backends sharing one process can tag
 *  identically -- see the "single-process production wiring" section
 *  below for exactly that case). */
[[nodiscard]] auto row_owner(pg::Connection& admin, std::int64_t id) -> std::optional<std::string> {
    std::array<std::optional<std::string>, 1> params{std::to_string(id)};
    auto res = admin.exec_params("SELECT lease_owner FROM inference_jobs WHERE id = $1::bigint",
                                   params);
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

/** A local InferenceBackend stand-in that never talks to any real model --
 *  it deterministically tags its output so a test can tell "this answer came
 *  from the LOCAL fallback path" apart from "this answer came from the
 *  shared Postgres queue". */
class LocalStubBackend final : public InferenceBackend {
  public:
    explicit LocalStubBackend(std::string tag) : tag_(std::move(tag)) {}

    [[nodiscard]] auto submit(std::string prompt) -> std::optional<InferenceOutcome> override {
        return InferenceOutcome{.ok = true, .text = tag_ + ":" + prompt, .error = {}};
    }
    [[nodiscard]] auto name() const noexcept -> std::string_view override { return "local-stub"; }

  private:
    std::string tag_;
};

/** A trivial concrete QueuedBackend standing in for SensenBackend/
 *  LlamaCppBackend's real decode loop: it echoes every prompt it takes,
 *  tagged "leased:", regardless of whether the job came from a local
 *  submission or -- the case this suite actually exercises -- from a
 *  PostgresLeaseSource fill(). No gate: always runs immediately, since these
 *  tests care about the cross-worker/fencing protocol, not fine-grained
 *  admission-queue timing (that is test_inference_admission.cpp's job).
 *
 *  Deliberately does NOT start its owner thread in the constructor -- every
 *  section below that installs a lease source calls set_lease_source() THEN
 *  start(), mirroring the exact order assistant_service.cpp/mortgage_
 *  assistant_service.cpp's Worker constructors now enforce (see
 *  QueuedBackend::start()'s own doc in inference_admission.cppm for the
 *  startup race that order exists to make impossible). Starting eagerly here
 *  is exactly the bug this task's own regression test (below) exists to
 *  catch. */
class MockDecodeBackend final : public QueuedBackend {
  public:
    /** `decode_delay` is 0 for the cross-worker/fallback sections (fast is
     *  fine there) and a real delay for the fencing section below, which
     *  needs a window where the row is genuinely `leased` (fresh token, not
     *  yet completed) to attempt a stale write against -- without it, the
     *  real completion can land before the test ever gets to try. */
    explicit MockDecodeBackend(std::size_t max_concurrent = 2,
                                std::chrono::milliseconds decode_delay = 0ms)
        : QueuedBackend("mock decode backend shutting down"), decode_delay_(decode_delay) {
        max_concurrent_ = max_concurrent;
        max_queue_depth_ = 8;
    }
    ~MockDecodeBackend() override {
        if (worker_.joinable()) {
            worker_.request_stop();
            worker_.join();
        }
        drain_and_fail("mock decode backend shutting down");
    }

    [[nodiscard]] auto name() const noexcept -> std::string_view override { return "mock-decode"; }

    auto start() -> void override {
        worker_ = std::jthread([this](std::stop_token st) { run(st); });
    }

  private:
    auto run(std::stop_token st) -> void {
        while (!st.stop_requested()) {
            auto jobs = take_jobs(st, max_concurrent_, /*block=*/true);
            for (auto& job : jobs) {
                if (decode_delay_.count() > 0) std::this_thread::sleep_for(decode_delay_);
                job.promise.set_value(
                    InferenceOutcome{.ok = true, .text = "leased:" + job.prompt, .error = {}});
            }
        }
    }

    std::chrono::milliseconds decode_delay_;
    std::jthread worker_;
};

}  // namespace

auto main() -> int {
    if (base_conninfo().empty()) {
        std::printf(
            "FATAL: DATABASE_URL is not set. This suite needs a real, reachable Postgres -- "
            "see this file's own banner for how to point it at one. NO tests below were run.\n");
        return 1;
    }

    auto admin = admin_connect("admission_test_admin");

    // -----------------------------------------------------------------
    section("Two-worker cross-instance hand-off: replica A submits, replica B leases and decodes");
    {
        reset_db(admin);

        // Replica A: submit-only. Its local backend is a stub that must
        // NEVER be called if the shared-queue path works -- a successful
        // result tagged "LOCAL_FALLBACK" would mean the cross-worker path
        // silently degraded to local, which is exactly what this section is
        // here to rule out.
        auto queue_a = make_queue("admission_test_replica_a");
        LocalStubBackend local_a{"LOCAL_FALLBACK"};
        PostgresAdmission admission_a{queue_a, inference_queue::Surface::Strategy, local_a, 8000ms};

        // Replica B: lease-only. A mock decode backend with a REAL
        // PostgresLeaseSource installed, sharing the same Postgres as A but
        // otherwise a completely independent Queue/Pool -- the same shape
        // two separate engine replicas would have in production.
        auto queue_b = make_queue("admission_test_replica_b");
        MockDecodeBackend backend_b;
        auto lease_source_b = std::make_shared<PostgresLeaseSource>(
            queue_b, inference_queue::Surface::Strategy, "worker-B");
        backend_b.set_lease_source(lease_source_b);
        // start() AFTER set_lease_source(), never before -- see
        // MockDecodeBackend's own class banner and QueuedBackend::start()'s
        // doc for why the order is load-bearing, not stylistic.
        backend_b.start();

        std::optional<InferenceOutcome> result;
        std::thread submitter([&] { result = admission_a.submit("hello-cross-worker"); });
        submitter.join();

        check(result.has_value() && result->ok,
              "replica A's submit() returned a successful InferenceOutcome");
        check(result.has_value() && result->ok && result->text == "leased:hello-cross-worker",
              "the result is replica B's decode ('leased:hello-cross-worker'), NOT replica A's "
              "local fallback -- proving the job crossed from A to B through the shared queue, "
              "not through PostgresAdmission's own degrade path");
    }

    // -----------------------------------------------------------------
    section("Cross-PROCESS hand-off: job submitted by process A is served by a GENUINELY "
            "SEPARATE process B (fork(), not merely two objects in one process)");
    {
        reset_db(admin);

        // No other threads are alive in this process at this point -- every
        // earlier section's backend/worker threads have already been joined
        // by their own destructors on scope exit -- so fork() here is safe:
        // the child inherits a single-threaded, consistent copy of this
        // process's state. The child never touches `admin` (the parent's own
        // already-open connection); it opens its own fresh connections after
        // forking, the same safe pattern any libpq + fork() program follows.
        const pid_t pid = fork();
        if (pid < 0) {
            std::printf("FATAL: fork() failed\n");
            std::exit(1);
        }
        if (pid == 0) {
            // ---- Child: replica B, lease + decode only, in its own process.
            auto queue_b = make_queue("admission_test_fork_child");
            MockDecodeBackend backend_b;
            auto lease_source_b = std::make_shared<PostgresLeaseSource>(
                queue_b, inference_queue::Surface::Strategy, "fork-child-worker");
            backend_b.set_lease_source(lease_source_b);
            backend_b.start();

            // Serve for up to 5s -- long enough for the parent to submit and
            // observe completion -- then exit unconditionally. _Exit (not
            // exit) skips static destructors and atexit handlers: this
            // child shares fd's with the parent (stdout, the inherited-but-
            // unused `admin` socket) that a normal exit's teardown could
            // otherwise disturb.
            std::this_thread::sleep_for(5000ms);
            std::_Exit(0);
        }

        // ---- Parent: replica A, submit-only, via PostgresAdmission wrapping
        // a stub local backend that must NEVER answer if the cross-process
        // path works -- exactly the same discriminating trick as the
        // single-process hand-off section above, just across a real process
        // boundary this time.
        auto queue_a = make_queue("admission_test_fork_parent");
        LocalStubBackend local_a{"LOCAL_FALLBACK"};
        PostgresAdmission admission_a{queue_a, inference_queue::Surface::Strategy, local_a, 8000ms};

        auto result = admission_a.submit("hello-from-another-process");

        int status = 0;
        waitpid(pid, &status, 0);

        check(result.has_value() && result->ok,
              "the parent process's submit() returned a successful InferenceOutcome");
        check(result.has_value() && result->ok &&
                  result->text == "leased:hello-from-another-process",
              "the result is the CHILD PROCESS's decode ('leased:hello-from-another-process'), "
              "NOT the parent's own LOCAL_FALLBACK stub -- proving the job crossed a real OS "
              "process boundary through Postgres, the cross-process property this whole feature "
              "exists for");
    }

    // -----------------------------------------------------------------
    section("Production wiring shape: PostgresAdmission wraps the SAME backend that also leases "
            "from Postgres (exactly assistant_service.cpp/mortgage_assistant_service.cpp's own "
            "Worker classes) -- job served via PG, fallback proven NOT to have fired");
    {
        reset_db(admin);
        auto queue = make_queue("admission_test_same_object");

        MockDecodeBackend backend;
        auto lease_source = std::make_shared<PostgresLeaseSource>(
            queue, inference_queue::Surface::Strategy, "same-object-worker");
        backend.set_lease_source(lease_source);
        backend.start();  // correct order, exactly like the real Worker constructors

        // The SAME object is both the lease source's decode target AND
        // PostgresAdmission's local fallback -- because both tag results
        // "leased:..." regardless of which internal path fed them, text
        // alone cannot tell "served by leasing from Postgres" apart from
        // "served by PostgresAdmission's fallback calling backend.submit()
        // directly" here. The row's own `lease_owner` column can: it is set
        // ONLY by Queue::lease(), never touched by the fallback path -- see
        // row_owner()'s own doc.
        PostgresAdmission admission{queue, inference_queue::Surface::Strategy, backend, 8000ms};

        const auto t0 = std::chrono::steady_clock::now();
        auto result = admission.submit("same-object-request");
        const auto elapsed = std::chrono::steady_clock::now() - t0;

        check(result.has_value() && result->ok, "submit() returned a successful outcome");
        check(elapsed < 2000ms,
              "returned in " +
                  std::to_string(
                      std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()) +
                  "ms, well under the 8000ms remote_deadline -- consistent with a healthy lease "
                  "completing normally rather than a timeout being exhausted first");

        // Job id is 1: this section's reset_db() TRUNCATEs with RESTART
        // IDENTITY and submits exactly once before this point.
        check(row_state(admin, 1) == "done", "the row reached 'done'");
        check(row_result(admin, 1).has_value() &&
                  row_result(admin, 1)->find("same-object-request") != std::string::npos,
              "the row's own result carries the decoded text");
        const auto owner = row_owner(admin, 1);
        check(owner.has_value() && *owner == "same-object-worker",
              "lease_owner is 'same-object-worker' -- set ONLY by a real Queue::lease() call. "
              "THE key assertion: this proves the request was served by leasing from Postgres, "
              "not by PostgresAdmission's fallback, which never calls lease() and never touches "
              "lease_owner at all");
    }

    // -----------------------------------------------------------------
    // Regression pair for the actual production bug this task fixes: a
    // backend whose owner thread's FIRST take_jobs() call happens before
    // set_lease_source() installs a lease source commits that thread to an
    // UNBOUNDED wait keyed only on the local queue (see take_jobs()'s own
    // doc in inference_admission.cppm) -- and nothing about installing a
    // lease source afterward ever wakes it. In production this manifested
    // as: a row appears, attempts stays 0, lease_owner stays NULL, forever.
    // These two sections reproduce the WRONG order deliberately (proving the
    // failure mode is real, not hypothetical) and then the FIXED order
    // (proving the same job is served once the order is corrected) --
    // exactly the "break it, see FAIL; fix it, see PASS" discriminating
    // pair this task's own testing brief asks for, kept as a standing
    // regression test rather than a one-off manual check.
    // -----------------------------------------------------------------
    section("Regression (broken order): start() BEFORE set_lease_source() -- the job is NEVER "
            "leased");
    {
        reset_db(admin);
        auto queue = make_queue("admission_test_startup_race_broken");

        MockDecodeBackend broken_backend;
        broken_backend.start();  // WRONG ORDER, deliberately -- see the section banner above.
        // Give the owner thread a real chance to enter its first take_jobs()
        // wait before the lease source lands -- production's own gap here
        // (model load, LISTEN pump setup) is far larger than this.
        std::this_thread::sleep_for(50ms);
        auto broken_lease_source = std::make_shared<PostgresLeaseSource>(
            queue, inference_queue::Surface::Strategy, "broken-worker");
        broken_backend.set_lease_source(broken_lease_source);

        auto submitted = queue->submit_remote(inference_queue::Surface::Strategy,
                                               R"({"prompt":"stuck"})", kFarFuture());
        check(submitted.has_value(), "job submitted to the broken backend's queue");
        const auto stuck_id = submitted->job_id;

        std::this_thread::sleep_for(1500ms);  // generous: real leasing takes milliseconds
        check(row_state(admin, stuck_id) == "pending",
              "with start() called BEFORE set_lease_source(), the job is STILL 'pending' 1.5s "
              "later, with no lease_owner -- reproducing the production bug exactly");
        check(!row_owner(admin, stuck_id).has_value(),
              "lease_owner is still NULL -- lease() was never even attempted successfully");
    }

    section("Regression (fixed order): set_lease_source() BEFORE start() -- the same job leases "
            "and completes promptly");
    {
        reset_db(admin);
        auto queue = make_queue("admission_test_startup_race_fixed");

        MockDecodeBackend fixed_backend;
        auto fixed_lease_source = std::make_shared<PostgresLeaseSource>(
            queue, inference_queue::Surface::Strategy, "fixed-worker");
        fixed_backend.set_lease_source(fixed_lease_source);
        fixed_backend.start();  // CORRECT ORDER -- the fix this task makes.

        auto submitted = queue->submit_remote(inference_queue::Surface::Strategy,
                                               R"({"prompt":"unstuck"})", kFarFuture());
        check(submitted.has_value(), "job submitted to the fixed backend's queue");
        const auto unstuck_id = submitted->job_id;

        bool done = false;
        for (int attempt = 0; attempt < 50 && !done; ++attempt) {
            std::this_thread::sleep_for(50ms);
            done = (row_state(admin, unstuck_id) == "done");
        }
        check(done, "with set_lease_source() called BEFORE start(), the SAME shape of job reaches "
                    "'done' well within 2.5s -- the owner thread's first take_jobs() call already "
                    "saw the lease source");
        check(row_result(admin, unstuck_id).has_value() &&
                  row_result(admin, unstuck_id)->find("leased:unstuck") != std::string::npos,
              "the result is the leased decode ('leased:unstuck'), proving the PG path served it");
        check(row_owner(admin, unstuck_id).has_value() &&
                  *row_owner(admin, unstuck_id) == "fixed-worker",
              "lease_owner is 'fixed-worker' -- a real lease() call served this job");
    }

    // -----------------------------------------------------------------
    section("Postgres unreachable -- PostgresAdmission::submit() falls back to local, quickly");
    {
        reset_db(admin);

        // Port 1 is a privileged port nothing in this environment listens
        // on; a connect attempt to it fails fast (connection refused) rather
        // than timing out the full connect_timeout, which is exactly the
        // "bounded, never hangs" property this section checks for.
        pg::PoolConfig bad_config;
        bad_config.conninfo = "host=127.0.0.1 port=1 dbname=nope user=postgres";
        bad_config.size = 2;
        auto bad_pool = std::make_shared<pg::Pool>(std::move(bad_config));
        auto bad_queue = std::make_shared<inference_queue::Queue>(bad_pool);

        LocalStubBackend local_fallback{"LOCAL_FALLBACK"};
        PostgresAdmission admission{bad_queue, inference_queue::Surface::Strategy, local_fallback,
                                     3000ms};

        const auto t0 = std::chrono::steady_clock::now();
        auto result = admission.submit("fallback-test");
        const auto elapsed = std::chrono::steady_clock::now() - t0;

        check(result.has_value() && result->ok && result->text == "LOCAL_FALLBACK:fallback-test",
              "with Postgres unreachable, submit() returns the LOCAL backend's own answer, not "
              "an error and not a hang");
        check(elapsed < 3000ms,
              "the fallback happened well inside the 3000ms remote_deadline (took " +
                  std::to_string(
                      std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()) +
                  "ms) -- bounded by pg::PoolConfig's own connect_timeout/acquire_timeout, never "
                  "an unbounded hang");
    }

    // -----------------------------------------------------------------
    section("Fenced completion is discarded even when the REAL write-back went through "
            "PostgresLeaseSource's own helper thread");
    {
        reset_db(admin);
        auto queue = make_queue("admission_test_fencing");

        auto submitted =
            queue->submit_remote(inference_queue::Surface::Mortgage, R"({"prompt":"fence-test"})",
                                  kFarFuture());
        check(submitted.has_value(), "job submitted");
        const auto job_id = submitted->job_id;

        // A "stale worker" leases the job directly against the raw Queue API
        // -- simulating some OTHER process that grabbed it and then, from
        // the queue's point of view, vanished (crashed, or its lease simply
        // expired without a heartbeat).
        auto stale_lease = queue->lease(inference_queue::Surface::Mortgage, "stale-worker");
        check(stale_lease.has_value() && stale_lease->has_value(), "the stale worker leases it");
        const auto stale_token = (*stale_lease)->fencing_token.value_or(-1);

        force_expire_lease(admin, job_id);
        auto swept = queue->sweep_once();
        check(swept.has_value() && swept->ran && swept->requeued == 1,
              "sweep_once() requeues the abandoned lease back to pending");
        // sweep_once() resets state/lease_owner/lease_deadline but -- by
        // design, see inference_queue.cpp's own SQL -- does NOT clear
        // fencing_token, so the row still carries the stale worker's token
        // until something leases it again. Confirmed here so the section
        // below is provably racing a REAL re-lease, not a no-op.
        check(row_state(admin, job_id) == "pending", "the row is back to 'pending' after the sweep");

        // NOW the real admission layer takes over: a MockDecodeBackend with a
        // PostgresLeaseSource installed leases the SAME job fresh (a NEW
        // fencing token), and writes the result back through
        // PostgresLeaseSource::spawn_writeback -- the actual new code path,
        // not a direct Queue::complete() call. A deliberate decode delay
        // (800ms) keeps the row genuinely `leased` (fresh token, not yet
        // completed) for long enough that the stale-write attempt below is
        // racing a LIVE lease, not an already-terminal row -- without this,
        // the row can reach 'done' before the stale attempt ever runs, and
        // the ORDINARY `state = 'leased'` guard (not the fencing_token
        // comparison specifically) would be all that blocks it, which would
        // not actually exercise fencing at all.
        MockDecodeBackend backend{/*max_concurrent=*/2, /*decode_delay=*/800ms};
        auto lease_source = std::make_shared<PostgresLeaseSource>(
            queue, inference_queue::Surface::Mortgage, "worker-recovers");
        backend.set_lease_source(lease_source);
        backend.start();  // AFTER set_lease_source() -- see the class banner.

        // Wait for the fresh lease to land (state -> 'leased' again, with a
        // DIFFERENT token than the stale worker's), but not yet for
        // completion -- the decode_delay above guarantees a window.
        bool released = false;
        for (int attempt = 0; attempt < 50 && !released; ++attempt) {
            std::this_thread::sleep_for(20ms);
            released = (row_state(admin, job_id) == "leased");
        }
        check(released, "the admission layer's lease source re-leases the job (state -> "
                         "'leased' again) within 1s");

        // THE discriminating check, now genuinely racing a LIVE lease: the
        // original stale worker, unaware its lease was reclaimed, tries to
        // report success with its OLD token WHILE the row is still 'leased'
        // under the fresh token -- directly against the raw Queue API,
        // exactly as a real stale worker process would, and BEFORE the real
        // admission-layer completion has had a chance to land.
        check(row_state(admin, job_id) == "leased",
              "sanity: the row is still 'leased' (not yet completed) at the moment of the stale "
              "attempt -- the race this section exists to test");
        auto stale_complete =
            queue->complete(job_id, stale_token, R"({"text":"from-stale-worker"})");
        check(stale_complete.has_value() && stale_complete.value() == false,
              "the stale worker's fenced complete() with the OLD token, issued while the row is "
              "STILL 'leased' under a fresher token, updates ZERO rows and reports false -- this "
              "is the fencing_token comparison specifically, not merely the state guard");
        check(row_state(admin, job_id) == "leased",
              "the row is still 'leased' immediately after the stale attempt -- it did not land");

        bool completed = false;
        for (int attempt = 0; attempt < 50 && !completed; ++attempt) {
            std::this_thread::sleep_for(100ms);
            completed = (row_state(admin, job_id) == "done");
        }
        check(completed,
              "the admission layer's OWN helper thread (PostgresLeaseSource::spawn_writeback) "
              "still writes back its real completion afterward");
        const auto real_result = row_result(admin, job_id);
        check(real_result.has_value() && real_result->find("leased:fence-test") != std::string::npos &&
                  real_result->find("from-stale-worker") == std::string::npos,
              "the stored result is the admission layer's own decode ('leased:fence-test'), "
              "never the stale worker's payload -- no double-execution occurred");
    }

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
