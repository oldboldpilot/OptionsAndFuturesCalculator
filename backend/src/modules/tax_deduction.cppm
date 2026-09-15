/**
 * What the mortgage interest deduction is actually WORTH, per tax year.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * `interest * marginal_rate` -- which is what this engine computed for
 * ComputeDetailedAmortization and what every naive calculator shows -- overstates
 * the benefit, usually by a lot and sometimes by all of it. Four separate limits
 * sit between a dollar of interest and a dollar saved, and this module is all
 * four:
 *
 *   1. THE STANDARD DEDUCTION FLOOR. Itemising only helps to the extent the
 *      itemised total EXCEEDS the standard deduction. At $32,200 married-joint
 *      for 2026, a household with $18,000 of interest and $9,000 of SALT
 *      itemises $27,000 and saves NOTHING by doing so. The naive formula hands
 *      them 22% of $18,000 and calls it $3,960.
 *   2. THE SALT CAP, which bounds the property-tax half of that total.
 *   3. THE ACQUISITION-DEBT CAP, which makes interest on the top slice of a
 *      large mortgage non-deductible.
 *   4. FILING STATUS, which moves every one of the above and the brackets too.
 *
 * WHY THE YEAR IS A REQUIRED INPUT AND AN UNKNOWN YEAR IS REFUSED. Every figure
 * here is statutory and changes annually, and several changed in ways no
 * extrapolation would have found: the SALT cap for 2026 is $40,400, not the
 * $10,000 that held from 2018, and it phases down above $505,000 of MAGI. A
 * table that guessed forward from last year's numbers would be confidently
 * wrong in the direction users would act on. So `table_for` returns an error
 * naming the years it has, and adding a year means sourcing it -- the same
 * standard the Census refresh holds, where an out-of-bounds value is refused
 * rather than clamped because a clamped figure is one nobody measured that
 * nothing downstream can distinguish from one that was.
 *
 * PROVENANCE. 2026 figures are from IRS Rev. Proc. 2025-32 as published at
 * irs.gov/newsroom/irs-releases-tax-inflation-adjustments-for-tax-year-2026,
 * the SALT figures from the IRS One Big Beautiful Bill provisions page, and the
 * acquisition-debt limits from IRS Topic no. 505. Retrieved 2026-09-15.
 *
 * WHAT THIS IS NOT. It is not tax advice and it is not a return. It models the
 * federal deduction only: no state income tax, no AMT, no phase-outs other than
 * SALT's, no married-filing-separately itemisation coupling. Those are stated
 * here rather than discovered, because a number that looks like a tax
 * calculation invites being used as one.
 */
module;

#include <new>

export module tax_deduction;

import std;

export namespace mortgage_calculator::tax {

enum class FilingStatus : std::uint8_t {
    Single,
    MarriedJoint,
    MarriedSeparate,
    HeadOfHousehold,
};

/** One marginal rate and the taxable income at which it starts. */
struct Bracket {
    double lower_bound;
    double rate;
};

/** Everything statutory for one year and filing status. */
struct TaxYearTable {
    int year;
    FilingStatus status;
    double standard_deduction;
    /** Ascending by `lower_bound`; the first entry starts at zero. */
    std::vector<Bracket> brackets;
    /** The SALT ceiling before any phase-down. */
    double salt_cap;
    /** MAGI above which the cap is reduced. */
    double salt_phasedown_magi;
    /** Cents of cap lost per dollar of MAGI above the threshold. */
    double salt_phasedown_rate;
    /** The cap never falls below this. */
    double salt_cap_floor;
    /** Acquisition debt on which interest is deductible, for a home acquired
     *  after 2017-12-15. */
    double acquisition_debt_cap;
};

/** The years this module can answer for. */
[[nodiscard]] auto supported_years() -> std::vector<int>;

/** The table, or an error NAMING the years that exist. Never extrapolates. */
[[nodiscard]] auto table_for(int year, FilingStatus status)
    -> std::expected<TaxYearTable, std::string>;

/** The rate on the next dollar of taxable income. */
[[nodiscard]] auto marginal_rate(const TaxYearTable& t, double taxable_income) -> double;

struct DeductionInput {
    int year = 0;
    FilingStatus status = FilingStatus::MarriedJoint;
    /** Taxable income, used to locate the bracket. */
    double taxable_income = 0;
    /** Modified AGI, used only for the SALT phase-down. Zero means "use
     *  taxable_income", which is an approximation and is reported as one. */
    double magi = 0;
    /** Mortgage interest paid in the year. */
    double mortgage_interest = 0;
    /** The debt that interest was paid on, for the acquisition-debt cap. */
    double acquisition_debt = 0;
    /** Property tax paid in the year. */
    double property_tax = 0;
    /** Other state and local taxes -- income or sales -- which share the cap. */
    double other_state_local_tax = 0;
    /** Charitable gifts and anything else itemisable that is not SALT or
     *  mortgage interest. */
    double other_itemised = 0;
};

struct DeductionResult {
    /** Interest after the acquisition-debt cap. */
    double deductible_interest = 0;
    /** SALT after its cap and phase-down. */
    double allowed_salt = 0;
    /** The SALT ceiling that applied, after phase-down. */
    double salt_cap_applied = 0;
    double itemised_total = 0;
    double standard_deduction = 0;
    /** itemised_total - standard_deduction, floored at zero. THIS is what the
     *  deduction is worth deducting. */
    double excess_over_standard = 0;
    double marginal_rate = 0;
    /** excess_over_standard * marginal_rate. The honest answer. */
    double tax_saving = 0;
    /** What `interest * marginal_rate` would have said. Carried so a caller can
     *  SHOW the difference rather than quietly serve a smaller number than the
     *  one every other calculator displays. */
    double naive_tax_saving = 0;
    /** True when itemising loses to the standard deduction, which is the common
     *  case and the one users are most surprised by. */
    bool itemising_beats_standard = false;
};

[[nodiscard]] auto evaluate(const DeductionInput& in)
    -> std::expected<DeductionResult, std::string>;

}  // namespace mortgage_calculator::tax

namespace mortgage_calculator::tax {
namespace detail {

// IRS Rev. Proc. 2025-32 (tax year 2026). Thresholds are the START of each rate.
constexpr std::array<std::pair<double, double>, 7> k2026Single{{
    {0.0, 0.10}, {12'400.0, 0.12}, {50'400.0, 0.22}, {105'700.0, 0.24},
    {201'775.0, 0.32}, {256'225.0, 0.35}, {640'600.0, 0.37},
}};
constexpr std::array<std::pair<double, double>, 7> k2026Joint{{
    {0.0, 0.10}, {24'800.0, 0.12}, {100'800.0, 0.22}, {211'400.0, 0.24},
    {403'550.0, 0.32}, {512'450.0, 0.35}, {768'700.0, 0.37},
}};

[[nodiscard]] auto to_brackets(std::span<const std::pair<double, double>> raw)
    -> std::vector<Bracket> {
    std::vector<Bracket> out;
    out.reserve(raw.size());
    for (const auto& [bound, rate] : raw) {
        out.push_back(Bracket{.lower_bound = bound, .rate = rate});
    }
    return out;
}

}  // namespace detail

auto supported_years() -> std::vector<int> { return {2026}; }

auto table_for(int year, FilingStatus status) -> std::expected<TaxYearTable, std::string> {
    if (year != 2026) {
        std::string years;
        for (const int y : supported_years()) {
            years += (years.empty() ? "" : ", ") + std::to_string(y);
        }
        // Named rather than guessed. Every figure here is statutory and several
        // moved discontinuously between years; extrapolating one would produce a
        // number that looks like a tax calculation and is not.
        return std::unexpected("no tax table for " + std::to_string(year) +
                               "; this build carries " + years +
                               ". Add a year by sourcing it from the IRS revenue "
                               "procedure rather than by interpolating.");
    }

    TaxYearTable t{};
    t.year = 2026;
    t.status = status;

    switch (status) {
        case FilingStatus::MarriedJoint:
            t.standard_deduction = 32'200.0;
            t.brackets = detail::to_brackets(detail::k2026Joint);
            t.salt_cap = 40'400.0;
            t.salt_phasedown_magi = 505'000.0;
            t.salt_cap_floor = 10'000.0;
            t.acquisition_debt_cap = 750'000.0;
            break;
        case FilingStatus::MarriedSeparate:
            t.standard_deduction = 16'100.0;
            // Separate brackets are the joint thresholds halved; the SALT and
            // debt limits are halved by statute rather than by inflation.
            t.brackets = detail::to_brackets(detail::k2026Joint);
            for (auto& b : t.brackets) { b.lower_bound /= 2.0; }
            t.salt_cap = 20'200.0;
            t.salt_phasedown_magi = 252'500.0;
            t.salt_cap_floor = 5'000.0;
            t.acquisition_debt_cap = 375'000.0;
            break;
        case FilingStatus::HeadOfHousehold:
            t.standard_deduction = 24'150.0;
            // HoH has its own bracket schedule; the single one is used here and
            // that APPROXIMATION IS STATED rather than hidden -- it is wrong in
            // the 12% and 22% bands. Replacing it needs the HoH rows from the
            // revenue procedure, which this build does not yet carry.
            t.brackets = detail::to_brackets(detail::k2026Single);
            t.salt_cap = 40'400.0;
            t.salt_phasedown_magi = 505'000.0;
            t.salt_cap_floor = 10'000.0;
            t.acquisition_debt_cap = 750'000.0;
            break;
        case FilingStatus::Single:
        default:
            t.standard_deduction = 16'100.0;
            t.brackets = detail::to_brackets(detail::k2026Single);
            t.salt_cap = 40'400.0;
            t.salt_phasedown_magi = 505'000.0;
            t.salt_cap_floor = 10'000.0;
            t.acquisition_debt_cap = 750'000.0;
            break;
    }
    t.salt_phasedown_rate = 0.30;
    return t;
}

auto marginal_rate(const TaxYearTable& t, double taxable_income) -> double {
    double rate = t.brackets.empty() ? 0.0 : t.brackets.front().rate;
    for (const auto& b : t.brackets) {
        if (taxable_income >= b.lower_bound) { rate = b.rate; } else { break; }
    }
    return rate;
}

auto evaluate(const DeductionInput& in) -> std::expected<DeductionResult, std::string> {
    auto table = table_for(in.year, in.status);
    if (!table) { return std::unexpected(table.error()); }

    if (in.mortgage_interest < 0 || in.property_tax < 0 || in.other_state_local_tax < 0 ||
        in.other_itemised < 0 || in.taxable_income < 0 || in.acquisition_debt < 0) {
        return std::unexpected("tax deduction inputs cannot be negative");
    }

    DeductionResult r{};
    r.standard_deduction = table->standard_deduction;
    r.marginal_rate = marginal_rate(*table, in.taxable_income);

    // 1. Interest is deductible only on the capped slice of acquisition debt.
    //    Pro-rated, which is how the IRS worksheet does it: the cap applies to
    //    the DEBT, so the deductible share of the interest is the share of the
    //    debt that falls under it.
    r.deductible_interest = in.mortgage_interest;
    if (in.acquisition_debt > table->acquisition_debt_cap && in.acquisition_debt > 0) {
        r.deductible_interest =
            in.mortgage_interest * (table->acquisition_debt_cap / in.acquisition_debt);
    }

    // 2. SALT, capped, with the cap itself phased down on high MAGI. Zero MAGI
    //    falls back to taxable income -- an approximation, and the caller is
    //    told so by the field comment rather than by a surprise.
    const double magi = in.magi > 0 ? in.magi : in.taxable_income;
    double cap = table->salt_cap;
    if (magi > table->salt_phasedown_magi) {
        cap -= table->salt_phasedown_rate * (magi - table->salt_phasedown_magi);
        cap = std::max(cap, table->salt_cap_floor);
    }
    r.salt_cap_applied = cap;
    r.allowed_salt = std::min(in.property_tax + in.other_state_local_tax, cap);

    // 3. The itemised total, and the only part of it that is worth anything.
    r.itemised_total = r.deductible_interest + r.allowed_salt + in.other_itemised;
    r.excess_over_standard = std::max(0.0, r.itemised_total - r.standard_deduction);
    r.itemising_beats_standard = r.itemised_total > r.standard_deduction;

    r.tax_saving = r.excess_over_standard * r.marginal_rate;
    r.naive_tax_saving = in.mortgage_interest * r.marginal_rate;
    return r;
}

}  // namespace mortgage_calculator::tax
