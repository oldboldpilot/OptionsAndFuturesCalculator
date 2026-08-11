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

#include <grpcpp/grpcpp.h>
#include "calculator.pb.h"
#include "calculator.grpc.pb.h"

import calculator_service;

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
        check(resp.pnl_matrix_size() == 0 && resp.max_profit() == 0.0,
              "...and returns NO payoff points, so nothing can be read off a curve that "
              "was never drawn against the right variable");

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

    // -----------------------------------------------------------------
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
