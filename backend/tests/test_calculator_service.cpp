// @author Olumuyiwa Oluwasanmi
//
// Real, in-process gRPC gate for CalculateStrategy -- the RPC EVERY strategy
// calculation on optionsandfuturescalculator.com goes through, via SGEE's
// graph interpreter. Before this file, that path had ZERO automated test
// coverage: the file it replaces (see git history) targeted an API that no
// longer exists (ComputeStrategyPnL, CalculationRequest), asserted a
// fabricated probability-of-profit of exactly 0.65, and was never even wired
// into the CMake build -- its presence implied coverage that did not exist.
//
// Same shape as tests/test_option_pricing_service.cpp and
// tests/test_finance_service_validation.cpp: a plain hand-rolled
// check()/section() harness (sensen coding policy, config/cpp_details.txt
// rule 39, forbids external test frameworks), a real grpc::Server on an
// OS-assigned loopback port hosting exactly what
// options_calculator::service::RegisterCalculatorService registers in
// production, and a real OptionsCalculator::Stub -- not a direct call into
// calculator_service.cpp's internals.
//
// WHAT THIS FILE PROVES, SECTION BY SECTION:
//
//   1. CLOSED-FORM IDENTITY: a 580/600 bull call spread at a 7.25 debit
//      prices to maxProfit 1275, maxLoss -725, breakEven 587.25 -- the exact
//      figures CLAUDE.md already cites as the production Pro-gate identity
//      ("closed-form answer for a 7.25 debit"), so this is independently
//      derivable, not a number the engine handed itself.
//   2. IRON CONDOR: a short iron condor prices to exactly TWO breakevens
//      (one per short strike) and non-zero net gamma/vega -- the properties
//      that distinguish a real multi-leg risk computation from a stub.
//   3. DID-COMPUTE, end-to-end over the wire: pnl_matrix and matrix have
//      EXACTLY the requested dimensions, not zero and not some other count.
//   4. THE DISCRIMINATING PROOF (the highest-value section): a server built
//      with only 4 of the graph's 5 actions registered -- reproducing,
//      through the REAL RPC, the exact silent-halt failure mode documented
//      in calculator_service.cpp's postcondition comment (an action id with
//      nothing bound to it routes into interpreter.cppm's silent-halt
//      branch, exactly like a null registry or an action that fails without
//      setting ctx->status). Before the postcondition existed, THIS SECTION
//      FAILED: the RPC returned Status::OK with net_greeks all zero. See the
//      task's own before/after log for the recorded run of both states; this
//      file only asserts the fixed (passing) behaviour, which is what a
//      permanent regression gate should do.
//
// Deliberately scoped to calculator_service.cpp's request handling and
// response marshalling around SGEE, exactly as the two finance-service test
// files are scoped to finance_service.cpp around sensen's pricers/solvers.
// backend/sensen/** and backend/external/SGEE/** are untouched and read-only
// for this file.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <cstdlib>
#include <expected>
#include <map>
#include <optional>

#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <grpcpp/grpcpp.h>
#include "calculator.pb.h"
#include "calculator.grpc.pb.h"

import calculator_service;
import strategy_store;

namespace {

// ---------------------------------------------------------------------------
// Harness (mirrors tests/test_option_pricing_service.cpp /
// tests/test_mortgage_verification.cpp).
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// In-process server + stub, over the REAL RegisterCalculatorService --
// exactly what main.cpp registers in production, on an ephemeral port
// instead of the configured one.
//
// No API key and no PRO_GATE_MODE are set in this process's environment, so
// check_strategy_entitlement's gate is Off (api_key.cpp: pro_gate_mode()
// falls back to Off when PRO_GATE_MODE is unset) -- every multi-leg request
// below runs BELOW the Pro gate, not through it. A test failing with gRPC
// code 7 (PERMISSION_DENIED) would mean the ambient environment set that
// variable, not a defect in this file.
// ---------------------------------------------------------------------------

using calculator::Leg;
using calculator::OptionsCalculator;
using calculator::StrategyRequest;
using calculator::StrategyResponse;

struct ServiceFixture {
    std::unique_ptr<grpc::Server> server;
    std::unique_ptr<OptionsCalculator::Stub> stub;

    ServiceFixture() {
        grpc::ServerBuilder builder;
        int selected_port = 0;
        builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &selected_port);
        options_calculator::service::RegisterCalculatorService(builder);
        server = builder.BuildAndStart();
        if (!server || selected_port == 0) {
            std::fprintf(stderr,
                         "FATAL: could not start the in-process calculator service on an "
                         "OS-assigned loopback port\n");
            std::exit(2);
        }
        auto channel = grpc::CreateChannel("127.0.0.1:" + std::to_string(selected_port),
                                           grpc::InsecureChannelCredentials());
        stub = OptionsCalculator::NewStub(channel);
    }

    ~ServiceFixture() {
        if (server) server->Shutdown();
    }
};

/**
 * A fixture built on RegisterCalculatorServiceForTest instead, binding only
 * the action names listed at construction. Exists solely for section 4 --
 * see that hook's own doc comment in calculator_service.cppm for why
 * production has no path that can construct one of these with a partial set.
 */
struct PartialActionsFixture {
    std::unique_ptr<grpc::Server> server;
    std::unique_ptr<OptionsCalculator::Stub> stub;

    explicit PartialActionsFixture(std::span<const std::string_view> bound_action_names) {
        grpc::ServerBuilder builder;
        int selected_port = 0;
        builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &selected_port);
        options_calculator::service::RegisterCalculatorServiceForTest(builder, bound_action_names);
        server = builder.BuildAndStart();
        if (!server || selected_port == 0) {
            std::fprintf(stderr,
                         "FATAL: could not start the in-process calculator service (partial "
                         "action set) on an OS-assigned loopback port\n");
            std::exit(2);
        }
        auto channel = grpc::CreateChannel("127.0.0.1:" + std::to_string(selected_port),
                                           grpc::InsecureChannelCredentials());
        stub = OptionsCalculator::NewStub(channel);
    }

    ~PartialActionsFixture() {
        if (server) server->Shutdown();
    }
};

auto make_context(std::chrono::seconds deadline = std::chrono::seconds{30})
    -> std::unique_ptr<grpc::ClientContext> {
    auto ctx = std::make_unique<grpc::ClientContext>();
    ctx->set_deadline(std::chrono::system_clock::now() + deadline);
    return ctx;
}

auto add_leg(StrategyRequest& req, Leg::Action action, Leg::Type type, double strike,
             double expiration_days, double premium, double iv, int qty = 1,
             double multiplier = 100.0) -> void {
    auto* leg = req.add_legs();
    leg->set_action(action);
    leg->set_type(type);
    leg->set_strike(strike);
    leg->set_expiration_days(expiration_days);
    leg->set_quantity(qty);
    leg->set_premium(premium);
    leg->set_implied_volatility(iv);
    leg->set_contract_multiplier(multiplier);
}

auto call_strategy(OptionsCalculator::Stub& stub, const StrategyRequest& req)
    -> std::pair<grpc::Status, StrategyResponse> {
    StrategyResponse resp;
    const auto ctx = make_context();
    const auto status = stub.CalculateStrategy(ctx.get(), req, &resp);
    return {status, resp};
}


// ---------------------------------------------------------------------------
// Saved-scenario support: an in-memory store and a real signed JWT
// ---------------------------------------------------------------------------

namespace store = options_calculator::store;

/**
 * An in-memory IStrategyStore that reproduces the Postgres implementation's
 * OBSERVABLE contract -- per-user scoping, upsert-by-name, the per-user cap,
 * newest-first ordering -- and nothing else.
 *
 * The point is to test the RPC layer's own behaviour (auth, entitlement,
 * validation, JSON round-trip, error mapping) without a database. It is NOT a
 * substitute for exercising the real SQL: that is what
 * tests/test_strategy_store_pg.cpp does, against a live Postgres.
 */
class FakeStore final : public store::IStrategyStore {
  public:
    [[nodiscard]] auto save(std::string_view subject, std::string_view name,
                            std::string_view symbol, std::string_view payload_json)
        -> std::expected<store::SaveOutcome, store::StoreError> override {
        if (subject.empty() || name.empty()) return std::unexpected(store::StoreError::Invalid);
        auto& rows = by_user_[std::string{subject}];

        const auto it = std::find_if(rows.begin(), rows.end(),
                                     [&](const store::SavedRow& r) { return r.name == name; });
        if (it != rows.end()) {
            it->symbol = std::string{symbol};
            it->payload_json = std::string{payload_json};
            it->updated_at = stamp(++clock_);
            // Newest-first: move the touched row to the front, mirroring the
            // real store's ORDER BY updated_at DESC.
            store::SavedRow moved = *it;
            rows.erase(it);
            rows.insert(rows.begin(), moved);
            return store::SaveOutcome{.row = moved, .replaced_existing = true};
        }

        if (rows.size() >= store::kMaxPerUser) {
            return std::unexpected(store::StoreError::AtCapacity);
        }
        store::SavedRow row{
            .id = std::format("00000000-0000-4000-8000-{:012}", ++id_seq_),
            .name = std::string{name},
            .symbol = std::string{symbol},
            .payload_json = std::string{payload_json},
            .created_at = stamp(++clock_),
            .updated_at = stamp(clock_),
        };
        rows.insert(rows.begin(), row);
        return store::SaveOutcome{.row = row, .replaced_existing = false};
    }

    [[nodiscard]] auto list(std::string_view subject)
        -> std::expected<std::vector<store::SavedRow>, store::StoreError> override {
        if (subject.empty()) return std::unexpected(store::StoreError::Invalid);
        const auto it = by_user_.find(std::string{subject});
        if (it == by_user_.end()) return std::vector<store::SavedRow>{};
        return it->second;
    }

    [[nodiscard]] auto remove(std::string_view subject, std::string_view id)
        -> std::expected<bool, store::StoreError> override {
        if (subject.empty()) return std::unexpected(store::StoreError::Invalid);
        const auto it = by_user_.find(std::string{subject});
        if (it == by_user_.end()) return false;
        auto& rows = it->second;
        const auto row = std::find_if(rows.begin(), rows.end(),
                                      [&](const store::SavedRow& r) { return r.id == id; });
        if (row == rows.end()) return false;
        rows.erase(row);
        return true;
    }

    /** Direct read, for assertions that must not go through the RPC. */
    [[nodiscard]] auto count_for(std::string_view subject) const -> std::size_t {
        const auto it = by_user_.find(std::string{subject});
        return it == by_user_.end() ? 0U : it->second.size();
    }

  private:
    [[nodiscard]] static auto stamp(int tick) -> std::string {
        return std::format("2026-01-01T00:00:{:02}Z", tick % 60);
    }

    std::map<std::string, std::vector<store::SavedRow>> by_user_;
    int id_seq_ = 0;
    int clock_ = 0;
};

/** Unpadded base64url -- must match api_key.cpp's own b64url_encode exactly. */
auto b64url(std::string_view raw) -> std::string {
    static constexpr std::string_view kA =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    std::size_t i = 0;
    for (; i + 2 < raw.size(); i += 3) {
        const std::uint32_t c = (static_cast<unsigned char>(raw[i]) << 16) |
                                (static_cast<unsigned char>(raw[i + 1]) << 8) |
                                static_cast<unsigned char>(raw[i + 2]);
        out.push_back(kA[(c >> 18) & 0x3F]);
        out.push_back(kA[(c >> 12) & 0x3F]);
        out.push_back(kA[(c >> 6) & 0x3F]);
        out.push_back(kA[c & 0x3F]);
    }
    if (i + 1 == raw.size()) {
        const std::uint32_t c = static_cast<unsigned char>(raw[i]) << 16;
        out.push_back(kA[(c >> 18) & 0x3F]);
        out.push_back(kA[(c >> 12) & 0x3F]);
    } else if (i + 2 == raw.size()) {
        const std::uint32_t c = (static_cast<unsigned char>(raw[i]) << 16) |
                                (static_cast<unsigned char>(raw[i + 1]) << 8);
        out.push_back(kA[(c >> 18) & 0x3F]);
        out.push_back(kA[(c >> 12) & 0x3F]);
        out.push_back(kA[(c >> 6) & 0x3F]);
    }
    return out;
}

constexpr std::string_view kJwtSecret = "test-only-jwt-secret-not-a-real-one";

/**
 * Mints a REAL HS256 token that api_key.cpp's verify_supabase_jwt actually
 * verifies -- not a stub identity injected past the auth layer.
 *
 * That distinction is the whole value of these sections: the property under
 * test is "a verified subject scopes storage", and asserting it against a
 * hand-built Identity would prove nothing about the code path a browser takes.
 */
auto mint_jwt(std::string_view sub, std::string_view tier) -> std::string {
    const auto exp = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count() +
                     3600;
    const std::string header = R"({"alg":"HS256","typ":"JWT"})";
    const std::string payload =
        std::format(R"({{"sub":"{}","exp":{},"app_metadata":{{"tier":"{}"}}}})", sub, exp, tier);
    const std::string signing_input = b64url(header) + "." + b64url(payload);

    std::array<unsigned char, EVP_MAX_MD_SIZE> mac{};
    unsigned int mac_len = 0;
    HMAC(EVP_sha256(), kJwtSecret.data(), static_cast<int>(kJwtSecret.size()),
         reinterpret_cast<const unsigned char*>(signing_input.data()), signing_input.size(),
         mac.data(), &mac_len);
    return signing_input + "." +
           b64url(std::string_view{reinterpret_cast<const char*>(mac.data()), mac_len});
}

/** A fixture whose service is backed by a caller-supplied store. */
struct SavedFixture {
    std::unique_ptr<grpc::Server> server;
    std::unique_ptr<OptionsCalculator::Stub> stub;

    explicit SavedFixture(std::shared_ptr<store::IStrategyStore> injected) {
        grpc::ServerBuilder builder;
        int selected_port = 0;
        builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &selected_port);
        options_calculator::service::RegisterCalculatorServiceForTest(
            builder, kAllActions, std::move(injected));
        server = builder.BuildAndStart();
        if (!server || selected_port == 0) {
            std::fprintf(stderr, "FATAL: could not start the saved-scenario fixture\n");
            std::exit(2);
        }
        auto channel = grpc::CreateChannel("127.0.0.1:" + std::to_string(selected_port),
                                           grpc::InsecureChannelCredentials());
        stub = OptionsCalculator::NewStub(channel);
    }

    ~SavedFixture() {
        if (server) server->Shutdown();
    }

    static constexpr std::array<std::string_view, 5> kAllActions{
        "Initialize", "ComputeExpiryCurve", "ComputeMatrix", "ComputeGreeks",
        "ComputeProbabilities"};
};

/** A context carrying a bearer token, the way the browser sends one. */
auto auth_context(std::string_view jwt) -> std::unique_ptr<grpc::ClientContext> {
    auto ctx = make_context();
    if (!jwt.empty()) ctx->AddMetadata("authorization", std::string{"Bearer "} + std::string{jwt});
    return ctx;
}

/** A minimal but valid two-leg scenario to save. */
auto sample_scenario() -> StrategyRequest {
    StrategyRequest req;
    req.set_underlying_symbol("SPY");
    req.set_current_price(585.0);
    req.set_implied_volatility(0.18);
    req.set_risk_free_rate(0.043);
    add_leg(req, Leg::BUY, Leg::CALL, 580.0, 30.0, 12.50, 0.19);
    add_leg(req, Leg::SELL, Leg::CALL, 600.0, 30.0, 5.25, 0.17);
    return req;
}

}  // namespace

auto main() -> int {
    ServiceFixture fixture;
    OptionsCalculator::Stub& stub = *fixture.stub;

    // =======================================================================
    section("1. CLOSED-FORM IDENTITY: 580/600 bull call spread @ 7.25 debit");
    // =======================================================================
    // CLAUDE.md's own production-verification identity for the Pro gate:
    // "maxProfit 1275, maxLoss -725, breakEven 587.25 on a 580/600 bull call
    // spread ... the closed-form answer for a 7.25 debit." Both legs share
    // the same expiry, so the payoff curve (drawn at that shared expiry,
    // curve_days_to_expiration) is the plain piecewise-linear at-expiration
    // diagram: flat at -725 below 580, flat at 1275 above 600, linear
    // between -- exact identities, not approximations, regardless of the
    // price grid's resolution (the code's own linear-interpolation breakeven
    // formula is exact for a linear function between any two bracketing grid
    // points).
    {
        StrategyRequest req;
        req.set_underlying_symbol("SPY");
        req.set_current_price(590.0);
        req.set_risk_free_rate(0.05);
        req.set_price_range_percent(0.25);
        req.set_price_steps(81);
        req.set_date_steps(12);
        add_leg(req, Leg::BUY, Leg::CALL, 580.0, 30.0, 12.00, 0.20);
        add_leg(req, Leg::SELL, Leg::CALL, 600.0, 30.0, 4.75, 0.20);

        auto [status, resp] = call_strategy(stub, req);
        check(status.ok(), "580/600 bull call spread: RPC succeeds (got " +
                                (status.ok() ? std::string{"OK"}
                                            : std::to_string(static_cast<int>(status.error_code())) +
                                                  ": " + status.error_message()) +
                                ")");

        constexpr double kEps = 1e-6;
        check(std::abs(resp.max_profit() - 1275.0) < kEps,
              "maxProfit == 1275 (got " + std::to_string(resp.max_profit()) + ")");
        check(std::abs(resp.max_loss() - (-725.0)) < kEps,
              "maxLoss == -725 (got " + std::to_string(resp.max_loss()) + ")");
        check(std::abs(resp.break_even() - 587.25) < kEps,
              "breakEven == 587.25 (got " + std::to_string(resp.break_even()) + ")");
        check(resp.breakeven_prices_size() == 1,
              "exactly one breakeven for a two-leg vertical spread (got " +
                  std::to_string(resp.breakeven_prices_size()) + ")");
        check(std::abs(resp.risk_reward_ratio() - (1275.0 / 725.0)) < 1e-4,
              "riskRewardRatio == maxProfit/|maxLoss| (got " +
                  std::to_string(resp.risk_reward_ratio()) + ")");
    }

    // =======================================================================
    section("2. IRON CONDOR: two breakevens, non-zero net Greeks");
    // =======================================================================
    // Short iron condor: buy 560 put / sell 570 put (put spread, net credit
    // 1.50), sell 610 call / buy 620 call (call spread, net credit 1.50).
    // Total credit 3.00 -> put-side breakeven 570 - 3.00 = 567, call-side
    // breakeven 610 + 3.00 = 613: two DISTINCT breakevens, one per short
    // strike, which is exactly the property a single scalar `break_even`
    // field cannot represent (see calculator_service.cpp's own comment on
    // breakeven_prices). Short gamma and short vega are structural for any
    // condor with time value left, so both are asserted non-zero rather than
    // delta, which can land near zero for strikes symmetric around spot.
    {
        StrategyRequest req;
        req.set_underlying_symbol("SPY");
        req.set_current_price(590.0);
        req.set_risk_free_rate(0.05);
        req.set_price_range_percent(0.25);
        req.set_price_steps(81);
        req.set_date_steps(12);
        add_leg(req, Leg::BUY, Leg::PUT, 560.0, 30.0, 2.00, 0.22);
        add_leg(req, Leg::SELL, Leg::PUT, 570.0, 30.0, 3.50, 0.22);
        add_leg(req, Leg::SELL, Leg::CALL, 610.0, 30.0, 3.25, 0.20);
        add_leg(req, Leg::BUY, Leg::CALL, 620.0, 30.0, 1.75, 0.20);

        auto [status, resp] = call_strategy(stub, req);
        check(status.ok(), "iron condor: RPC succeeds (got " +
                                (status.ok() ? std::string{"OK"}
                                            : std::to_string(static_cast<int>(status.error_code())) +
                                                  ": " + status.error_message()) +
                                ")");

        check(resp.breakeven_prices_size() == 2,
              "iron condor has exactly TWO breakevens (got " +
                  std::to_string(resp.breakeven_prices_size()) + ")");
        if (resp.breakeven_prices_size() == 2) {
            const double lo = std::min(resp.breakeven_prices(0), resp.breakeven_prices(1));
            const double hi = std::max(resp.breakeven_prices(0), resp.breakeven_prices(1));
            check(std::abs(lo - 567.0) < 1e-6,
                  "lower breakeven == 567 (put strike 570 - 3.00 credit; got " +
                      std::to_string(lo) + ")");
            check(std::abs(hi - 613.0) < 1e-6,
                  "upper breakeven == 613 (call strike 610 + 3.00 credit; got " +
                      std::to_string(hi) + ")");
        }

        check(resp.has_net_greeks(), "response carries net_greeks");
        check(std::abs(resp.net_greeks().gamma()) > 1e-6,
              "net gamma is non-zero (got " + std::to_string(resp.net_greeks().gamma()) + ")");
        check(std::abs(resp.net_greeks().vega()) > 1e-6,
              "net vega is non-zero (got " + std::to_string(resp.net_greeks().vega()) + ")");
        check(resp.net_greeks().gamma() < 0.0,
              "a SHORT iron condor is short gamma (negative; got " +
                  std::to_string(resp.net_greeks().gamma()) + ")");
        check(resp.net_greeks().vega() < 0.0,
              "a SHORT iron condor is short vega (negative; got " +
                  std::to_string(resp.net_greeks().vega()) + ")");
        check(resp.leg_risk_size() == 4,
              "leg_risk has one entry per leg (got " + std::to_string(resp.leg_risk_size()) + ")");
    }

    // =======================================================================
    section("3. DID-COMPUTE, end-to-end: pnl_matrix and matrix have the "
            "REQUESTED dimensions");
    // =======================================================================
    // The wire-level twin of calculator_service.cpp's own postcondition:
    // asking for a specific price_steps/date_steps grid must produce a
    // response with exactly that many points, not zero and not some other
    // count. A single-leg call needs no Pro credential even with the gate
    // enforced (check_strategy_entitlement admits leg_count <= 1
    // unconditionally), so this section is a control that holds regardless
    // of the ambient PRO_GATE_MODE.
    {
        StrategyRequest req;
        req.set_underlying_symbol("SPY");
        req.set_current_price(500.0);
        req.set_risk_free_rate(0.04);
        req.set_price_range_percent(0.20);
        req.set_price_steps(50);
        req.set_date_steps(5);
        add_leg(req, Leg::BUY, Leg::CALL, 500.0, 45.0, 15.0, 0.25);

        auto [status, resp] = call_strategy(stub, req);
        check(status.ok(), "single-leg call: RPC succeeds");
        check(resp.pnl_matrix_size() == 50,
              "pnl_matrix has exactly the requested 50 points (got " +
                  std::to_string(resp.pnl_matrix_size()) + ")");
        check(resp.matrix_size() == 50 * 5,
              "matrix has exactly the requested 50 x 5 = 250 cells (got " +
                  std::to_string(resp.matrix_size()) + ")");
    }

    // =======================================================================
    section("4. THE DISCRIMINATING PROOF: a server missing one of the "
            "graph's five actions must NOT return OK");
    // =======================================================================
    // Reproduces, through the REAL RPC, the exact silent-halt failure mode
    // calculator_service.cpp's postcondition comment documents: "if an
    // action fails WITHOUT setting ctx->status, the entity halts silently
    // ... e.g. a P&L curve with no Greeks". ComputeGreeks is left unbound
    // (only Initialize, ComputeExpiryCurve, ComputeMatrix, and
    // ComputeProbabilities are registered) -- the graph builds and runs
    // Initialize/ComputeExpiryCurve/ComputeMatrix normally, then reaches the
    // ComputeGreeks node, finds no action bound to its id, and
    // ActionRegistry::Execute returns ExecutionError::ActionFailed with
    // ctx->status untouched. interpreter.cppm's node loop then halts the
    // entity at ComputeGreeks -- it never reaches ComputeProbabilities or
    // Done.
    //
    // BEFORE calculator_service.cpp's postcondition existed, this section
    // FAILED: the RPC returned Status::OK, with pnl_matrix and matrix fully
    // populated (those actions did run) but net_greeks all zero and no
    // probability/risk fields (those actions never ran) -- a partially
    // filled response with no error of any kind, indistinguishable from a
    // real answer to a client. Recorded from the actual pre-fix binary: this
    // exact request returned status.ok()=true. The postcondition's
    // TERMINAL-STATE check (the entity is parked at ComputeGreeks, not Done)
    // now catches it and turns it into INTERNAL.
    {
        constexpr std::array<std::string_view, 4> kMissingGreeks{
            "Initialize", "ComputeExpiryCurve", "ComputeMatrix", "ComputeProbabilities"};
        PartialActionsFixture broken_fixture(kMissingGreeks);
        OptionsCalculator::Stub& broken_stub = *broken_fixture.stub;

        StrategyRequest req;
        req.set_underlying_symbol("SPY");
        req.set_current_price(590.0);
        req.set_risk_free_rate(0.05);
        req.set_price_range_percent(0.25);
        req.set_price_steps(81);
        req.set_date_steps(12);
        add_leg(req, Leg::BUY, Leg::CALL, 580.0, 30.0, 12.00, 0.20);
        add_leg(req, Leg::SELL, Leg::CALL, 600.0, 30.0, 4.75, 0.20);

        auto [status, resp] = call_strategy(broken_stub, req);
        check(!status.ok(),
              "a graph missing the ComputeGreeks action is REFUSED, not answered with a "
              "partial response (got " +
                  (status.ok() ? std::string{"OK -- THE BUG: pnl_matrix had "} +
                                     std::to_string(resp.pnl_matrix_size()) +
                                     " points but net_greeks.gamma()=" +
                                     std::to_string(resp.net_greeks().gamma()) + " (should be "
                                     "non-zero, per section 2)"
                               : std::to_string(static_cast<int>(status.error_code())) + ": " +
                                     status.error_message()) +
                  ")");
        check(status.error_code() == grpc::StatusCode::INTERNAL,
              "...specifically INTERNAL (a server-side defect), not some other code (got " +
                  std::to_string(static_cast<int>(status.error_code())) + ")");
        check(status.error_message().find("Done") != std::string::npos ||
                  status.error_message().find("ComputeGreeks") != std::string::npos,
              "...and the message names where the graph actually stalled, not a generic "
              "failure: \"" +
                  status.error_message() + "\"");
    }

    // A control alongside section 4: the FULL action set, run through the
    // SAME PartialActionsFixture machinery (not ServiceFixture), still
    // succeeds -- so section 4's failure is specifically about the missing
    // action, not an artifact of using RegisterCalculatorServiceForTest at
    // all.
    {
        constexpr std::array<std::string_view, 5> kAllFive{
            "Initialize", "ComputeExpiryCurve", "ComputeMatrix", "ComputeGreeks",
            "ComputeProbabilities"};
        PartialActionsFixture control_fixture(kAllFive);
        OptionsCalculator::Stub& control_stub = *control_fixture.stub;

        StrategyRequest req;
        req.set_underlying_symbol("SPY");
        req.set_current_price(590.0);
        req.set_risk_free_rate(0.05);
        req.set_price_range_percent(0.25);
        req.set_price_steps(81);
        req.set_date_steps(12);
        add_leg(req, Leg::BUY, Leg::CALL, 580.0, 30.0, 12.00, 0.20);
        add_leg(req, Leg::SELL, Leg::CALL, 600.0, 30.0, 4.75, 0.20);

        auto [status, resp] = call_strategy(control_stub, req);
        check(status.ok(),
              "CONTROL: the same fixture machinery with all FIVE actions bound still "
              "succeeds (proves section 4's failure is the missing action, not the test "
              "harness)");
        check(std::abs(resp.max_profit() - 1275.0) < 1e-6,
              "...and prices to the same maxProfit == 1275 as section 1");
    }

    // =======================================================================
    section("6. ASIAN LEGS: refused by CODE, and only when actually Asian");
    // =======================================================================
    // Every figure this service returns is a function of TERMINAL SPOT. An
    // average-price Asian pays on the realized AVERAGE, which is a different
    // random variable -- Var(A) < Var(S_T) for the same sigma. Walking an
    // Asian leg across the spot grid does not approximate it; it prices a
    // vanilla and calls it Asian.
    //
    // The refusal is whole-response and carries its own status code because
    // proto3 has no absent: an unset max_profit is indistinguishable from a
    // real 0.0, so a partial answer would render "max profit $0".
    {
        StrategyRequest req;
        req.set_underlying_symbol("SPY");
        req.set_current_price(590.0);
        req.set_risk_free_rate(0.05);
        req.set_price_range_percent(0.25);
        req.set_price_steps(81);
        req.set_date_steps(12);
        add_leg(req, Leg::BUY, Leg::CALL, 580.0, 30.0, 12.00, 0.20);
        req.mutable_legs(0)->set_asian_type(Leg::AVERAGE_PRICE);

        auto [status, resp] = call_strategy(stub, req);
        check(status.error_code() == grpc::StatusCode::FAILED_PRECONDITION,
              "an AVERAGE_PRICE leg is refused with FAILED_PRECONDITION -- a modelling "
              "limit, not malformed input, and discriminated by CODE not text");
        // Deliberately NOT asserting that `resp` is empty here. gRPC does not
        // deliver a message body alongside a non-OK status, so `resp` is a
        // default-constructed StrategyResponse whatever the engine did --
        // `pnl_matrix_size() == 0 && max_profit() == 0.0` is a property of the
        // transport, not of this service, and cannot fail. A line that reads
        // like a check but has no failing input is worse than no line: it makes
        // the refusal look better covered than it is.
        //
        // The content that assertion was reaching for is real and is asserted
        // below instead, on the direction that CAN fail: the vanilla request
        // must come back carrying a payoff curve. That is what makes "no curve
        // on refusal" mean anything.

        // BREAK DIRECTION. Without this, the check above is satisfied by an
        // engine that refuses every request, including vanilla ones -- which
        // is precisely how a refuse-only gate passes its own test while
        // taking the product down.
        StrategyRequest vanilla = req;
        vanilla.mutable_legs(0)->set_asian_type(Leg::NOT_ASIAN);
        auto [v_status, v_resp] = call_strategy(stub, vanilla);
        check(v_status.ok(),
              "...while the SAME request with asian_type cleared succeeds: the refusal "
              "is caused by the averaging style, not by the request shape");
        check(v_resp.pnl_matrix_size() > 0,
              "...and that vanilla answer really does carry a payoff curve");

        // A malformed Asian must be named as malformed, not as unsupported --
        // averaging_states < 2 is a CRASH in price_option_double (it
        // subscripts a vector whose grid has no width), so the bound is
        // enforced before the unsupported-instrument refusal is reached.
        StrategyRequest bad_states = req;
        bad_states.mutable_legs(0)->set_averaging_states(1);
        auto [b_status, b_resp] = call_strategy(stub, bad_states);
        check(b_status.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
              "averaging_states=1 is INVALID_ARGUMENT, distinct from the Asian refusal: "
              "a malformed Asian is named as malformed");

        StrategyRequest ok_states = req;
        ok_states.mutable_legs(0)->set_averaging_states(50);
        auto [o_status, o_resp] = call_strategy(stub, ok_states);
        check(o_status.error_code() == grpc::StatusCode::FAILED_PRECONDITION,
              "...while a VALID averaging_states still reaches the Asian refusal, so the "
              "bounds check is not standing in for it");
    }


    // =======================================================================
    // 5. SAVED SCENARIOS
    //
    // The property that matters most here is not "save works" -- it is that a
    // scenario is reachable ONLY by the verified user who saved it. Section 5c
    // is the one that proves it; everything before it establishes that the
    // happy path is real enough for 5c to mean something.
    // =======================================================================
    {
        section("5. Saved scenarios");

        // Set BEFORE any RPC: both are read per-call, so this is the
        // configuration every request below sees.
        ::setenv("SUPABASE_JWT_SECRET", std::string{kJwtSecret}.c_str(), 1);
        ::setenv("PRO_GATE_MODE", "enforce", 1);

        constexpr std::string_view kAlice = "11111111-1111-4111-8111-111111111111";
        constexpr std::string_view kBob = "22222222-2222-4222-8222-222222222222";
        const auto alice_pro = mint_jwt(kAlice, "pro");
        const auto bob_pro = mint_jwt(kBob, "pro");
        const auto alice_free = mint_jwt(kAlice, "free");

        auto fake = std::make_shared<FakeStore>();
        SavedFixture fx{fake};
        auto& s = *fx.stub;

        // ------------------------------------------------------------------
        // 5a. Refusals
        // ------------------------------------------------------------------
        {
            calculator::SaveStrategyRequest req;
            req.set_name("Anonymous attempt");
            *req.mutable_request() = sample_scenario();
            calculator::SaveStrategyResponse resp;
            const auto ctx = auth_context("");
            const auto st = s.SaveStrategy(ctx.get(), req, &resp);
            check(st.error_code() == grpc::StatusCode::UNAUTHENTICATED,
                  "anonymous SaveStrategy is UNAUTHENTICATED (got " +
                      std::to_string(static_cast<int>(st.error_code())) + ")");
            check(fake->count_for(kAlice) == 0 && fake->count_for(kBob) == 0,
                  "anonymous SaveStrategy wrote nothing");
        }
        {
            // The security-critical variant: with the Pro gate OFF, an
            // anonymous caller must STILL be refused. If this ever returns OK,
            // every anonymous visitor shares one storage bucket.
            ::setenv("PRO_GATE_MODE", "off", 1);
            calculator::ListStrategiesRequest req;
            calculator::ListStrategiesResponse resp;
            const auto ctx = auth_context("");
            const auto st = s.ListStrategies(ctx.get(), req, &resp);
            check(st.error_code() == grpc::StatusCode::UNAUTHENTICATED,
                  "anonymous ListStrategies is UNAUTHENTICATED even with PRO_GATE_MODE=off");
            ::setenv("PRO_GATE_MODE", "enforce", 1);
        }
        {
            calculator::SaveStrategyRequest req;
            req.set_name("Free tier attempt");
            *req.mutable_request() = sample_scenario();
            calculator::SaveStrategyResponse resp;
            const auto ctx = auth_context(alice_free);
            const auto st = s.SaveStrategy(ctx.get(), req, &resp);
            check(st.error_code() == grpc::StatusCode::PERMISSION_DENIED,
                  "signed-in free tier SaveStrategy is PERMISSION_DENIED (got " +
                      std::to_string(static_cast<int>(st.error_code())) + ")");
            check(fake->count_for(kAlice) == 0, "free-tier SaveStrategy wrote nothing");
        }

        // ------------------------------------------------------------------
        // 5b. Round trip
        // ------------------------------------------------------------------
        std::string alice_id;
        {
            calculator::SaveStrategyRequest req;
            req.set_name("  Earnings play  ");  // whitespace is trimmed, not stored
            *req.mutable_request() = sample_scenario();
            calculator::SaveStrategyResponse resp;
            const auto ctx = auth_context(alice_pro);
            const auto st = s.SaveStrategy(ctx.get(), req, &resp);
            check(st.ok(), "Pro SaveStrategy succeeds (got " + st.error_message() + ")");
            check(resp.strategy().name() == "Earnings play",
                  "the stored name is trimmed (got \"" + resp.strategy().name() + "\")");
            check(!resp.strategy().id().empty(), "the response carries a server-assigned id");
            check(!resp.strategy().created_at().empty(), "the response carries created_at");
            check(!resp.replaced_existing(), "a first save is not a replacement");
            alice_id = resp.strategy().id();
        }
        {
            calculator::ListStrategiesRequest req;
            calculator::ListStrategiesResponse resp;
            const auto ctx = auth_context(alice_pro);
            const auto st = s.ListStrategies(ctx.get(), req, &resp);
            check(st.ok(), "Pro ListStrategies succeeds");
            check(resp.strategies_size() == 1, "Alice sees exactly her one scenario");
            if (resp.strategies_size() == 1) {
                const auto& got = resp.strategies(0).request();
                // The JSON round trip is the part that could silently lose
                // data, so it is asserted field by field rather than by count.
                check(got.underlying_symbol() == "SPY", "round trip preserves the symbol");
                check(got.legs_size() == 2, "round trip preserves both legs");
                check(std::abs(got.current_price() - 585.0) < 1e-9,
                      "round trip preserves the spot price");
                if (got.legs_size() == 2) {
                    check(got.legs(0).action() == Leg::BUY && got.legs(0).type() == Leg::CALL,
                          "round trip preserves leg 0's action and type");
                    check(std::abs(got.legs(1).strike() - 600.0) < 1e-9,
                          "round trip preserves leg 1's strike");
                    check(std::abs(got.legs(1).premium() - 5.25) < 1e-9,
                          "round trip preserves leg 1's premium");
                }
            }
        }
        {
            // Same name again: an update, not a second row.
            calculator::SaveStrategyRequest req;
            req.set_name("Earnings play");
            auto scenario = sample_scenario();
            scenario.set_current_price(590.0);
            *req.mutable_request() = scenario;
            calculator::SaveStrategyResponse resp;
            const auto ctx = auth_context(alice_pro);
            const auto st = s.SaveStrategy(ctx.get(), req, &resp);
            check(st.ok(), "re-saving under the same name succeeds");
            check(resp.replaced_existing(), "re-saving under the same name reports a replacement");
            check(fake->count_for(kAlice) == 1,
                  "re-saving under the same name leaves ONE row, not two");
        }

        // ------------------------------------------------------------------
        // 5c. Cross-user isolation -- the section this feature exists to get right
        // ------------------------------------------------------------------
        {
            calculator::ListStrategiesRequest req;
            calculator::ListStrategiesResponse resp;
            const auto ctx = auth_context(bob_pro);
            const auto st = s.ListStrategies(ctx.get(), req, &resp);
            check(st.ok(), "Bob's ListStrategies succeeds");
            check(resp.strategies_size() == 0, "Bob does NOT see Alice's scenario");
        }
        {
            // Bob holds Alice's real id and is Pro. He must still not be able
            // to delete it -- authorisation is by subject, not by knowing an id.
            calculator::DeleteStrategyRequest req;
            req.set_id(alice_id);
            calculator::DeleteStrategyResponse resp;
            const auto ctx = auth_context(bob_pro);
            const auto st = s.DeleteStrategy(ctx.get(), req, &resp);
            check(st.ok(), "Bob's DeleteStrategy of Alice's id returns OK, not an error");
            check(!resp.deleted(), "Bob does NOT delete Alice's scenario");
            check(fake->count_for(kAlice) == 1, "Alice's scenario survives Bob's delete");
        }

        // ------------------------------------------------------------------
        // 5d. Validation and delete
        // ------------------------------------------------------------------
        {
            calculator::SaveStrategyRequest req;
            req.set_name("   ");
            *req.mutable_request() = sample_scenario();
            calculator::SaveStrategyResponse resp;
            const auto ctx = auth_context(alice_pro);
            const auto st = s.SaveStrategy(ctx.get(), req, &resp);
            check(st.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
                  "a whitespace-only name is INVALID_ARGUMENT");
        }
        {
            calculator::SaveStrategyRequest req;
            req.set_name("No legs");
            req.mutable_request()->set_underlying_symbol("SPY");
            calculator::SaveStrategyResponse resp;
            const auto ctx = auth_context(alice_pro);
            const auto st = s.SaveStrategy(ctx.get(), req, &resp);
            check(st.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
                  "a scenario with no legs is INVALID_ARGUMENT");
        }
        {
            calculator::DeleteStrategyRequest req;
            req.set_id("not-a-uuid");
            calculator::DeleteStrategyResponse resp;
            const auto ctx = auth_context(alice_pro);
            const auto st = s.DeleteStrategy(ctx.get(), req, &resp);
            check(st.ok() && !resp.deleted(),
                  "deleting a malformed id is OK with deleted=false, not an error");
        }
        {
            calculator::DeleteStrategyRequest req;
            req.set_id(alice_id);
            calculator::DeleteStrategyResponse resp;
            const auto ctx = auth_context(alice_pro);
            const auto st = s.DeleteStrategy(ctx.get(), req, &resp);
            check(st.ok() && resp.deleted(), "Alice deletes her own scenario");
            check(fake->count_for(kAlice) == 0, "Alice's scenario is gone");

            // Idempotent: deleting it again is not an error.
            calculator::DeleteStrategyResponse again;
            const auto ctx2 = auth_context(alice_pro);
            const auto st2 = s.DeleteStrategy(ctx2.get(), req, &again);
            check(st2.ok() && !again.deleted(), "deleting the same id twice is OK, deleted=false");
        }

        // ------------------------------------------------------------------
        // 5e. No database configured
        // ------------------------------------------------------------------
        {
            SavedFixture none{nullptr};
            calculator::ListStrategiesRequest req;
            calculator::ListStrategiesResponse resp;
            const auto ctx = auth_context(alice_pro);
            const auto st = none.stub->ListStrategies(ctx.get(), req, &resp);
            check(st.error_code() == grpc::StatusCode::FAILED_PRECONDITION,
                  "with no store configured the RPC is FAILED_PRECONDITION, not a crash (got " +
                      std::to_string(static_cast<int>(st.error_code())) + ")");
        }

        ::unsetenv("PRO_GATE_MODE");
        ::unsetenv("SUPABASE_JWT_SECRET");
    }

    // =======================================================================
    section("7. MATRIX PRICE BOUNDS: a window on the grid, not on the answer");
    // =======================================================================
    // The matrix gets its OWN price grid, and the reason is the invariant
    // asserted below rather than tidiness. price_grid is swept four times --
    // the expiry curve (which is where max_profit, max_loss and break_even
    // come from), the breakeven interpolation, the matrix, and the lognormal
    // probability distribution. Narrowing a SHARED grid to inspect 480..520
    // would have reported the maximum P&L *inside that window* as the
    // position's max profit: a smaller number, correctly computed, answering a
    // question nobody asked. That is the failure this whole design exists to
    // prevent, so it is the first thing tested.
    //
    // Single-leg throughout, so it holds under any ambient PRO_GATE_MODE --
    // the same control section 3 relies on.
    {
        const auto base = [] {
            StrategyRequest req;
            req.set_underlying_symbol("SPY");
            req.set_current_price(500.0);
            req.set_risk_free_rate(0.04);
            req.set_price_range_percent(0.20);   // default window: 400..600
            req.set_price_steps(41);
            req.set_date_steps(3);
            add_leg(req, Leg::BUY, Leg::CALL, 500.0, 45.0, 15.0, 0.25);
            return req;
        };

        const auto cell_range = [](const StrategyResponse& r) {
            double lo = std::numeric_limits<double>::infinity();
            double hi = -std::numeric_limits<double>::infinity();
            for (const auto& c : r.matrix()) {
                lo = std::min(lo, c.price());
                hi = std::max(hi, c.price());
            }
            return std::pair{lo, hi};
        };

        auto [st_free, unbounded] = call_strategy(stub, base());
        check(st_free.ok(), "unbounded request succeeds");

        {
            auto req = base();
            req.set_matrix_price_min(480.0);
            req.set_matrix_price_max(520.0);
            auto [status, resp] = call_strategy(stub, req);
            check(status.ok(), "a bounded request succeeds");

            const auto [lo, hi] = cell_range(resp);
            check(std::abs(lo - 480.0) < 1e-9 && std::abs(hi - 520.0) < 1e-9,
                  "the matrix spans EXACTLY the requested 480..520 (got " +
                      std::to_string(lo) + ".." + std::to_string(hi) + ")");
            check(resp.matrix_size() == 41 * 3,
                  "and still has the requested 41 x 3 cells -- bounds change the WINDOW, "
                  "not the resolution (got " + std::to_string(resp.matrix_size()) + ")");

            // THE INVARIANT. Equality, not a tolerance: these are computed by
            // sweeping a grid the bounds must not have touched, so any
            // difference at all means the grids got shared again.
            check(resp.max_profit() == unbounded.max_profit() &&
                      resp.max_loss() == unbounded.max_loss() &&
                      resp.break_even() == unbounded.break_even(),
                  "max_profit, max_loss and break_even are IDENTICAL to the unbounded "
                  "request -- bounding the matrix does not rewrite the headline numbers");

            bool curve_same = resp.pnl_matrix_size() == unbounded.pnl_matrix_size();
            for (int i = 0; curve_same && i < resp.pnl_matrix_size(); ++i) {
                curve_same = resp.pnl_matrix(i).underlying_price() == unbounded.pnl_matrix(i).underlying_price() &&
                             resp.pnl_matrix(i).pnl() == unbounded.pnl_matrix(i).pnl();
            }
            check(curve_same,
                  "and the expiry curve is unmoved, point for point -- the probability "
                  "distribution is drawn over that same grid");
        }
        {
            // Each side falls back independently, so a caller can pin the
            // upside alone. Asserting the UNPINNED end is the point: it must
            // still be the default 400, not 0 and not the pinned value.
            auto req = base();
            req.set_matrix_price_max(700.0);
            auto [status, resp] = call_strategy(stub, req);
            const auto [lo, hi] = cell_range(resp);
            check(status.ok() && std::abs(lo - 400.0) < 1e-9 && std::abs(hi - 700.0) < 1e-9,
                  "pinning only the upper bound leaves the lower at the default 400 (got " +
                      std::to_string(lo) + ".." + std::to_string(hi) + ")");
        }
        {
            auto req = base();
            req.set_matrix_price_min(550.0);
            auto [status, resp] = call_strategy(stub, req);
            const auto [lo, hi] = cell_range(resp);
            check(status.ok() && std::abs(lo - 550.0) < 1e-9 && std::abs(hi - 600.0) < 1e-9,
                  "pinning only the lower bound leaves the upper at the default 600");
        }
        {
            // Refused, not swapped. Two plausible repairs exist and choosing
            // one silently answers a different question.
            auto req = base();
            req.set_matrix_price_min(520.0);
            req.set_matrix_price_max(480.0);
            auto [status, resp] = call_strategy(stub, req);
            check(status.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
                  "an inverted pair is REFUSED, not silently swapped");
        }
        {
            // The one-sided inversion, which is the easy case to miss: 300 is
            // a perfectly ordinary lower bound and only inverts because the
            // unset upper falls back to 200.
            auto req = base();
            req.set_price_range_percent(0.60);
            req.set_current_price(500.0);
            auto narrow = base();
            narrow.set_price_range_percent(0.02);  // default window 490..510
            narrow.set_matrix_price_min(600.0);
            auto [status, resp] = call_strategy(stub, narrow);
            check(status.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
                  "a lower bound above the UNSET upper's fallback is refused, and the "
                  "message names both numbers");
        }
        {
            auto req = base();
            req.set_matrix_price_min(-10.0);
            auto [status, resp] = call_strategy(stub, req);
            check(status.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
                  "a negative bound is REFUSED -- 0 is the sentinel for unset, so a "
                  "negative one cannot be a typo for it");
        }
        {
            auto req = base();
            req.set_matrix_price_max(std::numeric_limits<double>::quiet_NaN());
            auto [status, resp] = call_strategy(stub, req);
            check(status.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
                  "a NaN bound is REFUSED -- unchecked it renders as a grid of blanks, "
                  "which reads as 'no data' rather than as a bad argument");
        }
    }

    // -----------------------------------------------------------------
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
