/** Latency of the derivation layer. @author Olumuyiwa Oluwasanmi */
#include <cstdio>
#include <new>
import std;
import mortgage_derivation;
namespace md = mortgage_calculator::assistant::derive;

auto main(int argc, char** argv) -> int {
    const int iters = argc > 1 ? std::atoi(argv[1]) : 200;
    const std::array<std::pair<std::string_view, std::string_view>, 4> cases{{
        {"ComputePayment", "Payment on $323,300 at 5.64% over 30-year?"},
        {"ComputeAmortization",
         "Amortize the loan on a $796,000 property, a 10% deposit, 5.97%, 30-year."},
        {"ComputeClosingCosts",
         "Closing costs on a $450,000 home with 10% down at 6.75%: 0.75% origination, 0% discount "
         "points, $1,400 other lender fees, 0.55% title, $650 appraisal, 15 days prepaid."},
        {"ComputeRentVsBuy",
         "Rent or buy over 7 years? Rent $2,800/month, 4.3% increase. Buy: $437,900, 24% down, "
         "6.1% over 30-year, 1.1% tax, $1,900 insurance, 7% investment return."},
    }};
    // warm
    for (const auto& [op, u] : cases) { (void)md::derive_candidates(op, u); }

    for (const auto& [op, u] : cases) {
        const auto t0 = std::chrono::steady_clock::now();
        std::size_t fields = 0;
        for (int i = 0; i < iters; ++i) { fields += md::derive_candidates(op, u).size(); }
        const auto t1 = std::chrono::steady_clock::now();
        const double us =
            std::chrono::duration<double, std::micro>(t1 - t0).count() / iters;
        std::println("{:<22} {:8.1f} us/call   ({} fields derived)", op, us, fields / iters);
    }
    return 0;
}
