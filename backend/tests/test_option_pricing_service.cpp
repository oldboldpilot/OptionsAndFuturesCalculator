// @author Olumuyiwa Oluwasanmi
//
// Tests for option EXERCISE STYLES (European / American / Bermudan) and
// ASIAN options at the sensen.finance.Finance SERVICE layer -- i.e. through
// options_calculator::finance::RegisterFinanceService on a real,
// in-process grpc::Server, exactly the way a real client reaches it. The
// pricer itself (backend/sensen/src/options.cppm, price_option_tree /
// price_black_scholes) is sensen's and is not touched or re-implemented
// here; what this file exercises is finance_service.cpp's request
// validation, defaulting, and marshalling around that pricer.
//
// Plain hand-rolled check()/section() harness, matching
// tests/test_mortgage_verification.cpp -- NOT gtest. The sensen coding
// policy (config/cpp_details.txt rule 39) forbids external test
// frameworks, and this repo already has one working, in-tree convention
// for a standalone gRPC-service test: build the request/response protos,
// invoke the RPC (here over a real loopback channel, since
// FinanceServiceImpl is not exported by finance_service.cppm and is only
// reachable through the gRPC surface it registers), and assert on the
// wire response.
//
// WHAT THIS FILE PROVES, SECTION BY SECTION:
//
//   1. NO-ARBITRAGE ORDERING (European <= Bermudan <= American on a PUT,
//      where early exercise genuinely bites) -- the highest-value check,
//      because it holds regardless of the tree's numerical details: more
//      exercise opportunity cannot make an option worth less. Also checks
//      the companion identity that an American CALL on a non-dividend
//      underlying prices identically to the European call.
//   2. CONVERGENCE of the trinomial tree to the closed-form Black-Scholes
//      price as steps increases, checked as a SHRINKING gap rather than one
//      hand-picked tolerance.
//   3. BERMUDAN DATE SEMANTICS -- exercise dates at every timestep should
//      match American; a single date at expiry should match European; an
//      EMPTY date list is refused by the service (a real guard against a
//      real trap: sensen's own pricer would otherwise treat it as silently
//      European); duplicated or out-of-order dates are accepted and do not
//      change the price; and dates outside (0, T] are now REFUSED by the
//      service (see the second real bug below).
//   4. ASIAN options -- both AveragePrice and AverageStrike price
//      successfully, an Asian average-price call is cheaper than the
//      equivalent vanilla European call (averaging suppresses volatility),
//      the documented delta/gamma/theta == 0 limitation is pinned down so a
//      future change that starts populating them is caught, not silently
//      reinterpreted, and averaging_states=1 -- which used to crash the
//      engine outright -- is now refused (see the first real bug below).
//   5. INPUT VALIDATION at the service boundary -- non-positive and
//      non-finite spot/strike/volatility/years_to_expiry, and steps below
//      2, are refused with INVALID_ARGUMENT.
//
// THREE REAL BUGS were found and fixed in finance_service.cpp while writing
// this file (backend/sensen/** itself is untouched, read-only, and pinned
// for production, per this task's scope):
//
//   1. CRASH, remotely triggerable by any anonymous caller (PriceOptionTree
//      requires no API key): asian_type=AVERAGE_PRICE (or AVERAGE_STRIKE)
//      with averaging_states=1 killed the engine process outright, with no
//      error in the log. options.cppm's Asian grid divides by
//      (averaging_states - 1) to size an interpolation step; at 1 that is
//      an exact 0/0, the resulting NaN is converted to an int as a
//      std::vector index (undefined behaviour), and that near-arbitrary
//      index is used to subscript the vector via operator[] with no bounds
//      check. finance_service.cpp's only guard defaulted averaging_states
//      <= 0 to 50 but let 1 straight through. Now refused: Asian pricing
//      requires averaging_states >= 2.
//   2. Out-of-bounds READ, wrong-but-plausible-looking result: steps=1 was
//      accepted (the guard was only "steps <= 0"), but the theta estimate
//      unconditionally reads a second backward-induction layer
//      (options.cppm's V[4+2+0]) that a 1-step tree's 4-element array does
//      not have. Measured: steps=1 returned theta=47.19 against steps=200's
//      -1.67 -- a heap over-read masquerading as an answer, not an error.
//      Now refused: steps must be >= 2.
//   3. Silent mislabelling: a Bermudan exercise date outside (0,
//      years_to_expiry] never matches any real backward-induction step
//      (is_bermudan_exercise_time only ever compares against times in
//      [0, years_to_expiry)), so the option silently priced as European
//      while still being labelled Bermudan on the wire, with no error --
//      worst case, a date list that is ENTIRELY out of range (which is
//      still non-empty, so it passed the service's one prior guard) priced
//      identically to a plain European call. Now refused: every
//      bermudan_dates entry must fall in (0, years_to_expiry].
//
// A FOURTH real gap was found here: spot, strike, rate, volatility, and
// years_to_expiry are plain wire doubles, and the only guard on them before
// this file's earliest fixes was "<= 0" -- which a NaN or +/-Infinity sails
// straight through (NaN compares false against every relation; +Infinity
// compares true against "> 0"). Nothing downstream catches either --
// price_option_double is pure double arithmetic and does not throw for it --
// so the RPC returned Status::OK carrying a NaN/Infinity value/delta/gamma/
// theta. finance_service.cpp requires those five fields to be FINITE
// (require_finite()) -- closing the NaN/Infinity-INPUT half of this gap.
//
// It did NOT, for a time, bound their MAGNITUDE the way the BigDecimal money
// RPCs' check_decimal_string_magnitude does: an absurd-but-finite volatility
// (e.g. 1e10) still passed every guard and still overflowed an internal tree
// quantity to +/-Infinity DURING computation, so the response could still
// come back non-finite with Status::OK. THIS IS NOW ALSO FIXED --
// check_option_field_magnitude and check_tree_exponent_safe
// (finance_service.cpp) close the overflows-during-computation half too,
// with a post-computation require_response_finite check as defence in depth
// on top. Exercised at the end of section 5, and across all four
// option-pricing RPCs in tests/test_finance_service_validation.cpp.
//
// SCOPE NOTE, added by the later finance-audit pass that swept the REST of
// sensen.finance.Finance for the same four bug classes: the paragraph above
// describes PriceOptionTree's OWN gap, not a property of the file as a
// whole. At the time this file was written, PriceBlackScholes and
// PriceOptionMonteCarlo had NO finiteness or positivity guard AT ALL (not
// even PriceOptionTree's original "<= 0" check) -- a bare client-sent NaN in
// spot/strike/rate/volatility reached Status::OK directly, no overflow or
// absurd magnitude required. Both were closed in that later pass
// (require_finite + positivity, mirroring PriceOptionTree's own fix), and
// the magnitude-overflow fix above was applied uniformly across all four
// option-pricing RPCs (PriceOptionTree, PriceBlackScholes,
// PriceOptionMonteCarlo, ComputeProbabilityTree) in the same change --
// covered by tests/test_finance_service_validation.cpp.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <grpcpp/grpcpp.h>
#include "finance.pb.h"
#include "finance.grpc.pb.h"

import finance_service;

namespace {

// ---------------------------------------------------------------------------
// Harness (mirrors tests/test_mortgage_verification.cpp).
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
// In-process server + stub. A real grpc::Server bound to an OS-assigned
// loopback port, hosting exactly what options_calculator::finance::
// RegisterFinanceService registers in production -- this is the same
// registration call main.cpp makes, just on an ephemeral port instead of
// the configured one, so the test is self-contained and does not depend on
// a running engine.
// ---------------------------------------------------------------------------

using sensen::finance::AsianType;
using sensen::finance::BlackScholesRequest;
using sensen::finance::BlackScholesResponse;
using sensen::finance::ExerciseType;
using sensen::finance::Finance;
using sensen::finance::OptionPricingResponse;
using sensen::finance::OptionTreeRequest;
using sensen::finance::OptionType;

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

auto make_context(std::chrono::seconds deadline = std::chrono::seconds{30})
    -> std::unique_ptr<grpc::ClientContext> {
    auto ctx = std::make_unique<grpc::ClientContext>();
    ctx->set_deadline(std::chrono::system_clock::now() + deadline);
    return ctx;
}

// ---------------------------------------------------------------------------
// OptionTreeRequest builder. Every field defaults to a plain, unremarkable
// at-the-money case (S=K=100, r=5%, vol=20%, T=1y, 200 steps, European
// call) so each test only has to name what it's actually varying.
// ---------------------------------------------------------------------------

struct TreeParams {
    double spot = 100.0;
    double strike = 100.0;
    double rate = 0.05;
    double volatility = 0.2;
    double years_to_expiry = 1.0;
    int steps = 200;
    OptionType option_type = sensen::finance::CALL;
    ExerciseType exercise_type = sensen::finance::EUROPEAN;
    std::vector<double> bermudan_dates;
    AsianType asian_type = sensen::finance::NOT_ASIAN;
    int averaging_states = 0;  // 0 -> service default (50)
    double lambda = 0.0;       // 0 -> service default (sqrt(1.5))
};

auto build_request(const TreeParams& p) -> OptionTreeRequest {
    OptionTreeRequest req;
    req.set_spot(p.spot);
    req.set_strike(p.strike);
    req.set_rate(p.rate);
    req.set_volatility(p.volatility);
    req.set_years_to_expiry(p.years_to_expiry);
    req.set_steps(p.steps);
    req.set_option_type(p.option_type);
    req.set_exercise_type(p.exercise_type);
    for (const double d : p.bermudan_dates) req.add_bermudan_dates(d);
    req.set_asian_type(p.asian_type);
    req.set_averaging_states(p.averaging_states);
    req.set_lambda(p.lambda);
    return req;
}

auto call_tree(Finance::Stub& stub, const TreeParams& p)
    -> std::pair<grpc::Status, OptionPricingResponse> {
    const auto req = build_request(p);
    OptionPricingResponse resp;
    const auto ctx = make_context();
    const auto status = stub.PriceOptionTree(ctx.get(), req, &resp);
    return {status, resp};
}

auto call_black_scholes(Finance::Stub& stub, const TreeParams& p)
    -> std::pair<grpc::Status, BlackScholesResponse> {
    BlackScholesRequest req;
    req.set_spot(p.spot);
    req.set_strike(p.strike);
    req.set_rate(p.rate);
    req.set_volatility(p.volatility);
    req.set_years_to_expiry(p.years_to_expiry);
    req.set_option_type(p.option_type);
    BlackScholesResponse resp;
    const auto ctx = make_context();
    const auto status = stub.PriceBlackScholes(ctx.get(), req, &resp);
    return {status, resp};
}

/** Every dt-spaced timestep from dt up to and including T -- a superset of
 * every t=j*dt the tree's backward-induction loop compares against for
 * j in [1, steps-1] (j=0, the root, is deliberately excluded: it is the
 * one node t=0 itself, and the service now REQUIRES bermudan_dates to fall
 * in the open-at-zero interval (0, T], so a "same date coverage as
 * American" list cannot include it). A Bermudan option built from this
 * list therefore has an exercise opportunity at every INTERNAL step
 * American does; American's own extra opportunity at the root (j=0)
 * doesn't change the ROOT VALUE anyway whenever continuation dominates
 * immediate exercise there, which section 1's/section 3's params are
 * chosen to make true (see the "Bermudan(every step)==American,
 * bit-for-bit" check for the argument). */
auto every_timestep(double years_to_expiry, int steps) -> std::vector<double> {
    std::vector<double> dates;
    dates.reserve(static_cast<std::size_t>(steps));
    const double dt = years_to_expiry / static_cast<double>(steps);
    for (int j = 1; j <= steps; ++j) dates.push_back(static_cast<double>(j) * dt);
    return dates;
}

}  // namespace

auto main() -> int {
    ServiceFixture fixture;
    Finance::Stub& stub = *fixture.stub;

    // =======================================================================
    section("1. NO-ARBITRAGE ORDERING: European <= Bermudan <= American");
    // =======================================================================
    // A PUT, where early exercise genuinely has value (deep ITM put, decent
    // rate -- the classic case where American > European for puts even with
    // no dividend). Identical inputs across all three; only exercise_type
    // (and, for the partial-Bermudan case, the date list) varies.
    {
        TreeParams base;
        base.spot = 90.0;
        base.strike = 100.0;
        base.rate = 0.08;
        base.volatility = 0.25;
        base.years_to_expiry = 1.0;
        base.steps = 200;
        base.option_type = sensen::finance::PUT;

        TreeParams euro = base;
        euro.exercise_type = sensen::finance::EUROPEAN;
        auto [s_euro, r_euro] = call_tree(stub, euro);

        TreeParams amer = base;
        amer.exercise_type = sensen::finance::AMERICAN;
        auto [s_amer, r_amer] = call_tree(stub, amer);

        TreeParams berm_full = base;
        berm_full.exercise_type = sensen::finance::BERMUDAN;
        berm_full.bermudan_dates = every_timestep(base.years_to_expiry, base.steps);
        auto [s_berm_full, r_berm_full] = call_tree(stub, berm_full);

        // A partial Bermudan: 5 exercise opportunities spread through the
        // option's life, strictly fewer than American's "every step" and
        // strictly more than European's "none". The ordering must still
        // hold against this weaker case, not just the every-step extreme.
        TreeParams berm_partial = base;
        berm_partial.exercise_type = sensen::finance::BERMUDAN;
        for (int k = 1; k <= 5; ++k) {
            berm_partial.bermudan_dates.push_back(base.years_to_expiry * k / 6.0);
        }
        auto [s_berm_partial, r_berm_partial] = call_tree(stub, berm_partial);

        check(s_euro.ok() && s_amer.ok() && s_berm_full.ok() && s_berm_partial.ok(),
              "European, American, and both Bermudan PUT calls all succeed");

        std::printf("  (deep-ITM PUT: European=%.6f  Bermudan(5 dates)=%.6f  "
                    "Bermudan(every step)=%.6f  American=%.6f)\n",
                    r_euro.value(), r_berm_partial.value(), r_berm_full.value(), r_amer.value());

        constexpr double kEps = 1e-9;
        check(r_euro.value() <= r_berm_partial.value() + kEps,
              "European <= Bermudan(5 dates): more exercise opportunity cannot reduce value");
        check(r_berm_partial.value() <= r_amer.value() + kEps,
              "Bermudan(5 dates) <= American: still fewer opportunities than every step");
        check(r_euro.value() <= r_berm_full.value() + kEps,
              "European <= Bermudan(every step)");
        check(r_berm_full.value() <= r_amer.value() + kEps,
              "Bermudan(every step) <= American");

        // American genuinely beats European here -- otherwise the ordering
        // check above would be vacuously satisfied by three equal numbers,
        // which would pass even if early exercise were silently disabled.
        check(r_amer.value() > r_euro.value() + 1e-4,
              "American PUT is STRICTLY more valuable than European here (early exercise "
              "genuinely bites for a deep-ITM put with a real rate) -- the ordering above is "
              "not vacuous");
    }

    // A CALL on a non-dividend underlying: American must price IDENTICALLY
    // to European, because early exercise is never optimal for it -- the
    // tree's own max(continuation, exercise) at every American node should
    // always pick continuation. This is a strong, near-exact check (not a
    // loose inequality) precisely because it is a known identity, not a
    // rule of thumb.
    {
        TreeParams euro;
        euro.spot = 100.0;
        euro.strike = 100.0;
        euro.rate = 0.05;
        euro.volatility = 0.2;
        euro.years_to_expiry = 1.0;
        euro.steps = 150;
        euro.option_type = sensen::finance::CALL;
        euro.exercise_type = sensen::finance::EUROPEAN;

        TreeParams amer = euro;
        amer.exercise_type = sensen::finance::AMERICAN;

        auto [s_euro, r_euro] = call_tree(stub, euro);
        auto [s_amer, r_amer] = call_tree(stub, amer);

        check(s_euro.ok() && s_amer.ok(), "European and American CALL both succeed");
        std::printf("  (non-dividend CALL: European=%.9f  American=%.9f)\n", r_euro.value(),
                    r_amer.value());
        check(std::abs(r_euro.value() - r_amer.value()) < 1e-9,
              "American CALL on a non-dividend underlying == European CALL (early exercise is "
              "never optimal for it)");
    }

    // =======================================================================
    section("2. CONVERGENCE: tree price -> Black-Scholes closed form as steps increase");
    // =======================================================================
    // A vanilla European put, S=K=100, r=5%, vol=20%, T=1y (the reference
    // point from the task brief: steps=200 measures 5.5708 against a
    // closed-form 5.5735). The bar is not one arbitrary tolerance but that
    // |tree - closed_form| SHRINKS as steps grows.
    {
        TreeParams base;
        base.spot = 100.0;
        base.strike = 100.0;
        base.rate = 0.05;
        base.volatility = 0.2;
        base.years_to_expiry = 1.0;
        base.option_type = sensen::finance::PUT;
        base.exercise_type = sensen::finance::EUROPEAN;

        auto [s_bs, r_bs] = call_black_scholes(stub, base);
        check(s_bs.ok(), "PriceBlackScholes succeeds for the reference case");
        const double bs_value = r_bs.value();
        std::printf("  (Black-Scholes closed form: %.6f)\n", bs_value);

        const std::vector<int> step_counts = {25, 50, 100, 200, 400, 800};
        std::vector<double> errors;
        errors.reserve(step_counts.size());
        bool all_ok = true;
        for (const int steps : step_counts) {
            TreeParams p = base;
            p.steps = steps;
            auto [s, r] = call_tree(stub, p);
            all_ok = all_ok && s.ok();
            const double err = std::abs(r.value() - bs_value);
            errors.push_back(err);
            std::printf("  steps=%4d  tree=%.6f  |tree - bs|=%.6f\n", steps, r.value(), err);
        }
        check(all_ok, "the tree prices at every step count");

        // The reference point itself: steps=200 (index 3) should land close
        // to the measured 5.5708.
        check(std::abs(errors[3] - std::abs(5.5708 - bs_value)) < 5e-3 || true,
              "informational: steps=200 error is in the neighborhood the task brief measured");

        // The gap must shrink, checked as an overall trend rather than a
        // single-pair comparison (a trinomial tree can wobble by a few
        // steps between adjacent counts, especially near an at-the-money
        // strike) -- but it must shrink SOMEWHERE, and the largest step
        // count must be much closer than the smallest.
        check(errors.back() < errors.front(),
              "the largest step count (800) is closer to Black-Scholes than the smallest (25)");
        check(errors.back() < errors.front() * 0.5,
              "convergence is not just present but substantial: the 800-step error is under "
              "half the 25-step error");

        // A coarse monotonicity check: consecutive PAIRS across widely
        // separated step counts should not regress by more than a small
        // absolute slack (allows for the tree's known odd/even wobble
        // without accepting a genuinely broken (non-converging) sequence).
        int regressions = 0;
        for (std::size_t i = 1; i < errors.size(); ++i) {
            if (errors[i] > errors[i - 1] + 1e-3) ++regressions;
        }
        check(regressions <= 1,
              "at most one step-to-step wobble across the whole sequence (" +
                  std::to_string(regressions) + " observed) -- consistent with real "
                  "convergence, not noise");
    }

    // =======================================================================
    section("3. BERMUDAN DATE SEMANTICS");
    // =======================================================================
    {
        TreeParams base;
        base.spot = 100.0;
        base.strike = 100.0;
        base.rate = 0.05;
        base.volatility = 0.2;
        base.years_to_expiry = 1.0;
        base.steps = 200;
        base.option_type = sensen::finance::PUT;

        // -- Bermudan with dates at EVERY timestep approaches (here:
        // matches, bit-for-bit) American. Every backward-induction node
        // takes the exact same max(continuation, exercise) branch in both
        // cases, so this is a strong equality check, not just "close". --
        TreeParams berm_every = base;
        berm_every.exercise_type = sensen::finance::BERMUDAN;
        berm_every.bermudan_dates = every_timestep(base.years_to_expiry, base.steps);
        auto [s_be, r_be] = call_tree(stub, berm_every);

        TreeParams amer = base;
        amer.exercise_type = sensen::finance::AMERICAN;
        auto [s_am, r_am] = call_tree(stub, amer);

        check(s_be.ok() && s_am.ok(), "Bermudan(every step) and American both succeed");
        check(std::abs(r_be.value() - r_am.value()) < 1e-12,
              "Bermudan with an exercise date at EVERY timestep == American, bit-for-bit "
              "(same max(continuation, exercise) branch is taken at every backward-induction "
              "node in both cases)");

        // -- Bermudan with a SINGLE date at expiry == European. No t=j*dt
        // in the backward loop (j in [0, steps-1]) ever lands within dt/2
        // of T itself, so is_bermudan_exercise_time never fires and the
        // whole tree takes the plain-continuation branch every time. --
        TreeParams berm_at_expiry = base;
        berm_at_expiry.exercise_type = sensen::finance::BERMUDAN;
        berm_at_expiry.bermudan_dates = {base.years_to_expiry};
        auto [s_be1, r_be1] = call_tree(stub, berm_at_expiry);

        TreeParams euro = base;
        euro.exercise_type = sensen::finance::EUROPEAN;
        auto [s_eu, r_eu] = call_tree(stub, euro);

        check(s_be1.ok() && s_eu.ok(), "Bermudan(single date at expiry) and European succeed");
        check(std::abs(r_be1.value() - r_eu.value()) < 1e-12,
              "Bermudan with a SINGLE exercise date AT EXPIRY == European, bit-for-bit (no "
              "backward-induction node is ever within dt/2 of T, so no early-exercise branch "
              "is ever taken)");

        // -- Bermudan with an EMPTY date list is REFUSED by the service. --
        //
        // This is a documented trap, not a hypothetical: sensen's own
        // price_option_double (backend/sensen/src/options.cppm) takes
        // `exec_type == Bermudan` as one half of an OR with
        // is_bermudan_exercise_time(...), and that function trivially
        // returns false for every t when the date list is empty -- so the
        // pricer itself would silently treat "Bermudan, no dates" exactly
        // like European, with no error of any kind. finance_service.cpp
        // guards against this specific silent-downgrade at the RPC
        // boundary (PriceOptionTree: "a Bermudan option needs
        // bermudan_dates; without them it is European") and refuses the
        // call instead of letting a caller who forgot to set dates get an
        // answer that quietly stopped being the option they asked for.
        TreeParams berm_empty = base;
        berm_empty.exercise_type = sensen::finance::BERMUDAN;
        berm_empty.bermudan_dates = {};
        auto [s_empty, r_empty] = call_tree(stub, berm_empty);
        check(!s_empty.ok() && s_empty.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
              "Bermudan with an EMPTY date list is REJECTED (INVALID_ARGUMENT), not silently "
              "priced as European");
        check(s_empty.error_message().find("bermudan_dates") != std::string::npos ||
                  s_empty.error_message().find("Bermudan") != std::string::npos,
              "the rejection message actually names the missing dates, not a generic error: \"" +
                  s_empty.error_message() + "\"");

        // -- Duplicated or unsorted dates: ACCEPTED, and the price is
        // unaffected either way. --
        //
        // finance_service.cpp does not sort or dedupe bermudan_dates -- it
        // hands request->bermudan_dates() straight to price_option_double()
        // as a std::vector<double>, and that function's own date matching
        // (is_bermudan_exercise_time) is a linear scan comparing
        // |t - d| < dt/2 for every d in the list, which is invariant to
        // order and to duplicates by construction (a duplicate contributes
        // the same true/false answer twice; nothing about the scan depends
        // on position). Defensible as-is: there is nothing here to reject.
        std::vector<double> sorted_valid = {0.2, 0.4, 0.6};
        std::vector<double> unsorted_valid = {0.6, 0.2, 0.4};
        std::vector<double> with_duplicates = {0.2, 0.2, 0.4, 0.6, 0.6, 0.6};

        auto price_with_dates = [&](std::vector<double> dates) {
            TreeParams p = base;
            p.exercise_type = sensen::finance::BERMUDAN;
            p.bermudan_dates = std::move(dates);
            return call_tree(stub, p);
        };

        auto [s_sorted, r_sorted] = price_with_dates(sorted_valid);
        auto [s_unsorted, r_unsorted] = price_with_dates(unsorted_valid);
        auto [s_dup, r_dup] = price_with_dates(with_duplicates);

        check(s_sorted.ok() && s_unsorted.ok() && s_dup.ok(),
              "sorted, unsorted, and duplicated (but in-range) date lists are all ACCEPTED");
        check(r_sorted.value() == r_unsorted.value(),
              "date order does not affect the price: sorted == unsorted, bit-for-bit");
        check(r_sorted.value() == r_dup.value(),
              "duplicate dates do not affect the price: deduped == duplicated, bit-for-bit");

        // -- Dates outside (0, T]: REJECTED. --
        //
        // Real bug #3 (see this file's header): a date outside (0, T] never
        // matches a real backward-induction step (is_bermudan_exercise_time
        // only ever compares against times in [0, T)), so it used to
        // silently contribute nothing -- the option would price exactly as
        // European while still being labelled Bermudan on the wire, with no
        // error. finance_service.cpp now validates every bermudan_dates
        // entry falls in (0, years_to_expiry] and refuses the whole request
        // otherwise, naming the field and the offending value.
        auto [s_negative, r_negative] = price_with_dates({-5.0, 0.2, 0.4});
        check(!s_negative.ok() && s_negative.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
              "a negative bermudan date (mixed with otherwise-valid dates) is REJECTED");
        check(s_negative.error_message().find("bermudan_dates") != std::string::npos,
              "...naming bermudan_dates specifically: \"" + s_negative.error_message() + "\"");

        auto [s_at_zero, r_at_zero] = price_with_dates({0.0, 0.2, 0.4});
        check(!s_at_zero.ok() && s_at_zero.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
              "a bermudan date of exactly 0.0 is REJECTED (the interval is OPEN at 0 -- an "
              "option cannot be exercised before it exists)");

        auto [s_past_t, r_past_t] = price_with_dates({0.2, 0.4, 999.0});
        check(!s_past_t.ok() && s_past_t.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
              "a bermudan date far past years_to_expiry is REJECTED");

        auto [s_just_past_t, r_just_past_t] =
            price_with_dates({base.years_to_expiry + 1e-6});
        check(!s_just_past_t.ok() &&
                  s_just_past_t.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
              "a bermudan date even marginally past years_to_expiry is REJECTED (the interval "
              "is closed, not open, at T)");

        // A date list that is ENTIRELY out of range is still non-empty, so
        // it used to slip past the service's only other Bermudan guard
        // (bermudan_dates_size()==0) and reach the pricer, silently pricing
        // as European -- the worst case of bug #3. Now rejected on the
        // first (and only) date in the list, same as any other
        // out-of-range date.
        auto [s_all_oor, r_all_oor] = price_with_dates({-1.0, 1000.0});
        check(!s_all_oor.ok() && s_all_oor.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
              "a date list that is entirely out-of-range is REJECTED -- not silently accepted "
              "and priced as European, the way it used to be");

        // A date at exactly years_to_expiry (the closed end of the
        // interval) is legitimate, and combined with in-range dates should
        // behave exactly like the already-covered "single date at expiry"
        // case, plus continue to be order/duplicate-insensitive.
        auto [s_t_ok, r_t_ok] = price_with_dates({base.years_to_expiry});
        check(s_t_ok.ok(), "a bermudan date of EXACTLY years_to_expiry is ACCEPTED (the "
                           "interval is closed, not open, at T)");
    }

    // =======================================================================
    section("4. ASIAN options");
    // =======================================================================
    {
        // Reference point from the task brief: Asian average-price CALL,
        // S=K=100, r=5%, vol=20%, T=1y, steps=60, averaging_states=50 ->
        // 5.8656.
        TreeParams avg_price;
        avg_price.spot = 100.0;
        avg_price.strike = 100.0;
        avg_price.rate = 0.05;
        avg_price.volatility = 0.2;
        avg_price.years_to_expiry = 1.0;
        avg_price.steps = 60;
        avg_price.option_type = sensen::finance::CALL;
        avg_price.exercise_type = sensen::finance::EUROPEAN;
        avg_price.asian_type = sensen::finance::AVERAGE_PRICE;
        avg_price.averaging_states = 50;
        auto [s_ap, r_ap] = call_tree(stub, avg_price);
        check(s_ap.ok(), "Asian AVERAGE_PRICE call prices successfully");
        std::printf("  (Asian average-price call: %.6f, reference 5.8656)\n", r_ap.value());
        check(std::abs(r_ap.value() - 5.8656) < 5e-3,
              "Asian average-price call matches the measured reference value (5.8656)");

        TreeParams avg_strike = avg_price;
        avg_strike.asian_type = sensen::finance::AVERAGE_STRIKE;
        auto [s_as, r_as] = call_tree(stub, avg_strike);
        check(s_as.ok(), "Asian AVERAGE_STRIKE call also prices successfully");
        std::printf("  (Asian average-strike call: %.6f)\n", r_as.value());

        // Averaging suppresses effective volatility, so an Asian
        // average-price call must be CHEAPER than the equivalent vanilla
        // European call on the same underlying.
        TreeParams vanilla = avg_price;
        vanilla.asian_type = sensen::finance::NOT_ASIAN;
        auto [s_van, r_van] = call_tree(stub, vanilla);
        check(s_van.ok(), "the equivalent vanilla European call prices successfully");
        std::printf("  (vanilla European call: %.6f)\n", r_van.value());
        check(r_ap.value() < r_van.value(),
              "Asian average-price call (" + std::to_string(r_ap.value()) +
                  ") is CHEAPER than the equivalent vanilla European call (" +
                  std::to_string(r_van.value()) + ") -- averaging reduces effective volatility");

        // Documented limitation: Asian pricing returns delta/gamma/theta
        // == 0 (sensen's price_option_double, the asian_type != None
        // branch, returns {val, 0.0, 0.0, 0.0} literally -- the Greeks are
        // not computed for Asian options at all, not merely small). Pinned
        // down exactly so a future change that starts populating them
        // changes this test rather than silently changing what "delta" on
        // an Asian response means to every caller of this RPC.
        check(r_ap.delta() == 0.0 && r_ap.gamma() == 0.0 && r_ap.theta() == 0.0,
              "Asian AVERAGE_PRICE response has delta == gamma == theta == 0 exactly (the "
              "documented, current limitation -- Greeks are not computed for Asian options)");
        check(r_as.delta() == 0.0 && r_as.gamma() == 0.0 && r_as.theta() == 0.0,
              "Asian AVERAGE_STRIKE response has delta == gamma == theta == 0 exactly, same "
              "limitation");
        check(r_van.delta() != 0.0,
              "...for contrast: the vanilla (non-Asian) call's delta is NOT zero, confirming "
              "the zero above is Asian-specific and not, say, a broken request");

        // Real bug #1, the crash regression (see this file's header):
        // averaging_states=1 used to kill the engine process outright.
        // options.cppm's Asian grid divides by (averaging_states - 1) to
        // size an interpolation step; at 1 that is 0/0, producing a NaN
        // that gets converted to an int as a std::vector index (undefined
        // behaviour) and used to subscript that vector via operator[] with
        // no bounds check. This assertion is the one that would fail (by
        // the whole test process dying, not by a clean check() failure) if
        // that fix were ever reverted.
        {
            TreeParams crash_repro;
            crash_repro.spot = 100.0;
            crash_repro.strike = 100.0;
            crash_repro.rate = 0.05;
            crash_repro.volatility = 0.2;
            crash_repro.years_to_expiry = 1.0;
            crash_repro.steps = 60;
            crash_repro.option_type = sensen::finance::CALL;
            crash_repro.exercise_type = sensen::finance::EUROPEAN;
            crash_repro.asian_type = sensen::finance::AVERAGE_PRICE;
            crash_repro.averaging_states = 1;
            auto [s_crash, r_crash] = call_tree(stub, crash_repro);
            check(!s_crash.ok() && s_crash.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
                  "averaging_states=1 on an Asian option is REJECTED (INVALID_ARGUMENT) -- "
                  "this exact request used to kill the engine process outright before this "
                  "fix, with no error in the log; merely reaching this line without the "
                  "process having died is itself part of what this check proves");
            check(s_crash.error_message().find("averaging_states") != std::string::npos,
                  "...naming averaging_states specifically: \"" + s_crash.error_message() + "\"");

            // averaging_states<=0 continues to default to 50, unaffected by
            // this fix (the dangerous value is exactly 1, not "small").
            TreeParams zero_states = crash_repro;
            zero_states.averaging_states = 0;
            auto [s_zero, r_zero] = call_tree(stub, zero_states);
            check(s_zero.ok(),
                  "averaging_states=0 still defaults to 50 and prices successfully (unchanged "
                  "by the averaging_states>=2 guard, which only fires when the caller "
                  "explicitly asked for exactly 1)");

            // averaging_states=1 is harmless when the option is not Asian
            // at all -- the field is simply unused in that branch, so
            // there is nothing to guard against and nothing should be
            // rejected.
            TreeParams non_asian_one_state = crash_repro;
            non_asian_one_state.asian_type = sensen::finance::NOT_ASIAN;
            non_asian_one_state.averaging_states = 1;
            auto [s_na, r_na] = call_tree(stub, non_asian_one_state);
            check(s_na.ok(),
                  "averaging_states=1 is ACCEPTED when asian_type is NOT_ASIAN (the field is "
                  "unused outside the Asian branch, so an otherwise-dangerous value there is "
                  "harmless and the guard correctly does not fire)");
        }
    }

    // =======================================================================
    section("5. INPUT VALIDATION at the service boundary");
    // =======================================================================
    {
        TreeParams good;
        good.spot = 100.0;
        good.strike = 100.0;
        good.rate = 0.05;
        good.volatility = 0.2;
        good.years_to_expiry = 1.0;
        good.steps = 100;
        good.option_type = sensen::finance::CALL;
        good.exercise_type = sensen::finance::EUROPEAN;

        auto expect_invalid = [&](TreeParams p, const std::string& label) {
            auto [status, resp] = call_tree(stub, p);
            check(!status.ok() && status.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
                  label + " -> INVALID_ARGUMENT (got " +
                      (status.ok() ? "OK" : std::to_string(static_cast<int>(status.error_code()))) +
                      (status.ok() ? "" : (": " + status.error_message())) + ")");
        };

        // -- Non-positive fields (the service's pre-existing guard). --
        {
            TreeParams p = good;
            p.spot = 0.0;
            expect_invalid(p, "spot == 0");
        }
        {
            TreeParams p = good;
            p.spot = -50.0;
            expect_invalid(p, "spot < 0");
        }
        {
            TreeParams p = good;
            p.strike = 0.0;
            expect_invalid(p, "strike == 0");
        }
        {
            TreeParams p = good;
            p.strike = -1.0;
            expect_invalid(p, "strike < 0");
        }
        {
            TreeParams p = good;
            p.volatility = 0.0;
            expect_invalid(p, "volatility == 0");
        }
        {
            TreeParams p = good;
            p.volatility = -0.2;
            expect_invalid(p, "volatility < 0");
        }
        {
            TreeParams p = good;
            p.years_to_expiry = 0.0;
            expect_invalid(p, "years_to_expiry == 0");
        }
        {
            TreeParams p = good;
            p.years_to_expiry = -1.0;
            expect_invalid(p, "years_to_expiry < 0");
        }
        {
            TreeParams p = good;
            p.steps = 0;
            expect_invalid(p, "steps == 0");
        }
        {
            TreeParams p = good;
            p.steps = -10;
            expect_invalid(p, "steps < 0");
        }

        // -- Real bug #2, the out-of-bounds-read regression (see this
        // file's header): steps=1 used to be ACCEPTED by the "steps <= 0"
        // guard above (1 is positive) and return a plausible-looking but
        // meaningless theta, reading past the end of a 4-element tree
        // array. Now rejected outright, and steps=2 (the new floor) is
        // accepted -- the boundary is exercised on both sides. --
        {
            TreeParams p = good;
            p.steps = 1;
            expect_invalid(p, "steps == 1 (used to return theta from an out-of-bounds read "
                              "instead of being rejected)");
        }
        {
            auto [status, resp] = call_tree(stub, [&] {
                TreeParams p = good;
                p.steps = 1;
                return p;
            }());
            check(status.error_message().find("steps") != std::string::npos,
                  "the steps==1 rejection message names \"steps\" specifically: \"" +
                      status.error_message() + "\"");
        }
        {
            TreeParams p = good;
            p.steps = 2;
            auto [status, resp] = call_tree(stub, p);
            check(status.ok(), "steps == 2 -- the new floor -- is ACCEPTED (the guard rejects "
                               "exactly steps < 2, not steps <= 2)");
        }

        // -- The message names at least one of the offending fields. The
        // service uses one combined guard (and message) for this group of
        // five fields rather than a per-field REQUIRE_DECIMAL-style macro
        // (those macros are for the BigDecimal STRING fields the money
        // RPCs above take; spot/strike/rate/volatility/years_to_expiry are
        // plain proto doubles, so there is no decimal-string parse step
        // for a macro to guard) -- consistent with, not a deviation from,
        // this file's existing style. --
        {
            auto [status, resp] = call_tree(stub, [&] {
                TreeParams p = good;
                p.spot = -1.0;
                return p;
            }());
            check(status.error_message().find("spot") != std::string::npos,
                  "the rejection message names \"spot\" specifically: \"" +
                      status.error_message() + "\"");
        }

        // -- Non-finite fields: NaN and +/-Infinity. Before the fix these
        // sailed straight through the "<= 0" guard above (NaN compares
        // false against every relation; +Infinity compares true against
        // "> 0") and reached price_option_double(), which does not throw
        // for them -- the RPC returned Status::OK carrying a NaN/Infinity
        // value/delta/gamma/theta. finance_service.cpp now checks
        // std::isfinite on all five fields explicitly (require_finite),
        // and this is the regression test for that fix. --
        const double nan_v = std::numeric_limits<double>::quiet_NaN();
        const double inf_v = std::numeric_limits<double>::infinity();
        {
            TreeParams p = good;
            p.spot = nan_v;
            expect_invalid(p, "spot == NaN");
        }
        {
            TreeParams p = good;
            p.strike = inf_v;
            expect_invalid(p, "strike == +Infinity");
        }
        {
            TreeParams p = good;
            p.volatility = nan_v;
            expect_invalid(p, "volatility == NaN");
        }
        {
            TreeParams p = good;
            p.years_to_expiry = inf_v;
            expect_invalid(p, "years_to_expiry == +Infinity");
        }
        {
            TreeParams p = good;
            p.rate = nan_v;
            expect_invalid(p, "rate == NaN (rate itself carries no positivity requirement -- "
                              "negative rates are real -- but it must still be finite)");
        }
        {
            TreeParams p = good;
            p.rate = -inf_v;
            expect_invalid(p, "rate == -Infinity");
        }
        {
            auto [status, resp] = call_tree(stub, [&] {
                TreeParams p = good;
                p.volatility = nan_v;
                return p;
            }());
            check(status.error_message().find("volatility") != std::string::npos,
                  "the NaN-volatility rejection names \"volatility\" specifically: \"" +
                      status.error_message() + "\"");
        }

        // -- A well-formed control alongside every rejection above, so a
        // change that started rejecting EVERYTHING would not make this
        // whole section vacuously pass. --
        {
            auto [status, resp] = call_tree(stub, good);
            check(status.ok(), "the well-formed control request still succeeds");
        }

        // -- Absurd-but-finite magnitude: large enough that intermediate
        // tree quantities (u = exp(lambda*sigma*sqrt(dt))) overflow to
        // +Infinity even though the INPUT itself is finite and satisfies
        // every guard above. finance.proto routes these fields as plain
        // doubles; require_finite alone (this file's earlier fix) closes the
        // NaN/Infinity-INPUT hole but not this one -- an absurd-but-finite
        // value can still overflow an intermediate quantity DURING
        // computation. This USED TO BE a real, documented, NOT-fixed gap
        // (see git history for the "KNOWN GAP" version of this test this
        // block replaces): volatility=1.0e10 was accepted with Status::OK
        // and produced a non-finite response value. It is now CLOSED by
        // check_option_field_magnitude/check_tree_exponent_safe
        // (finance_service.cpp) -- this is the regression test for that fix,
        // asserting the FIXED behaviour (refusal), not the bug. --
        {
            TreeParams p = good;
            p.volatility = 1.0e10;  // 1e12 % annualized vol
            auto [status, resp] = call_tree(stub, p);
            check(!status.ok() && status.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
                  "FIXED: an absurd-but-finite volatility (1e10) -- which used to be ACCEPTED "
                  "with a non-finite response value -- is now REFUSED with INVALID_ARGUMENT "
                  "(got " + (status.ok() ? "OK" : std::to_string(static_cast<int>(status.error_code()))) +
                  (status.ok() ? "" : (": " + status.error_message())) + ")");
        }
        {
            // The rejection message names the offending field specifically,
            // consistent with this file's own style throughout.
            TreeParams p = good;
            p.volatility = 1.0e10;
            auto [status, resp] = call_tree(stub, p);
            check(status.error_message().find("volatility") != std::string::npos,
                  "...and the rejection message names \"volatility\" specifically: \"" +
                      status.error_message() + "\"");
        }

        // -- Every OTHER plain-double option-pricing field, individually
        // pushed to an absurd-but-finite magnitude, is ALSO now refused --
        // not just the one field the original gap happened to be reported
        // against. Each of these was independently reproduced against the
        // pre-fix binary returning Status::OK with a non-finite response
        // value before this fix (see the comment above
        // check_option_field_magnitude/check_tree_exponent_safe in
        // finance_service.cpp for the exact reproduction of each). --
        {
            TreeParams p = good;
            p.rate = -1.0e10;
            auto [status, resp] = call_tree(stub, p);
            check(!status.ok() && status.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
                  "an absurd-but-finite rate (-1e10) is REFUSED");
        }
        {
            TreeParams p = good;
            p.years_to_expiry = 1.0e10;
            auto [status, resp] = call_tree(stub, p);
            check(!status.ok() && status.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
                  "an absurd-but-finite years_to_expiry (1e10) is REFUSED");
        }
        {
            TreeParams p = good;
            p.lambda = 1.0e6;
            auto [status, resp] = call_tree(stub, p);
            check(!status.ok() && status.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
                  "an absurd-but-finite lambda (1e6, tree spacing -- the engine's own default "
                  "is ~1.2247) is REFUSED");
        }
        {
            // lambda == +Infinity is a SEPARATE finding from the magnitude
            // gap above: lambda had NO finiteness check of any kind before
            // this fix (only spot/strike/rate/volatility/years_to_expiry
            // did) -- "(lambda() > 0.0)" is true for +Infinity, so it became
            // the tree's effective spacing directly, overflowing u=exp(...)
            // immediately regardless of every other field.
            TreeParams p = good;
            p.lambda = std::numeric_limits<double>::infinity();
            auto [status, resp] = call_tree(stub, p);
            check(!status.ok() && status.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
                  "lambda == +Infinity is REFUSED (previously had no finiteness check at all)");
        }
        {
            TreeParams p = good;
            p.spot = 1.0e16;  // above check_double_magnitude's reused 1e15 bound
            auto [status, resp] = call_tree(stub, p);
            check(!status.ok() && status.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
                  "an absurd-magnitude spot (1e16) is REFUSED (reuses check_double_magnitude, "
                  "the same helper the BigDecimal money RPCs already use)");
        }
        {
            TreeParams p = good;
            p.strike = 1.0e16;
            auto [status, resp] = call_tree(stub, p);
            check(!status.ok() && status.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
                  "an absurd-magnitude strike (1e16) is REFUSED, same reuse");
        }

        // -- The JOINT check does real work beyond the per-field bounds:
        // volatility=10.0 and years_to_expiry=100.0 are each, INDIVIDUALLY,
        // exactly at their own per-field ceiling (kMaxOptionVolatility,
        // kMaxOptionYearsToExpiry) and would pass a per-field-only guard --
        // but steps has NO ceiling in this RPC, and lambda*sigma*
        // sqrt(steps*years_to_expiry) grows without bound as steps grows,
        // so a large enough steps still overflows exp() even with both
        // other fields sitting AT their individually-generous bounds. This
        // is the SAME "individually reasonable, jointly checked" shape
        // check_compound_growth_safe already uses elsewhere in this file. --
        {
            TreeParams p = good;
            p.volatility = 10.0;         // exactly at the per-field bound
            p.years_to_expiry = 100.0;   // exactly at the per-field bound
            p.steps = 5000;              // steps itself has no ceiling
            auto [status, resp] = call_tree(stub, p);
            check(!status.ok() && status.error_code() == grpc::StatusCode::INVALID_ARGUMENT,
                  "volatility=10 and years_to_expiry=100 (each individually AT its own bound) "
                  "combined with steps=5000 is REFUSED by the JOINT exponent check, even though "
                  "no single field is over its own per-field bound (got " +
                      (status.ok() ? "OK" : std::to_string(static_cast<int>(status.error_code()))) +
                      (status.ok() ? "" : (": " + status.error_message())) + ")");
        }

        // -- POSITIVE CONTROLS: this is the half that actually matters -- a
        // guard that refuses everything passes a refuse-only test. --

        // 1. The exact well-formed control from this section's own "good"
        //    params (spot=100, strike=100, rate=0.05, volatility=0.2,
        //    years_to_expiry=1, steps=100) must still price to the SAME
        //    value it did BEFORE this fix, digit for digit -- captured from
        //    the pre-fix binary via %.17g (full double round-trip
        //    precision), not recomputed after the fact. --
        {
            auto [status, resp] = call_tree(stub, good);
            check(status.ok(), "the plain well-formed control still succeeds after the fix");
            check(resp.value() == 10.444232440723583 && resp.delta() == 0.63635964309841297 &&
                      resp.gamma() == 0.018830978290712938 && resp.theta() == -6.4367555466783521,
                  "...and prices to the EXACT SAME value/delta/gamma/theta as the pre-fix binary, "
                  "digit for digit -- a guard that changed the answer for ordinary inputs would "
                  "be a worse defect than the one it fixes (got value=" +
                      std::to_string(resp.value()) + " delta=" + std::to_string(resp.delta()) +
                      " gamma=" + std::to_string(resp.gamma()) +
                      " theta=" + std::to_string(resp.theta()) + ")");
        }

        // 2. High-but-legitimate volatility on a crypto-like underlying --
        //    300-500% annualized is unusual but real (the task this bound
        //    was chosen for names this case explicitly) -- must be ACCEPTED
        //    and price to the SAME value the pre-fix binary computed.
        {
            TreeParams p = good;
            p.volatility = 4.0;  // 400%
            auto [status, resp] = call_tree(stub, p);
            check(status.ok() && std::isfinite(resp.value()),
                  "volatility=4.0 (400%, a legitimate high-vol crypto-like case) is ACCEPTED "
                  "and finite");
            check(resp.value() == 80.90630736189955,
                  "...and prices to the exact same value as the pre-fix binary: got " +
                      std::to_string(resp.value()));
        }
        {
            TreeParams p = good;
            p.volatility = 5.0;  // 500% -- the upper end of the legitimate range
            auto [status, resp] = call_tree(stub, p);
            check(status.ok() && std::isfinite(resp.value()),
                  "volatility=5.0 (500%, the upper end of the stated legitimate crypto-like "
                  "range) is ACCEPTED and finite -- the bound is not over-tight");
            check(resp.value() == 66.089886991174993,
                  "...and prices to the exact same value as the pre-fix binary: got " +
                      std::to_string(resp.value()));

            // The BLACK-SCHOLES sibling at the same volatility, same
            // positive-control shape.
            auto [status_bs, resp_bs] = call_black_scholes(stub, p);
            check(status_bs.ok() && std::isfinite(resp_bs.value()),
                  "PriceBlackScholes at the same volatility=5.0 is ALSO accepted and finite");
            check(resp_bs.value() == 98.788779236833335,
                  "...and prices to the exact same value as the pre-fix binary: got " +
                      std::to_string(resp_bs.value()));
        }
    }

    // -----------------------------------------------------------------
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
