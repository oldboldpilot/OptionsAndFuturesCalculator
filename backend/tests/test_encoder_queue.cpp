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
using options_calculator::encoder_queue::QueueSlots;
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

/** The two per-request lines `EncoderService::answer` writes, and the only two a request that
 *  reached the shared queue can produce. */
constexpr std::string_view kThroughQueue = "answered through the shared queue";
constexpr std::string_view kDegradedLocally = "answered locally after the shared queue degraded";

using ChainResult = std::expected<std::optional<Parsed>, std::string>;

/** A chain with the three outcomes the real one has, selected by the utterance, and a counter of
 *  how many times THIS instance ran -- the only way to say which replica did the work. */
struct CountingChain {
    std::shared_ptr<std::atomic<int>> runs = std::make_shared<std::atomic<int>>(0);
    std::chrono::milliseconds work{0};

    [[nodiscard]] auto make() const -> EncoderBackend::Chain {
        return [runs = runs, work = work](const Turns& turns) -> ChainResult {
            runs->fetch_add(1);
            if (work.count() > 0) std::this_thread::sleep_for(work);
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
        EncoderService service{chain.make(), &render, "test encoder", 4};
        const EncoderRequest request{"pay 1000", "", ""};
        check(service.answer(nullptr, request) == service.backend().answer(request),
              "local mode answers exactly what the backend's answer() gives");
        check(chain.runs->load() == 2, "and it ran the chain in this process, once per call");
    }

    section("a burst submitted through the queue is executed by the replicas that lease it");
    {
        InMemoryQueue queue;
        CountingChain submitter_chain;  // the SUBMITTING replica's own chain: must stay idle
        EncoderService submitter{submitter_chain.make(), &render, "submitter", 64};  // no bound in play

        CountingChain chain_a{.work = 3ms};
        CountingChain chain_b{.work = 3ms};
        EncoderService replica_a{chain_a.make(), &render, "replica A", 64};
        EncoderService replica_b{chain_b.make(), &render, "replica B", 64};
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
                        got[i] = submitter.answer(&queue, request);
                        want[i] = submitter.backend().answer(request);
                    }
                });
            }
        }  // jthreads join

        check(got == want,
              "every answer that came back through the queue equals the one the process gives "
              "itself (params, <NONE> and refusals alike)");
        check(log_lines_containing(kThroughQueue) == kRequests,
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
        EncoderService worker{chain.make(), &render, "replica", 4};
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
        // A queue backend that holds each submission for a while and records how many were inside
        // it at once -- the quantity the bound exists to cap.
        class SlowCountingQueue final : public InferenceBackend {
          public:
            [[nodiscard]] auto submit(std::string) -> std::optional<InferenceOutcome> override {
                const int now = ++inside_;
                int seen = peak_.load();
                while (now > seen && !peak_.compare_exchange_weak(seen, now)) {}
                ++submitted_;
                std::this_thread::sleep_for(30ms);
                --inside_;
                const Answer answer{Verdict::Params, "<params>{\"from\":\"queue\"}</params>"};
                return InferenceOutcome{.ok = true,
                                        .text = options_calculator::encoder_queue::encode_answer(answer),
                                        .error = {}};
            }
            [[nodiscard]] auto name() const noexcept -> std::string_view override { return "slow"; }
            std::atomic<int> inside_{0};
            std::atomic<int> peak_{0};
            std::atomic<int> submitted_{0};
        };

        CountingChain chain;
        EncoderService service{chain.make(), &render, "test encoder", 2};
        SlowCountingQueue queue;
        constexpr int kCallers = 16;
        std::vector<Answer> got(kCallers);
        {
            std::vector<std::jthread> callers;
            for (int i = 0; i < kCallers; ++i) {
                callers.emplace_back([&, i] {
                    got[i] = service.answer(&queue, {"pay " + std::to_string(i), "", ""});
                });
            }
        }
        const auto queued_answers = std::ranges::count_if(
            got, [](const Answer& a) { return a.text.contains("\"from\":\"queue\""); });
        check(queue.peak_.load() <= 2, "never more than the bound were inside the queue at once");
        check(queue.submitted_.load() >= 1, "and the queue was used while it had room");
        check(queue.submitted_.load() + chain.runs->load() == kCallers,
              "every caller was answered exactly once: through the queue or by this replica");
        check(queued_answers == queue.submitted_.load(),
              "the callers that went through the queue got ITS answer, the rest got their own");
        const int before = queue.submitted_.load();
        (void)service.answer(&queue, {"pay 99", "", ""});
        (void)service.answer(&queue, {"pay 100", "", ""});
        check(queue.submitted_.load() == before + 2,
              "and every slot was released: two more sequential requests both reach the queue -- a "
              "leaked slot is a replica that has quietly stopped using it");

        // A slot is released on the failure paths too, or one bad answer would starve the queue.
        FixedBackend failed{InferenceOutcome{.ok = false, .text = {}, .error = "cluster down"}};
        EncoderService one{chain.make(), &render, "test encoder", 1};
        for (int i = 0; i < 3; ++i) (void)one.answer(&failed, {"pay 1", "", ""});
        check(failed.calls.load() == 3, "a failed submission releases its slot: all three tried");
        QueueSlots exact{1};
        check(exact.try_acquire() && !exact.try_acquire(),
              "the bound is exact: a bound of one admits one and refuses the second");
    }

    section("a queue that cannot answer costs nothing but the queue");
    {
        CountingChain chain;
        EncoderService service{chain.make(), &render, "test encoder", 4};
        const EncoderRequest request{"pay 1000", "", ""};

        // The honest stand-in for "the cluster is unreachable": a client with no peers.
        options_calculator::inference_admission::SgeeAdmission unreachable{
            SgeeQueueClient{}, options_calculator::inference_queue::Surface::Mortgage,
            service.backend(), 90s};
        const auto started = std::chrono::steady_clock::now();
        const auto answer = service.answer(&unreachable, request);
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
        const int through_before = log_lines_containing(kThroughQueue);
        const int degraded_before = log_lines_containing(kDegradedLocally);
        for (int i = 0; i < kFallbacks; ++i) (void)service.answer(&unreachable, request);
        check(log_lines_containing(kThroughQueue) - through_before == 0,
              "with the queue unreachable NO request is reported as answered through it");
        check(log_lines_containing(kDegradedLocally) - degraded_before == kFallbacks,
              "and each of the " + std::to_string(kFallbacks) + " that were answered here says so");

        FixedBackend failed{InferenceOutcome{.ok = false, .text = {}, .error = "cluster down"}};
        check(service.answer(&failed, request) == service.backend().answer(request),
              "a queue that reports failure is answered locally");
        FixedBackend garbled{InferenceOutcome{.ok = true, .text = "not an answer", .error = {}}};
        check(service.answer(&garbled, request) == service.backend().answer(request),
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

    section("ENCODER_QUEUE_MAX_IN_FLIGHT is read strictly: 0 is a value, a typo is an error");
    {
        using options_calculator::encoder_queue::kDefaultMaxInFlight;
        using options_calculator::encoder_queue::parse_max_in_flight;
        const auto parsed = [](const char* raw) { return parse_max_in_flight(raw); };

        check(parsed(nullptr) == std::expected<std::size_t, std::string>{kDefaultMaxInFlight},
              "unset reads as the default");
        check(parsed("") == std::expected<std::size_t, std::string>{kDefaultMaxInFlight},
              "and so does an empty value");
        check(parsed("0") == std::expected<std::size_t, std::string>{0},
              "0 is accepted as 'this replica never submits' -- QueueSlots{0} refuses every acquire");
        check(parsed("64") == std::expected<std::size_t, std::string>{64}, "64 reads as 64");
        for (const char* bad : {"abc", " 64", "64 ", "-1", "+3", "1.5", "0x10", "6 4", "99999999999999999999"}) {
            const auto result = parsed(bad);
            check(!result.has_value() && result.error().contains(std::string("\"") + bad + "\""),
                  std::string("'") + bad + "' is refused, and the message names the value");
        }

        // A bound of zero must really mean it: not one request reaches the queue, every one is
        // answered here, and the log says each was answered in-process.
        CountingChain chain;
        EncoderService never{chain.make(), &render, "test encoder", 0};
        FixedBackend untouched{InferenceOutcome{.ok = false, .text = {}, .error = "must not be called"}};
        for (int i = 0; i < 3; ++i) (void)never.answer(&untouched, {"pay 1", "", ""});
        check(untouched.calls.load() == 0 && chain.runs->load() == 3,
              "with a bound of 0 the queue is never submitted to and the chain ran locally 3 times");
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
            return std::make_unique<EncoderService>(chain.make(), &render, "test encoder", 4);
        };

        AssistantRuntime bare{Surface::Mortgage, "Mortgage assistant"};
        check(!bare.available() && !bare.holds_model() && !bare.queued() && bare.encoder() == nullptr,
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
        check(with_encoder.answer(request) == with_encoder.encoder()->backend().answer(request),
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
