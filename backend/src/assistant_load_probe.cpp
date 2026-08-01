// Throwaway load harness: fires N concurrent ParseStrategy RPCs at a running
// engine and reports per-request latency, wall clock and correctness. Used to
// measure the assistant's continuous-batching behaviour at 1/2/4/8 concurrent
// users. A measurement harness, not a product surface: it speaks the same
// public gRPC contract any client would, so it can be pointed at a deployed
// engine as easily as a local one.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include <grpcpp/grpcpp.h>
#include "assistant.pb.h"
#include "assistant.grpc.pb.h"

namespace {

struct Result {
    bool ok = false;
    int outcome_case = 0;
    std::string detail;
    double ms = 0.0;
};

const char* kUtterances[] = {
    "Iron condor on SPY, 30 days out, one contract.",
    "Bull call spread on AAPL expiring in 45 days, 3 contracts.",
    "Bear put spread on TSLA, 60 days, 2 contracts.",
    "Covered call on NVDA 30 days out, 1 contract.",
    "Call butterfly on SPY in 21 days, 4 contracts.",
    "Iron condor on AAPL, 14 days, 2 contracts.",
    "Bull call spread on QQQ 30 days out, 1 contract.",
    "Straddle on QQQ expiring in 7 days, 5 contracts.",
};

}  // namespace

auto main(int argc, char** argv) -> int {
    const std::string target = (argc > 1) ? argv[1] : "localhost:50051";
    const int n = (argc > 2) ? std::atoi(argv[2]) : 1;

    std::vector<Result> results(static_cast<std::size_t>(n));
    std::vector<std::thread> threads;
    threads.reserve(static_cast<std::size_t>(n));

    // Every thread gets its own channel and stub so the measurement is of the
    // server's concurrency, not of contention inside one client channel.
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < n; ++i) {
        threads.emplace_back([&, i] {
            auto channel = grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
            auto stub = calculator::assistant::StrategyAssistant::NewStub(channel);

            calculator::assistant::ParseRequest req;
            req.set_utterance(kUtterances[i % 8]);
            calculator::assistant::ParseResponse resp;
            grpc::ClientContext ctx;
            ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(300));

            const auto s = std::chrono::steady_clock::now();
            const grpc::Status st = stub->ParseStrategy(&ctx, req, &resp);
            const auto e = std::chrono::steady_clock::now();

            Result r;
            r.ms = std::chrono::duration<double, std::milli>(e - s).count();
            if (!st.ok()) {
                r.ok = false;
                r.detail = "gRPC " + std::to_string(st.error_code()) + ": " + st.error_message();
            } else {
                r.ok = true;
                r.outcome_case = static_cast<int>(resp.outcome_case());
                if (resp.has_params()) {
                    r.detail = "PARAMS symbol=" + resp.params().symbol() +
                               " strategy=" + resp.params().strategy() +
                               " days=" + std::to_string(resp.params().expiration_days()) +
                               " qty=" + std::to_string(resp.params().quantity());
                } else if (resp.has_clarification()) {
                    r.detail = "CLARIFY " + resp.clarification().question();
                } else if (resp.has_refusal()) {
                    r.detail = "REFUSAL reason=" +
                               std::to_string(static_cast<int>(resp.refusal().reason())) + " " +
                               resp.refusal().message();
                } else {
                    r.detail = "EMPTY";
                }
            }
            results[static_cast<std::size_t>(i)] = std::move(r);
        });
    }
    for (auto& t : threads) t.join();
    const auto t1 = std::chrono::steady_clock::now();

    double sum = 0.0;
    double worst = 0.0;
    for (int i = 0; i < n; ++i) {
        const auto& r = results[static_cast<std::size_t>(i)];
        sum += r.ms;
        if (r.ms > worst) worst = r.ms;
        std::printf("[%d] %8.1f ms  %s\n", i, r.ms, r.detail.c_str());
    }
    const double wall = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::printf("N=%d wall=%.1f ms  avg_latency=%.1f ms  max_latency=%.1f ms\n", n, wall,
                sum / n, worst);
    return 0;
}
