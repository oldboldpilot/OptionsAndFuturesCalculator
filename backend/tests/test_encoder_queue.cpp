// @author Olumuyiwa Oluwasanmi
//
// The encoder assistants on the shared queue: the wire codec, EncoderBackend, answer(), and
// LeaseRunner -- the pieces that let a request submitted on one replica be executed by another.
//
// HERMETIC BY DESIGN: no model, no cluster, no Postgres. The chain is a stub, and the shared
// queue is an in-memory double that implements BOTH halves a real queue has (the submitter's
// InferenceBackend and the worker's LeaseSource), so the properties that belong to THIS module
// are decidable in milliseconds:
//
//   * a request submitted through the queue is executed by whichever replica leases it, and the
//     answer is the one the process would have given itself;
//   * work is SHARED -- two runners draining one queue both execute, and together execute
//     exactly what was submitted;
//   * a queue that cannot answer costs nothing but the queue: the request is answered locally;
//   * a refusal from the chain is an ANSWER that completes the task, not a failure that burns
//     its attempts.
//
// What this file cannot decide -- that the real SGEE broker honours the surface filter, that the
// real services submit at all -- is decided against a real three-node cluster by
// tests/integration/encoder_queue_cluster_test.sh. Plain check()/section() harness, as in every
// test here (config/cpp_details.txt rule 39).
#include <cstdio>
#include <stdlib.h>
#include <unistd.h>

import std;
import assistant_runtime;
import inference_admission;
import encoder_queue;
import inference_queue;
import logger;
import sgee_queue_client;
import sensen.encoder_assistant;

using namespace std::chrono_literals;
using options_calculator::encoder_queue::Answer;
using options_calculator::encoder_queue::EncoderBackend;
using options_calculator::encoder_queue::EncoderService;
using options_calculator::encoder_queue::EncoderRequest;
using options_calculator::encoder_queue::Bounds;
using options_calculator::encoder_queue::InFlightCount;
using options_calculator::encoder_queue::Verdict;
using options_calculator::inference_admission::InferenceBackend;
using options_calculator::inference_admission::InferenceOutcome;
using options_calculator::inference_admission::LeaseRunner;
using options_calculator::inference_admission::LeaseSource;
using options_calculator::inference_admission::PendingJob;
using sensen::encoder_assistant::Parsed;
using sensen::encoder_assistant::Turns;

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

/** True when a count can be left by hand: it must not be, only its `Entered` token leaves. */
template <typename Count>
concept CanLeaveByHand = requires(Count& count) { count.leave(); };

auto section(const char* title) -> void { std::printf("\n=== %s ===\n", title); }

/** The file the logger writes to for this run. The logger flushes every line, so a test can count
 *  what the code under test SAID -- which is the thing an operator's accounting is built from. */
[[nodiscard]] auto log_path() -> const std::filesystem::path& {
    static const auto path = std::filesystem::temp_directory_path() /
                             ("test_encoder_queue_" + std::to_string(::getpid()) + ".log");
    return path;
}

[[nodiscard]] auto log_lines_containing(std::string_view needle) -> int {
    std::ifstream in{log_path()};
    int count = 0;
    for (std::string line; std::getline(in, line);) {
        if (line.contains(needle)) ++count;
    }
    return count;
}

/** The three per-request lines `EncoderService::answer` writes -- one per request, because "how was
 *  this answered" is a count an operator reads. The first is also written, with a suffix, when the
 *  replica was busy and had no queue slot to spill into. */
constexpr std::string_view kInProcess = "answered in-process";
constexpr std::string_view kSpilled = "spilled to the queue and was answered by it";
constexpr std::string_view kDegradedLocally = "answered locally after the shared queue degraded";

using ChainResult = std::expected<std::optional<Parsed>, std::string>;

/** A chain with the three outcomes the real one has, selected by the utterance, and a counter of
 *  how many times THIS instance ran -- the only way to say which replica did the work. */
struct CountingChain {
    std::shared_ptr<std::atomic<int>> runs = std::make_shared<std::atomic<int>>(0);
    /** How many executions of THIS chain are inside it right now, and the most there ever were: the
     *  quantity the local bound caps, observed from inside the chain rather than assumed. */
    std::shared_ptr<std::atomic<int>> active = std::make_shared<std::atomic<int>>(0);
    std::shared_ptr<std::atomic<int>> peak = std::make_shared<std::atomic<int>>(0);
    std::chrono::milliseconds work{0};
    /** Every execution waits here before it answers. Open (count 0) unless a test holds the chain
     *  on purpose: a test that needs "a request is inside the chain" releases it explicitly, rather
     *  than sleeping and hoping the next arrival lands inside the window. */
    std::shared_ptr<std::latch> gate = std::make_shared<std::latch>(0);

    [[nodiscard]] auto make() const -> EncoderBackend::Chain {
        return [runs = runs, active = active, peak = peak, work = work,
                gate = gate](const Turns& turns) -> ChainResult {
            runs->fetch_add(1);
            const int now = active->fetch_add(1) + 1;
            for (int seen = peak->load(); now > seen && !peak->compare_exchange_weak(seen, now);) {}
            struct Done {
                std::atomic<int>& active;
                ~Done() { active.fetch_sub(1); }
            } const done{*active};
            if (work.count() > 0) std::this_thread::sleep_for(work);
            gate->wait();
            if (turns.utterance == "none") return std::optional<Parsed>{};
            if (turns.utterance.starts_with("refuse")) {
                return std::unexpected(std::string("token straddles a literal: ") +
                                       std::string(turns.utterance));
            }
            Parsed parsed;
            parsed.operation = "ComputePayment";
            parsed.params["echo"] = std::string(turns.utterance) + "|" +
                                    std::string(turns.prior_question) + "|" +
                                    std::string(turns.prior_clarification);
            return std::optional<Parsed>{std::move(parsed)};
        };
    }
};

[[nodiscard]] auto render(const Parsed& parsed) -> std::string { return parsed.to_json(); }

[[nodiscard]] auto make_backend(const CountingChain& chain) -> std::unique_ptr<EncoderBackend> {
    return std::make_unique<EncoderBackend>(chain.make(), &render, "test encoder");
}

/** The shared queue, in memory: the submitter's half and the worker's half in one object, which
 *  is what a real queue is. `submit()` parks the caller until some LeaseRunner answers. */
class InMemoryQueue final : public InferenceBackend, public LeaseSource {
  public:
    [[nodiscard]] auto submit(std::string prompt) -> std::optional<InferenceOutcome> override {
        std::future<InferenceOutcome> future;
        {
            const std::lock_guard lock{mutex_};
            PendingJob job;
            job.prompt = std::move(prompt);
            future = job.promise.get_future();
            pending_.push_back(std::move(job));
            ++submitted_;
        }
        if (future.wait_for(10s) != std::future_status::ready) {
            return InferenceOutcome{.ok = false, .text = {}, .error = "no worker answered"};
        }
        return future.get();
    }

    [[nodiscard]] auto name() const noexcept -> std::string_view override { return "in-memory"; }

    [[nodiscard]] auto fill(std::size_t want) -> std::vector<PendingJob> override {
        std::vector<PendingJob> out;
        const std::lock_guard lock{mutex_};
        while (out.size() < want && !pending_.empty()) {
            out.push_back(std::move(pending_.front()));
            pending_.pop_front();
        }
        return out;
    }

    [[nodiscard]] auto submitted() const -> int {
        const std::lock_guard lock{mutex_};
        return submitted_;
    }

  private:
    mutable std::mutex mutex_;
    std::deque<PendingJob> pending_;
    int submitted_{0};
};

/** Polls `ready` for up to two seconds. A test that waits for a state its code under test is meant to
 *  produce must not wait FOREVER if the code is wrong: a hang reports nothing, a bounded wait reports
 *  which expectation failed. */
template <typename Ready>
[[nodiscard]] auto wait_until(Ready ready) -> bool {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!ready()) {
        if (std::chrono::steady_clock::now() > deadline) return false;
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

/** A shared queue that answers itself: holds each submission for `work`, records how many were inside
 *  it at once, and returns an answer that says it came from the queue. */
class RecordingQueue final : public InferenceBackend {
  public:
    explicit RecordingQueue(std::chrono::milliseconds work = 0ms) : work_(work) {}
    [[nodiscard]] auto submit(std::string) -> std::optional<InferenceOutcome> override {
        const int now = ++inside_;
        for (int seen = peak_.load(); now > seen && !peak_.compare_exchange_weak(seen, now);) {}
        ++submitted_;
        if (work_.count() > 0) std::this_thread::sleep_for(work_);
        --inside_;
        const Answer answer{Verdict::Params, "<params>{\"from\":\"queue\"}</params>"};
        return InferenceOutcome{.ok = true,
                                .text = options_calculator::encoder_queue::encode_answer(answer),
                                .error = {}};
    }
    [[nodiscard]] auto name() const noexcept -> std::string_view override { return "recording"; }
    std::atomic<int> inside_{0};
    std::atomic<int> peak_{0};
    std::atomic<int> submitted_{0};

  private:
    std::chrono::milliseconds work_;
};

[[nodiscard]] auto from_queue(const Answer& a) -> bool { return a.text.contains("\"from\":\"queue\""); }

/** An admission object that always returns what it was told to, for the degrade paths. */
class FixedBackend final : public InferenceBackend {
  public:
    explicit FixedBackend(InferenceOutcome outcome) : outcome_(std::move(outcome)) {}
    [[nodiscard]] auto submit(std::string) -> std::optional<InferenceOutcome> override {
        ++calls;
        return outcome_;
    }
    [[nodiscard]] auto name() const noexcept -> std::string_view override { return "fixed"; }
    std::atomic<int> calls{0};

  private:
    InferenceOutcome outcome_;
};

/** An executor that throws, to prove a runner fulfils the promise anyway. */
class ThrowingBackend final : public InferenceBackend {
  public:
    [[nodiscard]] auto submit(std::string) -> std::optional<InferenceOutcome> override {
        throw std::runtime_error("boom");
    }
    [[nodiscard]] auto name() const noexcept -> std::string_view override { return "throwing"; }
};

}  // namespace

int main() {
    logger::Logger::getInstance().initialize(log_path().string(), logger::LogLevel::INFO, false);

    section("the wire codec round-trips anything a visitor can type");
    {
        const std::vector<EncoderRequest> requests{
            {"amortize 480000 at 6.5% for 30 years", "", ""},
            {"What's the payment on a $420,000 loan at 6.5%?", "Over how many years?", "30 years"},
            {"quotes \" backslash \\ newline \n tab \t unicode \xC3\xA9\xE2\x82\xAC", "", "x"},
            {"a bare control byte \x01 and a DEL \x7f", "\x1f", "\x02"},
            {"", "", ""},
        };
        for (const auto& request : requests) {
            const auto decoded = options_calculator::encoder_queue::decode_request(
                options_calculator::encoder_queue::encode_request(request));
            check(decoded.has_value() && *decoded == request,
                  "request round-trips: " + request.utterance.substr(0, 24));
        }
        for (const auto verdict : {Verdict::Params, Verdict::None, Verdict::Refused}) {
            const Answer answer{verdict, "text with \"quotes\" and \n lines"};
            const auto decoded = options_calculator::encoder_queue::decode_answer(
                options_calculator::encoder_queue::encode_answer(answer));
            check(decoded.has_value() && *decoded == answer, "answer round-trips for every verdict");
        }
        check(!options_calculator::encoder_queue::decode_request(
                   "<|im_start|>system\nYou convert a request into parameters.<|im_end|>")
                   .has_value(),
              "a DECODER prompt is not an encoder task -- a worker handed one fails it by name");
        check(!options_calculator::encoder_queue::decode_answer("not json").has_value(),
              "an undecodable answer is an error, not an empty answer");
        check(!options_calculator::encoder_queue::decode_answer(
                   R"({"verdict":"maybe","text":""})")
                   .has_value(),
              "an unknown verdict is an error rather than silently becoming one of the three");
    }

    section("EncoderBackend: one place decides what the chain's outcome means");
    {
        CountingChain chain;
        const auto backend = make_backend(chain);

        const auto params = backend->answer({"pay 1000", "q", "r"});
        check(params.verdict == Verdict::Params && params.text.starts_with("<params>") &&
                  params.text.ends_with("</params>"),
              "a parse is the same <params>...</params> block the decoder path produces");
        check(backend->answer({"none", "", ""}) == Answer{Verdict::None, {}},
              "<NONE> is its own verdict with no text");
        const auto refused = backend->answer({"refuse me", "", ""});
        check(refused.verdict == Verdict::Refused && refused.text.contains("straddles"),
              "a chain refusal keeps its reason, because the strategy surface shows it");

        const EncoderRequest request{"pay 1000", "q", "r"};
        auto via_wire = backend->submit(options_calculator::encoder_queue::encode_request(request));
        check(via_wire.has_value() && via_wire->ok &&
                  options_calculator::encoder_queue::decode_answer(via_wire->text) ==
                      std::expected<Answer, std::string>{backend->answer(request)},
              "submit(wire) is answer() on the wire -- the queue path cannot say anything else");
        auto garbage = backend->submit("<|im_start|>not ours");
        check(garbage.has_value() && !garbage->ok && !garbage->error.empty(),
              "a prompt that is not an encoder task is a populated failure, never a hang");
        check(std::string_view(backend->name()) == "test encoder", "it names itself for the logs");
    }

    section("answer() with no queue is today's in-process call");
    {
        CountingChain chain;
        EncoderService service{chain.make(), &render, "test encoder", Bounds{.local = 4, .queue = 4}};
        const EncoderRequest request{"pay 1000", "", ""};
        check(service.answer(request) == service.backend().answer(request),
              "local mode answers exactly what the backend's answer() gives");
        check(chain.runs->load() == 2, "and it ran the chain in this process, once per call");
    }

    section("a burst submitted through the queue is executed by the replicas that lease it");
    {
        InMemoryQueue queue;
        CountingChain submitter_chain;  // the SUBMITTING replica's own chain: must stay idle
        EncoderService submitter{submitter_chain.make(), &render, "submitter",
                                 Bounds{.local = 0, .queue = 64}};  // busy always; the queue bound is not in play

        CountingChain chain_a{.work = 3ms};
        CountingChain chain_b{.work = 3ms};
        EncoderService replica_a{chain_a.make(), &render, "replica A", Bounds{.local = 0, .queue = 64}};
        EncoderService replica_b{chain_b.make(), &render, "replica B", Bounds{.local = 0, .queue = 64}};
        // Non-owning views of the SAME queue object: it is the shared substrate.
        const auto source = std::shared_ptr<LeaseSource>(std::shared_ptr<void>{}, &queue);
        replica_a.serve(source);
        replica_b.serve(source);

        constexpr int kRequests = 48;
        std::vector<Answer> got(kRequests);
        std::vector<Answer> want(kRequests);
        {
            std::vector<std::jthread> callers;
            for (int t = 0; t < 8; ++t) {
                callers.emplace_back([&, t] {
                    for (int i = t; i < kRequests; i += 8) {
                        const EncoderRequest request{
                            i % 7 == 0 ? "refuse " + std::to_string(i)
                                       : (i % 5 == 0 ? "none" : "pay " + std::to_string(i)),
                            "q" + std::to_string(i), "r" + std::to_string(i)};
                        got[i] = submitter.answer(queue, request);
                        want[i] = submitter.backend().answer(request);
                    }
                });
            }
        }  // jthreads join

        check(got == want,
              "every answer that came back through the queue equals the one the process gives "
              "itself (params, <NONE> and refusals alike)");
        check(log_lines_containing(kSpilled) == kRequests,
              "and the service SAID so for each of them -- the positive control for the "
              "degraded-path count below, which proves nothing if this line never appears");
        check(queue.submitted() == kRequests,
              "every request was SUBMITTED to the queue -- none bypassed it");
        const int a = chain_a.runs->load();
        const int b = chain_b.runs->load();
        check(a + b == kRequests, "replica A + replica B executed exactly what was submitted");
        check(a > 0 && b > 0, "and BOTH did -- the work was shared, not served by one");
        // submitter_chain ran once per `want[i]` above and never for a queued answer.
        check(submitter_chain.runs->load() == kRequests,
              "the submitting replica ran its chain only for the reference answer, never to "
              "answer through the queue");
    }

    section("a refusal from the chain completes the task");
    {
        InMemoryQueue queue;
        CountingChain chain;
        EncoderService worker{chain.make(), &render, "replica", Bounds{.local = 0, .queue = 4}};
        worker.serve(std::shared_ptr<LeaseSource>(std::shared_ptr<void>{}, &queue));
        const auto raw = queue.submit(options_calculator::encoder_queue::encode_request(
            {"refuse this", "", ""}));
        check(raw.has_value() && raw->ok,
              "the task is COMPLETED, not failed -- a deterministic refusal retried on another "
              "replica could only refuse again");
        const auto decoded = raw.has_value() ? options_calculator::encoder_queue::decode_answer(raw->text)
                                             : std::expected<Answer, std::string>{std::unexpected("none")};
        check(decoded.has_value() && decoded->verdict == Verdict::Refused,
              "and the submitter reads the refusal as a refusal");
    }

    section("a replica never has more requests on the queue than its bound, and answers the rest itself");
    {
        CountingChain chain;
        EncoderService service{chain.make(), &render, "test encoder", Bounds{.local = 0, .queue = 2}};
        RecordingQueue queue{30ms};
        constexpr int kCallers = 16;
        std::vector<Answer> got(kCallers);
        {
            std::vector<std::jthread> callers;
            for (int i = 0; i < kCallers; ++i) {
                callers.emplace_back([&, i] {
                    got[i] = service.answer(queue, {"pay " + std::to_string(i), "", ""});
                });
            }
        }
        const auto queued_answers = std::ranges::count_if(got, from_queue);
        check(queue.peak_.load() <= 2, "never more than the bound were inside the queue at once");
        check(queue.submitted_.load() >= 1, "and the queue was used while it had room");
        check(queue.submitted_.load() + chain.runs->load() == kCallers,
              "every caller was answered exactly once: through the queue or by this replica");
        check(queued_answers == queue.submitted_.load(),
              "the callers that went through the queue got ITS answer, the rest got their own");
        const int before = queue.submitted_.load();
        (void)service.answer(queue, {"pay 99", "", ""});
        (void)service.answer(queue, {"pay 100", "", ""});
        check(queue.submitted_.load() == before + 2,
              "and every slot was released: two more sequential requests both reach the queue -- a "
              "leaked slot is a replica that has quietly stopped using it");

        // A slot is released on the failure paths too, or one bad answer would starve the queue.
        FixedBackend failed{InferenceOutcome{.ok = false, .text = {}, .error = "cluster down"}};
        EncoderService one{chain.make(), &render, "test encoder", Bounds{.local = 0, .queue = 1}};
        for (int i = 0; i < 3; ++i) (void)one.answer(failed, {"pay 1", "", ""});
        check(failed.calls.load() == 3, "a failed submission releases its slot: all three tried");

        // The slot bounds what is OUTSTANDING ON THE QUEUE, so it is released the moment the queue has
        // answered or failed -- not held while the local fallback runs the chain. Hold the first
        // request inside its fallback; a second must still find the slot free and reach the queue.
        const std::array<InferenceOutcome, 2> unusable{
            InferenceOutcome{.ok = false, .text = {}, .error = "cluster down"},
            InferenceOutcome{.ok = true, .text = "not an encoded answer", .error = {}}};
        for (const auto& outcome : unusable) {
            CountingChain held{.gate = std::make_shared<std::latch>(1)};
            EncoderService fallback{held.make(), &render, "fallback replica", Bounds{.local = 0, .queue = 1}};
            FixedBackend broken{outcome};
            std::jthread first{[&] { (void)fallback.answer(broken, {"pay 1", "", ""}); }};
            const bool in_fallback = wait_until([&] { return held.active->load() == 1; });
            check(in_fallback && broken.calls.load() == 1,
                  "the first request tried the queue, failed, and is now running the chain locally");
            std::jthread second{[&] { (void)fallback.answer(broken, {"pay 2", "", ""}); }};
            const bool reached_queue = wait_until([&] { return broken.calls.load() == 2; });
            held.gate->count_down();  // release both fallbacks before judging, so a failure cannot hang
            first.join();
            second.join();
            check(reached_queue,
                  "while it did, the queue slot was free: the second request reached the queue too ("
                  "an unusable answer: " + std::string(outcome.ok ? "undecodable" : "refused") + ")");
        }
        InFlightCount exact;
        {
            const auto first = exact.try_enter(1);
            check(first.has_value() && !exact.try_enter(1).has_value() && exact.load() == 1,
                  "the bound is exact: a bound of one admits one and refuses the second");
        }
        check(exact.load() == 0, "and leaving is the token's destructor: the count is back to 0");
        check(!InFlightCount{}.try_enter(0).has_value(), "and a bound of zero admits nothing");

        // The token cannot be copied, assigned or left by hand, so the count cannot be unbalanced.
        using Entered = InFlightCount::Entered;
        static_assert(!std::is_copy_constructible_v<Entered> && !std::is_copy_assignable_v<Entered> &&
                      !std::is_move_assignable_v<Entered> && std::is_nothrow_move_constructible_v<Entered>);
        static_assert(!CanLeaveByHand<InFlightCount>);
        {
            auto moved_from = exact.enter();
            check(exact.load() == 1, "an unconditional enter is counted");
            const Entered moved_to{std::move(moved_from)};
            check(exact.load() == 1, "moving the token transfers the entry: it is not counted twice");
        }
        check(exact.load() == 0, "and the moved-from token leaves nothing behind: left exactly once");
    }

    // ROUTING: a request is answered IN-PROCESS and spills to the queue only when the replica is
    // BUSY. The first routing sent every request to the queue first, which at the deployed 300 ms
    // Raft heartbeat made a one-visitor request ~600x slower than answering it.
    section("an idle replica answers in-process and never touches the queue");
    {
        CountingChain chain;
        EncoderService service{chain.make(), &render, "idle replica", Bounds{.local = 2, .queue = 4}};
        RecordingQueue queue;
        const int in_process0 = log_lines_containing(kInProcess);
        const int spilled0 = log_lines_containing(kSpilled);
        const int degraded0 = log_lines_containing(kDegradedLocally);
        constexpr int kRequests = 10;
        for (int i = 0; i < kRequests; ++i) {
            const EncoderRequest request{"pay " + std::to_string(i), "", ""};
            check(service.answer(queue, request) == service.backend().answer(request),
                  "request " + std::to_string(i) + " answered exactly as the chain gives it");
        }
        // The reference answers above ran the chain too: 10 via answer() + 10 as references.
        check(queue.submitted_.load() == 0,
              "NOTHING was submitted to the queue: at concurrency 1 a replica is never busy");
        check(log_lines_containing(kInProcess) - in_process0 == kRequests &&
                  log_lines_containing(kSpilled) - spilled0 == 0 &&
                  log_lines_containing(kDegradedLocally) - degraded0 == 0,
              "and the log says so: " + std::to_string(kRequests) +
                  " 'answered in-process', no 'spilled', no 'degraded'");
        check(service.backend().in_flight() == 0, "and nothing is left counted in flight");
    }

    section("a busy replica spills to the queue, and what it counts as busy is its own chain");
    {
        // Busy = `local` chain executions in flight. One long request holds the only local slot;
        // the next arrival must go to the queue, not wait and not run beside it.
        CountingChain chain{.gate = std::make_shared<std::latch>(1)};
        EncoderService service{chain.make(), &render, "busy replica", Bounds{.local = 1, .queue = 4}};
        RecordingQueue queue;
        const int in_process0 = log_lines_containing(kInProcess);
        const int spilled0 = log_lines_containing(kSpilled);

        Answer first;
        std::jthread holder{[&] { first = service.answer(queue, {"pay 1", "", ""}); }};
        const bool entered = wait_until([&] { return chain.active->load() != 0; });
        check(entered, "the first request entered the chain (the replica was idle)");
        check(service.backend().in_flight() == 1, "the measured signal reads 1 while the chain executes");
        const auto second = service.answer(queue, {"pay 2", "", ""});
        chain.gate->count_down();  // only now may the first request leave the chain
        holder.join();

        check(from_queue(second) && queue.submitted_.load() == 1,
              "the second request, arriving while the first held the only local slot, went to the queue");
        check(!from_queue(first) && first == service.backend().answer({"pay 1", "", ""}),
              "and the first was answered in-process, by the chain");
        check(log_lines_containing(kInProcess) - in_process0 == 1 &&
                  log_lines_containing(kSpilled) - spilled0 == 1,
              "one line each: 'answered in-process' and 'spilled to the queue'");

        // WORK LEASED FROM THE QUEUE COUNTS: a core spent on another replica's parse is a core this
        // replica's own request does not have, so "busy" is what the replica is actually doing.
        CountingChain leased_chain{.gate = std::make_shared<std::latch>(1)};
        EncoderService leasing{leased_chain.make(), &render, "leasing replica", Bounds{.local = 1, .queue = 4}};
        RecordingQueue queue2;
        std::jthread leased{[&] {
            (void)leasing.backend().submit(options_calculator::encoder_queue::encode_request({"leased", "", ""}));
        }};
        check(wait_until([&] { return leased_chain.active->load() != 0; }),
              "the leased job entered the chain");
        const auto own = leasing.answer(queue2, {"pay 3", "", ""});
        leased_chain.gate->count_down();
        leased.join();
        check(from_queue(own) && queue2.submitted_.load() == 1,
              "executing a leased job makes the replica busy for its OWN requests too");
        check(service.backend().in_flight() == 0 && leasing.backend().in_flight() == 0,
              "and every execution left the count: nothing leaks");
    }

    section("above the local bound the overflow spills; the local bound is never exceeded");
    {
        CountingChain chain{.work = 10ms};
        EncoderService service{chain.make(), &render, "burst replica", Bounds{.local = 2, .queue = 64}};
        RecordingQueue queue{5ms};
        const int in_process0 = log_lines_containing(kInProcess);
        const int spilled0 = log_lines_containing(kSpilled);
        constexpr int kCallers = 24;
        std::vector<Answer> got(kCallers);
        {
            // Everyone leaves the gate together: staggered thread start-up on a loaded host could
            // otherwise let each request finish before the next arrives, and nothing would be busy.
            std::latch gate{kCallers};
            std::vector<std::jthread> callers;
            for (int i = 0; i < kCallers; ++i) {
                callers.emplace_back([&, i] {
                    gate.arrive_and_wait();
                    got[i] = service.answer(queue, {"pay " + std::to_string(i), "", ""});
                });
            }
        }
        const int in_process = log_lines_containing(kInProcess) - in_process0;
        const int spilled = log_lines_containing(kSpilled) - spilled0;
        check(chain.peak->load() <= 2,
              "never more than the local bound were inside the chain at once (peak " +
                  std::to_string(chain.peak->load()) + ")");
        check(in_process >= 1 && spilled >= 1,
              "and both routes were used: " + std::to_string(in_process) + " in-process, " +
                  std::to_string(spilled) + " spilled");
        check(in_process + spilled == kCallers && queue.submitted_.load() == spilled &&
                  std::ranges::count_if(got, from_queue) == spilled,
              "every caller was answered exactly once, and the ones who spilled got the QUEUE's answer");

        // Busy with no queue slot: the replica does the work it was always able to do, and still
        // counts it -- the next arrival must not read an idle replica that is not.
        CountingChain crowded_chain{.work = 20ms};
        EncoderService crowded{crowded_chain.make(), &render, "crowded replica", Bounds{.local = 1, .queue = 0}};
        RecordingQueue unused;
        {
            std::latch gate{8};
            std::vector<std::jthread> callers;
            for (int i = 0; i < 8; ++i) {
                callers.emplace_back([&, i] {
                    gate.arrive_and_wait();
                    (void)crowded.answer(unused, {"pay " + std::to_string(i), "", ""});
                });
            }
        }
        check(unused.submitted_.load() == 0 && crowded_chain.runs->load() == 8,
              "with no queue slot a busy replica answers all 8 itself and submits nothing");
        check(crowded_chain.peak->load() > 1 && crowded.backend().in_flight() == 0,
              "(it exceeded its local bound to do so, because there was nowhere else to send them) "
              "and still left the count at 0");
    }

    section("0 is a value on both bounds");
    {
        CountingChain chain;
        // local 0: every request is busy, so every request goes to the queue first (the first routing).
        EncoderService queue_first{chain.make(), &render, "queue-first", Bounds{.local = 0, .queue = 4}};
        RecordingQueue queue;
        for (int i = 0; i < 3; ++i) (void)queue_first.answer(queue, {"pay 1", "", ""});
        check(queue.submitted_.load() == 3 && chain.runs->load() == 0,
              "a local bound of 0 spills every request: 3 submitted, the chain never ran here");

        // queue 0: this assistant never submits, however busy it is.
        EncoderService never{chain.make(), &render, "never-submit", Bounds{.local = 0, .queue = 0}};
        FixedBackend untouched{InferenceOutcome{.ok = false, .text = {}, .error = "must not be called"}};
        for (int i = 0; i < 3; ++i) (void)never.answer(untouched, {"pay 1", "", ""});
        check(untouched.calls.load() == 0 && chain.runs->load() == 3,
              "a queue bound of 0 never submits: the queue is untouched and the chain ran 3 times");
    }

    section("a queue that cannot answer costs nothing but the queue");
    {
        CountingChain chain;
        EncoderService service{chain.make(), &render, "test encoder", Bounds{.local = 0, .queue = 4}};
        const EncoderRequest request{"pay 1000", "", ""};

        // The honest stand-in for "the cluster is unreachable": a client with no peers.
        options_calculator::inference_admission::SgeeAdmission unreachable{
            SgeeQueueClient{}, options_calculator::inference_queue::Surface::Mortgage,
            service.backend(), 90s};
        const auto started = std::chrono::steady_clock::now();
        const auto answer = service.answer(unreachable, request);
        const auto elapsed = std::chrono::steady_clock::now() - started;
        check(answer == service.backend().answer(request),
              "an unreachable SGEE cluster still yields the same answer");
        const auto raw = unreachable.submit(options_calculator::encoder_queue::encode_request(request));
        check(raw.has_value() && raw->ok && raw->degraded,
              "and the admission object says the outcome was a DEGRADE, not the queue's answer");
        check(elapsed < 5s, "and promptly, not after the 90 s remote deadline");

        // THE ACCOUNTING LINE. An admission object degrades by returning `local_.submit()`, whose
        // outcome is ok == true -- the same bytes a queue success has -- so a request answered right
        // here used to be logged as "answered through the shared queue", and the operator's
        // `queued == executed` accounting lied by exactly the number of fallbacks.
        constexpr int kFallbacks = 3;
        const int through_before = log_lines_containing(kSpilled);
        const int degraded_before = log_lines_containing(kDegradedLocally);
        for (int i = 0; i < kFallbacks; ++i) (void)service.answer(unreachable, request);
        check(log_lines_containing(kSpilled) - through_before == 0,
              "with the queue unreachable NO request is reported as answered through it");
        check(log_lines_containing(kDegradedLocally) - degraded_before == kFallbacks,
              "and each of the " + std::to_string(kFallbacks) + " that were answered here says so");

        FixedBackend failed{InferenceOutcome{.ok = false, .text = {}, .error = "cluster down"}};
        check(service.answer(failed, request) == service.backend().answer(request),
              "a queue that reports failure is answered locally");
        FixedBackend garbled{InferenceOutcome{.ok = true, .text = "not an answer", .error = {}}};
        check(service.answer(garbled, request) == service.backend().answer(request),
              "a queue that returns an undecodable answer is answered locally");
    }

    section("LeaseRunner never leaves a leased job unanswered");
    {
        ThrowingBackend thrower;
        InMemoryQueue queue;
        const auto source = std::shared_ptr<LeaseSource>(std::shared_ptr<void>{}, &queue);
        LeaseRunner runner{source, thrower, "throwing replica"};
        const auto outcome = queue.submit("anything");
        check(outcome.has_value() && !outcome->ok && outcome->error == "boom",
              "an executor that throws yields a populated failure, not a broken promise");

        CountingChain chain;
        const auto executor = make_backend(chain);
        const auto started = std::chrono::steady_clock::now();
        {
            InMemoryQueue idle;
            const auto idle_source = std::shared_ptr<LeaseSource>(std::shared_ptr<void>{}, &idle);
            LeaseRunner idle_runner{idle_source, *executor, "idle replica"};
        }
        check(std::chrono::steady_clock::now() - started < 2s,
              "an idle runner stops promptly when its owner goes away");
    }

    section("the two bounds are read strictly: 0 is a value, a typo is an error");
    {
        using options_calculator::encoder_queue::kDefaultLocalMaxInFlight;
        using options_calculator::encoder_queue::kDefaultMaxInFlight;
        using options_calculator::encoder_queue::parse_in_flight_bound;
        const auto queue_bound = [](const std::optional<std::string>& raw) {
            return parse_in_flight_bound("ENCODER_QUEUE_MAX_IN_FLIGHT", raw, kDefaultMaxInFlight,
                                         "never submit");
        };
        const auto local_bound = [](const std::optional<std::string>& raw) {
            return parse_in_flight_bound("ENCODER_LOCAL_MAX_IN_FLIGHT", raw, kDefaultLocalMaxInFlight,
                                         "every request spills to the queue first");
        };
        using Parsed = std::expected<std::size_t, std::string>;

        check(queue_bound(std::nullopt) == Parsed{kDefaultMaxInFlight}, "unset reads as the queue default");
        check(local_bound(std::nullopt) == Parsed{kDefaultLocalMaxInFlight}, "and as the local default");
        check(queue_bound("") == Parsed{kDefaultMaxInFlight}, "an empty value reads as unset");
        check(queue_bound("0") == Parsed{0},
              "0 is accepted as 'this assistant never submits' -- a bound of 0 admits nothing");
        check(local_bound("0") == Parsed{0},
              "and for the local bound as 'every request is busy' -- the queue-first routing");
        check(local_bound("64") == Parsed{64}, "64 reads as 64");
        for (const std::string bad : {"abc", " 64", "64 ", "-1", "+3", "1.5", "0x10", "6 4", "99999999999999999999"}) {
            const auto result = queue_bound(bad);
            check(!result.has_value() && result.error().contains(std::string("\"") + bad + "\"") &&
                      result.error().contains("ENCODER_QUEUE_MAX_IN_FLIGHT"),
                  "'" + bad + "' is refused, and the message names the variable and the value");
        }
        const auto wrong = local_bound("many");
        check(!wrong.has_value() && wrong.error().contains("ENCODER_LOCAL_MAX_IN_FLIGHT") &&
                  wrong.error().contains("spills to the queue first"),
              "and the local bound's message says what 0 would have meant for it");
    }

    section("a build is fingerprinted from its bytes, and an unreadable one isolates itself");
    {
        using options_calculator::encoder_queue::build_fingerprint;
        const auto dir = std::filesystem::temp_directory_path() /
                         ("test_encoder_queue_fp_" + std::to_string(::getpid()));
        std::filesystem::create_directories(dir);
        const auto write = [&](const char* name, std::string_view bytes) {
            std::ofstream{dir / name, std::ios::binary} << bytes;
            return dir / name;
        };
        const auto model_a = write("a.gguf", "weights, schema and tokenizer of build A");
        const auto model_a2 = write("a_copy.gguf", "weights, schema and tokenizer of build A");
        const auto model_b = write("b.gguf", "weights, schema and tokenizer of build B");
        const auto exe_1 = write("exe1", "code of binary one");
        const auto exe_2 = write("exe2", "code of binary two");

        check(build_fingerprint(model_a, exe_1) == build_fingerprint(model_a2, exe_1),
              "the same bytes under another path are the same build -- it is the CONTENT that counts");
        check(build_fingerprint(model_a, exe_1) != build_fingerprint(model_b, exe_1),
              "a retrained model (same code) is another build");
        check(build_fingerprint(model_a, exe_1) != build_fingerprint(model_a, exe_2),
              "a changed binary (same model) is another build -- the renderer lives in the code");
        const auto running = build_fingerprint(model_a);
        check(!running.starts_with("unshared.") && running == build_fingerprint(model_a),
              "this process's own executable is readable, and fingerprints the same every time");

        const auto isolated_1 = build_fingerprint(model_a, dir / "no-such-binary");
        const auto isolated_2 = build_fingerprint(model_a, dir / "no-such-binary");
        check(isolated_1.starts_with("unshared.") && isolated_1 != isolated_2,
              "a build that cannot be identified shares with NO ONE, not even an identical twin");
        check(build_fingerprint(dir / "no-such-model", exe_1).starts_with("unshared."),
              "and so does one whose model cannot be read");
        std::filesystem::remove_all(dir);
    }

    section("AssistantRuntime: what executes here, what is available, and a queue that cannot be built stays local");
    {
        using options_calculator::assistant_runtime::AssistantRuntime;
        using options_calculator::inference_queue::Surface;
        const auto encoder_service = [] {
            static CountingChain chain;
            return std::make_unique<EncoderService>(chain.make(), &render, "test encoder",
                                                    Bounds{.local = 4, .queue = 4});
        };

        AssistantRuntime bare{Surface::Mortgage, "Mortgage assistant"};
        check(!bare.available() && !bare.holds_model() && !bare.queued() && !bare.encoder().has_value() &&
                  !bare.decoder().has_value(),
              "a runtime that adopted nothing is unavailable, holds no model and has no queue");
        const auto refused = bare.submit("a decoder prompt");
        check(refused.has_value() && !refused->ok && refused->error == "model not loaded",
              "and a decoder prompt is a populated failure, never a wait on a thread that does not exist");

        AssistantRuntime with_encoder{Surface::Strategy, "Strategy assistant"};
        with_encoder.adopt(encoder_service());
        const EncoderRequest request{"pay 1", "q", "r"};
        check(with_encoder.available() && with_encoder.holds_model() && !with_encoder.queued(),
              "an encoder makes it available AND loaded -- the two questions are answered separately "
              "and forgetting either was a measured outage");
        check(with_encoder.answer(request) == with_encoder.encoder()->get().backend().answer(request),
              "and with no queue it answers in this process, exactly as the backend does");

        // Every way of asking for a queue that cannot be built must leave a working local assistant:
        // a cluster that cannot be reached costs the shared queue and nothing else.
        ::unsetenv("DATABASE_URL");
        ::unsetenv("SGEE_PEERS");
        for (const char* mode : {"nonsense", "postgres", "sgee"}) {
            ::setenv("INFERENCE_QUEUE", mode, 1);
            AssistantRuntime runtime{Surface::Mortgage, "Mortgage assistant"};
            runtime.adopt(encoder_service());
            runtime.configure_queue();
            check(!runtime.queued() && runtime.available(),
                  std::string("INFERENCE_QUEUE=") + mode +
                      " that cannot be configured leaves the assistant on local-only inference");
        }
        ::unsetenv("INFERENCE_QUEUE");
    }

    section("a lease source that throws costs a tick, not the process");
    {
        // fill() allocates and spawns a write-back thread for each job it leases; either can throw
        // under a container's thread or memory limit. The runner's thread is a std::jthread, so an
        // exception escaping it is std::terminate -- both assistants and the calculator with them.
        class ThrowOnceSource final : public LeaseSource {
          public:
            explicit ThrowOnceSource(LeaseSource& inner) : inner_(inner) {}
            [[nodiscard]] auto fill(std::size_t want) -> std::vector<PendingJob> override {
                if (!thrown_.exchange(true)) throw std::runtime_error("fill boom");
                return inner_.fill(want);
            }
            [[nodiscard]] auto thrown() const -> bool { return thrown_.load(); }

          private:
            LeaseSource& inner_;
            std::atomic<bool> thrown_{false};
        };

        InMemoryQueue queue;
        auto source = std::make_shared<ThrowOnceSource>(queue);
        CountingChain chain;
        const auto executor = make_backend(chain);
        LeaseRunner runner{source, *executor, "fragile replica"};
        const auto outcome = queue.submit(
            options_calculator::encoder_queue::encode_request({"pay 1", "", ""}));
        check(source->thrown(), "the source did throw (the case is real, not vacuous)");
        check(outcome.has_value() && outcome->ok && chain.runs->load() == 1,
              "and the runner kept going: the next job was leased and executed");
    }

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
