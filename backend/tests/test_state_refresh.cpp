/**
 * Gates for the Census ACS state-assumptions refresh.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * NO EXTERNAL TEST FRAMEWORK. Rule 39 forbids GTest and Catch2 outright, and
 * the tree follows it: across every file in tests/ there is not one include of
 * an external framework. The harness is a `check()` and two counters, the same
 * shape `test_mortgage_verification.cpp` and `test_finance_service_validation.cpp`
 * use, and a non-zero exit is the failure signal ctest reads.
 *
 * WHAT THIS COVERS, and why it is the validator rather than the RPC:
 * `validate_acs_row` is split out of the service for the same reason
 * `validate_closing_costs` is -- a validator reachable only through a network
 * call is one nobody exercises at its boundaries. Every refusal below is the
 * SAME refusal a live run would produce, because it is the same function.
 *
 * The bounds are not invented here. They come from the handoff contract and
 * are duplicated, deliberately, in migration 07's CHECK constraints -- so a
 * value that slips past this validator is still refused by Postgres. These
 * tests pin the C++ half; section 0 of the migration pins the SQL half.
 */
#include <cstdio>
#include <string>

import std;
import state_refresh;

namespace {

int g_checks = 0;
int g_failures = 0;

auto check(bool condition, const std::string& what) -> void {
    ++g_checks;
    if (condition) {
        std::printf("  PASS: %s\n", what.c_str());
    } else {
        std::printf("  FAIL: %s\n", what.c_str());
        ++g_failures;
    }
}

auto section(const char* title) -> void { std::printf("\n=== %s ===\n", title); }

}  // namespace

// COMPILE-TIME proof that the constexpr on these helpers is real. A `constexpr`
// that never gets constant-evaluated is decoration: the compiler accepts it and
// the function still only ever runs at runtime. These fail the BUILD if that
// silently regresses -- for instance if someone reintroduces std::tolower,
// which is locale-dependent and not constant-evaluable.
static_assert(state_refresh::detail::trim("  texas  ") == "texas");
static_assert(state_refresh::detail::is_not_a_state("Puerto Rico"));
static_assert(!state_refresh::detail::is_not_a_state("Texas"));

auto main() -> int {
    namespace sr = state_refresh;

    std::printf("State assumptions refresh gates\n");

    // =======================================================================
    section("1. A well-formed row is accepted, and the rate is DERIVED");
    // =======================================================================
    // Alabama's real 2023 ACS figures, taken from a live response: price
    // 195100, rent 963, taxes 738. 738/195100*100 = 0.3783..., which rounds to
    // 0.38. Asserted to the digit rather than to a range, because the rounding
    // is what lands in a numeric column and a half-up slip would be invisible
    // in any looser check.
    {
        const auto row = sr::validate_acs_row("Alabama", "195100", "963", "738");
        check(row.has_value(), "a well-formed ACS row is accepted");
        if (row.has_value()) {
            check(row->slug == "alabama", "NAME is slugified: Alabama -> alabama");
            check(row->median_price == "195100.00", "median_price is a 2dp decimal string");
            check(row->median_rent == "963.00", "median_rent is a 2dp decimal string");
            check(row->property_tax_rate == "0.38",
                  "property_tax_rate is DERIVED as taxes/price*100, rounded to 2dp "
                  "(738/195100*100 = 0.3783 -> 0.38)");
        }
    }
    {
        // Multi-word states are where a slug convention usually breaks, and the
        // slug is the JOIN KEY -- a wrong one silently updates nothing, which
        // looks identical to a state the ACS did not return.
        const auto row = sr::validate_acs_row("New Hampshire", "428500", "1553", "6520");
        check(row.has_value() && row->slug == "new-hampshire",
              "a multi-word NAME slugifies with hyphens, matching migration 07's seed");
    }

    // =======================================================================
    section("2. ACS sentinels are MISSING, not values");
    // =======================================================================
    // The ACS encodes 'not available' as a large negative rather than as null.
    // Treating any non-positive as missing covers every sentinel without
    // hardcoding the list -- and a zero median price is not a measurement
    // either, so it must fail the same way.
    {
        const auto row = sr::validate_acs_row("Nowhere", "-666666666", "963", "738");
        check(!row.has_value(), "the -666666666 sentinel is refused, not read as a price");
    }
    {
        const auto row = sr::validate_acs_row("Nowhere", "195100", "963", "0");
        check(!row.has_value(),
              "a zero tax figure is refused rather than deriving a 0.00 rate -- the rate "
              "would be structurally valid and factually invented");
    }
    {
        const auto row = sr::validate_acs_row("Nowhere", "195100", "", "738");
        check(!row.has_value(), "an empty field is refused");
    }
    {
        const auto row = sr::validate_acs_row("Nowhere", "195100", "not-a-number", "738");
        check(!row.has_value(), "a non-numeric field is refused rather than parsed to zero");
    }

    // =======================================================================
    section("3. Out-of-bounds values are REFUSED, never clamped");
    // =======================================================================
    // Clamping is a fabricated default wearing a validator's clothes: it
    // produces a number nobody measured, and nothing downstream can tell it
    // from one that was. Each bound is checked from BOTH sides so a
    // one-sided comparison cannot pass.
    {
        const auto lo = sr::validate_acs_row("Nowhere", "49999", "963", "500");
        const auto hi = sr::validate_acs_row("Nowhere", "3000001", "963", "30000");
        check(!lo.has_value() && !hi.has_value(),
              "median_price outside 50000..3000000 is refused at BOTH ends");
    }
    {
        const auto lo = sr::validate_acs_row("Nowhere", "195100", "299", "738");
        const auto hi = sr::validate_acs_row("Nowhere", "195100", "8001", "738");
        check(!lo.has_value() && !hi.has_value(),
              "median_rent outside 300..8000 is refused at BOTH ends");
    }
    {
        // rate = 20/195100*100 = 0.01, below the 0.05 floor.
        const auto lo = sr::validate_acs_row("Nowhere", "195100", "963", "20");
        // rate = 100000/195100*100 = 51.3, far above the 4 ceiling.
        const auto hi = sr::validate_acs_row("Nowhere", "195100", "963", "100000");
        check(!lo.has_value() && !hi.has_value(),
              "a derived property_tax_rate outside 0.05..4 is refused at BOTH ends");
    }
    {
        // The refusal must NAME the field, because the run logs it per state and
        // "rejected Wyoming" without a reason is an alert nobody can action.
        const auto row = sr::validate_acs_row("Nowhere", "49999", "963", "500");
        check(!row.has_value() && row.error().find("median_price") != std::string::npos,
              "the refusal names the offending field");
    }

    // =======================================================================
    section("4. Boundary values are INSIDE the bounds");
    // =======================================================================
    // A bound stated as inclusive must behave inclusively. Off-by-one here
    // would silently reject a legitimate state at the extreme, and the run
    // would still report success with 49 states.
    {
        const auto lo = sr::validate_acs_row("Nowhere", "50000", "300", "1000");
        check(lo.has_value(), "exactly 50000 / 300 is accepted -- the bounds are inclusive");
    }

    // =======================================================================
    section("5. Candidate vintages are DERIVED from the clock, not hardcoded");
    // =======================================================================
    // A literal {2023, 2022} is a line that silently rots every January, and
    // whose failure looks like an upstream outage rather than a stale
    // constant.
    {
        const auto years = sr::candidate_years(2026);
        check(!years.empty() && years.front() == 2024,
              "the newest candidate is two years back -- ACS 5-year releases lag ~18 months");
        check(years.size() >= 2 && years[1] == 2023, "the fallback is the year before that");
        const auto later = sr::candidate_years(2030);
        check(!later.empty() && later.front() == 2028,
              "and it moves with the clock: 2030 asks for 2028 first, with no code change");
    }

    // =======================================================================
    section("6. The abort floor is the contract's, not an ad-hoc number");
    // =======================================================================
    check(sr::kMinUsableStates == 40,
          "fewer than 40 usable states aborts the run and keeps existing data -- "
          "stale-but-honest beats fresh-but-wrong");

    // -----------------------------------------------------------------
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
