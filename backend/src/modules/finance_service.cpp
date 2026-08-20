module;
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <exception>
#include <string>
#include <string_view>
#include <vector>

#include <grpcpp/grpcpp.h>
#include "finance.pb.h"
#include "finance.grpc.pb.h"

module finance_service;

import sensen.bigdecimal;
import sensen.financial;
import sensen.options;
import sensen.portfolio;
import sensen.linear_algebra;
import logger;
import quota;
import api_key;

// SGEE: used ONLY by the four option-pricing RPCs' shared request-lifecycle
// graph (PriceOptionTree, PriceBlackScholes, PriceOptionMonteCarlo,
// ComputeProbabilityTree) -- see the "Shared request-lifecycle graph" comment
// below for why the other ~46 RPCs in this file are NOT routed through SGEE.
import sgee.builder.fluent;
import sgee.runtime.context;
import sgee.runtime.interpreter;
import sgee.runtime.action_registry;
import sgee.core.blueprint;
import sgee.core.types;

namespace options_calculator::finance {

using grpc::ServerContext;
using grpc::Status;
using sensen::BigDecimal;

namespace {

// ---------------------------------------------------------------------------
// Decimal marshalling
// ---------------------------------------------------------------------------

/**
 * Strict decimal parsing, because BigDecimal's own is not.
 *
 * `BigDecimal::parse` skips any character that is not a digit. "abc" therefore
 * parses to 0 and "12x3" parses to 123 -- silently, with no error. That is
 * survivable inside a library whose callers are compiled against it; it is not
 * survivable on a public API, where the input is whatever arrived over the
 * wire. A typo becoming a number is exactly the class of failure a financial
 * service must not have.
 *
 * So the string is validated HERE, against the grammar BigDecimal actually
 * documents, before it is handed over: optional sign, digits, at most one
 * point, at least one digit somewhere. An empty string is proto3's default for
 * an unset field and legitimately means zero -- every optional decimal in
 * finance.proto relies on that.
 */
[[nodiscard]] auto parse_decimal(const std::string& s, BigDecimal& out) noexcept -> bool {
    std::string_view v{s};
    while (!v.empty() && (std::isspace(static_cast<unsigned char>(v.front())) != 0)) {
        v.remove_prefix(1);
    }
    while (!v.empty() && (std::isspace(static_cast<unsigned char>(v.back())) != 0)) {
        v.remove_suffix(1);
    }
    if (v.empty()) {
        out = BigDecimal(0);
        return true;
    }

    std::string_view body = v;
    if (body.front() == '-' || body.front() == '+') body.remove_prefix(1);
    if (body.empty()) return false;

    bool seen_digit = false;
    bool seen_point = false;
    for (const char c : body) {
        if (c == '.') {
            if (seen_point) return false;   // two points is not a number
            seen_point = true;
        } else if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
            seen_digit = true;
        } else {
            return false;
        }
    }
    if (!seen_digit) return false;          // "." and "-" alone are not numbers

    out = BigDecimal(v);
    return true;
}

/** The error a malformed decimal earns. Names the field so it is actionable. */
[[nodiscard]] auto bad_decimal(std::string_view field, const std::string& value) -> Status {
    return Status(grpc::StatusCode::INVALID_ARGUMENT,
                  std::string(field) + " is not a decimal number: \"" + value + "\"");
}

/**
 * The error an ABSENT-but-required decimal field earns.
 *
 * Distinct from bad_decimal: this is not a parse failure, it is proto3
 * scalar non-presence -- an unset string field and one explicitly set to ""
 * are indistinguishable on the wire, and BOTH read back as "". That
 * ambiguity is exactly why READ_DECIMAL treats "" as a legitimate zero for
 * fields the maths can genuinely default (future_value, PMI, overpayments,
 * ...): a caller who means "$0" can and should say so explicitly. For a
 * field the computation cannot proceed without -- a rate, a principal, a
 * price -- the same "" must instead be read as "the caller said nothing",
 * because silently substituting 0 there is precisely the defect this
 * service exists to refuse (an absent rate becoming a 0% loan, an absent
 * principal becoming a free one). See REQUIRE_DECIMAL below.
 */
[[nodiscard]] auto missing_field(std::string_view field) -> Status {
    return Status(grpc::StatusCode::INVALID_ARGUMENT,
                  std::string(field) + " is required and was not supplied");
}

/**
 * Reads one decimal field or fails the RPC.
 *
 * A macro rather than a function because it returns from the CALLER on
 * failure, which is the whole point: there is no sensible value to substitute
 * for a number that could not be read, and continuing with a zero would be the
 * silent-default this service exists to avoid.
 */
#define READ_DECIMAL(dest, expr, name)                       \
    BigDecimal dest;                                         \
    do {                                                     \
        const std::string& _s = (expr);                      \
        if (!parse_decimal(_s, dest)) {                      \
            return bad_decimal((name), _s);                  \
        }                                                    \
    } while (false)

/**
 * Reads one decimal field the computation cannot proceed without, or fails
 * the RPC -- and, critically, fails it on an EMPTY string, not just a
 * malformed one.
 *
 * This is READ_DECIMAL's sibling, not a replacement for it: most decimal
 * fields in this file legitimately default to zero when absent
 * (future_value, PMI, overpayments, ...) and READ_DECIMAL is exactly right
 * for those. REQUIRE_DECIMAL is for the other kind -- a rate, a principal, a
 * price -- where an absent field is a caller who said nothing, and treating
 * that silence as "$0" or "0%" produces a plausible, confidently wrong
 * answer (a mortgage payment computed at 0% interest) rather than an
 * obviously broken one. An explicit "0" still parses and still means
 * exactly what it says; only emptiness is refused here.
 */
#define REQUIRE_DECIMAL(dest, expr, name)                    \
    BigDecimal dest;                                         \
    do {                                                     \
        const std::string& _s = (expr);                      \
        if (_s.empty()) {                                    \
            return missing_field((name));                    \
        }                                                    \
        if (!parse_decimal(_s, dest)) {                      \
            return bad_decimal((name), _s);                  \
        }                                                    \
    } while (false)

[[nodiscard]] auto timing_of(sensen::finance::AnnuityTiming t) noexcept -> int {
    return (t == sensen::finance::BEGINNING_OF_PERIOD) ? 1 : 0;
}

[[nodiscard]] auto option_type_of(sensen::finance::OptionType t) noexcept -> sensen::OptionType {
    return (t == sensen::finance::PUT) ? sensen::OptionType::Put : sensen::OptionType::Call;
}

[[nodiscard]] auto asian_type_of(sensen::finance::AsianType t) noexcept -> sensen::AsianType {
    switch (t) {
        case sensen::finance::AVERAGE_PRICE:  return sensen::AsianType::AveragePrice;
        case sensen::finance::AVERAGE_STRIKE: return sensen::AsianType::AverageStrike;
        default:                              return sensen::AsianType::None;
    }
}

/**
 * Refuses a non-finite (NaN or +/-Infinity) option-pricing field.
 *
 * PriceOptionTree/PriceBlackScholes/PriceOptionMonteCarlo's numeric fields
 * are plain wire doubles, not the BigDecimal STRINGS the money RPCs above
 * validate via READ_DECIMAL/REQUIRE_DECIMAL -- there is no decimal-string
 * parse step here for those macros to guard, so nothing else in this file
 * catches a NaN or +/-Infinity in one of them. That matters because a "<= 0"
 * positivity check (PriceOptionTree's own guard, right below) does not
 * catch either: NaN compares false against every relation including "<= 0"
 * and "> 0", and +Infinity compares true against "> 0" same as any other
 * huge positive number. Both sail straight through and reach
 * price_option_double(), which is pure double arithmetic and does not
 * throw for either -- without this, the RPC would return Status::OK
 * carrying a NaN/Infinity value, delta, gamma or theta instead of refusing
 * the request that produced it.
 */
[[nodiscard]] auto require_finite(double v, std::string_view field) -> Status {
    if (!std::isfinite(v)) {
        return Status(grpc::StatusCode::INVALID_ARGUMENT,
                      std::string(field) + " must be a finite number, got " + std::to_string(v));
    }
    return Status::OK;
}

/** A std::expected failure becomes the gRPC error carrying its own message. */
template <typename T>
[[nodiscard]] auto fail(const std::expected<T, std::string>& e) -> Status {
    return Status(grpc::StatusCode::FAILED_PRECONDITION, e.error());
}

// ---------------------------------------------------------------------------
// Adversarial-input guards shared by the six home-finance RPCs
// (ComputeRefinance, ComputePayoffTiming, ComputeMortgageRecast,
// ComputeHomeFutureValue, ComputeRentVsBuy, ComputeHomeNpv).
//
// Three independent hazards, none of them hypothetical -- each was measured
// against a live engine before this guard was written:
//
//   1. `payments_per_year` had a floor (>0) but no ceiling. sensen multiplies
//      it against a caller-supplied year count to get a period count that
//      either becomes a loop bound (calculate_refinance_metrics) or a
//      BigDecimal exponent (pmt/pv/fv). A request with
//      new_term_years=100, payments_per_year=20,000,000 measured 11.5s of
//      wall-clock CPU on this engine for ONE call, charged only
//      cost_amortization(1200) -- 101 compute units against a 120,000/hour
//      anonymous budget -- because the CHARGE line priced
//      new_term_years*12, not new_term_years*payments_per_year. A larger
//      payments_per_year overflows the int32 product outright and returns a
//      wrong-but-plausible-looking answer instead of an error (observed:
//      payoff_date_shift_months = -1863463212 from payments_per_year =
//      2,000,000,000).
//   2. BigDecimal is exact but not unbounded: __int128 scaled by 1e18 caps a
//      representable value at roughly 1.7e20 (about 20 integer digits), and
//      neither its string constructor nor its multiply()/pow() detect an
//      overflow -- they wrap silently. A 200-digit `current_loan_balance`
//      measured back as a NEGATIVE new_loan_amount; a 30-digit principal
//      produced a `new_loan_amount` with no relation to the input. Money
//      fields are strings specifically so a client is not asked to trust an
//      inexact double -- silently wrapping past __int128 is the same
//      failure by a different route.
//   3. A rate at or below -100% is not a rate this service's models can
//      price: it zeroes or negates `1 + rate_per_period`, which is either a
//      domain error for `pow()`/`log()` or, worse, evaluates cleanly to a
//      wrong answer. Measured: annual_rate=-1 (payments_per_year=1) made
//      ComputePayoffTiming return original_months_remaining=0 -- "this loan
//      is already paid off" -- for a loan that is not, because
//      log(0)/log(1-1) reduces through IEEE floating point to exactly 0.0
//      rather than raising. And independently, a rate and a term that are
//      each individually plausible can compound into a factor no fixed-point
//      type can hold (6% compounded ANNUALLY, not monthly, over 1200
//      periods) -- neither field alone catches that; only their product does.
// ---------------------------------------------------------------------------

/**
 * Refuses `payments_per_year` above a real payment cadence.
 *
 * 366 (daily, leap year) is the finest schedule this service's own field
 * comments name ("12 monthly, 26 bi-weekly"); nothing finer is a real
 * payment frequency. The ceiling exists because sensen multiplies this value
 * against a caller-controlled year count -- see the guard header above.
 */
[[nodiscard]] auto check_payments_per_year(int payments_per_year) -> Status {
    if (payments_per_year <= 0) {
        return Status(grpc::StatusCode::INVALID_ARGUMENT,
                      "payments_per_year must be positive (12 monthly, 26 bi-weekly)");
    }
    if (payments_per_year > 366) {
        return Status(grpc::StatusCode::INVALID_ARGUMENT,
                      "payments_per_year exceeds 366 (daily, leap year); no real payment "
                      "schedule is finer than that");
    }
    return Status::OK;
}

/**
 * Refuses a decimal field whose magnitude would silently overflow
 * BigDecimal's exact __int128 range once it enters the request's
 * add/multiply/pow chains.
 *
 * Operates on the RAW WIRE STRING, not on a BigDecimal already constructed
 * from it -- an overflowed BigDecimal is already garbage, so checking its
 * own to_double() afterward cannot reliably detect that the overflow
 * happened. 15 integer digits (up to a quadrillion) is already an absurd
 * figure for anything this calculator prices and leaves five orders of
 * magnitude of headroom below BigDecimal's ~20-digit hard ceiling for the
 * arithmetic that follows.
 */
[[nodiscard]] auto check_decimal_string_magnitude(const std::string& s, std::string_view field)
    -> Status {
    std::string_view v{s};
    while (!v.empty() && (std::isspace(static_cast<unsigned char>(v.front())) != 0)) {
        v.remove_prefix(1);
    }
    while (!v.empty() && (std::isspace(static_cast<unsigned char>(v.back())) != 0)) {
        v.remove_suffix(1);
    }
    if (!v.empty() && (v.front() == '-' || v.front() == '+')) v.remove_prefix(1);

    std::size_t int_digits = 0;
    for (const char c : v) {
        if (c == '.') break;
        if (std::isdigit(static_cast<unsigned char>(c)) != 0) ++int_digits;
    }
    if (int_digits > 15) {
        return Status(grpc::StatusCode::INVALID_ARGUMENT,
                      std::string(field) +
                          " is too large in magnitude for this calculator (the exact decimal "
                          "engine has a finite range)");
    }
    return Status::OK;
}

/**
 * Reads a decimal field AND rejects it if its magnitude cannot safely enter
 * BigDecimal arithmetic. The magnitude check runs on the raw string, before
 * `READ_DECIMAL` ever constructs a BigDecimal from it -- see
 * check_decimal_string_magnitude's own comment for why that ordering is
 * load-bearing.
 */
#define READ_DECIMAL_SAFE(dest, expr, name)                             \
    do {                                                                \
        if (auto _m = check_decimal_string_magnitude((expr), (name));   \
            !_m.ok()) {                                                 \
            return _m;                                                 \
        }                                                               \
    } while (false);                                                    \
    READ_DECIMAL(dest, expr, name)

/**
 * REQUIRE_DECIMAL's sibling for the six RPCs guarded by
 * check_decimal_string_magnitude: refuses an EMPTY field (the caller said
 * nothing) same as REQUIRE_DECIMAL, then applies the magnitude guard before
 * parsing, same as READ_DECIMAL_SAFE. An empty string is checked first
 * because "" has zero magnitude and would otherwise sail through the
 * magnitude check only to be silently parsed as 0 -- exactly the defect
 * this macro exists to close.
 */
#define REQUIRE_DECIMAL_SAFE(dest, expr, name)                          \
    do {                                                                \
        if ((expr).empty()) {                                          \
            return missing_field((name));                              \
        }                                                               \
        if (auto _m = check_decimal_string_magnitude((expr), (name));   \
            !_m.ok()) {                                                 \
            return _m;                                                 \
        }                                                               \
    } while (false);                                                    \
    READ_DECIMAL(dest, expr, name)

/**
 * Refuses a per-period rate at or below -100% (`1 + rate_per_period <= 0`).
 *
 * Below this floor sensen's own math either hits a domain error it cannot
 * always detect (see the header comment's payoff-timing example, where it
 * silently evaluates to a wrong zero instead) or, for a BigDecimal `pow()`
 * caller, a zero or negative base raised to a large integer exponent.
 * Neither is a rate any real financial instrument carries.
 */
[[nodiscard]] auto check_rate_floor(double rate_per_period, std::string_view field) -> Status {
    const double one_plus_r = 1.0 + rate_per_period;
    if (!(one_plus_r > 0.0) || !std::isfinite(one_plus_r)) {
        return Status(grpc::StatusCode::INVALID_ARGUMENT,
                      std::string(field) + ": rate per period must be greater than -100%");
    }
    return Status::OK;
}

/**
 * Refuses an (annual rate, payment frequency, period count) triple whose
 * compound growth factor -- (1 + rate/payments_per_year)^periods -- would
 * overflow BigDecimal's pow() once multiplied against a real principal.
 *
 * This is a JOINT check on purpose. `payments_per_year` is already capped at
 * 366 and every period count this file accepts is already capped at 1200
 * months / 100 years, and each bound is individually reasonable -- but their
 * PRODUCT is what sensen's pow() actually raises a rate to, and a rate that
 * is itself reasonable (6%) compounded over that product with the wrong
 * frequency (annually, not monthly, over 1200 periods) is already
 * astronomically outside any fixed-point type's range. 40 natural-log units
 * of growth (~2.35e17-fold) is already an absurd compounding outcome for any
 * real financial instrument and sits comfortably under BigDecimal's ~46.6
 * nat ceiling, leaving headroom for the multiply against principal that the
 * caller's pmt/pv/fv/recast call performs next.
 */
[[nodiscard]] auto check_compound_growth_safe(double annual_rate, int payments_per_year,
                                              int periods, std::string_view context) -> Status {
    if (payments_per_year <= 0) return Status::OK;  // caught by check_payments_per_year already
    const double rate_per_period = annual_rate / static_cast<double>(payments_per_year);
    if (auto s = check_rate_floor(rate_per_period, context); !s.ok()) return s;
    if (periods <= 0) return Status::OK;

    const double log_growth = static_cast<double>(periods) * std::log1p(rate_per_period);
    if (!std::isfinite(log_growth) || log_growth > 40.0 || log_growth < -40.0) {
        return Status(grpc::StatusCode::INVALID_ARGUMENT,
                      std::string(context) +
                          ": rate and term combine to a compounding factor too extreme for "
                          "this calculator to price (would overflow the exact decimal engine)");
    }
    return Status::OK;
}

/**
 * check_compound_growth_safe's sibling for the TIME-VALUE-OF-MONEY RPCs
 * (ComputePayment/ComputePresentValue/ComputeFutureValue/ComputeInterestPayment/
 * ComputePrincipalPayment), whose `rate` field is already documented as a
 * PER-PERIOD rate (finance.proto: "per-period rate, decimal (0.004166...
 * monthly)") -- unlike the six home-finance RPCs above, there is no
 * payments_per_year to divide an annual rate by first.
 *
 * Reachable exactly the same way ComputeMortgageRecast's own annual_rate gap
 * was: `pmt`/`pv`/`fv` (financial.cppm) all raise `1+rate` to `periods` via
 * BigDecimal::pow -- exponentiation by squaring, so the call itself is cheap
 * (~31 multiplies) regardless of how large `periods` is, but a `rate` whose
 * *magnitude* is absurd (nothing stops periods, rate, or both from being
 * whatever the caller likes; ComputePayment's REQUIRE_DECIMAL has no upper
 * bound at all) still wraps BigDecimal's exact __int128 range. Reproduced
 * directly: ComputeAmortization{annual_rate=1000000, term_months=1200}
 * (calculate_mortgage_amortization -> the SAME pmt() with monthly_rate=
 * annual_rate/12) returned total_interest_paid =
 * "29999999999999.999999999880000000" -- thirty trillion dollars of
 * interest on a request that never named a loan_amount over $300,000,
 * silently wrapped rather than refused.
 */
[[nodiscard]] auto check_compound_growth_safe_periods(double rate_per_period, int periods,
                                                      std::string_view context) -> Status {
    if (auto s = check_rate_floor(rate_per_period, context); !s.ok()) return s;
    if (periods <= 0) return Status::OK;

    const double log_growth = static_cast<double>(periods) * std::log1p(rate_per_period);
    if (!std::isfinite(log_growth) || log_growth > 40.0 || log_growth < -40.0) {
        return Status(grpc::StatusCode::INVALID_ARGUMENT,
                      std::string(context) +
                          ": rate and periods combine to a compounding factor too extreme for "
                          "this calculator to price (would overflow the exact decimal engine)");
    }
    return Status::OK;
}

/**
 * Refuses a period COUNT above any real payment schedule -- the TVM-family
 * analogue of check_term's 1200-month cap, generalized because "periods"
 * here is not always months (a per-period rate can describe daily, weekly,
 * or any other cadence). 100,000 periods is already an absurd figure (over
 * 270 years of DAILY payments) and exists purely to bound the O(periods)
 * work `ipmt`/`ppmt` (financial.cppm) do internally -- their per-period
 * balance-walk loop runs `per` times, and neither `periods` (nper) nor
 * `period` (per) carried any upper bound before this guard. Reproduced
 * directly: ComputeInterestPayment{period=periods=3,000,000, ...} is
 * accepted today and measurably burns CPU proportional to `period` for an
 * unauthenticated, flat-rate-charged caller -- cheap per request at this
 * particular size, but genuinely unbounded, and the quota system charges
 * quota::cost_default() regardless of how large the caller asks for it to
 * be, exactly the same pattern ComputeRefinance's own payments_per_year
 * guard (above) was written to close.
 */
[[nodiscard]] auto check_period_count_ceiling(int periods, std::string_view field) -> Status {
    if (periods > 100'000) {
        return Status(grpc::StatusCode::INVALID_ARGUMENT,
                      std::string(field) +
                          " exceeds 100,000; no real payment schedule (even daily, for "
                          "centuries) needs more periods than that");
    }
    return Status::OK;
}

/**
 * Refuses a non-finite element anywhere in a repeated-double cash-flow
 * field. `ComputeNpv`/`ComputeXnpv`/`ComputeIrr`/`ComputeXirr`/
 * `ComputePaybackPeriod` all sum or Newton-iterate directly over these
 * values in double arithmetic (financial.cppm's npv_double/xnpv/irr/xirr/
 * payback_period) with no finiteness check of their own; a single NaN
 * anywhere in the list propagates through the arithmetic straight into the
 * response. Newton solvers (irr/xirr) usually self-protect by simply never
 * converging on a NaN chase and returning FAILED_PRECONDITION after their
 * fixed 100-iteration budget -- but the plain-sum RPCs (npv_double/xnpv) do
 * not iterate at all, so a NaN reaches Status::OK. Checked uniformly here
 * regardless of which failure mode a given callee would hit, so the error
 * names the actual bad element rather than a generic solver-convergence
 * message.
 */
template <typename Repeated>
[[nodiscard]] auto require_all_finite(const Repeated& values, std::string_view field) -> Status {
    int idx = 0;
    for (const double v : values) {
        if (!std::isfinite(v)) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          std::string(field) + "[" + std::to_string(idx) +
                              "] must be a finite number, got " + std::to_string(v));
        }
        ++idx;
    }
    return Status::OK;
}

/**
 * Refuses a raw wire-double field whose magnitude would overflow
 * BigDecimal's exact __int128 range once `BigDecimal{double}` --
 * `static_cast<__int128_t>(std::round(val * 1e18))` -- constructs from it.
 *
 * check_decimal_string_magnitude's sibling for the handful of RPCs that
 * carry money as a raw wire DOUBLE rather than a decimal STRING
 * (ComputeAmortizationBatch's loan_amounts/annual_rates/extra_payments/
 * pmi_rates/home_values, ComputeCumulative's rate/present_value) -- there is
 * no parse step for that helper's digit-count scan to run against, so the
 * bound is applied directly to the value. `require_finite` alone is NOT
 * enough here: above roughly 1.7e20 the scaled value (`val * 1e18`) no
 * longer fits in __int128 at all, so casting it is UNDEFINED BEHAVIOUR per
 * [conv.fpint], not merely an overflowed-but-defined wraparound -- and a
 * perfectly finite double as "small" as 1e30 already crosses that line, so
 * `require_finite`'s own "is this a real number" check passes it straight
 * through. Same 1e15 bound as check_decimal_string_magnitude (15 integer
 * digits, already an absurd figure for anything this calculator prices) for
 * the identical reason -- headroom below BigDecimal's own ~20-digit ceiling
 * for the arithmetic that follows.
 */
[[nodiscard]] auto check_double_magnitude(double v, std::string_view field) -> Status {
    if (std::abs(v) >= 1e15) {
        return Status(grpc::StatusCode::INVALID_ARGUMENT,
                      std::string(field) +
                          " is too large in magnitude for this calculator (the exact decimal "
                          "engine has a finite range)");
    }
    return Status::OK;
}

// ---------------------------------------------------------------------------
// Magnitude guards for the FOUR option-pricing RPCs (PriceOptionTree,
// PriceBlackScholes, PriceOptionMonteCarlo, ComputeProbabilityTree).
//
// `require_finite` (above) closes the NaN/+-Infinity-INPUT hole: it stops a
// bad wire value from sailing through a "<= 0" positivity check the way NaN
// and +Infinity both do. It does NOT stop a FINITE-but-absurd value from
// overflowing an INTERMEDIATE quantity DURING computation. Reproduced
// directly against this file's pre-fix binary: PriceOptionTree{spot=100,
// strike=100, rate=0.05, volatility=1.0e10, years_to_expiry=1, steps=100}
// returned Status::OK with value=-nan -- every individual field passed
// require_finite and the ">0" positivity checks, and the response was still
// garbage.
//
// The mechanism (sensen::price_option_double /
// sensen::calculate_probability_tree, options.cppm): both build a trinomial
// tree whose most extreme node is
//     S0 * u^steps,  u = exp(lambda * sigma * sqrt(years_to_expiry / steps))
// which reduces to
//     S0 * exp(lambda * sigma * sqrt(steps * years_to_expiry))
// -- an EXPONENT that grows with volatility, years_to_expiry, lambda AND
// steps, all four jointly, not any one of them alone. `std::exp` overflows
// to +Infinity once its argument exceeds ln(DBL_MAX) =~ 709.78; the bounds
// below keep that exponent under 650 (comment on
// check_tree_exponent_safe has the arithmetic), leaving ~60 nats
// (~8%) of headroom below the hard ceiling for whatever the backward
// induction or Black-Scholes' own Greek formulas do on top of it -- the
// same "individually-generous, jointly-checked" shape check_compound_growth_
// safe already uses for the BigDecimal TVM/mortgage RPCs, just for a
// different closed-form limit (IEEE double's ~1.8e308 range via exp(), not
// BigDecimal's ~1.7e20 __int128 range).
//
// Every bound is chosen to be an order of magnitude or more above the
// largest figure a real options desk would ever submit, per field:
//   - volatility: 1000% (10.0) annualised. A 300-500% annualised volatility
//     is unusual but real for a crypto-like underlying -- the task this
//     guard was written for names that case explicitly -- so the bound sits
//     at 2x that, not merely just-above-the-legitimate-value.
//   - rate: +/-500% (5.0) continuously compounded. No real risk-free,
//     dividend, or cost-of-carry rate approaches this even in a hyper-
//     inflationary economy; it is kept symmetric because a very negative
//     rate is exactly as capable of overflowing exp(-rate*T) as a very
//     positive one is capable of overflowing exp(+rate*T) (BS's discount
//     factor is exp(-rate*T); MC's per-step drift is (rate-0.5*sigma^2)*dt).
//   - years_to_expiry: 100 years. The SAME bound this file already uses
//     elsewhere (ComputeFutureValueDetailed's `years <= 100`: "no real
//     projection horizon runs longer than a lifetime"); real options rarely
//     exceed 30-year LEAPS.
//   - lambda (tree spacing; PriceOptionTree/ComputeProbabilityTree only):
//     10.0. Every real caller either omits it (0 selects the engine's own
//     default, sqrt(1.5) =~ 1.2247) or passes something close to that
//     default -- there is no legitimate reason to deviate far from it, and
//     it enters the SAME exponent as sigma multiplicatively, so an
//     unchecked lambda is exactly as dangerous as an unchecked volatility.
//
// spot/strike are NOT given a new bound here: they are ordinary money-like
// quantities, exactly what check_double_magnitude (above) already exists
// to bound, so this guard reuses it rather than inventing a third
// magnitude helper for the same shape of field.
[[nodiscard]] auto check_option_field_magnitude(double v, std::string_view field, double bound,
                                                std::string_view legitimate_ceiling) -> Status {
    if (std::abs(v) > bound) {
        return Status(grpc::StatusCode::INVALID_ARGUMENT,
                      std::string(field) +
                          " is too large in magnitude for this option pricer (no real position "
                          "needs " + std::string(field) + " beyond " +
                          std::string(legitimate_ceiling) + ")");
    }
    return Status::OK;
}

constexpr double kMaxOptionVolatility = 10.0;       // 1000% annualised
constexpr double kMaxOptionRate = 5.0;              // +/-500% continuously compounded
constexpr double kMaxOptionYearsToExpiry = 100.0;   // 100 years
constexpr double kMaxOptionLambda = 10.0;           // tree spacing; default is sqrt(1.5)=~1.2247

/**
 * The joint check that actually closes the overflow gap for the two TREE
 * RPCs (PriceOptionTree, ComputeProbabilityTree): even with every one of the
 * per-field bounds above enforced, `steps` itself carries NO ceiling in
 * either RPC (PriceOptionTree only floors it at >=2; ComputeProbabilityTree
 * only requires >0) -- and steps enters the SAME exponent through
 * sqrt(steps * years_to_expiry). A caller at exactly the per-field bounds
 * (volatility=10, years_to_expiry=100) with a large enough steps can still
 * overflow: sqrt(steps*100) grows without limit as steps grows, so the
 * per-field bounds ALONE cannot make this safe -- the exponent must be
 * checked directly, mirroring check_compound_growth_safe's own "each bound
 * is individually reasonable but their PRODUCT is what matters" reasoning.
 *
 * lambda's own effective value mirrors the RPC's own default-selection
 * logic exactly (lambda <= 0 selects the engine's sqrt(1.5) default) so
 * this check evaluates the SAME exponent the pricer itself will compute.
 *
 * 650 nats leaves ~60 nats (~8%) of headroom below IEEE double's exp()
 * overflow threshold (ln(DBL_MAX) =~ 709.78) for the backward induction's
 * own combination of neighbouring tree nodes on top of the single extreme
 * node this function bounds.
 */
[[nodiscard]] auto check_tree_exponent_safe(double sigma, double lambda_field,
                                            double years_to_expiry, int steps,
                                            std::string_view context) -> Status {
    const double lambda_eff = (lambda_field > 0.0) ? lambda_field : 1.224744871391589;
    const double exponent =
        lambda_eff * sigma * std::sqrt(static_cast<double>(steps) * years_to_expiry);
    if (!std::isfinite(exponent) || exponent > 650.0) {
        return Status(grpc::StatusCode::INVALID_ARGUMENT,
                      std::string(context) +
                          ": lambda, volatility, years_to_expiry and steps combine to a tree-node "
                          "exponent too extreme for this calculator to price (would overflow the "
                          "underlying exp() to +Infinity)");
    }
    return Status::OK;
}

/**
 * Refuses a response whose numeric fields came back non-finite despite every
 * guard above -- defence in depth, not the primary guard. The per-field and
 * joint checks above close the SPECIFIC, reproduced overflow path (the tree
 * node exponent); this closes whatever this audit did NOT enumerate, on the
 * theory that a service whose contract is "a priced option or a clear
 * refusal" should never depend on having found every overflow path by
 * inspection. Concretely, it is what would also catch, for example, the
 * Kamrad-Ritchken transition probabilities (p_u/p_d in price_option_double)
 * going outside [0,1] for a rate large relative to volatility -- a genuine,
 * pre-existing numerical-stability limitation of that scheme, present even
 * at inputs well inside every bound above, that is out of this fix's scope
 * to correct but not out of scope to REFUSE rather than silently return.
 * Cheap relative to the O(steps^2) tree computation already done, so there
 * is no real cost argument against always running it.
 */
[[nodiscard]] auto require_response_finite(
    std::initializer_list<std::pair<double, std::string_view>> fields,
    std::string_view rpc_name) -> Status {
    for (const auto& [v, name] : fields) {
        if (!std::isfinite(v)) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          std::string(rpc_name) + " produced a non-finite " + std::string(name) +
                              " (" + std::to_string(v) +
                              ") from an input combination this calculator cannot represent "
                              "accurately -- refusing rather than returning a confidently wrong "
                              "answer");
        }
    }
    return Status::OK;
}

/**
 * Refuses a repeated-double field's element count above 100,000 -- the same
 * "period ceiling" shape check_period_count_ceiling already applies to an
 * int32 period count, applied here to a caller-controlled array length.
 * SimulateMarginAccount's daily_prices (a mark-to-market path,
 * O(len(daily_prices)) in simulate_futures_margin_account) and
 * ComputePortfolioStats' portfolio_returns/market_returns (O(n log n) for
 * the historical VaR sort) both had no ceiling of any kind beyond
 * quota::cost_margin_simulation/cost_portfolio_stats's proportional pricing
 * -- which bounds what a REPEATED call costs, not what a SINGLE absurdly
 * long request costs to execute. 100,000 entries is already an absurd
 * figure for anything this calculator prices (270+ years of daily data).
 */
[[nodiscard]] auto check_series_length_ceiling(int len, std::string_view field) -> Status {
    if (len > 100'000) {
        return Status(grpc::StatusCode::INVALID_ARGUMENT,
                      std::string(field) +
                          " exceeds 100,000 entries; no real series this calculator prices "
                          "needs more than that");
    }
    return Status::OK;
}

/**
 * Refuses a portfolio asset count above 1,000. OptimizePortfolio and
 * ComputeRiskContributions both solve against a size*size covariance matrix
 * (LU decomposition / a marginal-risk matrix-vector product) -- O(size^3)
 * and O(size^2) work respectively -- and read_covariance's own length checks
 * bound the request's SHAPE (size matches the accompanying vector, the flat
 * array holds exactly size*size entries) but not its MAGNITUDE. A
 * caller-chosen size in the low thousands is already enough to make a
 * single request's LU decomposition run for an unreasonable time regardless
 * of how the quota system prices it -- the same DoS shape
 * check_period_count_ceiling closes for period counts, applied here to a
 * covariance-solve dimension. 1,000 assets is already an enormous portfolio
 * (the S&P 500 is 500 names).
 */
[[nodiscard]] auto check_portfolio_size_ceiling(int size, std::string_view field) -> Status {
    if (size > 1'000) {
        return Status(grpc::StatusCode::INVALID_ARGUMENT,
                      std::string(field) +
                          " exceeds 1,000 assets; the covariance solve is O(size^3) and no "
                          "real portfolio needs more than that");
    }
    return Status::OK;
}

/**
 * Prices ComputeRefinance from the UNVALIDATED request, because CHARGE runs
 * before the validation below (a refused call must not spend an
 * unauthenticated caller's allowance on the work it never does).
 * `new_term_years` and `payments_per_year` are both still caller-controlled
 * at this point, and their product is exactly the loop bound
 * calculate_refinance_metrics walks -- computed here in int64 and clamped
 * before narrowing back to int, because that product can overflow int32
 * (100 * 20,000,000 already does).
 */
[[nodiscard]] auto refinance_charge_months(int current_remaining_months, int new_term_years,
                                           int payments_per_year) noexcept -> int {
    constexpr std::int64_t kCeil = 1'000'000'000;
    const std::int64_t ppy64 = std::clamp<std::int64_t>(payments_per_year, 0, kCeil);
    const std::int64_t years64 = std::clamp<std::int64_t>(new_term_years, 0, kCeil);
    const std::int64_t product = std::clamp<std::int64_t>(ppy64 * years64, 0, kCeil);
    const std::int64_t remaining64 = std::clamp<std::int64_t>(current_remaining_months, 0, kCeil);
    return static_cast<int>(std::max(remaining64, product));
}

/**
 * Prices ComputeHomeNpv the same unvalidated-input-safe way as
 * refinance_charge_months above: holding_period_years is still
 * caller-controlled when CHARGE runs, and holding_period_years*12 can
 * overflow int32 (INT32_MAX*12 does) -- undefined behaviour for a value that
 * gets refused two lines later regardless. Computed in int64 and clamped.
 */
[[nodiscard]] auto home_npv_charge_months(int holding_period_years) noexcept -> int {
    constexpr std::int64_t kCeil = 1'000'000'000;
    const std::int64_t years64 = std::clamp<std::int64_t>(holding_period_years, 0, kCeil);
    const std::int64_t months64 = std::clamp<std::int64_t>(years64 * 12, 0, kCeil);
    return static_cast<int>(months64);
}

/**
 * Charges this call against the caller's quota, or refuses it.
 *
 * A macro because it must RETURN from the RPC on refusal -- the whole point is
 * that the work below never runs. Placed before any computation and after the
 * null check, so a refused call costs the server a hash lookup rather than a
 * Monte Carlo.
 *
 * `cost` is priced from the request's own arguments, which is why this sits at
 * the call site instead of in a gRPC interceptor: an interceptor sees the
 * method name and the metadata, but not `paths`, `steps` or `term_months`, so
 * it could only ever charge every RPC the same.
 */
/**
 * The admission guard every finance RPC runs first: authenticate, then charge.
 *
 * The order is not arbitrary. Authentication is the cheaper check and the more
 * decisive one, and it produces the identity that quota then meters against --
 * charging first would spend an unauthenticated caller's allowance on a request
 * that was going to be refused anyway, and would meter a verified partner
 * against the shared anonymous bucket.
 *
 * It stays a macro, and stays at the top of each RPC, for the reason quota.cppm
 * documents: a synchronous C++ server interceptor cannot return a Status to
 * reject a call, and cannot see the deserialized request, so it could not price
 * the work. One visible line per RPC is debuggable and greppable; an RPC that
 * forgets it is a visible omission rather than an invisible hole.
 */
#define CHARGE(method_name, cost)                                                \
    ::options_calculator::auth::Identity _id;                                    \
    do {                                                                         \
        if (auto _a = ::options_calculator::auth::KeyRegistry::instance()         \
                          .authenticate(*context, "finance", (method_name), _id); \
            !_a.ok()) {                                                          \
            return _a;                                                           \
        }                                                                        \
        ::options_calculator::quota::TierLimits _lim{_id.requests_per_minute,     \
                                                    _id.compute_units_per_hour}; \
        if (auto _q = ::options_calculator::quota::QuotaEnforcer::instance()      \
                          .admit_identity(_id.id, _id.tier, (method_name),        \
                                          (cost),                                 \
                                          _id.has_limits ? &_lim : nullptr);      \
            !_q.ok()) {                                                          \
            return _q;                                                           \
        }                                                                        \
    } while (false)

// ---------------------------------------------------------------------------
// Shared request-lifecycle graph for the option-pricing RPCs
// ---------------------------------------------------------------------------
//
// SCOPING DECISION: only PriceOptionTree, PriceBlackScholes,
// PriceOptionMonteCarlo and ComputeProbabilityTree route through an SGEE
// graph. The other ~46 RPCs in this file do NOT, and that is deliberate, not
// an oversight -- the reasoning is recorded here because the next person to
// touch this file will otherwise wonder why the coverage is partial.
//
// Finance's ~50 RPCs are single-shot, single-entity calls: read fields ->
// run a handful of bespoke guards -> call one sensen function -> set the
// response. There is no genuine multi-stage DATA DEPENDENCY between steps
// the way calculator_service.cpp's OptionsWorkflow has (ComputeExpiryCurve's
// output feeds ComputeMatrix, which feeds ComputeGreeks, over a price grid of
// up to thousands of points) -- nothing here batches, retries, or branches
// on a predicate; every RPC is CHARGE -> guards -> one call -> respond, and
// C++'s own early-return already expresses "run these checks in order, stop
// at the first failure" without help from a graph engine. Wrapping all fifty
// RPCs' already-bespoke, already-well-documented guard chains in a fixed
// four-stage SGEE graph would not describe anything the C++ doesn't already
// describes: every RPC would still write the exact same validation code it
// writes today, just inside a lambda instead of the method body, run through
// an Interpreter that adds an EngineContext, an entities vector and a
// postcondition check for a single entity that never branches. That is
// exactly the "ceremony with no benefit" this task's own brief warns against
// -- so the other ~46 RPCs are left as plain sequential C++, unchanged.
//
// The four option-pricing RPCs are different, and are the closest thing this
// file has to a genuinely reusable, graph-shaped shape: all four share the
// SAME four-stage skeleton (validate the numeric fields via the SAME shared
// guard functions -- check_option_field_magnitude, check_tree_exponent_safe,
// check_double_magnitude, require_finite -- dispatch to a sensen pricer,
// check the response came back finite via require_response_finite /
// require_all_finite, and set the response), and unlike the rest of the
// file, this ladder is exactly the guard/defense-in-depth pattern the task
// brief calls out as worth making explicit. Routing these four through one
// shared graph means the four-stage shape is enforced by the graph's own
// structure (a stage cannot be silently skipped) rather than by every RPC
// independently remembering to run all four steps in order.
//
// Behaviour is IDENTICAL to before this refactor: every guard function
// below is called with the EXACT SAME arguments, in the EXACT SAME order, as
// the pre-refactor code -- the only change is that the guard SEQUENCE for
// each RPC now lives inside a `validate` closure that the graph's "Validate"
// node runs, instead of running directly in the RPC method body. The status
// code and message a caller sees for any given bad request is unchanged.

using sgee::ExecutionResult;

/**
 * Per-call state for one run through the shared FinanceRequestLifecycle
 * graph.
 *
 * Generic across all four option-pricing RPCs on purpose: each RPC has its
 * own concrete request/response proto type, so this graph cannot be
 * templated on one concrete Ctx the way calculator_service.cpp's
 * OptionsWorkflow is (its one concrete ComputeContext holds the one concrete
 * StrategyRequest/Response). Here each of the four stages is a closure the
 * calling RPC method supplies -- capturing its own request/response/result
 * variables by reference -- so the graph supplies only the CONTROL FLOW
 * (validate -> dispatch -> check response -> respond, short-circuiting on
 * the first failure) that is genuinely identical across all four RPCs; the
 * RPC-specific guard code, dispatch call and response fields stay exactly
 * where they always were; only their execution is now graph-driven.
 */
struct FinanceLifecycleState {
    std::function<Status()> validate;
    std::function<Status()> dispatch;
    std::function<Status()> check_response;
    std::function<void()> respond;
    // Mirrors ComputeContext::status in calculator_service.cpp: an action
    // that fails sets this AND returns std::unexpected so the entity halts
    // rather than silently walking on to the next stage with garbage state.
    Status status = Status::OK;
};
using FinanceEntity = std::shared_ptr<FinanceLifecycleState>;
using FinanceActionRegistry = sgee::runtime::ActionRegistry<FinanceEntity>;

[[nodiscard]] auto finance_action_validate(FinanceEntity& ctx) -> ExecutionResult<> {
    if (auto s = ctx->validate(); !s.ok()) {
        ctx->status = s;
        return std::unexpected(sgee::ExecutionError::ActionFailed);
    }
    return {};
}

[[nodiscard]] auto finance_action_dispatch(FinanceEntity& ctx) -> ExecutionResult<> {
    if (auto s = ctx->dispatch(); !s.ok()) {
        ctx->status = s;
        return std::unexpected(sgee::ExecutionError::ActionFailed);
    }
    return {};
}

[[nodiscard]] auto finance_action_check_response(FinanceEntity& ctx) -> ExecutionResult<> {
    if (auto s = ctx->check_response(); !s.ok()) {
        ctx->status = s;
        return std::unexpected(sgee::ExecutionError::ActionFailed);
    }
    return {};
}

[[nodiscard]] auto finance_action_respond(FinanceEntity& ctx) -> ExecutionResult<> {
    ctx->respond();
    return {};
}

/**
 * The four action names the FinanceRequestLifecycle graph defines, in graph
 * order. Kept as one array so production construction and any future test
 * construction (mirroring calculator_service.cpp's kAllActionNames /
 * RegisterCalculatorServiceForTest split, should a test ever need a
 * deliberately-incomplete registry) cannot drift into two different ideas of
 * "the full set".
 */
inline constexpr std::array<std::string_view, 4> kFinanceLifecycleActionNames{
    "Validate", "Dispatch", "CheckResponse", "Respond"};

// ---------------------------------------------------------------------------
// Service
// ---------------------------------------------------------------------------

class FinanceServiceImpl final : public sensen::finance::Finance::Service {
  private:
    // Only used by the four option-pricing RPCs -- see the
    // "Shared request-lifecycle graph" comment above this class.
    std::shared_ptr<FinanceActionRegistry> lifecycle_actions_;
    std::shared_ptr<const sgee::GraphBlueprint> lifecycle_graph_;
    std::uint16_t lifecycle_done_node_id_{0};

  public:
    FinanceServiceImpl() : lifecycle_actions_{std::make_shared<FinanceActionRegistry>()} {
        auto& log = logger::Logger::getInstance();

        auto graph_result = sgee::Builder<FinanceEntity>("FinanceRequestLifecycle")
            .Node("Validate")
                .Execute("Validate")
                .Next("Dispatch")
            .Node("Dispatch")
                .Execute("Dispatch")
                .Next("CheckResponse")
            .Node("CheckResponse")
                .Execute("CheckResponse")
                .Next("Respond")
            .Node("Respond")
                .Execute("Respond")
                .Next("Done")
            .Node("Done")
                .IsTerminal()
            .Build();

        if (!graph_result) {
            log.error("Failed to build Finance SGEE graph: {}", graph_result.error());
            return;
        }
        lifecycle_graph_ = graph_result.value();
        lifecycle_done_node_id_ = lifecycle_graph_->GetNodeId("Done");

        /*
         * Bind each action by the ID the builder assigned, NEVER by name --
         * see calculator_service.cpp's identical comment at its own
         * "Action registration by ID" block for the full explanation.
         * ActionRegistry::Register hashes the name while Builder::Execute
         * assigns sequential IDs from its own table; the two numbering
         * schemes never agree, so registering by name compiles and runs but
         * the action silently never executes. GetActionId() is the only
         * thing that bridges them.
         */
        const std::array<std::pair<std::string_view, FinanceActionRegistry::ActionFunction>, 4>
            bindings{{
                {"Validate", finance_action_validate},
                {"Dispatch", finance_action_dispatch},
                {"CheckResponse", finance_action_check_response},
                {"Respond", finance_action_respond},
            }};

        for (const auto& [name, fn] : bindings) {
            const auto id = lifecycle_graph_->GetActionId(name);
            if (!id) {
                log.error("Finance action '{}' is not present in the graph; refusing to start",
                          name);
                lifecycle_graph_.reset();
                return;
            }
            lifecycle_actions_->RegisterById(*id, fn);
        }

        log.info(
            "Finance SGEE graph initialized: {} registered actions for the option-pricing "
            "request lifecycle (PriceOptionTree, PriceBlackScholes, PriceOptionMonteCarlo, "
            "ComputeProbabilityTree)",
            bindings.size());
    }

    ~FinanceServiceImpl() override = default;

    // -- Time value of money -------------------------------------------------

    auto ComputePayment(ServerContext* context, const sensen::finance::PaymentRequest* request,
                        sensen::finance::DecimalResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputePayment", quota::cost_default());
        if (auto s = check_periods(request->periods()); !s.ok()) return s;
        if (auto s = check_period_count_ceiling(request->periods(), "periods"); !s.ok()) return s;
        REQUIRE_DECIMAL(rate, request->rate(), "rate");
        REQUIRE_DECIMAL(pv, request->present_value(), "present_value");
        READ_DECIMAL(fv, request->future_value(), "future_value");
        // rate is a PER-PERIOD rate here (finance.proto's own field comment),
        // so pmt()'s BigDecimal pow(periods) is checked directly against it --
        // see check_compound_growth_safe_periods's own comment for the
        // reproduction (ComputeAmortization{annual_rate=1000000,
        // term_months=1200}, the identical missing-guard shape on a sibling
        // RPC that already had it retrofitted).
        if (auto s = check_compound_growth_safe_periods(rate.to_double(), request->periods(), "rate");
            !s.ok()) {
            return s;
        }
        const auto r = sensen::pmt(rate, request->periods(), pv, fv, timing_of(request->timing()));
        response->set_value(r.to_string());
        return Status::OK;
    }

    auto ComputePresentValue(ServerContext* context, const sensen::finance::PresentValueRequest* request,
                             sensen::finance::DecimalResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputePresentValue", quota::cost_default());
        if (auto s = check_periods(request->periods()); !s.ok()) return s;
        if (auto s = check_period_count_ceiling(request->periods(), "periods"); !s.ok()) return s;
        REQUIRE_DECIMAL(rate, request->rate(), "rate");
        REQUIRE_DECIMAL(pmt_v, request->payment(), "payment");
        READ_DECIMAL(fv, request->future_value(), "future_value");
        if (auto s = check_compound_growth_safe_periods(rate.to_double(), request->periods(), "rate");
            !s.ok()) {
            return s;
        }
        const auto r = sensen::pv(rate, request->periods(), pmt_v, fv, timing_of(request->timing()));
        response->set_value(r.to_string());
        return Status::OK;
    }

    auto ComputeFutureValue(ServerContext* context, const sensen::finance::FutureValueRequest* request,
                            sensen::finance::DecimalResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputeFutureValue", quota::cost_default());
        if (auto s = check_periods(request->periods()); !s.ok()) return s;
        if (auto s = check_period_count_ceiling(request->periods(), "periods"); !s.ok()) return s;
        REQUIRE_DECIMAL(rate, request->rate(), "rate");
        REQUIRE_DECIMAL(pmt_v, request->payment(), "payment");
        READ_DECIMAL(pv_v, request->present_value(), "present_value");
        if (auto s = check_compound_growth_safe_periods(rate.to_double(), request->periods(), "rate");
            !s.ok()) {
            return s;
        }
        const auto r = sensen::fv(rate, request->periods(), pmt_v, pv_v, timing_of(request->timing()));
        response->set_value(r.to_string());
        return Status::OK;
    }

    auto ComputeFutureValueDetailed(ServerContext* context,
                                    const sensen::finance::FutureValueDetailedRequest* request,
                                    sensen::finance::FutureValueDetailedResponse* response)
        -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputeFutureValueDetailed", quota::cost_default());
        // Refused rather than defaulted. A compounding frequency changes the
        // answer materially, and picking one the caller did not state would be
        // this service inventing an assumption on their behalf.
        if (request->compound_frequency() <= 0) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "compound_frequency must be positive (12 monthly, 4 quarterly, 1 annual)");
        }
        // compound_frequency had no CEILING either -- calculate_future_value_detailed
        // (financial.cppm) multiplies it against years to form total_periods
        // (an int), the same payments_per_year shape check_payments_per_year
        // already bounds elsewhere in this file. No real compounding
        // schedule is finer than daily.
        if (request->compound_frequency() > 366) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "compound_frequency exceeds 366 (daily); no real compounding schedule "
                          "is finer than that");
        }
        if (request->years() < 0) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT, "years cannot be negative");
        }
        // years also had no ceiling: `years * compound_frequency`
        // (calculate_future_value_detailed's total_periods, an int) can
        // overflow int32 outright when both are large -- undefined
        // behaviour regardless of whether the request is refused for its
        // magnitude two lines later -- and years alone feeds
        // annual_inflation_rate's own BigDecimal::pow(years). 100 years is
        // already longer than any real projection horizon.
        if (request->years() > 100) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "years must be at most 100; no real projection horizon runs longer "
                          "than a lifetime");
        }
        // annual_rate/annual_contribution/current_principal used PLAIN
        // REQUIRE_DECIMAL here -- neither the magnitude bound
        // (check_decimal_string_magnitude, via REQUIRE_DECIMAL_SAFE) nor the
        // compound-growth bound (check_compound_growth_safe) the six
        // home-finance RPCs and ComputeAmortization already carry for the
        // IDENTICAL rate/compound_frequency, pow(total_periods) shape
        // calculate_future_value_detailed's own fv() call performs. This is
        // the same missing-guard pattern ComputeAmortization's own comment
        // documents, never retrofitted onto this RPC either. Reproduced
        // directly: {annual_rate=1000000, years=100, compound_frequency=12,
        // annual_contribution=5000, current_principal=10000} returned
        // Status::OK with total_interest_earned silently wrapped to a
        // fabricated figure rather than refused.
        REQUIRE_DECIMAL_SAFE(rate, request->annual_rate(), "annual_rate");
        REQUIRE_DECIMAL_SAFE(contrib, request->annual_contribution(), "annual_contribution");
        REQUIRE_DECIMAL_SAFE(principal, request->current_principal(), "current_principal");
        READ_DECIMAL_SAFE(inflation, request->annual_inflation_rate(), "annual_inflation_rate");
        if (auto s = check_compound_growth_safe(rate.to_double(), request->compound_frequency(),
                                                request->years() * request->compound_frequency(),
                                                "annual_rate");
            !s.ok()) {
            return s;
        }
        // annual_inflation_rate also raises (1+rate) to `years` via
        // BigDecimal::pow (the inflation-adjustment leg), with no
        // compound-growth bound of its own -- check_compound_growth_safe_periods
        // is the exact shape this plain annual compounding needs (rate per
        // period == the annual rate itself, periods == years).
        if (auto s = check_compound_growth_safe_periods(inflation.to_double(), request->years(),
                                                        "annual_inflation_rate");
            !s.ok()) {
            return s;
        }

        const auto s = sensen::calculate_future_value_detailed(
            rate, request->years(), contrib, principal, inflation, request->compound_frequency());
        response->set_nominal_fv(s.nominal_fv.to_string());
        response->set_inflation_adjusted_fv(s.inflation_adjusted_fv.to_string());
        response->set_total_contributions(s.total_contributions.to_string());
        response->set_total_interest_earned(s.total_interest_earned.to_string());
        return Status::OK;
    }

    auto ComputeInterestPayment(ServerContext* context, const sensen::finance::PeriodPaymentRequest* request,
                                sensen::finance::DecimalResponse* response) -> Status override {
        CHARGE("ComputeInterestPayment", quota::cost_default());
        return period_payment(request, response, /*interest=*/true);
    }

    auto ComputePrincipalPayment(ServerContext* context, const sensen::finance::PeriodPaymentRequest* request,
                                 sensen::finance::DecimalResponse* response) -> Status override {
        CHARGE("ComputePrincipalPayment", quota::cost_default());
        return period_payment(request, response, /*interest=*/false);
    }

    auto ComputeRate(ServerContext* context, const sensen::finance::RateRequest* request,
                     sensen::finance::DecimalResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputeRate", quota::cost_default());
        if (auto s = check_periods(request->periods()); !s.ok()) return s;
        REQUIRE_DECIMAL(pmt_v, request->payment(), "payment");
        REQUIRE_DECIMAL(pv_v, request->present_value(), "present_value");
        READ_DECIMAL(fv_v, request->future_value(), "future_value");
        READ_DECIMAL(guess, request->guess(), "guess");
        // rate_fn solves iteratively in double, unlike the closed-form annuity
        // functions above -- so the inputs narrow here rather than pretending
        // to a precision the solver does not carry.
        //
        // An unset guess is 0, which is a legitimate rate but a poor seed; the
        // engine's own default is better, so an unstated guess uses it.
        const auto r = guess.is_zero()
            ? sensen::rate_fn(request->periods(), pmt_v.to_double(), pv_v.to_double(),
                              fv_v.to_double(), timing_of(request->timing()))
            : sensen::rate_fn(request->periods(), pmt_v.to_double(), pv_v.to_double(),
                              fv_v.to_double(), timing_of(request->timing()), guess.to_double());
        if (!r) return fail(r);
        response->set_value(BigDecimal(*r).to_string());
        return Status::OK;
    }

    auto ComputePeriods(ServerContext* context, const sensen::finance::PeriodsRequest* request,
                        sensen::finance::DecimalResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputePeriods", quota::cost_default());
        REQUIRE_DECIMAL(rate, request->rate(), "rate");
        REQUIRE_DECIMAL(pmt_v, request->payment(), "payment");
        REQUIRE_DECIMAL(pv_v, request->present_value(), "present_value");
        READ_DECIMAL(fv_v, request->future_value(), "future_value");
        // nper_fn is a double-domain closed form, same as rate_fn above.
        const auto r = sensen::nper_fn(rate.to_double(), pmt_v.to_double(), pv_v.to_double(),
                                       fv_v.to_double(), timing_of(request->timing()));
        if (!r) return fail(r);
        response->set_value(BigDecimal(*r).to_string());
        return Status::OK;
    }

    auto ConvertInterestRate(ServerContext* context, const sensen::finance::RateConversionRequest* request,
                             sensen::finance::DoubleResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ConvertInterestRate", quota::cost_default());
        if (request->periods_per_year() <= 0.0) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT, "periods_per_year must be positive");
        }
        const double v =
            (request->direction() == sensen::finance::RateConversionRequest::EFFECTIVE_TO_NOMINAL)
                ? sensen::nominal(request->rate(), request->periods_per_year())
                : sensen::effect(request->rate(), request->periods_per_year());
        response->set_value(v);
        return Status::OK;
    }

    auto ComputeFisherRate(ServerContext* context, const sensen::finance::FisherRequest* request,
                           sensen::finance::DoubleResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputeFisherRate", quota::cost_default());
        const double v =
            (request->direction() == sensen::finance::FisherRequest::REAL_TO_NOMINAL)
                ? sensen::fisher_nominal_rate(request->rate(), request->inflation_rate())
                : sensen::fisher_real_rate(request->rate(), request->inflation_rate());
        response->set_value(v);
        return Status::OK;
    }

    // -- Mortgages -----------------------------------------------------------

    auto ComputeAmortization(ServerContext* context, const sensen::finance::AmortizationRequest* request,
                             sensen::finance::AmortizationResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputeAmortization", quota::cost_amortization(request->term_months()));
        if (auto s = check_term(request->term_months()); !s.ok()) return s;
        // loan_amount/annual_rate used PLAIN REQUIRE_DECIMAL here -- neither
        // the magnitude bound (check_decimal_string_magnitude, via
        // REQUIRE_DECIMAL_SAFE) nor the compound-growth bound
        // (check_compound_growth_safe) the six home-finance RPCs and
        // ComputeMortgageRecast already carry for the IDENTICAL
        // monthly_rate=annual_rate/12, pow(term_months) shape
        // calculate_mortgage_amortization's own pmt() call performs. This is
        // the SAME missing-guard pattern ComputeMortgageRecast's own comment
        // documents ("annual_rate=1000000 returned a new_monthly_payment
        // with no relation to the input"), just never retrofitted onto this
        // RPC. Reproduced directly on THIS RPC: {loan_amount=300000,
        // annual_rate=1000000, term_months=1200} returns Status::OK with
        // total_interest_paid="29999999999999.999999999880000000" -- $30
        // trillion of interest on a loan under $300,000, BigDecimal's exact
        // __int128 range silently wrapped rather than refused.
        REQUIRE_DECIMAL_SAFE(loan, request->loan_amount(), "loan_amount");
        REQUIRE_DECIMAL_SAFE(rate, request->annual_rate(), "annual_rate");
        READ_DECIMAL(extra, request->monthly_overpayment(), "monthly_overpayment");
        READ_DECIMAL(pmi, request->pmi_annual_rate(), "pmi_annual_rate");
        READ_DECIMAL(home, request->original_home_value(), "original_home_value");
        if (auto s = check_compound_growth_safe(rate.to_double(), 12, request->term_months(),
                                                "annual_rate");
            !s.ok()) {
            return s;
        }

        auto [schedule, summary] = sensen::calculate_mortgage_amortization(
            loan, rate, request->term_months(), extra, pmi, home);

        for (const auto& row : schedule) {
            auto& r = *response->add_schedule();
            r.set_period(row.period);
            r.set_start_balance(row.start_balance.to_string());
            r.set_scheduled_payment(row.scheduled_payment.to_string());
            r.set_extra_payment(row.extra_payment.to_string());
            r.set_interest_paid(row.interest_paid.to_string());
            r.set_principal_paid(row.principal_paid.to_string());
            r.set_pmi_paid(row.pmi_paid.to_string());
            r.set_end_balance(row.end_balance.to_string());
        }
        auto& s = *response->mutable_summary();
        s.set_total_principal_paid(summary.total_principal_paid.to_string());
        s.set_total_interest_paid(summary.total_interest_paid.to_string());
        s.set_total_pmi_paid(summary.total_pmi_paid.to_string());
        s.set_total_payments_paid(summary.total_payments_paid.to_string());
        s.set_actual_term_months(summary.actual_term_months);
        return Status::OK;
    }

    auto ComputeDetailedAmortization(ServerContext* context,
                                     const sensen::finance::DetailedAmortizationRequest* request,
                                     sensen::finance::DetailedAmortizationResponse* response)
        -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputeDetailedAmortization", quota::cost_amortization(request->term_months()));
        if (auto s = check_term(request->term_months()); !s.ok()) return s;
        // Same missing-guard shape as ComputeAmortization just above --
        // see that RPC's comment for the reproduction.
        REQUIRE_DECIMAL_SAFE(loan, request->loan_amount(), "loan_amount");
        REQUIRE_DECIMAL_SAFE(rate, request->annual_rate(), "annual_rate");
        READ_DECIMAL(extra, request->monthly_overpayment(), "monthly_overpayment");
        READ_DECIMAL(pmi, request->pmi_annual_rate(), "pmi_annual_rate");
        READ_DECIMAL(home, request->original_home_value(), "original_home_value");
        READ_DECIMAL(tax, request->annual_tax_rate(), "annual_tax_rate");
        if (auto s = check_compound_growth_safe(rate.to_double(), 12, request->term_months(),
                                                "annual_rate");
            !s.ok()) {
            return s;
        }

        auto [schedule, summary] = sensen::calculate_detailed_mortgage_amortization(
            loan, rate, request->term_months(), extra, pmi, home, tax);

        for (const auto& row : schedule) {
            auto& r = *response->add_schedule();
            r.set_period(row.period);
            r.set_start_balance(row.start_balance.to_string());
            r.set_scheduled_payment(row.scheduled_payment.to_string());
            r.set_extra_payment(row.extra_payment.to_string());
            r.set_interest_paid(row.interest_paid.to_string());
            r.set_principal_paid(row.principal_paid.to_string());
            r.set_pmi_paid(row.pmi_paid.to_string());
            r.set_tax_savings(row.tax_savings.to_string());
            r.set_end_balance(row.end_balance.to_string());
        }
        auto& s = *response->mutable_summary();
        s.set_total_principal_paid(summary.total_principal_paid.to_string());
        s.set_total_interest_paid(summary.total_interest_paid.to_string());
        s.set_total_pmi_paid(summary.total_pmi_paid.to_string());
        s.set_total_payments_paid(summary.total_payments_paid.to_string());
        s.set_total_tax_savings(summary.total_tax_savings.to_string());
        s.set_actual_term_months(summary.actual_term_months);
        return Status::OK;
    }

    auto ComputeAmortizationBatch(ServerContext* context,
                                  const sensen::finance::AmortizationBatchRequest* request,
                                  sensen::finance::AmortizationBatchResponse* response)
        -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputeAmortizationBatch",
               quota::cost_amortization_batch(request->loan_amounts_size(),
                                              request->term_months_size() > 0
                                                  ? request->term_months(0) : 0));
        const int n = request->loan_amounts_size();
        if (n == 0) return Status::OK;

        // A ragged batch is refused, not truncated to the shortest column.
        // Silently dropping loans would return a shorter list that looks like a
        // complete answer.
        const auto same = [n](int m, const char* which) -> Status {
            if (m != n) {
                return Status(grpc::StatusCode::INVALID_ARGUMENT,
                              std::string(which) + " has " + std::to_string(m) +
                                  " entries but loan_amounts has " + std::to_string(n) +
                                  "; every field must describe the same loans");
            }
            return Status::OK;
        };
        if (auto s = same(request->annual_rates_size(), "annual_rates"); !s.ok()) return s;
        if (auto s = same(request->term_months_size(), "term_months"); !s.ok()) return s;
        if (auto s = same(request->extra_payments_size(), "extra_payments"); !s.ok()) return s;
        if (auto s = same(request->pmi_rates_size(), "pmi_rates"); !s.ok()) return s;
        if (auto s = same(request->home_values_size(), "home_values"); !s.ok()) return s;

        for (int i = 0; i < n; ++i) {
            if (auto s = check_term(request->term_months(i)); !s.ok()) return s;
        }
        // loan_amounts/annual_rates/extra_payments/pmi_rates/home_values are
        // raw wire doubles (unlike the single-loan ComputeAmortization above,
        // this batch RPC's proto message never grew a decimal-string field),
        // and calculate_mortgage_batch_cpu (financial.cppm) constructs a
        // BigDecimal DIRECTLY from each one: `BigDecimal(loan_amounts[i])`.
        // That constructor is `value_ = static_cast<__int128_t>(std::round(
        // val * 1e18))` (bigdecimal.cppm) -- casting a NaN double to
        // __int128_t is UNDEFINED BEHAVIOUR per [conv.fpint], not merely a
        // NaN that propagates predictably. Reproduced directly: a batch
        // entry of loan_amounts[0]=NaN is accepted today (Status::OK, one
        // summary returned) rather than refused; what garbage value the UB
        // actually produces is compiler/build-dependent by construction, so
        // it cannot be relied on to "look wrong" the way ComputeCumulative's
        // NaN reproduction (see require_finite there) happened to.
        //
        // require_finite alone is NOT enough, either: a FINITE loan_amount
        // as "small" as 1e30 already overflows __int128 once
        // BigDecimal(double) scales it by 1e18 (~1e48, far past __int128's
        // ~1.7e38 range) -- casting a value that cannot be represented in
        // the destination type is undefined behaviour per [conv.fpint], not
        // a defined wraparound. check_double_magnitude closes exactly this
        // gap (the raw-double sibling of check_decimal_string_magnitude,
        // which the STRING-money RPCs elsewhere in this file already carry).
        for (int i = 0; i < n; ++i) {
            if (auto s = require_finite(request->loan_amounts(i), "loan_amounts"); !s.ok()) {
                return s;
            }
            if (auto s = check_double_magnitude(request->loan_amounts(i), "loan_amounts");
                !s.ok()) {
                return s;
            }
            if (auto s = require_finite(request->annual_rates(i), "annual_rates"); !s.ok()) {
                return s;
            }
            if (auto s = check_double_magnitude(request->annual_rates(i), "annual_rates");
                !s.ok()) {
                return s;
            }
            if (auto s = require_finite(request->extra_payments(i), "extra_payments"); !s.ok()) {
                return s;
            }
            if (auto s = check_double_magnitude(request->extra_payments(i), "extra_payments");
                !s.ok()) {
                return s;
            }
            if (auto s = require_finite(request->pmi_rates(i), "pmi_rates"); !s.ok()) return s;
            if (auto s = check_double_magnitude(request->pmi_rates(i), "pmi_rates"); !s.ok()) {
                return s;
            }
            if (auto s = require_finite(request->home_values(i), "home_values"); !s.ok()) return s;
            if (auto s = check_double_magnitude(request->home_values(i), "home_values");
                !s.ok()) {
                return s;
            }
        }

        const std::vector<double> loans(request->loan_amounts().begin(), request->loan_amounts().end());
        const std::vector<double> rates(request->annual_rates().begin(), request->annual_rates().end());
        const std::vector<int> terms(request->term_months().begin(), request->term_months().end());
        const std::vector<double> extras(request->extra_payments().begin(), request->extra_payments().end());
        const std::vector<double> pmis(request->pmi_rates().begin(), request->pmi_rates().end());
        const std::vector<double> homes(request->home_values().begin(), request->home_values().end());

        const auto summaries =
            sensen::calculate_mortgage_batch_cpu(loans, rates, terms, extras, pmis, homes);
        for (const auto& summary : summaries) {
            auto& s = *response->add_summaries();
            s.set_total_principal_paid(summary.total_principal_paid.to_string());
            s.set_total_interest_paid(summary.total_interest_paid.to_string());
            s.set_total_pmi_paid(summary.total_pmi_paid.to_string());
            s.set_total_payments_paid(summary.total_payments_paid.to_string());
            s.set_actual_term_months(summary.actual_term_months);
        }
        return Status::OK;
    }

    auto ComputeHeloc(ServerContext* context, const sensen::finance::HelocRequest* request,
                      sensen::finance::HelocResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputeHeloc", quota::cost_default());
        // This RPC predates the six home-finance RPCs below and originally
        // carried only a >0 floor on both caller-controlled integers. That is
        // the same shape the adversarial pass found exploitable on
        // ComputeRefinance, and it is worse here in one respect:
        // calculate_heloc_metrics computes
        //     int total_repayment_periods = repayment_term_years * payments_per_year;
        // (financial.cppm) -- an int32 product of two UNBOUNDED caller inputs,
        // which is signed overflow, i.e. UB, before the value is ever used.
        // Unlike refinance this is not a wall-clock DoS (BigDecimal::pow uses
        // exponentiation by squaring, so ~31 multiplies rather than a walk), so
        // the damage is UB plus a confidently wrong payment figure -- which is
        // the worse outcome for a caller who cannot tell.
        if (auto s = check_payments_per_year(request->payments_per_year()); !s.ok()) return s;
        if (request->repayment_term_years() <= 0 || request->repayment_term_years() > 100) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "repayment_term_years must be positive and at most 100; no real "
                          "HELOC repayment period runs longer than a lifetime");
        }
        REQUIRE_DECIMAL_SAFE(home, request->home_value(), "home_value");
        REQUIRE_DECIMAL_SAFE(balance, request->current_mortgage_balance(), "current_mortgage_balance");
        REQUIRE_DECIMAL_SAFE(ltv, request->max_ltv_rate(), "max_ltv_rate");
        READ_DECIMAL_SAFE(drawn, request->drawn_amount(), "drawn_amount");
        REQUIRE_DECIMAL_SAFE(rate, request->annual_rate(), "annual_rate");
        // Both multiplicands are bounded now (<=366 and <=100), so the product
        // cannot overflow -- but a bounded product still overflows pmt()'s
        // BigDecimal pow() at an extreme rate, exactly as it did on recast.
        if (auto s = check_compound_growth_safe(rate.to_double(), request->payments_per_year(),
                                                request->repayment_term_years() *
                                                    request->payments_per_year(),
                                                "annual_rate");
            !s.ok()) {
            return s;
        }

        const auto s = sensen::calculate_heloc_metrics(home, balance, ltv, drawn, rate,
                                                       request->repayment_term_years(),
                                                       request->payments_per_year());
        response->set_available_equity(s.available_equity.to_string());
        response->set_draw_period_payment(s.draw_period_payment.to_string());
        response->set_repayment_period_payment(s.repayment_period_payment.to_string());
        return Status::OK;
    }

    auto ComputeRefinance(ServerContext* context, const sensen::finance::RefinanceRequest* request,
                         sensen::finance::RefinanceResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        // The engine walks max(current_remaining_months, new_term_years*ppy)
        // months building the cash-flow comparison (financial.cppm:1938) --
        // an amortization walk, twice over. Priced from the raw,
        // still-unvalidated request (see refinance_charge_months's own
        // comment for why that is deliberate and why the multiplication
        // cannot be done directly here).
        CHARGE("ComputeRefinance",
               quota::cost_amortization(refinance_charge_months(
                   request->current_remaining_months(), request->new_term_years(),
                   request->payments_per_year())));
        if (request->current_remaining_months() <= 0 || request->current_remaining_months() > 1200) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "current_remaining_months must be positive and at most 1200 (a hundred "
                          "years); no real loan runs that long");
        }
        if (request->new_term_years() <= 0 || request->new_term_years() > 100) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "new_term_years must be positive and at most 100");
        }
        if (auto s = check_payments_per_year(request->payments_per_year()); !s.ok()) return s;

        sensen::RefinanceClosingCostType cc_type{};
        switch (request->closing_cost_type()) {
            case sensen::finance::RefinanceRequest::PAID_IN_CASH:
                cc_type = sensen::RefinanceClosingCostType::PaidInCash;
                break;
            case sensen::finance::RefinanceRequest::ROLLED_INTO_LOAN:
                cc_type = sensen::RefinanceClosingCostType::RolledIntoLoan;
                break;
            default:
                return Status(grpc::StatusCode::INVALID_ARGUMENT, "unknown closing cost type");
        }

        REQUIRE_DECIMAL_SAFE(balance, request->current_loan_balance(), "current_loan_balance");
        REQUIRE_DECIMAL_SAFE(cur_pmt, request->current_monthly_payment(), "current_monthly_payment");
        REQUIRE_DECIMAL_SAFE(cur_rate, request->current_annual_rate(), "current_annual_rate");
        REQUIRE_DECIMAL_SAFE(prop_value, request->property_value(), "property_value");
        REQUIRE_DECIMAL_SAFE(new_rate, request->new_annual_rate(), "new_annual_rate");
        REQUIRE_DECIMAL_SAFE(closing, request->closing_costs(), "closing_costs");
        // cash_out_amount is documented "omit for a rate-and-term refinance"
        // (finance.proto), and current/new_pmi_monthly document 0 as "never
        // had PMI" -- all three are legitimately, meaningfully absent.
        // pmi_drop_off_ltv rides along with them: calculate_refinance_metrics
        // (financial.cppm:1913-1917) forces both *_pmi_drop_off_months to 0
        // whenever the matching *_pmi_monthly is zero, BEFORE the LTV
        // threshold this field feeds is ever consulted -- so an absent LTV
        // is inert precisely when there is no PMI to drop off, and only
        // matters once a caller has already opted in by stating a real PMI
        // amount.
        READ_DECIMAL_SAFE(cash_out, request->cash_out_amount(), "cash_out_amount");
        READ_DECIMAL_SAFE(cur_pmi, request->current_pmi_monthly(), "current_pmi_monthly");
        READ_DECIMAL_SAFE(new_pmi, request->new_pmi_monthly(), "new_pmi_monthly");
        READ_DECIMAL_SAFE(pmi_ltv, request->pmi_drop_off_ltv(), "pmi_drop_off_ltv");

        // current_annual_rate never reaches a BigDecimal pow() in
        // calculate_refinance_metrics (the old-loan leg is a plain month-by-
        // month loop, not a closed form), so only the floor matters here.
        // new_annual_rate DOES feed pmt()'s pow(), against new_term_months --
        // already bounded above by new_term_years and payments_per_year, but
        // their PRODUCT is the actual exponent, hence the joint check.
        if (auto s = check_rate_floor(cur_rate.to_double() / request->payments_per_year(),
                                      "current_annual_rate");
            !s.ok()) {
            return s;
        }
        if (auto s = check_compound_growth_safe(
                new_rate.to_double(), request->payments_per_year(),
                request->new_term_years() * request->payments_per_year(), "new_annual_rate");
            !s.ok()) {
            return s;
        }

        sensen::RefinanceInput input{};
        input.current_loan_balance = balance;
        input.current_monthly_payment = cur_pmt;
        input.current_annual_rate = cur_rate;
        input.current_remaining_months = request->current_remaining_months();
        input.property_value = prop_value;
        input.new_annual_rate = new_rate;
        input.new_term_years = request->new_term_years();
        input.closing_costs = closing;
        input.closing_cost_type = cc_type;
        input.cash_out_amount = cash_out;
        input.current_pmi_monthly = cur_pmi;
        input.new_pmi_monthly = new_pmi;
        input.pmi_drop_off_ltv = pmi_ltv;
        input.payments_per_year = request->payments_per_year();

        const auto s = sensen::calculate_refinance_metrics(input);
        response->set_new_loan_amount(s.new_loan_amount.to_string());
        response->set_new_monthly_payment(s.new_monthly_payment.to_string());
        response->set_monthly_savings_initial(s.monthly_savings_initial.to_string());
        response->set_current_loan_pmi_drop_off_months(s.current_loan_pmi_drop_off_months);
        response->set_new_loan_pmi_drop_off_months(s.new_loan_pmi_drop_off_months);
        response->set_payoff_date_shift_months(s.payoff_date_shift_months);
        response->set_simple_break_even_months(s.simple_break_even_months);
        response->set_cash_flow_break_even_months(s.cash_flow_break_even_months);
        response->set_equity_adjusted_break_even_months(s.equity_adjusted_break_even_months);
        // Double, not a decimal string: the engine accumulates this over the
        // whole month-by-month comparison loop in double
        // (financial.cppm:1932-1980, total_old_paid/total_new_paid are
        // double). An 18-place string here would claim digits the
        // computation never had.
        response->set_total_savings_over_life(s.total_savings_over_life.to_double());
        return Status::OK;
    }

    auto ComputePayoffTiming(ServerContext* context, const sensen::finance::PayoffTimingRequest* request,
                            sensen::finance::PayoffTimingResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputePayoffTiming", quota::cost_default());
        if (auto s = check_payments_per_year(request->payments_per_year()); !s.ok()) return s;
        REQUIRE_DECIMAL_SAFE(balance, request->current_loan_balance(), "current_loan_balance");
        REQUIRE_DECIMAL_SAFE(rate, request->annual_rate(), "annual_rate");
        REQUIRE_DECIMAL_SAFE(pmt_v, request->current_monthly_payment(), "current_monthly_payment");
        // extra_monthly_payment=0 is a legitimate ask ("what's my timeline
        // with no extra payment"), and the negative-value check just below
        // already treats 0 as the valid floor.
        READ_DECIMAL_SAFE(extra, request->extra_monthly_payment(), "extra_monthly_payment");
        if (extra.is_negative()) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "extra_monthly_payment cannot be negative");
        }
        // calculate_payoff_timing's own nper_fn detects "payment does not
        // cover interest" via a numerator/denominator sign mismatch -- but a
        // rate at or below -100% (1 + rate_per_period <= 0) does not trip
        // that check; it evaluates cleanly to log(0)/log(<=0), which IEEE
        // arithmetic can reduce to an in-range value rather than
        // NaN/Inf (measured: annual_rate=-1, payments_per_year=1 returns
        // original_months_remaining=0 -- "already paid off" -- for a loan
        // that is not). This floor is what the engine's own check misses.
        if (auto s = check_rate_floor(rate.to_double() / request->payments_per_year(),
                                      "annual_rate");
            !s.ok()) {
            return s;
        }

        // calculate_payoff_timing returns std::expected<PayoffTimingSummary,
        // std::string> (sensen commit 4d4b4cbd): a payment that does not
        // cover one period's interest can never amortize, and nper_fn fails
        // rather than the caller being told "0 months remaining". That
        // failure is mapped straight to a gRPC refusal below -- computing a
        // guess here would reintroduce exactly the defect the sensen fix
        // removed, through this RPC.
        const auto r = sensen::calculate_payoff_timing(balance, rate, pmt_v, extra,
                                                        request->payments_per_year());
        if (!r) return fail(r);
        response->set_original_months_remaining(r->original_months_remaining);
        response->set_new_months_remaining(r->new_months_remaining);
        response->set_months_saved(r->months_saved);
        response->set_total_interest_saved(r->total_interest_saved.to_string());
        return Status::OK;
    }

    auto ComputeMortgageRecast(ServerContext* context, const sensen::finance::MortgageRecastRequest* request,
                              sensen::finance::MortgageRecastResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputeMortgageRecast", quota::cost_default());
        if (request->remaining_months() <= 0 || request->remaining_months() > 1200) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "remaining_months must be positive and at most 1200 (a hundred years); "
                          "no real loan runs that long");
        }
        if (auto s = check_payments_per_year(request->payments_per_year()); !s.ok()) return s;
        REQUIRE_DECIMAL_SAFE(balance, request->current_loan_balance(), "current_loan_balance");
        REQUIRE_DECIMAL_SAFE(cur_pmt, request->current_monthly_payment(), "current_monthly_payment");
        // lump_sum_payment=0 is a legitimate ask ("what would recasting look
        // like with no extra paydown"), and the negative-value check just
        // below already treats 0 as the valid floor.
        READ_DECIMAL_SAFE(lump, request->lump_sum_payment(), "lump_sum_payment");
        REQUIRE_DECIMAL_SAFE(rate, request->annual_rate(), "annual_rate");
        if (lump.is_negative()) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "lump_sum_payment cannot be negative; a recast only pays the balance "
                          "down, and the engine would otherwise happily grow it");
        }
        // annual_rate feeds pmt()'s BigDecimal pow(remaining_months) directly
        // -- remaining_months is already capped at 1200, but an extreme rate
        // still overflows it (measured: annual_rate=1000000 returned a
        // new_monthly_payment with no relation to the input).
        if (auto s = check_compound_growth_safe(rate.to_double(), request->payments_per_year(),
                                                request->remaining_months(), "annual_rate");
            !s.ok()) {
            return s;
        }

        const auto s = sensen::calculate_mortgage_recast(balance, cur_pmt, lump, rate,
                                                          request->remaining_months(),
                                                          request->payments_per_year());
        response->set_new_monthly_payment(s.new_monthly_payment.to_string());
        response->set_monthly_savings(s.monthly_savings.to_string());
        return Status::OK;
    }

    // -- Cash flow -----------------------------------------------------------

    auto ComputeNpv(ServerContext* context, const sensen::finance::NpvRequest* request,
                    sensen::finance::DoubleResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputeNpv", quota::cost_cash_flow(request->values_size()));
        // npv_double (financial.cppm) is a PLAIN SUM -- unlike irr/xirr below,
        // it does not iterate, so it has no Newton-Raphson "never converges on
        // a NaN chase" self-protection. rate/values are raw wire doubles with
        // no check of any kind before this fix. Reproduced directly: rate=NaN
        // returns Status::OK with value=nan.
        if (auto s = require_finite(request->rate(), "rate"); !s.ok()) return s;
        if (auto s = require_all_finite(request->values(), "values"); !s.ok()) return s;
        const std::vector<double> values(request->values().begin(), request->values().end());
        response->set_value(sensen::npv_double(request->rate(), values));
        return Status::OK;
    }

    auto ComputeIrr(ServerContext* context, const sensen::finance::IrrRequest* request,
                    sensen::finance::DoubleResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputeIrr", quota::cost_cash_flow(request->values_size()));
        // irr (financial.cppm) Newton-Raphson-solves in double; a NaN
        // anywhere in `values` or in `guess` makes every iteration produce
        // NaN, which never satisfies either convergence check (a NaN
        // comparison is always false), so it burns the full 100-iteration
        // budget and returns FAILED_PRECONDITION rather than Status::OK with
        // a NaN result -- self-protecting, unlike ComputeNpv above. Checked
        // explicitly anyway for a clear, specific error instead of a
        // misleading "failed to converge" after 100 wasted iterations.
        if (auto s = require_all_finite(request->values(), "values"); !s.ok()) return s;
        if (auto s = require_finite(request->guess(), "guess"); !s.ok()) return s;
        const std::vector<double> values(request->values().begin(), request->values().end());
        const auto r = (request->guess() == 0.0) ? sensen::irr(values)
                                                 : sensen::irr(values, request->guess());
        if (!r) return fail(r);
        response->set_value(*r);
        return Status::OK;
    }

    auto ComputeXnpv(ServerContext* context, const sensen::finance::DatedCashFlowRequest* request,
                     sensen::finance::DoubleResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputeXnpv", quota::cost_cash_flow(request->values_size()));
        if (auto s = check_dated(request); !s.ok()) return s;
        // xnpv (financial.cppm) is a plain sum, same as npv_double above --
        // no Newton-Raphson self-protection. Reproduced directly: rate=NaN
        // (values/dates otherwise well-formed) returns Status::OK with
        // value=nan.
        if (auto s = require_finite(request->rate(), "rate"); !s.ok()) return s;
        if (auto s = require_all_finite(request->values(), "values"); !s.ok()) return s;
        if (auto s = require_all_finite(request->dates(), "dates"); !s.ok()) return s;
        const std::vector<double> values(request->values().begin(), request->values().end());
        const std::vector<double> dates(request->dates().begin(), request->dates().end());
        const auto r = sensen::xnpv(request->rate(), values, dates);
        if (!r) return fail(r);
        response->set_value(*r);
        return Status::OK;
    }

    auto ComputeXirr(ServerContext* context, const sensen::finance::DatedCashFlowRequest* request,
                     sensen::finance::DoubleResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputeXirr", quota::cost_cash_flow(request->values_size()));
        if (auto s = check_dated(request); !s.ok()) return s;
        // xirr Newton-Raphson-solves like irr above (self-protecting against
        // NaN via non-convergence), checked explicitly for the same clearer-
        // error reason.
        if (auto s = require_all_finite(request->values(), "values"); !s.ok()) return s;
        if (auto s = require_all_finite(request->dates(), "dates"); !s.ok()) return s;
        if (auto s = require_finite(request->guess(), "guess"); !s.ok()) return s;
        const std::vector<double> values(request->values().begin(), request->values().end());
        const std::vector<double> dates(request->dates().begin(), request->dates().end());
        const auto r = (request->guess() == 0.0) ? sensen::xirr(values, dates)
                                                 : sensen::xirr(values, dates, request->guess());
        if (!r) return fail(r);
        response->set_value(*r);
        return Status::OK;
    }

    auto ComputePaybackPeriod(ServerContext* context, const sensen::finance::PaybackRequest* request,
                              sensen::finance::DoubleResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputePaybackPeriod", quota::cost_cash_flow(request->values_size()));
        // payback_period/discounted_payback_period (financial.cppm) compare a
        // running cumulative sum against ">= 0.0" -- a NaN comparison is
        // always false, so a NaN cumulative sum today happens to fall through
        // to the loop's own "never recovered" error rather than an OK
        // response (self-protecting by luck of the comparison, not by
        // design). Checked explicitly anyway rather than relying on that.
        if (auto s = require_all_finite(request->values(), "values"); !s.ok()) return s;
        if (request->discounted()) {
            if (auto s = require_finite(request->rate(), "rate"); !s.ok()) return s;
        }
        const std::vector<double> values(request->values().begin(), request->values().end());
        const auto r = request->discounted()
                           ? sensen::discounted_payback_period(request->rate(), values)
                           : sensen::payback_period(values);
        if (!r) return fail(r);
        response->set_value(*r);
        return Status::OK;
    }

    auto ComputeCumulative(ServerContext* context, const sensen::finance::CumulativeRequest* request,
                           sensen::finance::DoubleResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputeCumulative", quota::cost_amortization(request->periods()));
        // `rate`/`present_value` are raw wire doubles (CumulativeRequest has
        // no decimal-string field at all), and `BigDecimal{double}` is
        // `static_cast<__int128_t>(std::round(val * 1e18))` -- casting a NaN
        // double to __int128_t is UNDEFINED BEHAVIOUR, not a NaN that merely
        // propagates. Reproduced directly: rate=NaN (present_value=300000,
        // periods=360, start_period=end_period=1) returned Status::OK with
        // value=-1424480681.094903 -- a large, plausible-looking, completely
        // fabricated number with no relationship to any real computation.
        if (auto s = require_finite(request->rate(), "rate"); !s.ok()) return s;
        if (auto s = require_finite(request->present_value(), "present_value"); !s.ok()) {
            return s;
        }
        // require_finite alone does not close this: a FINITE rate/
        // present_value as "small" as 1e30 already overflows __int128 once
        // BigDecimal(double) scales it by 1e18 (~1e48, far past __int128's
        // ~1.7e38 range) -- the same undefined-behaviour cast this RPC's own
        // comment above already names for NaN, just reachable through a
        // large-but-finite value require_finite cannot see is a problem.
        if (auto s = check_double_magnitude(request->rate(), "rate"); !s.ok()) return s;
        if (auto s = check_double_magnitude(request->present_value(), "present_value");
            !s.ok()) {
            return s;
        }
        // periods (nper) and the [start_period, end_period] range were
        // completely unbounded: cumipmt/cumprinc (financial.cppm) run
        // `for (per = start_period; per <= end_period; ++per)` unconditionally
        // -- the loop itself costs O(end_period - start_period) regardless of
        // how small `periods` is, since ipmt/ppmt's own per>nper early exit
        // only makes each ITERATION cheap, not the iteration COUNT. Reproduced
        // directly: start_period=-2,000,000, end_period=2,000,000 (periods=1)
        // is accepted today and walks 4,000,001 iterations for a flat
        // quota::cost_amortization(1) charge.
        if (auto s = check_period_count_ceiling(request->periods(), "periods"); !s.ok()) return s;
        if (request->start_period() < -100'000 || request->start_period() > 100'000) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "start_period must fall within [-100000, 100000]");
        }
        if (request->end_period() < -100'000 || request->end_period() > 100'000) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "end_period must fall within [-100000, 100000]");
        }
        // Both endpoints are now individually bounded to +/-100,000, so their
        // difference (computed in int64 to avoid the int32 overflow a naive
        // subtraction of two such endpoints could otherwise hit) is already
        // capped at 200,000 -- no further range check is needed on top of the
        // two bounds above.
        const BigDecimal rate{request->rate()};
        const BigDecimal pv_v{request->present_value()};
        const auto r =
            (request->component() == sensen::finance::CumulativeRequest::PRINCIPAL)
                ? sensen::cumprinc(rate, request->periods(), pv_v, request->start_period(),
                                   request->end_period(), timing_of(request->timing()))
                : sensen::cumipmt(rate, request->periods(), pv_v, request->start_period(),
                                  request->end_period(), timing_of(request->timing()));
        response->set_value(r.to_double());
        return Status::OK;
    }

    // -- Depreciation --------------------------------------------------------

    auto ComputeDepreciation(ServerContext* context, const sensen::finance::DepreciationRequest* request,
                             sensen::finance::DoubleResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputeDepreciation", quota::cost_default());
        // cost/salvage/life/period/factor are raw wire doubles. life's own
        // "<= 0.0" checks below (present before this fix) do not catch NaN
        // (NaN <= 0.0 is false), and cost/salvage/period/factor had no check
        // of any kind -- sln/syd/ddb/macrs (financial.cppm) are pure double
        // arithmetic with no guard of their own. Reproduced directly:
        // {method=STRAIGHT_LINE, cost=10000, salvage=1000, life=NaN} returns
        // Status::OK with value=nan (life<=0.0 is false for NaN, so the
        // existing check let it straight through).
        if (auto s = require_finite(request->cost(), "cost"); !s.ok()) return s;
        if (auto s = require_finite(request->salvage(), "salvage"); !s.ok()) return s;
        if (auto s = require_finite(request->life(), "life"); !s.ok()) return s;
        if (auto s = require_finite(request->period(), "period"); !s.ok()) return s;
        if (auto s = require_finite(request->factor(), "factor"); !s.ok()) return s;
        double v = 0.0;
        switch (request->method()) {
            case sensen::finance::DepreciationRequest::STRAIGHT_LINE:
                if (request->life() <= 0.0) {
                    return Status(grpc::StatusCode::INVALID_ARGUMENT, "life must be positive");
                }
                v = sensen::sln(request->cost(), request->salvage(), request->life());
                break;
            case sensen::finance::DepreciationRequest::SUM_OF_YEARS_DIGITS:
                if (request->life() <= 0.0) {
                    return Status(grpc::StatusCode::INVALID_ARGUMENT, "life must be positive");
                }
                v = sensen::syd(request->cost(), request->salvage(), request->life(),
                                request->period());
                break;
            case sensen::finance::DepreciationRequest::DECLINING_BALANCE: {
                if (request->life() <= 0.0) {
                    return Status(grpc::StatusCode::INVALID_ARGUMENT, "life must be positive");
                }
                // 2.0 is the double-declining convention. Unlike a compounding
                // frequency, an unstated factor has one overwhelmingly standard
                // value, and the field name says which.
                const double factor = (request->factor() > 0.0) ? request->factor() : 2.0;
                v = sensen::ddb(request->cost(), request->salvage(), request->life(),
                                request->period(), factor);
                break;
            }
            case sensen::finance::DepreciationRequest::MACRS:
                if (request->recovery_period() <= 0 || request->year() <= 0) {
                    return Status(grpc::StatusCode::INVALID_ARGUMENT,
                                  "MACRS needs a positive recovery_period and year");
                }
                v = sensen::macrs(request->cost(), request->recovery_period(), request->year());
                break;
            default:
                return Status(grpc::StatusCode::INVALID_ARGUMENT, "unknown depreciation method");
        }
        response->set_value(v);
        return Status::OK;
    }

    // -- Fixed income --------------------------------------------------------

    auto AnalyzeBond(ServerContext* context, const sensen::finance::BondRequest* request,
                     sensen::finance::BondResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("AnalyzeBond", quota::cost_default());
        // par/coupon_rate/years_to_maturity/redemption/yield_guess and
        // whichever of yield/price the caller supplied are all raw wire
        // doubles. coupon_rate had NO check of any kind before this fix;
        // par/years_to_maturity's own "<= 0.0" guards (below) do not catch
        // NaN, the same class-4 defeat PriceOptionTree's own original bug
        // had ("NaN <= 0" is false). years_to_maturity is the more serious
        // of the two: price_bond/yield_bond/duration_bond/convexity_bond
        // (financial.cppm) all cast it into an int loop bound via
        // `static_cast<int>(std::round(years_to_maturity * frequency))`,
        // and casting a NaN double to int is UNDEFINED BEHAVIOUR per
        // [conv.fpint], not merely a NaN that propagates predictably.
        // Reproduced directly: {par=1000, coupon_rate=0.05, frequency=2,
        // years_to_maturity=NaN, yield=0.045} returned Status::OK with
        // every field of the response NaN.
        if (auto s = require_finite(request->par(), "par"); !s.ok()) return s;
        if (auto s = require_finite(request->coupon_rate(), "coupon_rate"); !s.ok()) return s;
        if (auto s = require_finite(request->years_to_maturity(), "years_to_maturity"); !s.ok()) {
            return s;
        }
        if (auto s = require_finite(request->redemption(), "redemption"); !s.ok()) return s;
        if (auto s = require_finite(request->yield_guess(), "yield_guess"); !s.ok()) return s;
        switch (request->known_case()) {
            case sensen::finance::BondRequest::kYield:
                if (auto s = require_finite(request->yield(), "yield"); !s.ok()) return s;
                break;
            case sensen::finance::BondRequest::kPrice:
                if (auto s = require_finite(request->price(), "price"); !s.ok()) return s;
                break;
            default:
                break;  // caught by the "supply either yield or price" check below.
        }
        if (request->frequency() <= 0) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "frequency must be positive (2 = semi-annual)");
        }
        if (request->years_to_maturity() <= 0.0) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT, "years_to_maturity must be positive");
        }
        if (request->par() <= 0.0) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT, "par must be positive");
        }
        // Redemption is quoted as a percent of par; 100 means redeemed at par.
        const double redemption = (request->redemption() > 0.0) ? request->redemption() : 100.0;

        double yld = 0.0;
        double price = 0.0;
        switch (request->known_case()) {
            case sensen::finance::BondRequest::kYield:
                yld = request->yield();
                price = sensen::price_bond(request->par(), request->coupon_rate(), yld,
                                           request->frequency(), request->years_to_maturity(),
                                           redemption);
                break;
            case sensen::finance::BondRequest::kPrice: {
                price = request->price();
                const double guess =
                    (request->yield_guess() != 0.0) ? request->yield_guess() : 0.05;
                const auto y = sensen::yield_bond(request->par(), request->coupon_rate(), price,
                                                  request->frequency(),
                                                  request->years_to_maturity(), redemption, guess);
                if (!y) return fail(y);
                yld = *y;
                break;
            }
            default:
                // Neither supplied. There is no bond figure that can be derived
                // from the remaining parameters alone, so this is a request
                // that cannot be answered rather than one worth guessing at.
                return Status(grpc::StatusCode::INVALID_ARGUMENT,
                              "supply either yield or price; the other is derived from it");
        }

        response->set_price(price);
        response->set_yield(yld);
        response->set_macaulay_duration(sensen::duration_bond(
            request->par(), request->coupon_rate(), yld, request->frequency(),
            request->years_to_maturity(), redemption));
        response->set_convexity(sensen::convexity_bond(
            request->par(), request->coupon_rate(), yld, request->frequency(),
            request->years_to_maturity(), redemption));
        return Status::OK;
    }

    auto AnalyzeTreasuryBill(ServerContext* context, const sensen::finance::TreasuryBillRequest* request,
                             sensen::finance::TreasuryBillResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("AnalyzeTreasuryBill", quota::cost_default());
        // face_value's own "<= 0.0" guard (below) does not catch NaN, and
        // discount_rate/price (whichever the caller supplied) had no check
        // of any kind -- price_tbill (financial.cppm) is pure double
        // arithmetic that does not throw on NaN, and the "price <= 0.0"
        // self-check further down does not catch a NaN price either.
        // Reproduced directly: {face_value=10000, discount_rate=NaN,
        // days_to_maturity=90} returned Status::OK with price=nan and every
        // yield field nan.
        if (auto s = require_finite(request->face_value(), "face_value"); !s.ok()) return s;
        switch (request->known_case()) {
            case sensen::finance::TreasuryBillRequest::kDiscountRate:
                if (auto s = require_finite(request->discount_rate(), "discount_rate"); !s.ok()) {
                    return s;
                }
                break;
            case sensen::finance::TreasuryBillRequest::kPrice:
                if (auto s = require_finite(request->price(), "price"); !s.ok()) return s;
                break;
            default:
                break;  // caught by the "supply either discount_rate or price" check below.
        }
        if (request->days_to_maturity() <= 0) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT, "days_to_maturity must be positive");
        }
        if (request->face_value() <= 0.0) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT, "face_value must be positive");
        }

        double price = 0.0;
        switch (request->known_case()) {
            case sensen::finance::TreasuryBillRequest::kDiscountRate:
                price = sensen::price_tbill(request->face_value(), request->discount_rate(),
                                            request->days_to_maturity());
                break;
            case sensen::finance::TreasuryBillRequest::kPrice:
                price = request->price();
                break;
            default:
                return Status(grpc::StatusCode::INVALID_ARGUMENT,
                              "supply either discount_rate or price");
        }
        if (price <= 0.0) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT, "price must be positive");
        }

        response->set_price(price);
        response->set_bond_equivalent_yield(
            sensen::yield_tbill_bey(price, request->face_value(), request->days_to_maturity()));
        response->set_money_market_yield(
            sensen::yield_tbill_mmy(price, request->face_value(), request->days_to_maturity()));
        response->set_bank_discount_yield(
            sensen::yield_tbill_bdy(price, request->face_value(), request->days_to_maturity()));
        return Status::OK;
    }

    // -- Futures -------------------------------------------------------------

    auto PriceFutures(ServerContext* context, const sensen::finance::FuturesPricingRequest* request,
                      sensen::finance::DoubleResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("PriceFutures", quota::cost_default());
        // PriceFutures had NO validation of any kind -- not even a bare
        // "<= 0" check. price_futures (financial.cppm) either exponentiates
        // (continuous compounding) or raises (1+cost_of_carry) to
        // years_to_maturity via std::pow (discrete compounding); a NaN in
        // any field, or a (1+cost_of_carry) at or below zero raised to a
        // non-integer years_to_maturity under discrete compounding (a
        // std::pow domain error -> NaN), reaches Status::OK carrying a
        // NaN/Infinity value. Reproduced directly: spot=NaN returns
        // Status::OK with value=nan; {cost_of_carry=-2,
        // years_to_maturity=0.5, continuous=false} returns Status::OK with
        // value=nan.
        if (auto s = require_finite(request->spot(), "spot"); !s.ok()) return s;
        if (auto s = require_finite(request->rate(), "rate"); !s.ok()) return s;
        if (auto s = require_finite(request->cost_of_carry(), "cost_of_carry"); !s.ok()) return s;
        if (auto s = require_finite(request->years_to_maturity(), "years_to_maturity"); !s.ok()) {
            return s;
        }
        // Discrete (non-continuous) compounding raises (1+cost_of_carry) to
        // years_to_maturity via std::pow; a base at or below zero raised to
        // a fractional exponent is a domain error (NaN), not a real futures
        // price. check_rate_floor is the exact same "1+x > 0" bound this
        // field needs, already used elsewhere in this file for per-period
        // rates entering the identical shape of expression.
        if (!request->continuous()) {
            if (auto s = check_rate_floor(request->cost_of_carry(), "cost_of_carry"); !s.ok()) {
                return s;
            }
        }
        sensen::FuturesPricingParams p{};
        p.spot = request->spot();
        p.rate = request->rate();
        p.cost_of_carry = request->cost_of_carry();
        p.years_to_maturity = request->years_to_maturity();
        p.continuous = request->continuous();
        response->set_value(sensen::price_futures(p));
        return Status::OK;
    }

    auto ValueFutures(ServerContext* context, const sensen::finance::FuturesValuationRequest* request,
                      sensen::finance::DoubleResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ValueFutures", quota::cost_default());
        // ValueFutures had NO validation of any kind either. value_futures
        // (financial.cppm) is exp(-rate*years)*(spot-delivery), which does
        // not itself hit a domain error for any real input -- but a NaN in
        // any field still reaches Status::OK carrying a NaN value.
        if (auto s = require_finite(request->current_spot(), "current_spot"); !s.ok()) return s;
        if (auto s = require_finite(request->delivery_price(), "delivery_price"); !s.ok()) {
            return s;
        }
        if (auto s = require_finite(request->rate(), "rate"); !s.ok()) return s;
        if (auto s = require_finite(request->years_to_maturity(), "years_to_maturity"); !s.ok()) {
            return s;
        }
        response->set_value(sensen::value_futures(request->current_spot(), request->delivery_price(),
                                                  request->rate(), request->years_to_maturity(),
                                                  request->is_long()));
        return Status::OK;
    }

    auto SimulateMarginAccount(ServerContext* context, const sensen::finance::MarginSimulationRequest* request,
                               sensen::finance::MarginSimulationResponse* response)
        -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("SimulateMarginAccount",
               quota::cost_margin_simulation(request->daily_prices_size()));
        if (request->contract_size() <= 0) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT, "contract_size must be positive");
        }
        // initial_deposit/initial_margin_requirement/
        // maintenance_margin_requirement/entry_price had no validation of
        // any kind, and daily_prices (the mark-to-market path) was
        // completely unbounded -- simulate_futures_margin_account
        // (financial.cppm) walks it in a plain for loop, O(len(daily_prices))
        // regardless of how quota::cost_margin_simulation prices a single
        // request. Reproduced directly: entry_price=NaN returns Status::OK
        // with balance=nan; a single NaN inside daily_prices propagates a
        // NaN balance the same way, the per-element case a scalar guard
        // cannot catch.
        if (auto s = require_finite(request->initial_deposit(), "initial_deposit"); !s.ok()) {
            return s;
        }
        if (auto s = require_finite(request->initial_margin_requirement(),
                                    "initial_margin_requirement");
            !s.ok()) {
            return s;
        }
        if (auto s = require_finite(request->maintenance_margin_requirement(),
                                    "maintenance_margin_requirement");
            !s.ok()) {
            return s;
        }
        if (auto s = require_finite(request->entry_price(), "entry_price"); !s.ok()) return s;
        if (auto s = check_series_length_ceiling(request->daily_prices_size(), "daily_prices");
            !s.ok()) {
            return s;
        }
        if (auto s = require_all_finite(request->daily_prices(), "daily_prices"); !s.ok()) {
            return s;
        }
        const std::vector<double> path(request->daily_prices().begin(),
                                       request->daily_prices().end());
        const auto s = sensen::simulate_futures_margin_account(
            request->initial_deposit(), request->initial_margin_requirement(),
            request->maintenance_margin_requirement(), request->contract_size(),
            request->entry_price(), path, request->is_long());
        response->set_balance(s.balance);
        response->set_initial_margin(s.initial_margin);
        response->set_maintenance_margin(s.maintenance_margin);
        response->set_contract_size(s.contract_size);
        response->set_margin_call(s.margin_call);
        return Status::OK;
    }

    auto ComputeHedge(ServerContext* context, const sensen::finance::HedgeRequest* request,
                      sensen::finance::HedgeResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputeHedge", quota::cost_default());
        // The only existing guard was an EXACT-equality check against zero;
        // NaN == 0.0 is false, so a NaN futures_volatility sailed straight
        // through to calculate_hedge_ratio's own "<= 0.0" guard, which NaN
        // defeats the same way. A negative futures_volatility is not a real
        // one either -- the exact-equality check let it through to that
        // same fallback-to-zero rather than being refused as nonsensical.
        // asset_volatility/correlation had no check of any kind. Reproduced
        // directly: futures_volatility=NaN returns Status::OK with
        // hedge_ratio=nan.
        if (auto s = require_finite(request->asset_volatility(), "asset_volatility"); !s.ok()) {
            return s;
        }
        if (auto s = require_finite(request->futures_volatility(), "futures_volatility");
            !s.ok()) {
            return s;
        }
        if (auto s = require_finite(request->correlation(), "correlation"); !s.ok()) return s;
        if (request->futures_volatility() <= 0.0) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "futures_volatility must be positive; the hedge ratio divides by it");
        }
        const double ratio = sensen::calculate_hedge_ratio(
            request->asset_volatility(), request->futures_volatility(), request->correlation());
        response->set_hedge_ratio(ratio);

        // spot_value/contract_multiplier/futures_price feed the optional
        // contract-count leg. have_position is decided by "!= 0.0" -- and
        // NaN != 0.0 is TRUE, so a NaN position input was previously treated
        // as "a genuine position was supplied" and produced a NaN contracts
        // count in an otherwise-OK response. Checked finite here regardless
        // of whether have_position ends up true, since have_position itself
        // is what a NaN would corrupt.
        if (auto s = require_finite(request->spot_value(), "spot_value"); !s.ok()) return s;
        if (auto s = require_finite(request->contract_multiplier(), "contract_multiplier");
            !s.ok()) {
            return s;
        }
        if (auto s = require_finite(request->futures_price(), "futures_price"); !s.ok()) {
            return s;
        }
        // A contract count needs a position size and a contract spec. Without
        // them the count is left absent and SAID to be absent, rather than
        // returned as a zero that reads like "no contracts needed".
        const bool have_position = request->spot_value() != 0.0 &&
                                   request->contract_multiplier() != 0.0 &&
                                   request->futures_price() != 0.0;
        if (have_position) {
            response->set_contracts(sensen::calculate_hedge_contracts(
                request->spot_value(), request->contract_multiplier(), request->futures_price(),
                ratio));
            response->set_contracts_computed(true);
        }
        return Status::OK;
    }

    auto ComputeCommoditySpread(ServerContext* context,
                                const sensen::finance::CommoditySpreadRequest* request,
                                sensen::finance::DoubleResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputeCommoditySpread", quota::cost_default());
        // No validation of any kind existed here -- calculate_crack_spread_321/
        // calculate_spark_spread/calculate_crush_spread (financial.cppm) are
        // all pure double arithmetic with no domain requirement of their
        // own, but a NaN in any field still reaches Status::OK carrying a
        // NaN spread value, same class as rate on PriceOptionTree/
        // ComputeProbabilityTree elsewhere in this file (no positivity
        // requirement, but must still be finite).
        if (auto s = require_finite(request->a(), "a"); !s.ok()) return s;
        if (auto s = require_finite(request->b(), "b"); !s.ok()) return s;
        if (auto s = require_finite(request->c(), "c"); !s.ok()) return s;
        double v = 0.0;
        switch (request->spread()) {
            case sensen::finance::CommoditySpreadRequest::CRACK_321:
                v = sensen::calculate_crack_spread_321(request->a(), request->b(), request->c());
                break;
            case sensen::finance::CommoditySpreadRequest::SPARK:
                v = sensen::calculate_spark_spread(request->a(), request->b(), request->c());
                break;
            case sensen::finance::CommoditySpreadRequest::CRUSH:
                v = sensen::calculate_crush_spread(request->a(), request->b(), request->c());
                break;
            default:
                return Status(grpc::StatusCode::INVALID_ARGUMENT, "unknown spread");
        }
        response->set_value(v);
        return Status::OK;
    }

    // -- Real estate ---------------------------------------------------------

    auto ComputeRentalRoi(ServerContext* context, const sensen::finance::RentalRoiRequest* request,
                          sensen::finance::RentalRoiResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputeRentalRoi", quota::cost_default());
        if (request->periods_per_year() <= 0) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "periods_per_year must be positive (12 monthly, 26 bi-weekly)");
        }
        REQUIRE_DECIMAL(value, request->property_value(), "property_value");
        REQUIRE_DECIMAL(cash, request->total_cash_invested(), "total_cash_invested");
        REQUIRE_DECIMAL(rent, request->periodic_gross_rent(), "periodic_gross_rent");
        REQUIRE_DECIMAL(opex, request->periodic_operating_expenses(), "periodic_operating_expenses");
        // periodic_mortgage_payment defaults to 0 in sensen's own signature
        // (calculate_rental_roi) -- an all-cash purchase has no debt service,
        // and that is exactly what an absent value here means.
        READ_DECIMAL(debt, request->periodic_mortgage_payment(), "periodic_mortgage_payment");

        const auto s = sensen::calculate_rental_roi(value, cash, rent, opex, debt,
                                                    request->periods_per_year());
        response->set_net_operating_income(s.net_operating_income.to_string());
        response->set_annual_cash_flow(s.annual_cash_flow.to_string());
        response->set_cash_on_cash_return(s.cash_on_cash_return.to_string());
        response->set_cap_rate(s.cap_rate.to_string());
        response->set_gross_rent_multiplier(s.gross_rent_multiplier.to_string());
        return Status::OK;
    }

    auto ComputeHomeFutureValue(ServerContext* context,
                               const sensen::finance::HomeFutureValueRequest* request,
                               sensen::finance::HomeFutureValueResponse* response)
        -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputeHomeFutureValue", quota::cost_default());
        if (request->target_years() <= 0 || request->target_years() > 100) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "target_years must be positive and at most 100");
        }
        if (auto s = check_payments_per_year(request->payments_per_year()); !s.ok()) return s;
        REQUIRE_DECIMAL_SAFE(prop_value, request->current_property_value(),
                             "current_property_value");
        REQUIRE_DECIMAL_SAFE(appreciation, request->annual_appreciation_rate(),
                             "annual_appreciation_rate");
        REQUIRE_DECIMAL_SAFE(loan_balance, request->current_loan_balance(), "current_loan_balance");
        REQUIRE_DECIMAL_SAFE(mortgage_rate, request->annual_mortgage_rate(),
                             "annual_mortgage_rate");
        REQUIRE_DECIMAL_SAFE(cur_pmt, request->current_monthly_payment(),
                             "current_monthly_payment");
        // annual_mortgage_rate feeds fv()'s BigDecimal pow(target_years *
        // payments_per_year) -- both factors are individually capped
        // (target_years<=100, payments_per_year<=366) but their product,
        // not either alone, is the exponent that can overflow it.
        if (auto s = check_compound_growth_safe(
                mortgage_rate.to_double(), request->payments_per_year(),
                request->target_years() * request->payments_per_year(), "annual_mortgage_rate");
            !s.ok()) {
            return s;
        }

        const auto s = sensen::calculate_home_future_value(
            prop_value, appreciation, loan_balance, mortgage_rate, cur_pmt,
            request->target_years(), request->payments_per_year());
        // Double: compound appreciation is computed in std::pow double
        // (financial.cppm:2067-2068).
        response->set_future_property_value(s.future_property_value.to_double());
        // Exact: the remaining balance is a BigDecimal closed form (fv()),
        // clamped to 0 once the loan would have retired.
        response->set_future_loan_balance(s.future_loan_balance.to_string());
        // Exact BigDecimal subtraction of the two fields above, but the
        // property leg entered in double -- the string carries the
        // arithmetic faithfully, not 18 fresh digits.
        response->set_future_equity(s.future_equity.to_string());
        return Status::OK;
    }

    auto ComputeRentVsBuy(ServerContext* context, const sensen::finance::RentVsBuyRequest* request,
                         sensen::finance::RentVsBuyResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        // The engine loops `years` iterations (financial.cppm:2144) -- a
        // trivial double loop, bounded to <=100 by the validation below, so
        // cost_default() prices it honestly rather than reaching for a new
        // helper for an O(<=100) walk.
        CHARGE("ComputeRentVsBuy", quota::cost_default());
        if (request->years() <= 0 || request->years() > 100) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "years must be positive and at most 100");
        }
        REQUIRE_DECIMAL_SAFE(price, request->property_price(), "property_price");
        REQUIRE_DECIMAL_SAFE(down, request->down_payment(), "down_payment");
        REQUIRE_DECIMAL_SAFE(piti, request->monthly_piti_and_maintenance(),
                             "monthly_piti_and_maintenance");
        REQUIRE_DECIMAL_SAFE(appreciation, request->annual_home_appreciation(),
                             "annual_home_appreciation");
        REQUIRE_DECIMAL_SAFE(rent, request->current_monthly_rent(), "current_monthly_rent");
        REQUIRE_DECIMAL_SAFE(rent_increase, request->annual_rent_increase(),
                             "annual_rent_increase");
        REQUIRE_DECIMAL_SAFE(investment_return, request->annual_investment_return(),
                             "annual_investment_return");

        const auto s = sensen::calculate_rent_vs_buy(price, down, piti, appreciation, rent,
                                                      rent_increase, investment_return,
                                                      request->years());
        // All doubles: the rent escalation, appreciation and investment legs
        // are all computed in double (financial.cppm:2135-2153), and the
        // buy/rent figures are only meaningful against each other -- quoting
        // one side to 18 places would misstate which digits are real.
        response->set_total_cost_of_buying(s.total_cost_of_buying.to_double());
        response->set_total_cost_of_renting(s.total_cost_of_renting.to_double());
        response->set_is_buying_better(s.is_buying_better);
        response->set_buying_advantage(s.buying_advantage.to_double());
        return Status::OK;
    }

    auto ComputeHomeNpv(ServerContext* context, const sensen::finance::HomeNpvRequest* request,
                       sensen::finance::HomeNpvResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        // The engine builds holding_period_years*12 monthly cash flows and
        // then Newton-iterates a full NPV per step in xirr -- the same shape
        // ComputeXirr already prices with cost_cash_flow. Priced from the
        // raw, still-unvalidated request, like refinance_charge_months --
        // holding_period_years*12 can overflow int32 on its own
        // (INT32_MAX*12 does), which is undefined behaviour regardless of
        // the request being refused two lines later, so the multiply is
        // done in int64 and clamped rather than directly here.
        CHARGE("ComputeHomeNpv",
               quota::cost_cash_flow(home_npv_charge_months(request->holding_period_years())));
        if (request->holding_period_years() <= 0 || request->holding_period_years() > 100) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "holding_period_years must be positive and at most 100");
        }
        if (request->loan_term_years() <= 0 || request->loan_term_years() > 100) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "loan_term_years must be positive and at most 100");
        }
        REQUIRE_DECIMAL_SAFE(price, request->property_price(), "property_price");
        REQUIRE_DECIMAL_SAFE(down, request->down_payment(), "down_payment");
        REQUIRE_DECIMAL_SAFE(closing, request->closing_costs_buy(), "closing_costs_buy");
        REQUIRE_DECIMAL_SAFE(loan_amt, request->loan_amount(), "loan_amount");
        REQUIRE_DECIMAL_SAFE(loan_rate, request->loan_annual_rate(), "loan_annual_rate");
        REQUIRE_DECIMAL_SAFE(taxes, request->monthly_taxes_ins_hoa(), "monthly_taxes_ins_hoa");
        REQUIRE_DECIMAL_SAFE(maint, request->monthly_maintenance(), "monthly_maintenance");
        REQUIRE_DECIMAL_SAFE(appreciation, request->annual_appreciation_rate(),
                             "annual_appreciation_rate");
        REQUIRE_DECIMAL_SAFE(sell_pct, request->selling_closing_cost_percent(),
                             "selling_closing_cost_percent");
        REQUIRE_DECIMAL_SAFE(rent_saved, request->monthly_rent_saved(), "monthly_rent_saved");
        REQUIRE_DECIMAL_SAFE(rent_increase, request->annual_rent_increase(),
                             "annual_rent_increase");
        REQUIRE_DECIMAL_SAFE(discount, request->annual_discount_rate(), "annual_discount_rate");

        // loan_annual_rate feeds pmt()'s BigDecimal pow(loan_term_years*12)
        // -- loan_term_years is already capped at 100 (1200 months), but an
        // extreme rate still overflows it on its own.
        if (auto s = check_compound_growth_safe(loan_rate.to_double(), 12,
                                                request->loan_term_years() * 12,
                                                "loan_annual_rate");
            !s.ok()) {
            return s;
        }

        sensen::HomeNPVInput input{};
        input.property_price = price;
        input.down_payment = down;
        input.closing_costs_buy = closing;
        input.loan_amount = loan_amt;
        input.loan_annual_rate = loan_rate;
        input.loan_term_years = request->loan_term_years();
        input.monthly_taxes_ins_hoa = taxes;
        input.monthly_maintenance = maint;
        input.annual_appreciation_rate = appreciation;
        input.selling_closing_cost_percent = sell_pct;
        input.monthly_rent_saved = rent_saved;
        input.annual_rent_increase = rent_increase;
        input.annual_discount_rate = discount;
        input.holding_period_years = request->holding_period_years();

        // calculate_home_npv returns std::expected<HomeNPVSummary,
        // std::string> (sensen commit 4d4b4cbd): an xnpv/xirr failure inside
        // the model is refused rather than silently reported as NPV 0.0 or
        // IRR 0.0, either of which is indistinguishable from a genuine
        // breakeven. Residual: xirr non-convergence that sensen's own solver
        // does not detect as failure is outside what this wrapper can catch
        // -- see docs/superpowers/specs/2026-08-05-finance-proto-extension.md
        // open item 3.
        const auto r = sensen::calculate_home_npv(input);
        if (!r) return fail(r);
        response->set_net_present_value(r->net_present_value.to_double());
        response->set_internal_rate_of_return(r->internal_rate_of_return.to_double());
        response->set_future_sale_price(r->future_sale_price.to_double());
        response->set_future_equity(r->future_equity.to_double());
        return Status::OK;
    }

    /** Itemises closing costs and totals the cash a buyer brings to closing.
     *
     * Flat cost: this is eleven multiplies and a sum with no iteration, no
     * schedule and no solve, so it is the cheapest thing on the real-estate
     * surface rather than being priced like its neighbours.
     */
    auto ComputeClosingCosts(ServerContext* context,
                             const sensen::finance::ClosingCostsRequest* request,
                             sensen::finance::ClosingCostsResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputeClosingCosts", quota::cost_default());

        // Bounded rather than merely non-negative. An escrow of a few months is
        // the whole point of the field; a four-figure one is a typo that would
        // otherwise return a confident number built from it.
        if (request->tax_escrow_months() < 0 || request->tax_escrow_months() > 24) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "tax_escrow_months must be between 0 and 24");
        }
        if (request->has_prepaid_interest_days() &&
            (request->prepaid_interest_days() < 0 || request->prepaid_interest_days() > 365)) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "prepaid_interest_days must be between 0 and 365");
        }

        // Only three fields are genuinely required. The rest are costs a
        // closing may simply not have, and REQUIRE_DECIMAL_SAFE refuses an
        // EMPTY string -- so requiring them would force every client to send
        // "0" eleven times and refuse the request the first time it forgot
        // one. READ_DECIMAL_SAFE keeps the magnitude guard and reads an
        // absent field as zero, which is what "I have no inspection fee"
        // means. Sign is checked in the engine, so an omitted field and a
        // hostile negative one are still told apart.
        REQUIRE_DECIMAL_SAFE(price, request->home_price(), "home_price");
        REQUIRE_DECIMAL_SAFE(down_pct, request->down_payment_percent(), "down_payment_percent");
        REQUIRE_DECIMAL_SAFE(rate, request->annual_rate(), "annual_rate");
        READ_DECIMAL_SAFE(orig_pct, request->origination_fee_percent(),
                             "origination_fee_percent");
        READ_DECIMAL_SAFE(points_pct, request->discount_points_percent(),
                             "discount_points_percent");
        READ_DECIMAL_SAFE(other_fees, request->other_lender_fees(), "other_lender_fees");
        READ_DECIMAL_SAFE(title_pct, request->title_settlement_percent(),
                             "title_settlement_percent");
        READ_DECIMAL_SAFE(appraisal, request->appraisal_fee(), "appraisal_fee");
        READ_DECIMAL_SAFE(inspection, request->inspection_fee(), "inspection_fee");
        READ_DECIMAL_SAFE(recording, request->recording_fees(), "recording_fees");
        READ_DECIMAL_SAFE(transfer_pct, request->transfer_tax_percent(),
                             "transfer_tax_percent");
        READ_DECIMAL_SAFE(hoi, request->homeowners_insurance_annual(),
                             "homeowners_insurance_annual");
        READ_DECIMAL_SAFE(ptax, request->property_tax_annual(), "property_tax_annual");
        READ_DECIMAL_SAFE(credits, request->seller_lender_credits(), "seller_lender_credits");

        sensen::ClosingCostsInput input{};
        input.home_price = price;
        input.down_payment_percent = down_pct;
        input.annual_rate = rate;
        input.origination_fee_percent = orig_pct;
        input.discount_points_percent = points_pct;
        input.other_lender_fees = other_fees;
        input.title_settlement_percent = title_pct;
        input.appraisal_fee = appraisal;
        input.inspection_fee = inspection;
        input.recording_fees = recording;
        input.transfer_tax_percent = transfer_pct;
        input.homeowners_insurance_annual = hoi;
        input.property_tax_annual = ptax;
        input.tax_escrow_months = request->tax_escrow_months();
        input.seller_lender_credits = credits;
        if (request->has_prepaid_interest_days()) {
            input.prepaid_interest_days = request->prepaid_interest_days();
        }

        // Validate BEFORE dispatching, against the engine's OWN rules --
        // `sensen::validate_closing_costs` is the same function
        // `calculate_closing_costs` runs, not a second copy that could drift.
        //
        // It matters which status code says so. `fail()` maps an engine error
        // to FAILED_PRECONDITION, which is right for "the system cannot do
        // this now" and WRONG for "your argument is bad": a negative fee is
        // invalid regardless of any system state. Running the contract here
        // lets a bad request answer INVALID_ARGUMENT, while a direct sensen
        // caller still gets the identical refusal from the engine itself.
        if (auto ok = sensen::validate_closing_costs(input); !ok) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT, ok.error());
        }

        const auto r = sensen::calculate_closing_costs(input);
        if (!r) return fail(r);

        response->set_origination_fee(r->origination_fee.to_string());
        response->set_discount_points(r->discount_points.to_string());
        response->set_other_lender_fees(r->other_lender_fees.to_string());
        response->set_title_settlement(r->title_settlement.to_string());
        response->set_appraisal_fee(r->appraisal_fee.to_string());
        response->set_inspection_fee(r->inspection_fee.to_string());
        response->set_recording_fees(r->recording_fees.to_string());
        response->set_transfer_tax(r->transfer_tax.to_string());
        response->set_homeowners_insurance_prepaid(r->homeowners_insurance_prepaid.to_string());
        response->set_property_tax_escrow(r->property_tax_escrow.to_string());
        response->set_prepaid_interest(r->prepaid_interest.to_string());
        response->set_prepaid_interest_days(r->prepaid_interest_days);
        response->set_itemised_subtotal(r->itemised_subtotal.to_string());
        response->set_seller_lender_credits(r->seller_lender_credits.to_string());
        response->set_total_closing_costs(r->total_closing_costs.to_string());
        response->set_loan_amount(r->loan_amount.to_string());
        response->set_down_payment(r->down_payment.to_string());
        response->set_total_cash_to_close(r->total_cash_to_close.to_string());
        response->set_closing_costs_percent_of_price(r->closing_costs_percent_of_price);
        return Status::OK;
    }

    // -- Options -------------------------------------------------------------

    auto PriceOptionTree(ServerContext* context, const sensen::finance::OptionTreeRequest* request,
                         sensen::finance::OptionPricingResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("PriceOptionTree",
               quota::cost_option_tree(request->steps(), request->averaging_states()));

        // Derived from the request by Validate, consumed by Dispatch -- see
        // the "Shared request-lifecycle graph" comment above this class for
        // why these live as outer-scope locals captured by the closures
        // below rather than being threaded through FinanceLifecycleState.
        sensen::ExerciseType ex = sensen::ExerciseType::European;
        std::vector<double> dates;
        int avg_states = 50;
        double lambda = 1.224744871391589;
        sensen::AsianType asian = sensen::AsianType::None;
        sensen::OptionPricingResult<double> r{};

        const auto validate = [&]() -> Status {
            // price_option_double THROWS on these rather than returning an
            // error, so they are checked here and reported as the caller's
            // mistake they are, not as an internal fault.
            if (request->steps() <= 0 || request->years_to_expiry() <= 0.0 ||
                request->spot() <= 0.0 || request->strike() <= 0.0 ||
                request->volatility() <= 0.0) {
                return Status(
                    grpc::StatusCode::INVALID_ARGUMENT,
                    "steps, years_to_expiry, spot, strike and volatility must all be positive");
            }
            // A 1-step trinomial tree has no second backward-induction layer,
            // but the Greeks calculation below unconditionally reads one:
            // theta reads "step 2, node 0" (options.cppm's V[4+2+0])
            // regardless of how many steps the caller asked for. At steps=1
            // the tree array itself only holds (1+1)^2=4 elements, so that
            // read runs past the end of it -- measured returning a
            // plausible-looking but meaningless theta (47.19, against -1.67
            // at steps=200) rather than an error, because a small heap
            // over-read does not reliably fault. Refused here instead of
            // left to silently return that.
            if (request->steps() < 2) {
                return Status(grpc::StatusCode::INVALID_ARGUMENT,
                              "steps must be at least 2 (a 1-step tree has no second "
                              "backward-induction layer for the theta estimate to read)");
            }
            // Every numeric field below is a plain wire double, so a NaN or
            // +/-Infinity survives the positivity check above -- see
            // require_finite's own comment for why. rate carries no
            // positivity requirement of its own (negative rates are real)
            // but must still be finite.
            if (auto s = require_finite(request->spot(), "spot"); !s.ok()) return s;
            if (auto s = require_finite(request->strike(), "strike"); !s.ok()) return s;
            if (auto s = require_finite(request->rate(), "rate"); !s.ok()) return s;
            if (auto s = require_finite(request->volatility(), "volatility"); !s.ok()) return s;
            if (auto s = require_finite(request->years_to_expiry(), "years_to_expiry"); !s.ok()) {
                return s;
            }
            // lambda had NO finiteness check of any kind before this fix --
            // a caller-sent +Infinity survives "(lambda() > 0.0)" (Infinity
            // > 0.0 is true) and becomes lambda_eff itself, so
            // u=exp(lambda*sigma* sqrt(dt)) overflows to +Infinity directly
            // regardless of how sane every other field is. (A NaN lambda is
            // harmless on its own -- "NaN > 0.0" is false, so it falls
            // through to the sqrt(1.5) default -- but is refused anyway for
            // the same reason every other field is: a NaN on the wire is a
            // caller mistake, not silently ignorable input.)
            if (auto s = require_finite(request->lambda(), "lambda"); !s.ok()) return s;

            // -- Magnitude guards: closes the gap named in this file's own
            // "KNOWN GAP, not fixed by this change" test (tests/
            // test_option_pricing_service.cpp) -- volatility=1.0e10 (and
            // the other fields at absurd-but-finite magnitude) used to sail
            // past every check above and overflow an internal tree quantity
            // to +/-Infinity DURING computation. See
            // check_option_field_magnitude's own comment for why each bound
            // is what it is. --
            if (auto s = check_option_field_magnitude(request->volatility(), "volatility",
                                                       kMaxOptionVolatility, "1000% annualised");
                !s.ok()) {
                return s;
            }
            if (auto s = check_option_field_magnitude(request->rate(), "rate", kMaxOptionRate,
                                                       "+/-500% continuously compounded");
                !s.ok()) {
                return s;
            }
            if (auto s =
                    check_option_field_magnitude(request->years_to_expiry(), "years_to_expiry",
                                                  kMaxOptionYearsToExpiry, "100 years");
                !s.ok()) {
                return s;
            }
            if (auto s = check_option_field_magnitude(request->lambda(), "lambda",
                                                       kMaxOptionLambda,
                                                       "10 (the engine's own default is ~1.2247)");
                !s.ok()) {
                return s;
            }
            if (auto s = check_double_magnitude(request->spot(), "spot"); !s.ok()) return s;
            if (auto s = check_double_magnitude(request->strike(), "strike"); !s.ok()) return s;
            // The joint check: even with every field above individually
            // bounded, `steps` itself carries no ceiling here, and it enters
            // the SAME exponent -- see check_tree_exponent_safe's own
            // comment.
            if (auto s = check_tree_exponent_safe(request->volatility(), request->lambda(),
                                                  request->years_to_expiry(), request->steps(),
                                                  "PriceOptionTree");
                !s.ok()) {
                return s;
            }

            const auto exercise = request->exercise_type();
            if (exercise == sensen::finance::BERMUDAN && request->bermudan_dates_size() == 0) {
                return Status(
                    grpc::StatusCode::INVALID_ARGUMENT,
                    "a Bermudan option needs bermudan_dates; without them it is European");
            }
            // A Bermudan exercise date outside (0, years_to_expiry] can
            // never be within half a step of any real backward-induction
            // time (is_bermudan_exercise_time only ever compares against
            // t = j*dt for j in [0, steps-1], i.e. times in
            // [0, years_to_expiry)), so it silently contributes nothing:
            // the option prices exactly as European while still being
            // labelled Bermudan on the wire, with no error to tell the
            // caller their date list did not do what they asked. A date
            // list that is entirely out of range is the extreme case of
            // this and is otherwise invisible, since it is non-empty and so
            // passes the guard directly above. Refused here instead, one
            // date list validated as a whole rather than
            // accepted-then-silently-ignored member by member. (This also
            // catches a NaN date: NaN compares false against "> 0.0", so it
            // fails the same test.)
            // is_bermudan_exercise_time (options.cppm) only ever compares a
            // backward-induction time t=j*dt, j in [0, steps-1] -- i.e.
            // t in [0, years_to_expiry - dt] -- against each date, with a
            // +/-dt/2 match window. The largest matchable time is therefore
            // (steps-1)*dt + dt/2 == years_to_expiry - dt/2, strictly less
            // than years_to_expiry itself. A date in the half-open band
            // [years_to_expiry - dt/2, years_to_expiry) therefore satisfies
            // the (0, years_to_expiry] guard directly above -- it is
            // neither rejected by that guard nor equal to years_to_expiry
            // -- yet STILL can never match any j*dt: the identical
            // silent-mislabelling defect the guard above exists to close,
            // just in a narrower band right at the far edge of the option's
            // life. years_to_expiry itself is deliberately exempt: a date
            // exactly AT expiry does not need to match a backward-induction
            // step at all -- the tree's own final-step payoff already
            // implements exercise-at-expiry independently of
            // is_bermudan_exercise_time (proven by the existing "Bermudan
            // with a single date at expiry == European, bit-for-bit" test),
            // so it is never silently ignored the way an interior dead-band
            // date is. Reproduced directly: steps=200, years_to_expiry=1.0
            // (dt=0.005, dead band = [0.9975, 1.0)) -- a Bermudan date of
            // 0.999 priced BIT-IDENTICAL to plain European (6.200231 ==
            // 6.200231) despite passing every other guard, while a date of
            // 0.5 (well inside the matchable range) genuinely changed the
            // price (6.662411).
            if (exercise == sensen::finance::BERMUDAN) {
                const double dt =
                    request->years_to_expiry() / static_cast<double>(request->steps());
                const double dead_band_start = request->years_to_expiry() - dt * 0.5;
                for (const double d : request->bermudan_dates()) {
                    if (!(d > 0.0) || d > request->years_to_expiry()) {
                        return Status(
                            grpc::StatusCode::INVALID_ARGUMENT,
                            "bermudan_dates must all fall in (0, years_to_expiry]; got " +
                                std::to_string(d) + " against years_to_expiry=" +
                                std::to_string(request->years_to_expiry()) +
                                " -- a date outside that range never matches a real "
                                "backward-induction step and would silently price as European");
                    }
                    if (d != request->years_to_expiry() && d >= dead_band_start) {
                        return Status(
                            grpc::StatusCode::INVALID_ARGUMENT,
                            "bermudan_dates entry " + std::to_string(d) +
                                " cannot be represented at steps=" +
                                std::to_string(request->steps()) +
                                " -- it falls within half a step of years_to_expiry (" +
                                std::to_string(request->years_to_expiry()) +
                                ") without being exactly years_to_expiry, so it can never "
                                "match a real backward-induction time and would silently "
                                "price as European; use years_to_expiry exactly for a date "
                                "at expiry, or raise steps so this date has its own step");
                    }
                }
            }

            if (exercise == sensen::finance::AMERICAN) ex = sensen::ExerciseType::American;
            if (exercise == sensen::finance::BERMUDAN) ex = sensen::ExerciseType::Bermudan;

            dates.assign(request->bermudan_dates().begin(), request->bermudan_dates().end());
            avg_states = (request->averaging_states() > 0) ? request->averaging_states() : 50;
            // sqrt(1.5), the spacing that makes the middle branch
            // probability 1/3.
            lambda = (request->lambda() > 0.0) ? request->lambda() : 1.224744871391589;
            asian = asian_type_of(request->asian_type());
            // options.cppm's Asian branch discretizes each node's
            // running-average range into a grid of averaging_states values,
            // and divides by (averaging_states - 1) to size the grid step
            // (its terminal-state init at line ~175 and, far more
            // dangerously, its interpolate() helper at line ~214). At
            // averaging_states=1 that divisor is exactly 0. Reproduced
            // directly: PriceOptionTree{spot=100, strike=100, rate=0.05,
            // volatility=0.2, years_to_expiry=1, steps=60,
            // asian_type=AVERAGE_PRICE, averaging_states=1} killed the
            // engine process outright (no error in the log -- an abrupt
            // SIGSEGV-class termination, not a thrown exception this file
            // could catch). The mechanism: the resulting NaN average is
            // converted to an int as an interpolation grid index (int(NaN)
            // is undefined behaviour), and that near-arbitrary index is
            // then used to subscript a std::vector<double> via operator[]
            // with no bounds check. Every other anonymous caller of this
            // public, no-API-key-required RPC could reach the same crash,
            // taking down the calculator and both assistant services
            // sharing this process with it. Refused here, before the
            // pricer is ever called. (averaging_states<=0 does not reach
            // this: it is defaulted to 50 above, same as before this fix.)
            if (asian != sensen::AsianType::None && avg_states < 2) {
                return Status(grpc::StatusCode::INVALID_ARGUMENT,
                              "averaging_states must be at least 2 for an Asian option (it "
                              "discretizes a continuous running-average range into a grid of "
                              "that many states; at 1 the grid has no width)");
            }
            return Status::OK;
        };

        const auto dispatch = [&]() -> Status {
            try {
                r = sensen::price_option_double(
                    request->spot(), request->strike(), request->rate(), request->volatility(),
                    request->years_to_expiry(), request->steps(),
                    option_type_of(request->option_type()), ex, dates, asian, avg_states, lambda);
                return Status::OK;
            } catch (const std::exception& e) {
                return Status(grpc::StatusCode::INVALID_ARGUMENT, e.what());
            }
        };

        const auto check_response = [&]() -> Status {
            // Defence in depth -- see require_response_finite's own comment.
            return require_response_finite(
                {{r.value, "value"}, {r.delta, "delta"}, {r.gamma, "gamma"}, {r.theta, "theta"}},
                "PriceOptionTree");
        };

        const auto respond = [&]() {
            response->set_value(r.value);
            response->set_delta(r.delta);
            response->set_gamma(r.gamma);
            response->set_theta(r.theta);
        };

        return run_lifecycle(validate, dispatch, check_response, respond);
    }

    auto PriceBlackScholes(ServerContext* context, const sensen::finance::BlackScholesRequest* request,
                           sensen::finance::BlackScholesResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("PriceBlackScholes", quota::cost_default());

        sensen::BlackScholesResult r{};

        const auto validate = [&]() -> Status {
            // Unlike PriceOptionTree (fixed in the commit this file's
            // finance audit continues from), this RPC had NO input
            // validation at all -- not even a positivity check.
            // price_black_scholes (options.cppm) divides by (volatility *
            // sqrt(years_to_expiry)) to form d1/d2 with no guard of its
            // own; spot/strike feed std::log(spot/strike). A zero or NaN
            // volatility (division by exactly 0), a non-positive
            // spot/strike (log of <=0 is NaN), or a bare NaN in any field
            // all reach Status::OK carrying NaN/Infinity in value and every
            // Greek. Reproduced directly:
            // {spot=100,strike=100,rate=0.05,volatility=0,years_to_expiry=1}
            // returns value=4.877058 (a plausible number) but gamma=NaN
            // (0.0/0.0) in the SAME response; {spot=NaN, ...} returns
            // value=NaN outright. years_to_expiry<=0 is deliberately NOT
            // rejected here: price_black_scholes has its own explicit T<=0
            // branch returning the intrinsic value with every Greek at
            // exactly 0 -- a real, intentional answer for an
            // expired/expiring option, not a defect this guard should
            // refuse.
            if (auto s = require_finite(request->spot(), "spot"); !s.ok()) return s;
            if (auto s = require_finite(request->strike(), "strike"); !s.ok()) return s;
            if (auto s = require_finite(request->rate(), "rate"); !s.ok()) return s;
            if (auto s = require_finite(request->volatility(), "volatility"); !s.ok()) return s;
            if (auto s = require_finite(request->years_to_expiry(), "years_to_expiry"); !s.ok()) {
                return s;
            }
            if (request->spot() <= 0.0 || request->strike() <= 0.0 ||
                request->volatility() <= 0.0) {
                return Status(grpc::StatusCode::INVALID_ARGUMENT,
                              "spot, strike and volatility must all be positive");
            }
            // -- Magnitude guards: PriceBlackScholes's own overflow path is
            // narrower than PriceOptionTree's (normal_cdf/normal_pdf,
            // options. cppm, saturate gracefully for a huge but finite
            // d1/d2 via erf -- volatility alone up to 1e10 was measured NOT
            // to produce a non-finite value here), but rate is not: the
            // discount factor exp(-rate*years_to_expiry) overflows to
            // +Infinity for a large enough NEGATIVE rate, and that Infinity
            // times a normal_cdf(d2) that has itself saturated to exactly
            // 0.0 is a genuine 0*Infinity -> NaN. Reproduced directly:
            // {spot=100,strike=100,rate=-1e10, volatility=0.2,
            // years_to_expiry=1} returned Status::OK with value=-nan. The
            // SAME per-field bounds as PriceOptionTree are applied here
            // anyway (not just rate) for a consistent API surface across
            // the four option-pricing RPCs -- see
            // check_option_field_magnitude's own comment for why each
            // bound is what it is. --
            if (auto s = check_option_field_magnitude(request->volatility(), "volatility",
                                                       kMaxOptionVolatility, "1000% annualised");
                !s.ok()) {
                return s;
            }
            if (auto s = check_option_field_magnitude(request->rate(), "rate", kMaxOptionRate,
                                                       "+/-500% continuously compounded");
                !s.ok()) {
                return s;
            }
            if (auto s =
                    check_option_field_magnitude(request->years_to_expiry(), "years_to_expiry",
                                                  kMaxOptionYearsToExpiry, "100 years");
                !s.ok()) {
                return s;
            }
            if (auto s = check_double_magnitude(request->spot(), "spot"); !s.ok()) return s;
            if (auto s = check_double_magnitude(request->strike(), "strike"); !s.ok()) return s;
            return Status::OK;
        };

        const auto dispatch = [&]() -> Status {
            r = sensen::price_black_scholes(request->spot(), request->strike(), request->rate(),
                                            request->volatility(), request->years_to_expiry(),
                                            option_type_of(request->option_type()));
            return Status::OK;
        };

        const auto check_response = [&]() -> Status {
            // Defence in depth -- see require_response_finite's own comment.
            return require_response_finite({{r.value, "value"},
                                            {r.delta, "delta"},
                                            {r.gamma, "gamma"},
                                            {r.theta, "theta"},
                                            {r.vega, "vega"},
                                            {r.rho, "rho"},
                                            {r.vanna, "vanna"},
                                            {r.volga, "volga"},
                                            {r.charm, "charm"},
                                            {r.color, "color"},
                                            {r.speed, "speed"}},
                                            "PriceBlackScholes");
        };

        const auto respond = [&]() {
            response->set_value(r.value);
            response->set_delta(r.delta);
            response->set_gamma(r.gamma);
            response->set_theta(r.theta);
            response->set_vega(r.vega);
            response->set_rho(r.rho);
            response->set_vanna(r.vanna);
            response->set_volga(r.volga);
            response->set_charm(r.charm);
            response->set_color(r.color);
            response->set_speed(r.speed);
        };

        return run_lifecycle(validate, dispatch, check_response, respond);
    }

    auto PriceOptionMonteCarlo(ServerContext* context, const sensen::finance::MonteCarloRequest* request,
                               sensen::finance::DoubleResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("PriceOptionMonteCarlo",
               quota::cost_monte_carlo(request->paths(), request->steps()));

        double value = 0.0;

        const auto validate = [&]() -> Status {
            if (request->paths() <= 0 || request->steps() <= 0) {
                return Status(grpc::StatusCode::INVALID_ARGUMENT,
                              "paths and steps must be positive");
            }
            // Same gap as PriceBlackScholes above:
            // spot/strike/rate/volatility/ years_to_expiry were never
            // checked at all. price_option_monte_carlo (options.cppm)
            // computes dt=T/steps, drift=(r-0.5*sigma^2)*dt,
            // vol=sigma*sqrt(dt) with no guard -- a negative
            // years_to_expiry makes dt negative and vol=sqrt(negative)=NaN,
            // propagating NaN through every simulated path and back out as
            // Status::OK. Unlike PriceBlackScholes, T==0 is not a
            // meaningful "priced at expiry" case for a path simulation
            // (dt=0 degenerates every path to S0, silently mispricing
            // rather than erroring), so years_to_expiry is held to the same
            // strict ">0" bar as spot/strike/volatility here.
            if (auto s = require_finite(request->spot(), "spot"); !s.ok()) return s;
            if (auto s = require_finite(request->strike(), "strike"); !s.ok()) return s;
            if (auto s = require_finite(request->rate(), "rate"); !s.ok()) return s;
            if (auto s = require_finite(request->volatility(), "volatility"); !s.ok()) return s;
            if (auto s = require_finite(request->years_to_expiry(), "years_to_expiry"); !s.ok()) {
                return s;
            }
            if (request->spot() <= 0.0 || request->strike() <= 0.0 ||
                request->volatility() <= 0.0 || request->years_to_expiry() <= 0.0) {
                return Status(
                    grpc::StatusCode::INVALID_ARGUMENT,
                    "spot, strike, volatility and years_to_expiry must all be positive");
            }
            // -- Magnitude guards: price_option_monte_carlo's per-step
            // update is S *= exp(drift + vol*Z), drift=(rate-0.5*
            // volatility^2)*dt, vol=volatility*sqrt(dt) -- an absurd
            // volatility or rate (or a huge years_to_expiry combined with a
            // small steps, which makes dt large) overflows that exp() the
            // same way PriceOptionTree's tree-node exponent does.
            // Reproduced directly: {volatility=0.2, years_to_expiry=1e6}
            // and {rate=1e5, years_to_expiry=1} both returned Status::OK
            // with value=nan. Same bounds as the sibling option-pricing
            // RPCs -- see check_option_field_magnitude's own comment. --
            if (auto s = check_option_field_magnitude(request->volatility(), "volatility",
                                                       kMaxOptionVolatility, "1000% annualised");
                !s.ok()) {
                return s;
            }
            if (auto s = check_option_field_magnitude(request->rate(), "rate", kMaxOptionRate,
                                                       "+/-500% continuously compounded");
                !s.ok()) {
                return s;
            }
            if (auto s =
                    check_option_field_magnitude(request->years_to_expiry(), "years_to_expiry",
                                                  kMaxOptionYearsToExpiry, "100 years");
                !s.ok()) {
                return s;
            }
            if (auto s = check_double_magnitude(request->spot(), "spot"); !s.ok()) return s;
            if (auto s = check_double_magnitude(request->strike(), "strike"); !s.ok()) return s;
            // paths and steps together are the O(paths*steps) work
            // price_option_monte_carlo actually does (a TBB parallel_reduce
            // over paths, each walking steps RNG draws);
            // quota::cost_monte_carlo(paths, steps) prices that work
            // proportionally but never refuses a SINGLE request whose own
            // product is large enough to run for an unreasonable time
            // regardless of how it is billed -- the same DoS shape
            // check_period_count_ceiling/check_portfolio_size_ceiling close
            // elsewhere in this file for other caller-chosen work sizes.
            // 100 million path-steps is already far beyond any real Monte
            // Carlo option pricer's needs (10,000 paths x 252 daily steps =
            // 2.52M is already a generous real-world figure).
            if (static_cast<long long>(request->paths()) *
                    static_cast<long long>(request->steps()) >
                100'000'000LL) {
                return Status(grpc::StatusCode::INVALID_ARGUMENT,
                              "paths * steps exceeds 100,000,000; no real Monte Carlo option "
                              "pricer needs more path-steps than that");
            }
            return Status::OK;
        };

        const auto dispatch = [&]() -> Status {
            const int threads = (request->num_threads() > 0) ? request->num_threads() : -1;
            value = sensen::price_option_monte_carlo(
                request->spot(), request->strike(), request->rate(), request->volatility(),
                request->years_to_expiry(), request->paths(), request->steps(),
                option_type_of(request->option_type()), asian_type_of(request->asian_type()),
                threads);
            return Status::OK;
        };

        const auto check_response = [&]() -> Status {
            // Defence in depth -- see require_response_finite's own comment.
            return require_response_finite({{value, "value"}}, "PriceOptionMonteCarlo");
        };

        const auto respond = [&]() { response->set_value(value); };

        return run_lifecycle(validate, dispatch, check_response, respond);
    }

    auto ComputeProbabilityTree(ServerContext* context, const sensen::finance::ProbabilityTreeRequest* request,
                                sensen::finance::ProbabilityTreeResponse* response)
        -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputeProbabilityTree", quota::cost_probability_tree(request->steps()));

        sensen::TrinomialTreeInfo t{};

        const auto validate = [&]() -> Status {
            if (request->steps() <= 0 || request->years_to_expiry() <= 0.0 ||
                request->volatility() <= 0.0) {
                return Status(grpc::StatusCode::INVALID_ARGUMENT,
                              "steps, years_to_expiry and volatility must be positive");
            }
            // The "<= 0.0" checks above do not catch NaN (NaN <= 0.0 is
            // false), same class as PriceBlackScholes/PriceOptionMonteCarlo
            // above and PriceOptionTree before this file's finance-audit
            // pass. rate has no positivity check of its own (a negative
            // rate is real) but must still be finite --
            // calculate_probability_tree feeds it directly into a per-step
            // exp(rate*dt) growth factor.
            if (auto s = require_finite(request->rate(), "rate"); !s.ok()) return s;
            if (auto s = require_finite(request->volatility(), "volatility"); !s.ok()) return s;
            if (auto s = require_finite(request->years_to_expiry(), "years_to_expiry"); !s.ok()) {
                return s;
            }
            // lambda had no finiteness check of any kind before this fix --
            // same gap, same reasoning, as PriceOptionTree's own lambda
            // check above (a caller-sent +Infinity survives "(lambda() >
            // 0.0)" and becomes the effective lambda directly).
            if (auto s = require_finite(request->lambda(), "lambda"); !s.ok()) return s;

            // -- Magnitude guards: calculate_probability_tree (options.cppm)
            // shares PriceOptionTree's identical u=exp(lambda*sigma*sqrt(dt))
            // driver, so it overflows the same way for the same reason --
            // reproduced directly: {volatility=1e10} and
            // {years_to_expiry=1e10} both returned Status::OK with
            // non-finite entries in stock_prices. Same bounds as
            // PriceOptionTree -- see check_option_field_magnitude's own
            // comment. --
            if (auto s = check_option_field_magnitude(request->volatility(), "volatility",
                                                       kMaxOptionVolatility, "1000% annualised");
                !s.ok()) {
                return s;
            }
            if (auto s = check_option_field_magnitude(request->rate(), "rate", kMaxOptionRate,
                                                       "+/-500% continuously compounded");
                !s.ok()) {
                return s;
            }
            if (auto s =
                    check_option_field_magnitude(request->years_to_expiry(), "years_to_expiry",
                                                  kMaxOptionYearsToExpiry, "100 years");
                !s.ok()) {
                return s;
            }
            if (auto s = check_option_field_magnitude(request->lambda(), "lambda",
                                                       kMaxOptionLambda,
                                                       "10 (the engine's own default is ~1.2247)");
                !s.ok()) {
                return s;
            }
            // The joint check, identical reasoning to PriceOptionTree's own:
            // steps carries no ceiling here either.
            if (auto s = check_tree_exponent_safe(request->volatility(), request->lambda(),
                                                  request->years_to_expiry(), request->steps(),
                                                  "ComputeProbabilityTree");
                !s.ok()) {
                return s;
            }
            return Status::OK;
        };

        const auto dispatch = [&]() -> Status {
            const double lambda =
                (request->lambda() > 0.0) ? request->lambda() : 1.224744871391589;
            t = sensen::calculate_probability_tree(request->rate(), request->volatility(),
                                                    request->years_to_expiry(), request->steps(),
                                                    lambda);
            return Status::OK;
        };

        const auto check_response = [&]() -> Status {
            // Defence in depth -- see require_response_finite's own comment.
            // (calculate_probability_tree returns two repeated fields
            // rather than a handful of named scalars, so this reuses
            // require_all_finite -- the same helper the REQUEST-side
            // repeated cash-flow fields use elsewhere in this file --
            // rather than require_response_finite's named-field list, which
            // does not fit a variable-length array.)
            if (auto s = require_all_finite(t.stock_prices, "ComputeProbabilityTree stock_prices");
                !s.ok()) {
                return s;
            }
            return require_all_finite(t.state_probabilities,
                                      "ComputeProbabilityTree state_probabilities");
        };

        const auto respond = [&]() {
            for (const double p : t.stock_prices) response->add_stock_prices(p);
            for (const double p : t.state_probabilities) response->add_state_probabilities(p);
            response->set_steps(t.steps);
        };

        return run_lifecycle(validate, dispatch, check_response, respond);
    }

    // -- Portfolio -----------------------------------------------------------

    auto ComputePortfolioStats(ServerContext* context, const sensen::finance::PortfolioStatsRequest* request,
                               sensen::finance::PortfolioStatsResponse* response)
        -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("ComputePortfolioStats",
               quota::cost_portfolio_stats(request->portfolio_returns_size()));
        if (request->portfolio_returns_size() == 0) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT, "portfolio_returns is empty");
        }
        const bool benchmarked = request->market_returns_size() > 0;
        if (benchmarked && request->market_returns_size() != request->portfolio_returns_size()) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "market_returns has " + std::to_string(request->market_returns_size()) +
                              " entries but portfolio_returns has " +
                              std::to_string(request->portfolio_returns_size()) +
                              "; a benchmark must cover the same periods");
        }
        // portfolio_returns/market_returns were unbounded beyond the length
        // check above -- calculate_portfolio_stats (portfolio.cppm) is
        // O(n log n) for the historical VaR/CVaR sort, work that
        // quota::cost_portfolio_stats(n) prices proportionally but never
        // refuses outright for a single absurdly long series. A NaN
        // anywhere in either series also propagates through every one of
        // the eighteen statistics in the response -- the per-element case a
        // whole-vector emptiness/length check cannot catch.
        if (auto s = check_series_length_ceiling(request->portfolio_returns_size(),
                                                 "portfolio_returns");
            !s.ok()) {
            return s;
        }
        if (auto s = require_all_finite(request->portfolio_returns(), "portfolio_returns");
            !s.ok()) {
            return s;
        }
        if (auto s = require_all_finite(request->market_returns(), "market_returns"); !s.ok()) {
            return s;
        }
        if (auto s = require_finite(request->risk_free_rate(), "risk_free_rate"); !s.ok()) {
            return s;
        }

        const std::vector<double> pr(request->portfolio_returns().begin(),
                                     request->portfolio_returns().end());
        const std::vector<double> mr(request->market_returns().begin(),
                                     request->market_returns().end());
        const auto s = sensen::calculate_portfolio_stats(pr, mr, request->risk_free_rate());

        response->set_sharpe_ratio(s.sharpe_ratio);
        response->set_sortino_ratio(s.sortino_ratio);
        response->set_treynor_ratio(s.treynor_ratio);
        response->set_beta(s.beta);
        response->set_alpha(s.alpha);
        response->set_max_drawdown(s.max_drawdown);
        response->set_var_historical_95(s.var_historical_95);
        response->set_var_historical_99(s.var_historical_99);
        response->set_cvar_historical_95(s.cvar_historical_95);
        response->set_cvar_historical_99(s.cvar_historical_99);
        response->set_var_parametric_95(s.var_parametric_95);
        response->set_var_parametric_99(s.var_parametric_99);
        response->set_cvar_parametric_95(s.cvar_parametric_95);
        response->set_cvar_parametric_99(s.cvar_parametric_99);
        response->set_omega_ratio(s.omega_ratio);
        response->set_calmar_ratio(s.calmar_ratio);
        response->set_information_ratio(s.information_ratio);
        response->set_tracking_error(s.tracking_error);
        // Lets a caller tell an absent beta from a measured beta of zero.
        response->set_benchmark_supplied(benchmarked);
        return Status::OK;
    }

    auto OptimizePortfolio(ServerContext* context, const sensen::finance::PortfolioOptimizeRequest* request,
                           sensen::finance::PortfolioOptimizeResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        CHARGE("OptimizePortfolio", quota::cost_portfolio_optimize(request->size()));
        // size was bounded only in SHAPE by read_covariance below (it must
        // agree with expected_returns' length, and covariance must hold
        // exactly size*size entries) -- never in MAGNITUDE.
        // optimize_portfolio (portfolio.cppm) LU-decomposes a size*size
        // matrix, O(size^3) work quota::cost_portfolio_optimize(size) prices
        // proportionally but never refuses outright for a single request.
        if (auto s = check_portfolio_size_ceiling(request->size(), "size"); !s.ok()) return s;
        sensen::MatrixT<double> cov;
        if (auto s = read_covariance(request->size(), request->expected_returns_size(),
                                     request->covariance(), cov);
            !s.ok()) {
            return s;
        }
        // A NaN anywhere in expected_returns/covariance propagates through
        // the LU solve into every weight and stat in the response --
        // optimize_portfolio's own guards (a near-zero weight-sum check,
        // etc.) all compare against NaN, which is always false, so they
        // never catch it either.
        if (auto s = require_all_finite(request->expected_returns(), "expected_returns");
            !s.ok()) {
            return s;
        }
        if (auto s = require_all_finite(request->covariance(), "covariance"); !s.ok()) return s;
        if (auto s = require_finite(request->risk_free_rate(), "risk_free_rate"); !s.ok()) {
            return s;
        }
        const std::vector<double> mu(request->expected_returns().begin(),
                                     request->expected_returns().end());
        const auto r = sensen::optimize_portfolio(mu, cov, request->risk_free_rate(),
                                                  request->max_sharpe());
        if (!r) return fail(r);
        for (const double w : r->weights) response->add_weights(w);
        response->set_expected_return(r->expected_return);
        response->set_volatility(r->volatility);
        response->set_sharpe_ratio(r->sharpe_ratio);
        return Status::OK;
    }

    auto ComputeRiskContributions(ServerContext* context,
                                  const sensen::finance::RiskContributionRequest* request,
                                  sensen::finance::RiskContributionResponse* response)
        -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        // Same O(n^3) covariance solve as OptimizePortfolio, so the same price.
        CHARGE("ComputeRiskContributions", quota::cost_portfolio_optimize(request->size()));
        // Same unbounded-magnitude gap as OptimizePortfolio above, on the
        // identical size*size covariance shape.
        if (auto s = check_portfolio_size_ceiling(request->size(), "size"); !s.ok()) return s;
        sensen::MatrixT<double> cov;
        if (auto s = read_covariance(request->size(), request->weights_size(), request->covariance(),
                                     cov);
            !s.ok()) {
            return s;
        }
        // A NaN anywhere in weights/covariance propagates through the
        // marginal-risk matrix-vector product into every contribution.
        if (auto s = require_all_finite(request->weights(), "weights"); !s.ok()) return s;
        if (auto s = require_all_finite(request->covariance(), "covariance"); !s.ok()) return s;
        const std::vector<double> w(request->weights().begin(), request->weights().end());
        const auto r = sensen::calculate_risk_contributions(w, cov);
        if (!r) return fail(r);
        for (const double c : *r) response->add_contributions(c);
        return Status::OK;
    }

  private:
    /**
     * Runs one call through the shared FinanceRequestLifecycle graph:
     * validate -> dispatch -> check response -> respond, short-circuiting on
     * the first failure -- exactly the sequence of `if (!s.ok()) return s;`
     * checks a hand-written RPC body already used to run directly, now made
     * explicit as the graph's own structure. Only the four option-pricing
     * RPCs call this; every other RPC in this file is unchanged plain C++.
     *
     * Mirrors CalculatorServiceImpl::CalculateStrategy's own two-tier
     * postcondition, for the identical reason: interpreter.Run() returns
     * void, so success and a silent partial halt (a null action registry, an
     * action that fails without setting status, or an unregistered action
     * id -- see the constructor's own "Action registration by ID" comment)
     * are otherwise indistinguishable from here.
     *
     *   1. ctx->status is checked FIRST. An action that fails sets it AND
     *      returns std::unexpected, which halts the entity before it reaches
     *      Done -- so a validate/dispatch/check-response failure is caught
     *      here and returned with its own exact status code and message,
     *      regardless of whether Done was ever reached.
     *   2. Only once status is OK is "did the entity reach Done" verified.
     *      Reaching Done with a still-OK status but NOT via a failed action
     *      is exactly the silent-halt trap: proof that the graph was never
     *      actually wired up, not a request problem.
     */
    [[nodiscard]] auto run_lifecycle(std::function<Status()> validate,
                                     std::function<Status()> dispatch,
                                     std::function<Status()> check_response,
                                     std::function<void()> respond) const -> Status {
        if (!lifecycle_graph_) {
            return Status(grpc::StatusCode::INTERNAL, "Execution graph not initialized");
        }

        auto ctx = std::make_shared<FinanceLifecycleState>();
        ctx->validate = std::move(validate);
        ctx->dispatch = std::move(dispatch);
        ctx->check_response = std::move(check_response);
        ctx->respond = std::move(respond);

        sgee::runtime::EngineContext<FinanceEntity> engine;
        std::vector<FinanceEntity> entities{ctx};
        engine.Load(entities);

        // Sequential: each request is a single entity, so batch parallelism
        // buys nothing -- same reasoning as calculator_service.cpp's own
        // CalculateStrategy.
        sgee::runtime::Interpreter<FinanceEntity> interpreter(
            lifecycle_graph_, sgee::runtime::ParallelismLevel::Sequential,
            lifecycle_actions_.get());
        interpreter.Run(engine);

        if (!ctx->status.ok()) return ctx->status;

        const auto& state_ids = engine.GetStateIds();
        if (state_ids.empty() || state_ids[0] != lifecycle_done_node_id_) {
            auto& log = logger::Logger::getInstance();
            const std::string stalled_at =
                state_ids.empty() ? std::string{"<no entity>"}
                                  : std::string{lifecycle_graph_->GetNodeName(state_ids[0])};
            log.error("Finance RPC: graph halted at '{}' instead of reaching Done -- an action "
                      "failed without setting status, an action id was not registered, or the "
                      "action registry was never wired up",
                      stalled_at);
            return Status(grpc::StatusCode::INTERNAL,
                          "Request computation halted before completion (stalled at '" +
                              stalled_at +
                              "'); this is a server-side defect, not a problem with the "
                              "request");
        }
        return Status::OK;
    }

    /**
     * Refuses a non-positive period count where the closed-form annuity math
     * (pmt/pv/fv/ipmt/ppmt) genuinely needs one.
     *
     * periods=0 does not raise inside sensen's own pmt()/pv()/fv() -- every
     * BigDecimal divide-by-zero there resolves through .value_or(BigDecimal(0))
     * to a silent 0, and ipmt() explicitly returns BigDecimal(0) whenever
     * per > nper (which per<=0<nper never is, and per>=1>nper=0 always is).
     * So an absent/zero periods count does not fail loudly on its own; it
     * produces a payment of exactly $0 for a loan that was never described --
     * the same silent-default failure class as an absent rate, just reached
     * through an int32 default instead of an empty string.
     */
    [[nodiscard]] static auto check_periods(int periods, std::string_view field = "periods")
        -> Status {
        if (periods <= 0) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          std::string(field) + " must be positive");
        }
        return Status::OK;
    }

    [[nodiscard]] static auto check_term(int term_months) -> Status {
        if (term_months <= 0) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT, "term_months must be positive");
        }
        // A schedule is one row per period and the engine reserves that many up
        // front. Without a ceiling a caller could ask for a term long enough to
        // exhaust memory on the server, which is a denial of service dressed as
        // a mortgage. 1200 months is a hundred years -- beyond any real loan.
        if (term_months > 1200) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "term_months exceeds 1200 (a hundred years); no real loan runs that long");
        }
        return Status::OK;
    }

    [[nodiscard]] static auto check_dated(const sensen::finance::DatedCashFlowRequest* request)
        -> Status {
        if (request->values_size() != request->dates_size()) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "values has " + std::to_string(request->values_size()) +
                              " entries but dates has " + std::to_string(request->dates_size()) +
                              "; every cash flow needs its own date");
        }
        if (request->values_size() == 0) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT, "no cash flows supplied");
        }
        return Status::OK;
    }

    /**
     * Reads a square covariance matrix sent flat, row-major.
     *
     * Refuses a size that disagrees with the vector it must multiply, and a
     * flat array whose length is not size*size. Reshaping either to fit would
     * produce a number rather than an answer.
     */
    template <typename Repeated>
    [[nodiscard]] static auto read_covariance(int size, int vector_len, const Repeated& flat,
                                              sensen::MatrixT<double>& out) -> Status {
        if (size <= 0) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT, "size must be positive");
        }
        if (vector_len != size) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "size is " + std::to_string(size) + " but the accompanying vector has " +
                              std::to_string(vector_len) + " entries");
        }
        const long long expected = static_cast<long long>(size) * size;
        if (static_cast<long long>(flat.size()) != expected) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "covariance must hold size*size = " + std::to_string(expected) +
                              " entries, got " + std::to_string(flat.size()));
        }
        out = sensen::MatrixT<double>(static_cast<std::size_t>(size),
                                      static_cast<std::size_t>(size));
        for (int i = 0; i < size; ++i) {
            for (int j = 0; j < size; ++j) {
                out(static_cast<std::size_t>(i), static_cast<std::size_t>(j)) =
                    flat.Get(i * size + j);
            }
        }
        return Status::OK;
    }

    [[nodiscard]] auto period_payment(const sensen::finance::PeriodPaymentRequest* request,
                                      sensen::finance::DecimalResponse* response, bool interest)
        -> Status {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        // No charge here: this is the shared body of ComputeInterestPayment
        // and ComputePrincipalPayment, and both charge at their own entry
        // point. Charging again would bill one call twice.
        if (auto s = check_periods(request->periods()); !s.ok()) return s;
        // ipmt/ppmt (financial.cppm) walk a month-by-month balance in a
        // `for (t=1; t<=per; ++t)` loop whenever `per` (request->period(),
        // itself never bounded -- the proto's own contract is merely
        // "outside [1, periods] returns 0", which does not stop `period`
        // from equalling a huge `periods`) falls in [1, periods]. Neither
        // field had an upper bound before this guard, so a caller could ask
        // for an arbitrarily long walk for a flat quota::cost_default()
        // charge. Reproduced directly: period=periods=3,000,000 is accepted
        // today and burns CPU proportional to `period`, unauthenticated.
        // Capping `periods` alone is sufficient (per<=periods always, or the
        // engine's own contract already zeroes it), matching the DoS class
        // this file already guards elsewhere (check_payments_per_year,
        // check_term).
        if (auto s = check_period_count_ceiling(request->periods(), "periods"); !s.ok()) return s;
        REQUIRE_DECIMAL(rate, request->rate(), "rate");
        REQUIRE_DECIMAL(pv_v, request->present_value(), "present_value");
        READ_DECIMAL(fv_v, request->future_value(), "future_value");
        const int timing = timing_of(request->timing());
        const auto r = interest ? sensen::ipmt(rate, request->period(), request->periods(), pv_v,
                                               fv_v, timing)
                                : sensen::ppmt(rate, request->period(), request->periods(), pv_v,
                                               fv_v, timing);
        response->set_value(r.to_string());
        return Status::OK;
    }
};

}  // namespace

auto RegisterFinanceService(grpc::ServerBuilder& builder) -> void {
    // Static storage duration for the same reason the calculator service uses
    // it: gRPC's RegisterService takes the address and does not take ownership,
    // so the service must outlive both the builder and the server.
    static FinanceServiceImpl service;
    builder.RegisterService(&service);
    logger::Logger::getInstance().info("Registered {} on the same port as the calculator",
                                       sensen::finance::Finance::service_full_name());

    // Force the enforcer to load its policy NOW rather than on the first RPC.
    // Otherwise "are quotas on?" is a question only traffic can answer, and a
    // policy that failed to parse stays silent until the moment it matters.
    const bool on = quota::QuotaEnforcer::instance().enabled();
    logger::Logger::getInstance().info(
        "Quota enforcement is {}", on ? "ON" : "OFF (set QUOTA_POLICY to enable)");
}

}  // namespace options_calculator::finance
