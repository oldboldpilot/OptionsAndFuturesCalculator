/*
 * Adversarial probe client for the strategy assistant. LOCAL USE ONLY.
 *
 * Not part of the deploy gate -- this is a throwaway tool for the assistant
 * security review (see the task brief). Sends one ParseStrategy call and
 * prints the outcome plus wall-clock latency, so an attack prompt can be
 * fired and its result inspected without wiring up gRPC-Web or a Python
 * client. Reuses smoke_client's proto stubs; adds nothing to the shipped
 * binary graph beyond one more throwaway executable target.
 *
 *   ./adv_client <utterance> [prior_clarification] [host:port]
 */
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#include <grpcpp/grpcpp.h>
#include "assistant.pb.h"
#include "assistant.grpc.pb.h"

auto main(int argc, char** argv) -> int {
    if (argc < 2) {
        std::cerr << "usage: adv_client <utterance> [prior_clarification] [host:port]\n";
        return 2;
    }
    const std::string utterance = argv[1];
    const std::string prior = (argc > 2) ? argv[2] : "";
    const std::string target = (argc > 3) ? argv[3] : "localhost:50051";

    auto channel = grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
    auto stub = calculator::assistant::StrategyAssistant::NewStub(channel);

    calculator::assistant::ParseRequest req;
    req.set_utterance(utterance);
    req.set_prior_clarification(prior);
    calculator::assistant::ParseResponse res;

    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(180));

    const auto t0 = std::chrono::steady_clock::now();
    const auto status = stub->ParseStrategy(&ctx, req, &res);
    const auto t1 = std::chrono::steady_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    std::cout << "latency_ms=" << ms << "\n";
    if (!status.ok()) {
        std::cout << "GRPC_ERROR code=" << static_cast<int>(status.error_code())
                   << " message=" << status.error_message() << "\n";
        return 0;
    }

    switch (res.outcome_case()) {
        case calculator::assistant::ParseResponse::kParams: {
            const auto& p = res.params();
            std::cout << "OUTCOME=PARAMS symbol=" << p.symbol()
                       << " asset_class=" << p.asset_class() << " strategy=" << p.strategy()
                       << " expiration_days=" << p.expiration_days()
                       << " far_expiration_days=" << p.far_expiration_days()
                       << " quantity=" << p.quantity() << "\n";
            break;
        }
        case calculator::assistant::ParseResponse::kClarification:
            std::cout << "OUTCOME=CLARIFICATION question=" << res.clarification().question()
                       << "\n";
            break;
        case calculator::assistant::ParseResponse::kRefusal:
            std::cout << "OUTCOME=REFUSAL reason=" << static_cast<int>(res.refusal().reason())
                       << " message=" << res.refusal().message() << "\n";
            break;
        default:
            std::cout << "OUTCOME=UNSET\n";
            break;
    }
    return 0;
}
