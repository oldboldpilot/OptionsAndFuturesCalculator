/**
 * PMI cancellation policy: the refactor is inert, and the new knobs do exactly
 * what they say.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * THE FIRST TEST IS THE IMPORTANT ONE AND IT IS A BYTE-IDENTITY GATE, not a
 * tolerance. Extracting the PMI rule from two inline copies into one shared
 * `pmi_for_month` is the kind of change that is obviously correct by reading and
 * occasionally wrong by a digit, and a schedule is 360 rows where a single
 * wrong month is invisible in a total. So the whole schedule is folded into one
 * digest and compared against a constant RECORDED FROM THE CODE BEFORE THE
 * REFACTOR. If the fold changes, something moved, and the test says which
 * scenario rather than merely that a number differs.
 *
 * This lives in THIS repository rather than sensen's, deliberately. sensen is
 * built here with BUILD_TESTS OFF ... FORCE, so a gate written in its tree runs
 * for nobody on this side -- the lesson section 23 of
 * test_finance_service_validation already records, where zero of 99 ctest tests
 * touched an operation that looked covered from three directions.
 */
#include <cstdio>
#include <new>

import std;
import sensen.bigdecimal;
import sensen.financial;

namespace {
int g_checks = 0;
int g_failures = 0;

auto check(bool cond, const std::string& what) -> void {
    ++g_checks;
    std::printf("  %s: %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (!cond) { ++g_failures; }
}

/** A stable fold over every figure a schedule produces. Exact decimal strings
 *  go in, so this cannot agree by rounding the way a tolerance would. */
auto digest(const std::vector<sensen::AmortizationRow>& rows,
            const sensen::MortgageSummary& sum) -> std::string {
    std::uint64_t h = 1469598103934665603ULL;   // FNV-1a offset basis
    const auto absorb = [&h](const std::string& s) {
        for (const unsigned char c : s) {
            h ^= c;
            h *= 1099511628211ULL;
        }
        h ^= '|';
        h *= 1099511628211ULL;
    };
    for (const auto& r : rows) {
        absorb(std::to_string(r.period));
        absorb(r.start_balance.to_string());
        absorb(r.scheduled_payment.to_string());
        absorb(r.extra_payment.to_string());
        absorb(r.interest_paid.to_string());
        absorb(r.principal_paid.to_string());
        absorb(r.pmi_paid.to_string());
        absorb(r.end_balance.to_string());
    }
    absorb(sum.total_principal_paid.to_string());
    absorb(sum.total_interest_paid.to_string());
    absorb(sum.total_pmi_paid.to_string());
    absorb(sum.total_payments_paid.to_string());

    std::string out(16, '0');
    for (int i = 15; i >= 0; --i) {
        out[static_cast<std::size_t>(i)] = "0123456789abcdef"[h & 0xF];
        h >>= 4;
    }
    return out;
}

/** The last month a premium was charged; 0 if none ever was. Directional
 *  assertions are written against this rather than a total, because "PMI ended
 *  sooner" is the claim being tested and a total can move for other reasons. */
auto last_pmi_month(const std::vector<sensen::AmortizationRow>& rows) -> int {
    int last = 0;
    for (const auto& r : rows) {
        if (r.pmi_paid > sensen::BigDecimal(0)) { last = r.period; }
    }
    return last;
}

struct Scenario {
    std::string_view name;
    sensen::BigDecimal loan;
    sensen::BigDecimal rate;
    int term;
    sensen::BigDecimal overpay;
    sensen::BigDecimal pmi_rate;
    sensen::BigDecimal home_value;
};

auto scenarios() -> std::vector<Scenario> {
    using BD = sensen::BigDecimal;
    return {
        // The ordinary case: PMI cancels part-way once 20% is paid off.
        {"716400 @5.97% 30y, 0.6% PMI, 796000 value",
         BD("716400"), BD("0.0597"), 360, BD(0), BD("0.006"), BD("796000")},
        // NO home value. The fallback to the loan amount is a behaviour people
        // depend on without knowing it, so it is pinned.
        {"no home value -> basis falls back to the loan",
         BD("716400"), BD("0.0597"), 360, BD(0), BD("0.006"), BD(0)},
        // Overpayment, which cancels PMI sooner because the test is on the
        // ACTUAL balance rather than the original schedule.
        {"with 250/mo overpayment",
         BD("716400"), BD("0.0597"), 360, BD("250"), BD("0.006"), BD("796000")},
        // No PMI at all: the zero path through the same helper.
        {"no PMI",
         BD("300000"), BD("0.0525"), 360, BD(0), BD(0), BD("400000")},
        // A small deposit on a long term, where PMI runs a long way.
        {"3.5% down, PMI runs long",
         BD("386000"), BD("0.0675"), 360, BD(0), BD("0.0085"), BD("400000")},
    };
}
}  // namespace

auto main() -> int {
    std::printf("1. DIGEST: the schedule is unchanged apart from the money scale\n");
    {
        // Recorded by running this same fold against the code as it stood at
        // 40de144a, BEFORE pmi_for_month existed. A default-constructed policy
        // must reproduce it exactly: 80% of the value at origination, tested on
        // the actual balance.
        //
        // RE-RECORDED 2026-09-23 for the sensen bump to 20c20a44, which widened
        // BigDecimal from 128 to 256 bits and took the money scale from 18
        // decimal places to 38. `digest()` folds `to_string()`, which emits
        // exactly SCALE digits, so EVERY digest here had to move whether or not
        // a single value changed. A digest that must change on a formatting
        // change cannot, on its own, tell you whether the arithmetic changed.
        //
        // So the new constants were NOT accepted because the test went green.
        // They were accepted because the answers were re-verified against
        // INDEPENDENT identities first -- `smoke_client ... finance` at rc=0:
        // schedule closure, the closed-form annuity, bond price/yield
        // inversion, NPV(IRR) = 0, and recast/refinance linearity. Section 2
        // below, which is directional rather than recorded, passed UNCHANGED
        // through the bump.
        //
        // The scale change is a correction, not merely more digits. On the
        // canonical 495000 @ 0.5625%/mo x 360 payment the OLD 18-place value
        // agreed with the closed form to 13 decimal places; the new 38-place
        // value agrees to 36. The last five digits of the figure this project
        // quoted everywhere were rounding noise from the 128-bit intermediate.
        const std::vector<std::pair<std::string_view, std::string_view>> golden{
            {"716400 @5.97% 30y, 0.6% PMI, 796000 value", "800edc89df6de9af"},
            {"no home value -> basis falls back to the loan", "c7155eb17c4bcd05"},
            {"with 250/mo overpayment", "7f3c5b86fdebb58e"},
            {"no PMI", "e59b661779c3075d"},
            {"3.5% down, PMI runs long", "ec2cc368d69ef3e5"},
        };

        std::size_t i = 0;
        for (const auto& s : scenarios()) {
            const auto [rows, sum] = sensen::calculate_mortgage_amortization(
                s.loan, s.rate, s.term, s.overpay, s.pmi_rate, s.home_value);
            const std::string got = digest(rows, sum);
            std::printf("    | %-46s %s\n", std::string{s.name}.c_str(), got.c_str());
            check(got == golden[i].second,
                  std::string{s.name} + " digest unchanged (" + got + ")");
            ++i;
        }
    }

    std::printf("\n2. the knobs move the cancellation month in the stated direction\n");
    {
        using BD = sensen::BigDecimal;
        const BD loan{"716400"};
        const BD rate{"0.0597"};
        const BD pmi{"0.006"};
        const BD value{"796000"};

        const auto run = [&](const sensen::PmiTermination& policy, BD overpay = BD(0)) {
            const auto [rows, sum] = sensen::calculate_mortgage_amortization(
                loan, rate, 360, overpay, pmi, value, policy);
            return std::pair{last_pmi_month(rows), sum.total_pmi_paid};
        };

        const auto [base_month, base_total] = run({});
        check(base_month > 0 && base_month < 360,
              "the default policy cancels PMI part-way (month " + std::to_string(base_month) + ")");

        // NEVER: the legacy shape, nameable rather than discovered.
        const auto [never_month, never_total] = run({.basis = sensen::PmiBasis::Never});
        check(never_month == 360 && never_total > base_total,
              "Never charges every month of the term and costs more");

        // A LOWER threshold means the balance must fall FURTHER, so PMI runs
        // LONGER. Stated because the direction reads backwards at a glance:
        // a smaller number is a stricter requirement here, not a looser one.
        const auto [strict_month, strict_total] = run({.threshold_ltv = BD("0.78")});
        check(strict_month > base_month,
              "a 0.78 threshold cancels LATER than 0.80 (month " +
                  std::to_string(strict_month) + " vs " + std::to_string(base_month) + ")");

        // A HIGHER threshold cancels sooner, the mirror of the above.
        const auto [loose_month, loose_total] = run({.threshold_ltv = BD("0.90")});
        check(loose_month < base_month,
              "a 0.90 threshold cancels SOONER (month " + std::to_string(loose_month) + ")");

        // Appreciation only counts when the caller asks for it.
        const auto [appr_month, appr_total] = run(
            {.annual_appreciation = BD("0.03"), .basis = sensen::PmiBasis::AppreciatedValue});
        check(appr_month < base_month,
              "an appreciating basis cancels sooner (month " + std::to_string(appr_month) + ")");
        check(appr_total < base_total, "and costs less in total");

        const auto [ignored_month, ignored_total] = run({.annual_appreciation = BD("0.03")});
        check(ignored_month == base_month && ignored_total == base_total,
              "the SAME appreciation on the ORIGINAL basis changes nothing -- the basis "
              "decides whether it counts, not the presence of a rate");

        // Overpayment and appreciation compose, but NOT additively, and the
        // first version of this check asserted the wrong thing by assuming they
        // did. On an appreciating basis the threshold STEPS at each year
        // boundary -- at 3% on 796,000 it jumps 20,267 between month 36 and 37 --
        // and a 250/mo overpayment contributes about 9,000 over the same three
        // years. The step dominates, so both land on the same month. That is the
        // model behaving correctly, not a lever doing nothing.
        const auto [both_month, both_total] = run(
            {.annual_appreciation = BD("0.03"), .basis = sensen::PmiBasis::AppreciatedValue},
            BD("250"));
        check(both_month <= appr_month,
              "an overpayment never delays cancellation on an appreciating basis (month " +
                  std::to_string(both_month) + " vs " + std::to_string(appr_month) + ")");
        check(both_total <= appr_total, "and never costs more");

        // An overpayment large enough to outrun the annual step DOES move it,
        // which is what proves the two levers genuinely compose rather than one
        // being inert on this basis.
        const auto [big_month, big_total] = run(
            {.annual_appreciation = BD("0.03"), .basis = sensen::PmiBasis::AppreciatedValue},
            BD("8000"));
        check(big_month < appr_month,
              "a large overpayment outruns the annual step and cancels sooner (month " +
                  std::to_string(big_month) + " vs " + std::to_string(appr_month) + ")");

        // And on the ORIGINAL basis, where there is no step to hide behind, an
        // ordinary overpayment moves the month on its own.
        const auto [over_month, over_total] = run({}, BD("250"));
        check(over_month < base_month,
              "on the original basis a 250/mo overpayment cancels sooner (month " +
                  std::to_string(over_month) + " vs " + std::to_string(base_month) + ")");
    }

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
