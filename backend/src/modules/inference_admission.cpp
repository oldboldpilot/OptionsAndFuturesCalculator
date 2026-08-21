module;
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <exception>
#include <expected>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

module inference_admission;

import inference_queue;
import fastjson;
import logger;

/**
 * @author Olumuyiwa Oluwasanmi
 *
 * See inference_admission.cppm's module banner for the protocol. This file is
 * the implementation: the local-only admission queue (mechanically unchanged
 * from its two pre-extraction copies) plus the Postgres lease source and
 * submission wrapper.
 */
namespace options_calculator::inference_admission {

namespace {

/** Encodes one prompt as the small JSON payload `inference_queue::Queue`
 *  stores opaquely -- see that module's own banner for why it never
 *  interprets this text itself. */
[[nodiscard]] auto encode_prompt(const std::string& prompt) -> std::string {
    fastjson::json_object obj;
    obj["prompt"] = fastjson::json_value(prompt);
    return fastjson::json_value(std::move(obj)).to_string();
}

/** The inverse of encode_prompt(), used on the leasing/worker side to recover
 *  the prompt a submitter encoded. Returns an empty string on anything
 *  malformed rather than throwing -- a corrupted payload should decode to "an
 *  empty prompt", which the model will refuse sensibly, not crash the
 *  worker's owner thread. */
[[nodiscard]] auto decode_prompt(std::string_view payload_json) -> std::string {
    auto parsed = fastjson::parse(payload_json);
    if (!parsed.has_value() || !parsed->is_object()) return {};
    const auto& obj = parsed.value();
    if (!obj.contains("prompt") || !obj["prompt"].is_string()) return {};
    return std::string(obj["prompt"].as_string());
}

/** Encodes a finished InferenceOutcome's text as the small JSON result
 *  `Queue::complete()` stores. */
[[nodiscard]] auto encode_result(const std::string& text) -> std::string {
    fastjson::json_object obj;
    obj["text"] = fastjson::json_value(text);
    return fastjson::json_value(std::move(obj)).to_string();
}

/** The inverse of encode_result(), used on the submitting side once
 *  await_result() reports a job Done. */
[[nodiscard]] auto decode_result(std::string_view result_json) -> std::string {
    auto parsed = fastjson::parse(result_json);
    if (!parsed.has_value() || !parsed->is_object()) return {};
    const auto& obj = parsed.value();
    if (!obj.contains("text") || !obj["text"].is_string()) return {};
    return std::string(obj["text"].as_string());
}


// ── SGEE surface routing ──────────────────────────────────────────────────────
//
// SGEE's lease() takes a worker id and, until the payload filter below existed,
// no routing dimension at all -- PlacementTarget says WHERE to run (Cpu/Gpu),
// not WHICH model. Both assistants therefore polled one undifferentiated queue,
// and a strategy worker ran mortgage prompts through the strategy fine-tune,
// completed them OK, and the mortgage grounding verifier refused the result --
// silently, because nothing failed at any layer. Measured cost on a fixed 16-row
// holdout through the live ingress: 11/16 -> ~6/16, a different failing row set
// every run.
//
// The fix is a filter applied where the broker CHOOSES, not a check after it
// hands the task over. Leasing-then-releasing cannot work here and the reason is
// structural: lease() has no skip, and a released task returns to the head with
// its ORIGINAL enqueue time, so the wrong worker is blocked by the same task
// next time AND burns one `attempt` per release -- `lease` being the only place
// `attempt` is incremented. An idle worker drives a perfectly good task into the
// DLQ in seconds without ever executing it.

/**
 * How long a lease source stands down after being handed a task belonging to
 * another surface DESPITE having asked the broker to filter.
 *
 * With `payload_filter` honoured this never fires. It exists for the one window
 * where it can: a new engine talking to a queue node old enough to ignore the
 * field. Two seconds rather than a few hundred milliseconds because the thing
 * being waited on is another worker finishing a DECODE, which takes seconds; a
 * backoff an order of magnitude shorter than the event it waits for is just a
 * faster spin.
 */
constexpr auto kForeignLeaseBackoff = std::chrono::seconds(2);

/**
 * What a payload says about its owning surface. THREE states, not two, and the
 * third is why this is an enum rather than an optional:
 *
 *  - None     no `surface` field: a legacy payload written before this existed.
 *             It has no owner, so any worker that is HANDED one may run it.
 *
 *             Read that precisely. Once the broker applies the lease filter, an
 *             untagged task matches NO worker's filter and is therefore never
 *             handed to anybody -- this branch is defence in depth for the
 *             unfiltered path, not a guarantee that legacy work drains. See
 *             `docs/SGEE_QUEUE_CLUSTER.md` on deploy order: the engines are the
 *             only producer, so untagged tasks exist only while an OLD engine is
 *             still enqueuing, and an old engine leases unfiltered and drains its
 *             own. The window that can strand one is an untagged task enqueued
 *             just before the last old engine goes away. No REQUEST is lost even
 *             then -- SgeeAdmission::submit falls back to the local backend -- but
 *             the task itself is orphaned in `pending_`, so the queue should be
 *             drained before the cutover rather than during it.
 *  - Known    a surface this build recognises.
 *  - Unknown  a `surface` field naming something this build has never heard of.
 *             That is a POSITIVE statement that another worker owns the task,
 *             and running it here is guaranteed wrong. Collapsing it onto None
 *             would re-create this very defect on the third surface anyone adds.
 */
enum class SurfaceTag : std::uint8_t { None, Known, Unknown };

struct DecodedSurface {
    SurfaceTag tag{SurfaceTag::None};
    options_calculator::inference_queue::Surface surface{};
};

/**
 * Recovers what `encode_prompt_for_surface` wrote.
 *
 * Malformed JSON decodes to None rather than Unknown: a payload that is not an
 * object never made a claim about ownership, so there is nothing to honour.
 * `decode_prompt` yields an empty prompt for it and the model refuses that
 * sensibly, which is the pre-existing contract for a corrupt payload.
 *
 * The name-to-enum mapping is `inference_queue::surface_from_string`,
 * deliberately NOT an if-chain here. A hand-rolled copy is a table a new
 * `Surface` fails to update while `to_string`'s switch is caught by -Wswitch --
 * and a surface this table silently failed to recognise is one every worker
 * would then run.
 */
[[nodiscard]] auto decode_surface(std::string_view payload_json) -> DecodedSurface {
    auto parsed = fastjson::parse(payload_json);
    if (!parsed.has_value() || !parsed->is_object()) return {};
    const auto& obj = parsed.value();
    if (!obj.contains("surface") || !obj["surface"].is_string()) return {};
    if (const auto s = options_calculator::inference_queue::surface_from_string(
            obj["surface"].as_string())) {
        return {SurfaceTag::Known, *s};
    }
    return {SurfaceTag::Unknown, {}};
}

}  // namespace

/**
 * Encodes one prompt for the SGEE queue, naming the surface it belongs to.
 *
 * The field is ADDITIVE: `decode_prompt` reads "prompt" and ignores everything
 * else, so a worker built before this change still recovers the prompt from a
 * payload written after it. That is what makes a rolling deploy safe in either
 * order -- an old worker simply cannot honour a tag it does not read.
 */
[[nodiscard]] auto encode_prompt_for_surface(options_calculator::inference_queue::Surface surface,
                                             const std::string& prompt) -> std::string {
    fastjson::json_object obj;
    obj["surface"] = fastjson::json_value(std::string(to_string(surface)));
    obj["prompt"] = fastjson::json_value(prompt);
    return fastjson::json_value(std::move(obj)).to_string();
}

/**
 * The bytes a lease source asks the broker to match, built by the ENCODER's own
 * serialiser so the filter and the payload cannot drift apart by being written
 * twice. Returns the `"surface":"..."` member without the enclosing braces:
 * what has to appear inside the real payload is the member, not a whole object.
 */
[[nodiscard]] auto surface_lease_filter(options_calculator::inference_queue::Surface surface)
    -> std::string {
    fastjson::json_object probe;
    probe["surface"] = fastjson::json_value(std::string(to_string(surface)));
    const auto rendered = fastjson::json_value(std::move(probe)).to_string();
    return rendered.substr(1, rendered.size() - 2);
}

/** The raw `surface` string a payload carries, or nullopt when it carries none.
 *  See decode_surface() above for why the NAME rather than a Surface. */
[[nodiscard]] auto decode_surface_name(std::string_view payload_json)
    -> std::optional<std::string> {
    auto parsed = fastjson::parse(payload_json);
    if (!parsed.has_value() || !parsed->is_object()) return std::nullopt;
    const auto& obj = parsed.value();
    if (!obj.contains("surface") || !obj["surface"].is_string()) return std::nullopt;
    return std::string(obj["surface"].as_string());
}

/** Exported forwarder for the anonymous-namespace `decode_prompt`, so a test
 *  exercises the SAME function the worker path uses, not a re-implementation. */
[[nodiscard]] auto decode_prompt_payload(std::string_view payload_json) -> std::string {
    return decode_prompt(payload_json);
}


// QueuedBackend's submit()/take_jobs()/drain_and_fail()/set_lease_source()/
// lease_source_snapshot() are all defined in inference_admission.cppm itself
// (the module INTERFACE unit), not here -- see that file's comments on
// take_jobs()'s declaration for the two independent reasons (an incomplete-
// type ordering constraint, and a libc++ link failure specific to defining
// that function in THIS file). Everything below is genuinely new code that
// does not have that constraint.

// ---------------------------------------------------------------------------
// PostgresLeaseSource
// ---------------------------------------------------------------------------

PostgresLeaseSource::PostgresLeaseSource(
    std::shared_ptr<options_calculator::inference_queue::Queue> queue,
    options_calculator::inference_queue::Surface surface, std::string worker_id)
    : queue_(std::move(queue)), surface_(surface), worker_id_(std::move(worker_id)) {}

PostgresLeaseSource::~PostgresLeaseSource() {
    // Every helper thread is joined before any member (in particular queue_,
    // which those threads hold their own shared_ptr copy of, but which this
    // object must not appear to outlive with unjoined workers still able to
    // touch it indirectly) is torn down.
    const std::lock_guard lock{helpers_mu_};
    for (auto& h : helpers_) {
        if (h.joinable()) h.join();
    }
}

auto PostgresLeaseSource::fill(std::size_t want) -> std::vector<PendingJob> {
    std::vector<PendingJob> out;
    if (want == 0) return out;

    {
        const std::lock_guard lock{helpers_mu_};
        reap_finished_locked();
    }

    for (std::size_t i = 0; i < want; ++i) {
        auto leased = queue_->lease(surface_, worker_id_);
        if (!leased.has_value()) {
            logger::Logger::getInstance().warn(
                "inference_admission: PostgresLeaseSource::lease() failed ({}) -- stopping this "
                "fill pass early",
                options_calculator::inference_queue::to_string(leased.error()));
            break;
        }
        if (!leased->has_value()) break;  // nothing eligible right now -- not an error

        options_calculator::inference_queue::Job job = std::move(**leased);
        const std::int64_t job_id = job.id;
        const std::int64_t fencing_token = job.fencing_token.value_or(0);

        PendingJob pending;
        pending.prompt = decode_prompt(job.payload);
        auto future = pending.promise.get_future();
        out.push_back(std::move(pending));

        spawn_writeback(job_id, fencing_token, std::move(future));
    }
    return out;
}

auto PostgresLeaseSource::spawn_writeback(std::int64_t job_id, std::int64_t fencing_token,
                                           std::future<InferenceOutcome> future) -> void {
    auto done = std::make_shared<std::atomic<bool>>(false);
    // The helper thread takes its own shared_ptr copy of queue_ rather than
    // capturing `this`: its job (await the decode result, write it back) has
    // nothing to do with this PostgresLeaseSource's own state once it has
    // been spawned, and this keeps the Queue/Pool alive for exactly as long
    // as the thread needs them regardless of exactly when the destructor's
    // join catches up to it.
    std::jthread worker{[queue = queue_, job_id, fencing_token, future = std::move(future),
                          done]() mutable {
        InferenceOutcome outcome;
        try {
            outcome = future.get();
        } catch (const std::exception& e) {
            outcome = InferenceOutcome{.ok = false, .text = {}, .error = e.what()};
        } catch (...) {
            outcome = InferenceOutcome{
                .ok = false, .text = {}, .error = "unknown failure awaiting the local decode result"};
        }

        // Everything below can throw -- queue->complete()/fail() reach a
        // database, and encode_result() allocates -- and none of it may be
        // allowed to escape this lambda: it is a std::jthread entry function,
        // and an escaping exception calls std::terminate() and kills the
        // whole process, not just this job. The catch clauses themselves must
        // not be able to throw either, so each one wraps its own logging in a
        // further try/catch that swallows anything the logger itself raises.
        try {
            std::expected<bool, options_calculator::inference_queue::SubmitError> written;
            if (outcome.ok) {
                written = queue->complete(job_id, fencing_token, encode_result(outcome.text));
            } else {
                written = queue->fail(job_id, fencing_token, outcome.error);
            }

            if (!written.has_value()) {
                logger::Logger::getInstance().warn(
                    "inference_admission: writing back job {} failed ({}) -- the submitting side's "
                    "own await_result() will time out and fall back to its local backend",
                    job_id, options_calculator::inference_queue::to_string(written.error()));
            } else if (!written.value()) {
                // Fenced out: a fresher lease already owns this job (the sweeper
                // reclaimed it as abandoned, or something else re-leased it).
                // Per inference_queue.cppm's own contract this result MUST be
                // discarded, not retried -- retrying would overwrite whatever the
                // fresher lease-holder is doing with a stale answer.
                logger::Logger::getInstance().info(
                    "inference_admission: job {} was fenced out before this worker's result could be "
                    "written back -- discarding (a fresher lease already owns it)",
                    job_id);
            }
        } catch (const std::exception& e) {
            try {
                logger::Logger::getInstance().warn(
                    "inference_admission: writing back job {} threw ({}) -- the submitting side's "
                    "own await_result() will time out and fall back to its local backend",
                    job_id, e.what());
            } catch (...) {
                // The logger must not be able to bring this thread down either.
            }
        } catch (...) {
            try {
                logger::Logger::getInstance().warn(
                    "inference_admission: writing back job {} threw an unknown exception -- the "
                    "submitting side's own await_result() will time out and fall back to its local "
                    "backend",
                    job_id);
            } catch (...) {
            }
        }
        done->store(true, std::memory_order_release);
    }};

    const std::lock_guard lock{helpers_mu_};
    helpers_.push_back(std::move(worker));
    helper_done_.push_back(std::move(done));
}

auto PostgresLeaseSource::reap_finished_locked() -> void {
    std::size_t i = 0;
    while (i < helpers_.size()) {
        if (helper_done_[i]->load(std::memory_order_acquire)) {
            if (helpers_[i].joinable()) helpers_[i].join();
            helpers_.erase(helpers_.begin() + static_cast<std::ptrdiff_t>(i));
            helper_done_.erase(helper_done_.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
}

// ---------------------------------------------------------------------------
// PostgresAdmission
// ---------------------------------------------------------------------------

PostgresAdmission::PostgresAdmission(
    std::shared_ptr<options_calculator::inference_queue::Queue> queue,
    options_calculator::inference_queue::Surface surface, InferenceBackend& local,
    std::chrono::milliseconds remote_deadline)
    : queue_(std::move(queue)), surface_(surface), local_(local), remote_deadline_(remote_deadline),
      sgee_client_(SgeeQueueClient::create_from_env()) {}

auto PostgresAdmission::submit(std::string prompt) -> std::optional<InferenceOutcome> {
    const auto deadline = std::chrono::system_clock::now() + remote_deadline_;
    const std::string payload_json = encode_prompt(prompt);

    auto submitted = queue_->submit_remote(surface_, payload_json, deadline);
    if (!submitted.has_value()) {
        // ANY submit_remote failure -- QueueFull, ConnectFailed, CircuitOpen,
        // PoolExhausted, Timeout, DatabaseError, InvalidSurface -- degrades to
        // the wrapped local backend's own admission queue immediately. This
        // is the mandatory degrade-never-hang property: a Postgres outage
        // must never turn into an unbounded hang or an error the caller
        // cannot get a real answer behind, when local capacity might still
        // serve it.
        logger::Logger::getInstance().warn(
            "inference_admission: submit_remote failed ({}) -- falling back to the local backend",
            options_calculator::inference_queue::to_string(submitted.error()));
        return local_.submit(std::move(prompt));
    }

    if (sgee_client_.has_value()) {
        sgee_client_->enqueue_mirror(payload_json);
    }

    auto job = queue_->await_result(submitted->job_id, deadline);
    if (!job.has_value()) {
        // Reachable even after a successful submit_remote: Postgres can
        // become unreachable mid-wait, or nothing leases the job before
        // `deadline` (Timeout). Same fallback, same rationale.
        logger::Logger::getInstance().warn(
            "inference_admission: await_result failed ({}) for job {} -- falling back to the "
            "local backend",
            options_calculator::inference_queue::to_string(job.error()), submitted->job_id);
        return local_.submit(std::move(prompt));
    }

    switch (job->state) {
        case options_calculator::inference_queue::JobState::Done: {
            InferenceOutcome outcome;
            outcome.ok = true;
            outcome.text = decode_result(job->result.value_or(std::string{}));
            return outcome;
        }
        case options_calculator::inference_queue::JobState::Failed:
        case options_calculator::inference_queue::JobState::Dead: {
            InferenceOutcome outcome;
            outcome.ok = false;
            outcome.error = job->error.value_or("the shared inference queue reported failure");
            return outcome;
        }
        case options_calculator::inference_queue::JobState::Pending:
        case options_calculator::inference_queue::JobState::Leased:
        default:
            // await_result() only returns on a terminal state or a Timeout
            // (handled above as !job.has_value()) -- this is unreachable in
            // practice. A non-terminal state is not something to treat as
            // success either way, so it fails closed to the local backend
            // rather than fabricating an outcome.
            logger::Logger::getInstance().warn(
                "inference_admission: await_result returned non-terminal state {} for job {} -- "
                "falling back to the local backend",
                options_calculator::inference_queue::to_string(job->state), submitted->job_id);
            return local_.submit(std::move(prompt));
    }
}

// ---------------------------------------------------------------------------
// SgeeLeaseSource
// ---------------------------------------------------------------------------

SgeeLeaseSource::SgeeLeaseSource(SgeeQueueClient client,
                                   options_calculator::inference_queue::Surface surface,
                                   std::uint64_t worker_id, std::uint64_t visibility_ms)
    : client_(std::move(client)), surface_(surface),
      lease_filter_(surface_lease_filter(surface)), worker_id_(worker_id),
      visibility_ms_(visibility_ms) {}

SgeeLeaseSource::~SgeeLeaseSource() {
    // Same contract as PostgresLeaseSource's destructor: every helper is joined
    // before any member is torn down, so no thread can still be holding a
    // reference to this object's client while it is being destroyed.
    const std::lock_guard lock{helpers_mu_};
    for (auto& h : helpers_) {
        if (h.joinable()) h.join();
    }
}

auto SgeeLeaseSource::fill(std::size_t want) -> std::vector<PendingJob> {
    std::vector<PendingJob> out;
    if (want == 0) return out;

    {
        const std::lock_guard lock{helpers_mu_};
        reap_finished_locked();
    }

    // Only reachable when the broker did NOT honour the filter -- see the branch
    // below. Standing down hands the queue to the worker that can run the head.
    if (std::chrono::steady_clock::now() < foreign_backoff_until_) return out;

    for (std::size_t i = 0; i < want; ++i) {
        // The filter is applied by the broker when it CHOOSES, so a task belonging
        // to another surface is normally never leased here at all -- and, just as
        // importantly, this worker's own tasks queued BEHIND one stay reachable. A
        // post-lease check can give neither property, because lease() has no skip.
        auto leased = client_.lease_blocking(worker_id_, visibility_ms_, lease_filter_);
        if (!leased.has_value()) {
            // Nothing eligible, OR the cluster did not answer. Both stop this
            // fill pass and both are correct to treat identically here -- the
            // owner thread's next move is the same either way. The client has
            // already logged whichever it was.
            break;
        }

        // Defence in depth for ONE window: a new engine against a queue node old
        // enough to ignore `payload_filter`. It cannot fire otherwise.
        //
        // The task is failed ONCE rather than released-and-retried, and that is
        // the whole difference from the design an earlier revision of this file
        // shipped. A retry loop burns one `attempt` per pass -- `lease` being the
        // only place `attempt` is incremented -- and kills a perfectly good task
        // in seconds without executing it. One failure is answered by
        // SgeeAdmission::submit falling back to the LOCAL backend: a correct
        // answer, on the right model, slightly slower. Unknown counts as foreign,
        // for the reason DecodedSurface documents.
        if (const auto tagged = decode_surface(leased->payload);
            (tagged.tag == SurfaceTag::Known && tagged.surface != surface_) ||
            tagged.tag == SurfaceTag::Unknown) {
            const bool reported = client_.fail_blocking(*leased);
            ++foreign_leases_;
            foreign_backoff_until_ = std::chrono::steady_clock::now() + kForeignLeaseBackoff;
            // ERROR rather than warn: with the lease filter in place this is a
            // statement that the broker is not applying it, which is a deployment
            // fault, not a routine event. The reason is named for the same reason
            // the writeback path names its own -- "it failed" without "why" cannot
            // tell a refused task from a moved leader.
            logger::Logger::getInstance().error(
                "inference_admission: SGEE handed a '{}' task ({}) to the '{}' worker despite a "
                "payload filter -- the broker is not applying it (an older queue node?). "
                "Reporting the task failed so the submitter falls back locally{}; {} foreign "
                "lease(s) so far.",
                tagged.tag == SurfaceTag::Unknown ? std::string("unrecognised")
                                                  : std::string(to_string(tagged.surface)),
                leased->task_id, to_string(surface_),
                reported ? std::string{}
                         : std::format(" -- BUT THE REPORT ITSELF FAILED ({}), so the task stays "
                                       "leased until its {} ms visibility window expires",
                                       client_.last_failure(), visibility_ms_),
                foreign_leases_);
            break;
        }

        PendingJob pending;
        pending.prompt = decode_prompt(leased->payload);
        auto future = pending.promise.get_future();
        out.push_back(std::move(pending));

        spawn_writeback(std::move(*leased), std::move(future));
    }
    return out;
}

auto SgeeLeaseSource::spawn_writeback(SgeeLeasedJob job, std::future<InferenceOutcome> future)
    -> void {
    auto done = std::make_shared<std::atomic<bool>>(false);
    // The helper takes its OWN copy of the client rather than capturing `this`,
    // for the same reason PostgresLeaseSource's helper copies the queue
    // shared_ptr: once spawned, its work has nothing to do with this object's
    // state, and the copy keeps the channel pool alive for exactly as long as
    // the thread needs it.
    std::jthread worker{[client = client_, job = std::move(job), future = std::move(future),
                          done]() mutable {
        InferenceOutcome outcome;
        try {
            outcome = future.get();
        } catch (const std::exception& e) {
            outcome = InferenceOutcome{.ok = false, .text = {}, .error = e.what()};
        } catch (...) {
            outcome = InferenceOutcome{
                .ok = false, .text = {}, .error = "unknown failure awaiting the local decode result"};
        }

        // Everything below can throw -- complete_blocking()/fail_blocking() are
        // gRPC calls, and encode_result() allocates -- and none of it may be
        // allowed to escape this lambda: it is a std::jthread entry function,
        // and an escaping exception calls std::terminate() and kills the whole
        // process, not just this task. The catch clauses themselves must not be
        // able to throw either, so each one wraps its own logging in a further
        // try/catch that swallows anything the logger itself raises.
        try {
            bool written = false;
            if (outcome.ok) {
                written = client.complete_blocking(job, encode_result(outcome.text));
            } else {
                // The SGEE queue replicates RESULTS, not error strings -- see
                // task_queue.proto's comment on Task.result for why widening
                // BrokerFail was not taken. A failure is signalled by the task
                // reaching its terminal Dead state, which the submitter reads from
                // `state`; the text is logged here, where it is still available.
                logger::Logger::getInstance().info(
                    "inference_admission: SGEE task {} failed locally ({}) -- reporting the failure to "
                    "the cluster; the reason is logged here because the queue carries results, not "
                    "error text",
                    job.task_id, outcome.error);
                written = client.fail_blocking(job);
            }

            if (!written) {
                // Name the REASON. This warning used to carry only the task id, because the
                // call reports success as a bool -- so a production writeback failing on every
                // request could not say whether the cluster had refused the task, the leader
                // had moved, or the deadline had blown. Those are three different faults with
                // three different fixes, and to_string(ClientOutcome) was available the whole
                // time.
                logger::Logger::getInstance().warn(
                    "inference_admission: writing back SGEE task {} failed ({}) -- the submitting "
                    "side's own poll will time out and fall back to its local backend",
                    job.task_id, client.last_failure());
            }
        } catch (const std::exception& e) {
            try {
                logger::Logger::getInstance().warn(
                    "inference_admission: writing back SGEE task {} threw ({}) -- the submitting "
                    "side's own poll will time out and fall back to its local backend",
                    job.task_id, e.what());
            } catch (...) {
                // The logger must not be able to bring this thread down either.
            }
        } catch (...) {
            try {
                logger::Logger::getInstance().warn(
                    "inference_admission: writing back SGEE task {} threw an unknown exception -- "
                    "the submitting side's own poll will time out and fall back to its local "
                    "backend",
                    job.task_id);
            } catch (...) {
            }
        }
        done->store(true, std::memory_order_release);
    }};

    const std::lock_guard lock{helpers_mu_};
    helpers_.push_back(std::move(worker));
    helper_done_.push_back(std::move(done));
}

auto SgeeLeaseSource::reap_finished_locked() -> void {
    std::size_t i = 0;
    while (i < helpers_.size()) {
        if (helper_done_[i]->load(std::memory_order_acquire)) {
            if (helpers_[i].joinable()) helpers_[i].join();
            helpers_.erase(helpers_.begin() + static_cast<std::ptrdiff_t>(i));
            helper_done_.erase(helper_done_.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
}

// ---------------------------------------------------------------------------
// SgeeAdmission
// ---------------------------------------------------------------------------

SgeeAdmission::SgeeAdmission(SgeeQueueClient client,
                               options_calculator::inference_queue::Surface surface,
                               InferenceBackend& local,
                               std::chrono::milliseconds remote_deadline)
    : client_(std::move(client)), surface_(surface), local_(local),
      remote_deadline_(remote_deadline) {}

auto SgeeAdmission::submit(std::string prompt) -> std::optional<InferenceOutcome> {
    const auto deadline = std::chrono::steady_clock::now() + remote_deadline_;
    const std::string payload_json = encode_prompt_for_surface(surface_, prompt);

    auto task_id = client_.submit_blocking(payload_json);
    if (!task_id.has_value()) {
        logger::Logger::getInstance().warn(
            "inference_admission: SGEE submit failed -- falling back to the local backend");
        return local_.submit(std::move(prompt));
    }

    // Poll rather than await. The interval is a compromise with one real cost on
    // each side: too long adds latency to every request that finishes early, too
    // short multiplies GetTask RPCs against a three-node cluster for no benefit.
    // 25 ms is well under the smallest plausible decode and cheap at this
    // concurrency.
    constexpr auto kPollInterval = std::chrono::milliseconds(25);
    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(kPollInterval);

        auto outcome = client_.poll_task(*task_id);
        if (!outcome.has_value()) {
            // The poll itself did not complete. Keep trying until the deadline
            // rather than degrading immediately: a single lost RPC against a
            // cluster that is mid-election is expected, and the work may well
            // already be running. Degrading here would run the whole inference
            // twice.
            continue;
        }
        if (!outcome->found) {
            // Answered, and the task is gone. Retention reclaimed it, or it never
            // landed. Either way the answer is not coming.
            logger::Logger::getInstance().warn(
                "inference_admission: SGEE task {} is no longer present -- falling back to the "
                "local backend",
                *task_id);
            return local_.submit(std::move(prompt));
        }
        if (!outcome->terminal) continue;

        if (outcome->ok) {
            InferenceOutcome result;
            result.ok = true;
            result.text = decode_result(outcome->result);
            return result;
        }
        // The queue reported the task Dead. That is a failure OF THE QUEUE PATH,
        // and this class's contract -- stated in its own header, honoured by every
        // other branch here -- is to fall back to the local backend on any such
        // failure. Returning the error instead made attempt exhaustion a
        // user-visible request failure while the local model sat idle and able to
        // answer correctly.
        logger::Logger::getInstance().warn(
            "inference_admission: SGEE task {} reached a terminal failure -- falling back to the "
            "local backend rather than failing the request",
            *task_id);
        return local_.submit(std::move(prompt));
    }

    logger::Logger::getInstance().warn(
        "inference_admission: SGEE task {} did not reach a terminal state before the deadline -- "
        "falling back to the local backend",
        *task_id);
    return local_.submit(std::move(prompt));
}

}  // namespace options_calculator::inference_admission
