/**
 * The tax on a rental: what is owed, what is deductible, and what is suspended.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * THIS IS NOT THE HOMEOWNER MODEL AND REUSING THAT ONE WOULD BE WRONG IN THE
 * EXPENSIVE DIRECTION. `tax_deduction` applies a standard-deduction floor,
 * because mortgage interest on a PRIMARY RESIDENCE is an itemised deduction and
 * only counts above that floor. On a rental, interest and operating expenses are
 * SCHEDULE E deductions against rental income -- above the line, with no floor at
 * all. An investor who was told their interest only counts above $32,200 would be
 * understated by the whole of it.
 *
 * A rental also has three things the homeowner case does not:
 *
 *   1. TAXABLE INCOME, not merely deductions. Rent is income and the obligation
 *      is the point; a calculator that models only the deductions shows an
 *      investor half their tax position and the flattering half.
 *   2. DEPRECIATION, which is the largest deduction most rentals have and the
 *      only one that costs nothing. 27.5-year straight line on the BUILDING --
 *      land is not depreciable, and using the purchase price as the basis
 *      overstates it by whatever the land is worth.
 *   3. PASSIVE ACTIVITY LOSS LIMITS. A loss is not automatically usable. Up to
 *      $25,000 may offset other income with active participation, phasing out
 *      50 cents per dollar of MAGI above $100,000 and gone at $150,000. Above
 *      that the loss is SUSPENDED, not lost -- and reporting it as a tax saving
 *      the investor cannot take this year is the defect this models around.
 *
 * MID-MONTH IS IMPLEMENTED RATHER THAN REFUSED, which closes a gap this project
 * documented against `sensen::macrs`: 27.5- and 39-year property were refused
 * there because both use the mid-month convention and the month placed in service
 * is an input that message does not carry. Here it does, so the first and last
 * years are pro-rated properly instead of being approximated by a half-year.
 *
 * PROVENANCE: passive-activity figures from IRS Publication 925 (2025),
 * retrieved 2026-09-15. Brackets and the standard deduction come from
 * `tax_deduction`, which carries its own sourcing.
 *
 * WHAT IT IS NOT: no depreciation recapture at sale (a disposition model, not a
 * holding-period one), no at-risk limits beyond the passive rules, no
 * qualified-business-income deduction, no local tax below the state. Stated
 * rather than discovered.
 */
module;

#include <new>

export module rental_tax;

import std;
import tax_deduction;

export namespace mortgage_calculator::tax {

struct RentalTaxInput {
    int year = 0;
    FilingStatus status = FilingStatus::MarriedJoint;

    /** Wages and everything else, used to locate the bracket and to decide how
     *  much of a rental loss is usable. */
    double other_taxable_income = 0;
    /** Zero falls back to `other_taxable_income`. */
    double magi = 0;

    /** Rent actually COLLECTED -- effective gross income, after vacancy. */
    double rental_income = 0;
    /** Everything deductible that is not interest or depreciation: tax,
     *  insurance, repairs, management, HOA. NOT capex reserves, which are not
     *  an expense until spent, and not principal, which is never one. */
    double operating_expenses = 0;
    double mortgage_interest = 0;
    /** HELOC interest is deductible against the rental when the draw was used
     *  to acquire or improve it -- tracing rules decide that, and the caller
     *  asserts it by passing the figure here rather than this module guessing. */
    double heloc_interest = 0;

    /** Depreciable basis: the BUILDING, excluding land. */
    double building_basis = 0;
    /** 1..12. Zero means January. Drives the mid-month convention. */
    int month_placed_in_service = 0;
    /** Which year of ownership this is, 1-based. */
    int ownership_year = 1;

    /** The investor makes management decisions -- approves tenants, sets terms.
     *  Without it the $25,000 allowance does not apply at all. */
    bool active_participation = true;
    /** Marginal state income tax rate, e.g. 0.05. `state_assumptions` carries
     *  one per state. */
    double state_tax_rate = 0;
};

struct RentalTaxResult {
    double depreciation = 0;
    /** Income less every deduction. Negative is the common outcome and is not
     *  an error. */
    double net_rental_income = 0;
    /** The part of a loss usable against other income this year. */
    double deductible_loss = 0;
    /** The part carried forward. Suspended, NOT lost. */
    double suspended_loss = 0;
    /** The $25,000 allowance after the MAGI phase-out. */
    double passive_allowance = 0;
    double marginal_rate = 0;
    /** Positive is tax owed; negative is tax saved by a usable loss. */
    double federal_tax = 0;
    double state_tax = 0;
    double total_tax = 0;
};

[[nodiscard]] auto evaluate_rental(const RentalTaxInput& in)
    -> std::expected<RentalTaxResult, std::string>;

}  // namespace mortgage_calculator::tax

namespace mortgage_calculator::tax {

auto evaluate_rental(const RentalTaxInput& in) -> std::expected<RentalTaxResult, std::string> {
    auto table = table_for(in.year, in.status);
    if (!table) { return std::unexpected(table.error()); }
    if (in.rental_income < 0 || in.operating_expenses < 0 || in.mortgage_interest < 0 ||
        in.heloc_interest < 0 || in.building_basis < 0) {
        return std::unexpected("rental tax inputs cannot be negative");
    }
    if (in.month_placed_in_service < 0 || in.month_placed_in_service > 12) {
        return std::unexpected("month_placed_in_service must be 1..12");
    }
    if (in.ownership_year < 1) {
        return std::unexpected("ownership_year must be 1 or more");
    }
    if (in.state_tax_rate < 0 || in.state_tax_rate > 1) {
        return std::unexpected("state_tax_rate must be between 0 and 1");
    }

    RentalTaxResult r{};

    // --- depreciation: 27.5-year straight line, mid-month -------------------
    // The building only. A caller who passes the purchase price gets a
    // deduction inflated by the land, which is why the field is named for the
    // basis rather than for the price.
    constexpr double kResidentialLife = 27.5;
    if (in.building_basis > 0) {
        const double annual = in.building_basis / kResidentialLife;
        const int month = in.month_placed_in_service == 0 ? 1 : in.month_placed_in_service;
        if (in.ownership_year == 1) {
            // Mid-month: the month of service counts as a half month, so a
            // property let in September gets 3.5 of 12 months, not 4 and not 6.
            const double months = (12.0 - month) + 0.5;
            r.depreciation = annual * (months / 12.0);
        } else if (in.ownership_year <= 28) {
            r.depreciation = annual;
        } else {
            r.depreciation = 0;   // fully depreciated
        }
    }

    // --- Schedule E: no standard-deduction floor ---------------------------
    const double deductions =
        in.operating_expenses + in.mortgage_interest + in.heloc_interest + r.depreciation;
    r.net_rental_income = in.rental_income - deductions;

    const double magi = in.magi > 0 ? in.magi : in.other_taxable_income;
    const auto& t = *table;
    r.marginal_rate = marginal_rate(t, in.other_taxable_income);

    if (r.net_rental_income >= 0) {
        // Profit: taxed at the marginal rate, federally and by the state.
        r.federal_tax = r.net_rental_income * r.marginal_rate;
        r.state_tax = r.net_rental_income * in.state_tax_rate;
        r.deductible_loss = 0;
        r.suspended_loss = 0;
        r.passive_allowance = 0;
    } else {
        // Loss: usable only up to the allowance, which phases out on MAGI.
        // IRS Pub 925 -- $25,000, 50 cents per dollar above $100,000, zero at
        // $150,000. Halved for married-filing-separately living apart. These
        // figures are NOT inflation-indexed and have not moved since 1986,
        // which is why they are constants here rather than table columns.
        const double cap =
            in.status == FilingStatus::MarriedSeparate ? 12'500.0 : 25'000.0;
        const double phase_start =
            in.status == FilingStatus::MarriedSeparate ? 50'000.0 : 100'000.0;

        double allowance = 0;
        if (in.active_participation) {
            allowance = cap - 0.50 * std::max(0.0, magi - phase_start);
            allowance = std::max(0.0, allowance);
        }
        r.passive_allowance = allowance;

        const double loss = -r.net_rental_income;
        r.deductible_loss = std::min(loss, allowance);
        r.suspended_loss = loss - r.deductible_loss;

        // Negative tax: a usable loss REDUCES tax on other income. The
        // suspended part reduces nothing this year, and reporting it as a
        // saving is the defect this whole branch exists to avoid.
        r.federal_tax = -r.deductible_loss * r.marginal_rate;
        r.state_tax = -r.deductible_loss * in.state_tax_rate;
    }

    r.total_tax = r.federal_tax + r.state_tax;
    return r;
}

}  // namespace mortgage_calculator::tax
