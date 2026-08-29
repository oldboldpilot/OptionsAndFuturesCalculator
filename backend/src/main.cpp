#include <cstdlib> // std::getenv, std::strtol
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <grpcpp/grpcpp.h>


import calculator_service;
import finance_service;
import assistant_service;
import mortgage_assistant_service;
import api_key;
import fips_mode;

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

/// Listening port when `ENGINE_GRPC_PORT` is unset or unusable. This is the
/// port `backend/envoy.yaml` proxies to, so the default is load-bearing.
constexpr int kDefaultPort = 50051;

}  // namespace

/**
 * The gRPC port to listen on: `ENGINE_GRPC_PORT` if it names a valid one,
 * else 50051.
 *
 * **Deliberately NOT `PORT`.** Railway injects `PORT=8080` into this service,
 * and 8080 is ENVOY's public listener -- Envoy proxies inbound gRPC-Web to
 * this engine on 50051 (`backend/envoy.yaml`). Reading `PORT` here would move
 * the engine onto Envoy's own port: the two would collide, and Envoy would go
 * on proxying to a 50051 nobody is listening on. That is a full outage for
 * both live sites, produced by a variable that looks like it means "the port
 * this program listens on" and does not.
 *
 * The variable exists because the address was previously a hardcoded literal,
 * so two engines started with different ports both bound 50051 -- and because
 * the listener uses SO_REUSEPORT the kernel silently SPLIT requests between
 * them instead of failing the second bind. Both processes look healthy, both
 * log "Server listening", and a caller aiming at one gets answers from
 * whichever the kernel picked. That is the "several engines each holding a
 * different model" trap this project has already been bitten by, and it is
 * what makes a split-fleet test on one host possible to run honestly.
 *
 * Out-of-range or non-numeric values fall back to the default rather than
 * failing the boot, matching how every other optional variable here behaves.
 */
[[nodiscard]] auto resolve_port() -> int {
    const char* raw = std::getenv("ENGINE_GRPC_PORT");
    if (raw == nullptr || *raw == '\0') {
        return kDefaultPort;
    }
    char* end = nullptr;
    const long parsed = std::strtol(raw, &end, 10);
    if (end == raw || *end != '\0' || parsed < 1 || parsed > 65535) {
        std::cerr << "ENGINE_GRPC_PORT=\"" << raw << "\" is not a valid port; using " << kDefaultPort
                  << '\n';
        return kDefaultPort;
    }
    return static_cast<int>(parsed);
}

auto RunServer() -> void {
    std::string server_address("0.0.0.0:" + std::to_string(resolve_port()));
    grpc::ServerBuilder builder;
    // BEFORE the listener binds. Under FIPS_MODE=required a failure must stop
    // the process, and a process that has already accepted a request cannot
    // un-accept it -- serving one call with unapproved cryptography and exiting
    // afterwards is, to the caller holding that answer, the same as no gate.
    {
        const auto fips_status = fips::apply_from_environment();
        std::cout << fips::describe(fips_status) << std::endl;
        if (fips_status.requested == fips::Mode::Required &&
            !(fips_status.fips_provider_loaded && fips_status.default_properties_fips)) {
            std::cerr << "FATAL: FIPS_MODE=required and FIPS is not in force. Refusing to start."
                      << std::endl;
            // std::exit, not a return: RunServer() returns void, and this must
            // stop the PROCESS. Refusing to bind while letting main() carry on
            // would leave a container that looks alive to Railway's healthcheck
            // and serves nothing -- the shape this repo has already been bitten
            // by, where a deployment reported SUCCESS while crash-looping.
            std::exit(1);
        }
    }

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

    // The mortgage / time-value-of-money assistant, fourth on the same port and
    // optional on exactly the same terms: it needs its OWN fine-tuned model,
    // named by MORTGAGE_MODEL_PATH, and an image may legitimately be built with
    // one assistant's weights, both, or neither. It deliberately does not fall
    // back to MODEL_PATH -- that names the STRATEGY model, and loading it here
    // would put a model trained on option strategies behind a mortgage
    // contract. Registration never throws on a missing or unloadable model; the
    // service registers regardless and answers every call with a refusal saying
    // it is unavailable.
    //
    // Envoy needs no change to reach it: the route in backend/envoy.yaml is a
    // catch-all `- match: { prefix: "/" }` onto the one gRPC cluster, matched by
    // prefix precisely so a new service on this port is routed without touching
    // the proxy.
    options_calculator::mortgage_assistant::RegisterMortgageAssistantService(builder);

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
