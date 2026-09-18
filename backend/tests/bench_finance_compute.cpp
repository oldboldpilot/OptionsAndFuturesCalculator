/** Raw COMPUTE latency of the finance kernels, with no gRPC and no network.
 *
 *  This exists because a round-trip figure minus an estimate of the network is
 *  an inference, not a measurement, and the optimisation policy in
 *  sensen/config/cpp_details.txt (rule 57) asks for the bottleneck to be
 *  DISCOVERED rather than assumed. Every number here is a direct call into
 *  sensen::* on this thread.
 *
 *  Reported as ns/call so a kernel that costs less than a microsecond is not
 *  rounded into looking free.
 *
 *  @author Olumuyiwa Oluwasanmi
 */
#include <cstdio>
#include <new>
import std;
import sensen.bigdecimal;
import sensen.financial;

namespace {

/** Keeps a result observable so the optimiser cannot delete the work being
 *  timed. A `volatile` sink is deliberate: an accumulator the compiler can see
 *  is dead would make every row below report the cost of an empty loop. */
volatile double g_sink = 0.0;

template <typename F>
auto time_ns(std::string_view label, int iters, F&& f) -> void {
    for (int i = 0; i < 16; ++i) { g_sink += f(); }  // warm
    const auto t0 = std::chrono::steady_clock::now();
    double acc = 0.0;
    for (int i = 0; i < iters; ++i) { acc += f(); }
    const auto t1 = std::chrono::steady_clock::now();
    g_sink += acc;
    const double ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / iters;
    if (ns < 1000.0) {
        std::println("{:<34} {:10.1f} ns", label, ns);
    } else {
        std::println("{:<34} {:10.1f} ns   ({:.3f} us)", label, ns, ns / 1000.0);
    }
}

[[nodiscard]] auto make_rvb() -> sensen::RentVsBuyInput {
    sensen::RentVsBuyInput in{};
    in.property_price = sensen::BigDecimal(437900.0);
    in.down_payment = sensen::BigDecimal(105096.0);
    in.annual_home_appreciation = sensen::BigDecimal(0.03);
    in.current_monthly_rent = sensen::BigDecimal(2800.0);
    in.annual_rent_increase = sensen::BigDecimal(0.043);
    in.annual_investment_return = sensen::BigDecimal(0.07);
    in.years = 7;
    in.loan_annual_rate = sensen::BigDecimal(0.061);
    in.loan_term_years = 30;
    in.monthly_taxes_ins_maintenance = sensen::BigDecimal(560.0);
    return in;
}

[[nodiscard]] auto make_rental() -> sensen::RentalCashFlowInput {
    sensen::RentalCashFlowInput in{};
    in.property_price = sensen::BigDecimal(420000.0);
    in.down_payment = sensen::BigDecimal(84000.0);
    in.closing_costs = sensen::BigDecimal(9500.0);
    in.loan_annual_rate = sensen::BigDecimal(0.0665);
    in.loan_term_years = 30;
    in.monthly_gross_rent = sensen::BigDecimal(3100.0);
    in.annual_rent_growth = sensen::BigDecimal(0.03);
    in.occupancy_rate = sensen::BigDecimal(0.90);
    in.annual_property_tax = sensen::BigDecimal(5400.0);
    in.annual_insurance = sensen::BigDecimal(1800.0);
    in.annual_repairs = sensen::BigDecimal(2100.0);
    in.annual_capex_reserve = sensen::BigDecimal(2500.0);
    in.monthly_hoa = sensen::BigDecimal(120.0);
    in.management_fee_rate = sensen::BigDecimal(0.08);
    in.annual_expense_growth = sensen::BigDecimal(0.025);
    in.annual_appreciation = sensen::BigDecimal(0.03);
    in.selling_cost_percent = sensen::BigDecimal(0.06);
    in.years = 10;
    return in;
}

}  // namespace

auto main(int argc, char** argv) -> int {
    const int iters = argc > 1 ? std::atoi(argv[1]) : 2000;

    std::println("raw compute, no gRPC, no network   ({} iterations each)\n", iters);

    // ---- the BigDecimal primitives everything above is built from ----------
    const sensen::BigDecimal loan0(495000.0);
    const sensen::BigDecimal arate0(0.0675);
    const sensen::BigDecimal a(1.0051234567);
    const sensen::BigDecimal b(437900.125);
    time_ns("BigDecimal::multiply", iters * 500, [&] { return a.multiply(b).to_double(); });
    time_ns("BigDecimal::divide", iters * 500,
            [&] { return b.divide(a).value_or(sensen::BigDecimal(0)).to_double(); });
    time_ns("BigDecimal::pow(360)", iters * 50, [&] { return a.pow(360).to_double(); });

    // ---- the SERIALISATION path, which is what every RPC field goes through
    // and which a to_double()-based benchmark cannot see at all --------------
    time_ns("BigDecimal::to_string", iters * 100,
            [&] { return static_cast<double>(b.to_string().size()); });
    time_ns("amort 360m + to_string x6/row", iters / 4, [&] {
        auto [rows, sum] = sensen::calculate_mortgage_amortization(loan0, arate0, 360);
        std::size_t n = 0;
        for (const auto& r : rows) {
            n += r.scheduled_payment.to_string().size() + r.principal_paid.to_string().size()
               + r.interest_paid.to_string().size() + r.end_balance.to_string().size();
        }
        return static_cast<double>(n);
    });

    // ---- the RPC kernels ---------------------------------------------------
    const sensen::BigDecimal rate(0.005625);
    const sensen::BigDecimal pv(495000.0);
    time_ns("pmt            [ComputePayment]", iters * 50,
            [&] { return sensen::pmt(rate, 360, pv).to_double(); });

    const sensen::BigDecimal loan(495000.0);
    const sensen::BigDecimal arate(0.0675);
    time_ns("amortization 360m  [ComputeAmort]", iters, [&] {
        auto [rows, sum] = sensen::calculate_mortgage_amortization(loan, arate, 360);
        return static_cast<double>(rows.size());
    });

    const auto rvb = make_rvb();
    time_ns("rent_vs_buy 7y  [ComputeRentVsBuy]", iters,
            [&] { return sensen::calculate_rent_vs_buy(rvb).has_value() ? 1.0 : 0.0; });

    const auto rcf = make_rental();
    time_ns("rental_cash_flow 10y  [RentalCF]", iters,
            [&] { return sensen::calculate_rental_cash_flow(rcf).has_value() ? 1.0 : 0.0; });

    // ---- the batch path: 1000 INDEPENDENT scenarios ------------------------
    std::vector<sensen::RentVsBuyInput> batch(1000, rvb);
    time_ns("rent_vs_buy_batch_cpu n=1000", std::max(iters / 20, 5), [&] {
        auto out = sensen::calculate_rent_vs_buy_batch_cpu(std::span<const sensen::RentVsBuyInput>(batch));
        return static_cast<double>(out.size());
    });

    return 0;
}
