/**
 * Rental cash flow for an investor: vacancy, itemised expenses, and a HELOC
 * that funded the deposit.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * Each section pins one error this model exists to prevent, and every one of
 * them produces a plausible number rather than a failure -- which is why they
 * are asserted rather than trusted to review.
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

/** A 300k rental, 25% down, let at 2,000/mo. */
auto base_case() -> sensen::RentalCashFlowInput {
    sensen::RentalCashFlowInput in{};
    in.property_price = BD("300000");
    in.down_payment = BD("75000");
    in.closing_costs = BD("6000");
    in.loan_annual_rate = BD("0.0675");
    in.loan_term_years = 30;
    in.monthly_gross_rent = BD("2000");
    in.annual_property_tax = BD("3600");
    in.annual_insurance = BD("1400");
    in.annual_repairs = BD("2400");
    in.annual_capex_reserve = BD("1800");
    in.years = 10;
    return in;
}
}  // namespace

auto main() -> int {
    std::printf("1. VACANCY is modelled, and it is not a rounding detail\n");
    {
        auto full = base_case();
        const auto a = *sensen::calculate_rental_cash_flow(full);

        auto vacant = base_case();
        vacant.occupancy_rate = BD("0.92");           // 8% vacancy
        const auto b = *sensen::calculate_rental_cash_flow(vacant);

        check(near(a.first.front().gross_scheduled_rent, 24'000.0),
              "scheduled rent is 12 x 2,000");
        check(near(a.first.front().vacancy_loss, 0.0),
              "an omitted occupancy means FULLY OCCUPIED, not never occupied");
        check(near(b.first.front().vacancy_loss, 1'920.0),
              "8% vacancy costs 1,920 a year");
        check(near(b.first.front().effective_gross_income, 22'080.0),
              "collected rent is 22,080, not 24,000");

        // The point, stated as the thing that is actually true. An earlier
        // version of this check claimed the vacancy loss always exceeds the
        // whole cash flow. It often does and here it does not -- 1,920 against
        // a cash flow of -4,633 -- so the claim was rhetoric rather than a
        // property. What IS a property is that vacancy comes off the bottom
        // line dollar for dollar, which is why omitting it does not shade an
        // answer so much as move it.
        const double with_vac = std::stod(b.first.front().cash_flow.to_string());
        const double without = std::stod(a.first.front().cash_flow.to_string());
        check(std::fabs((without - with_vac) - 1'920.0) < 0.5,
              "vacancy costs the cash flow exactly the rent that went uncollected");
    }

    std::printf("\n2. the management fee is charged on COLLECTED rent\n");
    {
        auto in = base_case();
        in.occupancy_rate = BD("0.90");
        in.management_fee_rate = BD("0.10");
        const auto r = *sensen::calculate_rental_cash_flow(in);

        // 10% of 21,600 collected, not 10% of 24,000 scheduled. Billing the
        // owner for collecting rent nobody paid is the error being excluded.
        const double opex = std::stod(r.first.front().operating_expenses.to_string());
        const double fixed = 3600 + 1400 + 2400 + 1800;
        check(std::fabs(opex - (fixed + 2160.0)) < 0.5,
              "fee is 2,160 (10% of collected) not 2,400 (10% of scheduled)");
    }

    std::printf("\n3. NOI EXCLUDES debt service, so the cap rate is a property of the ASSET\n");
    {
        auto cash = base_case();
        cash.down_payment = BD("300000");             // no loan at all
        const auto a = *sensen::calculate_rental_cash_flow(cash);

        auto levered = base_case();                   // 225k loan
        const auto b = *sensen::calculate_rental_cash_flow(levered);

        check(near(a.second.year_one_noi, std::stod(b.second.year_one_noi.to_string())),
              "two identical houses have the same NOI however they were financed");
        check(near(a.second.cap_rate, std::stod(b.second.cap_rate.to_string()), 0.0001),
              "and therefore the same cap rate -- folding debt in makes cap rates "
              "incomparable, which is the error");
        check(std::stod(b.first.front().mortgage_debt_service.to_string()) > 0,
              "while the levered case does carry debt service");
        check(std::stod(b.first.front().cash_flow.to_string()) <
                  std::stod(a.first.front().cash_flow.to_string()),
              "which shows up in CASH FLOW, where it belongs");
    }

    std::printf("\n4. repairs and capex are separate lines, and both bite\n");
    {
        auto none = base_case();
        none.annual_repairs = BD(0);
        none.annual_capex_reserve = BD(0);
        const auto a = *sensen::calculate_rental_cash_flow(none);
        const auto b = *sensen::calculate_rental_cash_flow(base_case());

        const double delta = std::stod(a.first.front().cash_flow.to_string()) -
                             std::stod(b.first.front().cash_flow.to_string());
        check(std::fabs(delta - 4'200.0) < 0.5,
              "4,200 of repairs and reserve is 4,200 off the cash flow");

        // A boiler is not a leaking tap. Budgeting one as the other leaves the
        // owner short by the difference exactly when it is least convenient.
        auto grown = base_case();
        grown.annual_expense_growth = BD("0.03");
        const auto g = *sensen::calculate_rental_cash_flow(grown);
        check(std::stod(g.first.back().operating_expenses.to_string()) >
                  std::stod(g.first.front().operating_expenses.to_string()),
              "expenses inflate over the horizon");
        check(near(g.first.back().mortgage_debt_service,
                   std::stod(g.first.front().mortgage_debt_service.to_string()), 1.0),
              "but a FIXED-RATE debt service does not -- inflating it is the slip");
    }

    std::printf("\n5. a HELOC-funded deposit: less own cash, more debt service\n");
    {
        auto in = base_case();
        in.heloc_drawn_amount = BD("75000");          // the whole deposit
        in.heloc_annual_rate = BD("0.085");
        in.heloc_term_years = 10;
        const auto r = *sensen::calculate_rental_cash_flow(in);
        const auto plain = *sensen::calculate_rental_cash_flow(base_case());

        check(near(r.second.total_cash_to_close, 81'000.0),
              "cash to close is unchanged -- the money still had to appear");
        check(near(r.second.own_cash_invested, 6'000.0),
              "but only the 6,000 of closing costs was the investor's own");
        check(std::stod(r.first.front().heloc_debt_service.to_string()) > 0,
              "and the HELOC adds a second debt service");
        check(std::stod(r.first.front().cash_flow.to_string()) <
                  std::stod(plain.first.front().cash_flow.to_string()),
              "which the rental has to carry, so cash flow is worse");
        check(near(r.first.front().loan_balance,
                   std::stod(plain.first.front().loan_balance.to_string()), 1.0),
              "the HELOC is secured elsewhere, so it never touches THIS loan's balance");

        // Year 11 of a 10-year HELOC: the payment stops.
        auto long_horizon = in;
        long_horizon.years = 12;
        const auto lh = *sensen::calculate_rental_cash_flow(long_horizon);
        check(near(lh.first[10].heloc_debt_service, 0.0),
              "and it stops once its own term ends");
    }

    std::printf("\n6. cash-on-cash on ZERO own cash is UNDEFINED, not enormous\n");
    {
        auto in = base_case();
        in.heloc_drawn_amount = BD("81000");          // deposit AND closing costs
        in.heloc_annual_rate = BD("0.085");
        const auto r = *sensen::calculate_rental_cash_flow(in);

        check(near(r.second.own_cash_invested, 0.0), "no own cash went in");
        check(!r.second.cash_on_cash_defined,
              "so cash-on-cash is reported UNDEFINED -- a return on zero is not a "
              "large number, it is not a number");
        check(r.second.debt_service_coverage_defined,
              "while DSCR is still defined, because there is still debt to cover");
    }

    std::printf("\n7. a negative year is surfaced, not averaged away\n");
    {
        auto in = base_case();
        in.monthly_gross_rent = BD("1400");           // under-rented
        in.annual_appreciation = BD("0.05");          // but appreciating fast
        in.selling_cost_percent = BD("0.06");
        const auto r = *sensen::calculate_rental_cash_flow(in);

        check(r.second.any_year_negative,
              "a year the owner funds out of pocket is flagged");
        check(std::stod(r.second.total_profit.to_string()) > 0,
              "even when the sale still shows a profit");
        check(!r.second.debt_service_coverage_defined ||
                  std::stod(r.second.debt_service_coverage.to_string()) < 1.0,
              "and DSCR below 1.0 says the rent does not cover the borrowing");
    }

    std::printf("\n8. refusals, rather than plausible answers\n");
    {
        auto in = base_case();
        in.occupancy_rate = BD("1.5");
        check(!sensen::calculate_rental_cash_flow(in).has_value(),
              "occupancy above 1.0 is refused");

        in = base_case();
        in.down_payment = BD("400000");
        check(!sensen::calculate_rental_cash_flow(in).has_value(),
              "a deposit larger than the price is refused");

        in = base_case();
        in.property_price = BD(0);
        check(!sensen::calculate_rental_cash_flow(in).has_value(),
              "a zero price is refused rather than dividing by it for the cap rate");
    }

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
