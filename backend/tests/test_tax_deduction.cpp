/**
 * The mortgage interest deduction, bounded by the four things that bound it.
 *
 * @author Olumuyiwa Oluwasanmi
 */
#include <cstdio>
#include <new>

import std;
import tax_deduction;

namespace tx = mortgage_calculator::tax;

namespace {
int g_checks = 0;
int g_failures = 0;

auto check(bool cond, const std::string& what) -> void {
    ++g_checks;
    std::printf("  %s: %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (!cond) { ++g_failures; }
}

auto near(double a, double b, double tol = 0.01) -> bool { return std::fabs(a - b) < tol; }
}  // namespace

auto main() -> int {
    std::printf("1. an UNKNOWN YEAR is refused, and the refusal names what exists\n");
    {
        // Every figure in this module is statutory and several moved
        // discontinuously -- the SALT cap went from 10,000 to 40,400. A table
        // that extrapolated would be confidently wrong in the direction a user
        // would act on, so it refuses instead.
        const auto t = tx::table_for(2024, tx::FilingStatus::MarriedJoint);
        check(!t.has_value(), "2024 has no table and is not invented");
        check(t.error().find("2026") != std::string::npos,
              "the refusal names the year that IS carried");
        check(tx::table_for(2026, tx::FilingStatus::MarriedJoint).has_value(),
              "2026 resolves");
    }

    std::printf("\n2. the brackets are the IRS ones, at the edges\n");
    {
        const auto t = *tx::table_for(2026, tx::FilingStatus::MarriedJoint);
        check(near(t.standard_deduction, 32'200.0), "MFJ standard deduction is 32,200");
        check(near(tx::marginal_rate(t, 24'800.0), 0.12), "24,800 is the first dollar at 12%");
        check(near(tx::marginal_rate(t, 24'799.0), 0.10), "one dollar below is still 10%");
        check(near(tx::marginal_rate(t, 100'800.0), 0.22), "100,800 starts the 22% band");
        check(near(tx::marginal_rate(t, 768'700.0), 0.37), "768,700 starts the top band");

        const auto s = *tx::table_for(2026, tx::FilingStatus::Single);
        check(near(s.standard_deduction, 16'100.0), "single standard deduction is 16,100");
        check(near(tx::marginal_rate(s, 640'600.0), 0.37), "single tops out at 640,600");
    }

    std::printf("\n3. THE CASE THE NAIVE FORMULA FLATTERS: itemising loses\n");
    {
        // 18,000 of interest and 9,000 of SALT is 27,000 itemised against a
        // 32,200 standard deduction. The deduction is worth NOTHING, and
        // `interest * rate` says 3,960.
        tx::DeductionInput in{};
        in.year = 2026;
        in.status = tx::FilingStatus::MarriedJoint;
        in.taxable_income = 150'000.0;
        in.mortgage_interest = 18'000.0;
        in.acquisition_debt = 320'000.0;
        in.property_tax = 9'000.0;

        const auto r = *tx::evaluate(in);
        check(!r.itemising_beats_standard, "itemising loses to the standard deduction");
        check(near(r.excess_over_standard, 0.0), "so the excess is zero");
        check(near(r.tax_saving, 0.0), "and the deduction is worth exactly nothing");
        check(near(r.naive_tax_saving, 18'000.0 * 0.22),
              "where interest * marginal_rate claims $" +
                  std::to_string(static_cast<int>(r.naive_tax_saving)));
        check(r.naive_tax_saving > r.tax_saving,
              "the naive figure is carried so a caller can SHOW the gap, not hide it");
    }

    std::printf("\n4. when itemising DOES win, only the excess counts\n");
    {
        tx::DeductionInput in{};
        in.year = 2026;
        in.status = tx::FilingStatus::MarriedJoint;
        in.taxable_income = 300'000.0;         // 24% band
        in.mortgage_interest = 34'000.0;
        in.acquisition_debt = 600'000.0;
        in.property_tax = 12'000.0;
        in.other_itemised = 5'000.0;

        const auto r = *tx::evaluate(in);
        check(near(r.allowed_salt, 12'000.0), "SALT is under the 40,400 cap and passes whole");
        check(near(r.itemised_total, 51'000.0), "itemised total is 34k + 12k + 5k");
        check(near(r.excess_over_standard, 51'000.0 - 32'200.0), "only the excess over 32,200 counts");
        check(near(r.tax_saving, (51'000.0 - 32'200.0) * 0.24),
              "worth the excess at the marginal rate");
        check(r.tax_saving < r.naive_tax_saving,
              "still less than interest * rate, which ignores the floor entirely");
    }

    std::printf("\n5. the SALT cap, and the phase-down above 505,000 MAGI\n");
    {
        tx::DeductionInput in{};
        in.year = 2026;
        in.status = tx::FilingStatus::MarriedJoint;
        in.taxable_income = 400'000.0;
        in.property_tax = 30'000.0;
        in.other_state_local_tax = 25'000.0;   // 55,000 of SALT against a 40,400 cap

        const auto under = *tx::evaluate(in);
        check(near(under.salt_cap_applied, 40'400.0), "below the MAGI threshold the full cap applies");
        check(near(under.allowed_salt, 40'400.0), "and SALT is capped, not passed whole");

        // 30 cents of cap per dollar of MAGI above 505,000.
        in.magi = 605'000.0;
        const auto over = *tx::evaluate(in);
        check(near(over.salt_cap_applied, 40'400.0 - 0.30 * 100'000.0),
              "the cap phases down by 30% of the MAGI excess");

        // ...but never below the 10,000 floor.
        in.magi = 1'000'000.0;
        const auto floored = *tx::evaluate(in);
        check(near(floored.salt_cap_applied, 10'000.0),
              "and stops at the 10,000 floor rather than going negative");
    }

    std::printf("\n6. the acquisition-debt cap pro-rates the interest\n");
    {
        tx::DeductionInput in{};
        in.year = 2026;
        in.status = tx::FilingStatus::MarriedJoint;
        in.taxable_income = 300'000.0;
        in.mortgage_interest = 60'000.0;
        in.acquisition_debt = 1'500'000.0;     // twice the 750,000 cap

        const auto r = *tx::evaluate(in);
        check(near(r.deductible_interest, 30'000.0),
              "half the debt is over the cap, so half the interest is deductible");

        in.acquisition_debt = 700'000.0;       // under the cap
        const auto whole = *tx::evaluate(in);
        check(near(whole.deductible_interest, 60'000.0),
              "under the cap the interest passes whole");
    }

    std::printf("\n7. married-filing-separately halves the statutory limits\n");
    {
        const auto t = *tx::table_for(2026, tx::FilingStatus::MarriedSeparate);
        check(near(t.salt_cap, 20'200.0), "SALT cap is halved");
        check(near(t.acquisition_debt_cap, 375'000.0), "acquisition debt cap is halved");
        check(near(t.salt_cap_floor, 5'000.0), "and so is the floor");
        check(near(t.standard_deduction, 16'100.0), "standard deduction matches single");
    }

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
