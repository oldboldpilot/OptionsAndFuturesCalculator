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

}  // namespace

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
            return local_.submit(std::move(prompt));
    }
}

}  // namespace options_calculator::inference_admission
