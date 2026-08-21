export module inference_admission;
import std;

import inference_queue;
import sgee_queue_client;

/**
 * @author Olumuyiwa Oluwasanmi
 *
 * The admission-queue/backpressure layer shared by the strategy assistant
 * (assistant_service.cpp) and the mortgage assistant (mortgage_assistant_
 * service.cpp), extracted out of both files' near-identical anonymous
 * namespaces into one module. `InferenceOutcome`, `PendingJob`,
 * `InferenceBackend` and `QueuedBackend` below are the SAME classes that used
 * to live twice -- once per file -- with the SAME names and the SAME
 * behaviour; only their address changed. `SensenBackend`/`LlamaCppBackend`
 * stay in their own service files: they are surface-specific (different model
 * path env vars, different system prompts, different grammars) and were never
 * duplicated in the first place.
 *
 * Also home to the Postgres-backed extension of this layer
 * (`PostgresLeaseSource` / `PostgresAdmission`), selected by
 * `INFERENCE_QUEUE=postgres` in each service's Worker class. This extension
 * is ADDITIVE and OFF by default:
 *
 *   - `QueuedBackend::set_lease_source()` defaults to installing nothing
 *     (`lease_source_` stays null for the whole process lifetime unless a
 *     Worker explicitly calls it), and `take_jobs()`'s local (`local`-mode)
 *     branch is untouched code -- same statements, same lock, same unbounded
 *     `cv_.wait(lock, stoken, pred)` -- when `lease_source_` is null. See
 *     `take_jobs()`'s own doc for exactly what changes when it is not.
 *   - `PostgresAdmission` wraps a Worker's real local `InferenceBackend` (the
 *     `SensenBackend`/`LlamaCppBackend` already doing decode) and falls back
 *     to calling it directly -- today's exact in-process path -- on ANY
 *     failure of the Postgres path, at either the submit step or the await
 *     step. A Postgres outage therefore degrades this service to `local`
 *     behaviour, bounded by `pg::PoolConfig`'s `connect_timeout`/
 *     `statement_timeout`, rather than hanging or erroring the RPC.
 *
 * WHY THIS MODULE IMPORTS `inference_queue` DIRECTLY (and is therefore only
 * safe to import from assistant_service.cpp / mortgage_assistant_service.cpp,
 * never from finance_service.cpp or calculator_service.cpp): the Postgres
 * extension's types are expressed directly in terms of
 * `inference_queue::Queue`/`Surface`/`Job` rather than behind a further
 * abstraction, because there is exactly one production consumer of that
 * queue (this admission layer) and a third layer of indirection would buy
 * nothing. The Finance/Calculator isolation this repo's CLAUDE.md and this
 * task's own brief require is enforced by NOT importing this module (or
 * `pg`/`inference_queue`) from those two services at all -- see this
 * project's CMakeLists.txt comment above `find_package(PostgreSQL REQUIRED)`
 * for the build-level half of that same claim.
 */
namespace options_calculator::inference_admission {

// ---------------------------------------------------------------------------
// Device selection (ASSISTANT_DEVICE / MORTGAGE_DEVICE)
// ---------------------------------------------------------------------------

/**
 * Which physical device a backend's decode loop actually runs on.
 *
 * There is no third value. Hybrid decode is unimplemented in sensen and
 * hybrid prefill is broken for its Q8_0 kernel, so a device is either `Cpu`
 * or fully `Cuda` -- never a partial layer split. Everywhere this codebase
 * would otherwise be tempted to offer a fraction, it must not.
 */
export enum class Device : std::uint8_t { Cpu, Cuda };

/** `device`'s wire/log spelling -- "cpu" or "cuda", the same two strings
 *  `ASSISTANT_DEVICE`/`MORTGAGE_DEVICE` accept. Centralised so a ready-line
 *  log and a `device()` override never drift into spelling it differently. */
export [[nodiscard]] constexpr auto device_name(Device device) noexcept -> std::string_view {
    return device == Device::Cuda ? "cuda" : "cpu";
}

/**
 * The result of resolving one surface's `*_DEVICE` env var against what this
 * build and this process can actually do. `device` is only meaningful when
 * `refuse` is false -- a refusal means the caller must return without
 * constructing any backend at all, the same shape as the pre-existing
 * `ASSISTANT_BACKEND=llamacpp`-without-`ENABLE_LLAMACPP_BACKEND` refusal.
 */
export struct DeviceResolution {
    Device device = Device::Cpu;
    bool refuse = false;
};

/**
 * Pure decision function behind `ASSISTANT_DEVICE`/`MORTGAGE_DEVICE`, factored
 * out of both Worker constructors so it is unit-testable without an actual
 * CUDA build or a real device: `cuda_build`/`cuda_device_ready` are the two
 * facts a caller has already established -- both at RUNTIME, via one
 * `sensen::cuda::CudaBackend::query()` call whose failure message tells the
 * two apart ("CUDA not compiled (ENABLE_CUDA=OFF)" vs a device-probe
 * failure); NOT via a compile-time `#ifdef SENSEN_HAS_CUDA` in the caller's
 * own file, which cannot see that macro at all -- backend/CMakeLists.txt
 * defines it PRIVATE to the sensen_slim target, and C++20/23 modules do not
 * leak preprocessor macros across the import boundary. See
 * AssistantWorker's own call site (assistant_service.cpp) for the full
 * story. Both facts are passed in rather than queried here so this function
 * has no build-configuration or sensen-module dependency of its own and
 * every branch is reachable from a plain unit test.
 *
 * Four cases, matching this task's own brief exactly:
 *
 *   - `requested != "cuda"` (unset, "cpu", or anything unrecognised) ->
 *     `{Cpu, false}`, byte-identical to every build before this selector
 *     existed. An unrecognised string is intentionally treated as "cpu", NOT
 *     refused -- unlike `ASSISTANT_BACKEND`'s own unrecognised-value branch --
 *     because the brief for this selector specifically calls out "cpu (or
 *     anything unrecognised) -> byte-identical to today" as the contract.
 *   - `requested == "cuda"` and `!cuda_build` -> `{_, true}` (refuse). Never
 *     silently substitute an engine, or here a device, the gates do not
 *     cover -- the exact `ENABLE_LLAMACPP_BACKEND=OFF` precedent
 *     (assistant_service.cpp's own three-branch ASSISTANT_BACKEND selector).
 *   - `requested == "cuda"`, `cuda_build`, `!cuda_device_ready` -> `{Cpu,
 *     false}`. Loud degrade, not a refusal: a missing or broken device at
 *     runtime is not the build-configuration mistake the branch above is.
 *     The caller is expected to log this at error level itself (this
 *     function stays silent -- see its own "no logger dependency" note above)
 *     precisely so the ready line can still report `device=cpu`, ground
 *     truth, not the abandoned request.
 *   - `requested == "cuda"`, `cuda_build`, `cuda_device_ready` -> `{Cuda,
 *     false}`.
 */
export [[nodiscard]] constexpr auto resolve_device(std::string_view requested, bool cuda_build,
                                                    bool cuda_device_ready) noexcept
    -> DeviceResolution {
    if (requested != "cuda") {
        return DeviceResolution{.device = Device::Cpu, .refuse = false};
    }
    if (!cuda_build) {
        return DeviceResolution{.device = Device::Cpu, .refuse = true};
    }
    if (!cuda_device_ready) {
        return DeviceResolution{.device = Device::Cpu, .refuse = false};
    }
    return DeviceResolution{.device = Device::Cuda, .refuse = false};
}

/**
 * What a backend hands back for one generation request.
 *
 * The three timing fields exist because, before they did, nobody could tell
 * where a slow request's time actually went -- "the model" was the only
 * available answer, and it was often wrong (see the investigation that added
 * these: production's ~5s ParseOperation calls turned out to be ordinary
 * single-stream decode throughput on the deployed CPU, not queueing,
 * contention, or grammar overhead, and the only way to tell was to measure
 * prefill and decode separately and log it on every real request). Populated
 * on a best-effort basis -- `0.0`/`0` on any path that predates or bypasses
 * the timed admit()/finish() sequence (e.g. an early admission failure) --
 * so a caller must not treat a zero as "instant", only as "not measured".
 */
export struct InferenceOutcome {
    bool ok = false;
    std::string text;   // the model's raw decoded output, valid iff ok
    std::string error;  // human-readable failure detail, valid iff !ok

    // Wall-clock milliseconds from admission (prompt handed to the engine)
    // to the first sampled token -- tokenisation + prefill + first sample.
    double prefill_ms = 0.0;
    // Wall-clock milliseconds from the first token to the last, across every
    // batched decode_step() this sequence rode in (so it includes whatever
    // time other sequences in the same batch cost this one, which is the
    // real number a caller waited, not an isolated-model number).
    double decode_ms = 0.0;
    std::size_t tokens_generated = 0;
};

/**
 * One accepted request, in flight from the gRPC handler thread that accepted
 * it to the backend owner thread that will serve it (in `local` mode), or
 * from `PostgresLeaseSource::fill()` to that same owner thread's decode loop
 * (in `postgres` mode, for a job leased from the shared queue rather than
 * submitted locally).
 *
 * The promise is fulfilled exactly once, on every path out of the owner
 * thread including shutdown -- a promise destroyed unfulfilled would make
 * the waiting side's `future.get()` throw `std::future_error` instead of
 * returning, which is a strictly worse failure than an honest error string.
 */
export struct PendingJob {
    std::string prompt;
    std::promise<InferenceOutcome> promise;
};

/**
 * The narrow contract the RPC layer depends on, and the ONLY thing it knows
 * about inference. Both `QueuedBackend` (local decode) and `PostgresAdmission`
 * (the Postgres-backed submission wrapper) implement it, so a Worker class can
 * hold either behind one `InferenceBackend*`/`InferenceBackend&` without the
 * RPC handler ever knowing which.
 */
export class InferenceBackend {
  public:
    InferenceBackend() = default;
    virtual ~InferenceBackend() = default;
    InferenceBackend(const InferenceBackend&) = delete;
    auto operator=(const InferenceBackend&) -> InferenceBackend& = delete;
    InferenceBackend(InferenceBackend&&) = delete;
    auto operator=(InferenceBackend&&) -> InferenceBackend& = delete;

    /**
     * Submits one prompt and blocks the CALLING thread until the backend has
     * produced a result.
     *
     * Returns `std::nullopt` iff the admission queue was already full when
     * this call arrived -- the caller must map that, and only that, to
     * RESOURCE_EXHAUSTED. Any other outcome, including a failed generation,
     * comes back as an `InferenceOutcome` with `ok == false`.
     */
    [[nodiscard]] virtual auto submit(std::string prompt) -> std::optional<InferenceOutcome> = 0;

    /** Identifier for logs and for the startup banner. Never user-facing. */
    [[nodiscard]] virtual auto name() const noexcept -> std::string_view = 0;

    /**
     * Which device is ACTUALLY serving requests through this backend right
     * now -- "cpu" or "cuda" (see `device_name`), never a partial value.
     * Distinct from what a deployment REQUESTED via `ASSISTANT_DEVICE`/
     * `MORTGAGE_DEVICE`: a backend that degraded from a `cuda` request to
     * `cpu` at runtime (see `resolve_device`) reports "cpu" here, so a caller
     * logging this states ground truth, not the abandoned request.
     *
     * Defaults to "cpu" so every backend that never offloads anything --
     * `LlamaCppBackend` (this task does not extend GPU offload to it; see
     * this project's CLAUDE.md on llama.cpp being a cross-checking tool, not
     * a serving alternative) and every `QueuedBackend` test double in this
     * tree -- needs no override at all.
     */
    [[nodiscard]] virtual auto device() const noexcept -> std::string_view { return "cpu"; }
};

/**
 * Where a `QueuedBackend`'s owner thread draws SHARED work from, in addition to
 * its own local queue.
 *
 * This is an abstract base rather than a concrete class because there are now
 * two shared queues -- Postgres (`PostgresLeaseSource`) and the SGEE cluster
 * (`SgeeLeaseSource`) -- and `QueuedBackend` must be able to hold either without
 * knowing which. It previously named `PostgresLeaseSource` directly, which made
 * the substrate part of the decode loop's type rather than a deployment choice.
 *
 * The interface is deliberately one method. Everything else a shared queue needs
 * -- leader discovery, fencing tokens, retries, writing a result back -- happens
 * on the far side of `fill()` and is none of the decode loop's business. In
 * particular `fill()` MUST NOT block on a decode result: it is called ON the
 * owner thread, whose job is to keep feeding the fused decode step. Both
 * implementations spawn a short-lived helper thread to do the waiting.
 */
export class LeaseSource {
  public:
    LeaseSource() = default;
    virtual ~LeaseSource() = default;
    LeaseSource(const LeaseSource&) = delete;
    auto operator=(const LeaseSource&) -> LeaseSource& = delete;
    LeaseSource(LeaseSource&&) = delete;
    auto operator=(LeaseSource&&) -> LeaseSource& = delete;

    /**
     * Leases up to `want` jobs. Fewer, or none, is the ORDINARY case -- an
     * empty result means the shared queue had nothing eligible, never that
     * something failed. A genuine failure is also reported as an empty result,
     * after logging: the owner thread's correct response to both is identical
     * (serve local work and try again next tick), and giving it an error to
     * handle would only invite it to handle them differently.
     */
    [[nodiscard]] virtual auto fill(std::size_t want) -> std::vector<PendingJob> = 0;
};

/**
 * The admission queue and back-pressure policy both local backends share.
 *
 * WHY A BOUNDED QUEUE AT ALL:
 *
 * Every accepted request holds a gRPC handler thread blocked on `submit()`
 * for the duration of one extraction. An unbounded queue would let load pile
 * up invisibly -- the caller-facing symptom would be requests taking longer
 * and longer to answer, with no signal distinguishing "briefly busy" from
 * "about to wait minutes", and no way for a client to make an informed retry
 * decision. A bounded queue makes the choice explicit instead: once it is
 * full, a request fails FAST with RESOURCE_EXHAUSTED rather than joining an
 * invisible line.
 *
 * The queue lives on the calling gRPC threads' side: `submit()` never blocks
 * to make room. If the queue is already full when a new request arrives it is
 * turned away immediately, full stop.
 *
 * CONTINUOUS BATCHING: a single owner thread holds a set of in-flight
 * sequences and advances all of them by one token per iteration in one fused
 * forward pass, admitting newly-queued requests into free slots as earlier
 * ones finish. The queue below is therefore the WAITING room in front of that
 * set, not the whole capacity.
 *
 * THE OPTIONAL POSTGRES LEASE SOURCE:
 *
 * `set_lease_source()` installs an optional `PostgresLeaseSource` that
 * `take_jobs()` consults AFTER draining the local queue, to fill any
 * remaining free decode slots by leasing jobs from the shared Postgres queue
 * -- "lease late, never early": a local submission always wins a free slot
 * over a leased one, since local jobs are drained first and leasing only
 * tops up whatever slots are still empty. See `take_jobs()`'s own doc for
 * the wait-loop change this requires, and for the guarantee that `local`
 * mode (`lease_source_ == nullptr`) is unaffected.
 */
export class QueuedBackend : public InferenceBackend {
  public:
    // submit()/drain_and_fail() are defined INLINE, in-class, here. take_jobs()
    // is declared here but DEFINED further down THIS SAME FILE (still the
    // module INTERFACE unit, inference_admission.cppm -- never
    // inference_admission.cpp), after PostgresLeaseSource's own definition,
    // for two independent reasons:
    //
    //   1. Its body calls `lease_source->fill(...)`, which needs
    //      PostgresLeaseSource to be a COMPLETE type -- impossible from
    //      inside QueuedBackend's own class body, which is compiled in
    //      QueuedBackend's complete-class context and therefore BEFORE
    //      PostgresLeaseSource (forward-declared just above, defined later)
    //      is complete.
    //   2. Its body also calls
    //      `std::condition_variable_any::wait(lock, stop_token, pred)`, and
    //      that call failed to LINK ("undefined reference to
    //      std::__1::__atomic_unique_lock<...>::__set_locked_bit") when
    //      defined out-of-line in inference_admission.cpp (a module
    //      IMPLEMENTATION unit) -- but links (unreliably; see
    //      `force_take_jobs_symbol_emission`'s own doc below for why it is
    //      not simply "fine") defined out-of-line here, in the INTERFACE
    //      unit itself, which is the same reason PostgresLeaseSource/
    //      PostgresAdmission -- defined out-of-line in the .cpp without
    //      issue, since neither touches condition_variable_any -- were left
    //      there rather than moved here too. This is a genuine, diagnosed
    //      upstream Clang bug, not a mystery local to this file -- see
    //      `force_take_jobs_symbol_emission` for the diagnosis, the upstream
    //      report, and what would let this comment (and that function) be
    //      deleted.
    [[nodiscard]] auto submit(std::string prompt) -> std::optional<InferenceOutcome> final {
        // The promise/future pair is only constructed once there is
        // confirmed room -- the full-queue rejection path stays as cheap as
        // an immediate refusal should be, rather than paying for setup work
        // whose result is about to be discarded.
        std::future<InferenceOutcome> future;
        {
            const std::lock_guard lock{mutex_};
            if (shutting_down_) {
                return InferenceOutcome{.ok = false, .text = {}, .error = shutting_down_message_};
            }
            if (queue_.size() >= max_queue_depth_) {
                return std::nullopt;
            }
            PendingJob job;
            job.prompt = std::move(prompt);
            future = job.promise.get_future();
            queue_.push_back(std::move(job));
        }
        cv_.notify_one();
        return future.get();
    }

    /**
     * Starts this backend's owner thread (spawns it and returns immediately
     * -- never blocks waiting for it to do anything). Every concrete backend
     * (SensenBackend, LlamaCppBackend, and this codebase's test doubles)
     * implements this as little more than `worker_ = std::jthread(...)`.
     *
     * CALLER-ENFORCED ORDERING, AND WHY IT IS A SEPARATE METHOD RATHER THAN
     * PART OF CONSTRUCTION: `set_lease_source()` -- if a caller is going to
     * call it at all -- MUST be called before start(), never after. The
     * owner thread's `take_jobs()` decides, on its VERY FIRST call, which of
     * two structurally different waits to enter (see take_jobs()'s own doc):
     * an UNBOUNDED wait keyed only on the local queue when `lease_source_` is
     * null at that moment, or a periodic, lease-source-polling wait when it
     * is not. That decision is made once, from whatever `lease_source_` holds
     * the instant take_jobs() is first entered -- it is not re-decided later.
     * A backend started before its lease source is installed can therefore
     * commit its owner thread to the unbounded local-only branch and never
     * revisit that decision: nothing about set_lease_source() (installed
     * afterward) wakes a thread already parked there, since the predicate it
     * is waiting on is "the local queue is non-empty", which installing a
     * lease source does not change. The thread then never leases anything
     * from Postgres for the rest of the process's life -- a shared queue that
     * accumulates rows and serves none of them, indistinguishable from the
     * outside from Postgres itself being broken. This is exactly the failure
     * this ordering contract exists to make impossible: every production
     * Worker constructor calls `configure_inference_queue()` (which calls
     * `set_lease_source()` when `INFERENCE_QUEUE=postgres`) BEFORE calling
     * `backend_->start()`, so the owner thread's first take_jobs() call
     * always observes the correct, final `lease_source_` -- non-null in
     * postgres mode, still null (and staying null for the process's whole
     * life) in local mode, where nothing about this ordering is observable
     * at all.
     */
    virtual auto start() -> void = 0;

    /**
     * Installs (or, with `nullptr`, removes) the shared lease source this
     * backend's owner thread will additionally draw from. Thread-safe: may be
     * called once at Worker construction (the only case this codebase
     * exercises) or, in principle, at any time -- `take_jobs()` snapshots the
     * pointer under `mutex_` on every call rather than caching it.
     */
    auto set_lease_source(std::shared_ptr<LeaseSource> source) -> void {
        const std::lock_guard lock{mutex_};
        lease_source_ = std::move(source);
    }

  protected:
    /**
     * A per-surface human-readable name for the "shutting down" message
     * `submit()` returns to a caller that arrives after shutdown has begun.
     * Defaults to the strategy assistant's original wording so
     * assistant_service.cpp's `SensenBackend`/`LlamaCppBackend` need no
     * change; mortgage_assistant_service.cpp's `SensenBackend` passes its own
     * original wording explicitly. This is the one piece of `QueuedBackend`'s
     * externally-visible behaviour that genuinely differed between the two
     * pre-extraction copies, so it is parameterized rather than unified.
     */
    explicit QueuedBackend(
        std::string_view shutting_down_message = "assistant backend is shutting down")
        : shutting_down_message_(shutting_down_message) {}

    /**
     * Blocks the owner thread until at least one job is queued or a stop is
     * requested, then moves up to `want` jobs out of the local queue --
     * and, if a lease source is installed and local jobs did not fill every
     * requested slot, tops up the remainder by leasing from Postgres.
     *
     * `want` is how many free in-flight slots the caller has right now, so a
     * backend never dequeues work it has no slot to run.
     *
     * `block` is false when the caller already has sequences in flight: it
     * must not park on the condition variable while there is decoding to do,
     * it just wants whatever happens to be waiting.
     *
     * WAIT BEHAVIOUR, PRECISELY:
     *
     *   `lease_source_ == nullptr` (local mode, the default): byte-identical
     *   to the pre-extraction code -- `cv_.wait(lock, stoken, pred)`, an
     *   UNBOUNDED wait for either a local job or a stop request. Nothing
     *   about this branch changed, and nothing below it is reachable when
     *   this one is taken.
     *
     *   `lease_source_ != nullptr` (postgres mode): TRY FIRST, WAIT ONLY AS A
     *   BACKSTOP -- the local queue is drained and `lease_source->fill()` is
     *   attempted BEFORE this thread ever waits on anything. Only if that
     *   attempt comes up completely empty (and `block` is true) does it wait
     *   -- for one `kLeasePollTick`, via the plain, non-stop_token-aware
     *   `cv_.wait_for(lock, kLeasePollTick, pred)` overload (deliberately,
     *   not `wait_for(lock, stoken, tick, pred)`: that stop_token-aware
     *   timed-wait template failed to LINK against this build's libc++ when
     *   tried, with the same undefined reference to
     *   `std::__1::__atomic_unique_lock<...>::__set_locked_bit` diagnosed at
     *   `force_take_jobs_symbol_emission` below -- this file works around it
     *   by simply not using that overload here, rather than by forcing a
     *   second symbol; the plain overload used here and the no-timeout
     *   `wait(lock, stoken, pred)` used in the `local` branch above are both
     *   covered by that one forced definition instead) -- and tries once
     *   more before returning. A stop request is still noticed promptly by
     *   the caller's own `while (!stoken.stop_requested())` loop, at worst
     *   one `kLeasePollTick` later.
     *
     *   THIS ORDER IS LOAD-BEARING FOR LATENCY, NOT JUST A STYLE CHOICE: an
     *   earlier version of this function waited FIRST, unconditionally,
     *   whenever a lease source was installed and `block` was true -- so
     *   EVERY request to an otherwise-idle worker paid up to a full
     *   `kLeasePollTick` before this thread even asked Postgres whether
     *   anything was waiting, even when a job was sitting there ready to
     *   lease the instant it asked. Measured against a local Postgres this
     *   was up to 200ms of pure, avoidable latency on every request in
     *   INFERENCE_QUEUE=postgres mode, stacked on top of a decode that only
     *   takes ~1.4s -- see this task's own latency breakdown for the
     *   before/after numbers. Trying first collapses that to the cost of one
     *   `lease()` round trip (low single-digit ms against a same-datacentre
     *   Postgres) on the common case of a job already waiting, and only pays
     *   the poll tick on the genuinely idle case, where nothing is waiting
     *   to be leased and there is nothing faster to do.
     */
    [[nodiscard]] auto take_jobs(std::stop_token stoken, std::size_t want, bool block)
        -> std::vector<PendingJob>;

    /**
     * Fails every job still queued so no caller blocked in `submit()`'s
     * `future.get()` hangs forever waiting on a promise this backend will
     * never fulfil. Called from the owner thread as it exits.
     */
    auto drain_and_fail(std::string_view reason) -> void {
        std::deque<PendingJob> leftovers;
        {
            const std::lock_guard lock{mutex_};
            shutting_down_ = true;
            leftovers.swap(queue_);
        }
        for (auto& job : leftovers) {
            job.promise.set_value(
                InferenceOutcome{.ok = false, .text = {}, .error = std::string{reason}});
        }
    }

    /** How many requests may be decoding simultaneously in one fused batch. */
    std::size_t max_concurrent_ = 1;
    /** How many may WAIT for a slot before further arrivals are refused. */
    std::size_t max_queue_depth_ = 1;

  private:
    [[nodiscard]] auto lease_source_snapshot() -> std::shared_ptr<LeaseSource> {
        const std::lock_guard lock{mutex_};
        return lease_source_;
    }

    /**
     * Exists ONLY to force THIS translation unit -- inference_admission.cppm.o,
     * compiled fresh for, and linked into, every target that uses this
     * module -- to emit a genuine, usable definition of `take_jobs`'s
     * compiled body, rather than leaving that to whichever calling TU
     * (assistant_service.cpp, mortgage_assistant_service.cpp, a test driver,
     * ...) happens to need it first.
     *
     * DIAGNOSIS (root cause, not a shrug): `take_jobs`'s `local`-mode branch
     * calls `std::condition_variable_any::wait(lock, stop_token, pred)`,
     * whose compiled body references a libc++-internal symbol
     * (`std::__1::__atomic_unique_lock<unsigned int, 2>::__set_locked_bit`,
     * reached via `__stop_state::__add_callback`/`__remove_callback` --
     * `<condition_variable>`'s stop_token-aware `wait` overload is built on
     * `<stop_token>`'s `__stop_state`, and THAT is where this symbol lives).
     * This is a genuine, upstream CLANG bug (not libc++, not a toolchain
     * misconfiguration here), independently reproduced with a minimal
     * 3-file example on this project's exact toolchain and canonical flags
     * (clang 22.1.0, `scripts/build_common.sh`'s CANONICAL_FLAGS): a module
     * whose interface unit defines a member function OUT-OF-LINE (`inline`,
     * still inside the interface unit -- exactly `take_jobs`'s own shape,
     * required here because its body needs `PostgresLeaseSource` complete,
     * see this class's `take_jobs` declaration above) that calls
     * `condition_variable_any::wait(lock, stop_token, pred)`, called from a
     * second TU that both `import`s that module AND independently
     * `#include`s `<stop_token>` (as every real caller here does, directly
     * or transitively) fails to link with this EXACT symbol and the EXACT
     * "hidden symbol ... isn't defined" wording. Defining the same function
     * IN-CLASS instead (implicit inline, at the point of the class
     * definition) links fine -- but that shape is unavailable to
     * `take_jobs` for the independent, structural reason given above it.
     *
     * This matches, and is very likely one instance of, a confirmed CLANG
     * (not libc++) bug: llvm/llvm-project#172241, "C++20 modules and
     * std::jthread: link failed with clang & libc++ v21.1.x". A Clang
     * maintainer (ChuanqiXu9) reproduced it, called it a Clang bug rather
     * than a libc++ one, and root-caused it to
     * `clang/lib/Serialization/ASTReaderDecl.cpp`'s declaration-merging
     * logic: when a TU both `import`s a module whose global module fragment
     * `#include`s a header AND separately `#include`s that SAME header
     * itself (exactly this file's shape: inference_admission.cppm's own
     * global module fragment includes `<condition_variable>`/
     * `<stop_token>`, and every calling TU reaches those same headers too),
     * an "optimization" in that merge marks the definition read from the
     * imported module's serialized AST as a mere DECLARATION rather than a
     * definition -- so neither the importing TU nor the module's own
     * compiled object reliably ends up owning a real, emitted copy, and the
     * linker sees only an undefined reference. A fix for this landed
     * upstream and was then REVERTED (commit
     * `42065cfd74dfd95916cc50cf4d324083585a9210`); the issue was reopened
     * and remained unresolved upstream as of 2026-08-09, the date this was
     * last checked.
     *
     * Empirically, on this build's toolchain, whether a given calling TU
     * happens to dodge the bug -- by independently instantiating the same
     * libc++ internals for some unrelated reason of its own, which sidesteps
     * the merge-as-declaration defect entirely rather than depending on it
     * being fixed -- is NOT reliable: assistant_service.cpp.o and
     * mortgage_assistant_service.cpp.o happened to emit a real definition
     * (both construct `std::jthread` workers directly, which independently
     * instantiates this same `__stop_state` machinery); this task's own
     * tests/test_inference_admission.cpp.o and
     * tests/test_inference_admission_pg.cpp.o did not, and failed to link
     * with "undefined reference to
     * std::__1::__atomic_unique_lock<...>::__set_locked_bit" as a result --
     * even though calculator_engine (whose much larger link graph
     * incidentally included an object that DID emit it) linked fine.
     *
     * THE FIX: since the bug is confirmed genuine and, as of this writing,
     * unresolved upstream, this function is the correct mitigation, not a
     * placeholder for a real one -- it removes the dependency on any calling
     * TU's own luck entirely. Defined `[[gnu::used]]` so the optimizer
     * cannot decide it is unreferenced and elide it -- see its out-of-line
     * definition, right after `take_jobs` itself, which is the actual
     * address-of that forces emission. Since inference_admission.cppm.o is
     * part of EVERY target's link by construction, every target gets a
     * reliable source for the symbol regardless of what any calling TU does
     * on its own. Because `take_jobs` is ONE function containing both the
     * `local`-mode `wait(...)` call and the postgres-mode `wait_for(...)`
     * call, forcing emission of its whole body forces both call sites'
     * internal instantiations together -- no second forcing function is
     * needed for the `wait_for` overload used below.
     *
     * WHAT WOULD LET THIS BE REMOVED: once the upstream fix lands AND stays
     * landed (is not reverted again) in a Clang release this project's
     * canonical toolchain (`clang++-22`, `config/cpp_details.txt` rule 50)
     * adopts, delete this function and its out-of-line address-of below, and
     * confirm test_inference_admission/test_inference_admission_pg/
     * calculator_engine/every other target that imports this module still
     * link without it.
     */
    [[maybe_unused, gnu::used]] static auto force_take_jobs_symbol_emission() noexcept
        -> std::vector<PendingJob> (QueuedBackend::*)(std::stop_token, std::size_t, bool);

    /** The BACKSTOP idle-poll interval for `take_jobs()`'s postgres mode --
     *  only ever paid when an immediate lease attempt already came up empty
     *  (see that function's own doc for the try-first ordering this backs
     *  up). Local mode never uses this constant at all.
     *
     *  50ms, not the 200ms this was originally set to: the try-first fix
     *  means this value no longer gates the common case (a job already
     *  waiting), only the genuinely-idle case (nothing to lease at all,
     *  where a worker thread is going to retry regardless and the only
     *  question is how soon) -- so a smaller value buys a tighter worst-case
     *  discovery latency for whatever arrives WHILE this thread happens to
     *  be mid-wait, at the cost of a cheap indexed no-op lease() query up to
     *  20x/sec/idle-worker instead of 5x/sec against Postgres. Against a
     *  same-datacentre Postgres (low single-digit ms per round trip) that
     *  cost is negligible; the latency this buys back is not, when the
     *  budget for the whole postgres-mode path is ~10-15% over local mode's
     *  1.6-2.5s (see this task's own latency breakdown). */
    static constexpr std::chrono::milliseconds kLeasePollTick{50};

    std::mutex mutex_;
    std::condition_variable_any cv_;
    std::deque<PendingJob> queue_;
    bool shutting_down_ = false;
    std::string shutting_down_message_;
    std::shared_ptr<LeaseSource> lease_source_;
};

// ---------------------------------------------------------------------------
// The Postgres-backed shared queue extension (INFERENCE_QUEUE=postgres)
// ---------------------------------------------------------------------------

/**
 * Leases jobs from the shared `inference_queue::Queue` on behalf of one
 * `QueuedBackend`'s owner thread, and writes each one's result back once the
 * SAME decode loop that serves local jobs has produced it.
 *
 * THE WRITE-BACK PROBLEM THIS CLASS SOLVES: `fill()` is called FROM the owner
 * thread (inside `take_jobs()`), which must never block on network I/O for a
 * result that has not been decoded yet -- that thread's job is to keep
 * feeding the fused decode step, not to sit blocked on one job's
 * `future.get()`. So `fill()` only leases and hands back fresh
 * `PendingJob`s (synthetic local jobs, indistinguishable to the decode loop
 * from a real local submission); a short-lived helper thread, spawned here,
 * is what later blocks on that job's `future.get()` and reports the result
 * to Postgres via `complete()`/`fail()`.
 *
 * THREAD LIFETIME: every helper thread this class spawns is joined either
 * when a later `fill()` call notices it finished (`reap_finished_locked()`)
 * or, unconditionally, in `~PostgresLeaseSource()`. This class -- and
 * therefore the `Queue`/`Pool` it holds a `shared_ptr` to -- is guaranteed to
 * outlive every helper thread it spawns: the destructor blocks until each one
 * has been joined before any member is torn down.
 */
export class PostgresLeaseSource final : public LeaseSource {
  public:
    PostgresLeaseSource(std::shared_ptr<options_calculator::inference_queue::Queue> queue,
                         options_calculator::inference_queue::Surface surface,
                         std::string worker_id);
    ~PostgresLeaseSource();

    PostgresLeaseSource(const PostgresLeaseSource&) = delete;
    auto operator=(const PostgresLeaseSource&) -> PostgresLeaseSource& = delete;
    PostgresLeaseSource(PostgresLeaseSource&&) = delete;
    auto operator=(PostgresLeaseSource&&) -> PostgresLeaseSource& = delete;

    /**
     * Leases up to `want` jobs for this source's surface (fewer, or none, if
     * the shared queue does not have that many eligible right now -- an
     * empty/partial result is the ordinary case, not a failure). Each leased
     * job's write-back is delegated to a freshly-spawned helper thread; this
     * call itself never blocks on a decode result.
     */
    [[nodiscard]] auto fill(std::size_t want) -> std::vector<PendingJob> override;

  private:
    auto spawn_writeback(std::int64_t job_id, std::int64_t fencing_token,
                          std::future<InferenceOutcome> future) -> void;
    /** Caller must hold `helpers_mu_`. */
    auto reap_finished_locked() -> void;

    std::shared_ptr<options_calculator::inference_queue::Queue> queue_;
    options_calculator::inference_queue::Surface surface_;
    std::string worker_id_;

    std::mutex helpers_mu_;
    std::vector<std::jthread> helpers_;
    std::vector<std::shared_ptr<std::atomic<bool>>> helper_done_;
};

/**
 * Out-of-line, but still inside inference_admission.cppm (the module
 * INTERFACE unit) -- see QueuedBackend's own comment on `take_jobs`'s
 * declaration for why it cannot be defined earlier (needs PostgresLeaseSource
 * complete) or in inference_admission.cpp (fails to link).
 */
inline auto QueuedBackend::take_jobs(std::stop_token stoken, std::size_t want, bool block)
    -> std::vector<PendingJob> {
    std::vector<PendingJob> taken;
    if (want == 0) return taken;

    const auto lease_source = lease_source_snapshot();

    if (lease_source == nullptr) {
        // BYTE-IDENTICAL to the pre-extraction code: local mode is entirely
        // self-contained in this branch, untouched by anything below it.
        std::unique_lock lock{mutex_};
        if (block) {
            const bool has_job = cv_.wait(lock, stoken, [this] { return !queue_.empty(); });
            if (!has_job) return taken;
        }
        while (!queue_.empty() && taken.size() < want) {
            taken.push_back(std::move(queue_.front()));
            queue_.pop_front();
        }
        return taken;
    }

    // Postgres mode: drain the local queue and try leasing from Postgres
    // BEFORE waiting on anything -- see this function's own declaration-site
    // doc for why the order (try first, wait only as a backstop) is
    // load-bearing for latency, not merely a style choice.
    const auto drain_and_fill = [&] {
        {
            const std::lock_guard lock{mutex_};
            while (!queue_.empty() && taken.size() < want) {
                taken.push_back(std::move(queue_.front()));
                queue_.pop_front();
            }
        }
        if (taken.size() < want) {
            // "Lease late, never early": local submissions were already
            // drained above and always win a free slot; this only tops up
            // whatever slots are STILL empty after that.
            auto leased = lease_source->fill(want - taken.size());
            for (auto& job : leased) taken.push_back(std::move(job));
        }
    };

    drain_and_fill();
    if (!taken.empty() || !block) return taken;

    // Nothing was ready on the first attempt and the caller has nothing
    // else to do -- NOW it is worth waiting, for one tick, before trying
    // once more. See this function's own declaration-site doc for why this
    // is the plain (non-stop_token) wait_for overload.
    {
        std::unique_lock lock{mutex_};
        static_cast<void>(cv_.wait_for(lock, kLeasePollTick, [this] { return !queue_.empty(); }));
    }
    drain_and_fill();
    return taken;
}

/** See the in-class declaration's doc for why this exists. Taking
 *  `take_jobs`'s address here is what forces this translation unit to emit
 *  a real, usable definition of its compiled body. */
inline auto QueuedBackend::force_take_jobs_symbol_emission() noexcept
    -> std::vector<PendingJob> (QueuedBackend::*)(std::stop_token, std::size_t, bool) {
    return &QueuedBackend::take_jobs;
}

/**
 * Wraps a Worker's real local `InferenceBackend` and prefers routing each
 * request through the shared Postgres queue instead of straight to it --
 * falling back to calling the wrapped backend DIRECTLY, i.e. today's exact
 * in-process path, on any failure of the Postgres path (queue full, pool
 * exhausted, circuit open, connect/query failure, or a wait that times out
 * before the job reaches a terminal state). This is the mandatory
 * degrade-never-hang property: a Postgres outage must not turn into an
 * unbounded RPC hang or an error the caller cannot get a real answer behind.
 */
export class PostgresAdmission final : public InferenceBackend {
  public:
    PostgresAdmission(std::shared_ptr<options_calculator::inference_queue::Queue> queue,
                       options_calculator::inference_queue::Surface surface,
                       InferenceBackend& local, std::chrono::milliseconds remote_deadline);

    [[nodiscard]] auto submit(std::string prompt) -> std::optional<InferenceOutcome> override;
    [[nodiscard]] auto name() const noexcept -> std::string_view override { return "postgres"; }

    /** Forwards to the wrapped local backend -- a leased-out job still
     *  decodes on whatever device that backend's own owner thread runs on,
     *  so this is the honest answer, not a hardcoded "cpu". */
    [[nodiscard]] auto device() const noexcept -> std::string_view override {
        return local_.device();
    }

  private:
    std::shared_ptr<options_calculator::inference_queue::Queue> queue_;
    options_calculator::inference_queue::Surface surface_;
    InferenceBackend& local_;
    std::chrono::milliseconds remote_deadline_;
    std::optional<SgeeQueueClient> sgee_client_;
};

// ---------------------------------------------------------------------------
// The SGEE-cluster-backed shared queue extension (INFERENCE_QUEUE=sgee)
// ---------------------------------------------------------------------------

/**
 * `PostgresLeaseSource`'s counterpart against the SGEE Raft cluster.
 *
 * Structurally identical on purpose -- lease on the owner thread, never block
 * there, hand the write-back to a short-lived helper -- because the constraint
 * that shaped it is a property of the DECODE LOOP, not of Postgres. Only the
 * substrate underneath differs.
 *
 * The one real difference is what a fencing token is. Postgres mints a bare
 * int64; SGEE mints a (raft_term, raft_index, local_token) triple whose
 * raft_term is what fences a deposed leader. It is carried whole and forwarded
 * unchanged -- never collapsed to its local_token, which would drop precisely
 * the field that makes it safe across a leader change.
 */
/**
 * The SGEE payload's surface tag and the lease-time filter built from it — the
 * wire contract between a submitter and whichever worker leases its task.
 * Exported so `test_inference_admission` can gate them directly: this is a
 * format shared by two processes, and the defect it fixes was invisible to
 * every layer above it.
 */
export [[nodiscard]] auto encode_prompt_for_surface(
    options_calculator::inference_queue::Surface surface, const std::string& prompt) -> std::string;

/** The raw `surface` string a payload carries, or nullopt when it carries none.
 *  Deliberately returns the NAME rather than a `Surface`: "no tag" and "a tag
 *  naming something this build does not know" are different facts with different
 *  handling, and an optional<Surface> cannot express both. */
export [[nodiscard]] auto decode_surface_name(std::string_view payload_json)
    -> std::optional<std::string>;

/** The bytes a lease source asks the broker to match against a task's payload. */
export [[nodiscard]] auto surface_lease_filter(
    options_calculator::inference_queue::Surface surface) -> std::string;

/** The prompt half of the same payload, through the SAME decoder the worker path
 *  uses — so a test can prove a surface-tagged payload still yields its prompt. */
export [[nodiscard]] auto decode_prompt_payload(std::string_view payload_json) -> std::string;

export class SgeeLeaseSource final : public LeaseSource {
  public:
    /** @param surface the ONLY surface this source may execute. The broker applies
     *         it as a lease-time payload filter, so a task belonging to somebody
     *         else is never chosen for this worker in the first place. */
    SgeeLeaseSource(SgeeQueueClient client,
                     options_calculator::inference_queue::Surface surface,
                     std::uint64_t worker_id, std::uint64_t visibility_ms);
    ~SgeeLeaseSource() override;

    [[nodiscard]] auto fill(std::size_t want) -> std::vector<PendingJob> override;

  private:
    auto spawn_writeback(SgeeLeasedJob job, std::future<InferenceOutcome> future) -> void;
    /** Caller must hold `helpers_mu_`. */
    auto reap_finished_locked() -> void;

    SgeeQueueClient client_;
    options_calculator::inference_queue::Surface surface_;
    /** The bytes sent as the broker's lease-time payload filter. Built once from
     *  the ENCODER's own serialiser so it cannot drift from what is written. */
    std::string lease_filter_;
    std::uint64_t worker_id_;
    std::uint64_t visibility_ms_;

    /** Owner-thread only, like every member here except those under `helpers_mu_`:
     *  fill() is called solely from QueuedBackend::take_jobs on the backend's own
     *  worker thread. Both exist only for the broker-not-filtering path, which is
     *  a deployment fault rather than a routine event. */
    std::chrono::steady_clock::time_point foreign_backoff_until_{};
    std::uint64_t foreign_leases_{0};

    std::mutex helpers_mu_;
    std::vector<std::jthread> helpers_;
    std::vector<std::shared_ptr<std::atomic<bool>>> helper_done_;
};

/**
 * `PostgresAdmission`'s counterpart: prefer routing a request through the SGEE
 * cluster, and fall back to calling the wrapped local backend DIRECTLY on any
 * failure of that path. Same mandatory degrade-never-hang property, same
 * reasoning -- a cluster outage must not become an unbounded RPC hang or an
 * error the caller cannot get a real answer behind.
 *
 * WHAT DIFFERS FROM THE POSTGRES PATH, and it is not cosmetic: Postgres offers
 * `await_result()`, a single blocking call with a `pg_notify` wakeup hint. SGEE
 * offers no await, so this POLLS `GetTask` on a fixed interval until the task is
 * terminal or the deadline passes. That is the same shape the Postgres path
 * degrades to whenever its notify hint does not arrive -- which that module's
 * banner is explicit is never load-bearing for correctness -- so the two have
 * the same failure modes rather than a new set.
 *
 * A task that has DISAPPEARED between submit and poll is treated as a lost
 * answer and falls back, not as a failure. Retention reclaims terminal tasks,
 * so a reclaimed task and one that never existed are the same reply.
 */
export class SgeeAdmission final : public InferenceBackend {
  public:
    /** @param surface stamped into every submitted payload, and the value the
     *         leasing worker's filter matches on. */
    SgeeAdmission(SgeeQueueClient client,
                   options_calculator::inference_queue::Surface surface,
                   InferenceBackend& local, std::chrono::milliseconds remote_deadline);

    [[nodiscard]] auto submit(std::string prompt) -> std::optional<InferenceOutcome> override;
    [[nodiscard]] auto name() const noexcept -> std::string_view override { return "sgee"; }

    /** Forwards to the wrapped local backend, for the same reason
     *  PostgresAdmission does: a leased-out job still decodes on whatever device
     *  that backend's owner thread runs on. */
    [[nodiscard]] auto device() const noexcept -> std::string_view override {
        return local_.device();
    }

  private:
    SgeeQueueClient client_;
    options_calculator::inference_queue::Surface surface_;
    InferenceBackend& local_;
    std::chrono::milliseconds remote_deadline_;
};

}  // namespace options_calculator::inference_admission
