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

    // -----------------------------------------------------------------
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
