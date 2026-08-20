// @author Olumuyiwa Oluwasanmi
//
// Input-validation audit of the REST of sensen.finance.Finance -- every RPC
// other than PriceOptionTree, which a prior commit already hardened against
// four bug classes found there: a remote SIGSEGV (divide-by-(n-1) -> NaN ->
// unchecked vector index), an out-of-bounds read (a fixed-offset Greeks read
// assuming a minimum tree size), silent mislabelling (a Bermudan date that
// can never match a real backward-induction step, priced European while
// still labelled Bermudan), and NaN/+Infinity bypassing every "<= 0"
// positivity guard (NaN compares false against every relation).
//
// This file proves the SAME four classes -- plus two more this sweep found
// along the way, a magnitude-overflow "silent wrong number" and an
// unbounded-iteration-count DoS -- recur across the rest of the surface,
// prioritising the RPCs mortgagefvcalculator.com actually calls
// (ComputeAmortization, ComputeRefinance, ComputeMortgageRecast, ComputeHeloc,
// ComputePayoffTiming, ComputeCumulative, ComputeNpv, ComputeIrr,
// ComputeXnpv/Xirr, ComputeHomeFutureValue, ComputeRentVsBuy,
// ComputeDepreciation, and the ComputePayment/Rate/PresentValue/FutureValue
// TVM family).
//
// Plain hand-rolled check()/section() harness, matching
// tests/test_option_pricing_service.cpp and tests/test_mortgage_verification.cpp
// -- NOT gtest (sensen coding policy, config/cpp_details.txt rule 39, BINDING).
// A real in-process grpc::Server hosting exactly what
// options_calculator::finance::RegisterFinanceService registers in
// production, reached over a real loopback gRPC channel -- this is a service-
// BOUNDARY test. backend/sensen/** (the pricers/solvers themselves) is
// read-only and untouched; every fix under test lives in finance_service.cpp.
//
// EVERY finding below was reproduced against the pre-fix binary before being
// fixed (an ad hoc scratch harness, not committed) -- the numbers quoted in
// each section's comment are what that reproduction actually returned, not
// predicted values.
//
//   1. ComputeAmortization / ComputeDetailedAmortization -- MAGNITUDE
//      OVERFLOW ("wrong number", not a crash): loan_amount/annual_rate used
//      plain REQUIRE_DECIMAL (no check_decimal_string_magnitude bound, no
//      check_compound_growth_safe), unlike every sibling mortgage RPC.
//      Reproduced: {loan_amount=300000, annual_rate=1000000, term_months=1200}
//      returned Status::OK with total_interest_paid =
//      "29999999999999.999999999880000000" -- $30 trillion of interest on a
//      loan under $300,000, BigDecimal's exact __int128 range silently
//      wrapped rather than refused.
//   2. TVM family (ComputePayment/PresentValue/FutureValue) -- the identical
//      missing-guard shape as (1): `rate` is documented as a PER-PERIOD rate,
//      fed straight into pmt()/pv()/fv()'s BigDecimal pow(periods) with no
//      compound-growth bound.
//   3. period_payment (ComputeInterestPayment/ComputePrincipalPayment) --
//      UNBOUNDED ITERATION COUNT: neither `periods` (nper) nor `period` (per)
//      had an upper bound, and ipmt/ppmt (financial.cppm) walk a
//      `for (t=1; t<=per; ++t)` balance loop whenever per<=periods.
//      Reproduced: period=periods=3,000,000 accepted, CPU cost scaling with
//      the caller-chosen value, for a flat quota charge.
//   4. ComputeCumulative -- TWO findings on one RPC: (a) rate/present_value
//      are raw wire doubles fed straight into `BigDecimal{double}`, whose
//      constructor is `static_cast<__int128_t>(std::round(NaN * 1e18))` --
//      UNDEFINED BEHAVIOUR, not merely a NaN that propagates predictably.
//      Reproduced: rate=NaN returned Status::OK with value=-1424480681.094903,
//      a large, plausible-looking, fabricated number with no relationship to
//      any real computation. (b) start_period/end_period/periods were
//      completely unbounded and cumprinc/cumipmt's outer loop costs
//      O(end_period - start_period) regardless of periods. Reproduced:
//      start_period=-2000000, end_period=2000000 (periods=1) accepted,
//      walking 4,000,001 iterations for a flat charge.
//   5. ComputeNpv / ComputeXnpv -- NaN/Infinity BYPASSING EVERY GUARD: `rate`
//      (a raw double) feeds a PLAIN SUM (npv_double/xnpv, financial.cppm) with
//      no Newton-Raphson self-protection. Reproduced: rate=NaN returned
//      Status::OK with value=nan on both RPCs.
//   6. ComputeIrr / ComputeXirr / ComputePaybackPeriod -- same raw-double
//      surface, but their Newton-Raphson solvers (irr/xirr) or ">= 0.0"
//      comparisons (payback_period) happen to self-protect against NaN via
//      non-convergence/false-comparison rather than by design; hardened
//      anyway for a clear, specific error instead of an opaque
//      "failed to converge" after 100 wasted iterations.
//   7. ComputeDepreciation -- the SAME class-4 defeat as PriceOptionTree's
//      own original bug: `life`'s "<= 0.0" guard does not catch NaN.
//      cost/salvage/period/factor had no check of any kind. Reproduced:
//      {method=STRAIGHT_LINE, cost=10000, salvage=1000, life=NaN} returned
//      Status::OK with value=nan.
//   8. PriceBlackScholes -- had NO validation of ANY kind, not even
//      PriceOptionTree's original bare "<= 0" check. Reproduced: zero
//      volatility (spot=strike=100, rate=0.05, T=1) returned Status::OK with
//      value=4.877058 (plausible) but gamma=NaN in the SAME response
//      (0.0/0.0 division); spot=NaN returned Status::OK with value=nan
//      outright.
//   9. PriceOptionMonteCarlo -- same missing-guard shape as (8). Reproduced:
//      years_to_expiry=-1 returned Status::OK with value=nan.
//  10. ComputeProbabilityTree -- rate had no check at all, and the existing
//      "<= 0.0" checks on years_to_expiry/volatility do not catch NaN.
//  11. ComputeAmortizationBatch -- the batch sibling of (4)'s UB finding:
//      loan_amounts/annual_rates/extra_payments/pmi_rates/home_values are raw
//      wire doubles fed directly into `BigDecimal{double}` by
//      calculate_mortgage_batch_cpu (financial.cppm), same UB-on-NaN
//      construction as ComputeCumulative.
//  12. PriceOptionTree's OWN Bermudan-date guard (added by the prior commit)
//      has a narrow residual gap: is_bermudan_exercise_time
//      (options.cppm) only ever compares a backward-induction time
//      t=j*dt, j in [0, steps-1] -- i.e. t in [0, years_to_expiry-dt] -- with
//      a +/-dt/2 window, so the largest matchable time is
//      years_to_expiry - dt/2. A date in the half-open band
//      [years_to_expiry - dt/2, years_to_expiry) satisfies the (0,
//      years_to_expiry] guard yet still can never match a real step -- the
//      identical silent-mislabelling defect in a narrower band. Reproduced:
//      steps=200, years_to_expiry=1.0 (dt=0.005, dead band=[0.9975, 1.0)) --
//      a Bermudan date of 0.999 priced BIT-IDENTICAL to European (6.200231),
//      while a date of 0.5 genuinely changed the price (6.662411).
//      years_to_expiry itself is exempt (a date exactly at expiry does not
//      need to match a step -- proven harmless by the prior commit's own
//      "Bermudan(single date at expiry) == European, bit-for-bit" test).
//
// EVERY RPC below not listed above was READ and is reported CLEAN in the
// per-RPC table this test's companion report carries -- see that report for
// the full accounting, including RPCs identified as having a similar class-4
// gap (ComputePortfolioStats, PriceFutures/ValueFutures/ComputeHedge,
// AnalyzeBond/AnalyzeTreasuryBill, ComputeCommoditySpread) that were NOT
// fixed in this pass (lower priority: not called by mortgagefvcalculator.com,
// and this pass's time budget went to the prioritised RPCs above).
#include <chrono>
#include <cmath>
#include <functional>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <grpcpp/grpcpp.h>
#include "finance.pb.h"
#include "finance.grpc.pb.h"

import finance_service;

namespace {

// ---------------------------------------------------------------------------
// Harness (mirrors tests/test_option_pricing_service.cpp).
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

using sensen::finance::Finance;

struct ServiceFixture {
    std::unique_ptr<grpc::Server> server;
    std::unique_ptr<Finance::Stub> stub;

    ServiceFixture() {
        grpc::ServerBuilder builder;
        int selected_port = 0;
        builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &selected_port);
        options_calculator::finance::RegisterFinanceService(builder);
        server = builder.BuildAndStart();
        if (!server || selected_port == 0) {
            std::fprintf(stderr,
                         "FATAL: could not start the in-process finance service on an "
                         "OS-assigned loopback port\n");
            std::exit(2);
        }
        auto channel = grpc::CreateChannel("127.0.0.1:" + std::to_string(selected_port),
                                           grpc::InsecureChannelCredentials());
        stub = Finance::NewStub(channel);
    }

    ~ServiceFixture() {
        if (server) server->Shutdown();
    }
};

auto make_context(std::chrono::seconds deadline = std::chrono::seconds{60})
    -> std::unique_ptr<grpc::ClientContext> {
    auto ctx = std::make_unique<grpc::ClientContext>();
    ctx->set_deadline(std::chrono::system_clock::now() + deadline);
    return ctx;
}

const double kNaN = std::numeric_limits<double>::quiet_NaN();
const double kInf = std::numeric_limits<double>::infinity();

auto is_invalid_argument(const grpc::Status& s) -> bool {
    return !s.ok() && s.error_code() == grpc::StatusCode::INVALID_ARGUMENT;
}

}  // namespace

auto main() -> int {
    ServiceFixture fixture;
    Finance::Stub& stub = *fixture.stub;

    // =======================================================================
    section("1. ComputeAmortization / ComputeDetailedAmortization: magnitude overflow");
    // =======================================================================
    {
        sensen::finance::AmortizationRequest req;
        req.set_loan_amount("300000");
        req.set_annual_rate("1000000");  // absurd, previously unchecked
        req.set_term_months(1200);
        sensen::finance::AmortizationResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeAmortization(ctx.get(), req, &resp);
        check(is_invalid_argument(status),
              "ComputeAmortization{annual_rate=1000000, term_months=1200} is REJECTED -- "
              "this exact request used to return Status::OK with total_interest_paid = "
              "\"29999999999999.999999999880000000\" ($30 trillion of interest on a "
              "$300,000 loan), a BigDecimal magnitude overflow silently wrapped, not refused");

        sensen::finance::AmortizationRequest good = req;
        good.set_annual_rate("0.065");
        sensen::finance::AmortizationResponse good_resp;
        auto ctx2 = make_context();
        auto status2 = stub.ComputeAmortization(ctx2.get(), good, &good_resp);
        check(status2.ok() && good_resp.schedule_size() == 1200,
              "...but a realistic annual_rate (6.5%) still succeeds with a full 1200-row "
              "schedule -- the fix is a magnitude bound, not a rejection of large-but-real "
              "mortgages");
    }
    {
        sensen::finance::DetailedAmortizationRequest req;
        req.set_loan_amount("300000");
        req.set_annual_rate("1000000");
        req.set_term_months(1200);
        sensen::finance::DetailedAmortizationResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeDetailedAmortization(ctx.get(), req, &resp);
        check(is_invalid_argument(status),
              "ComputeDetailedAmortization -- the same magnitude-overflow gap, fixed the same "
              "way, is REJECTED");
    }

    // =======================================================================
    section("2. TVM family (ComputePayment/PresentValue/FutureValue): compound-growth overflow");
    // =======================================================================
    {
        sensen::finance::PaymentRequest req;
        req.set_rate("1000000");  // per-period rate, absurd
        req.set_periods(1200);
        req.set_present_value("300000");
        sensen::finance::DecimalResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputePayment(ctx.get(), req, &resp);
        check(is_invalid_argument(status),
              "ComputePayment{rate=1000000, periods=1200} is REJECTED -- the identical "
              "missing-guard shape as ComputeAmortization above, on the RPC "
              "finance.proto itself documents as taking a PER-PERIOD rate");

        sensen::finance::PaymentRequest good;
        good.set_rate("0.00541666666666667");  // 6.5%/12
        good.set_periods(360);
        good.set_present_value("300000");
        sensen::finance::DecimalResponse good_resp;
        auto ctx2 = make_context();
        auto status2 = stub.ComputePayment(ctx2.get(), good, &good_resp);
        check(status2.ok() && !good_resp.value().empty(),
              "...but a realistic 30-year mortgage payment computation still succeeds");
    }
    {
        sensen::finance::PresentValueRequest req;
        req.set_rate("1000000");
        req.set_periods(1200);
        req.set_payment("-1000");
        sensen::finance::DecimalResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputePresentValue(ctx.get(), req, &resp);
        check(is_invalid_argument(status), "ComputePresentValue -- same guard, REJECTED");
    }
    {
        sensen::finance::FutureValueRequest req;
        req.set_rate("1000000");
        req.set_periods(1200);
        req.set_payment("-1000");
        sensen::finance::DecimalResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeFutureValue(ctx.get(), req, &resp);
        check(is_invalid_argument(status), "ComputeFutureValue -- same guard, REJECTED");
    }
    {
        // periods with no compound-growth extremity still gets a hard
        // ceiling (100,000): no real payment schedule needs more.
        sensen::finance::PaymentRequest req;
        req.set_rate("0.001");
        req.set_periods(500000);
        req.set_present_value("300000");
        sensen::finance::DecimalResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputePayment(ctx.get(), req, &resp);
        check(is_invalid_argument(status),
              "ComputePayment{periods=500000} (a modest rate, but an absurd period count) is "
              "REJECTED by the period-count ceiling");
    }

    // =======================================================================
    section("3. period_payment (ComputeInterestPayment/ComputePrincipalPayment): unbounded iteration");
    // =======================================================================
    {
        sensen::finance::PeriodPaymentRequest req;
        req.set_rate("0.004166666666666667");
        req.set_period(3'000'000);
        req.set_periods(3'000'000);
        req.set_present_value("300000");
        sensen::finance::DecimalResponse resp;
        auto ctx = make_context();
        const auto t0 = std::chrono::steady_clock::now();
        auto status = stub.ComputeInterestPayment(ctx.get(), req, &resp);
        const auto t1 = std::chrono::steady_clock::now();
        const double secs = std::chrono::duration<double>(t1 - t0).count();
        check(is_invalid_argument(status),
              "ComputeInterestPayment{period=periods=3,000,000} is REJECTED (period-count "
              "ceiling) instead of accepted and walking a 3-million-iteration balance loop "
              "(measured wall time of the rejection itself: " + std::to_string(secs) + "s)");
        check(secs < 1.0,
              "...and the rejection itself is fast (no longer proportional to the caller's "
              "chosen period count)");
    }
    {
        sensen::finance::PeriodPaymentRequest req;
        req.set_rate("0.004166666666666667");
        req.set_period(12);
        req.set_periods(360);
        req.set_present_value("300000");
        sensen::finance::DecimalResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeInterestPayment(ctx.get(), req, &resp);
        check(status.ok() && !resp.value().empty(),
              "...but a realistic 30-year loan's 12th-period interest payment still succeeds");
    }
    {
        sensen::finance::PeriodPaymentRequest req;
        req.set_rate("0.004166666666666667");
        req.set_period(999);  // outside [1, periods] -- the RPC's own documented
        req.set_periods(360); // contract: this returns 0, not an error.
        req.set_present_value("300000");
        sensen::finance::DecimalResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputePrincipalPayment(ctx.get(), req, &resp);
        check(status.ok(),
              "...and an out-of-[1,periods] `period` (999 against periods=360) is still "
              "ACCEPTED per the RPC's own documented contract (\"outside [1, periods] "
              "returns 0\"), not rejected -- the fix bounds `periods`, not `period`'s range");
    }

    // =======================================================================
    section("4. ComputeCumulative: BigDecimal(double) UB on NaN, and unbounded range");
    // =======================================================================
    {
        sensen::finance::CumulativeRequest req;
        req.set_component(sensen::finance::CumulativeRequest::INTEREST);
        req.set_rate(kNaN);
        req.set_periods(360);
        req.set_present_value(300000.0);
        req.set_start_period(1);
        req.set_end_period(1);
        sensen::finance::DoubleResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeCumulative(ctx.get(), req, &resp);
        check(is_invalid_argument(status),
              "ComputeCumulative{rate=NaN} is REJECTED -- this exact request used to return "
              "Status::OK with value=-1424480681.094903, a fabricated number from "
              "BigDecimal{double}'s undefined-behaviour NaN-to-__int128_t cast, not a NaN "
              "that merely propagated visibly");
    }
    {
        sensen::finance::CumulativeRequest req;
        req.set_component(sensen::finance::CumulativeRequest::INTEREST);
        req.set_rate(0.05);
        req.set_periods(360);
        req.set_present_value(kInf);
        req.set_start_period(1);
        req.set_end_period(1);
        sensen::finance::DoubleResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeCumulative(ctx.get(), req, &resp);
        check(is_invalid_argument(status), "ComputeCumulative{present_value=+Infinity} is REJECTED");
    }
    {
        sensen::finance::CumulativeRequest req;
        req.set_component(sensen::finance::CumulativeRequest::INTEREST);
        req.set_rate(0.05);
        req.set_periods(1);
        req.set_present_value(300000.0);
        req.set_start_period(-2'000'000);
        req.set_end_period(2'000'000);
        sensen::finance::DoubleResponse resp;
        auto ctx = make_context();
        const auto t0 = std::chrono::steady_clock::now();
        auto status = stub.ComputeCumulative(ctx.get(), req, &resp);
        const auto t1 = std::chrono::steady_clock::now();
        const double secs = std::chrono::duration<double>(t1 - t0).count();
        check(is_invalid_argument(status),
              "ComputeCumulative{start_period=-2000000, end_period=2000000} (periods=1, so "
              "cumipmt/cumprinc's O(range) outer loop -- independent of periods -- would walk "
              "4,000,001 iterations) is REJECTED, fast (measured " + std::to_string(secs) + "s)");
    }
    {
        sensen::finance::CumulativeRequest req;
        req.set_component(sensen::finance::CumulativeRequest::PRINCIPAL);
        req.set_rate(0.00541666666666667);
        req.set_periods(360);
        req.set_present_value(300000.0);
        req.set_start_period(1);
        req.set_end_period(12);
        sensen::finance::DoubleResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeCumulative(ctx.get(), req, &resp);
        check(status.ok(),
              "...but a realistic year-one cumulative-principal query (periods=360, "
              "start=1, end=12) still succeeds");
    }

    // =======================================================================
    section("5. ComputeNpv / ComputeXnpv: NaN/Infinity bypass -- direct-sum RPCs, no self-protection");
    // =======================================================================
    {
        sensen::finance::NpvRequest req;
        req.set_rate(kNaN);
        req.add_values(-1000.0);
        req.add_values(500.0);
        sensen::finance::DoubleResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeNpv(ctx.get(), req, &resp);
        check(is_invalid_argument(status),
              "ComputeNpv{rate=NaN} is REJECTED -- this exact request used to return "
              "Status::OK with value=nan (npv_double is a plain sum with no Newton-Raphson "
              "self-protection, unlike irr/xirr below)");
    }
    {
        sensen::finance::NpvRequest req;
        req.set_rate(0.1);
        req.add_values(-1000.0);
        req.add_values(kInf);
        sensen::finance::DoubleResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeNpv(ctx.get(), req, &resp);
        check(is_invalid_argument(status),
              "ComputeNpv with a +Infinity cash flow VALUE (not just rate) is also REJECTED");
        check(status.error_message().find("values[1]") != std::string::npos,
              "...and the error names the specific offending element: \"" +
                  status.error_message() + "\"");
    }
    {
        sensen::finance::NpvRequest req;
        req.set_rate(0.1);
        req.add_values(-1000.0);
        req.add_values(300.0);
        req.add_values(400.0);
        req.add_values(500.0);
        sensen::finance::DoubleResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeNpv(ctx.get(), req, &resp);
        check(status.ok(), "...but a normal cash-flow series still succeeds");
    }
    {
        sensen::finance::DatedCashFlowRequest req;
        req.set_rate(kNaN);
        req.add_values(-1000.0);
        req.add_values(1200.0);
        req.add_dates(0.0);
        req.add_dates(31536000.0);
        sensen::finance::DoubleResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeXnpv(ctx.get(), req, &resp);
        check(is_invalid_argument(status),
              "ComputeXnpv{rate=NaN} is REJECTED -- used to return Status::OK with "
              "value=nan (xnpv is likewise a plain sum, no solver)");
    }
    {
        sensen::finance::DatedCashFlowRequest req;
        req.set_rate(0.1);
        req.add_values(-1000.0);
        req.add_values(1200.0);
        req.add_dates(0.0);
        req.add_dates(31536000.0);
        sensen::finance::DoubleResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeXnpv(ctx.get(), req, &resp);
        check(status.ok(), "...but a normal dated cash-flow series still succeeds");
    }

    // =======================================================================
    section("6. ComputeIrr / ComputeXirr / ComputePaybackPeriod: hardened for a clear error");
    // =======================================================================
    {
        sensen::finance::IrrRequest req;
        req.add_values(-1000.0);
        req.add_values(kNaN);
        sensen::finance::DoubleResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeIrr(ctx.get(), req, &resp);
        check(!status.ok(),
              "ComputeIrr with a NaN cash flow is refused (self-protecting via Newton "
              "non-convergence even before this fix, but now with a specific "
              "INVALID_ARGUMENT rather than an opaque FAILED_PRECONDITION "
              "\"failed to converge\" after 100 wasted iterations)");
        check(status.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
              "...specifically INVALID_ARGUMENT, naming the bad input: \"" +
                  status.error_message() + "\"");
    }
    {
        sensen::finance::IrrRequest req;
        req.add_values(-1000.0);
        req.add_values(300.0);
        req.add_values(400.0);
        req.add_values(500.0);
        sensen::finance::DoubleResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeIrr(ctx.get(), req, &resp);
        check(status.ok(), "...but a normal IRR computation still succeeds");
    }
    {
        sensen::finance::DatedCashFlowRequest req;
        req.set_guess(kInf);
        req.add_values(-1000.0);
        req.add_values(1200.0);
        req.add_dates(0.0);
        req.add_dates(31536000.0);
        sensen::finance::DoubleResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeXirr(ctx.get(), req, &resp);
        check(is_invalid_argument(status), "ComputeXirr{guess=+Infinity} is REJECTED");
    }
    {
        sensen::finance::PaybackRequest req;
        req.set_discounted(true);
        req.set_rate(kNaN);
        req.add_values(-1000.0);
        req.add_values(600.0);
        req.add_values(600.0);
        sensen::finance::DoubleResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputePaybackPeriod(ctx.get(), req, &resp);
        check(is_invalid_argument(status),
              "ComputePaybackPeriod{discounted=true, rate=NaN} is REJECTED");
    }
    {
        sensen::finance::PaybackRequest req;
        req.set_discounted(false);
        req.add_values(-1000.0);
        req.add_values(kNaN);
        sensen::finance::DoubleResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputePaybackPeriod(ctx.get(), req, &resp);
        check(is_invalid_argument(status),
              "ComputePaybackPeriod with a NaN cash flow (undiscounted, so `rate` is never "
              "read) is still REJECTED via the values-array check");
    }

    // =======================================================================
    section("7. ComputeDepreciation: NaN bypasses every existing \"<= 0.0\" guard");
    // =======================================================================
    {
        sensen::finance::DepreciationRequest req;
        req.set_method(sensen::finance::DepreciationRequest::STRAIGHT_LINE);
        req.set_cost(10000.0);
        req.set_salvage(1000.0);
        req.set_life(kNaN);
        sensen::finance::DoubleResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeDepreciation(ctx.get(), req, &resp);
        check(is_invalid_argument(status),
              "ComputeDepreciation{method=STRAIGHT_LINE, life=NaN} is REJECTED -- this exact "
              "request used to return Status::OK with value=nan because \"life <= 0.0\" is "
              "false for NaN");
    }
    {
        sensen::finance::DepreciationRequest req;
        req.set_method(sensen::finance::DepreciationRequest::DECLINING_BALANCE);
        req.set_cost(kInf);
        req.set_salvage(1000.0);
        req.set_life(5.0);
        sensen::finance::DoubleResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeDepreciation(ctx.get(), req, &resp);
        check(is_invalid_argument(status),
              "ComputeDepreciation{method=DECLINING_BALANCE, cost=+Infinity} is REJECTED -- "
              "`cost` had no check of any kind before this fix, for any method");
    }
    {
        sensen::finance::DepreciationRequest req;
        req.set_method(sensen::finance::DepreciationRequest::STRAIGHT_LINE);
        req.set_cost(10000.0);
        req.set_salvage(1000.0);
        req.set_life(9.0);
        sensen::finance::DoubleResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeDepreciation(ctx.get(), req, &resp);
        check(status.ok() && std::abs(resp.value() - 1000.0) < 1e-9,
              "...but a normal SLN depreciation computation still succeeds "
              "((10000-1000)/9 = 1000.0, got " + std::to_string(resp.value()) + ")");
    }
    {
        sensen::finance::DepreciationRequest req;
        req.set_method(sensen::finance::DepreciationRequest::MACRS);
        req.set_cost(10000.0);
        req.set_recovery_period(5);
        req.set_year(1);
        sensen::finance::DoubleResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeDepreciation(ctx.get(), req, &resp);
        check(status.ok(),
              "...and MACRS (which never reads life/salvage/period/factor) still succeeds "
              "with those fields left at their proto-default 0.0 (finite, so the new blanket "
              "finiteness checks do not spuriously reject an unused field)");
    }

    // =======================================================================
    section("8. PriceBlackScholes: had NO validation of any kind");
    // =======================================================================
    {
        sensen::finance::BlackScholesRequest req;
        req.set_spot(100.0);
        req.set_strike(100.0);
        req.set_rate(0.05);
        req.set_volatility(0.0);  // division by (volatility * sqrt(T)) == 0
        req.set_years_to_expiry(1.0);
        req.set_option_type(sensen::finance::CALL);
        sensen::finance::BlackScholesResponse resp;
        auto ctx = make_context();
        auto status = stub.PriceBlackScholes(ctx.get(), req, &resp);
        check(is_invalid_argument(status),
              "PriceBlackScholes{volatility=0} is REJECTED -- this exact request used to "
              "return Status::OK with value=4.877058 (a PLAUSIBLE-LOOKING number) while "
              "gamma=NaN in the SAME response (0.0/0.0 from the zero-volatility division)");
    }
    {
        sensen::finance::BlackScholesRequest req;
        req.set_spot(kNaN);
        req.set_strike(100.0);
        req.set_rate(0.05);
        req.set_volatility(0.2);
        req.set_years_to_expiry(1.0);
        req.set_option_type(sensen::finance::CALL);
        sensen::finance::BlackScholesResponse resp;
        auto ctx = make_context();
        auto status = stub.PriceBlackScholes(ctx.get(), req, &resp);
        check(is_invalid_argument(status),
              "PriceBlackScholes{spot=NaN} is REJECTED -- previously reached Status::OK "
              "with value=nan because this RPC had ZERO validation of any field, not even "
              "PriceOptionTree's own original bare \"<= 0\" check");
    }
    {
        sensen::finance::BlackScholesRequest req;
        req.set_spot(-100.0);
        req.set_strike(100.0);
        req.set_rate(0.05);
        req.set_volatility(0.2);
        req.set_years_to_expiry(1.0);
        req.set_option_type(sensen::finance::CALL);
        sensen::finance::BlackScholesResponse resp;
        auto ctx = make_context();
        auto status = stub.PriceBlackScholes(ctx.get(), req, &resp);
        check(is_invalid_argument(status), "PriceBlackScholes{spot=-100} (finite, but "
                                            "non-positive) is REJECTED");
    }
    {
        sensen::finance::BlackScholesRequest req;
        req.set_spot(100.0);
        req.set_strike(100.0);
        req.set_rate(0.05);
        req.set_volatility(0.2);
        req.set_years_to_expiry(1.0);
        req.set_option_type(sensen::finance::CALL);
        sensen::finance::BlackScholesResponse resp;
        auto ctx = make_context();
        auto status = stub.PriceBlackScholes(ctx.get(), req, &resp);
        check(status.ok() && std::isfinite(resp.value()),
              "...but a normal call still prices successfully and finitely (value=" +
                  std::to_string(resp.value()) + ")");
    }
    {
        // years_to_expiry <= 0 is a REAL, intentional case (the intrinsic-
        // value branch in price_black_scholes), not one this fix should
        // reject -- it must stay reachable.
        sensen::finance::BlackScholesRequest req;
        req.set_spot(110.0);
        req.set_strike(100.0);
        req.set_rate(0.05);
        req.set_volatility(0.2);
        req.set_years_to_expiry(0.0);
        req.set_option_type(sensen::finance::CALL);
        sensen::finance::BlackScholesResponse resp;
        auto ctx = make_context();
        auto status = stub.PriceBlackScholes(ctx.get(), req, &resp);
        check(status.ok() && std::abs(resp.value() - 10.0) < 1e-9,
              "...and years_to_expiry=0 (an expiring option) is deliberately NOT rejected -- "
              "price_black_scholes has its own explicit T<=0 intrinsic-value branch, a real "
              "answer (110-100=10) not a defect (got " + std::to_string(resp.value()) + ")");
    }
    {
        // Magnitude overflow (a LATER finding, not the original "no
        // validation at all" gap above): a rate far enough negative
        // overflows PriceBlackScholes' own exp(-rate*years_to_expiry)
        // discount factor to +Infinity, and that Infinity times a
        // normal_cdf() that has itself saturated to exactly 0.0 is a
        // genuine 0*Infinity -> NaN. Reproduced directly against the
        // pre-fix binary: {spot=100,strike=100,rate=-1e10,volatility=0.2,
        // years_to_expiry=1} returned Status::OK with value=-nan.
        sensen::finance::BlackScholesRequest req;
        req.set_spot(100.0);
        req.set_strike(100.0);
        req.set_rate(-1.0e10);
        req.set_volatility(0.2);
        req.set_years_to_expiry(1.0);
        req.set_option_type(sensen::finance::CALL);
        sensen::finance::BlackScholesResponse resp;
        auto ctx = make_context();
        auto status = stub.PriceBlackScholes(ctx.get(), req, &resp);
        check(is_invalid_argument(status),
              "PriceBlackScholes{rate=-1e10} (absurd-but-finite magnitude) is REJECTED -- used "
              "to return Status::OK with value=-nan");
    }
    {
        // Positive control at the same shape as the magnitude test above,
        // and a genuinely high-but-legitimate volatility (500%, a
        // crypto-like underlying) alongside it -- both must still succeed
        // and price finitely.
        sensen::finance::BlackScholesRequest req;
        req.set_spot(100.0);
        req.set_strike(100.0);
        req.set_rate(0.05);
        req.set_volatility(5.0);
        req.set_years_to_expiry(1.0);
        req.set_option_type(sensen::finance::CALL);
        sensen::finance::BlackScholesResponse resp;
        auto ctx = make_context();
        auto status = stub.PriceBlackScholes(ctx.get(), req, &resp);
        check(status.ok() && std::isfinite(resp.value()) && std::isfinite(resp.gamma()) &&
                  std::isfinite(resp.vega()) && std::isfinite(resp.vanna()) &&
                  std::isfinite(resp.volga()) && std::isfinite(resp.charm()) &&
                  std::isfinite(resp.color()) && std::isfinite(resp.speed()),
              "...but volatility=5.0 (500%, a legitimate high-vol crypto-like case) is ACCEPTED "
              "with EVERY Greek finite -- the bound is not over-tight (value=" +
                  std::to_string(resp.value()) + ")");
    }

    // =======================================================================
    section("9. PriceOptionMonteCarlo: same missing-guard shape as PriceBlackScholes");
    // =======================================================================
    {
        sensen::finance::MonteCarloRequest req;
        req.set_spot(100.0);
        req.set_strike(100.0);
        req.set_rate(0.05);
        req.set_volatility(0.2);
        req.set_years_to_expiry(-1.0);
        req.set_paths(1000);
        req.set_steps(10);
        req.set_option_type(sensen::finance::CALL);
        sensen::finance::DoubleResponse resp;
        auto ctx = make_context();
        auto status = stub.PriceOptionMonteCarlo(ctx.get(), req, &resp);
        check(is_invalid_argument(status),
              "PriceOptionMonteCarlo{years_to_expiry=-1} is REJECTED -- this exact request "
              "used to return Status::OK with value=nan");
    }
    {
        sensen::finance::MonteCarloRequest req;
        req.set_spot(100.0);
        req.set_strike(100.0);
        req.set_rate(0.05);
        req.set_volatility(0.2);
        req.set_years_to_expiry(1.0);
        req.set_paths(2000);
        req.set_steps(20);
        req.set_option_type(sensen::finance::CALL);
        sensen::finance::DoubleResponse resp;
        auto ctx = make_context();
        auto status = stub.PriceOptionMonteCarlo(ctx.get(), req, &resp);
        check(status.ok() && std::isfinite(resp.value()),
              "...but a normal Monte Carlo run still succeeds (value=" +
                  std::to_string(resp.value()) + ")");
    }
    {
        // Magnitude overflow: price_option_monte_carlo's per-step update is
        // S *= exp(drift + vol*Z), and an absurd years_to_expiry (with
        // steps left modest) makes dt large enough to overflow that exp().
        // Reproduced directly against the pre-fix binary:
        // {volatility=0.2, years_to_expiry=1e6} returned Status::OK with
        // value=nan.
        sensen::finance::MonteCarloRequest req;
        req.set_spot(100.0);
        req.set_strike(100.0);
        req.set_rate(0.05);
        req.set_volatility(0.2);
        req.set_years_to_expiry(1.0e6);
        req.set_paths(100);
        req.set_steps(50);
        req.set_option_type(sensen::finance::CALL);
        sensen::finance::DoubleResponse resp;
        auto ctx = make_context();
        auto status = stub.PriceOptionMonteCarlo(ctx.get(), req, &resp);
        check(is_invalid_argument(status),
              "PriceOptionMonteCarlo{years_to_expiry=1e6} (absurd-but-finite magnitude) is "
              "REJECTED -- used to return Status::OK with value=nan");
    }
    {
        // Magnitude overflow via rate instead: reproduced directly:
        // {rate=1e5} returned Status::OK with value=nan.
        sensen::finance::MonteCarloRequest req;
        req.set_spot(100.0);
        req.set_strike(100.0);
        req.set_rate(1.0e5);
        req.set_volatility(0.2);
        req.set_years_to_expiry(1.0);
        req.set_paths(100);
        req.set_steps(50);
        req.set_option_type(sensen::finance::CALL);
        sensen::finance::DoubleResponse resp;
        auto ctx = make_context();
        auto status = stub.PriceOptionMonteCarlo(ctx.get(), req, &resp);
        check(is_invalid_argument(status),
              "PriceOptionMonteCarlo{rate=1e5} (absurd-but-finite magnitude) is REJECTED -- "
              "used to return Status::OK with value=nan");
    }
    {
        // Positive control: a legitimate high volatility (500%) still
        // succeeds and prices finitely.
        sensen::finance::MonteCarloRequest req;
        req.set_spot(100.0);
        req.set_strike(100.0);
        req.set_rate(0.05);
        req.set_volatility(5.0);
        req.set_years_to_expiry(1.0);
        req.set_paths(2000);
        req.set_steps(20);
        req.set_option_type(sensen::finance::CALL);
        sensen::finance::DoubleResponse resp;
        auto ctx = make_context();
        auto status = stub.PriceOptionMonteCarlo(ctx.get(), req, &resp);
        check(status.ok() && std::isfinite(resp.value()),
              "...but volatility=5.0 (500%, legitimate high-vol case) is ACCEPTED and finite "
              "(value=" + std::to_string(resp.value()) + ")");
    }

    // =======================================================================
    section("10. ComputeProbabilityTree: rate had no check, existing checks miss NaN");
    // =======================================================================
    {
        sensen::finance::ProbabilityTreeRequest req;
        req.set_rate(kNaN);
        req.set_volatility(0.2);
        req.set_years_to_expiry(1.0);
        req.set_steps(20);
        sensen::finance::ProbabilityTreeResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeProbabilityTree(ctx.get(), req, &resp);
        check(is_invalid_argument(status), "ComputeProbabilityTree{rate=NaN} is REJECTED");
    }
    {
        sensen::finance::ProbabilityTreeRequest req;
        req.set_rate(0.05);
        req.set_volatility(0.2);
        req.set_years_to_expiry(1.0);
        req.set_steps(20);
        sensen::finance::ProbabilityTreeResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeProbabilityTree(ctx.get(), req, &resp);
        check(status.ok(), "...but a normal request still succeeds");
    }
    {
        // Magnitude overflow: calculate_probability_tree shares
        // PriceOptionTree's identical u=exp(lambda*sigma*sqrt(dt)) driver.
        // Reproduced directly: {volatility=1e10} returned Status::OK with
        // a non-finite entry in stock_prices.
        sensen::finance::ProbabilityTreeRequest req;
        req.set_rate(0.05);
        req.set_volatility(1.0e10);
        req.set_years_to_expiry(1.0);
        req.set_steps(20);
        sensen::finance::ProbabilityTreeResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeProbabilityTree(ctx.get(), req, &resp);
        check(is_invalid_argument(status),
              "ComputeProbabilityTree{volatility=1e10} (absurd-but-finite magnitude) is "
              "REJECTED -- used to return Status::OK with a non-finite stock_prices entry");
    }
    {
        // Magnitude overflow via years_to_expiry instead: reproduced
        // directly: {years_to_expiry=1e10} returned Status::OK with a
        // non-finite entry.
        sensen::finance::ProbabilityTreeRequest req;
        req.set_rate(0.05);
        req.set_volatility(0.2);
        req.set_years_to_expiry(1.0e10);
        req.set_steps(20);
        sensen::finance::ProbabilityTreeResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeProbabilityTree(ctx.get(), req, &resp);
        check(is_invalid_argument(status),
              "ComputeProbabilityTree{years_to_expiry=1e10} (absurd-but-finite magnitude) is "
              "REJECTED -- used to return Status::OK with a non-finite stock_prices entry");
    }
    {
        // lambda == +Infinity: had NO finiteness check of any kind before
        // this fix, same finding as PriceOptionTree's own lambda gap.
        sensen::finance::ProbabilityTreeRequest req;
        req.set_rate(0.05);
        req.set_volatility(0.2);
        req.set_years_to_expiry(1.0);
        req.set_steps(20);
        req.set_lambda(std::numeric_limits<double>::infinity());
        sensen::finance::ProbabilityTreeResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeProbabilityTree(ctx.get(), req, &resp);
        check(is_invalid_argument(status),
              "ComputeProbabilityTree{lambda=+Infinity} is REJECTED (previously had no "
              "finiteness check at all)");
    }
    {
        // Positive control: a legitimate high volatility (500%) still
        // succeeds with every stock_prices/state_probabilities entry
        // finite.
        sensen::finance::ProbabilityTreeRequest req;
        req.set_rate(0.05);
        req.set_volatility(5.0);
        req.set_years_to_expiry(1.0);
        req.set_steps(20);
        sensen::finance::ProbabilityTreeResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeProbabilityTree(ctx.get(), req, &resp);
        bool all_finite = true;
        for (int i = 0; i < resp.stock_prices_size(); ++i) {
            if (!std::isfinite(resp.stock_prices(i))) all_finite = false;
        }
        for (int i = 0; i < resp.state_probabilities_size(); ++i) {
            if (!std::isfinite(resp.state_probabilities(i))) all_finite = false;
        }
        check(status.ok() && all_finite,
              "...but volatility=5.0 (500%, legitimate high-vol case) is ACCEPTED with every "
              "stock_prices/state_probabilities entry finite");
    }

    // =======================================================================
    section("11. ComputeAmortizationBatch: the batch sibling of ComputeCumulative's UB");
    // =======================================================================
    {
        sensen::finance::AmortizationBatchRequest req;
        req.add_loan_amounts(kNaN);
        req.add_annual_rates(0.065);
        req.add_term_months(360);
        req.add_extra_payments(0.0);
        req.add_pmi_rates(0.0);
        req.add_home_values(0.0);
        sensen::finance::AmortizationBatchResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeAmortizationBatch(ctx.get(), req, &resp);
        check(is_invalid_argument(status),
              "ComputeAmortizationBatch{loan_amounts=[NaN]} is REJECTED -- the batch RPC has "
              "no decimal-string field at all, so its raw doubles feed "
              "calculate_mortgage_batch_cpu's `BigDecimal(loan_amounts[i])` directly, the "
              "SAME undefined-behaviour NaN-to-__int128_t construction ComputeCumulative "
              "hit above");
        check(status.error_message().find("loan_amounts") != std::string::npos,
              "...naming loan_amounts specifically: \"" + status.error_message() + "\"");
    }
    {
        sensen::finance::AmortizationBatchRequest req;
        req.add_loan_amounts(300000.0);
        req.add_annual_rates(kInf);
        req.add_term_months(360);
        req.add_extra_payments(0.0);
        req.add_pmi_rates(0.0);
        req.add_home_values(0.0);
        sensen::finance::AmortizationBatchResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeAmortizationBatch(ctx.get(), req, &resp);
        check(is_invalid_argument(status),
              "...and an Infinity in annual_rates (not just loan_amounts) is REJECTED too");
    }
    {
        sensen::finance::AmortizationBatchRequest req;
        req.add_loan_amounts(300000.0);
        req.add_annual_rates(0.065);
        req.add_term_months(360);
        req.add_extra_payments(0.0);
        req.add_pmi_rates(0.0);
        req.add_home_values(0.0);
        req.add_loan_amounts(150000.0);
        req.add_annual_rates(0.05);
        req.add_term_months(180);
        req.add_extra_payments(0.0);
        req.add_pmi_rates(0.0);
        req.add_home_values(0.0);
        sensen::finance::AmortizationBatchResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeAmortizationBatch(ctx.get(), req, &resp);
        check(status.ok() && resp.summaries_size() == 2,
              "...but a normal 2-loan batch still succeeds");
    }

    // =======================================================================
    section("12. PriceOptionTree Bermudan dead band: [years_to_expiry - dt/2, years_to_expiry)");
    // =======================================================================
    {
        auto price_berm_put = [&](std::vector<double> dates,
                                   sensen::finance::ExerciseType ex) -> std::pair<grpc::Status, double> {
            sensen::finance::OptionTreeRequest req;
            req.set_spot(100.0);
            req.set_strike(100.0);
            req.set_rate(0.08);
            req.set_volatility(0.25);
            req.set_years_to_expiry(1.0);
            req.set_steps(200);
            req.set_option_type(sensen::finance::PUT);
            req.set_exercise_type(ex);
            for (const double d : dates) req.add_bermudan_dates(d);
            sensen::finance::OptionPricingResponse resp;
            auto ctx = make_context();
            auto status = stub.PriceOptionTree(ctx.get(), req, &resp);
            return {status, status.ok() ? resp.value() : -1.0};
        };

        // dt = 1.0/200 = 0.005; dead band = [0.9975, 1.0).
        auto [s_dead, v_dead] = price_berm_put({0.999}, sensen::finance::BERMUDAN);
        check(is_invalid_argument(s_dead),
              "a Bermudan date of 0.999 (inside the [0.9975, 1.0) dead band -- this exact "
              "request used to price BIT-IDENTICAL to European, 6.200231 == 6.200231, "
              "silently contributing nothing while still labelled Bermudan) is now REJECTED");
        check(s_dead.error_message().find("bermudan_dates") != std::string::npos,
              "...naming bermudan_dates: \"" + s_dead.error_message() + "\"");

        // Exactly at expiry is exempt -- still accepted, still prices as
        // European (proven by the prior commit's own test; re-asserted here
        // as the boundary this fix must NOT reject).
        auto [s_exp, v_exp] = price_berm_put({1.0}, sensen::finance::BERMUDAN);
        check(s_exp.ok(), "a Bermudan date of EXACTLY years_to_expiry (1.0) is still ACCEPTED "
                           "-- the dead-band guard is deliberately exempt at the boundary "
                           "itself, matching the terminal-step payoff's own independent "
                           "exercise-at-expiry handling");

        // A date safely inside the matchable range must still be accepted
        // and must still genuinely change the price -- proving the new
        // guard didn't accidentally reject legitimate Bermudan pricing.
        auto [s_real, v_real] = price_berm_put({0.5}, sensen::finance::BERMUDAN);
        check(s_real.ok(), "a Bermudan date of 0.5 (well inside the matchable range) is still "
                            "ACCEPTED");

        auto [s_euro, v_euro] = price_berm_put({}, sensen::finance::EUROPEAN);
        check(s_euro.ok(), "European reference prices successfully");
        check(v_real != v_euro,
              "...and genuinely changes the price relative to European (Bermudan(0.5)=" +
                  std::to_string(v_real) + " vs European=" + std::to_string(v_euro) +
                  ") -- the dead-band fix does not silently neuter ordinary Bermudan pricing");

        // A date just past the dead-band boundary on the SAFE side (still
        // matchable) is a useful negative control for the guard's own edge.
        auto [s_edge, v_edge] = price_berm_put({0.995}, sensen::finance::BERMUDAN);
        check(s_edge.ok(),
              "a Bermudan date of 0.995 (== (steps-1)*dt, the LARGEST real backward-induction "
              "time, well outside the dead band) is ACCEPTED");
    }

    // =======================================================================
    section("13. AnalyzeBond / AnalyzeTreasuryBill: NaN bypasses every existing \"<= 0\" guard");
    // =======================================================================
    {
        // Reproduced directly: AnalyzeBond{par=1000, coupon_rate=0.05,
        // frequency=2, years_to_maturity=NaN, yield=0.045} used to return
        // Status::OK with price=nan, macaulay_duration=nan, convexity=nan --
        // years_to_maturity's own "<= 0.0" guard does not catch NaN, and
        // price_bond (financial.cppm) additionally casts
        // static_cast<int>(std::round(years_to_maturity * frequency)) to form
        // its coupon-count loop bound: casting a NaN double to int is
        // UNDEFINED BEHAVIOUR per [conv.fpint], not merely a NaN that
        // propagates predictably.
        sensen::finance::BondRequest req;
        req.set_par(1000.0);
        req.set_coupon_rate(0.05);
        req.set_frequency(2);
        req.set_years_to_maturity(kNaN);
        req.set_yield(0.045);
        sensen::finance::BondResponse resp;
        auto ctx = make_context();
        auto status = stub.AnalyzeBond(ctx.get(), req, &resp);
        check(is_invalid_argument(status),
              "AnalyzeBond{years_to_maturity=NaN} is REJECTED -- years_to_maturity also feeds "
              "an int cast (static_cast<int>(std::round(NaN * frequency))) that is undefined "
              "behaviour on NaN, not just a propagated NaN price");
    }
    {
        // coupon_rate had NO check of any kind -- not even a bare "<= 0.0".
        sensen::finance::BondRequest req;
        req.set_par(1000.0);
        req.set_coupon_rate(kNaN);
        req.set_frequency(2);
        req.set_years_to_maturity(10.0);
        req.set_yield(0.045);
        sensen::finance::BondResponse resp;
        auto ctx = make_context();
        auto status = stub.AnalyzeBond(ctx.get(), req, &resp);
        check(is_invalid_argument(status),
              "AnalyzeBond{coupon_rate=NaN} is REJECTED -- coupon_rate had no validation of "
              "any kind before this fix");
    }
    {
        // par's own "<= 0.0" guard does not catch NaN either.
        sensen::finance::BondRequest req;
        req.set_par(kNaN);
        req.set_coupon_rate(0.05);
        req.set_frequency(2);
        req.set_years_to_maturity(10.0);
        req.set_yield(0.045);
        sensen::finance::BondResponse resp;
        auto ctx = make_context();
        auto status = stub.AnalyzeBond(ctx.get(), req, &resp);
        check(is_invalid_argument(status), "AnalyzeBond{par=NaN} is REJECTED");
    }
    {
        // The known_case=kYield branch feeds `yield` straight into
        // price_bond with no check of its own.
        sensen::finance::BondRequest req;
        req.set_par(1000.0);
        req.set_coupon_rate(0.05);
        req.set_frequency(2);
        req.set_years_to_maturity(10.0);
        req.set_yield(kNaN);
        sensen::finance::BondResponse resp;
        auto ctx = make_context();
        auto status = stub.AnalyzeBond(ctx.get(), req, &resp);
        check(is_invalid_argument(status), "AnalyzeBond{yield=NaN} is REJECTED");
    }
    {
        // Positive control: a realistic bond still prices normally.
        sensen::finance::BondRequest req;
        req.set_par(1000.0);
        req.set_coupon_rate(0.05);
        req.set_frequency(2);
        req.set_years_to_maturity(10.0);
        req.set_yield(0.045);
        sensen::finance::BondResponse resp;
        auto ctx = make_context();
        auto status = stub.AnalyzeBond(ctx.get(), req, &resp);
        check(status.ok(), "...but a realistic bond {par=1000, coupon=5%, freq=2, "
                            "years=10, yield=4.5%} still prices successfully");
        // Measured directly against the pre-fix binary (not hand-derived):
        // this is the figure this exact request always returned.
        check(std::abs(resp.price() - 1039.909281) < 0.001,
              "...at the same price it always returned (1039.909281), unchanged by this fix: "
              "got " + std::to_string(resp.price()));
    }
    {
        // Reproduced directly: AnalyzeTreasuryBill{face_value=10000,
        // discount_rate=NaN, days_to_maturity=90} used to return Status::OK
        // with price=nan (the "price <= 0.0" self-check does not catch NaN
        // either, since NaN <= 0.0 is false).
        sensen::finance::TreasuryBillRequest req;
        req.set_face_value(10000.0);
        req.set_discount_rate(kNaN);
        req.set_days_to_maturity(90);
        sensen::finance::TreasuryBillResponse resp;
        auto ctx = make_context();
        auto status = stub.AnalyzeTreasuryBill(ctx.get(), req, &resp);
        check(is_invalid_argument(status), "AnalyzeTreasuryBill{discount_rate=NaN} is REJECTED");
    }
    {
        sensen::finance::TreasuryBillRequest req;
        req.set_face_value(kNaN);
        req.set_discount_rate(0.02);
        req.set_days_to_maturity(90);
        sensen::finance::TreasuryBillResponse resp;
        auto ctx = make_context();
        auto status = stub.AnalyzeTreasuryBill(ctx.get(), req, &resp);
        check(is_invalid_argument(status), "AnalyzeTreasuryBill{face_value=NaN} is REJECTED");
    }
    {
        // Positive control.
        sensen::finance::TreasuryBillRequest req;
        req.set_face_value(10000.0);
        req.set_discount_rate(0.02);
        req.set_days_to_maturity(90);
        sensen::finance::TreasuryBillResponse resp;
        auto ctx = make_context();
        auto status = stub.AnalyzeTreasuryBill(ctx.get(), req, &resp);
        check(status.ok() && std::isfinite(resp.price()),
              "...but a realistic T-bill {face=10000, rate=2%, days=90} still prices "
              "successfully: price=" + std::to_string(resp.price()));
    }

    // =======================================================================
    section("14. PriceFutures / ValueFutures: NO validation of any kind, not even positivity");
    // =======================================================================
    {
        // PriceFutures had ZERO checks -- spot/rate/cost_of_carry/
        // years_to_maturity all passed straight through to price_futures
        // (financial.cppm). Reproduced directly: spot=NaN returns
        // Status::OK with value=nan.
        sensen::finance::FuturesPricingRequest req;
        req.set_spot(kNaN);
        req.set_rate(0.05);
        req.set_cost_of_carry(0.02);
        req.set_years_to_maturity(1.0);
        req.set_continuous(true);
        sensen::finance::DoubleResponse resp;
        auto ctx = make_context();
        auto status = stub.PriceFutures(ctx.get(), req, &resp);
        check(is_invalid_argument(status), "PriceFutures{spot=NaN} is REJECTED");
    }
    {
        // Non-continuous compounding raises (1+cost_of_carry) to a
        // fractional years_to_maturity power -- a base at or below -1 hits
        // std::pow's domain error (NaN) for any non-integer exponent.
        // Reproduced directly: cost_of_carry=-2, years_to_maturity=0.5
        // (continuous=false) returned Status::OK with value=nan.
        sensen::finance::FuturesPricingRequest req;
        req.set_spot(100.0);
        req.set_rate(0.05);
        req.set_cost_of_carry(-2.0);
        req.set_years_to_maturity(0.5);
        req.set_continuous(false);
        sensen::finance::DoubleResponse resp;
        auto ctx = make_context();
        auto status = stub.PriceFutures(ctx.get(), req, &resp);
        check(is_invalid_argument(status),
              "PriceFutures{cost_of_carry=-2, years_to_maturity=0.5, continuous=false} is "
              "REJECTED -- (1+cost_of_carry) <= 0 raised to a fractional power is a std::pow "
              "domain error (NaN), not a real futures price");
    }
    {
        // Positive control: continuous compounding.
        sensen::finance::FuturesPricingRequest req;
        req.set_spot(100.0);
        req.set_rate(0.05);
        req.set_cost_of_carry(0.02);
        req.set_years_to_maturity(1.0);
        req.set_continuous(true);
        sensen::finance::DoubleResponse resp;
        auto ctx = make_context();
        auto status = stub.PriceFutures(ctx.get(), req, &resp);
        check(status.ok(), "...but a realistic continuous-compounding futures price still "
                            "succeeds");
        check(std::abs(resp.value() - 102.0201) < 0.001,
              "...at the expected value (~102.0201), unchanged by this fix: got " +
                  std::to_string(resp.value()));
    }
    {
        // ValueFutures had ZERO checks either.
        sensen::finance::FuturesValuationRequest req;
        req.set_current_spot(kNaN);
        req.set_delivery_price(100.0);
        req.set_rate(0.05);
        req.set_years_to_maturity(0.5);
        req.set_is_long(true);
        sensen::finance::DoubleResponse resp;
        auto ctx = make_context();
        auto status = stub.ValueFutures(ctx.get(), req, &resp);
        check(is_invalid_argument(status), "ValueFutures{current_spot=NaN} is REJECTED");
    }
    {
        sensen::finance::FuturesValuationRequest req;
        req.set_current_spot(105.0);
        req.set_delivery_price(100.0);
        req.set_rate(0.05);
        req.set_years_to_maturity(0.5);
        req.set_is_long(true);
        sensen::finance::DoubleResponse resp;
        auto ctx = make_context();
        auto status = stub.ValueFutures(ctx.get(), req, &resp);
        check(status.ok(), "...but a realistic long futures valuation still succeeds");
        check(std::abs(resp.value() - 4.8765) < 0.001,
              "...at the expected value (~4.8765), unchanged by this fix: got " +
                  std::to_string(resp.value()));
    }

    // =======================================================================
    section("15. SimulateMarginAccount: unchecked scalars + unbounded daily_prices");
    // =======================================================================
    {
        // initial_deposit/initial_margin_requirement/maintenance_margin_requirement/
        // entry_price had no validation of any kind. Reproduced directly:
        // entry_price=NaN returns Status::OK with balance=nan.
        sensen::finance::MarginSimulationRequest req;
        req.set_initial_deposit(10000.0);
        req.set_initial_margin_requirement(5000.0);
        req.set_maintenance_margin_requirement(3000.0);
        req.set_contract_size(100);
        req.set_entry_price(kNaN);
        req.add_daily_prices(51.0);
        req.set_is_long(true);
        sensen::finance::MarginSimulationResponse resp;
        auto ctx = make_context();
        auto status = stub.SimulateMarginAccount(ctx.get(), req, &resp);
        check(is_invalid_argument(status), "SimulateMarginAccount{entry_price=NaN} is REJECTED");
    }
    {
        // A single NaN anywhere in the daily-prices mark-to-market path
        // propagates a NaN balance -- the per-element case a scalar guard
        // cannot catch.
        sensen::finance::MarginSimulationRequest req;
        req.set_initial_deposit(10000.0);
        req.set_initial_margin_requirement(5000.0);
        req.set_maintenance_margin_requirement(3000.0);
        req.set_contract_size(100);
        req.set_entry_price(50.0);
        req.add_daily_prices(51.0);
        req.add_daily_prices(kNaN);
        req.add_daily_prices(49.0);
        req.set_is_long(true);
        sensen::finance::MarginSimulationResponse resp;
        auto ctx = make_context();
        auto status = stub.SimulateMarginAccount(ctx.get(), req, &resp);
        check(is_invalid_argument(status),
              "SimulateMarginAccount{daily_prices=[51, NaN, 49]} is REJECTED -- a single NaN "
              "mid-path propagates into a NaN final balance, not caught by any scalar guard");
    }
    {
        // daily_prices was completely unbounded: the mark-to-market loop is
        // O(len(daily_prices)), and quota::cost_margin_simulation(days) prices
        // it proportionally but never refuses a single absurdly long path --
        // the same DoS shape check_period_count_ceiling already closes
        // elsewhere in this file. Bounded here to the same 100,000 ceiling.
        sensen::finance::MarginSimulationRequest req;
        req.set_initial_deposit(10000.0);
        req.set_initial_margin_requirement(5000.0);
        req.set_maintenance_margin_requirement(3000.0);
        req.set_contract_size(100);
        req.set_entry_price(50.0);
        for (int i = 0; i < 100'001; ++i) req.add_daily_prices(50.0 + (i % 3));
        req.set_is_long(true);
        sensen::finance::MarginSimulationResponse resp;
        auto ctx = make_context();
        auto status = stub.SimulateMarginAccount(ctx.get(), req, &resp);
        check(is_invalid_argument(status),
              "SimulateMarginAccount{daily_prices: 100,001 entries} is REJECTED by the same "
              "100,000-entry ceiling used elsewhere for caller-chosen iteration counts");
    }
    {
        // Positive control.
        sensen::finance::MarginSimulationRequest req;
        req.set_initial_deposit(10000.0);
        req.set_initial_margin_requirement(5000.0);
        req.set_maintenance_margin_requirement(3000.0);
        req.set_contract_size(100);
        req.set_entry_price(50.0);
        req.add_daily_prices(51.0);
        req.add_daily_prices(52.0);
        req.add_daily_prices(49.0);
        req.add_daily_prices(48.0);
        req.set_is_long(true);
        sensen::finance::MarginSimulationResponse resp;
        auto ctx = make_context();
        auto status = stub.SimulateMarginAccount(ctx.get(), req, &resp);
        check(status.ok(), "...but a realistic 4-day margin simulation still succeeds");
        check(std::abs(resp.balance() - 9800.0) < 0.01,
              "...at the expected ending balance (9800: -200 over the path), unchanged by "
              "this fix: got " + std::to_string(resp.balance()));
    }

    // =======================================================================
    section("16. ComputeHedge: futures_volatility==0.0 check misses NaN and negatives");
    // =======================================================================
    {
        // The only existing guard was an EXACT-equality check
        // (`futures_volatility() == 0.0`); NaN == 0.0 is false, so it sailed
        // straight through to calculate_hedge_ratio's own "<= 0.0" guard,
        // which NaN also defeats the same way. Reproduced directly:
        // futures_volatility=NaN returns Status::OK with hedge_ratio=nan.
        sensen::finance::HedgeRequest req;
        req.set_asset_volatility(0.2);
        req.set_futures_volatility(kNaN);
        req.set_correlation(0.9);
        sensen::finance::HedgeResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeHedge(ctx.get(), req, &resp);
        check(is_invalid_argument(status), "ComputeHedge{futures_volatility=NaN} is REJECTED");
    }
    {
        // A negative futures_volatility is not a real volatility either --
        // the exact-equality check let it through (only exact 0.0 was
        // refused), silently returning hedge_ratio=0.0 from the callee's own
        // "<= 0.0" fallback rather than refusing the nonsensical input.
        sensen::finance::HedgeRequest req;
        req.set_asset_volatility(0.2);
        req.set_futures_volatility(-0.1);
        req.set_correlation(0.9);
        sensen::finance::HedgeResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeHedge(ctx.get(), req, &resp);
        check(is_invalid_argument(status),
              "ComputeHedge{futures_volatility=-0.1} is REJECTED -- a negative volatility is "
              "not a real one, not merely a fallback-to-zero-ratio case");
    }
    {
        // have_position is decided by "!= 0.0" against spot_value/
        // contract_multiplier/futures_price -- NaN != 0.0 is TRUE, so a NaN
        // position input was treated as "position supplied" and produced a
        // NaN contract count in an otherwise-OK response.
        sensen::finance::HedgeRequest req;
        req.set_asset_volatility(0.2);
        req.set_futures_volatility(0.18);
        req.set_correlation(0.9);
        req.set_spot_value(kNaN);
        req.set_contract_multiplier(50.0);
        req.set_futures_price(100.0);
        sensen::finance::HedgeResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeHedge(ctx.get(), req, &resp);
        check(is_invalid_argument(status),
              "ComputeHedge{spot_value=NaN, contract_multiplier and futures_price both set} "
              "is REJECTED -- NaN != 0.0 is true, so the have_position gate previously treated "
              "this as a genuine position and returned a NaN contracts count");
    }
    {
        // Positive control.
        sensen::finance::HedgeRequest req;
        req.set_asset_volatility(0.2);
        req.set_futures_volatility(0.18);
        req.set_correlation(0.9);
        req.set_spot_value(100000.0);
        req.set_contract_multiplier(50.0);
        req.set_futures_price(100.0);
        sensen::finance::HedgeResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeHedge(ctx.get(), req, &resp);
        check(status.ok(), "...but a realistic hedge computation with a full position still "
                            "succeeds");
        check(std::abs(resp.hedge_ratio() - 1.0) < 1e-9,
              "...at the expected hedge_ratio (1.0 = 0.9*0.2/0.18), unchanged by this fix: "
              "got " + std::to_string(resp.hedge_ratio()));
        check(resp.contracts_computed() && std::abs(resp.contracts() - 20.0) < 1e-6,
              "...and the expected contract count (20 = 1.0*100000/(50*100)), unchanged by "
              "this fix: got " + std::to_string(resp.contracts()));
    }

    // =======================================================================
    section("17. ComputeCommoditySpread: no validation of any kind");
    // =======================================================================
    {
        sensen::finance::CommoditySpreadRequest req;
        req.set_spread(sensen::finance::CommoditySpreadRequest::CRACK_321);
        req.set_a(kNaN);
        req.set_b(2.1);
        req.set_c(2.0);
        sensen::finance::DoubleResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeCommoditySpread(ctx.get(), req, &resp);
        check(is_invalid_argument(status), "ComputeCommoditySpread{a=NaN} is REJECTED");
    }
    {
        sensen::finance::CommoditySpreadRequest req;
        req.set_spread(sensen::finance::CommoditySpreadRequest::CRACK_321);
        req.set_a(70.0);
        req.set_b(2.1);
        req.set_c(2.0);
        sensen::finance::DoubleResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeCommoditySpread(ctx.get(), req, &resp);
        check(status.ok(), "...but a realistic crack-321 spread still succeeds");
        // (2*(2.1*42) + 1*(2.0*42) - 3*70) / 3 = (176.4 + 84 - 210) / 3 = 16.8.
        check(std::abs(resp.value() - 16.8) < 1e-9,
              "...at the expected value (16.8), unchanged by this fix: got " +
                  std::to_string(resp.value()));
    }

    // =======================================================================
    section("18. ComputePortfolioStats: per-element NaN in a repeated field, and an unbounded size");
    // =======================================================================
    {
        // A single NaN anywhere in portfolio_returns propagates through
        // every statistic in the response -- the per-element case a
        // whole-vector emptiness check cannot catch.
        sensen::finance::PortfolioStatsRequest req;
        req.add_portfolio_returns(0.01);
        req.add_portfolio_returns(kNaN);
        req.add_portfolio_returns(0.02);
        req.set_risk_free_rate(0.001);
        sensen::finance::PortfolioStatsResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputePortfolioStats(ctx.get(), req, &resp);
        check(is_invalid_argument(status),
              "ComputePortfolioStats{portfolio_returns=[0.01, NaN, 0.02]} is REJECTED");
    }
    {
        sensen::finance::PortfolioStatsRequest req;
        req.add_portfolio_returns(0.01);
        req.add_portfolio_returns(0.02);
        req.add_market_returns(0.008);
        req.add_market_returns(kNaN);
        req.set_risk_free_rate(0.001);
        sensen::finance::PortfolioStatsResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputePortfolioStats(ctx.get(), req, &resp);
        check(is_invalid_argument(status),
              "ComputePortfolioStats{market_returns contains a NaN} is REJECTED");
    }
    {
        sensen::finance::PortfolioStatsRequest req;
        req.add_portfolio_returns(0.01);
        req.add_portfolio_returns(0.02);
        req.set_risk_free_rate(kNaN);
        sensen::finance::PortfolioStatsResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputePortfolioStats(ctx.get(), req, &resp);
        check(is_invalid_argument(status), "ComputePortfolioStats{risk_free_rate=NaN} is REJECTED");
    }
    {
        // A single-element series (n=1) is a documented edge the sensen
        // function itself already guards (its own dd_divisor picks n over
        // n-1 when n<=1) -- asserted here as a NON-finding: it must still be
        // ACCEPTED, not rejected, since the callee never divides by zero.
        sensen::finance::PortfolioStatsRequest req;
        req.add_portfolio_returns(0.01);
        req.set_risk_free_rate(0.0);
        sensen::finance::PortfolioStatsResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputePortfolioStats(ctx.get(), req, &resp);
        check(status.ok() && std::isfinite(resp.sharpe_ratio()),
              "ComputePortfolioStats{portfolio_returns=[single element]} is ACCEPTED and "
              "returns a finite result -- the n=1 variance-denominator edge is already safe "
              "in sensen's own calculate_portfolio_stats (n over n-1 when n<=1), not a "
              "finding this fix needs to guard against");
    }
    {
        // Unbounded array length: portfolio_returns/market_returns had no
        // ceiling, only quota::cost_portfolio_stats(n)'s proportional
        // pricing -- the same DoS shape closed elsewhere with an explicit
        // ceiling. Bounded to 100,000 entries (270+ years of daily returns).
        sensen::finance::PortfolioStatsRequest req;
        for (int i = 0; i < 100'001; ++i) req.add_portfolio_returns(0.0001 * (i % 7 - 3));
        req.set_risk_free_rate(0.001);
        sensen::finance::PortfolioStatsResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputePortfolioStats(ctx.get(), req, &resp);
        check(is_invalid_argument(status),
              "ComputePortfolioStats{portfolio_returns: 100,001 entries} is REJECTED by a "
              "100,000-entry ceiling");
    }
    {
        // Positive control.
        sensen::finance::PortfolioStatsRequest req;
        req.add_portfolio_returns(0.01);
        req.add_portfolio_returns(-0.02);
        req.add_portfolio_returns(0.03);
        req.add_portfolio_returns(0.015);
        req.add_portfolio_returns(-0.005);
        req.add_market_returns(0.008);
        req.add_market_returns(-0.015);
        req.add_market_returns(0.02);
        req.add_market_returns(0.01);
        req.add_market_returns(-0.003);
        req.set_risk_free_rate(0.001);
        sensen::finance::PortfolioStatsResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputePortfolioStats(ctx.get(), req, &resp);
        check(status.ok() && resp.benchmark_supplied(),
              "...but a realistic 5-period portfolio-vs-market series still succeeds");
        check(std::isfinite(resp.sharpe_ratio()) && std::isfinite(resp.beta()),
              "...with finite sharpe_ratio/beta: sharpe=" + std::to_string(resp.sharpe_ratio()) +
                  " beta=" + std::to_string(resp.beta()));
    }

    // =======================================================================
    section("19. OptimizePortfolio / ComputeRiskContributions: NaN in a covariance matrix, and O(n^3) DoS");
    // =======================================================================
    {
        sensen::finance::PortfolioOptimizeRequest req;
        req.add_expected_returns(0.08);
        req.add_expected_returns(kNaN);
        req.add_covariance(0.04);
        req.add_covariance(0.006);
        req.add_covariance(0.006);
        req.add_covariance(0.09);
        req.set_size(2);
        req.set_risk_free_rate(0.02);
        req.set_max_sharpe(true);
        sensen::finance::PortfolioOptimizeResponse resp;
        auto ctx = make_context();
        auto status = stub.OptimizePortfolio(ctx.get(), req, &resp);
        check(is_invalid_argument(status),
              "OptimizePortfolio{expected_returns contains a NaN} is REJECTED");
    }
    {
        sensen::finance::PortfolioOptimizeRequest req;
        req.add_expected_returns(0.08);
        req.add_expected_returns(0.12);
        req.add_covariance(0.04);
        req.add_covariance(kNaN);
        req.add_covariance(0.006);
        req.add_covariance(0.09);
        req.set_size(2);
        req.set_risk_free_rate(0.02);
        req.set_max_sharpe(true);
        sensen::finance::PortfolioOptimizeResponse resp;
        auto ctx = make_context();
        auto status = stub.OptimizePortfolio(ctx.get(), req, &resp);
        check(is_invalid_argument(status), "OptimizePortfolio{covariance contains a NaN} is REJECTED");
    }
    {
        // The LU-decompose-and-solve is O(size^3) -- `size` was completely
        // unbounded beyond agreeing with the accompanying vectors' lengths.
        // A caller-chosen size in the low thousands is already enough to
        // make a single request run for an unreasonable time regardless of
        // how the quota system prices it; bounded to 1000 (already an
        // enormous portfolio -- the S&P 500 is 500 names). The size ceiling
        // is checked BEFORE read_covariance's own shape validation, so this
        // is refused without ever needing a genuinely size*size-shaped
        // payload -- proving the guard is a magnitude check on `size`
        // itself, not merely a side effect of the shape check.
        sensen::finance::PortfolioOptimizeRequest req;
        req.set_size(1001);
        req.add_expected_returns(0.05);
        req.add_covariance(0.04);
        req.set_risk_free_rate(0.02);
        sensen::finance::PortfolioOptimizeResponse resp;
        auto ctx = make_context();
        auto status = stub.OptimizePortfolio(ctx.get(), req, &resp);
        check(is_invalid_argument(status),
              "OptimizePortfolio{size=1001} is REJECTED by a 1000-asset ceiling on the O(n^3) "
              "covariance solve");
    }
    {
        // Positive control.
        sensen::finance::PortfolioOptimizeRequest req;
        req.add_expected_returns(0.08);
        req.add_expected_returns(0.12);
        req.add_covariance(0.04);
        req.add_covariance(0.006);
        req.add_covariance(0.006);
        req.add_covariance(0.09);
        req.set_size(2);
        req.set_risk_free_rate(0.02);
        req.set_max_sharpe(true);
        sensen::finance::PortfolioOptimizeResponse resp;
        auto ctx = make_context();
        auto status = stub.OptimizePortfolio(ctx.get(), req, &resp);
        check(status.ok() && resp.weights_size() == 2,
              "...but a realistic 2-asset tangency-portfolio optimization still succeeds");
        check(std::isfinite(resp.sharpe_ratio()),
              "...with a finite sharpe_ratio: got " + std::to_string(resp.sharpe_ratio()));
    }
    {
        sensen::finance::RiskContributionRequest req;
        req.add_weights(0.5);
        req.add_weights(kNaN);
        req.add_covariance(0.04);
        req.add_covariance(0.006);
        req.add_covariance(0.006);
        req.add_covariance(0.09);
        req.set_size(2);
        sensen::finance::RiskContributionResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeRiskContributions(ctx.get(), req, &resp);
        check(is_invalid_argument(status), "ComputeRiskContributions{weights contains a NaN} is REJECTED");
    }
    {
        // Same "checked before the shape validation" property as
        // OptimizePortfolio's own size ceiling above.
        sensen::finance::RiskContributionRequest req;
        req.set_size(1001);
        req.add_weights(0.5);
        req.add_covariance(0.04);
        sensen::finance::RiskContributionResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeRiskContributions(ctx.get(), req, &resp);
        check(is_invalid_argument(status),
              "ComputeRiskContributions{size=1001} is REJECTED by the same 1000-asset ceiling");
    }
    {
        // Positive control.
        sensen::finance::RiskContributionRequest req;
        req.add_weights(0.5);
        req.add_weights(0.5);
        req.add_covariance(0.04);
        req.add_covariance(0.006);
        req.add_covariance(0.006);
        req.add_covariance(0.09);
        req.set_size(2);
        sensen::finance::RiskContributionResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeRiskContributions(ctx.get(), req, &resp);
        check(status.ok() && resp.contributions_size() == 2,
              "...but a realistic 2-asset risk-contribution decomposition still succeeds");
        check(std::isfinite(resp.contributions(0)) && std::isfinite(resp.contributions(1)),
              "...with finite contributions: " + std::to_string(resp.contributions(0)) + ", " +
                  std::to_string(resp.contributions(1)));
    }

    // =======================================================================
    section("20. PriceOptionMonteCarlo completeness re-check: unbounded paths*steps");
    // =======================================================================
    {
        // The prior pass's finite/positivity checks are present and correct
        // (verified by reading the code) -- but paths and steps together are
        // the O(paths*steps) work price_option_monte_carlo actually does, and
        // neither had any ceiling: quota::cost_monte_carlo(paths, steps)
        // prices the work proportionally but never refuses a single request
        // whose own product is large enough to run for an unreasonable time
        // regardless of how it is billed -- the same DoS shape closed
        // elsewhere in this file for period counts and covariance-solve size.
        // NOTE on the chosen magnitude: paths=200,000/steps=600 (a
        // 120-million path-step product, just over the 100-million bound)
        // is already far beyond any real Monte Carlo option pricer's needs
        // (10,000 paths x 252 daily steps = 2.52M is a generous real-world
        // figure) -- picked deliberately small enough that the PRE-FIX
        // binary still finishes this single request in a few seconds rather
        // than hanging the test suite; the genuinely catastrophic case this
        // guard exists for (paths=steps=2,000,000, a 4-trillion product) is
        // exactly what it refuses.
        sensen::finance::MonteCarloRequest req;
        req.set_spot(100.0);
        req.set_strike(100.0);
        req.set_rate(0.05);
        req.set_volatility(0.2);
        req.set_years_to_expiry(1.0);
        req.set_paths(200'000);
        req.set_steps(600);
        req.set_option_type(sensen::finance::CALL);
        sensen::finance::DoubleResponse resp;
        auto ctx = make_context();
        auto status = stub.PriceOptionMonteCarlo(ctx.get(), req, &resp);
        check(is_invalid_argument(status),
              "PriceOptionMonteCarlo{paths=200,000, steps=600} (a 120-million path-step "
              "product) is REJECTED by a bound on paths*steps");
    }
    {
        // Positive control: a realistic Monte Carlo run still succeeds.
        sensen::finance::MonteCarloRequest req;
        req.set_spot(100.0);
        req.set_strike(100.0);
        req.set_rate(0.05);
        req.set_volatility(0.2);
        req.set_years_to_expiry(1.0);
        req.set_paths(10'000);
        req.set_steps(50);
        req.set_option_type(sensen::finance::CALL);
        sensen::finance::DoubleResponse resp;
        auto ctx = make_context();
        auto status = stub.PriceOptionMonteCarlo(ctx.get(), req, &resp);
        check(status.ok() && std::isfinite(resp.value()) && resp.value() > 0.0,
              "...a realistic 10,000-path/50-step call still prices successfully: value=" +
                  std::to_string(resp.value()));
    }

    // =======================================================================
    section("21. ComputeFutureValueDetailed: the SAME missing-guard shape as ComputeAmortization, "
            "never retrofitted onto this RPC either");
    // =======================================================================
    {
        // annual_rate/annual_contribution/current_principal used plain
        // REQUIRE_DECIMAL (no magnitude bound), and years/compound_frequency
        // were each only floor-checked (years >= 0, compound_frequency > 0)
        // with NO ceiling -- calculate_future_value_detailed
        // (financial.cppm) divides annual_rate by compound_frequency and
        // raises (1+rate_per_period) to years*compound_frequency via
        // BigDecimal::pow, the identical missing-guard shape
        // ComputeAmortization/ComputeMortgageRecast/ComputePayment already
        // carry. Reproduced directly: {annual_rate=1000000, years=100,
        // compound_frequency=12, annual_contribution=5000,
        // current_principal=10000} returned Status::OK with
        // total_interest_earned wrapped to a fabricated, astronomically
        // large-but-finite BigDecimal figure rather than being refused.
        sensen::finance::FutureValueDetailedRequest req;
        req.set_annual_rate("1000000");
        req.set_years(100);
        req.set_annual_contribution("5000");
        req.set_current_principal("10000");
        req.set_compound_frequency(12);
        sensen::finance::FutureValueDetailedResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeFutureValueDetailed(ctx.get(), req, &resp);
        check(is_invalid_argument(status),
              "ComputeFutureValueDetailed{annual_rate=1000000, years=100, "
              "compound_frequency=12} is REJECTED -- the same compound-growth-overflow shape "
              "as ComputeAmortization, never retrofitted onto this RPC");
    }
    {
        // years had no ceiling at all -- combined with an unbounded
        // compound_frequency, `years * compound_frequency`
        // (financial.cppm's total_periods, an int) can overflow int32
        // outright, which is undefined behaviour regardless of whether the
        // request is later refused for its magnitude.
        sensen::finance::FutureValueDetailedRequest req;
        req.set_annual_rate("0.05");
        req.set_years(2'000'000'000);
        req.set_annual_contribution("1000");
        req.set_current_principal("1000");
        req.set_compound_frequency(12);
        sensen::finance::FutureValueDetailedResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeFutureValueDetailed(ctx.get(), req, &resp);
        check(is_invalid_argument(status),
              "ComputeFutureValueDetailed{years=2,000,000,000} is REJECTED by a years<=100 "
              "ceiling -- no real projection horizon needs more, and this closes the "
              "years*compound_frequency int32 overflow before it can happen");
    }
    {
        // annual_inflation_rate also raises (1+rate) to `years` via
        // BigDecimal::pow with no compound-growth bound of its own.
        sensen::finance::FutureValueDetailedRequest req;
        req.set_annual_rate("0.05");
        req.set_years(50);
        req.set_annual_contribution("1000");
        req.set_current_principal("1000");
        req.set_annual_inflation_rate("1000000");
        req.set_compound_frequency(12);
        sensen::finance::FutureValueDetailedResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeFutureValueDetailed(ctx.get(), req, &resp);
        check(is_invalid_argument(status),
              "ComputeFutureValueDetailed{annual_inflation_rate=1000000, years=50} is REJECTED "
              "-- the inflation adjustment's own pow(years) carries the identical missing "
              "guard");
    }
    {
        // Positive control: a realistic 20-year monthly-compounding
        // projection still succeeds, and at the same figures the RPC always
        // produced.
        sensen::finance::FutureValueDetailedRequest req;
        req.set_annual_rate("0.06");
        req.set_years(20);
        req.set_annual_contribution("5000");
        req.set_current_principal("10000");
        req.set_compound_frequency(12);
        sensen::finance::FutureValueDetailedResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeFutureValueDetailed(ctx.get(), req, &resp);
        check(status.ok(), "...but a realistic 20-year, 6%, monthly-compounding projection "
                            "with $5000/yr contributions on a $10000 principal still succeeds");
        check(!resp.nominal_fv().empty() && !resp.total_interest_earned().empty(),
              "...returning a non-empty nominal_fv (" + resp.nominal_fv() +
                  ") and total_interest_earned (" + resp.total_interest_earned() + ")");
    }

    // =======================================================================
    section("22. ComputeAmortizationBatch / ComputeCumulative: magnitude overflow is UB on the "
            "CPU path too, not just NaN");
    // =======================================================================
    {
        // Flagged by a concurrent architecture-analysis pass: require_finite
        // alone does NOT close the BigDecimal(double) UB this file's own
        // section-11/section-4 comments already document for NaN --
        // BigDecimal{double}'s constructor is
        // `static_cast<__int128_t>(std::round(val * 1e18))` (bigdecimal.cppm),
        // and a FINITE loan_amount as "small" as 1e30 scales to ~1e48, far
        // outside __int128's ~1.7e38 range. Casting a value the destination
        // type cannot represent is undefined behaviour per [conv.fpint]
        // regardless of whether the source value is finite. The
        // string-money RPCs elsewhere in this file already carry a raw-string
        // magnitude guard (check_decimal_string_magnitude, via
        // REQUIRE_DECIMAL_SAFE) -- ComputeAmortizationBatch takes raw
        // doubles instead of decimal strings and never got the equivalent.
        sensen::finance::AmortizationBatchRequest req;
        req.add_loan_amounts(1e30);
        req.add_annual_rates(0.05);
        req.add_term_months(360);
        req.add_extra_payments(0.0);
        req.add_pmi_rates(0.0);
        req.add_home_values(0.0);
        sensen::finance::AmortizationBatchResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeAmortizationBatch(ctx.get(), req, &resp);
        check(is_invalid_argument(status),
              "ComputeAmortizationBatch{loan_amounts=[1e30]} (finite, but far past "
              "BigDecimal's representable range once scaled by 1e18) is REJECTED");
        check(status.error_message().find("loan_amounts") != std::string::npos,
              "...naming loan_amounts specifically: \"" + status.error_message() + "\"");
    }
    {
        sensen::finance::AmortizationBatchRequest req;
        req.add_loan_amounts(300000.0);
        req.add_annual_rates(1e30);
        req.add_term_months(360);
        req.add_extra_payments(0.0);
        req.add_pmi_rates(0.0);
        req.add_home_values(0.0);
        sensen::finance::AmortizationBatchResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeAmortizationBatch(ctx.get(), req, &resp);
        check(is_invalid_argument(status),
              "ComputeAmortizationBatch{annual_rates=[1e30]} is REJECTED the same way");
    }
    {
        // Positive control: a legitimate, large-but-sane loan (a real
        // billion-dollar commercial mortgage, well under the 1e15 bound)
        // still computes -- the fix is a magnitude bound far above any real
        // loan, not a rejection of large-but-real ones.
        sensen::finance::AmortizationBatchRequest req;
        req.add_loan_amounts(1e9);
        req.add_annual_rates(0.05);
        req.add_term_months(360);
        req.add_extra_payments(0.0);
        req.add_pmi_rates(0.0);
        req.add_home_values(0.0);
        sensen::finance::AmortizationBatchResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeAmortizationBatch(ctx.get(), req, &resp);
        check(status.ok() && resp.summaries_size() == 1,
              "...but a legitimate $1 billion loan (loan_amounts=[1e9], well under the "
              "1e15 bound) still computes successfully");
        check(status.ok() && std::stod(resp.summaries(0).total_principal_paid()) > 9.9e8,
              "...paying off very close to the full principal over the loan's life "
              "(total_principal_paid=" +
                  (status.ok() ? resp.summaries(0).total_principal_paid() : "n/a") + ")");
    }
    {
        // ComputeCumulative has the identical shape -- `rate`/`present_value`
        // are raw doubles fed directly to `BigDecimal{double}` with no
        // decimal-string parse step for check_decimal_string_magnitude to
        // guard, and no magnitude bound of its own before this fix either.
        sensen::finance::CumulativeRequest req;
        req.set_component(sensen::finance::CumulativeRequest::INTEREST);
        req.set_rate(1e30);
        req.set_periods(360);
        req.set_present_value(300000.0);
        req.set_start_period(1);
        req.set_end_period(1);
        sensen::finance::DoubleResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeCumulative(ctx.get(), req, &resp);
        check(is_invalid_argument(status), "ComputeCumulative{rate=1e30} is REJECTED");
    }
    {
        sensen::finance::CumulativeRequest req;
        req.set_component(sensen::finance::CumulativeRequest::INTEREST);
        req.set_rate(0.05);
        req.set_periods(360);
        req.set_present_value(1e30);
        req.set_start_period(1);
        req.set_end_period(1);
        sensen::finance::DoubleResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeCumulative(ctx.get(), req, &resp);
        check(is_invalid_argument(status), "ComputeCumulative{present_value=1e30} is REJECTED");
    }
    {
        // Positive control: a legitimate, large-but-sane present_value (a
        // billion-dollar institutional loan) still computes.
        sensen::finance::CumulativeRequest req;
        req.set_component(sensen::finance::CumulativeRequest::INTEREST);
        req.set_rate(0.05);
        req.set_periods(360);
        req.set_present_value(1e9);
        req.set_start_period(1);
        req.set_end_period(12);
        sensen::finance::DoubleResponse resp;
        auto ctx = make_context();
        auto status = stub.ComputeCumulative(ctx.get(), req, &resp);
        check(status.ok() && std::isfinite(resp.value()) && resp.value() < 0.0,
              "...but a legitimate $1 billion present_value still computes a finite, "
              "negative (interest is an outflow) cumulative-interest figure: got " +
                  std::to_string(resp.value()));
    }

    // =======================================================================
    section("23. ComputeClosingCosts: bases, bounds, presence and optionality");
    // =======================================================================
    //
    // Wired into ctest deliberately. The engine-level assertions for this
    // operation live in sensen's own test_financial.cpp, which this repo does
    // NOT build (backend/CMakeLists.txt sets BUILD_TESTS OFF FORCE for the
    // sensen subtree), and in smoke_client, which needs a running engine and is
    // not a ctest target. Without this section the whole operation had zero
    // coverage in the 99-test suite while looking covered from three places.
    {
        // The scenario the production website shows at its defaults, so the
        // expected figures are an INDEPENDENT implementation's, not ours.
        const auto reference = [] {
            sensen::finance::ClosingCostsRequest r;
            r.set_home_price("450000");
            r.set_down_payment_percent("0.10");
            r.set_annual_rate("0.0675");
            r.set_origination_fee_percent("0.0075");
            r.set_discount_points_percent("0");
            r.set_other_lender_fees("1400");
            r.set_title_settlement_percent("0.0055");
            r.set_appraisal_fee("650");
            r.set_inspection_fee("500");
            r.set_recording_fees("225");
            r.set_transfer_tax_percent("0.005");
            r.set_homeowners_insurance_annual("2100");
            r.set_property_tax_annual("6300");
            r.set_tax_escrow_months(3);
            r.set_seller_lender_credits("0");
            // prepaid_interest_days deliberately absent -> 15-day convention.
            return r;
        };

        {
            auto req = reference();
            sensen::finance::ClosingCostsResponse resp;
            auto ctx = make_context();
            auto status = stub.ComputeClosingCosts(ctx.get(), req, &resp);
            check(status.ok(), "the reference scenario computes");
            if (status.ok()) {
                const double price = 450000.0;
                const double loan = price * 0.90;
                // Each percentage recomputed HERE against its own base. A
                // single wrong base still sums to the same subtotal, so the
                // sum identity alone cannot see it.
                check(std::abs(std::stod(resp.loan_amount()) - loan) < 0.005,
                      "loan = price - down payment");
                check(std::abs(std::stod(resp.origination_fee()) - loan * 0.0075) < 0.005,
                      "origination is 0.75% of the LOAN, not the price");
                check(std::abs(std::stod(resp.title_settlement()) - price * 0.0055) < 0.005,
                      "title is 0.55% of the PRICE, not the loan");
                check(std::abs(std::stod(resp.transfer_tax()) - price * 0.005) < 0.005,
                      "transfer tax is 0.5% of the PRICE");
                check(std::abs(std::stod(resp.property_tax_escrow()) - 6300.0 * 3.0 / 12.0) < 0.005,
                      "escrow is three twelfths of the annual tax bill");
                check(std::abs(std::stod(resp.prepaid_interest()) -
                               loan * 0.0675 / 365.0 * 15.0) < 0.01,
                      "prepaid interest is loan x rate / 365 x 15");
                check(resp.prepaid_interest_days() == 15,
                      "an ABSENT prepaid_interest_days resolves to the 15-day convention");
                check(std::abs(std::stod(resp.itemised_subtotal()) - 15335.9589) < 0.01,
                      "subtotal agrees with the reference implementation (15,336)");
                check(std::abs(std::stod(resp.total_cash_to_close()) - 60335.9589) < 0.01,
                      "cash to close agrees with the reference implementation (60,336)");
            }
        }
        {
            // Explicit presence: 0 must mean ZERO days, not the convention.
            // This is the whole reason the field is `optional int32`.
            auto req = reference();
            req.set_prepaid_interest_days(0);
            sensen::finance::ClosingCostsResponse resp;
            auto ctx = make_context();
            auto status = stub.ComputeClosingCosts(ctx.get(), req, &resp);
            check(status.ok() && resp.prepaid_interest_days() == 0 &&
                      std::stod(resp.prepaid_interest()) == 0.0,
                  "an EXPLICIT prepaid_interest_days = 0 means zero days, not 15 -- a closing "
                  "on the last day of a month owes no prepaid interest, and the sentinel this "
                  "replaced made that unrepresentable");
        }
        {
            // An all-cash purchase: no loan, so no lender lines and no prepaid
            // interest, but title/appraisal/recording/transfer/escrow all stand.
            auto req = reference();
            req.set_down_payment_percent("1.0");
            sensen::finance::ClosingCostsResponse resp;
            auto ctx = make_context();
            auto status = stub.ComputeClosingCosts(ctx.get(), req, &resp);
            check(status.ok(), "an ALL-CASH purchase (100% down) is accepted, not refused");
            if (status.ok()) {
                check(std::stod(resp.loan_amount()) == 0.0 &&
                          std::stod(resp.origination_fee()) == 0.0 &&
                          std::stod(resp.prepaid_interest()) == 0.0,
                      "...with every loan-derived charge at zero");
                check(std::abs(std::stod(resp.title_settlement()) - 450000.0 * 0.0055) < 0.005,
                      "...but the title fee, which is owed on the PRICE, is still charged");
            }
        }
        {
            // Eleven of the sixteen fields are genuinely optional. An omitted
            // one must read as zero rather than refusing the request.
            sensen::finance::ClosingCostsRequest req;
            req.set_home_price("450000");
            req.set_down_payment_percent("0.10");
            req.set_annual_rate("0.0675");
            sensen::finance::ClosingCostsResponse resp;
            auto ctx = make_context();
            auto status = stub.ComputeClosingCosts(ctx.get(), req, &resp);
            check(status.ok(), "a minimal three-field request is accepted");
            if (status.ok()) {
                check(std::abs(std::stod(resp.itemised_subtotal()) -
                               std::stod(resp.prepaid_interest())) < 0.005,
                      "...and with every optional cost omitted only prepaid interest remains");
            }
        }

        // -- refusals. Each is a request that would otherwise return a
        //    confident, wrong number rather than an error.
        struct Bad { const char* what; std::function<void(sensen::finance::ClosingCostsRequest&)> poison; };
        const Bad bad[] = {
            {"down_payment_percent above 1 (loan would go negative)",
             [](auto& r){ r.set_down_payment_percent("1.5"); }},
            {"a NEGATIVE seller credit (a surcharge wearing a credit's name)",
             [](auto& r){ r.set_seller_lender_credits("-5000"); }},
            {"a negative fee",
             [](auto& r){ r.set_appraisal_fee("-650"); }},
            {"a fee share above 100%",
             [](auto& r){ r.set_origination_fee_percent("1.5"); }},
            {"a whole-number percent where a decimal fraction belongs (75 for 0.75%)",
             [](auto& r){ r.set_title_settlement_percent("75"); }},
            {"tax_escrow_months beyond two years",
             [](auto& r){ r.set_tax_escrow_months(25); }},
            {"prepaid_interest_days beyond a year",
             [](auto& r){ r.set_prepaid_interest_days(366); }},
            {"a negative escrow month count",
             [](auto& r){ r.set_tax_escrow_months(-1); }},
            {"a malformed decimal string",
             [](auto& r){ r.set_home_price("4.5.0"); }},
            {"an empty REQUIRED field",
             [](auto& r){ r.set_home_price(""); }},
            {"a zero price (every percentage line would be meaningless)",
             [](auto& r){ r.set_home_price("0"); }},
        };
        for (const auto& b : bad) {
            auto req = reference();
            b.poison(req);
            sensen::finance::ClosingCostsResponse resp;
            auto ctx = make_context();
            auto status = stub.ComputeClosingCosts(ctx.get(), req, &resp);
            check(is_invalid_argument(status),
                  std::string("REJECTED: ") + b.what);
        }
        {
            // A credit larger than the bill, checked SEPARATELY because it
            // answers a different status code -- and deliberately so.
            //
            // Every refusal above is bad in isolation: a negative fee is
            // invalid whatever else the request says, so it is INVALID_ARGUMENT
            // and the service checks it before dispatching. A credit of
            // $100,000 is not invalid in isolation at all -- it is ordinary on
            // a larger closing. It is wrong only RELATIVE to the subtotal this
            // request computes, which cannot be known until the itemisation
            // runs. That is FAILED_PRECONDITION's meaning, and it is the code
            // the engine's own refusal carries through `fail()`.
            //
            // Asserted as a specific code rather than "not ok" so that a
            // future change collapsing the two back together fails here.
            auto req = reference();
            req.set_seller_lender_credits("100000");
            sensen::finance::ClosingCostsResponse resp;
            auto ctx = make_context();
            auto status = stub.ComputeClosingCosts(ctx.get(), req, &resp);
            check(!status.ok() &&
                      status.error_code() == grpc::StatusCode::FAILED_PRECONDITION,
                  "REJECTED (FAILED_PRECONDITION, not INVALID_ARGUMENT): a credit larger "
                  "than the itemised bill -- valid in isolation, impossible for THIS bill");
        }
        {
            // Zero escrow months is NOT an error -- plenty of loans collect no
            // escrow reserve. Paired with the -1 refusal above so the bound is
            // shown to discriminate rather than merely refuse.
            auto req = reference();
            req.set_tax_escrow_months(0);
            sensen::finance::ClosingCostsResponse resp;
            auto ctx = make_context();
            auto status = stub.ComputeClosingCosts(ctx.get(), req, &resp);
            check(status.ok() && std::stod(resp.property_tax_escrow()) == 0.0,
                  "...but ZERO escrow months is accepted and collects nothing -- a loan "
                  "without an escrow account is ordinary, not an error");
        }
    }

    // -----------------------------------------------------------------
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
