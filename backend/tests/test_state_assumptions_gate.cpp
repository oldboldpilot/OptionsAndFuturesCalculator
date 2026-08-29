/**
 * The write gate on RefreshStateAssumptions, in BOTH directions.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * WHY THIS IS ITS OWN BINARY. `KeyRegistry` is a Meyers singleton that reads
 * `FINANCE_API_KEYS` once, at first use. A process therefore has exactly one
 * key configuration for its whole life, so a test needing keys configured
 * cannot share a binary with `test_finance_service_validation`, whose ~200
 * checks all call anonymously. The env is set at the top of main, before the
 * fixture starts the server and before anything touches the registry.
 *
 * WHY IT DISCRIMINATES ON TIER RATHER THAN ON BEING SIGNED IN. The interesting
 * case is not anonymous-versus-authenticated -- a gate written as
 * `if (!_id.authenticated)` would pass that test and still let every Pro
 * subscriber rewrite fifty live pages. So the refuse direction is exercised
 * with a VALID pro key, authenticated and quota-metered, and refused anyway.
 *
 * WHAT "ADMITTED" LOOKS LIKE. This binary registers the finance service with no
 * refresh hook (that is `main.cpp`'s job and it needs libpq), so a partner
 * credential reaches the next layer and is refused there with
 * FAILED_PRECONDITION and a different, named reason. That is a clean admit
 * signal: PERMISSION_DENIED means the gate stopped the caller,
 * FAILED_PRECONDITION means it did not. A gate that refuses everyone -- the
 * failure this codebase has caught more than once -- cannot produce the second.
 *
 * No external test framework (rule 39): a `check()` and two counters.
 */
#include <cstdio>
#include <cstdlib>
#include <string>

#include <grpcpp/grpcpp.h>

#include "finance.grpc.pb.h"

import std;
import finance_service;

namespace {

int g_checks = 0;
int g_failures = 0;

auto check(bool condition, const std::string& what) -> void {
    ++g_checks;
    std::printf("  %s: %s\n", condition ? "PASS" : "FAIL", what.c_str());
    if (!condition) ++g_failures;
}

auto section(const char* title) -> void { std::printf("\n=== %s ===\n", title); }

// Synthetic keys. The registry stores SHA-512 hashes, never the key, so the
// literal below is what a caller presents and the hex is what is configured.
// Both are `pk_live_` + 43 characters, which is the shape the parser enforces.
constexpr const char* kPartnerKey = "pk_live_PPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPP";
constexpr const char* kProKey = "pk_live_RRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRR";

constexpr const char* kKeysJson = R"({
  "50d0f4624ee857d15b0aa19637489e8f30f85ae2ac6c94d6dd9636c43386e8fa5804743e1a55cd2b41b775911aa17932944b7ca35e7e496bbcafd386336b5b9a": {
    "id": "test-partner", "type": "publishable", "tier": "partner",
    "scopes": ["finance"], "enabled": true
  },
  "3dd6ef077a448bd65f903e080ffab5b9928918c6b9f5bcc869c9e354108a04877f5ec9f317a9d08dc70c2099def135f1e08f02e67d1371adc273974304d87eae": {
    "id": "test-pro", "type": "publishable", "tier": "pro",
    "scopes": ["finance"], "enabled": true
  }
})";

using sensen::finance::Finance;

struct ServiceFixture {
    std::unique_ptr<grpc::Server> server;
    std::unique_ptr<Finance::Stub> stub;

    ServiceFixture() {
        grpc::ServerBuilder builder;
        int port = 0;
        builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &port);
        // No hooks: see the header comment on what "admitted" looks like here.
        options_calculator::finance::RegisterFinanceService(builder);
        server = builder.BuildAndStart();
        if (!server || port == 0) {
            std::fprintf(stderr, "FATAL: could not start the in-process finance service\n");
            std::exit(2);
        }
        stub = Finance::NewStub(grpc::CreateChannel("127.0.0.1:" + std::to_string(port),
                                                    grpc::InsecureChannelCredentials()));
    }
    ~ServiceFixture() {
        if (server) server->Shutdown();
    }
};

auto make_context(const char* api_key) -> std::unique_ptr<grpc::ClientContext> {
    auto ctx = std::make_unique<grpc::ClientContext>();
    ctx->set_deadline(std::chrono::system_clock::now() + std::chrono::seconds{30});
    if (api_key != nullptr) ctx->AddMetadata("x-api-key", api_key);
    return ctx;
}

auto refresh_with(Finance::Stub& stub, const char* api_key) -> grpc::Status {
    sensen::finance::RefreshStateAssumptionsRequest req;
    req.set_dry_run(true);  // even the harmless shape must be refused
    sensen::finance::RefreshStateAssumptionsResponse resp;
    auto ctx = make_context(api_key);
    return stub.RefreshStateAssumptions(ctx.get(), req, &resp);
}

auto is_denied(const grpc::Status& s) -> bool {
    return !s.ok() && s.error_code() == grpc::StatusCode::PERMISSION_DENIED;
}

}  // namespace

auto main() -> int {
    // Before the fixture, and before anything can touch the registry.
    setenv("FINANCE_API_KEYS", kKeysJson, 1);
    // Observe is the default mode and the one production runs; it lets an
    // unauthenticated caller through as tier `anonymous` rather than refusing
    // at the door, which is exactly the caller section 1 needs to reach the
    // gate under test.
    unsetenv("FINANCE_API_KEY_MODE");

    ServiceFixture fixture;
    Finance::Stub& stub = *fixture.stub;

    std::printf("State-assumptions write gate\n");

    // =======================================================================
    section("1. An anonymous caller cannot write state assumptions");
    // =======================================================================
    {
        const auto s = refresh_with(stub, nullptr);
        check(is_denied(s), "RefreshStateAssumptions with no credential is PERMISSION_DENIED");
        check(s.error_message().find("partner") != std::string::npos,
              "the refusal names what is required, so a caller can act on it");
    }

    // =======================================================================
    section("2. A VALID Pro credential cannot write them either");
    // =======================================================================
    // This is the check that pins the gate to the tier. A Pro subscriber is a
    // customer of the calculator, not an operator of it, and `data_year` makes
    // the distinction load-bearing: every plausibility bound the validator
    // enforces is satisfied by a decade-old ACS vintage, so pinning one would
    // rewrite all fifty states with figures nothing downstream can tell from
    // current ones.
    {
        const auto s = refresh_with(stub, kProKey);
        check(is_denied(s),
              "RefreshStateAssumptions with an authenticated PRO key is PERMISSION_DENIED -- "
              "the gate keys on tier, not on being signed in");
    }

    // =======================================================================
    section("3. A partner credential is ADMITTED");
    // =======================================================================
    // A gate proven only to refuse is indistinguishable from one that refuses
    // everybody, which is the failure this repository has already recorded
    // twice. FAILED_PRECONDITION here is the unwired hook answering -- a
    // different layer, refusing for a different, stated reason.
    {
        const auto s = refresh_with(stub, kPartnerKey);
        check(!is_denied(s), "RefreshStateAssumptions with a PARTNER key passes the write gate");
        check(s.error_code() == grpc::StatusCode::FAILED_PRECONDITION,
              "and reaches the next layer, which reports the hook is not wired into this build");
    }

    // =======================================================================
    section("4. Reading stays open to everyone");
    // =======================================================================
    // The table holds public Census aggregates seeded into fifty rows. There is
    // no confidentiality to protect here, only integrity, and putting a
    // credential in front of the read would break the site's own state pages
    // for no gain.
    {
        sensen::finance::GetStateAssumptionsRequest req;
        sensen::finance::GetStateAssumptionsResponse resp;
        auto ctx = make_context(nullptr);
        const auto s = stub.GetStateAssumptions(ctx.get(), req, &resp);
        check(!is_denied(s), "GetStateAssumptions with no credential is NOT refused by the gate");
    }

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
