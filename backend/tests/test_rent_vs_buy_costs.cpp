/**
 * Rent vs buy, with the costs an owner meets and a renter does not: repairs,
 * PMI (and its cancellation), an overpayment, and a HELOC that funded the
 * deposit.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * Every one of these moves the answer in the SAME direction -- against buying
 * -- so omitting them is not neutral, and each section pins the specific way
 * the model could get one of them plausibly wrong.
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
auto d(const BD& b) -> double { return std::stod(b.to_string()); }
auto near(const BD& a, double b, double tol = 0.5) -> bool { return std::fabs(d(a) - b) < tol; }

auto base() -> sensen::RentVsBuyInput {
    sensen::RentVsBuyInput in{};
    in.property_price = BD("400000");
    in.down_payment = BD("40000");                 // 10% -> PMI applies
    in.annual_home_appreciation = BD("0.03");
    in.current_monthly_rent = BD("2200");
    in.annual_rent_increase = BD("0.03");
    in.annual_investment_return = BD("0.07");
    in.years = 10;
    in.loan_annual_rate = BD("0.065");
    in.loan_term_years = 30;
    in.monthly_taxes_ins_maintenance = BD("650");
    in.closing_costs_buy = BD("8000");
    return in;
}

/** A digest over every figure the summary reports, so "unchanged" means every
 *  one of them rather than the headline. */
auto digest(const sensen::RentVsBuySummary& s) -> std::uint64_t {
    std::uint64_t h = 1469598103934665603ULL;
    const auto eat = [&h](const std::string& v) {
        for (const unsigned char c : v) { h ^= c; h *= 1099511628211ULL; }
    };
    eat(s.total_cost_of_buying.to_string());
    eat(s.total_cost_of_renting.to_string());
    eat(s.buying_advantage.to_string());
    eat(s.owner_terminal_wealth.to_string());
    eat(s.renter_terminal_wealth.to_string());
    eat(s.final_loan_balance.to_string());
    eat(s.total_principal_paid.to_string());
    eat(s.total_interest_paid.to_string());
    eat(s.total_rent_paid.to_string());
    return h;
}
}  // namespace

auto main() -> int {
    std::printf("1. omitted means UNCHANGED, over every figure and not just the verdict\n");
    {
        const auto a = *sensen::calculate_rent_vs_buy(base());
        auto same = base();
        const auto b = *sensen::calculate_rent_vs_buy(same);
        check(digest(a) == digest(b), "the comparison is deterministic");
        check(near(a.total_repairs_paid, 0.0) && near(a.total_pmi_paid, 0.0),
              "and an omitted repair budget and PMI rate are zero, not a default");
        check(a.pmi_ends_month == 0, "with no PMI there is no cancellation month to report");
    }

    std::printf("\n2. repairs are charged to the OWNER only, and they move the verdict\n");
    {
        const auto plain = *sensen::calculate_rent_vs_buy(base());
        auto with_r = base();
        with_r.annual_repairs = BD("4800");
        const auto r = *sensen::calculate_rent_vs_buy(with_r);

        check(d(r.total_repairs_paid) > 0, "repairs are accumulated");
        check(d(r.total_cost_of_buying) > d(plain.total_cost_of_buying),
              "buying costs more once the roof is in the model");
        check(near(r.total_rent_paid, d(plain.total_rent_paid)),
              "and the renter pays exactly what they paid before -- a landlord's "
              "repairs are not the tenant's bill");
        check(d(r.buying_advantage) < d(plain.buying_advantage),
              "so the advantage of buying falls");
    }

    std::printf("\n3. PMI is charged until it CANCELS, which is the whole point of it\n");
    {
        auto in = base();
        in.pmi_annual_rate = BD("0.006");
        const auto r = *sensen::calculate_rent_vs_buy(in);

        check(d(r.total_pmi_paid) > 0, "PMI is charged on a 10% deposit");
        check(r.pmi_ends_month > 0 && r.pmi_ends_month < in.years * 12,
              "and it STOPS inside the horizon rather than running forever");

        // The error this excludes: treating PMI as a permanent carrying cost.
        // Over 10 years that overcharges by every month past cancellation.
        const double forever = d(in.pmi_annual_rate.multiply(BD("360000"))) * in.years;
        check(d(r.total_pmi_paid) < forever,
              "charging it for the whole horizon would cost far more than it does");
    }

    std::printf("\n4. an overpayment pulls the PMI cancellation FORWARD\n");
    {
        auto slow = base();
        slow.pmi_annual_rate = BD("0.006");
        const auto a = *sensen::calculate_rent_vs_buy(slow);

        auto fast = slow;
        fast.monthly_overpayment = BD("400");
        const auto b = *sensen::calculate_rent_vs_buy(fast);

        check(b.pmi_ends_month < a.pmi_ends_month,
              "paying down principal faster reaches 80% LTV sooner");
        check(d(b.total_pmi_paid) < d(a.total_pmi_paid), "so less PMI is paid in total");
        check(d(b.total_interest_paid) < d(a.total_interest_paid),
              "and less interest -- the interaction a separate PMI calculator cannot show");
        check(d(b.final_loan_balance) < d(a.final_loan_balance), "with a smaller balance left");
    }

    std::printf("\n5. a HELOC-funded deposit: the RENTER does not get to invest borrowed money\n");
    {
        auto cash = base();
        const auto a = *sensen::calculate_rent_vs_buy(cash);

        auto borrowed = base();
        borrowed.heloc_drawn_amount = BD("30000");     // most of the deposit
        borrowed.heloc_annual_rate = BD("0.085");
        borrowed.heloc_term_years = 10;
        const auto b = *sensen::calculate_rent_vs_buy(borrowed);

        // The defect this excludes is silent: opening the renter's portfolio
        // with the GROSS cash to close hands them 30,000 they never had,
        // making buying look worse by the value of a loan nobody took. Every
        // figure stays plausible.
        //
        // MEASURED OVER ONE YEAR, AND THE HORIZON IS THE POINT. The renter's
        // wealth is NOT monotonically lower with a HELOC: their portfolio
        // opens smaller, but it then receives `owner_outflow - rent` every
        // month, and the HELOC payment RAISES owner_outflow -- so the renter
        // also banks the payments they avoided. Over ten years those inflows
        // outweigh the smaller opening and the sign flips. An earlier version
        // of this check asserted the ten-year comparison and failed, which is
        // the test being wrong rather than the model. One year isolates the
        // opening balance, before the monthly differential swamps it.
        auto cash_1y = cash;    cash_1y.years = 1;
        auto borrow_1y = borrowed;  borrow_1y.years = 1;
        const auto a1 = *sensen::calculate_rent_vs_buy(cash_1y);
        const auto b1 = *sensen::calculate_rent_vs_buy(borrow_1y);
        check(d(b1.renter_terminal_wealth) < d(a1.renter_terminal_wealth),
              "over one year the renter's portfolio opens with the buyer's OWN "
              "cash, not the borrowed part");
        check(d(b.total_heloc_interest_paid) > 0, "the HELOC costs interest");
        check(d(b.total_cost_of_buying) > d(a.total_cost_of_buying),
              "which the owner carries, so buying costs more");
        check(near(b.final_loan_balance, d(a.final_loan_balance)),
              "and it never touches the MORTGAGE balance -- it is secured elsewhere");
    }

    std::printf("\n6. inflation escalates repairs, and a fixed-rate payment is immune\n");
    {
        auto flat = base();
        flat.annual_repairs = BD("4800");
        const auto a = *sensen::calculate_rent_vs_buy(flat);

        auto infl = flat;
        infl.annual_inflation_rate = BD("0.03");
        const auto b = *sensen::calculate_rent_vs_buy(infl);

        check(d(b.total_repairs_paid) > d(a.total_repairs_paid),
              "repairs inflate over the horizon");
        check(near(b.total_interest_paid, d(a.total_interest_paid), 1.0),
              "while a FIXED-RATE mortgage's interest does not -- inflating it is the slip");
        check(near(b.total_principal_paid, d(a.total_principal_paid), 1.0),
              "and neither does its principal");
    }

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
