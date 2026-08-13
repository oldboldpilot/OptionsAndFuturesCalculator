// @author Olumuyiwa Oluwasanmi
//
// Tests inference_admission.cppm's LOCAL admission queue -- QueuedBackend's
// submit()/take_jobs()/drain_and_fail() -- against a trivial concrete
// subclass standing in for SensenBackend/LlamaCppBackend's real decode loop.
// Needs NO Postgres: this suite IS the "lease_source_ stays null, `local`
// mode's behaviour is unaffected by the postgres extension" proof, so it is
// registered via add_test() and runs on every `ctest` invocation.
//
// Plain hand-rolled check()/section() harness, matching every other test in
// this tree -- not gtest (config/cpp_details.txt rule 39 forbids external
// test frameworks project-wide).
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

import inference_admission;
import sgee_queue_client;

using namespace std::chrono_literals;
using options_calculator::inference_admission::Device;
using options_calculator::inference_admission::InferenceOutcome;
using options_calculator::inference_admission::QueuedBackend;
using options_calculator::inference_admission::resolve_device;

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

/**
 * A trivial concrete QueuedBackend: one owner thread that calls take_jobs()
 * exactly the way SensenBackend::run() does (block on empty, drain up to
 * max_concurrent_), and for each job taken, blocks on a test-controlled gate
 * before echoing the prompt back as the result. The gate is what lets a test
 * deterministically hold one job "in flight" (already dequeued from the
 * local waiting room) while it fills the waiting room itself to
 * max_queue_depth_ and checks the refusal boundary precisely.
 */
class GatedEchoBackend final : public QueuedBackend {
  public:
    GatedEchoBackend(std::size_t max_concurrent, std::size_t queue_depth,
                      std::string_view shutting_down_message = "assistant backend is shutting down")
        : QueuedBackend(shutting_down_message), shutting_down_message_(shutting_down_message) {
        max_concurrent_ = max_concurrent;
        max_queue_depth_ = queue_depth;
        start();
    }

    // This test never installs a lease source (it is the pure-local-mode
    // suite -- see this file's own banner), so starting the owner thread
    // from the constructor is safe: there is no set_lease_source() call this
    // could ever race. See QueuedBackend::start()'s own doc for why
    // production code must NOT do this.
    auto start() -> void override { worker_ = std::jthread([this](std::stop_token st) { run(st); }); }

    ~GatedEchoBackend() override {
        worker_.request_stop();
        open_gate();  // unstick anything blocked on the gate so join() below completes
        worker_.join();
        // The SAME message this instance was constructed with -- drain_and_fail()'s
        // reason is a free parameter, not automatically tied to the base's own
        // shutting_down_message_ (used only by submit()'s post-shutdown path), so
        // this test deliberately passes the identical string to prove both call
        // sites agree, exactly as SensenBackend's real shutdown path does (see
        // assistant_service.cpp / mortgage_assistant_service.cpp's own
        // drain_and_fail(...) call sites).
        drain_and_fail(shutting_down_message_);
    }

    [[nodiscard]] auto name() const noexcept -> std::string_view override { return "gated-echo"; }

    auto open_gate() -> void {
        const std::lock_guard lock{gate_mu_};
        gate_open_ = true;
        gate_cv_.notify_all();
    }
    auto close_gate() -> void {
        const std::lock_guard lock{gate_mu_};
        gate_open_ = false;
    }

  private:
    auto run(std::stop_token st) -> void {
        while (!st.stop_requested()) {
            auto jobs = take_jobs(st, max_concurrent_, /*block=*/true);
            for (auto& job : jobs) {
                wait_for_gate(st);
                if (st.stop_requested()) {
                    job.promise.set_value(
                        InferenceOutcome{.ok = false, .text = {}, .error = "test worker stopping"});
                    continue;
                }
                job.promise.set_value(InferenceOutcome{.ok = true, .text = job.prompt, .error = {}});
            }
        }
    }

    auto wait_for_gate(std::stop_token st) -> void {
        std::unique_lock lock{gate_mu_};
        static_cast<void>(gate_cv_.wait(lock, st, [this] { return gate_open_; }));
    }

    std::mutex gate_mu_;
    std::condition_variable_any gate_cv_;
    bool gate_open_ = true;
    std::string shutting_down_message_;
    std::jthread worker_;
};

}  // namespace

auto main() -> int {
    // -----------------------------------------------------------------
    section("Ordering and the exact RESOURCE_EXHAUSTED-triggering nullopt condition");
    {
        // max_concurrent_=1 so exactly one job is "in flight" (already
        // dequeued, held on the gate) at a time; max_queue_depth_=2 so the
        // waiting room holds exactly two more before refusing.
        GatedEchoBackend backend{/*max_concurrent=*/1, /*queue_depth=*/2};
        backend.close_gate();

        std::optional<InferenceOutcome> result_a, result_b, result_c;
        std::thread thread_a([&] { result_a = backend.submit("A"); });
        // Give the worker thread time to dequeue "A" (queue_ becomes empty,
        // the job moves to "in flight, blocked on the gate").
        std::this_thread::sleep_for(150ms);

        std::thread thread_b([&] { result_b = backend.submit("B"); });
        std::this_thread::sleep_for(50ms);
        std::thread thread_c([&] { result_c = backend.submit("C"); });
        std::this_thread::sleep_for(50ms);

        // The waiting room now holds exactly {B, C} -- size 2 == max_queue_depth_.
        auto result_d = backend.submit("D");
        check(!result_d.has_value(),
              "submit() at the queue's exact capacity boundary returns nullopt (the ONLY "
              "condition the RPC layer maps to RESOURCE_EXHAUSTED)");

        backend.open_gate();
        thread_a.join();
        thread_b.join();
        thread_c.join();

        check(result_a.has_value() && result_a->ok && result_a->text == "A",
              "the in-flight job (A) completed successfully once the gate opened");
        check(result_b.has_value() && result_b->ok && result_b->text == "B",
              "the first queued job (B) was served after A, in submission order");
        check(result_c.has_value() && result_c->ok && result_c->text == "C",
              "the second queued job (C) was served after B, in submission order");
    }

    // -----------------------------------------------------------------
    section("A queue with free capacity never refuses");
    {
        GatedEchoBackend backend{/*max_concurrent=*/2, /*queue_depth=*/4};
        auto result = backend.submit("hello");
        check(result.has_value() && result->ok && result->text == "hello",
              "submit() with free capacity returns a real InferenceOutcome, never nullopt");
    }

    // -----------------------------------------------------------------
    section("Per-surface shutting-down wording is preserved through the shared base");
    {
        auto run_one = [](std::string_view message) {
            auto backend = std::make_unique<GatedEchoBackend>(/*max_concurrent=*/1,
                                                                /*queue_depth=*/1, message);
            backend->close_gate();

            // One job in flight (dequeued, blocked on the gate)...
            std::optional<InferenceOutcome> held;
            std::thread hold([&] { held = backend->submit("held"); });
            std::this_thread::sleep_for(150ms);

            // ...and one still sitting in the local waiting room.
            std::optional<InferenceOutcome> queued;
            std::thread enqueue([&] { queued = backend->submit("queued"); });
            std::this_thread::sleep_for(100ms);

            // Destroying the backend NOW (via reset(), synchronously) runs
            // request_stop() + open_gate() + worker_.join() + drain_and_fail()
            // -- exactly the shutdown path a real process exit takes -- and
            // returns only once every promise below is fulfilled, so it is
            // now safe to join the two helper threads.
            backend.reset();
            hold.join();
            enqueue.join();

            return std::pair{held, queued};
        };

        const auto [held_default, queued_default] = run_one("assistant backend is shutting down");
        check(held_default.has_value() && !held_default->ok &&
                  held_default->error == "test worker stopping",
              "the in-flight job is failed with the worker-stopping message on shutdown");
        check(queued_default.has_value() && !queued_default->ok &&
                  queued_default->error == "assistant backend is shutting down",
              "the still-queued job is failed by drain_and_fail() with the DEFAULT "
              "shutting-down message (matches assistant_service.cpp's original wording)");

        const auto [held_custom, queued_custom] =
            run_one("mortgage assistant backend is shutting down");
        check(held_custom.has_value() && !held_custom->ok &&
                  held_custom->error == "test worker stopping",
              "(custom-message instance) the in-flight job still fails with the same "
              "worker-stopping message -- that text is not parameterized");
        check(queued_custom.has_value() && !queued_custom->ok &&
                  queued_custom->error == "mortgage assistant backend is shutting down",
              "the still-queued job is failed with the CUSTOM shutting-down message (matches "
              "mortgage_assistant_service.cpp's original wording) -- proving the two surfaces' "
              "wording did not homogenize when the class was unified");
    }

    // -----------------------------------------------------------------
    // resolve_device() -- the ASSISTANT_DEVICE/MORTGAGE_DEVICE selector's
    // decision logic, factored into inference_admission.cppm as a pure
    // function precisely so every branch is testable here without an actual
    // CUDA build or a real device: cuda_build/cuda_device_ready are passed in
    // as plain booleans rather than queried from #ifdef SENSEN_HAS_CUDA or
    // sensen::cuda::CudaBackend::is_available() (which assistant_service.cpp
    // and mortgage_assistant_service.cpp do at their own real call sites).
    section("resolve_device() -- ASSISTANT_DEVICE/MORTGAGE_DEVICE selector");
    {
        // Default: no env var set, both callers pass "cpu" (env_string(...)
        // .value_or("cpu")) -- byte-identical to every build before this
        // selector existed, on a build/process combination that could not be
        // further from CUDA (neither compiled in nor ready).
        {
            const auto r = resolve_device("cpu", /*cuda_build=*/false, /*cuda_device_ready=*/false);
            check(r.device == Device::Cpu && !r.refuse,
                  "default (\"cpu\", no CUDA anywhere) resolves to {Cpu, refuse=false}");
        }

        // Explicit cpu, even on a hypothetical build that DOES have CUDA
        // compiled in and ready -- requesting cpu always wins; this proves
        // resolve_device() never "helpfully" upgrades an explicit cpu
        // request just because the machine happens to be capable.
        {
            const auto r = resolve_device("cpu", /*cuda_build=*/true, /*cuda_device_ready=*/true);
            check(r.device == Device::Cpu && !r.refuse,
                  "explicit \"cpu\" resolves to {Cpu, refuse=false} even on a CUDA-capable "
                  "build/process -- cpu always wins when it is what was asked for");
        }

        // An unrecognised value is treated exactly like "cpu", per this
        // task's own brief ("cpu (or anything unrecognised) -> byte-identical
        // to today") -- deliberately NOT refused, unlike ASSISTANT_BACKEND's
        // own unrecognised-value branch.
        {
            const auto r =
                resolve_device("tpu", /*cuda_build=*/false, /*cuda_device_ready=*/false);
            check(r.device == Device::Cpu && !r.refuse,
                  "an unrecognised device string (\"tpu\") resolves to {Cpu, refuse=false}, not "
                  "a refusal -- this axis's contract differs from ASSISTANT_BACKEND's on purpose");
        }

        // THE branch this build can actually exercise end-to-end today: cuda
        // requested, this binary was NOT built with CUDA support. Must
        // refuse -- the exact ENABLE_LLAMACPP_BACKEND=OFF precedent, never a
        // silent substitution.
        {
            const auto r =
                resolve_device("cuda", /*cuda_build=*/false, /*cuda_device_ready=*/false);
            check(r.refuse,
                  "\"cuda\" on a build without CUDA support refuses (the loud "
                  "build-configuration error), regardless of cuda_device_ready");
        }
        {
            // cuda_device_ready=true here is deliberately incoherent (a real
            // build never reports a ready device on a build that lacks CUDA
            // support at all) -- included to prove refuse is keyed on
            // cuda_build alone, not accidentally on cuda_device_ready too.
            const auto r =
                resolve_device("cuda", /*cuda_build=*/false, /*cuda_device_ready=*/true);
            check(r.refuse,
                  "\"cuda\" on a build without CUDA support still refuses even if "
                  "cuda_device_ready is (incoherently) true -- cuda_build is the only gate");
        }

        // The two branches this box cannot reach through a real CUDA build,
        // exercised here as pure decision logic (no faking of hardware or
        // compiler flags -- see this section's own banner): a CUDA build
        // with no ready device degrades loudly to CPU rather than refusing,
        // and a CUDA build with a ready device is honoured.
        {
            const auto r = resolve_device("cuda", /*cuda_build=*/true, /*cuda_device_ready=*/false);
            check(r.device == Device::Cpu && !r.refuse,
                  "\"cuda\" on a CUDA build with no ready device degrades to {Cpu, "
                  "refuse=false} -- a driver hiccup is not the build-configuration mistake "
                  "the branch above guards against, so it does not refuse");
        }
        {
            const auto r = resolve_device("cuda", /*cuda_build=*/true, /*cuda_device_ready=*/true);
            check(r.device == Device::Cuda && !r.refuse,
                  "\"cuda\" on a CUDA build with a ready device resolves to {Cuda, "
                  "refuse=false}");
        }
    }

    section("SgeeAdmission degrades to the local backend when the cluster is unreachable");
    {
        // The degrade-never-hang property, which is the ONE thing about this
        // path that must hold whether or not a cluster exists. It is asserted
        // against a client that has no peers at all -- the honest stand-in for
        // "the cluster is unreachable", and the only failure mode reproducible
        // without standing three nodes up.
        //
        // Two distinct claims, and the second is the one that bites: that the
        // caller gets the LOCAL answer (not an error, not nullopt), and that it
        // gets it PROMPTLY. A path that eventually degrades after holding a gRPC
        // handler thread for ninety seconds has satisfied the letter of
        // "degrade" and none of the point.
        GatedEchoBackend local{/*max_concurrent=*/1, /*queue_depth=*/4};
        local.open_gate();

        options_calculator::inference_admission::SgeeAdmission admission(
            SgeeQueueClient{}, local, std::chrono::milliseconds(90000));

        const auto started = std::chrono::steady_clock::now();
        const auto out = admission.submit("hello");
        const auto elapsed = std::chrono::steady_clock::now() - started;

        check(out.has_value(), "an unreachable SGEE cluster still yields an answer");
        check(out.has_value() && out->ok, "and that answer is the local backend's success");
        check(out.has_value() && out->text == "hello",
              "and it is the LOCAL backend's own text, not a fabricated one");
        check(elapsed < std::chrono::seconds(5),
              "and it arrives promptly rather than after the 90s remote deadline -- a submit "
              "that cannot even be sent must not spend the caller's whole budget discovering it");

        check(std::string_view(admission.name()) == "sgee",
              "SgeeAdmission reports its own name, so a log line naming the admission path is "
              "not silently the postgres one");
        check(std::string_view(admission.device()) == std::string_view(local.device()),
              "and it forwards device() to the wrapped backend rather than hardcoding \"cpu\" -- "
              "a leased job still decodes wherever that backend runs");
    }

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
