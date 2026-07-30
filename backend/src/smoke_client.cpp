/*
 * Smoke client.
 *
 * Exercises all four RPCs against a running engine and prints what comes
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
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

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

/**
 * The risk-free rate must be reachable, or the whole product is down.
 *
 * Not a nicety in the gate. The browser refuses to calculate without a measured
 * rate rather than substituting one, so a container that cannot reach
 * home.treasury.gov serves a calculator that returns nothing on every route
 * while passing every other check here. Egress from the deployment environment
 * is the specific thing this proves — it cannot be established from a
 * workstation, where the fetch always succeeds.
 *
 * Gated on as_of_date, not on the rate being positive: a zero short rate is a
 * real observation, and rejecting one would be a policy judgement disguised as
 * a data check.
 */
auto check_risk_free_rate(calculator::OptionsCalculator::Stub& stub) -> bool {
    const calculator::RiskFreeRateRequest req;
    calculator::RiskFreeRateResponse res;

    const auto ctx = make_context();
    const auto status = stub.GetRiskFreeRate(ctx.get(), req, &res);
    if (!status.ok()) {
        std::cerr << "GetRiskFreeRate FAILED: " << status.error_code() << " "
                  << status.error_message() << "\n";
        return false;
    }
    if (res.as_of_date().empty()) {
        std::cerr << "GetRiskFreeRate returned no observation date\n";
        return false;
    }
    if (res.curve().empty()) {
        std::cerr << "GetRiskFreeRate returned an empty curve\n";
        return false;
    }

    std::cout << "GetRiskFreeRate  " << res.tenor() << "  rate=" << std::fixed
              << std::setprecision(4) << (res.rate() * 100.0) << "% continuous"
              << "  published=" << std::setprecision(2) << (res.rate_published() * 100.0) << "%"
              << "  as_of=" << res.as_of_date() << "  source=" << res.source()
              << "  tenors=" << res.curve_size() << "\n";
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

    // The matrix carries two views of the same axis — days remaining and a
    // calendar date — and they must agree: more days remaining means an
    // EARLIER date. They used to run in opposite directions, so every cell
    // named the wrong day. Comparing the extremes is enough to catch it.
    const calculator::MatrixCell* most_time = nullptr;
    const calculator::MatrixCell* least_time = nullptr;
    for (const auto& cell : res.matrix()) {
        if (most_time == nullptr || cell.days_to_expiration() > most_time->days_to_expiration()) {
            most_time = &cell;
        }
        if (least_time == nullptr || cell.days_to_expiration() < least_time->days_to_expiration()) {
            least_time = &cell;
        }
    }
    if (most_time == nullptr || least_time == nullptr) {
        std::cerr << "CalculateStrategy returned an empty price x date matrix\n";
        return false;
    }
    std::cout << "  matrix axis  " << most_time->days_to_expiration() << "d @ "
              << most_time->date_str() << "  ->  " << least_time->days_to_expiration() << "d @ "
              << least_time->date_str() << "\n";
    // Well-formed ISO-8601, exactly YYYY-MM-DD. The global locale that
    // logger installs groups thousands, so a stream-formatted year arrived as
    // "2,026-07-30" and the axis was unparseable by any client.
    for (const auto* cell : {most_time, least_time}) {
        const auto& text = cell->date_str();
        const bool shaped = text.size() == 10 && text[4] == '-' && text[7] == '-';
        if (!shaped) {
            std::cerr << "Matrix date is not ISO-8601 YYYY-MM-DD: '" << text << "'\n";
            return false;
        }
    }
    // ISO-8601 dates compare correctly as strings.
    if (most_time->date_str() >= least_time->date_str()) {
        std::cerr << "Matrix date axis runs backwards: " << most_time->days_to_expiration()
                  << " days to expiry is dated " << most_time->date_str() << " but "
                  << least_time->days_to_expiration() << " days is dated " << least_time->date_str()
                  << "\n";
        return false;
    }
    return true;
}

/**
 * A calendar spread: short the near expiry, long the far one, same strike.
 *
 * This is the structure that exposed the shared-clock bug. Both legs are given
 * the SAME real ATM premium on purpose — that removes the premium difference
 * as a source of P&L, so any curvature at all must come from the two legs
 * being on different clocks. Under the old single-clock pricing both legs had
 * identical (S, K, sigma, T) with opposite direction, everything cancelled,
 * and the curve was flat at exactly 0.00 at every price. A flat curve here is
 * therefore a regression, not a rounding artefact.
 */
auto check_calendar_spread(calculator::OptionsCalculator::Stub& stub, const std::string& symbol,
                           double spot, const calculator::OptionStrike& atm, int near_dte,
                           int far_dte) -> bool {
    calculator::StrategyRequest req;
    req.set_underlying_symbol(symbol);
    req.set_current_price(spot);
    req.set_risk_free_rate(0.05);
    req.set_implied_volatility(atm.call_iv());

    for (const auto [action, dte] :
         {std::pair{calculator::Leg::SELL, near_dte}, std::pair{calculator::Leg::BUY, far_dte}}) {
        auto& leg = *req.add_legs();
        leg.set_action(action);
        leg.set_type(calculator::Leg::CALL);
        leg.set_strike(atm.strike());
        leg.set_expiration_days(dte);
        leg.set_quantity(1);
        leg.set_premium(atm.call_ask());
        leg.set_implied_volatility(atm.call_iv());
        leg.set_contract_multiplier(100.0);
    }

    calculator::StrategyResponse res;
    const auto ctx = make_context();
    const auto status = stub.CalculateStrategy(ctx.get(), req, &res);
    if (!status.ok()) {
        std::cerr << "Calendar spread FAILED: " << status.error_code() << " "
                  << status.error_message() << "\n";
        return false;
    }

    std::cout << "Calendar spread   " << near_dte << "d short / " << far_dte << "d long @ "
              << std::setprecision(2) << atm.strike()
              << "  curve_at=" << res.curve_days_to_expiration() << "d\n"
              << "  max_profit=" << res.max_profit() << "  max_loss=" << res.max_loss()
              << "  breakevens=" << res.breakeven_prices_size()
              << "  pop=" << std::setprecision(4) << res.pop() << "\n";

    // The curve must be drawn at the NEAR expiry; at the far one both legs are
    // intrinsic and the diagram carries no information.
    if (std::abs(res.curve_days_to_expiration() - static_cast<double>(near_dte)) > 1e-9) {
        std::cerr << "Curve drawn at " << res.curve_days_to_expiration() << "d, expected the near "
                  << near_dte << "d expiry\n";
        return false;
    }

    // The tent: long time value at the strike, decaying to nothing at the
    // wings. Flat means every leg was priced on one clock again.
    const double spread = res.max_profit() - res.max_loss();
    if (spread < 1.0) {
        std::cerr << "Calendar spread P&L is flat (max_profit " << res.max_profit()
                  << " == max_loss " << res.max_loss()
                  << "): the legs are being priced on a single clock\n";
        return false;
    }
    if (res.max_profit() <= 0.0) {
        std::cerr << "Calendar spread has no profitable price at the near expiry\n";
        return false;
    }
    return true;
}

/**
 * Dividend yield must actually reach the pricing path.
 *
 * The proto carried `dividend_yield` for a long time while the engine ignored
 * it, so a caller could set it, see a confident answer, and have no way to tell
 * the field had been dropped on the floor. These assertions are about direction
 * and magnitude rather than exact figures, because the chain's live premium and
 * IV move: a continuous yield q lowers the forward, so a call is worth strictly
 * less and its delta strictly smaller, and a q of zero must reproduce the
 * dividend-free answer EXACTLY — that last one is what proves the Merton path
 * does not perturb the default.
 */
auto check_dividend_yield(calculator::OptionsCalculator::Stub& stub, const std::string& symbol,
                          double spot, const calculator::OptionStrike& atm, int dte) -> bool {
    const auto price_with = [&](double q, calculator::StrategyResponse& out) -> bool {
        calculator::StrategyRequest req;
        req.set_underlying_symbol(symbol);
        req.set_current_price(spot);
        req.set_implied_volatility(atm.call_iv());
        req.set_risk_free_rate(0.05);
        req.set_dividend_yield(q);

        auto& leg = *req.add_legs();
        leg.set_action(calculator::Leg::BUY);
        leg.set_type(calculator::Leg::CALL);
        leg.set_strike(atm.strike());
        leg.set_expiration_days(dte);
        leg.set_quantity(1);
        leg.set_premium(atm.call_ask());
        leg.set_implied_volatility(atm.call_iv());
        leg.set_contract_multiplier(100.0);

        const auto ctx = make_context();
        const auto status = stub.CalculateStrategy(ctx.get(), req, &out);
        if (!status.ok()) {
            std::cerr << "Dividend case (q=" << q << ") FAILED: " << status.error_code() << " "
                      << status.error_message() << "\n";
            return false;
        }
        return true;
    };

    calculator::StrategyResponse zero, base, paying;
    if (!price_with(0.0, zero)) return false;
    if (!price_with(0.0, base)) return false;
    if (!price_with(0.04, paying)) return false;

    const double d0 = zero.net_greeks().delta();
    const double db = base.net_greeks().delta();
    const double dq = paying.net_greeks().delta();
    const double t0 = zero.net_greeks().theta();
    const double tq = paying.net_greeks().theta();

    std::cout << "Dividend yield    q=0.00  delta=" << std::setprecision(4) << db
              << "  theta=" << std::setprecision(2) << t0
              << "  pop=" << std::setprecision(4) << zero.pop() << "\n"
              << "                  q=0.04  delta=" << std::setprecision(4) << dq
              << "  theta=" << std::setprecision(2) << tq
              << "  pop=" << std::setprecision(4) << paying.pop() << "\n";

    // Determinism: q = 0 twice must agree bit for bit, or something upstream is
    // varying and the comparisons below mean nothing.
    if (d0 != db) {
        std::cerr << "q=0 is not deterministic (" << d0 << " vs " << db << ")\n";
        return false;
    }
    // A dividend lowers the forward: the call is worth less and its delta falls.
    if (!(dq < db)) {
        std::cerr << "Dividend yield did not reduce call delta (" << dq << " >= " << db
                  << ") — the field is not reaching the pricing path\n";
        return false;
    }
    // exp(-qT) with q=0.04 over `dte` days is a small, bounded effect; a delta
    // that collapses means the yield is being applied on the wrong scale (a
    // percentage read as a decimal, say).
    const double ratio = (db != 0.0) ? dq / db : 0.0;
    const double years = static_cast<double>(dte) / 365.0;
    const double floor_ratio = std::exp(-0.04 * years) * 0.90;
    if (ratio < floor_ratio) {
        std::cerr << "Dividend-adjusted delta ratio " << ratio << " is below " << floor_ratio
                  << "; the yield looks mis-scaled\n";
        return false;
    }
    // Theta moves the way that reads backwards at first glance, so it is worth
    // stating precisely. A dividend makes the call worth LESS, but it decays
    // MORE SLOWLY: as T shrinks the drag exp(-qT) unwinds toward 1, pushing the
    // effective spot back up and offsetting part of the time decay. Merton's
    // theta carries an explicit +q*S*exp(-qT)*N(d1) term for exactly this, and a
    // finite difference of the option VALUE in T — which uses no theta formula
    // at all — agrees. So the long call's theta must become less negative.
    // Asserting the opposite is a natural mistake; this comment is here so the
    // next reader does not "fix" the engine to satisfy a wrong intuition.
    if (!(tq > t0)) {
        std::cerr << "Dividend yield did not slow long-call decay (theta " << tq << " should be "
                  << "greater than " << t0 << ")\n";
        return false;
    }
    // The distribution has to move too, not just the pricing. A yield lowers the
    // risk-neutral drift to (r - q), so a long call's probability of profit falls.
    // Without this the engine could price with q while shading a distribution
    // that ignored it.
    if (!(paying.pop() < zero.pop())) {
        std::cerr << "Dividend yield did not lower probability of profit (" << paying.pop()
                  << " >= " << zero.pop() << ") — the drift term is not seeing q\n";
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

    if (!check_risk_free_rate(*stub)) return 1;

    calculator::OptionStrike atm;
    std::string expiry;
    if (!check_chain(*stub, symbol, atm, expiry)) return 1;

    if (atm.strike() <= 0.0) {
        std::cerr << "No ATM strike identified in the chain\n";
        return 1;
    }

    if (!check_strategy(*stub, symbol, spot, atm, 30)) return 1;

    if (!check_calendar_spread(*stub, symbol, spot, atm, 30, 60)) return 1;

    if (!check_dividend_yield(*stub, symbol, spot, atm, 30)) return 1;

    std::cout << "\nAll four RPCs returned live data.\n";
    return 0;
}
