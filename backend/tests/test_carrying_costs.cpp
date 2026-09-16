/**
 * Repairs and the owner's own insurance on an ORDINARY mortgage.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * These are optional by design, so the first thing asserted is that a caller
 * who does not ask for them gets exactly what they got before the fields
 * existed -- byte-identical over the whole schedule, not merely "about the
 * same totals".
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

using BD = sensen::BigDecimal;

auto near(const BD& a, double b, double tol = 0.5) -> bool {
    return std::fabs(std::stod(a.to_string()) - b) < tol;
}

/** FNV-1a over every field of every row: a digest that moves if ANY figure in
 *  the schedule moves, which is what "unchanged" has to mean here. */
auto digest(const std::vector<sensen::DetailedAmortizationRow>& rows) -> std::uint64_t {
    std::uint64_t h = 1469598103934665603ULL;
    const auto eat = [&h](const std::string& s) {
        for (const unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
    };
    for (const auto& r : rows) {
        eat(std::to_string(r.period));
        eat(r.start_balance.to_string());
        eat(r.scheduled_payment.to_string());
        eat(r.extra_payment.to_string());
        eat(r.interest_paid.to_string());
        eat(r.principal_paid.to_string());
        eat(r.pmi_paid.to_string());
        eat(r.tax_savings.to_string());
        eat(r.end_balance.to_string());
    }
    return h;
}

auto run(sensen::CarryingCosts c = {}) {
    return sensen::calculate_detailed_mortgage_amortization(
        BD("300000"), BD("0.065"), 360, BD(0), BD(0), BD("375000"), BD(0), {}, c);
}
}  // namespace

auto main() -> int {
    std::printf("1. OPTIONAL means the schedule is untouched when it is not asked for\n");
    {
        const auto plain = run();
        sensen::CarryingCosts c{.annual_repairs = BD("3600"), .annual_insurance = BD("900")};
        const auto withc = run(c);

        check(digest(plain.first) == digest(withc.first),
              "every loan figure -- balance, interest, principal, PMI -- is "
              "byte-identical with and without carrying costs");
        check(near(plain.second.total_repairs_paid, 0.0),
              "an omitted repair budget is zero, not a guess at the national average");
        check(near(plain.second.total_cost_of_ownership,
                   std::stod(plain.second.total_payments_paid.to_string())),
              "and cost of ownership collapses to debt service alone");
    }

    std::printf("\n2. repairs are charged monthly from an ANNUAL budget\n");
    {
        const auto r = run({.annual_repairs = BD("3600")});
        check(near(r.first.front().repairs_paid, 300.0),
              "3,600 a year is 300 a month, not 3,600 in month one");
        check(near(r.second.total_repairs_paid, 3600.0 * 30, 5.0),
              "and 108,000 over a 30-year term");
        check(std::stod(r.second.total_cost_of_ownership.to_string()) >
                  std::stod(r.second.total_payments_paid.to_string()),
              "cost of ownership exceeds what went to the lender");
    }

    std::printf("\n3. the owner's insurance is NOT PMI, and does not cancel\n");
    {
        // PMI protects the lender and stops at the LTV threshold; a new-build
        // warranty protects the owner and stops for no one. Folding them into
        // one line would make a cost that ends look like a cost that does not.
        const auto r = sensen::calculate_detailed_mortgage_amortization(
            BD("300000"), BD("0.065"), 360, BD(0), BD("0.005"), BD("310000"), BD(0), {},
            {.annual_insurance = BD("1200")});

        const auto& rows = r.first;
        check(std::stod(r.second.total_pmi_paid.to_string()) > 0, "PMI is charged");
        check(near(rows.back().pmi_paid, 0.0), "and has stopped by the final month");
        check(near(rows.back().insurance_paid, 100.0),
              "while the owner's cover is still being paid in that same month");
    }

    std::printf("\n4. growth compounds ANNUALLY, not monthly\n");
    {
        const auto flat = run({.annual_repairs = BD("3600")});
        const auto grow = run({.annual_repairs = BD("3600"), .annual_growth = BD("0.03")});

        check(near(grow.first.front().repairs_paid, 300.0),
              "year one is unchanged -- growth applies from the next year");
        check(near(grow.first[11].repairs_paid, 300.0),
              "and holds for all twelve months of it: a budget is set yearly");
        check(near(grow.first[12].repairs_paid, 309.0),
              "month 13 steps to 309, which is 3% once and not 3% twelve times");
        check(std::stod(grow.second.total_repairs_paid.to_string()) >
                  std::stod(flat.second.total_repairs_paid.to_string()),
              "so an inflating budget costs more over the term than a flat one");

        // The error this excludes: compounding per month would reach
        // 300 * 1.03^12 = 427.92 by month 13 -- a 43% jump nobody budgeted.
        check(std::stod(grow.first[12].repairs_paid.to_string()) < 400.0,
              "monthly compounding would have reached 427.92 by month 13");
    }

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
