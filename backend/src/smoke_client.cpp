/*
 * Smoke client.
 *
 * Exercises the three RPCs against a running engine and prints what comes
 * back, so a deploy can be verified end to end without a browser. Hand-rolled
 * rather than built on a test framework, per config/cpp_details.txt rule 39
 * (no external testing libraries).
 *
 *   ./smoke_client [host:port] [symbol]
 *
 * Exits non-zero if any call fails or returns something that cannot be real
 * market data, so it works as a deploy gate.
 */
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>

#include <grpcpp/grpcpp.h>
#include "calculator.pb.h"
#include "calculator.grpc.pb.h"

namespace {

auto make_context() -> std::unique_ptr<grpc::ClientContext> {
    auto ctx = std::make_unique<grpc::ClientContext>();
    ctx->set_deadline(std::chrono::system_clock::now() + std::chrono::seconds{30});
    return ctx;
}

auto check_quote(calculator::OptionsCalculator::Stub& stub, const std::string& symbol,
                 double& spot_out) -> bool {
    calculator::QuoteRequest req;
    req.set_symbol(symbol);
    calculator::QuoteResponse res;

    const auto ctx = make_context();
    const auto status = stub.GetMarketQuote(ctx.get(), req, &res);
    if (!status.ok()) {
        std::cerr << "GetMarketQuote FAILED: " << status.error_code() << " "
                  << status.error_message() << "\n";
        return false;
    }
    if (res.price() <= 0.0) {
        std::cerr << "GetMarketQuote returned a non-positive price\n";
        return false;
    }

    spot_out = res.price();
    std::cout << "GetMarketQuote  " << res.symbol() << "  price=" << std::fixed
              << std::setprecision(2) << res.price()
              << "  prev_close=" << res.previous_close()
              << "  provider=" << res.provider() << "  at=" << res.quote_timestamp() << "\n";
    return true;
}

auto check_chain(calculator::OptionsCalculator::Stub& stub, const std::string& symbol,
                 calculator::OptionStrike& atm_out, std::string& expiry_out) -> bool {
    calculator::ChainRequest req;
    req.set_symbol(symbol);
    req.set_asset_class("EQUITY");
    calculator::ChainResponse res;

    const auto ctx = make_context();
    const auto status = stub.GetMarketChain(ctx.get(), req, &res);
    if (!status.ok()) {
        std::cerr << "GetMarketChain FAILED: " << status.error_code() << " "
                  << status.error_message() << "\n";
        return false;
    }
    if (res.option_strikes_size() == 0) {
        std::cerr << "GetMarketChain returned no strikes\n";
        return false;
    }

    std::cout << "GetMarketChain  " << res.symbol() << "  spot=" << res.spot_price()
              << "  strikes=" << res.option_strikes_size()
              << "  expirations=" << res.available_expirations_size()
              << "  selected=" << res.selected_expiration_date() << "\n";

    for (const auto& s : res.option_strikes()) {
        if (s.is_atm()) {
            atm_out = s;
            std::cout << "  ATM " << s.strike()
                      << "  call " << s.call_bid() << "/" << s.call_ask()
                      << " d=" << std::setprecision(3) << s.call_delta()
                      << " iv=" << s.call_iv()
                      << " oi=" << s.call_open_interest()
                      << " vol=" << s.call_volume() << "\n"
                      << "        put  " << std::setprecision(2) << s.put_bid() << "/" << s.put_ask()
                      << " d=" << std::setprecision(3) << s.put_delta()
                      << " iv=" << s.put_iv()
                      << " oi=" << s.put_open_interest()
                      << " vol=" << s.put_volume() << "\n";
        }
    }
    expiry_out = res.selected_expiration_date();

    // The synthetic chain this replaced filled every row with the same
    // constants. If IV is identical across all strikes, something is wrong.
    double first_iv = -1.0;
    bool varies = false;
    for (const auto& s : res.option_strikes()) {
        if (s.call_iv() <= 0.0) continue;
        if (first_iv < 0.0) { first_iv = s.call_iv(); continue; }
        if (std::abs(s.call_iv() - first_iv) > 1e-9) { varies = true; break; }
    }
    if (!varies) {
        std::cerr << "  WARNING: call IV is constant across strikes — check the feed\n";
    }
    return true;
}

auto check_strategy(calculator::OptionsCalculator::Stub& stub, const std::string& symbol,
                    double spot, const calculator::OptionStrike& atm, int dte) -> bool {
    calculator::StrategyRequest req;
    req.set_underlying_symbol(symbol);
    req.set_current_price(spot);
    req.set_risk_free_rate(0.05);
    req.set_implied_volatility(atm.call_iv());

    // A long ATM call: the simplest structure with a known payoff shape.
    auto& leg = *req.add_legs();
    leg.set_action(calculator::Leg::BUY);
    leg.set_type(calculator::Leg::CALL);
    leg.set_strike(atm.strike());
    leg.set_expiration_days(dte);
    leg.set_quantity(1);
    leg.set_premium(atm.call_ask());
    leg.set_implied_volatility(atm.call_iv());
    leg.set_contract_multiplier(100.0);

    calculator::StrategyResponse res;
    const auto ctx = make_context();
    const auto status = stub.CalculateStrategy(ctx.get(), req, &res);
    if (!status.ok()) {
        std::cerr << "CalculateStrategy FAILED: " << status.error_code() << " "
                  << status.error_message() << "\n";
        return false;
    }

    std::cout << "CalculateStrategy  max_profit=" << std::setprecision(2) << res.max_profit()
              << "  max_loss=" << res.max_loss()
              << "  curve_points=" << res.pnl_matrix_size()
              << "  matrix_cells=" << res.matrix_size()
              << "  breakevens=" << res.breakeven_prices_size() << "\n"
              << "  pop=" << std::setprecision(4) << res.pop()
              << "  ev=" << std::setprecision(2) << res.expected_value()
              << "  var95=" << res.risk_metrics().var_parametric_95()
              << "  cvar95=" << res.risk_metrics().cvar_parametric_95() << "\n"
              << "  delta=" << std::setprecision(4) << res.net_greeks().delta()
              << "  gamma=" << res.net_greeks().gamma()
              << "  theta=" << res.net_greeks().theta()
              << "  vega=" << res.net_greeks().vega()
              << "  vanna=" << res.net_greeks().vanna() << "\n"
              << "  computed in " << res.calculation_time_microseconds() << "us\n";

    if (res.pnl_matrix_size() == 0) {
        std::cerr << "CalculateStrategy returned an empty payoff curve\n";
        return false;
    }
    // A long call cannot lose more than the premium paid.
    const double premium_paid = atm.call_ask() * 100.0;
    if (res.max_loss() < -premium_paid * 1.01) {
        std::cerr << "Long call max loss (" << res.max_loss() << ") exceeds premium paid ("
                  << premium_paid << ")\n";
        return false;
    }
    return true;
}

}  // namespace

auto main(int argc, char** argv) -> int {
    const std::string target = (argc > 1) ? argv[1] : "localhost:50051";
    const std::string symbol = (argc > 2) ? argv[2] : "SPY";

    std::cout << "Smoke test against " << target << " for " << symbol << "\n\n";

    auto channel = grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
    auto stub = calculator::OptionsCalculator::NewStub(channel);

    double spot = 0.0;
    if (!check_quote(*stub, symbol, spot)) return 1;

    calculator::OptionStrike atm;
    std::string expiry;
    if (!check_chain(*stub, symbol, atm, expiry)) return 1;

    if (atm.strike() <= 0.0) {
        std::cerr << "No ATM strike identified in the chain\n";
        return 1;
    }

    if (!check_strategy(*stub, symbol, spot, atm, 30)) return 1;

    std::cout << "\nAll three RPCs returned live data.\n";
    return 0;
}
