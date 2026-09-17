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
#include <cstdio>


#include <grpcpp/grpcpp.h>
#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include <google/protobuf/text_format.h>
#include "finance.pb.h"
#include "finance.grpc.pb.h"

import std;
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
        req.add_dates(365.0);  // one year, in the DAY offsets the wire takes
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
        req.add_dates(365.0);  // one year, in the DAY offsets the wire takes
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
        req.add_dates(365.0);  // one year, in the DAY offsets the wire takes
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
    // 24. ComputeRentVsBuy shape dispatch -- the WHOLE decision graph
    //
    // Why this section exists: on 2026-08-27 every ComputeRentVsBuy request
    // the ASSISTANT can emit -- both label shapes -- was refused with
    // INVALID_ARGUMENT. 100% of assistant traffic on the feature this
    // workstream exists to add, in production-shaped code, with a green test
    // suite. Section 23's style of test could not see it, because every test
    // here built its request the way a C++ caller would: omitting what it did
    // not need. The assistant CANNOT omit -- mortgage_verification.cppm's G2b
    // refuses a missing key -- so it says "not this shape" with the convention
    // value "0", and `.empty()` reads that as "present". One caller's silence
    // is the other caller's zero.
    //
    // So this section does two things no other section does:
    //
    //   (a) It enumerates the FULL cross product of the dispatch's own input
    //       space -- 5 composite signals x 4 group signals = 20 cells -- and
    //       names the expected outcome of each. A decision left undecided by
    //       accident shows up as a cell, not as a silence.
    //   (b) It sends the BYTES A MODEL ACTUALLY PRODUCES, taken verbatim from
    //       corpus B, rather than bytes a test author found convenient.
    //
    // Mutation checks, both of which reproduce the production symptom:
    //   - restoring `!field.empty()` as the legacy predicate flips every
    //     `assistant-*` row to BOTH-shapes and fails 4 checks;
    //   - testing only the legacy side for magnitude (the earlier
    //     `is_positive()` attempt) flips `zero x absent` and
    //     `absent x all-zero` back to a 200 OK carrying an invented loan.
    {
        std::printf("\n-- 24. ComputeRentVsBuy shape dispatch: the whole decision graph\n");

        enum class Want { Legacy, Amortising, Neither, Both, NegativeCost, Parse };

        // The seven amortising fields, in the three states a caller can put
        // them in, plus the malformed state that must outrank every shape.
        const auto apply_group = [](sensen::finance::RentVsBuyRequest& r,
                                    std::string_view group) {
            if (group == "absent") {
                return;  // a JSON caller omits them
            }
            if (group == "malformed") {
                r.set_loan_annual_rate("xyz");
                return;
            }
            const bool zero = (group == "all-zero");
            // "all-zero" is EXACTLY what the assistant emits for the shape it
            // is not using. It is not a contrived input.
            r.set_loan_annual_rate(zero ? "0.0000" : "0.0533");
            r.set_loan_term_years(zero ? 0 : 30);
            r.set_loan_amount(zero ? "0.00" : "476200.00");
            r.set_monthly_taxes_ins_maintenance(zero ? "0.00" : "1100.00");
            r.set_closing_costs_buy(zero ? "0.00" : "11300.00");
            r.set_selling_cost_percent(zero ? "0.0000" : "0.0600");
            r.set_annual_inflation_rate(zero ? "0.0000" : "0.0270");
        };

        const auto reference = []() {
            sensen::finance::RentVsBuyRequest r;
            r.set_property_price("546500.00");
            r.set_down_payment("70300.00");
            r.set_annual_home_appreciation("0.0456");
            r.set_current_monthly_rent("2900.00");
            r.set_annual_rent_increase("0.0487");
            r.set_annual_investment_return("0.0521");
            r.set_years(4);
            return r;
        };

        // An amortising answer is distinguishable from a legacy one WITHOUT
        // trusting the status: the legacy path sets only fields 1-4 and leaves
        // every string field empty, precisely so it cannot invent digits a
        // double never had. So a non-empty monthly_payment IS the amortising
        // model's signature.
        const auto ran_amortising = [](const sensen::finance::RentVsBuyResponse& r) {
            return !r.buying_advantage_exact().empty();
        };

        struct Cell {
            const char* composite;  // nullptr => field never set (absent)
            const char* group;
            Want want;
            const char* note;
        };
        const std::array<Cell, 20> kGraph{{
            {nullptr,    "absent",      Want::Neither,      "no question asked"},
            {nullptr,    "all-zero",    Want::Neither,      "all conventions: still no question"},
            {nullptr,    "substantive", Want::Amortising,   "JSON amortising caller"},
            {nullptr,    "malformed",   Want::Parse,        "parse outranks shape"},
            {"0.00",     "absent",      Want::Neither,      "assistant said 'neither shape'"},
            {"0.00",     "all-zero",    Want::Neither,      "every field a convention"},
            {"0.00",     "substantive", Want::Amortising,   "ASSISTANT amortising label"},
            {"0.00",     "malformed",   Want::Parse,        "parse outranks shape"},
            {"3200.00",  "absent",      Want::Legacy,       "JSON legacy caller / deployed v2"},
            {"3200.00",  "all-zero",    Want::Legacy,       "ASSISTANT legacy label"},
            {"3200.00",  "substantive", Want::Both,         "genuine contradiction"},
            {"3200.00",  "malformed",   Want::Parse,        "parse outranks shape"},
            {"-100",     "absent",      Want::NegativeCost, "a cost cannot be negative"},
            {"-100",     "all-zero",    Want::NegativeCost, "negative outranks shape"},
            {"-100",     "substantive", Want::NegativeCost, "negative outranks both-shapes"},
            {"-100",     "malformed",   Want::Parse,        "parse outranks negative"},
            {"abc",      "absent",      Want::Parse,        "malformed composite"},
            {"abc",      "all-zero",    Want::Parse,        "malformed outranks shape"},
            {"abc",      "substantive", Want::Parse,        "malformed outranks shape"},
            {"abc",      "malformed",   Want::Parse,        "both malformed"},
        }};

        for (const auto& cell : kGraph) {
            auto req = reference();
            if (cell.composite != nullptr) {
                req.set_monthly_piti_and_maintenance(cell.composite);
            }
            apply_group(req, cell.group);

            sensen::finance::RentVsBuyResponse resp;
            auto ctx = make_context();
            auto status = stub.ComputeRentVsBuy(ctx.get(), req, &resp);

            const std::string detail = status.error_message();
            const auto says = [&detail](const char* needle) {
                return detail.find(needle) != std::string::npos;
            };

            bool matched = false;
            switch (cell.want) {
                case Want::Legacy:
                    matched = status.ok() && !ran_amortising(resp);
                    break;
                case Want::Amortising:
                    matched = status.ok() && ran_amortising(resp);
                    break;
                case Want::Both:
                    matched = !status.ok() &&
                              status.error_code() == grpc::StatusCode::INVALID_ARGUMENT &&
                              says("cannot be combined");
                    break;
                case Want::Neither:
                    matched = !status.ok() &&
                              status.error_code() == grpc::StatusCode::INVALID_ARGUMENT &&
                              says("carries neither");
                    break;
                case Want::NegativeCost:
                    matched = !status.ok() &&
                              status.error_code() == grpc::StatusCode::INVALID_ARGUMENT &&
                              says("cannot be negative");
                    break;
                case Want::Parse:
                    // Reported by name, by the field that failed -- NOT by the
                    // shape decision. Asserting the code alone would pass on a
                    // shape refusal, which is the wrong diagnostic entirely.
                    matched = !status.ok() &&
                              status.error_code() == grpc::StatusCode::INVALID_ARGUMENT &&
                              (says("not a decimal") || says("is not a valid"));
                    break;
            }
            check(matched, std::string{"graph cell: composite="} +
                               (cell.composite == nullptr ? "<absent>" : cell.composite) +
                               " group=" + cell.group + " -> " + cell.note);
        }

        {
            // Backward compatibility, stated as an EQUALITY rather than as two
            // separate successes. The deployed v2 model omits the seven fields
            // it was never taught; the retrained model emits them as zeros.
            // Those are the same request and must produce the same number --
            // if they diverge, the convention stopped being a convention.
            auto omitted = reference();
            omitted.set_monthly_piti_and_maintenance("3200.00");

            auto zeroed = reference();
            zeroed.set_monthly_piti_and_maintenance("3200.00");
            apply_group(zeroed, "all-zero");

            sensen::finance::RentVsBuyResponse a;
            sensen::finance::RentVsBuyResponse b;
            auto ctx_a = make_context();
            auto ctx_b = make_context();
            auto sa = stub.ComputeRentVsBuy(ctx_a.get(), omitted, &a);
            auto sb = stub.ComputeRentVsBuy(ctx_b.get(), zeroed, &b);
            check(sa.ok() && sb.ok() &&
                      a.buying_advantage() == b.buying_advantage() &&
                      a.is_buying_better() == b.is_buying_better(),
                  "v2 (omits the seven) and the retrained model (emits them as zeros) get "
                  "the IDENTICAL legacy answer -- one convention, two spellings");
        }
    }

    // =======================================================================
    section("24b. ComputeRentVsBuy: the costs only an OWNER meets");
    // =======================================================================
    //
    // Repairs, mortgage insurance, an overpayment and a HELOC reached the WIRE
    // on 2026-09-15. They had been in sensen and gated there since the same
    // day, and unreachable from every client: correct, tested, and callable by
    // nobody. `RentVsBuyRequest` had 15 fields and the engine took 21.
    //
    // Each one moves the comparison in the SAME direction -- against buying --
    // so omitting them was never neutral, and the error grew with exactly the
    // horizon the comparison exists to show.
    {
        const auto base = []() {
            sensen::finance::RentVsBuyRequest r;
            r.set_property_price("400000.00");
            r.set_down_payment("40000.00");
            r.set_annual_home_appreciation("0.0300");
            r.set_current_monthly_rent("2200.00");
            r.set_annual_rent_increase("0.0300");
            r.set_annual_investment_return("0.0700");
            r.set_years(10);
            r.set_loan_annual_rate("0.0650");
            r.set_loan_term_years(30);
            r.set_monthly_taxes_ins_maintenance("650.00");
            r.set_closing_costs_buy("8000.00");
            return r;
        };
        const auto run = [&](const sensen::finance::RentVsBuyRequest& req,
                             sensen::finance::RentVsBuyResponse& resp) {
            auto ctx = make_context();
            return stub.ComputeRentVsBuy(ctx.get(), req, &resp);
        };

        sensen::finance::RentVsBuyResponse plain;
        check(run(base(), plain).ok(), "the reference scenario answers");
        // Compared as a NUMBER, not as the string "0.00". BigDecimal renders
        // eighteen places, so the exact spelling is "0.000000000000000000" --
        // asserting the literal tested this test's idea of the format rather
        // than the value it cares about.
        check(plain.total_repairs_paid().empty() ||
                  std::fabs(std::stod(plain.total_repairs_paid())) < 1e-9,
              "omitted repairs report zero, not a guess at a national average");
        check(plain.pmi_ends_month() == 0,
              "and with no mortgage insurance there is no cancellation month");

        {   // repairs reach the engine and move the answer
            auto req = base();
            req.set_annual_repairs("4800.00");
            sensen::finance::RentVsBuyResponse r;
            check(run(req, r).ok(), "a repair budget is accepted");
            check(!r.total_repairs_paid().empty() && r.total_repairs_paid() != "0.00",
                  "repairs are accumulated and reported as their own line");
            check(r.total_cost_of_buying() > plain.total_cost_of_buying(),
                  "buying costs more once the roof is in the model");
            check(std::fabs(r.total_rent_paid().empty() ? 0.0 : std::stod(r.total_rent_paid()) -
                            std::stod(plain.total_rent_paid())) < 0.5,
                  "and the RENTER pays exactly what they paid before -- a "
                  "landlord's repairs are not the tenant's bill");
        }

        {   // PMI cancels, and an overpayment pulls that date forward
            auto req = base();
            req.set_pmi_annual_rate("0.0060");
            sensen::finance::RentVsBuyResponse a;
            check(run(req, a).ok(), "mortgage insurance is accepted");
            check(a.pmi_ends_month() > 0 && a.pmi_ends_month() < 120,
                  "it STOPS inside the horizon rather than running for the term");

            req.set_monthly_overpayment("400.00");
            sensen::finance::RentVsBuyResponse b;
            check(run(req, b).ok(), "an overpayment is accepted");
            check(b.pmi_ends_month() < a.pmi_ends_month(),
                  "and pulls the cancellation forward -- the interaction no "
                  "separate PMI calculator can show");
            check(std::stod(b.total_pmi_paid()) < std::stod(a.total_pmi_paid()),
                  "so less mortgage insurance is paid in total");
        }

        {   // a HELOC costs interest and is refused without a term
            auto req = base();
            req.set_heloc_drawn_amount("30000.00");
            req.set_heloc_annual_rate("0.0850");
            sensen::finance::RentVsBuyResponse bad;
            auto s = run(req, bad);
            check(s.error_code() == grpc::StatusCode::INVALID_ARGUMENT &&
                      s.error_message().find("heloc_term_years") != std::string::npos,
                  "a draw with NO repayment term is refused: it would reduce the "
                  "cash invested and then cost nothing, understating buying");

            req.set_heloc_term_years(10);
            sensen::finance::RentVsBuyResponse good;
            check(run(req, good).ok(), "and is accepted once a term is given");
            check(!good.total_heloc_interest_paid().empty() &&
                      std::stod(good.total_heloc_interest_paid()) > 0,
                  "the draw costs interest, reported on its own line");
            check(std::fabs(std::stod(good.final_loan_balance()) -
                            std::stod(plain.final_loan_balance())) < 1.0,
                  "and never touches the MORTGAGE balance -- it is secured elsewhere");
        }

        {   // THE SILENT DROP THIS GUARD EXISTS FOR
            //
            // The legacy composite already conflates repairs and mortgage
            // insurance into its one number. Before these fields joined the
            // amortising group signal, pairing them with the composite took the
            // LEGACY path and discarded them -- a 200 OK computed from a model
            // that never saw the repairs the caller just stated.
            auto req = base();
            req.clear_loan_annual_rate();
            req.clear_loan_term_years();
            req.clear_monthly_taxes_ins_maintenance();
            req.clear_closing_costs_buy();
            req.set_monthly_piti_and_maintenance("3200.00");
            req.set_annual_repairs("4800.00");
            sensen::finance::RentVsBuyResponse r;
            auto s = run(req, r);
            check(s.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
                  "the legacy composite paired with annual_repairs is REFUSED, "
                  "not answered from a model that drops the repairs");
        }

        {   // refusals rather than plausible answers
            const auto refused = [&](auto&& mutate, const char* what) {
                auto req = base();
                mutate(req);
                sensen::finance::RentVsBuyResponse r;
                auto s = run(req, r);
                check(s.error_code() == grpc::StatusCode::INVALID_ARGUMENT, what);
            };
            refused([](auto& r) { r.set_annual_repairs("-100.00"); },
                    "a negative repair budget is refused");
            refused([](auto& r) { r.set_pmi_annual_rate("-0.01"); },
                    "a negative mortgage-insurance rate is refused");
            refused([](auto& r) { r.set_monthly_overpayment("-50.00"); },
                    "a negative overpayment is refused");
            refused([](auto& r) { r.set_heloc_term_years(101); },
                    "a HELOC term beyond a century is refused");
        }
    }

    // =======================================================================
    section("24e. ExplainMortgage: the scenario in words, traceable to figures");
    // =======================================================================
    //
    // Every sentence is a Horn-clause derivation over facts the schedule
    // ALREADY produced. There is no model and no template with a number
    // substituted in -- the layer never holds a figure the amortisation did
    // not compute, which is what makes the output auditable rather than
    // merely fluent.
    {
        const auto base = []() {
            sensen::finance::ExplainMortgageRequest r;
            r.set_loan_amount("300000.00");
            r.set_annual_rate("0.065");
            r.set_term_months(360);
            r.set_pmi_annual_rate("0.006");
            r.set_original_home_value("330000.00");
            return r;
        };
        const auto run = [&](const sensen::finance::ExplainMortgageRequest& req,
                             sensen::finance::ExplainMortgageResponse& resp) {
            auto ctx = make_context();
            return stub.ExplainMortgage(ctx.get(), req, &resp);
        };

        sensen::finance::ExplainMortgageResponse r;
        check(run(base(), r).ok(), "a scenario is explained");
        check(r.points_size() > 0, "and produces points");

        // ITEMISED 1..N WITH NO GAPS, across BOTH halves. The PMI derivation
        // and the output-driven totals each number from 1 on their own; an
        // export printing "1, 2, 1, 2" beside five sentences is worse than no
        // numbering at all.
        bool numbered = true;
        for (int i = 0; i < r.points_size(); ++i) {
            if (r.points(i).item() != i + 1) { numbered = false; }
        }
        check(numbered, "items run 1..N with no gaps and no restart");

        // THE LINK BACK. A sentence an export cannot anchor to a row is a
        // sentence the reader has to take on trust.
        bool anchored = true;
        for (const auto& p : r.points()) {
            if (p.topic().empty() || p.text().empty()) { anchored = false; }
        }
        check(anchored, "each carries a stable topic and a sentence");

        bool traceable = false;
        for (const auto& p : r.points()) {
            if (!p.field().empty() && !p.value().empty()) { traceable = true; }
        }
        check(traceable,
              "and the output-driven points name their line and its figure, "
              "so an export can link the sentence to the row");

        // THE FIGURES ARE THE SCHEDULE'S OWN. Compared against what
        // ComputeDetailedAmortization returns for the identical scenario --
        // if the explainer could reshape a number, this is where it shows.
        sensen::finance::DetailedAmortizationRequest dreq;
        dreq.set_loan_amount("300000.00");
        dreq.set_annual_rate("0.065");
        dreq.set_term_months(360);
        dreq.set_pmi_annual_rate("0.006");
        dreq.set_original_home_value("330000.00");
        sensen::finance::DetailedAmortizationResponse dres;
        auto dctx = make_context();
        check(stub.ComputeDetailedAmortization(dctx.get(), dreq, &dres).ok(),
              "the same scenario computes");
        bool matches = true;
        for (const auto& p : r.points()) {
            if (p.field() == "total_interest_paid" &&
                p.value() != dres.summary().total_interest_paid()) {
                matches = false;
            }
        }
        check(matches,
              "a carried value is VERBATIM from the schedule, not re-rendered");

        // EMPTY COLUMNS ARE NOT EXPLAINED. A caller who modelled no repairs
        // gets no sentence about repairs, rather than "repairs: $0".
        bool mentions_repairs = false;
        for (const auto& p : r.points()) {
            if (p.field() == "total_repairs_paid") { mentions_repairs = true; }
        }
        check(!mentions_repairs, "an unmodelled repair budget is silence, not a zero");

        auto costed = base();
        costed.set_annual_repairs("3600.00");
        sensen::finance::ExplainMortgageResponse c;
        check(run(costed, c).ok(), "with repairs it still explains");
        bool now_mentions = false;
        for (const auto& p : c.points()) {
            if (p.field() == "total_repairs_paid") { now_mentions = true; }
        }
        check(now_mentions, "and NOW it says what they cost");

        // WHAT THE OVERPAYMENT BOUGHT. Needs two schedules -- the one asked
        // for and the one without it -- so a single run cannot answer it, and
        // the service computes the baseline only when there is an overpayment.
        auto over = base();
        over.set_monthly_overpayment("400.00");
        sensen::finance::ExplainMortgageResponse o;
        check(run(over, o).ok(), "an overpayment scenario explains");
        bool says_pulled_forward = false;
        for (const auto& p : o.points()) {
            if (p.topic() == "pmi.overpayment") { says_pulled_forward = true; }
        }
        check(says_pulled_forward,
              "and says what the extra payment BOUGHT -- a question one "
              "schedule cannot answer");
    }

    // =======================================================================
    section("24d. ComputeDetailedAmortization: the house, and a HELOC beside it");
    // =======================================================================
    //
    // The mirror of 24c. Both screens ask the same question from opposite ends
    // -- "what do I actually owe each month" -- and an owner who reaches it
    // from the mortgage side should not have to rebuild their scenario
    // elsewhere to see the answer.
    {
        const auto base = []() {
            sensen::finance::DetailedAmortizationRequest r;
            r.set_loan_amount("300000.00");
            r.set_annual_rate("0.065");
            r.set_term_months(360);
            r.set_original_home_value("375000.00");
            r.set_annual_tax_rate("0.22");
            return r;
        };
        const auto run = [&](const sensen::finance::DetailedAmortizationRequest& req,
                             sensen::finance::DetailedAmortizationResponse& resp) {
            auto ctx = make_context();
            return stub.ComputeDetailedAmortization(ctx.get(), req, &resp);
        };

        sensen::finance::DetailedAmortizationResponse plain;
        check(run(base(), plain).ok(), "the plain schedule answers");
        check(std::fabs(std::stod(plain.summary().total_repairs_paid())) < 0.01,
              "omitted repairs are zero, not a guess at a national average");

        // Repairs and the OWNER's insurance, beside the payment.
        auto costed = base();
        costed.set_annual_repairs("3600.00");
        costed.set_annual_insurance("900.00");
        sensen::finance::DetailedAmortizationResponse c;
        check(run(costed, c).ok(), "the house's own costs are accepted");
        check(std::fabs(std::stod(c.schedule(0).repairs_paid()) - 300.0) < 0.01,
              "3,600 a year is charged as 300 a month, not 3,600 in month one");
        check(std::stod(c.summary().total_cost_of_ownership()) >
                  std::stod(c.summary().total_payments_paid()),
              "cost of ownership exceeds what went to the lender");
        // THE LOAN IS UNTOUCHED. Carrying costs sit beside the payment; folding
        // them in would corrupt the interest/principal split.
        check(c.summary().total_interest_paid() == plain.summary().total_interest_paid(),
              "and the loan's own interest is byte-identical");

        // A HELOC carried alongside.
        auto withHeloc = costed;
        withHeloc.set_heloc_drawn_amount("50000.00");
        withHeloc.set_heloc_annual_rate("0.085");
        withHeloc.set_heloc_term_years(10);
        sensen::finance::DetailedAmortizationResponse h;
        check(run(withHeloc, h).ok(), "a HELOC beside the mortgage is accepted");
        check(std::stod(h.schedule(0).heloc_payment()) > 0,
              "it shows as its own per-row payment");
        check(std::fabs(std::stod(h.schedule(120).heloc_payment())) < 0.01,
              "and STOPS at month 121 when its own 10-year term ends, while the "
              "mortgage runs on");
        check(h.summary().total_interest_paid() == plain.summary().total_interest_paid(),
              "the HELOC is secured elsewhere, so THIS loan's interest is unchanged");
        check(std::stod(h.summary().total_heloc_interest_paid()) > 0,
              "while the HELOC carries its own interest total");

        auto bad = base();
        bad.set_heloc_drawn_amount("50000.00");
        sensen::finance::DetailedAmortizationResponse ig;
        check(run(bad, ig).error_code() == grpc::StatusCode::INVALID_ARGUMENT,
              "a draw with no term is refused: it would cost nothing, which "
              "makes borrowing look free");
    }

    // =======================================================================
    section("24c. ComputeHeloc: BOTH debts, or the draw looks affordable");
    // =======================================================================
    //
    // A HELOC is a SECOND lien and nobody carries one alone. The borrower owes
    // the HELOC payment AND the mortgage payment, in the same month, out of
    // the same income -- so answering with the HELOC's payment by itself is
    // arithmetically correct and practically useless. It is precisely the
    // number that makes a draw look affordable.
    {
        const auto base = []() {
            sensen::finance::HelocRequest r;
            r.set_home_value("500000.00");
            r.set_current_mortgage_balance("300000.00");
            r.set_max_ltv_rate("0.85");
            r.set_drawn_amount("75000.00");
            r.set_annual_rate("0.0850");
            r.set_repayment_term_years(15);
            r.set_payments_per_year(12);
            return r;
        };
        const auto run = [&](const sensen::finance::HelocRequest& req,
                             sensen::finance::HelocResponse& resp) {
            auto ctx = make_context();
            return stub.ComputeHeloc(ctx.get(), req, &resp);
        };

        sensen::finance::HelocResponse plain;
        check(run(base(), plain).ok(), "the HELOC alone still answers");
        check(!plain.available_equity().empty() && !plain.repayment_period_payment().empty(),
              "and returns what it always returned");
        // OPT-IN, PROVEN. A caller that predates these fields must see exactly
        // what it saw before -- the same contract rent-vs-buy's legacy path
        // keeps, and the reason that one survived a model change.
        check(plain.mortgage_schedule().empty() && plain.heloc_schedule().empty(),
              "with NO schedules, because the first mortgage was not described");
        check(plain.combined_monthly_payment().empty(),
              "and no combined payment invented from a mortgage we know nothing about");

        auto both = base();
        both.set_current_mortgage_annual_rate("0.0625");
        both.set_current_mortgage_remaining_months(264);   // 22 years left
        sensen::finance::HelocResponse r;
        check(run(both, r).ok(), "describing the first mortgage is accepted");
        check(r.mortgage_schedule_size() == 264,
              "the mortgage schedule runs its REMAINING term, not the original 360");
        check(r.heloc_schedule_size() == 180,
              "and the HELOC amortises over its own 15 years");
        check(r.repayment_period_payment() == plain.repayment_period_payment(),
              "the HELOC's own figures are UNCHANGED by describing the mortgage");

        // The mortgage schedule opens on the REMAINING balance. Using the
        // original loan would price a payment the borrower stopped making
        // years ago -- and this message never carried the original amount.
        // Compared as a NUMBER. BigDecimal renders eighteen places, so the
        // string is "300000.000000000000000000" and equality against
        // "300000.00" tests this test's idea of the format -- the third time
        // that slip has appeared in this session's work.
        check(std::fabs(std::stod(r.mortgage_schedule(0).start_balance()) - 300000.0) < 0.01,
              "it opens on the balance still owed");

        const double m0 = std::stod(r.mortgage_schedule(0).scheduled_payment());
        const double h0 = std::stod(r.heloc_schedule(0).scheduled_payment());
        check(std::fabs(std::stod(r.combined_monthly_payment()) - (m0 + h0)) < 0.01,
              "the combined payment is what has to be found each month while BOTH run");
        check(std::stod(r.combined_monthly_payment()) > m0,
              "which is strictly more than the mortgage alone -- the point of "
              "showing them together");

        // THE SCHEDULES END IN DIFFERENT MONTHS, and the combined figure
        // applies only while both are live. A 15-year HELOC against a mortgage
        // with 22 years left leaves seven years of mortgage-only payments.
        check(r.heloc_ends_month() == 180 && r.mortgage_ends_month() == 264,
              "each debt reports its own final month -- 180 and 264, not one horizon");
        check(r.mortgage_ends_month() > r.heloc_ends_month(),
              "so the borrower keeps paying the mortgage after the HELOC is gone");

        check(std::stod(r.mortgage_summary().total_interest_paid()) > 0 &&
                  std::stod(r.heloc_summary().total_interest_paid()) > 0,
              "and both carry their own interest total");

        // TWO DEBTS ARE STILL NOT THE MONTHLY OBLIGATION. An owner deciding
        // whether to draw has to find the mortgage payment, the HELOC payment
        // AND the roof, the insurer and the county -- out of one income, in
        // the same month. Stopping at debt service is the same defect as
        // quoting the HELOC payment alone, one level up.
        auto costed = both;
        costed.set_annual_repairs("3600.00");
        costed.set_annual_insurance("1800.00");
        costed.set_annual_property_tax("6000.00");
        costed.set_monthly_hoa("150.00");
        sensen::finance::HelocResponse c;
        check(run(costed, c).ok(), "the house's own costs are accepted");

        // (3600 + 1800 + 6000)/12 + 150 = 950 + 150 = 1100
        check(std::fabs(std::stod(c.monthly_carrying_costs()) - 1100.0) < 0.01,
              "annual costs are charged monthly and HOA is already monthly");
        check(std::fabs(std::stod(c.total_monthly_obligation()) -
                        (std::stod(c.combined_monthly_payment()) + 1100.0)) < 0.01,
              "the total obligation is both debts PLUS the house");
        check(std::stod(c.total_monthly_obligation()) >
                  std::stod(c.combined_monthly_payment()),
              "which is strictly more than debt service -- the point of showing it");
        check(c.combined_monthly_payment() == r.combined_monthly_payment(),
              "and the DEBT figure is unchanged: the two answer different "
              "questions, what the lenders take and what the month takes");

        // Meaningful WITHOUT the first mortgage too: an owner who did not
        // describe their mortgage still owns the roof.
        auto costs_only = base();
        costs_only.set_annual_repairs("3600.00");
        sensen::finance::HelocResponse co;
        check(run(costs_only, co).ok(), "carrying costs alone are accepted");
        check(std::fabs(std::stod(co.monthly_carrying_costs()) - 300.0) < 0.01,
              "and reported even with no mortgage schedule to attach them to");

        // Omitted means NOT MODELLED, and the total collapses to debt service.
        check(std::fabs(std::stod(r.monthly_carrying_costs())) < 0.01,
              "an omitted repair budget is zero, not a national average");
        check(std::fabs(std::stod(r.total_monthly_obligation()) -
                        std::stod(r.combined_monthly_payment())) < 0.01,
              "so the obligation collapses to debt service alone");

        // Refusals rather than plausible answers.
        auto bad = both;
        bad.set_annual_repairs("-1.00");
        sensen::finance::HelocResponse neg;
        check(run(bad, neg).error_code() == grpc::StatusCode::INVALID_ARGUMENT,
              "a negative repair budget is refused");
        bad = both;
        bad.set_current_mortgage_annual_rate("-0.01");
        sensen::finance::HelocResponse ignored;
        check(run(bad, ignored).error_code() == grpc::StatusCode::INVALID_ARGUMENT,
              "a negative mortgage rate is refused");
        bad = both;
        bad.set_current_mortgage_remaining_months(1201);
        check(run(bad, ignored).error_code() == grpc::StatusCode::INVALID_ARGUMENT,
              "and a remaining term beyond a century is refused");
    }

    // =======================================================================
    section("25. ComputeXnpv / ComputeXirr: DAYS on the wire, SECONDS in the engine");
    // =======================================================================
    //
    // `finance.proto` documents `dates` as DAY offsets. `sensen::xnpv`/`xirr`
    // divide by 31,536,000 -- they compute in SECONDS. Nothing bridged the two
    // until 2026-09-03, so a day grid reached the engine unscaled, every
    // year_frac collapsed to ~0, and ComputeXnpv -- a plain sum with no solve
    // to fail -- returned the UNDISCOUNTED SUM with a 200 OK. Measured on
    // production: flows summing to 1000 came back 999.9954. ComputeXirr failed
    // the other way, blaming its own solver ("Newton-Raphson flat derivative"),
    // which is true and useless to a caller.
    //
    // Three of the four artifacts describing this field speak days -- the
    // proto, `mortgage_verification.cppm`'s SlotKind::DayOffsets bounded at
    // 36,525, and a corpus that labels "after 372 days" as 372.0 -- so the
    // wire stayed in days and `dates_to_seconds` bridges once, at the service.
    // A seconds wire would have cost a retrain to teach the model to emit
    // 32,140,800 for a stated 372: a number the user never wrote.
    //
    // These checks are written in BOTH directions on purpose. A guard that
    // refuses seconds proves nothing on its own; the day grid computing, with
    // the discounting demonstrably applied, is the half that was broken.
    {
        // The corpus's own shape: 120 monthly flows on a cumulative DAY grid,
        // values summing to exactly 1000.
        const auto day_grid = [] {
            sensen::finance::DatedCashFlowRequest r;
            r.set_rate(0.08);
            r.add_values(-5000.0);
            r.add_dates(0.0);
            double day = 0.0;
            for (int m = 1; m <= 120; ++m) {
                day += 30.44;
                r.add_values(50.0);
                r.add_dates(day);
            }
            return r;
        };

        {
            // The direction that was WRONG. Asserting ok() alone would pass
            // against the old build that returned the plain sum, so the value
            // is checked against an INDEPENDENT closed form -- the same
            // discounting written out here in days, which is what pins the
            // conversion factor at exactly 86,400. A conversion off by any
            // amount, or dropped entirely, fails this and nothing else.
            auto req = day_grid();
            double expected = 0.0;
            double plain_sum = 0.0;
            for (int i = 0; i < req.values_size(); ++i) {
                const double year_frac = (req.dates(i) - req.dates(0)) / 365.0;
                expected += req.values(i) / std::pow(1.0 + req.rate(), year_frac);
                plain_sum += req.values(i);
            }
            sensen::finance::DoubleResponse resp;
            auto ctx = make_context();
            auto status = stub.ComputeXnpv(ctx.get(), req, &resp);
            check(status.ok() && std::abs(resp.value() - expected) < 1e-6,
                  "ComputeXnpv on a DAY grid computes, and matches the closed form "
                  "discounted at days/365 -- this pins the conversion at 86400");
            check(status.ok() && std::abs(resp.value() - plain_sum) > 100.0,
                  "and it differs from the UNDISCOUNTED sum by more than 100 -- the "
                  "discounting actually ran (it used to return 1000 -> 999.9954)");
        }
        {
            // xirr on the same grid used to refuse, blaming its solver.
            auto req = day_grid();
            sensen::finance::DoubleResponse resp;
            auto ctx = make_context();
            auto status = stub.ComputeXirr(ctx.get(), req, &resp);
            check(status.ok() && resp.value() > 0.0 && resp.value() < 1.0,
                  "ComputeXirr on the SAME grid solves to a plausible rate, where it "
                  "used to fail on 'Newton-Raphson flat derivative'");
        }
        {
            // The mirror error, and the one this contract newly creates: a
            // caller who read sensen instead of the proto sends seconds. That
            // is a century-scale day span, refused NAMING THE UNIT rather than
            // silently discounting everything to nothing.
            sensen::finance::DatedCashFlowRequest req;
            req.set_rate(0.08);
            req.add_values(-5000.0);
            req.add_dates(0.0);
            double sec = 0.0;
            for (int m = 1; m <= 120; ++m) {
                sec += 30.44 * 86400.0;
                req.add_values(50.0);
                req.add_dates(sec);
            }
            sensen::finance::DoubleResponse resp;
            auto ctx = make_context();
            auto status = stub.ComputeXnpv(ctx.get(), req, &resp);
            const std::string msg = status.error_message();
            check(is_invalid_argument(status) && msg.find("DAY offsets") != std::string::npos &&
                      msg.find("seconds") != std::string::npos,
                  "the same schedule in SECONDS is REFUSED and the refusal names BOTH "
                  "units -- what this field takes and what the caller sent");
        }
        {
            // A lone cash flow spans nothing and its present value IS its face
            // value. Refusing it would break a legitimate call to buy nothing.
            sensen::finance::DatedCashFlowRequest req;
            req.set_rate(0.08);
            req.add_values(1000.0);
            req.add_dates(0.0);
            sensen::finance::DoubleResponse resp;
            auto ctx = make_context();
            auto status = stub.ComputeXnpv(ctx.get(), req, &resp);
            check(status.ok() && std::abs(resp.value() - 1000.0) < 1e-9,
                  "a single cash flow is EXEMPT from both bounds and returns its face value");
        }
        {
            // Both boundaries, from both sides. The gap between them is what
            // makes this a discriminator rather than a magnitude heuristic:
            // the longest schedule anyone discounts is 36,525 days and the
            // shortest a seconds grid can be is 86,400.
            const auto at_span = [&](double span) {
                sensen::finance::DatedCashFlowRequest r;
                r.set_rate(0.08);
                r.add_values(-1000.0);
                r.add_values(1100.0);
                r.add_dates(0.0);
                r.add_dates(span);
                sensen::finance::DoubleResponse resp;
                auto ctx = make_context();
                return stub.ComputeXnpv(ctx.get(), r, &resp);
            };
            check(at_span(1.0).ok(), "a span of exactly one day is ADMITTED");
            check(is_invalid_argument(at_span(0.9)),
                  "a span of 0.9 days is REFUSED -- under a day nothing discounts");
            check(at_span(36'525.0).ok(), "a hundred years in days is ADMITTED");
            check(is_invalid_argument(at_span(36'526.0)),
                  "a day past a hundred years is REFUSED -- past it the caller is "
                  "sending seconds, not a schedule");
        }
        {
            // Judged on max-minus-min, not last-minus-first. An unsorted vector
            // whose real extent is a decade must not be refused because its
            // final entry happens to sit near its first.
            sensen::finance::DatedCashFlowRequest req;
            req.set_rate(0.08);
            req.add_values(-1000.0);
            req.add_values(600.0);
            req.add_values(600.0);
            req.add_dates(0.0);
            req.add_dates(3650.0);  // +10 years, in days
            req.add_dates(1.0);     // back near the start
            sensen::finance::DoubleResponse resp;
            auto ctx = make_context();
            auto status = stub.ComputeXnpv(ctx.get(), req, &resp);
            check(status.ok(),
                  "an UNSORTED vector is judged on its extent (max-min), not on "
                  "last-minus-first -- a decade of flows is admitted");
        }
        {
            // Pinned to the CORPUS, verbatim. This row is
            // agent/dataset/data_mortgage/train.jsonl's own label for "I invest
            // $147,800 today and expect back ... after 372 days; ... after 2163
            // days", and the utterance the assistant reads is in days. If the
            // wire unit is ever moved to seconds, this check is what says so --
            // it is the one assertion here written in the model's units rather
            // than the test's.
            sensen::finance::DatedCashFlowRequest req;
            req.set_rate(0.0903);
            for (const double v : {-147800.0, 23099.61, 49842.12, 56991.96, 41328.32, 43430.39,
                                   68915.12}) {
                req.add_values(v);
            }
            for (const double d : {0.0, 372.0, 694.0, 1013.0, 1353.0, 1761.0, 2163.0}) {
                req.add_dates(d);
            }
            double expected = 0.0;
            for (int i = 0; i < req.values_size(); ++i) {
                expected += req.values(i) / std::pow(1.0 + req.rate(), req.dates(i) / 365.0);
            }
            sensen::finance::DoubleResponse resp;
            auto ctx = make_context();
            auto status = stub.ComputeXnpv(ctx.get(), req, &resp);
            check(status.ok() && std::abs(resp.value() - expected) < 1e-6,
                  "a training-corpus row, sent exactly as the model emits it, computes "
                  "the closed-form answer -- the corpus and the wire agree on the unit");
        }
    }

    section("26. ComputeRate / ComputePeriods: a TVM solve needs money flowing BOTH ways");
    // =======================================================================
    //
    // Both operations solve `PV*(1+r)^n + PMT*annuity(r,n) + FV = 0`. With
    // FV = 0 that has a solution only when PV and PMT have OPPOSITE signs.
    // The training corpus emits BOTH POSITIVE -- "$1,275,100 loan,
    // $7,751.77/month" is how a person says it -- and the two operations then
    // failed in opposite directions, only one of them safely:
    //
    //   ComputeRate     FAILED_PRECONDITION "Newton-Raphson solver failed to
    //                   converge" -- true of the solver, useless to the caller
    //   ComputePeriods  **200 OK with -119.702968202252976128**
    //
    // Measured against production on 2026-09-03. The right answer is 359.955,
    // so a 30-year mortgage came back as minus ten years with nothing in the
    // response saying so. Same shape as section 25's undiscounted sum: a plain
    // formula has no solver to fail, so a convention error arrives as a
    // plausible number rather than as an error.
    {
        // The corpus's own label, verbatim.
        const auto periods_req = [](const char* pmt, const char* pv) {
            sensen::finance::PeriodsRequest r;
            r.set_rate("0.005108");
            r.set_payment(pmt);
            r.set_present_value(pv);
            r.set_future_value("0.00");
            r.set_timing(sensen::finance::END_OF_PERIOD);
            return r;
        };
        {
            auto req = periods_req("7751.77", "1275100.00");
            sensen::finance::DecimalResponse resp;
            auto ctx = make_context();
            auto status = stub.ComputePeriods(ctx.get(), req, &resp);
            const std::string msg = status.error_message();
            check(is_invalid_argument(status) && msg.find("SAME sign") != std::string::npos,
                  "ComputePeriods on same-signed PV and payment is REFUSED naming the "
                  "convention -- it used to answer 200 OK with -119.70 periods");
        }
        {
            // The admit direction, checked against the closed form rather than
            // against ok(): -ln(1 - PV*r/PMT)/ln(1+r) = 359.9554...
            auto req = periods_req("-7751.77", "1275100.00");
            sensen::finance::DecimalResponse resp;
            auto ctx = make_context();
            auto status = stub.ComputePeriods(ctx.get(), req, &resp);
            const double got = status.ok() ? std::stod(resp.value()) : 0.0;
            check(status.ok() && std::abs(got - 359.955447) < 1e-4,
                  "the same request with the payment signed computes 359.955 -- the closed "
                  "form, and a 30-year loan");
        }
        {
            // A savings goal is legitimately same-signed, and is why this is
            // scoped to FV == 0 rather than being a sign heuristic: pay in
            // every month AND start with a balance, to reach a target.
            sensen::finance::PeriodsRequest req;
            req.set_rate("0.004");
            req.set_payment("-500.00");
            req.set_present_value("-10000.00");
            req.set_future_value("100000.00");
            req.set_timing(sensen::finance::END_OF_PERIOD);
            sensen::finance::DecimalResponse resp;
            auto ctx = make_context();
            auto status = stub.ComputePeriods(ctx.get(), req, &resp);
            check(status.ok() && std::stod(resp.value()) > 0.0,
                  "a NON-ZERO future value with same-signed PV and payment is ADMITTED -- a "
                  "savings goal is exactly that shape, so this is not a sign heuristic");
        }
        {
            // ComputeRate, same input, and the message must name the cause
            // rather than the solver -- the identical complaint section 25
            // records against xirr's "flat derivative".
            sensen::finance::RateRequest req;
            req.set_periods(240);
            req.set_payment("5083.69");
            req.set_present_value("731800.00");
            req.set_future_value("0.00");
            req.set_timing(sensen::finance::END_OF_PERIOD);
            sensen::finance::DecimalResponse resp;
            auto ctx = make_context();
            auto status = stub.ComputeRate(ctx.get(), req, &resp);
            const std::string msg = status.error_message();
            check(is_invalid_argument(status) && msg.find("SAME sign") != std::string::npos &&
                      msg.find("Newton") == std::string::npos,
                  "ComputeRate names the sign convention, not 'Newton-Raphson failed to "
                  "converge'");

            req.set_payment("-5083.69");
            sensen::finance::DecimalResponse ok_resp;
            auto ctx2 = make_context();
            auto ok_status = stub.ComputeRate(ctx2.get(), req, &ok_resp);
            const double got = ok_status.ok() ? std::stod(ok_resp.value()) : 0.0;
            check(ok_status.ok() && std::abs(got - 0.004683340) < 1e-7,
                  "and with the payment signed it solves to 0.0046833 -- 5.62% a year, the "
                  "closed-form rate that fits");
        }
        {
            // The post-check is NOT redundant with the pre-check, and this is
            // the case that proves it: a payment SMALLER than the first
            // period's interest never retires the loan. Signs are opposite, so
            // the pre-check passes; the formula still yields a meaningless
            // term.
            sensen::finance::PeriodsRequest req;
            req.set_rate("0.01");
            req.set_payment("-50.00");          // interest alone is 1000/period
            req.set_present_value("100000.00");
            req.set_future_value("0.00");
            req.set_timing(sensen::finance::END_OF_PERIOD);
            sensen::finance::DecimalResponse resp;
            auto ctx = make_context();
            auto status = stub.ComputePeriods(ctx.get(), req, &resp);
            check(!status.ok(),
                  "a payment below the periodic interest does not return a term -- the "
                  "post-check catches what the sign rule cannot");
        }
    }

    section("27. ComputeDepreciation MACRS: an unsupported class was served as ZERO");
    // =======================================================================
    //
    // `sensen::macrs` returned 0.0 BOTH for "no charge in this year" and for
    // "I have no table for that class", which is unanswerable at the call site.
    // Measured against production on 2026-09-03: 15-, 20-, 27.5- and 39-year
    // classes all came back `{"value":0}` with a 200 OK, and nothing in the
    // response said the class was unsupported. 3/5/7/10 were correct
    // throughout, so the failure was invisible to any spot check that used the
    // common classes.
    //
    // 15 and 20 are now in the table. The two REAL-PROPERTY classes are refused
    // rather than approximated: both use the MID-MONTH convention, and this
    // message carries no month-placed-in-service to apply it with.
    {
        const auto dep = [](int recovery, int year) {
            sensen::finance::DepreciationRequest r;
            r.set_method(sensen::finance::DepreciationRequest::MACRS);
            r.set_cost(100000.0);
            r.set_recovery_period(recovery);
            r.set_year(year);
            return r;
        };
        const auto charge = [&](int recovery, int year, double& out) {
            auto req = dep(recovery, year);
            sensen::finance::DoubleResponse resp;
            auto ctx = make_context();
            auto st = stub.ComputeDepreciation(ctx.get(), req, &resp);
            out = st.ok() ? resp.value() : -1.0;
            return st;
        };

        {
            double v = 0.0;
            auto st = charge(15, 1, v);
            check(st.ok() && std::abs(v - 5000.0) < 1e-6,
                  "MACRS 15-year, year 1 is 5.00% of cost -- it used to answer 0 with a 200 OK");
        }
        {
            double v = 0.0;
            auto st = charge(20, 2, v);
            check(st.ok() && std::abs(v - 7219.0) < 1e-6,
                  "MACRS 20-year, year 2 is 7.219% of cost");
        }
        {
            // EVERY table must sum to the whole cost. A dropped or mistyped row
            // shows up as a plausible charge in some year rather than as an
            // error, so the closure identity is the only thing that sees it.
            for (const int recovery : {3, 5, 7, 10, 15, 20}) {
                double total = 0.0;
                bool ok = true;
                for (int y = 1; y <= recovery + 1; ++y) {
                    double v = 0.0;
                    if (!charge(recovery, y, v).ok()) { ok = false; break; }
                    total += v;
                }
                check(ok && std::abs(total - 100000.0) < 1e-6,
                      "MACRS " + std::to_string(recovery) +
                          "-year table sums to the whole cost (" + std::to_string(total) + ")");
            }
        }
        {
            double v = 0.0;
            auto st = charge(27, 1, v);
            const std::string msg = st.error_message();
            check(is_invalid_argument(st) && msg.find("MID-MONTH") != std::string::npos,
                  "a 27-year class is REFUSED naming the mid-month convention, not served as 0");
        }
        {
            double v = 0.0;
            auto st = charge(39, 1, v);
            check(is_invalid_argument(st),
                  "39-year nonresidential real property is REFUSED -- it used to return 0");
        }
        {
            // The admit direction on the classes that always worked, so the
            // refusal above is proven to be scoped rather than blanket.
            double v = 0.0;
            auto st = charge(5, 2, v);
            check(st.ok() && std::abs(v - 32000.0) < 1e-6,
                  "MACRS 5-year, year 2 is still 32% -- the refusal is scoped to classes with "
                  "no table");
        }
    }


    // =======================================================================
    section("28. EVERY declared request field REACHES the engine (reflection sweep)");
    // =======================================================================
    //
    // A field can be in the proto, in the label space, in the slot classifier,
    // in the descriptor the client encodes against, and STILL be dropped --
    // because the only thing that makes it arrive is one line in
    // finance_service.cpp reading it. Nothing above this line can see that
    // line's absence.
    //
    // It is not hypothetical and it is not rare. Twice on 2026-09-16 a field
    // was wired to the WRONG message by a `.replace(..., 1)` landing on the
    // first match: HELOC fields went into ComputeRefinance rather than
    // ComputeHeloc, and repairs went into plain ComputeAmortization rather
    // than the Detailed sibling. Both compiled. Both passed every table check,
    // because every table was right -- the tables are not where it broke.
    //
    // THE SWEEP IS OVER THE DESCRIPTOR, NOT OVER A LIST. A list written here
    // would be the sixth hand-maintained copy of the contract, and it would
    // omit exactly the field most likely to be forgotten: the newest one. The
    // descriptor is generated from the .proto, so a field added tomorrow is
    // swept tomorrow with no edit to this file. That is the same reason the
    // client's ALLOWED_OPERATIONS is derived rather than typed.
    //
    // The method: take a baseline request that every field of that message can
    // meaningfully influence, perturb ONE field, and require the response to
    // CHANGE. A field that cannot change any answer is either unread or inert,
    // and inertness must be declared with a reason rather than discovered.
    //
    // `annual_cost_growth` and `pmi_drop_off_ltv` are what prompted this: both
    // are read by finance_service.cpp at four call sites and neither had a
    // single service-level assertion anywhere in this file.
    {
        namespace pb = google::protobuf;

        // Inert BY CONSTRUCTION, each with the reason. Not a suppression list
        // for awkward failures -- every entry is a field whose effect is
        // genuinely unobservable in the response, and saying why is the point.
        const auto inert_reason =
            [](const std::string& msg, const std::string& field) -> const char* {
            if (field == "guess") {
                return "a Newton SEED selects a root, it does not move one";
            }
            if (msg == "RefinanceRequest" && field == "current_monthly_payment") {
                return "the stated payment is reported back, not re-derived; "
                       "section 4 owns its validation";
            }
            return nullptr;
        };

        // A digest over the WHOLE response, so "changed" means any field of it
        // rather than a headline this test happened to pick.
        const auto digest = [](const pb::Message& m) -> std::string {
            std::string s;
            pb::TextFormat::PrintToString(m, &s);
            return s;
        };

        // Perturb one field to a value that is valid for its slot and
        // different from the baseline's. Returns false if the field's type is
        // one this sweep does not drive (repeated/message/enum are exercised
        // by their own sections).
        const auto perturb = [](pb::Message& m, const pb::FieldDescriptor* f) -> bool {
            const auto* refl = m.GetReflection();
            if (f->is_repeated()) { return false; }
            switch (f->cpp_type()) {
                case pb::FieldDescriptor::CPPTYPE_STRING: {
                    // Decimal strings on this surface. A rate-shaped field must
                    // stay inside the verifier's band or the engine refuses and
                    // "the response changed" would be true for the wrong reason.
                    const std::string cur = refl->GetString(m, f);
                    const bool rate_like =
                        f->name().find("rate") != std::string::npos ||
                        f->name().find("percent") != std::string::npos ||
                        f->name().find("growth") != std::string::npos ||
                        f->name().find("ltv") != std::string::npos ||
                        f->name().find("appreciation") != std::string::npos;
                    refl->SetString(&m, f, rate_like ? "0.0250" : "1234.00");
                    return refl->GetString(m, f) != cur;
                }
                case pb::FieldDescriptor::CPPTYPE_INT32: {
                    const int cur = refl->GetInt32(m, f);
                    refl->SetInt32(&m, f, cur == 7 ? 9 : 7);
                    return true;
                }
                case pb::FieldDescriptor::CPPTYPE_DOUBLE: {
                    const double cur = refl->GetDouble(m, f);
                    refl->SetDouble(&m, f, cur == 0.25 ? 0.5 : 0.25);
                    return true;
                }
                case pb::FieldDescriptor::CPPTYPE_BOOL: {
                    refl->SetBool(&m, f, !refl->GetBool(m, f));
                    return true;
                }
                default: return false;
            }
        };

        // ---- DetailedAmortizationRequest: the house's own costs ----
        {
            sensen::finance::DetailedAmortizationRequest base;
            base.set_loan_amount("360000.00");
            base.set_annual_rate("0.0650");
            base.set_term_months(360);
            base.set_monthly_overpayment("150.00");
            base.set_pmi_annual_rate("0.0060");
            base.set_original_home_value("400000.00");
            base.set_annual_tax_rate("0.2400");
            base.set_annual_repairs("3000.00");
            base.set_annual_insurance("1600.00");
            base.set_annual_cost_growth("0.0300");
            base.set_heloc_drawn_amount("40000.00");
            base.set_heloc_annual_rate("0.0850");
            base.set_heloc_term_years(10);

            sensen::finance::DetailedAmortizationResponse baseline;
            {
                auto ctx = make_context();
                auto st = stub.ComputeDetailedAmortization(ctx.get(), base, &baseline);
                check(st.ok(), "28a. the DetailedAmortization baseline is served (positive "
                               "control -- an erroring baseline makes every sweep below vacuous)");
            }
            const std::string base_digest = digest(baseline);

            std::vector<std::string> unread;
            const auto* desc = base.GetDescriptor();
            for (int i = 0; i < desc->field_count(); ++i) {
                const auto* f = desc->field(i);
                if (inert_reason(desc->name(), f->name()) != nullptr) { continue; }
                auto probe = base;
                if (!perturb(probe, f)) { continue; }
                sensen::finance::DetailedAmortizationResponse out;
                auto ctx = make_context();
                auto st = stub.ComputeDetailedAmortization(ctx.get(), probe, &out);
                // A refusal is an ANSWER -- the field was read and judged. Only
                // an OK response identical to the baseline proves it was dropped.
                if (st.ok() && digest(out) == base_digest) { unread.push_back(f->name()); }
            }
            std::string names;
            for (const auto& n : unread) { names += " " + n; }
            check(unread.empty(),
                  "28b. every DetailedAmortizationRequest field changes the answer;"
                  " dropped:" + (names.empty() ? std::string{" none"} : names));
        }

        // ---- AmortizationRequest: the STANDARD schedule's six new fields ----
        //
        // Added 2026-09-16 so a plain amortization can carry what the house
        // costs to keep and a second lien. Before that an utterance naming a
        // repair budget parsed as ComputeAmortization and the budget appeared
        // NOWHERE in the answer -- there was no field for it. Six new request
        // fields are six new chances for exactly the defect this section
        // exists to catch, so they are swept the same way.
        {
            sensen::finance::AmortizationRequest base;
            base.set_loan_amount("340000.00");
            base.set_annual_rate("0.0625");
            base.set_term_months(360);
            base.set_monthly_overpayment("150.00");
            // 85% LTV so PMI is genuinely live -- at a lower LTV the PMI fields
            // are inert and the sweep would report them unread, which is the
            // baseline mistake this section already records once.
            base.set_pmi_annual_rate("0.0070");
            base.set_original_home_value("400000.00");
            base.set_annual_repairs("3000.00");
            base.set_annual_insurance("1600.00");
            base.set_annual_cost_growth("0.0300");
            base.set_heloc_drawn_amount("40000.00");
            base.set_heloc_annual_rate("0.0850");
            base.set_heloc_term_years(10);

            sensen::finance::AmortizationResponse baseline;
            {
                auto ctx = make_context();
                auto st = stub.ComputeAmortization(ctx.get(), base, &baseline);
                check(st.ok(), "28r. the Amortization baseline is served (positive control)");
            }
            const std::string base_digest = digest(baseline);

            std::vector<std::string> unread;
            const auto* desc = base.GetDescriptor();
            for (int i = 0; i < desc->field_count(); ++i) {
                const auto* f = desc->field(i);
                if (inert_reason(desc->name(), f->name()) != nullptr) { continue; }
                auto probe = base;
                if (!perturb(probe, f)) { continue; }
                sensen::finance::AmortizationResponse out;
                auto ctx = make_context();
                auto st = stub.ComputeAmortization(ctx.get(), probe, &out);
                if (st.ok() && digest(out) == base_digest) { unread.push_back(f->name()); }
            }
            std::string names;
            for (const auto& n : unread) { names += " " + n; }
            check(unread.empty(),
                  "28s. every AmortizationRequest field changes the answer;"
                  " dropped:" + (names.empty() ? std::string{" none"} : names));

            // THE COMPATIBILITY PROPERTY, asserted rather than assumed.
            //
            // The handler routes to the DETAILED engine function when carrying
            // costs are present and to the plain one when they are not,
            // specifically so an existing caller cannot be affected. That is a
            // claim about two functions agreeing on the loan, and a claim is
            // worth a check: every figure a caller could already see must be
            // untouched by the six new fields.
            sensen::finance::AmortizationRequest bare;
            bare.set_loan_amount("340000.00");
            bare.set_annual_rate("0.0625");
            bare.set_term_months(360);
            bare.set_monthly_overpayment("150.00");
            bare.set_pmi_annual_rate("0.0070");
            bare.set_original_home_value("400000.00");

            sensen::finance::AmortizationResponse bare_out;
            {
                auto ctx = make_context();
                auto st = stub.ComputeAmortization(ctx.get(), bare, &bare_out);
                check(st.ok(), "28t. a request sending none of the six new fields is served");
            }
            auto only_carrying = bare;
            only_carrying.set_annual_repairs("3000.00");
            only_carrying.set_annual_insurance("1600.00");
            only_carrying.set_annual_cost_growth("0.0300");
            sensen::finance::AmortizationResponse carried_out;
            {
                auto ctx = make_context();
                auto st = stub.ComputeAmortization(ctx.get(), only_carrying, &carried_out);
                check(st.ok(), "28u. the same request WITH carrying costs is served");
            }
            check(bare_out.summary().total_principal_paid() ==
                          carried_out.summary().total_principal_paid() &&
                      bare_out.summary().total_interest_paid() ==
                          carried_out.summary().total_interest_paid() &&
                      bare_out.summary().total_pmi_paid() ==
                          carried_out.summary().total_pmi_paid() &&
                      bare_out.summary().total_payments_paid() ==
                          carried_out.summary().total_payments_paid() &&
                      bare_out.summary().actual_term_months() ==
                          carried_out.summary().actual_term_months(),
                  "28v. carrying costs change NOTHING about the loan -- principal, interest, "
                  "PMI, payments and term are identical (they are reported beside the debt "
                  "service, never folded into it)");
            check(bare_out.summary().total_repairs_paid()
                      .find_first_not_of("0.-+") == std::string::npos,
                  "28w. ... and an absent repair budget stays zero rather than a default");
            check(carried_out.summary().total_cost_of_ownership() !=
                      carried_out.summary().total_payments_paid(),
                  "28x. total_cost_of_ownership exceeds debt service once the house costs "
                  "something to keep -- an itemisation whose parts do not reach the total "
                  "they sit under is not an explanation");
        }

        // ---- RefinanceRequest: pmi_drop_off_ltv had no assertion at all ----
        {
            sensen::finance::RefinanceRequest base;
            // 85% LTV, DELIBERATELY. At 300k against a 400k value the loan is
            // already under the 80% drop-off, so sensen correctly charges no
            // PMI and both *_pmi_monthly fields become genuinely inert -- which
            // this sweep reported as "dropped". A baseline on which a field
            // cannot matter makes a correct field indistinguishable from an
            // unread one, and the fix is the baseline, not an exemption.
            base.set_current_loan_balance("340000.00");
            base.set_current_monthly_payment("2160.00");
            base.set_current_annual_rate("0.0725");
            base.set_current_remaining_months(300);
            base.set_property_value("400000.00");
            base.set_new_annual_rate("0.0575");
            base.set_new_term_years(30);
            base.set_closing_costs("6000.00");
            base.set_current_pmi_monthly("180.00");
            base.set_new_pmi_monthly("150.00");
            base.set_pmi_drop_off_ltv("0.8000");
            base.set_payments_per_year(12);

            sensen::finance::RefinanceResponse baseline;
            {
                auto ctx = make_context();
                auto st = stub.ComputeRefinance(ctx.get(), base, &baseline);
                check(st.ok(), "28c. the Refinance baseline is served (positive control)");
            }
            const std::string base_digest = digest(baseline);

            std::vector<std::string> unread;
            const auto* desc = base.GetDescriptor();
            for (int i = 0; i < desc->field_count(); ++i) {
                const auto* f = desc->field(i);
                if (inert_reason(desc->name(), f->name()) != nullptr) { continue; }
                auto probe = base;
                if (!perturb(probe, f)) { continue; }
                sensen::finance::RefinanceResponse out;
                auto ctx = make_context();
                auto st = stub.ComputeRefinance(ctx.get(), probe, &out);
                if (st.ok() && digest(out) == base_digest) { unread.push_back(f->name()); }
            }
            std::string names;
            for (const auto& n : unread) { names += " " + n; }
            check(unread.empty(),
                  "28d. every RefinanceRequest field changes the answer; dropped:" +
                      (names.empty() ? std::string{" none"} : names));
        }

        // ---- ExplainMortgageRequest: where a declared field hid unread ----
        //
        // This message is why the sweep exists in its general form. It declared
        // heloc_drawn_amount / heloc_annual_rate / heloc_term_years and the
        // handler read NONE of them: a caller who modelled a draw was handed an
        // explanation of a scenario without it, with nothing in the response
        // saying so. Every table was correct. Only the read was missing.
        {
            sensen::finance::ExplainMortgageRequest base;
            base.set_loan_amount("360000.00");
            base.set_annual_rate("0.0650");
            base.set_term_months(360);
            base.set_monthly_overpayment("200.00");
            base.set_pmi_annual_rate("0.0060");
            base.set_original_home_value("400000.00");
            base.set_annual_tax_rate("0.2400");
            base.set_annual_repairs("3000.00");
            base.set_annual_insurance("1600.00");
            base.set_annual_cost_growth("0.0300");
            base.set_heloc_drawn_amount("40000.00");
            base.set_heloc_annual_rate("0.0850");
            base.set_heloc_term_years(10);
            base.set_pmi_drop_off_ltv("0.8000");
            base.set_annual_appreciation("0.0300");

            sensen::finance::ExplainMortgageResponse baseline;
            {
                auto ctx = make_context();
                auto st = stub.ExplainMortgage(ctx.get(), base, &baseline);
                check(st.ok(), "28h. the ExplainMortgage baseline is served (positive control)");
                check(baseline.points_size() > 0,
                      "28i. ... and it produced sentences to compare, " +
                          std::to_string(baseline.points_size()) + " of them");
            }
            const std::string base_digest = digest(baseline);

            std::vector<std::string> unread;
            const auto* desc = base.GetDescriptor();
            for (int i = 0; i < desc->field_count(); ++i) {
                const auto* f = desc->field(i);
                if (inert_reason(desc->name(), f->name()) != nullptr) { continue; }
                auto probe = base;
                if (!perturb(probe, f)) { continue; }
                sensen::finance::ExplainMortgageResponse out;
                auto ctx = make_context();
                auto st = stub.ExplainMortgage(ctx.get(), probe, &out);
                if (st.ok() && digest(out) == base_digest) { unread.push_back(f->name()); }
            }
            std::string names;
            for (const auto& n : unread) { names += " " + n; }
            check(unread.empty(),
                  "28j. every ExplainMortgageRequest field changes the explanation;"
                  " dropped:" + (names.empty() ? std::string{" none"} : names));

            // And the itemisation must ACCOUNT for it, not merely react to it.
            // A HELOC that moves the total while having no sentence of its own
            // leaves the reader's arithmetic short with nothing named.
            bool names_heloc = false;
            for (const auto& pt : baseline.points()) {
                if (pt.field() == "total_heloc_interest_paid") { names_heloc = true; }
            }
            check(names_heloc,
                  "28k. the explanation ITEMISES the HELOC's interest, so the parts "
                  "reach the total they sit under");
        }

        // ---- The direction the sweep exists for, pinned by hand ----
        //
        // The sweep says "something changed". These two say WHICH WAY, because
        // a field wired to the wrong sensen member would also change the
        // answer -- and change it wrongly. The sweep catches a DROPPED field;
        // only a signed assertion catches a MISROUTED one.
        {
            const auto totals = [&](const std::string& growth) {
                sensen::finance::DetailedAmortizationRequest r;
                r.set_loan_amount("360000.00");
                r.set_annual_rate("0.0650");
                r.set_term_months(360);
                r.set_annual_tax_rate("0.2400");
                r.set_annual_repairs("3000.00");
                r.set_annual_insurance("1600.00");
                r.set_annual_cost_growth(growth);
                sensen::finance::DetailedAmortizationResponse out;
                auto ctx = make_context();
                auto st = stub.ComputeDetailedAmortization(ctx.get(), r, &out);
                return std::pair{st.ok(), out};
            };
            auto [ok0, flat] = totals("0.0000");
            auto [ok3, rising] = totals("0.0300");
            check(ok0 && ok3, "28e. both cost-growth arms are served");
            if (ok0 && ok3) {
                const double f = std::stod(flat.summary().total_repairs_paid());
                const double r = std::stod(rising.summary().total_repairs_paid());
                // The FLAT total is pinned to its closed form, 3000/yr x 30
                // years, before the ratio is asserted. A bare `r > f * 1.3` is
                // satisfied by any positive r when f is zero -- and a misroute
                // that leaves repairs unread makes f exactly zero, so the
                // direction test passed on `flat 0.0 -> rising 1.43` until this
                // line existed. Anchor the magnitude, then the growth.
                check(std::fabs(f - 90000.0) < 1.0,
                      "28f. flat repairs are 3000/yr x 30y = 90000, got " + std::to_string(f));
                // 3%/yr compounding over 30 years is ~1.6x the flat total. A
                // field read into the WRONG member -- the insurance slot, say --
                // would leave this equal while still moving the digest.
                check(r > f * 1.3,
                      "28f2. annual_cost_growth compounds REPAIRS specifically: flat " +
                          std::to_string(f) + " -> rising " + std::to_string(r));
                check(std::fabs(std::stod(flat.summary().total_insurance_paid()) - 48000.0) < 1.0,
                      "28g. flat insurance is 1600/yr x 30y = 48000, got " +
                          flat.summary().total_insurance_paid());
                check(std::stod(rising.summary().total_insurance_paid()) >
                          std::stod(flat.summary().total_insurance_paid()),
                      "28g2. ... and insurance too, so it is the growth rate and not a "
                      "repairs-only multiplier");
            }
        }
    }

    // -----------------------------------------------------------------
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
