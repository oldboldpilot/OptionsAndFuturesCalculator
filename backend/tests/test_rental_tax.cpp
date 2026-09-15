/**
 * Rental tax: Schedule E, depreciation, and the loss you cannot use yet.
 *
 * @author Olumuyiwa Oluwasanmi
 */
#include <cstdio>
#include <new>

import std;
import tax_deduction;
import rental_tax;

namespace tx = mortgage_calculator::tax;

namespace {
int g_checks = 0;
int g_failures = 0;

auto check(bool cond, const std::string& what) -> void {
    ++g_checks;
    std::printf("  %s: %s\n", cond ? "PASS" : "FAIL", what.c_str());
    if (!cond) { ++g_failures; }
}

auto near(double a, double b, double tol = 0.5) -> bool { return std::fabs(a - b) < tol; }

auto base() -> tx::RentalTaxInput {
    tx::RentalTaxInput in{};
    in.year = 2026;
    in.status = tx::FilingStatus::MarriedJoint;
    in.other_taxable_income = 150'000.0;      // 22% band
    in.rental_income = 22'080.0;              // collected, after vacancy
    in.operating_expenses = 9'200.0;
    in.mortgage_interest = 15'000.0;
    in.building_basis = 240'000.0;            // 300k price less 60k land
    in.month_placed_in_service = 1;
    in.ownership_year = 2;
    in.state_tax_rate = 0.05;
    return in;
}
}  // namespace

auto main() -> int {
    std::printf("1. NO standard-deduction floor -- this is Schedule E, not Schedule A\n");
    {
        // The homeowner model only counts interest ABOVE the standard
        // deduction. On a rental every dollar of interest counts from the
        // first, and applying the homeowner rule here would erase $32,200 of
        // real deductions.
        const auto r = *tx::evaluate_rental(base());
        const double expected_depr = 240'000.0 / 27.5;
        check(near(r.depreciation, expected_depr),
              "depreciation is basis / 27.5 = " + std::to_string(static_cast<int>(expected_depr)));
        check(near(r.net_rental_income, 22'080.0 - 9'200.0 - 15'000.0 - expected_depr),
              "net rental income deducts expenses, interest AND depreciation in full");
        check(r.net_rental_income < 0,
              "which puts this ordinary, profitable-looking rental at a tax LOSS");
    }

    std::printf("\n2. depreciation uses the BUILDING, and the mid-month convention\n");
    {
        auto land_included = base();
        land_included.building_basis = 300'000.0;     // land not excluded
        const auto bad = *tx::evaluate_rental(land_included);
        const auto good = *tx::evaluate_rental(base());
        check(bad.depreciation > good.depreciation,
              "passing the whole price inflates the deduction by the land's share");

        // Let in September: the month of service is a HALF month, so 3.5 of 12.
        auto sept = base();
        sept.ownership_year = 1;
        sept.month_placed_in_service = 9;
        const auto s = *tx::evaluate_rental(sept);
        check(near(s.depreciation, (240'000.0 / 27.5) * (3.5 / 12.0)),
              "a September start gets 3.5 months, not 4 and not a half year");

        auto jan = base();
        jan.ownership_year = 1;
        jan.month_placed_in_service = 1;
        const auto j = *tx::evaluate_rental(jan);
        check(near(j.depreciation, (240'000.0 / 27.5) * (11.5 / 12.0)),
              "and a January start gets 11.5, not 12 -- mid-month applies at both ends");

        auto done = base();
        done.ownership_year = 29;
        check(near(tx::evaluate_rental(done)->depreciation, 0.0),
              "after 27.5 years there is nothing left to depreciate");
    }

    std::printf("\n3. a loss is LIMITED, and the rest is suspended rather than lost\n");
    {
        auto low = base();
        low.other_taxable_income = 90'000.0;          // below the phase-out
        low.magi = 90'000.0;
        const auto a = *tx::evaluate_rental(low);
        check(near(a.passive_allowance, 25'000.0),
              "below $100,000 MAGI the full $25,000 allowance applies");
        check(a.deductible_loss > 0 && near(a.suspended_loss, 0.0),
              "and a loss this size is fully usable");
        check(a.federal_tax < 0, "so it REDUCES tax on other income");

        auto mid = base();
        mid.magi = 120'000.0;                         // halfway through
        const auto b = *tx::evaluate_rental(mid);
        check(near(b.passive_allowance, 25'000.0 - 0.50 * 20'000.0),
              "50 cents per dollar above $100,000 leaves $15,000");

        auto high = base();
        high.other_taxable_income = 160'000.0;
        high.magi = 160'000.0;
        const auto c = *tx::evaluate_rental(high);
        check(near(c.passive_allowance, 0.0), "at $150,000 the allowance is gone");
        check(near(c.deductible_loss, 0.0), "so none of the loss is usable this year");
        check(c.suspended_loss > 0, "but it is SUSPENDED, not lost");
        check(near(c.federal_tax, 0.0),
              "and reports NO tax saving -- claiming one the investor cannot take "
              "this year is the defect being excluded");
    }

    std::printf("\n4. without active participation there is no allowance at all\n");
    {
        auto passive = base();
        passive.other_taxable_income = 90'000.0;
        passive.magi = 90'000.0;
        passive.active_participation = false;
        const auto r = *tx::evaluate_rental(passive);
        check(near(r.passive_allowance, 0.0), "the $25,000 needs active participation");
        check(r.suspended_loss > 0, "so the whole loss is suspended");
    }

    std::printf("\n5. a PROFITABLE rental owes tax, federal AND state\n");
    {
        auto profit = base();
        profit.rental_income = 60'000.0;
        profit.building_basis = 0;                    // fully depreciated
        const auto r = *tx::evaluate_rental(profit);
        check(r.net_rental_income > 0, "income exceeds deductions");
        check(near(r.federal_tax, r.net_rental_income * 0.22),
              "federal tax at the 22% marginal rate");
        check(near(r.state_tax, r.net_rental_income * 0.05),
              "state tax at the state's own rate");
        check(near(r.total_tax, r.federal_tax + r.state_tax),
              "and the obligation is both, which a deductions-only model never shows");
    }

    std::printf("\n6. married-filing-separately halves the allowance and its threshold\n");
    {
        auto mfs = base();
        mfs.status = tx::FilingStatus::MarriedSeparate;
        mfs.other_taxable_income = 40'000.0;
        mfs.magi = 40'000.0;
        const auto r = *tx::evaluate_rental(mfs);
        check(near(r.passive_allowance, 12'500.0), "the cap is $12,500");

        mfs.magi = 60'000.0;                          // 10k past the 50k start
        const auto p = *tx::evaluate_rental(mfs);
        check(near(p.passive_allowance, 12'500.0 - 0.50 * 10'000.0),
              "phasing out from $50,000 rather than $100,000");
    }

    std::printf("\n7. refusals\n");
    {
        auto in = base();
        in.year = 2024;
        check(!tx::evaluate_rental(in).has_value(), "an unsourced tax year is refused");

        in = base();
        in.month_placed_in_service = 13;
        check(!tx::evaluate_rental(in).has_value(), "a thirteenth month is refused");

        in = base();
        in.state_tax_rate = 1.5;
        check(!tx::evaluate_rental(in).has_value(), "a state rate above 100% is refused");
    }

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
