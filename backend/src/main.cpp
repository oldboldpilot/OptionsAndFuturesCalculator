#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <grpcpp/grpcpp.h>


import calculator_service;
import finance_service;
import assistant_service;
import api_key;

namespace {

/**
 * Largest request the engine will deserialize, in bytes.
 *
 * This has to sit at the TRANSPORT layer, in front of the parser, because the
 * quota guard runs at the top of each RPC -- which is to say AFTER protobuf has
 * already deserialized the message. By the time a call can be priced and
 * refused, its payload is resident. A size limit applied any later than this
 * would be metering memory that has already been allocated.
 *
 * 1 MiB against gRPC's 4 MiB default. Every request on both services is a
 * handful of scalars plus, at worst, a cash-flow or returns array; the largest
 * legitimate message observed is a few kilobytes. Stating it here also means a
 * future change cannot silently raise it by inheriting a library default.
 */
constexpr int kMaxRequestBytes = 1 * 1024 * 1024;

}  // namespace

auto RunServer() -> void {
    std::string server_address("0.0.0.0:50051");
    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.SetMaxReceiveMessageSize(kMaxRequestBytes);

    options_calculator::service::RegisterCalculatorService(builder);

    // The sensen financial library, on the same port under its own contract.
    // Two services, one process: a caller that wants amortization schedules or
    // bond analytics does not have to know this application exists, and this
    // application does not have to grow a second deployment to offer them.
    options_calculator::finance::RegisterFinanceService(builder);

    // The strategy assistant, third on the same port. Unlike the two above it
    // is OPTIONAL: it needs a fine-tuned model on disk, named by MODEL_PATH, and
    // an image may legitimately be built without one. Registration therefore
    // never throws on a missing or unloadable model -- the service registers
    // regardless and answers every call with a refusal saying it is
    // unavailable. A calculator that stops serving payoff curves because an
    // assistant could not find its weights would be trading the product for a
    // feature, so the failure is reported per-call rather than at startup.
    options_calculator::assistant::RegisterAssistantService(builder);

    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());

    // State the security posture at startup, so "is authentication on?" is
    // answerable from the logs without sending traffic at it -- the same reason
    // the quota module logs what it loaded.
    const auto& keys = options_calculator::auth::KeyRegistry::instance();
    if (!keys.enabled()) {
        std::cout << "API key auth: DISABLED (FINANCE_API_KEYS unset)" << std::endl;
    } else {
        const auto mode = keys.mode();
        std::cout << "API key auth: " << keys.key_count() << " keys, mode "
                  << (mode == options_calculator::auth::Mode::Enforce  ? "ENFORCE"
                      : mode == options_calculator::auth::Mode::Warn   ? "WARN"
                                                                      : "OBSERVE")
                  << std::endl;
    }
    std::cout << "Max request size: " << kMaxRequestBytes << " bytes" << std::endl;
    std::cout << "Server listening on " << server_address << std::endl;

    server->Wait();
}

// Defined in issue_key.cpp. Declared rather than imported because main.cpp is a
// plain translation unit, and this is the only thing it needs from that file.
auto IssueKeyMain(int argc, char** argv) -> int;

auto main(int argc, char** argv) -> int {
    try {
        // Key minting lives in the SERVER binary so it necessarily uses the same
        // hashing and the same key format as the verification path. A separate
        // tool could drift, and the symptom of that drift would be a customer
        // whose brand-new key is rejected as a forgery.
        if (argc > 1 && std::string_view{argv[1]} == "issue-key") {
            return IssueKeyMain(argc, argv);
        }
        RunServer();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
}
