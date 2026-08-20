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
#include <array>
#include <chrono>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <grpcpp/grpcpp.h>
#include "calculator.pb.h"
#include "calculator.grpc.pb.h"
#include "finance.pb.h"
#include "finance.grpc.pb.h"
#include "assistant.pb.h"
#include "assistant.grpc.pb.h"

namespace {

/** A context with no identity. Used only where the anonymous tier is the point.
 *
 * Takes the deadline as a parameter, defaulted to the 30 seconds every
 * existing check was written against, so every current call site is
 * unaffected. The one caller that needs longer (the LLM assistant suite,
 * see kAssistantDeadline below) passes it explicitly instead of this
 * function growing a special case for one subsystem. */
auto make_anonymous_context(std::chrono::seconds deadline = std::chrono::seconds{30})
    -> std::unique_ptr<grpc::ClientContext> {
    auto ctx = std::make_unique<grpc::ClientContext>();
    ctx->set_deadline(std::chrono::system_clock::now() + deadline);
    return ctx;
}

/**
 * The context every functional check uses.
 *
 * Carries SMOKE_API_KEY when set, because otherwise this gate throttles itself:
 * it makes dozens of calls, and against a small anonymous tier the later checks
 * fail on quota rather than on anything they were written to test. A monitoring
 * client having its own key is also what a real deployment looks like.
 */
auto make_context(std::chrono::seconds deadline = std::chrono::seconds{30})
    -> std::unique_ptr<grpc::ClientContext> {
    auto ctx = make_anonymous_context(deadline);
    if (const char* key = std::getenv("SMOKE_API_KEY"); key != nullptr && *key != '\0') {
        ctx->AddMetadata("x-api-key", key);
    }
    // A Supabase access token, presented the way every Supabase client presents
    // one. Separate from SMOKE_API_KEY so the two identity paths can be
    // exercised independently -- and so a test can send both at once, which is
    // what a signed-in user on a page carrying the site's own key actually does.
    if (const char* jwt = std::getenv("SMOKE_BEARER"); jwt != nullptr && *jwt != '\0') {
        ctx->AddMetadata("authorization", std::string{"Bearer "} + jwt);
    }
    return ctx;
}

/**
 * The deadline for every call the "llm" suite makes -- 120 seconds, four
 * times the default, and it earns every extra second of it.
 *
 * The measured ground truth (~1.1s per extraction warm, ~34 tok/s) is not
 * what this deadline has to cover. RegisterAssistantService constructs
 * AssistantWorker -- which attempts to load a 639 MB GGUF off MODEL_PATH --
 * BEFORE main.cpp calls BuildAndStart, so the gRPC port does not open until
 * that load has already finished or failed. A smoke run launched right after
 * a deploy, with nothing else gating it on a readiness probe, is therefore
 * racing that entire startup critical path, not just one inference call:
 * the client's connection attempt blocks until the port exists at all, and
 * on a cold container filesystem that is dominated by disk I/O rather than
 * inference. Reading 639 MB at a conservative 20 MB/s takes ~32s; at a
 * pessimistic 10 MB/s (a cold, contended volume, which is the failure mode
 * worth budgeting for) it is ~64s. Add a few more seconds for llama.cpp to
 * parse the GGUF header, allocate the KV cache and build the compute graph,
 * and the warm ~1.1s for the actual ParseStrategy call once the port is
 * finally open, and the worst case this function is designed to survive is
 * on the order of 70-80 seconds. 120s leaves real headroom above that
 * without being so long that a genuinely hung assistant takes minutes to be
 * reported as FAILED.
 */
constexpr std::chrono::seconds kAssistantDeadline{120};

/**
 * Formats a double as a full-precision decimal string for a BigDecimal wire
 * field.
 *
 * `std::to_string(double)` truncates to 6 fractional digits, which is not
 * precise enough here: several of the new finance identities (payoff timing
 * in particular) are constructed so the closed-form answer lands exactly on
 * an integer month boundary, and a 6-digit-truncated payment can nudge the
 * engine's nper across that boundary while the test's own math -- computed
 * from the untruncated double -- does not. Twelve fractional digits is far
 * inside a double's ~15-17 significant digits, so this is lossless for any
 * value these tests construct.
 */
[[nodiscard]] auto dec(double v) -> std::string {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(12) << v;
    return oss.str();
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

    // Per-leg risk must reconcile with the aggregate. This is the property that
    // makes the breakdown worth showing: if the parts do not sum to the whole,
    // one of the two is computed from something the other did not see, and a
    // trader hedging off the per-leg numbers would be hedging a fiction.
    if (res.leg_risk_size() != 2) {
        std::cerr << "Expected leg_risk for both legs, got " << res.leg_risk_size() << "\n";
        return false;
    }
    double sum_delta = 0.0, sum_theta = 0.0, sum_vega = 0.0;
    for (const auto& lr : res.leg_risk()) {
        sum_delta += lr.greeks().delta();
        sum_theta += lr.greeks().theta();
        sum_vega  += lr.greeks().vega();
    }
    std::cout << "  leg risk     sum delta=" << std::setprecision(4) << sum_delta
              << " vs net " << res.net_greeks().delta()
              << " | sum theta=" << std::setprecision(2) << sum_theta
              << " vs net " << res.net_greeks().theta() << "\n";
    const auto close = [](double a, double b) {
        return std::abs(a - b) <= 1e-9 * std::max(1.0, std::max(std::abs(a), std::abs(b)));
    };
    if (!close(sum_delta, res.net_greeks().delta()) ||
        !close(sum_theta, res.net_greeks().theta()) ||
        !close(sum_vega,  res.net_greeks().vega())) {
        std::cerr << "Per-leg Greeks do not sum to net_greeks\n";
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

/**
 * The futures term structure is derived, and the gate checks it as derived.
 *
 * Forward price is cost-of-carry off a measured spot and a measured rate, so
 * the assertions are about internal consistency and provenance rather than
 * about matching a quote there is no vendor for: the curve must carry the
 * MODELLED marker, must be monotonic in carry when the rate is positive, and
 * must leave bid, ask, volume and open interest EMPTY. That last one is the
 * one that matters — those are order-book facts, no formula produces them, and
 * filling them with plausible numbers is exactly the failure the real-data-only
 * requirement exists to prevent.
 */
auto check_term_structure(calculator::OptionsCalculator::Stub& stub) -> bool {
    calculator::ChainRequest req;
    req.set_symbol("ES");
    req.set_asset_class("FUTURES");
    calculator::ChainResponse res;

    const auto ctx = make_context();
    const auto status = stub.GetMarketChain(ctx.get(), req, &res);
    if (!status.ok()) {
        std::cerr << "Term structure FAILED: " << status.error_code() << " "
                  << status.error_message() << "\n";
        return false;
    }
    if (res.futures_contracts_size() == 0) {
        std::cerr << "Term structure returned no contracts\n";
        return false;
    }

    const double spot = res.spot_price();
    std::cout << "Term structure    ES spot=" << std::setprecision(2) << spot
              << "  contracts=" << res.futures_contracts_size() << "\n";
    for (int i = 0; i < std::min(3, res.futures_contracts_size()); ++i) {
        const auto& c = res.futures_contracts(i);
        std::cout << "  " << c.code() << "  " << c.delivery_month()
                  << "  " << c.days_to_expiry() << "d"
                  << "  fwd=" << std::setprecision(2) << c.futures_price()
                  << "  basis=" << c.basis()
                  << "  carry=" << std::setprecision(4) << (c.annualized_yield() * 100.0) << "%"
                  << "  [" << c.state() << "]\n";
    }

    double prev_days = -1.0, prev_basis = -1e18;
    for (const auto& c : res.futures_contracts()) {
        if (c.state() != "MODELLED") {
            std::cerr << "Contract " << c.code() << " is not marked MODELLED (" << c.state()
                      << ") — a derived curve must not look like a quote\n";
            return false;
        }
        if (c.bid() != 0.0 || c.ask() != 0.0 || c.volume() != 0 || c.open_interest() != 0) {
            std::cerr << "Contract " << c.code() << " carries fabricated book data\n";
            return false;
        }
        if (c.futures_price() <= 0.0 || c.days_to_expiry() <= 0) {
            std::cerr << "Contract " << c.code() << " has a non-positive price or tenor\n";
            return false;
        }
        // Positive carry means the curve rises with tenor, and the basis with it.
        if (c.annualized_yield() > 0.0) {
            if (c.futures_price() <= spot) {
                std::cerr << "Positive carry but " << c.code() << " prices at or below spot\n";
                return false;
            }
            if (c.days_to_expiry() <= prev_days) {
                std::cerr << "Contracts are not ordered by tenor\n";
                return false;
            }
            if (c.basis() <= prev_basis) {
                std::cerr << "Basis does not widen with tenor under positive carry\n";
                return false;
            }
        }
        prev_days = c.days_to_expiry();
        prev_basis = c.basis();
    }

    // A curve is only allowed where the underlying is actually known. Roots that
    // collide with a listed equity of the same ticker must REFUSE rather than
    // price a mining stock or an oil major and call it a futures curve — the
    // failure that made ES read 71 instead of 7400.
    for (const char* root : {"GC", "CL", "NG", "SI", "ZB"}) {
        calculator::ChainRequest r2;
        r2.set_symbol(root);
        r2.set_asset_class("FUTURES");
        calculator::ChainResponse res2;
        const auto c2 = make_context();
        const auto s2 = stub.GetMarketChain(c2.get(), r2, &res2);
        if (s2.ok()) {
            std::cerr << root << " returned a curve, but no futures quote source is mapped "
                      << "for it — that curve is priced off the equity of the same name\n";
            return false;
        }
    }
    std::cout << "  unmapped roots  GC CL NG SI ZB all refuse rather than mispricing\n";

    // The mapped roots must both work and land at an index-like level.
    for (const auto& [root, floor] : {std::pair<const char*, double>{"ES", 1000.0},
                                      std::pair<const char*, double>{"NQ", 1000.0}}) {
        calculator::ChainRequest r3;
        r3.set_symbol(root);
        r3.set_asset_class("FUTURES");
        calculator::ChainResponse res3;
        const auto c3 = make_context();
        const auto s3 = stub.GetMarketChain(c3.get(), r3, &res3);
        if (!s3.ok() || res3.spot_price() < floor) {
            std::cerr << root << " resolved to " << res3.spot_price()
                      << ", below an index level — the equity ticker is being used again\n";
            return false;
        }
        std::cout << "  " << root << " spot " << std::setprecision(2) << res3.spot_price()
                  << " (index level, not the equity of the same ticker)\n";
    }
    return true;
}

/**
 * The quote and the curve must describe the SAME instrument.
 *
 * This is the check that was missing, and its absence is what let a shipped
 * build put an 8-contract E-mini curve around 7500 directly underneath a header
 * reading "ES spot 71.59". Both numbers were real quotes from the same feed.
 * One was the E-mini S&P; the other was Eversource Energy, a utility that
 * happens to own the ticker. Every per-contract assertion in the curve passed,
 * because the curve was right — it was the spot beside it, which prices every
 * leg on the screen, that was the wrong instrument.
 *
 * Checking either RPC alone cannot catch this. Only comparing them can.
 */
auto check_futures_quote(calculator::OptionsCalculator::Stub& stub) -> bool {
    for (const char* root : {"ES", "NQ"}) {
        calculator::QuoteRequest fq;
        fq.set_symbol(root);
        fq.set_asset_class("FUTURES");
        calculator::QuoteResponse fr;
        const auto c1 = make_context();
        if (const auto s1 = stub.GetMarketQuote(c1.get(), fq, &fr); !s1.ok()) {
            std::cerr << root << " as FUTURES has no quote: " << s1.error_message() << "\n";
            return false;
        }

        calculator::ChainRequest cq;
        cq.set_symbol(root);
        cq.set_asset_class("FUTURES");
        calculator::ChainResponse cr;
        const auto c2 = make_context();
        if (const auto s2 = stub.GetMarketChain(c2.get(), cq, &cr); !s2.ok()) {
            std::cerr << root << " as FUTURES has no chain: " << s2.error_message() << "\n";
            return false;
        }

        // Both read the same proxy moments apart, so they may differ by a tick,
        // but not by an instrument. 1% is far tighter than the ~100x gap an
        // equity/futures mix-up produces and far looser than intraday drift
        // between two calls.
        const double q = fr.price(), s = cr.spot_price();
        if (q <= 0.0 || std::abs(q - s) / s > 0.01) {
            std::cerr << root << " quote " << q << " disagrees with chain spot " << s
                      << " — the quote path and the curve are on different instruments\n";
            return false;
        }
        if (fr.provider().find("proxy") == std::string::npos) {
            std::cerr << root << " quote provider is " << fr.provider()
                      << ", which does not disclose that the level is derived\n";
            return false;
        }
        std::cout << "  " << root << " quote " << std::setprecision(2) << q
                  << " agrees with curve spot " << s << "  via " << fr.provider() << "\n";

        // The same ticker asked as an EQUITY is a different instrument and must
        // answer as one. If both classes return the same number, asset_class is
        // being ignored and the agreement above proves nothing.
        calculator::QuoteRequest eq;
        eq.set_symbol(root);
        eq.set_asset_class("EQUITY");
        calculator::QuoteResponse er;
        const auto c3 = make_context();
        if (const auto s3 = stub.GetMarketQuote(c3.get(), eq, &er); s3.ok()) {
            if (std::abs(er.price() - q) < 0.01) {
                std::cerr << root << " prices identically as EQUITY and FUTURES — asset_class "
                          << "is not routing\n";
                return false;
            }
            std::cout << "  " << root << " as EQUITY is " << std::setprecision(2) << er.price()
                      << ", a different instrument, and stays that way\n";
        }
    }

    // A futures root with no priceable proxy must refuse here too. Refusing in
    // the chain while the quote path serves an equity is exactly the split this
    // check exists to close.
    for (const char* root : {"GC", "CL", "NG", "SI", "ZB"}) {
        calculator::QuoteRequest r;
        r.set_symbol(root);
        r.set_asset_class("FUTURES");
        calculator::QuoteResponse res;
        const auto ctx = make_context();
        if (stub.GetMarketQuote(ctx.get(), r, &res).ok()) {
            std::cerr << root << " as FUTURES returned " << res.price()
                      << ", but no futures source is mapped — that is the listed equity\n";
            return false;
        }
    }
    std::cout << "  unmapped roots refuse on the quote path as well as the chain\n";
    return true;
}

/**
 * The sensen financial library, over its own contract.
 *
 * Every assertion below is checked against something derived INDEPENDENTLY of
 * the engine -- a closed-form formula evaluated here, or an identity the answer
 * must satisfy no matter how it was computed. Comparing the engine to a number
 * the engine produced earlier would only detect a crash, and this service's
 * failure mode is a wrong figure, not a crash.
 */
auto check_finance(sensen::finance::Finance::Stub& stub) -> bool {
    // -- Closing costs, against the identities that define the itemisation ---
    //
    // Deliberately NOT checked against figures this engine produced earlier.
    // Two properties define a correct itemisation and neither needs a
    // reference number: the lines SUM to the subtotal, and a credit moves the
    // total WITHOUT moving that subtotal. The one external figure here --
    // origination against its own base -- is recomputed from the request in
    // this file, sharing no code with the engine.
    {
        sensen::finance::ClosingCostsRequest req;
        req.set_home_price("450000");
        req.set_down_payment_percent("0.10");
        req.set_annual_rate("0.0675");
        req.set_origination_fee_percent("0.0075");
        req.set_discount_points_percent("0");
        req.set_other_lender_fees("1400");
        req.set_title_settlement_percent("0.0055");
        req.set_appraisal_fee("650");
        req.set_inspection_fee("500");
        req.set_recording_fees("225");
        req.set_transfer_tax_percent("0.005");
        req.set_homeowners_insurance_annual("2100");
        req.set_property_tax_annual("6300");
        req.set_tax_escrow_months(3);
        req.set_seller_lender_credits("0");
        // prepaid_interest_days deliberately NOT set -- absent means the
        // 15-day convention. Setting it to 0 now means zero days.

        sensen::finance::ClosingCostsResponse res;
        const auto ctx = make_context();
        if (const auto st = stub.ComputeClosingCosts(ctx.get(), req, &res); !st.ok()) {
            std::cerr << "ComputeClosingCosts FAILED: " << st.error_message() << "\n";
            return false;
        }

        const double lines =
            std::stod(res.origination_fee()) + std::stod(res.discount_points()) +
            std::stod(res.other_lender_fees()) + std::stod(res.title_settlement()) +
            std::stod(res.appraisal_fee()) + std::stod(res.inspection_fee()) +
            std::stod(res.recording_fees()) + std::stod(res.transfer_tax()) +
            std::stod(res.homeowners_insurance_prepaid()) +
            std::stod(res.property_tax_escrow()) + std::stod(res.prepaid_interest());
        const double subtotal = std::stod(res.itemised_subtotal());
        if (std::abs(lines - subtotal) > 0.005) {
            std::cerr << "closing-cost lines sum to " << lines << " but subtotal is "
                      << subtotal << "\n";
            return false;
        }
        // EVERY percentage line recomputed here against its OWN base, not just
        // origination. Checking one of them leaves the others free to use the
        // wrong base while the lines still sum to the subtotal -- the sum
        // identity above cannot see a base error, only a dropped term.
        const double price = 450000.0;
        const double loan = price * (1.0 - 0.10);          // 405000
        struct Line { double got; double want; const char* what; };
        const Line lines_v[] = {
            {std::stod(res.loan_amount()),          loan,                 "loan = price - down"},
            {std::stod(res.down_payment()),         price * 0.10,         "down payment = 10% of price"},
            {std::stod(res.origination_fee()),      loan * 0.0075,        "origination = 0.75% of LOAN"},
            {std::stod(res.title_settlement()),     price * 0.0055,       "title = 0.55% of PRICE"},
            {std::stod(res.transfer_tax()),         price * 0.005,        "transfer tax = 0.5% of PRICE"},
            {std::stod(res.property_tax_escrow()),  6300.0 * 3.0 / 12.0,  "escrow = 3/12 of the annual bill"},
            {std::stod(res.prepaid_interest()),     loan * 0.0675 / 365.0 * 15.0, "prepaid interest = loan*rate/365*15"},
            {std::stod(res.homeowners_insurance_prepaid()), 2100.0,       "HOI prepaid = the annual premium"},
            {std::stod(res.total_cash_to_close()),  60335.9589,           "cash to close"},
        };
        for (const auto& l : lines_v) {
            if (std::abs(l.got - l.want) > 0.01) {
                std::cerr << "closing-cost " << l.what << ": got " << l.got
                          << ", independently computed " << l.want << "\n";
                return false;
            }
        }
        if (res.prepaid_interest_days() != 15) {
            std::cerr << "prepaid_interest_days 0 did not resolve to the 15-day convention\n";
            return false;
        }
        if (std::abs(std::stod(res.total_cash_to_close()) -
                     (std::stod(res.down_payment()) + std::stod(res.total_closing_costs()))) > 0.005) {
            std::cerr << "cash to close is not down payment plus total closing costs\n";
            return false;
        }

        // The credit identity. Re-issued with one field changed, so the
        // comparison isolates exactly that field.
        req.set_seller_lender_credits("5000");
        sensen::finance::ClosingCostsResponse credited;
        const auto ctx2 = make_context();
        if (const auto st = stub.ComputeClosingCosts(ctx2.get(), req, &credited); !st.ok()) {
            std::cerr << "ComputeClosingCosts (credited) FAILED: " << st.error_message() << "\n";
            return false;
        }
        if (std::abs(std::stod(credited.itemised_subtotal()) - subtotal) > 0.005) {
            std::cerr << "a seller credit moved the itemised subtotal, which it must not\n";
            return false;
        }
        if (std::abs(std::stod(credited.total_closing_costs()) - (subtotal - 5000.0)) > 0.005) {
            std::cerr << "a seller credit did not reduce the total by its own amount\n";
            return false;
        }
        // Discount points were ZERO above, which is what the site's default
        // scenario uses -- and a zero line cannot distinguish "share of the
        // loan" from "share of the price", because both are zero. Priced
        // again with real points so that base is actually exercised.
        req.set_seller_lender_credits("0");
        req.set_discount_points_percent("0.01");   // one point
        sensen::finance::ClosingCostsResponse pointed;
        const auto ctx3 = make_context();
        if (const auto st = stub.ComputeClosingCosts(ctx3.get(), req, &pointed); !st.ok()) {
            std::cerr << "ComputeClosingCosts (points) FAILED: " << st.error_message() << "\n";
            return false;
        }
        if (std::abs(std::stod(pointed.discount_points()) - loan * 0.01) > 0.005) {
            std::cerr << "one discount point is not 1% of the LOAN (got "
                      << pointed.discount_points() << ", loan " << loan << ")\n";
            return false;
        }
        if (std::abs(std::stod(pointed.itemised_subtotal()) - (subtotal + loan * 0.01)) > 0.005) {
            std::cerr << "adding a discount point did not raise the subtotal by its own cost\n";
            return false;
        }
        req.set_discount_points_percent("0");

        // A NEGATIVE credit would be a surcharge wearing a credit's name: it
        // reduces nothing and silently inflates the total. Refused.
        req.set_seller_lender_credits("-5000");
        sensen::finance::ClosingCostsResponse negative;
        const auto ctx4 = make_context();
        if (stub.ComputeClosingCosts(ctx4.get(), req, &negative).ok()) {
            std::cerr << "a NEGATIVE seller credit was accepted; it inflates the total\n";
            return false;
        }
        req.set_seller_lender_credits("0");

        // Eleven of the sixteen fields are genuinely optional -- a closing may
        // have no inspection, no points, no transfer tax. An omitted field must
        // read as zero rather than refusing the request, so the minimal
        // three-field call has to succeed.
        sensen::finance::ClosingCostsRequest minimal;
        minimal.set_home_price("450000");
        minimal.set_down_payment_percent("0.10");
        minimal.set_annual_rate("0.0675");
        sensen::finance::ClosingCostsResponse min_res;
        const auto ctx5 = make_context();
        if (const auto st = stub.ComputeClosingCosts(ctx5.get(), minimal, &min_res); !st.ok()) {
            std::cerr << "a minimal closing-cost request was refused: " << st.error_message() << "\n";
            return false;
        }
        if (std::abs(std::stod(min_res.itemised_subtotal()) -
                     std::stod(min_res.prepaid_interest())) > 0.005) {
            std::cerr << "with every optional cost omitted, only prepaid interest should remain\n";
            return false;
        }

        // An ALL-CASH purchase is a real transaction: no loan, so no lender
        // lines and no prepaid interest, but title, appraisal, recording,
        // transfer tax and escrow are all still owed.
        sensen::finance::ClosingCostsRequest cash = req;
        cash.set_down_payment_percent("1.0");
        cash.set_seller_lender_credits("0");
        sensen::finance::ClosingCostsResponse cash_res;
        const auto ctx6 = make_context();
        if (const auto st = stub.ComputeClosingCosts(ctx6.get(), cash, &cash_res); !st.ok()) {
            std::cerr << "an all-cash purchase was refused: " << st.error_message() << "\n";
            return false;
        }
        if (std::stod(cash_res.loan_amount()) != 0.0 ||
            std::stod(cash_res.origination_fee()) != 0.0 ||
            std::stod(cash_res.prepaid_interest()) != 0.0) {
            std::cerr << "an all-cash purchase still produced loan-derived charges\n";
            return false;
        }
        if (std::abs(std::stod(cash_res.title_settlement()) - 450000.0 * 0.0055) > 0.005) {
            std::cerr << "an all-cash purchase dropped the title fee, which is owed on the PRICE\n";
            return false;
        }

        // A credit larger than the bill would return a negative total.
        sensen::finance::ClosingCostsRequest over = req;
        over.set_seller_lender_credits("100000");
        sensen::finance::ClosingCostsResponse over_res;
        const auto ctx7 = make_context();
        if (stub.ComputeClosingCosts(ctx7.get(), over, &over_res).ok()) {
            std::cerr << "a credit exceeding the closing costs was accepted\n";
            return false;
        }

        // An explicit ZERO day count must mean zero, not the convention.
        sensen::finance::ClosingCostsRequest zero_days = req;
        zero_days.set_seller_lender_credits("0");
        zero_days.set_prepaid_interest_days(0);
        sensen::finance::ClosingCostsResponse zd;
        const auto ctx8 = make_context();
        if (const auto st = stub.ComputeClosingCosts(ctx8.get(), zero_days, &zd); !st.ok()) {
            std::cerr << "an explicit 0-day prepaid interest was refused: " << st.error_message() << "\n";
            return false;
        }
        if (std::stod(zd.prepaid_interest()) != 0.0 || zd.prepaid_interest_days() != 0) {
            std::cerr << "an explicit 0 prepaid-interest days was silently treated as 15\n";
            return false;
        }

        std::cout << "Finance   closing costs: " << std::fixed << std::setprecision(2)
                  << subtotal << " itemised, " << std::stod(res.total_cash_to_close())
                  << " cash to close; credit of 5000 -> "
                  << std::stod(credited.total_closing_costs()) << "\n";
    }

    // -- Time value of money, against the closed-form annuity formula --------
    {
        sensen::finance::PaymentRequest req;
        req.set_rate("0.005");           // 6% nominal, monthly
        req.set_periods(360);            // 30 years
        req.set_present_value("300000");
        sensen::finance::DecimalResponse res;
        const auto ctx = make_context();
        if (const auto s = stub.ComputePayment(ctx.get(), req, &res); !s.ok()) {
            std::cerr << "ComputePayment FAILED: " << s.error_message() << "\n";
            return false;
        }
        const double pmt = std::stod(res.value());
        // PMT = -PV*r / (1 - (1+r)^-n). Written out here so it shares no code
        // with the engine.
        const double r = 0.005;
        const double expected = -300000.0 * r / (1.0 - std::pow(1.0 + r, -360.0));
        if (std::abs(pmt - expected) > 1e-6) {
            std::cerr << "pmt " << pmt << " disagrees with the closed form " << expected << "\n";
            return false;
        }
        std::cout << "Finance   pmt(300k, 6%, 30y) = " << std::fixed << std::setprecision(6)
                  << pmt << "  (closed form " << expected << ")\n";
    }

    // Interest + principal must reconstruct the payment exactly, and period-1
    // interest must be balance x rate. Both hold for any correct scheduler.
    {
        sensen::finance::PeriodPaymentRequest req;
        req.set_rate("0.005");
        req.set_period(1);
        req.set_periods(360);
        req.set_present_value("300000");
        sensen::finance::DecimalResponse ires;
        sensen::finance::DecimalResponse pres;
        const auto c1 = make_context();
        const auto c2 = make_context();
        if (!stub.ComputeInterestPayment(c1.get(), req, &ires).ok() ||
            !stub.ComputePrincipalPayment(c2.get(), req, &pres).ok()) {
            std::cerr << "ipmt/ppmt FAILED\n";
            return false;
        }
        const double ip = std::stod(ires.value());
        const double pp = std::stod(pres.value());
        const double r = 0.005;
        const double expected_pmt = -300000.0 * r / (1.0 - std::pow(1.0 + r, -360.0));
        if (std::abs((ip + pp) - expected_pmt) > 1e-6) {
            std::cerr << "ipmt+ppmt " << (ip + pp) << " != pmt " << expected_pmt << "\n";
            return false;
        }
        if (std::abs(std::abs(ip) - 300000.0 * r) > 1e-6) {
            std::cerr << "period-1 interest " << std::abs(ip) << " != PV*r " << (300000.0 * r)
                      << "\n";
            return false;
        }
        std::cout << "          ipmt + ppmt reconstructs pmt; period-1 interest = PV x r\n";
    }

    // -- Amortization: the schedule must close on itself --------------------
    {
        sensen::finance::AmortizationRequest req;
        req.set_loan_amount("300000");
        req.set_annual_rate("0.06");
        req.set_term_months(360);
        sensen::finance::AmortizationResponse res;
        const auto ctx = make_context();
        if (const auto s = stub.ComputeAmortization(ctx.get(), req, &res); !s.ok()) {
            std::cerr << "ComputeAmortization FAILED: " << s.error_message() << "\n";
            return false;
        }
        if (res.schedule_size() != 360) {
            std::cerr << "schedule has " << res.schedule_size() << " rows, expected 360\n";
            return false;
        }
        // start - principal == end on every row. A schedule that fails this is
        // not a schedule.
        double worst = 0.0;
        for (const auto& row : res.schedule()) {
            const double gap = std::abs(std::stod(row.start_balance()) -
                                        std::stod(row.principal_paid()) -
                                        std::stod(row.end_balance()));
            worst = std::max(worst, gap);
        }
        if (worst > 1e-6) {
            std::cerr << "amortization rows do not close: worst gap " << worst << "\n";
            return false;
        }
        const double final_balance = std::stod(res.schedule(359).end_balance());
        if (std::abs(final_balance) > 0.01) {
            std::cerr << "loan does not retire: final balance " << final_balance << "\n";
            return false;
        }
        // Total principal repaid is definitionally the amount borrowed.
        const double total_principal = std::stod(res.summary().total_principal_paid());
        if (std::abs(total_principal - 300000.0) > 0.01) {
            std::cerr << "total principal " << total_principal << " != loan 300000\n";
            return false;
        }
        std::cout << "          360 rows close to 0.00, total principal = the loan, interest = "
                  << std::setprecision(2) << std::stod(res.summary().total_interest_paid()) << "\n";
    }

    // An overpayment must retire the loan EARLY. This is the field a borrower
    // actually asks about, so it gets its own check rather than riding on the
    // schedule length.
    {
        sensen::finance::AmortizationRequest req;
        req.set_loan_amount("300000");
        req.set_annual_rate("0.06");
        req.set_term_months(360);
        req.set_monthly_overpayment("500");
        sensen::finance::AmortizationResponse res;
        const auto ctx = make_context();
        if (!stub.ComputeAmortization(ctx.get(), req, &res).ok()) {
            std::cerr << "overpayment amortization FAILED\n";
            return false;
        }
        if (res.summary().actual_term_months() >= 360) {
            std::cerr << "a 500/month overpayment did not shorten the term ("
                      << res.summary().actual_term_months() << ")\n";
            return false;
        }
        std::cout << "          +500/mo retires it in " << res.summary().actual_term_months()
                  << " months instead of 360\n";
    }

    // -- Black-Scholes, against put-call parity -----------------------------
    //
    // Parity is an arbitrage identity, not a pricing formula: it holds for any
    // correct call/put pair regardless of how they were computed, which is what
    // makes it a real check rather than a restatement of the model.
    {
        sensen::finance::BlackScholesRequest req;
        req.set_spot(100.0);
        req.set_strike(100.0);
        req.set_rate(0.05);
        req.set_volatility(0.2);
        req.set_years_to_expiry(1.0);
        sensen::finance::BlackScholesResponse call_res;
        sensen::finance::BlackScholesResponse put_res;
        const auto c1 = make_context();
        if (!stub.PriceBlackScholes(c1.get(), req, &call_res).ok()) {
            std::cerr << "PriceBlackScholes(call) FAILED\n";
            return false;
        }
        req.set_option_type(sensen::finance::PUT);
        const auto c2 = make_context();
        if (!stub.PriceBlackScholes(c2.get(), req, &put_res).ok()) {
            std::cerr << "PriceBlackScholes(put) FAILED\n";
            return false;
        }
        const double parity_lhs = call_res.value() - put_res.value();
        const double parity_rhs = 100.0 - 100.0 * std::exp(-0.05 * 1.0);
        if (std::abs(parity_lhs - parity_rhs) > 1e-9) {
            std::cerr << "put-call parity violated: C-P = " << parity_lhs << " vs S-Ke^-rT = "
                      << parity_rhs << "\n";
            return false;
        }
        if (std::abs((call_res.delta() - put_res.delta()) - 1.0) > 1e-9) {
            std::cerr << "delta_call - delta_put = " << (call_res.delta() - put_res.delta())
                      << ", must be exactly 1\n";
            return false;
        }
        // Gamma and vega do not depend on which side of the strike you are on.
        if (std::abs(call_res.gamma() - put_res.gamma()) > 1e-12 ||
            std::abs(call_res.vega() - put_res.vega()) > 1e-12) {
            std::cerr << "gamma/vega differ between call and put\n";
            return false;
        }
        std::cout << "          BS call " << std::setprecision(6) << call_res.value() << " put "
                  << put_res.value() << "  parity holds, delta_c - delta_p = 1\n";
    }

    // -- Bonds: price and yield must invert each other ----------------------
    {
        sensen::finance::BondRequest req;
        req.set_par(1000.0);
        req.set_coupon_rate(0.05);
        req.set_frequency(2);
        req.set_years_to_maturity(10.0);
        req.set_redemption(100.0);
        req.set_yield(0.04);
        sensen::finance::BondResponse res;
        const auto c1 = make_context();
        if (const auto s = stub.AnalyzeBond(c1.get(), req, &res); !s.ok()) {
            std::cerr << "AnalyzeBond FAILED: " << s.error_message() << "\n";
            return false;
        }
        // A coupon above the yield means the bond trades above par. If this is
        // ever false the sign convention has inverted somewhere.
        if (res.price() <= 1000.0) {
            std::cerr << "a 5% coupon at a 4% yield priced at " << res.price()
                      << ", which is not a premium\n";
            return false;
        }
        if (res.macaulay_duration() <= 0.0 || res.macaulay_duration() >= 10.0) {
            std::cerr << "duration " << res.macaulay_duration()
                      << " is not inside (0, maturity)\n";
            return false;
        }
        if (res.convexity() <= 0.0) {
            std::cerr << "convexity " << res.convexity() << " is not positive\n";
            return false;
        }
        // Feed the price back: the yield must come out where it went in.
        sensen::finance::BondRequest back;
        back.set_par(1000.0);
        back.set_coupon_rate(0.05);
        back.set_frequency(2);
        back.set_years_to_maturity(10.0);
        back.set_redemption(100.0);
        back.set_price(res.price());
        sensen::finance::BondResponse back_res;
        const auto c2 = make_context();
        if (!stub.AnalyzeBond(c2.get(), back, &back_res).ok()) {
            std::cerr << "AnalyzeBond(price) FAILED\n";
            return false;
        }
        if (std::abs(back_res.yield() - 0.04) > 1e-8) {
            std::cerr << "price -> yield did not invert: got " << back_res.yield() << "\n";
            return false;
        }
        std::cout << "          bond 5%/10y at 4% yield = " << std::setprecision(6) << res.price()
                  << ", inverts back to " << back_res.yield()
                  << ", duration " << res.macaulay_duration() << "\n";
    }

    // -- HELOC and rental ROI, the features added upstream today ------------
    {
        sensen::finance::HelocRequest req;
        req.set_home_value("500000");
        req.set_current_mortgage_balance("300000");
        req.set_max_ltv_rate("0.80");
        req.set_drawn_amount("50000");
        req.set_annual_rate("0.07");
        req.set_repayment_term_years(15);
        req.set_payments_per_year(12);
        sensen::finance::HelocResponse res;
        const auto ctx = make_context();
        if (const auto s = stub.ComputeHeloc(ctx.get(), req, &res); !s.ok()) {
            std::cerr << "ComputeHeloc FAILED: " << s.error_message() << "\n";
            return false;
        }
        // 500k x 0.80 = 400k ceiling, less 300k owed = 100k, less 50k drawn.
        if (std::abs(std::stod(res.available_equity()) - 50000.0) > 0.01) {
            std::cerr << "HELOC available equity " << res.available_equity()
                      << ", expected 50000\n";
            return false;
        }
        // The draw period is interest only: 50000 x 0.07/12.
        const double expected_draw = 50000.0 * 0.07 / 12.0;
        if (std::abs(std::stod(res.draw_period_payment()) - expected_draw) > 0.01) {
            std::cerr << "HELOC draw payment " << res.draw_period_payment() << ", expected "
                      << expected_draw << "\n";
            return false;
        }
        if (std::stod(res.repayment_period_payment()) <=
            std::stod(res.draw_period_payment())) {
            std::cerr << "amortizing payment is not above the interest-only payment\n";
            return false;
        }
        std::cout << "          HELOC equity " << std::setprecision(2)
                  << std::stod(res.available_equity()) << ", draw "
                  << std::stod(res.draw_period_payment()) << "/mo, repayment "
                  << std::stod(res.repayment_period_payment()) << "/mo\n";
    }

    {
        sensen::finance::RentalRoiRequest req;
        req.set_property_value("400000");
        req.set_total_cash_invested("100000");
        req.set_periodic_gross_rent("3000");
        req.set_periodic_operating_expenses("800");
        req.set_periodic_mortgage_payment("1500");
        req.set_periods_per_year(12);
        sensen::finance::RentalRoiResponse res;
        const auto ctx = make_context();
        if (const auto s = stub.ComputeRentalRoi(ctx.get(), req, &res); !s.ok()) {
            std::cerr << "ComputeRentalRoi FAILED: " << s.error_message() << "\n";
            return false;
        }
        // NOI excludes debt service: (3000 - 800) x 12.
        if (std::abs(std::stod(res.net_operating_income()) - 26400.0) > 0.01) {
            std::cerr << "NOI " << res.net_operating_income() << ", expected 26400 -- debt "
                      << "service may be leaking into operating expenses\n";
            return false;
        }
        // Cash flow subtracts it: 26400 - 18000.
        if (std::abs(std::stod(res.annual_cash_flow()) - 8400.0) > 0.01) {
            std::cerr << "cash flow " << res.annual_cash_flow() << ", expected 8400\n";
            return false;
        }
        // Cap rate is NOI over value, NOT cash flow over value.
        if (std::abs(std::stod(res.cap_rate()) - 0.066) > 1e-6) {
            std::cerr << "cap rate " << res.cap_rate() << ", expected 0.066 = 26400/400000\n";
            return false;
        }
        std::cout << "          rental NOI 26400, cash flow 8400, cap rate 6.60%\n";
    }

    // -- ComputeRefinance -----------------------------------------------------
    {
        // No-op refinance: refinance a loan into itself (same rate, term
        // equal to the months remaining, zero closing costs/cash-out/PMI).
        // Every "savings" figure must collapse to zero/no-shift. The payment
        // is the closed-form annuity payment computed here, sharing no code
        // with the engine.
        const double pv = 300000.0;
        const double annual_rate = 0.06;
        const double r = annual_rate / 12.0;
        const int n = 300;
        const double payment = pv * r / (1.0 - std::pow(1.0 + r, -static_cast<double>(n)));

        sensen::finance::RefinanceRequest req;
        req.set_current_loan_balance("300000");
        req.set_current_monthly_payment(dec(payment));
        req.set_current_annual_rate("0.06");
        req.set_current_remaining_months(n);
        req.set_property_value("300000");
        req.set_new_annual_rate("0.06");
        req.set_new_term_years(n / 12);
        req.set_closing_costs("0");
        req.set_closing_cost_type(sensen::finance::RefinanceRequest::PAID_IN_CASH);
        req.set_cash_out_amount("0");
        req.set_current_pmi_monthly("0");
        req.set_new_pmi_monthly("0");
        req.set_pmi_drop_off_ltv("0.80");
        req.set_payments_per_year(12);
        sensen::finance::RefinanceResponse res;
        const auto ctx = make_context();
        if (const auto s = stub.ComputeRefinance(ctx.get(), req, &res); !s.ok()) {
            std::cerr << "ComputeRefinance (no-op) FAILED: " << s.error_message() << "\n";
            return false;
        }
        if (std::abs(std::stod(res.new_loan_amount()) - pv) > 1e-6) {
            std::cerr << "no-op refinance new_loan_amount " << res.new_loan_amount() << " != PV "
                      << pv << "\n";
            return false;
        }
        if (std::abs(std::stod(res.new_monthly_payment()) - payment) > 1e-6) {
            std::cerr << "no-op refinance new_monthly_payment " << res.new_monthly_payment()
                      << " disagrees with the closed form " << payment << "\n";
            return false;
        }
        if (std::abs(std::stod(res.monthly_savings_initial())) > 1e-6) {
            std::cerr << "no-op refinance monthly_savings_initial "
                      << res.monthly_savings_initial() << " != 0\n";
            return false;
        }
        if (res.payoff_date_shift_months() != 0) {
            std::cerr << "no-op refinance payoff_date_shift_months "
                      << res.payoff_date_shift_months() << " != 0\n";
            return false;
        }
        if (std::abs(res.total_savings_over_life()) > 1e-4) {
            std::cerr << "no-op refinance total_savings_over_life "
                      << res.total_savings_over_life() << " != 0\n";
            return false;
        }
        std::cout << "          refinance(no-op) pmt = " << std::setprecision(6) << payment
                  << " (closed form), loan = PV, shift = 0, lifetime savings ~= 0\n";
    }
    {
        // Simple break-even against its own definition: 6% -> 4.5%, 300k,
        // 300 months remaining, new 30-year term, 6000 paid-in-cash closing
        // costs. Both payments are the closed-form annuity, computed here.
        const double pv = 300000.0;
        const double old_r = 0.06 / 12.0;
        const double new_r = 0.045 / 12.0;
        const int old_n = 300;
        const int new_n = 360;
        const double old_pmt = pv * old_r / (1.0 - std::pow(1.0 + old_r, -static_cast<double>(old_n)));
        const double new_pmt = pv * new_r / (1.0 - std::pow(1.0 + new_r, -static_cast<double>(new_n)));
        const double savings = old_pmt - new_pmt;
        const int expected_be = static_cast<int>(std::ceil(6000.0 / savings));

        sensen::finance::RefinanceRequest req;
        req.set_current_loan_balance("300000");
        req.set_current_monthly_payment(dec(old_pmt));
        req.set_current_annual_rate("0.06");
        req.set_current_remaining_months(old_n);
        req.set_property_value("300000");
        req.set_new_annual_rate("0.045");
        req.set_new_term_years(30);
        req.set_closing_costs("6000");
        req.set_closing_cost_type(sensen::finance::RefinanceRequest::PAID_IN_CASH);
        req.set_payments_per_year(12);
        sensen::finance::RefinanceResponse res;
        const auto ctx = make_context();
        if (const auto s = stub.ComputeRefinance(ctx.get(), req, &res); !s.ok()) {
            std::cerr << "ComputeRefinance (break-even) FAILED: " << s.error_message() << "\n";
            return false;
        }
        if (res.simple_break_even_months() != expected_be) {
            std::cerr << "simple_break_even_months " << res.simple_break_even_months()
                      << " != ceil(6000/savings) = " << expected_be << "\n";
            return false;
        }
        std::cout << "          refinance 6%->4.5% break-even = " << res.simple_break_even_months()
                  << " months (savings " << std::setprecision(2) << savings << "/mo)\n";

        // Closing-cost funding identity: the same request, PAID_IN_CASH vs
        // ROLLED_INTO_LOAN. new_loan_amount must differ by exactly
        // closing_costs, and the rolled variant's payment must be strictly
        // higher.
        sensen::finance::RefinanceRequest rolled = req;
        rolled.set_closing_cost_type(sensen::finance::RefinanceRequest::ROLLED_INTO_LOAN);
        sensen::finance::RefinanceResponse rolled_res;
        const auto ctx2 = make_context();
        if (const auto s = stub.ComputeRefinance(ctx2.get(), rolled, &rolled_res); !s.ok()) {
            std::cerr << "ComputeRefinance (rolled-in) FAILED: " << s.error_message() << "\n";
            return false;
        }
        const double cash_loan = std::stod(res.new_loan_amount());
        const double rolled_loan = std::stod(rolled_res.new_loan_amount());
        if (std::abs((rolled_loan - cash_loan) - 6000.0) > 1e-6) {
            std::cerr << "rolled - cash new_loan_amount = " << (rolled_loan - cash_loan)
                      << " != closing_costs 6000\n";
            return false;
        }
        if (std::stod(rolled_res.new_monthly_payment()) <= std::stod(res.new_monthly_payment())) {
            std::cerr << "rolling closing costs into the loan did not raise the payment\n";
            return false;
        }
        std::cout << "          refinance funding: rolled loan - cash loan = 6000 exactly, "
                     "rolled payment > cash payment\n";

        // Never-break-even sentinel: refinance to a rate high enough that
        // the new payment EXCEEDS the old one, so savings are negative.
        sensen::finance::RefinanceRequest worse = req;
        worse.set_new_annual_rate("0.08");
        sensen::finance::RefinanceResponse worse_res;
        const auto ctx3 = make_context();
        if (const auto s = stub.ComputeRefinance(ctx3.get(), worse, &worse_res); !s.ok()) {
            std::cerr << "ComputeRefinance (worse rate) FAILED: " << s.error_message() << "\n";
            return false;
        }
        if (worse_res.simple_break_even_months() != -1) {
            std::cerr << "refinancing to a higher rate produced break-even "
                      << worse_res.simple_break_even_months() << ", expected -1 (never)\n";
            return false;
        }
        std::cout << "          refinance to a higher rate never breaks even (-1)\n";
    }
    {
        // PMI drop-off zero case: the starting balance is already below the
        // LTV threshold on both loans, so both drop-off fields are 0 from
        // the lambda's early return (financial.cppm:1907) -- independent of
        // whether the PMI amount itself is zero.
        sensen::finance::RefinanceRequest req;
        req.set_current_loan_balance("300000");
        req.set_current_monthly_payment("1932.904204456543");
        req.set_current_annual_rate("0.06");
        req.set_current_remaining_months(300);
        req.set_property_value("1000000");   // threshold = 800000 at 80% LTV
        req.set_new_annual_rate("0.06");
        req.set_new_term_years(30);
        req.set_closing_costs("0");
        req.set_current_pmi_monthly("100");
        req.set_new_pmi_monthly("100");
        req.set_pmi_drop_off_ltv("0.80");
        req.set_payments_per_year(12);
        sensen::finance::RefinanceResponse res;
        const auto ctx = make_context();
        if (const auto s = stub.ComputeRefinance(ctx.get(), req, &res); !s.ok()) {
            std::cerr << "ComputeRefinance (PMI zero) FAILED: " << s.error_message() << "\n";
            return false;
        }
        if (res.current_loan_pmi_drop_off_months() != 0 || res.new_loan_pmi_drop_off_months() != 0) {
            std::cerr << "PMI drop-off months " << res.current_loan_pmi_drop_off_months() << "/"
                      << res.new_loan_pmi_drop_off_months()
                      << " != 0/0 for a balance already below the LTV threshold\n";
            return false;
        }
        std::cout << "          refinance PMI drop-off = 0/0 when the balance already sits "
                     "below the LTV threshold\n";
    }

    // -- ComputePayoffTiming: the annuity inversion ----------------------------
    {
        const double pv = 200000.0;
        const double r = 0.05 / 12.0;
        const int n = 180;
        const double pmt = pv * r / (1.0 - std::pow(1.0 + r, -static_cast<double>(n)));
        const double extra = 300.0;
        const auto nper_of = [&](double payment) {
            return -std::log(1.0 - pv * r / payment) / std::log(1.0 + r);
        };
        const int expected_orig = static_cast<int>(std::ceil(nper_of(pmt)));
        const int expected_new = static_cast<int>(std::ceil(nper_of(pmt + extra)));
        const double expected_interest_saved =
            (pmt * expected_orig - pv) - ((pmt + extra) * expected_new - pv);

        sensen::finance::PayoffTimingRequest req;
        req.set_current_loan_balance("200000");
        req.set_annual_rate("0.05");
        req.set_current_monthly_payment(dec(pmt));
        req.set_extra_monthly_payment(dec(extra));
        req.set_payments_per_year(12);
        sensen::finance::PayoffTimingResponse res;
        const auto ctx = make_context();
        if (const auto s = stub.ComputePayoffTiming(ctx.get(), req, &res); !s.ok()) {
            std::cerr << "ComputePayoffTiming FAILED: " << s.error_message() << "\n";
            return false;
        }
        if (res.original_months_remaining() != expected_orig) {
            std::cerr << "original_months_remaining " << res.original_months_remaining()
                      << " != closed-form nper " << expected_orig << "\n";
            return false;
        }
        if (res.new_months_remaining() != expected_new) {
            std::cerr << "new_months_remaining " << res.new_months_remaining()
                      << " != closed-form nper " << expected_new << "\n";
            return false;
        }
        if (res.months_saved() != expected_orig - expected_new) {
            std::cerr << "months_saved " << res.months_saved() << " != "
                      << (expected_orig - expected_new) << "\n";
            return false;
        }
        if (std::abs(std::stod(res.total_interest_saved()) - expected_interest_saved) > 1e-6) {
            std::cerr << "total_interest_saved " << res.total_interest_saved() << " != "
                      << expected_interest_saved << "\n";
            return false;
        }
        std::cout << "          payoff timing " << expected_orig << " -> " << expected_new
                  << " months (+" << extra << "/mo), interest saved " << std::setprecision(2)
                  << expected_interest_saved << "\n";
    }

    // -- ComputeMortgageRecast: pmt linearity in principal ---------------------
    {
        const double balance = 300000.0;
        const double lump = 80000.0;
        const double rate = 0.055;
        const int months = 240;
        const double r = rate / 12.0;
        const double expected_a = balance * r / (1.0 - std::pow(1.0 + r, -static_cast<double>(months)));

        sensen::finance::MortgageRecastRequest req_a;
        req_a.set_current_loan_balance(dec(balance));
        req_a.set_current_monthly_payment("2000");
        req_a.set_lump_sum_payment("0");
        req_a.set_annual_rate(dec(rate));
        req_a.set_remaining_months(months);
        req_a.set_payments_per_year(12);
        sensen::finance::MortgageRecastResponse res_a;
        const auto ctx_a = make_context();
        if (const auto s = stub.ComputeMortgageRecast(ctx_a.get(), req_a, &res_a); !s.ok()) {
            std::cerr << "ComputeMortgageRecast(A) FAILED: " << s.error_message() << "\n";
            return false;
        }
        const double payment_a = std::stod(res_a.new_monthly_payment());
        if (std::abs(payment_a - expected_a) > 1e-4) {
            std::cerr << "recast(no lump) payment " << payment_a << " != closed form "
                      << expected_a << "\n";
            return false;
        }
        if (std::abs(std::stod(res_a.monthly_savings()) - (2000.0 - payment_a)) > 1e-4) {
            std::cerr << "recast monthly_savings " << res_a.monthly_savings()
                      << " != current_payment - new_payment\n";
            return false;
        }

        sensen::finance::MortgageRecastRequest req_b = req_a;
        req_b.set_lump_sum_payment(dec(lump));
        sensen::finance::MortgageRecastResponse res_b;
        const auto ctx_b = make_context();
        if (const auto s = stub.ComputeMortgageRecast(ctx_b.get(), req_b, &res_b); !s.ok()) {
            std::cerr << "ComputeMortgageRecast(B) FAILED: " << s.error_message() << "\n";
            return false;
        }
        const double payment_b = std::stod(res_b.new_monthly_payment());

        sensen::finance::MortgageRecastRequest req_c;
        req_c.set_current_loan_balance(dec(lump));
        req_c.set_current_monthly_payment("2000");
        req_c.set_lump_sum_payment("0");
        req_c.set_annual_rate(dec(rate));
        req_c.set_remaining_months(months);
        req_c.set_payments_per_year(12);
        sensen::finance::MortgageRecastResponse res_c;
        const auto ctx_c = make_context();
        if (const auto s = stub.ComputeMortgageRecast(ctx_c.get(), req_c, &res_c); !s.ok()) {
            std::cerr << "ComputeMortgageRecast(C) FAILED: " << s.error_message() << "\n";
            return false;
        }
        const double payment_c = std::stod(res_c.new_monthly_payment());

        // pmt is linear in principal at a fixed rate/term: payment(balance)
        // - payment(balance-lump) == payment(lump). An identity no cached
        // constant can fake -- it takes three independent recasts to agree.
        if (std::abs((payment_a - payment_b) - payment_c) > 1e-4) {
            std::cerr << "pmt linearity violated: payment(B)-payment(B-L) = "
                      << (payment_a - payment_b) << " != payment(L) = " << payment_c << "\n";
            return false;
        }
        std::cout << "          recast pmt linearity: payment(B)-payment(B-L) = payment(L) = "
                  << std::setprecision(6) << payment_c << "\n";

        // Full-payoff edge: lump == balance -> new payment is exactly 0.
        sensen::finance::MortgageRecastRequest req_d = req_a;
        req_d.set_lump_sum_payment(dec(balance));
        sensen::finance::MortgageRecastResponse res_d;
        const auto ctx_d = make_context();
        if (const auto s = stub.ComputeMortgageRecast(ctx_d.get(), req_d, &res_d); !s.ok()) {
            std::cerr << "ComputeMortgageRecast(full payoff) FAILED: " << s.error_message() << "\n";
            return false;
        }
        if (std::abs(std::stod(res_d.new_monthly_payment())) > 1e-9) {
            std::cerr << "a lump sum equal to the balance left a payment of "
                      << res_d.new_monthly_payment() << ", expected exactly 0\n";
            return false;
        }
        std::cout << "          recast full payoff: lump == balance -> payment = 0\n";
    }

    // -- ComputeHomeFutureValue: both legs have closed forms -------------------
    {
        const double prop_value = 500000.0;
        const double appreciation = 0.03;
        const double loan_balance = 250000.0;
        const double mortgage_rate = 0.045;
        const int years = 10;
        const double r = mortgage_rate / 12.0;
        const int term_months = 360;
        const double pmt =
            loan_balance * r / (1.0 - std::pow(1.0 + r, -static_cast<double>(term_months)));
        const int m = years * 12;
        const double expected_future_prop = prop_value * std::pow(1.0 + appreciation, years);
        const double expected_balance =
            loan_balance * std::pow(1.0 + r, m) - pmt * (std::pow(1.0 + r, m) - 1.0) / r;

        sensen::finance::HomeFutureValueRequest req;
        req.set_current_property_value(dec(prop_value));
        req.set_annual_appreciation_rate("0.03");
        req.set_current_loan_balance(dec(loan_balance));
        req.set_annual_mortgage_rate("0.045");
        req.set_current_monthly_payment(dec(pmt));
        req.set_target_years(years);
        req.set_payments_per_year(12);
        sensen::finance::HomeFutureValueResponse res;
        const auto ctx = make_context();
        if (const auto s = stub.ComputeHomeFutureValue(ctx.get(), req, &res); !s.ok()) {
            std::cerr << "ComputeHomeFutureValue FAILED: " << s.error_message() << "\n";
            return false;
        }
        if (std::abs(res.future_property_value() - expected_future_prop) > 1e-4) {
            std::cerr << "future_property_value " << res.future_property_value() << " != "
                      << expected_future_prop << "\n";
            return false;
        }
        if (std::abs(std::stod(res.future_loan_balance()) - expected_balance) > 1e-2) {
            std::cerr << "future_loan_balance " << res.future_loan_balance()
                      << " disagrees with the independent closed-form remaining balance "
                      << expected_balance << "\n";
            return false;
        }
        const double cross_equity =
            res.future_property_value() - std::stod(res.future_loan_balance());
        if (std::abs(std::stod(res.future_equity()) - cross_equity) > 1e-6) {
            std::cerr << "future_equity " << res.future_equity()
                      << " != future_property_value - future_loan_balance = " << cross_equity
                      << "\n";
            return false;
        }
        std::cout << "          home FV: property " << std::setprecision(2)
                  << res.future_property_value() << ", balance "
                  << std::stod(res.future_loan_balance()) << " (closed form " << expected_balance
                  << ")\n";

        // Retirement identity: payment = the exact annuity payment over the
        // FULL target_years term -> the balance must clamp to exactly 0.
        const double pmt_full =
            loan_balance * r / (1.0 - std::pow(1.0 + r, -static_cast<double>(m)));
        sensen::finance::HomeFutureValueRequest retire = req;
        retire.set_current_monthly_payment(dec(pmt_full));
        sensen::finance::HomeFutureValueResponse retire_res;
        const auto ctx2 = make_context();
        if (const auto s = stub.ComputeHomeFutureValue(ctx2.get(), retire, &retire_res); !s.ok()) {
            std::cerr << "ComputeHomeFutureValue (retirement) FAILED: " << s.error_message()
                      << "\n";
            return false;
        }
        if (std::abs(std::stod(retire_res.future_loan_balance())) > 1e-4) {
            std::cerr << "a loan paid off exactly over its term left a balance of "
                      << retire_res.future_loan_balance() << "\n";
            return false;
        }
        std::cout << "          home FV retirement identity: exact annuity payment -> balance "
                     "= 0\n";
    }

    // -- ComputeRentVsBuy: no arbitrage identity, so an independent closed ----
    // -- form plus invariants and a degenerate control ------------------------
    {
        const double price = 400000.0, down = 80000.0, piti = 2200.0;
        const double appreciation = 0.03, rent0 = 2000.0, rent_growth = 0.02, inv_return = 0.05;
        const int years = 7;

        const double total_monthly_buy = piti * years * 12;
        const double fv_home = price * std::pow(1.0 + appreciation, years);
        const double equity = fv_home - (price - down);
        const double expected_buy = down + total_monthly_buy - equity;

        double rent_cost = 0.0;
        double cur_rent = rent0;
        for (int y = 0; y < years; ++y) {
            rent_cost += cur_rent * 12.0;
            cur_rent *= (1.0 + rent_growth);
        }
        const double dp_investment = down * std::pow(1.0 + inv_return, years);
        const double investment_gain = dp_investment - down;
        const double expected_rent = rent_cost - investment_gain;

        sensen::finance::RentVsBuyRequest req;
        req.set_property_price(dec(price));
        req.set_down_payment(dec(down));
        req.set_monthly_piti_and_maintenance(dec(piti));
        req.set_annual_home_appreciation(dec(appreciation));
        req.set_current_monthly_rent(dec(rent0));
        req.set_annual_rent_increase(dec(rent_growth));
        req.set_annual_investment_return(dec(inv_return));
        req.set_years(years);
        sensen::finance::RentVsBuyResponse res;
        const auto ctx = make_context();
        if (const auto s = stub.ComputeRentVsBuy(ctx.get(), req, &res); !s.ok()) {
            std::cerr << "ComputeRentVsBuy FAILED: " << s.error_message() << "\n";
            return false;
        }
        if (std::abs(res.total_cost_of_buying() - expected_buy) > 1e-4) {
            std::cerr << "total_cost_of_buying " << res.total_cost_of_buying()
                      << " != independent closed form " << expected_buy << "\n";
            return false;
        }
        if (std::abs(res.total_cost_of_renting() - expected_rent) > 1e-4) {
            std::cerr << "total_cost_of_renting " << res.total_cost_of_renting()
                      << " != independent closed form " << expected_rent << "\n";
            return false;
        }
        if (std::abs(res.buying_advantage() -
                     (res.total_cost_of_renting() - res.total_cost_of_buying())) > 1e-6) {
            std::cerr << "buying_advantage != total_cost_of_renting - total_cost_of_buying\n";
            return false;
        }
        if (res.is_buying_better() != (res.buying_advantage() > 0.0)) {
            std::cerr << "is_buying_better disagrees with the sign of buying_advantage\n";
            return false;
        }
        std::cout << "          rent-vs-buy: buy " << std::setprecision(2)
                  << res.total_cost_of_buying() << ", rent " << res.total_cost_of_renting()
                  << ", advantage " << res.buying_advantage() << " (closed form "
                  << (expected_rent - expected_buy) << ")\n";

        // Monotonicity: a higher starting rent must strictly favor buying.
        sensen::finance::RentVsBuyRequest higher_rent = req;
        higher_rent.set_current_monthly_rent("2500");
        sensen::finance::RentVsBuyResponse higher_res;
        const auto ctx2 = make_context();
        if (const auto s = stub.ComputeRentVsBuy(ctx2.get(), higher_rent, &higher_res); !s.ok()) {
            std::cerr << "ComputeRentVsBuy (higher rent) FAILED: " << s.error_message() << "\n";
            return false;
        }
        if (higher_res.buying_advantage() <= res.buying_advantage()) {
            std::cerr << "raising current_monthly_rent did not strictly increase "
                         "buying_advantage\n";
            return false;
        }
        std::cout << "          rent-vs-buy monotonicity: higher rent -> higher buying_advantage\n";

        // Degenerate control: zero appreciation, zero rent growth, zero
        // investment return collapse both sides to hand arithmetic.
        sensen::finance::RentVsBuyRequest degen;
        degen.set_property_price(dec(price));
        degen.set_down_payment(dec(down));
        degen.set_monthly_piti_and_maintenance(dec(piti));
        degen.set_annual_home_appreciation("0");
        degen.set_current_monthly_rent(dec(rent0));
        degen.set_annual_rent_increase("0");
        degen.set_annual_investment_return("0");
        degen.set_years(years);
        sensen::finance::RentVsBuyResponse degen_res;
        const auto ctx3 = make_context();
        if (const auto s = stub.ComputeRentVsBuy(ctx3.get(), degen, &degen_res); !s.ok()) {
            std::cerr << "ComputeRentVsBuy (degenerate) FAILED: " << s.error_message() << "\n";
            return false;
        }
        if (std::abs(degen_res.total_cost_of_buying() - total_monthly_buy) > 1e-6) {
            std::cerr << "degenerate buy cost " << degen_res.total_cost_of_buying()
                      << " != 12*years*M = " << total_monthly_buy << "\n";
            return false;
        }
        if (std::abs(degen_res.total_cost_of_renting() - (rent0 * 12.0 * years)) > 1e-6) {
            std::cerr << "degenerate rent cost " << degen_res.total_cost_of_renting()
                      << " != 12*years*R = " << (rent0 * 12.0 * years) << "\n";
            return false;
        }
        std::cout << "          rent-vs-buy degenerate control: buy = 12yM, rent = 12yR exactly\n";
    }

    // -- ComputeHomeNpv: NPV(IRR) == 0 as the identity -------------------------
    {
        const double property_price = 350000.0, down_payment = 70000.0, closing_buy = 5000.0;
        const double loan_amount = 280000.0, loan_rate = 0.05;
        const int loan_term_years = 30;
        const double taxes = 400.0, maintenance = 150.0;
        const double appreciation = 0.03, selling_pct = 0.06;
        const double rent_saved0 = 1800.0, rent_increase = 0.03, discount_rate = 0.06;
        const int holding_years = 7;

        sensen::finance::HomeNpvRequest req;
        req.set_property_price(dec(property_price));
        req.set_down_payment(dec(down_payment));
        req.set_closing_costs_buy(dec(closing_buy));
        req.set_loan_amount(dec(loan_amount));
        req.set_loan_annual_rate(dec(loan_rate));
        req.set_loan_term_years(loan_term_years);
        req.set_monthly_taxes_ins_hoa(dec(taxes));
        req.set_monthly_maintenance(dec(maintenance));
        req.set_annual_appreciation_rate(dec(appreciation));
        req.set_selling_closing_cost_percent(dec(selling_pct));
        req.set_monthly_rent_saved(dec(rent_saved0));
        req.set_annual_rent_increase(dec(rent_increase));
        req.set_annual_discount_rate(dec(discount_rate));
        req.set_holding_period_years(holding_years);
        sensen::finance::HomeNpvResponse res;
        const auto ctx = make_context();
        if (const auto s = stub.ComputeHomeNpv(ctx.get(), req, &res); !s.ok()) {
            std::cerr << "ComputeHomeNpv FAILED: " << s.error_message() << "\n";
            return false;
        }

        // Reconstruct the cash-flow stream independently from the request,
        // per the model calculate_home_npv specifies
        // (financial.cppm:2196-2260): initial outflow D+closing at t=0;
        // monthly rent_saved - (pmt+taxes+maintenance) with rent bumped
        // every 12 months; terminal +sale - selling costs - remaining
        // balance. DATE-UNIT TRAP: dates are seconds on a 365-day year,
        // seconds_per_month = 31536000/12, so each month's exponent reduces
        // to m/12 years -- this reconstruction must use that, not day
        // offsets, or the discounting is wrong by a units mismatch that
        // still "looks" plausible.
        const double loan_rate_period = loan_rate / 12.0;
        const int total_loan_months = loan_term_years * 12;
        const double pmt_val =
            loan_amount * loan_rate_period /
            (1.0 - std::pow(1.0 + loan_rate_period, -static_cast<double>(total_loan_months)));
        const double seconds_per_month = 31536000.0 / 12.0;

        std::vector<double> cash_flows{-(down_payment + closing_buy)};
        std::vector<double> dates{0.0};
        double current_time = 0.0;
        double current_balance = loan_amount;
        double current_rent = rent_saved0;
        for (int m = 1; m <= holding_years * 12; ++m) {
            current_time += seconds_per_month;
            const double interest = current_balance * loan_rate_period;
            const double principal = pmt_val - interest;
            current_balance -= principal;
            if (current_balance < 0.0) current_balance = 0.0;
            const double monthly_outflow = pmt_val + taxes + maintenance;
            if (m > 1 && m % 12 == 1) {
                current_rent *= (1.0 + rent_increase);
            }
            cash_flows.push_back(current_rent - monthly_outflow);
            dates.push_back(current_time);
        }
        const double fv_home = property_price * std::pow(1.0 + appreciation, holding_years);
        const double selling_costs = fv_home * selling_pct;
        const double net_sale = fv_home - selling_costs - current_balance;
        cash_flows.back() += net_sale;

        // NPV(IRR) == 0 by definition of IRR, regardless of how it was
        // computed -- this catches a wrong IRR AND a divergent
        // reconstruction in one assertion.
        double npv_at_irr = 0.0;
        for (std::size_t i = 0; i < cash_flows.size(); ++i) {
            const double year_frac = (dates[i] - dates[0]) / 31536000.0;
            npv_at_irr += cash_flows[i] / std::pow(1.0 + res.internal_rate_of_return(), year_frac);
        }
        if (std::abs(npv_at_irr) > 1.0) {
            std::cerr << "NPV at the returned IRR " << res.internal_rate_of_return() << " is "
                      << npv_at_irr << ", expected ~0 on a six-figure model\n";
            return false;
        }

        // Cross-check against the already-served ComputeXnpv, at the
        // request's own discount rate, over this same reconstructed stream.
        sensen::finance::DatedCashFlowRequest xnpv_req;
        xnpv_req.set_rate(discount_rate);
        for (const double v : cash_flows) xnpv_req.add_values(v);
        for (const double d : dates) xnpv_req.add_dates(d);
        sensen::finance::DoubleResponse xnpv_res;
        const auto ctx2 = make_context();
        if (const auto s = stub.ComputeXnpv(ctx2.get(), xnpv_req, &xnpv_res); !s.ok()) {
            std::cerr << "ComputeXnpv (cross-check) FAILED: " << s.error_message() << "\n";
            return false;
        }
        const double rel_gap = std::abs(xnpv_res.value() - res.net_present_value()) /
                               std::max(1.0, std::abs(res.net_present_value()));
        if (rel_gap > 1e-6) {
            std::cerr << "ComputeHomeNpv net_present_value " << res.net_present_value()
                      << " disagrees with ComputeXnpv on the same reconstructed stream: "
                      << xnpv_res.value() << "\n";
            return false;
        }

        // Terminal legs.
        if (std::abs(res.future_sale_price() - fv_home) > 1e-4) {
            std::cerr << "future_sale_price " << res.future_sale_price() << " != P(1+a)^y "
                      << fv_home << "\n";
            return false;
        }
        const double expected_equity = fv_home - current_balance;
        if (std::abs(res.future_equity() - expected_equity) > 1e-2) {
            std::cerr << "future_equity " << res.future_equity() << " != sale - balance "
                      << expected_equity << "\n";
            return false;
        }
        std::cout << "          home NPV " << std::setprecision(2) << res.net_present_value()
                  << ", IRR " << std::setprecision(4) << (res.internal_rate_of_return() * 100.0)
                  << "%, NPV(IRR) = " << std::setprecision(6) << npv_at_irr
                  << ", matches ComputeXnpv\n";
    }

    // -- Refusals: malformed input must be rejected, not coerced ------------
    //
    // BigDecimal's own parser skips every non-digit, so "12x3" would silently
    // become 123. The service validates before parsing; these prove it, and
    // prove the other places where an unstated input is refused rather than
    // defaulted.
    {
        sensen::finance::PaymentRequest req;
        req.set_rate("12x3");
        req.set_periods(12);
        req.set_present_value("1000");
        sensen::finance::DecimalResponse res;
        const auto ctx = make_context();
        if (stub.ComputePayment(ctx.get(), req, &res).ok()) {
            std::cerr << "\"12x3\" was accepted as a rate -- it silently becomes 123\n";
            return false;
        }
    }
    {
        // No compounding frequency stated. The engine must not pick one.
        sensen::finance::FutureValueDetailedRequest req;
        req.set_annual_rate("0.05");
        req.set_years(10);
        req.set_annual_contribution("1000");
        sensen::finance::FutureValueDetailedResponse res;
        const auto ctx = make_context();
        if (stub.ComputeFutureValueDetailed(ctx.get(), req, &res).ok()) {
            std::cerr << "an absent compound_frequency was defaulted rather than refused\n";
            return false;
        }
    }
    {
        // Neither yield nor price: nothing here is derivable.
        sensen::finance::BondRequest req;
        req.set_par(1000.0);
        req.set_coupon_rate(0.05);
        req.set_frequency(2);
        req.set_years_to_maturity(10.0);
        sensen::finance::BondResponse res;
        const auto ctx = make_context();
        if (stub.AnalyzeBond(ctx.get(), req, &res).ok()) {
            std::cerr << "a bond with neither yield nor price returned an answer\n";
            return false;
        }
    }
    {
        // Two loans, one rate. Truncating to the shortest column would return a
        // shorter list that reads like a complete answer.
        sensen::finance::AmortizationBatchRequest req;
        req.add_loan_amounts(100000.0);
        req.add_loan_amounts(200000.0);
        req.add_annual_rates(0.05);
        sensen::finance::AmortizationBatchResponse res;
        const auto ctx = make_context();
        if (stub.ComputeAmortizationBatch(ctx.get(), req, &res).ok()) {
            std::cerr << "a ragged batch was accepted rather than refused\n";
            return false;
        }
    }
    {
        // ComputePayoffTiming: a payment below the periodic interest can
        // never amortize. sensen's calculate_payoff_timing returns
        // std::expected and refuses this (commit 4d4b4cbd) rather than
        // reporting "0 months remaining" -- this probe pins that fix
        // through the new RPC, not just inside sensen's own test suite.
        sensen::finance::PayoffTimingRequest req;
        req.set_current_loan_balance("200000");
        req.set_annual_rate("0.05");
        req.set_current_monthly_payment("500");   // < 200000*0.05/12 = 833.33 interest
        req.set_payments_per_year(12);
        sensen::finance::PayoffTimingResponse res;
        const auto ctx = make_context();
        if (stub.ComputePayoffTiming(ctx.get(), req, &res).ok()) {
            std::cerr << "a payoff-timing payment below the periodic interest was accepted "
                         "rather than refused\n";
            return false;
        }
    }
    {
        // ComputeHomeNpv: zero holding_period_years is refused rather than
        // silently producing an empty cash-flow stream.
        sensen::finance::HomeNpvRequest req;
        req.set_property_price("350000");
        req.set_down_payment("70000");
        req.set_loan_amount("280000");
        req.set_loan_annual_rate("0.05");
        req.set_loan_term_years(30);
        req.set_holding_period_years(0);
        sensen::finance::HomeNpvResponse res;
        const auto ctx = make_context();
        if (stub.ComputeHomeNpv(ctx.get(), req, &res).ok()) {
            std::cerr << "a zero holding_period_years was accepted rather than refused\n";
            return false;
        }
    }
    {
        // ComputeHomeNpv: zero loan_term_years makes the internal pmt()
        // call meaningless (nper=0).
        sensen::finance::HomeNpvRequest req;
        req.set_property_price("350000");
        req.set_down_payment("70000");
        req.set_loan_amount("280000");
        req.set_loan_annual_rate("0.05");
        req.set_loan_term_years(0);
        req.set_holding_period_years(7);
        sensen::finance::HomeNpvResponse res;
        const auto ctx = make_context();
        if (stub.ComputeHomeNpv(ctx.get(), req, &res).ok()) {
            std::cerr << "a zero loan_term_years was accepted rather than refused\n";
            return false;
        }
    }
    {
        // ComputeHomeNpv: the same malformed-decimal probe as ComputePayment
        // above, reused on a new field -- "12x3" must not silently become
        // 123. Every OTHER required decimal field is filled in with a real
        // value so this specifically exercises the malformed-annual_discount_rate
        // parse failure, not one of the absent-required-field refusals
        // exercised separately below.
        sensen::finance::HomeNpvRequest req;
        req.set_property_price("350000");
        req.set_down_payment("70000");
        req.set_closing_costs_buy("5000");
        req.set_loan_amount("280000");
        req.set_loan_annual_rate("0.05");
        req.set_loan_term_years(30);
        req.set_monthly_taxes_ins_hoa("400");
        req.set_monthly_maintenance("150");
        req.set_annual_appreciation_rate("0.03");
        req.set_selling_closing_cost_percent("0.06");
        req.set_monthly_rent_saved("1800");
        req.set_annual_rent_increase("0.03");
        req.set_holding_period_years(7);
        req.set_annual_discount_rate("12x3");
        sensen::finance::HomeNpvResponse res;
        const auto ctx = make_context();
        if (stub.ComputeHomeNpv(ctx.get(), req, &res).ok()) {
            std::cerr << "\"12x3\" was accepted as annual_discount_rate\n";
            return false;
        }
    }
    // -------------------------------------------------------------------
    // Adversarial regression cases, six new home-finance RPCs.
    //
    // Each of these reproduces an attack that was verified live against a
    // running engine before the corresponding handler guard existed --
    // resource exhaustion, integer overflow, BigDecimal magnitude overflow,
    // and a silent-zero at a rate the engine's own solver does not detect
    // as a domain error. See docs/superpowers/specs/2026-08-05-
    // finance-proto-extension.md for the six-RPC design and
    // finance_service.cpp's check_payments_per_year /
    // check_decimal_string_magnitude / check_rate_floor /
    // check_compound_growth_safe for the fix. Pinned here so none of them
    // can regress silently.
    // -------------------------------------------------------------------
    {
        // ComputeRefinance: payments_per_year=20,000,000 with new_term_years
        // at its cap (100) makes calculate_refinance_metrics walk
        // max(current_remaining_months, new_term_years*payments_per_year) =
        // 2,000,000,000 months -- measured at 11.5s of engine wall-clock
        // for this ONE call before the payments_per_year ceiling existed,
        // charged only cost_amortization(1200) (101 compute units against a
        // 120,000/hour anonymous budget) because the CHARGE line priced
        // new_term_years*12, not new_term_years*payments_per_year. Refused
        // AND fast is the assertion -- a refusal that still took 11 seconds
        // would still be a denial of service.
        sensen::finance::RefinanceRequest req;
        req.set_current_loan_balance("300000");
        req.set_current_monthly_payment("2000");
        req.set_current_annual_rate("0.06");
        req.set_current_remaining_months(300);
        req.set_property_value("400000");
        req.set_new_annual_rate("0.045");
        req.set_new_term_years(100);
        req.set_closing_costs("6000");
        req.set_payments_per_year(20000000);
        sensen::finance::RefinanceResponse res;
        const auto ctx = make_context();
        const auto t0 = std::chrono::steady_clock::now();
        const auto status = stub.ComputeRefinance(ctx.get(), req, &res);
        const auto elapsed = std::chrono::steady_clock::now() - t0;
        if (status.ok()) {
            std::cerr << "ComputeRefinance accepted payments_per_year=20,000,000 rather than "
                         "refusing an unreal payment cadence\n";
            return false;
        }
        if (elapsed > std::chrono::seconds(2)) {
            std::cerr << "ComputeRefinance took " << std::chrono::duration<double>(elapsed).count()
                      << "s to REFUSE payments_per_year=20,000,000 -- the refusal must happen "
                         "before the amortization walk, not after it\n";
            return false;
        }
    }
    {
        // ComputeRefinance: payments_per_year large enough to overflow the
        // int32 product new_term_years*payments_per_year, rather than merely
        // inflate it. Before the fix this returned a "successful" response
        // with payoff_date_shift_months = -1863463212 -- a wrong-but-
        // plausible-looking number from the wraparound, not an error.
        sensen::finance::RefinanceRequest req;
        req.set_current_loan_balance("300000");
        req.set_current_monthly_payment("2000");
        req.set_current_annual_rate("0.06");
        req.set_current_remaining_months(300);
        req.set_property_value("400000");
        req.set_new_annual_rate("0.045");
        req.set_new_term_years(100);
        req.set_closing_costs("6000");
        req.set_payments_per_year(2000000000);
        sensen::finance::RefinanceResponse res;
        const auto ctx = make_context();
        if (stub.ComputeRefinance(ctx.get(), req, &res).ok()) {
            std::cerr << "ComputeRefinance accepted payments_per_year=2,000,000,000 (an int32 "
                         "overflow of new_term_years*payments_per_year) rather than refusing\n";
            return false;
        }
    }
    {
        // ComputePayoffTiming, ComputeMortgageRecast, ComputeHomeFutureValue:
        // the same payments_per_year ceiling, one probe each -- all three
        // handlers only had a `> 0` floor, no ceiling, before this fix.
        sensen::finance::PayoffTimingRequest pt;
        pt.set_current_loan_balance("300000");
        pt.set_annual_rate("0.06");
        pt.set_current_monthly_payment("2000");
        pt.set_payments_per_year(2000000000);
        sensen::finance::PayoffTimingResponse pt_res;
        const auto ctx1 = make_context();
        if (stub.ComputePayoffTiming(ctx1.get(), pt, &pt_res).ok()) {
            std::cerr << "ComputePayoffTiming accepted payments_per_year=2,000,000,000\n";
            return false;
        }

        sensen::finance::MortgageRecastRequest mr;
        mr.set_current_loan_balance("300000");
        mr.set_current_monthly_payment("2000");
        mr.set_lump_sum_payment("0");
        mr.set_annual_rate("0.06");
        mr.set_remaining_months(300);
        mr.set_payments_per_year(2000000000);
        sensen::finance::MortgageRecastResponse mr_res;
        const auto ctx2 = make_context();
        if (stub.ComputeMortgageRecast(ctx2.get(), mr, &mr_res).ok()) {
            std::cerr << "ComputeMortgageRecast accepted payments_per_year=2,000,000,000\n";
            return false;
        }

        sensen::finance::HomeFutureValueRequest hf;
        hf.set_current_property_value("400000");
        hf.set_annual_appreciation_rate("0.03");
        hf.set_current_loan_balance("300000");
        hf.set_annual_mortgage_rate("0.06");
        hf.set_current_monthly_payment("2000");
        hf.set_target_years(100);
        hf.set_payments_per_year(2000000000);
        sensen::finance::HomeFutureValueResponse hf_res;
        const auto ctx3 = make_context();
        if (stub.ComputeHomeFutureValue(ctx3.get(), hf, &hf_res).ok()) {
            std::cerr << "ComputeHomeFutureValue accepted payments_per_year=2,000,000,000\n";
            return false;
        }
    }
    {
        // ComputeHeloc: the SAME unbounded-integer shape as the five RPCs
        // above, on an RPC that predates them and was already deployed. It is
        // covered here rather than left to the newer six because
        // calculate_heloc_metrics computes
        //     int total_repayment_periods = repayment_term_years * payments_per_year;
        // -- an int32 product of two caller-controlled values that both
        // carried only a `> 0` floor, so the multiplication itself is signed
        // overflow (UB) before the result is used. Both operands get a probe,
        // because bounding only one of them still leaves the product
        // overflowable from the other side.
        const auto heloc_probe = [&](int term_years, int ppy, const char* rate,
                                     const char* what) -> bool {
            sensen::finance::HelocRequest req;
            req.set_home_value("500000");
            req.set_current_mortgage_balance("300000");
            req.set_max_ltv_rate("0.8");
            req.set_drawn_amount("50000");
            req.set_annual_rate(rate);
            req.set_repayment_term_years(term_years);
            req.set_payments_per_year(ppy);
            sensen::finance::HelocResponse res;
            const auto c = make_context();
            if (stub.ComputeHeloc(c.get(), req, &res).ok()) {
                std::cerr << "ComputeHeloc accepted " << what << " rather than refusing; it "
                          << "answered repayment_period_payment="
                          << res.repayment_period_payment() << "\n";
                return false;
            }
            return true;
        };
        if (!heloc_probe(100, 2000000000, "0.07",
                         "payments_per_year=2,000,000,000 (int32 overflow of "
                         "repayment_term_years*payments_per_year)")) {
            return false;
        }
        if (!heloc_probe(2000000000, 12, "0.07",
                         "repayment_term_years=2,000,000,000 (the same overflow from the "
                         "other operand)")) {
            return false;
        }
        // A rate extreme enough to overflow pmt()'s BigDecimal pow() even once
        // both integer operands are bounded -- the recast/home-FV failure mode,
        // which a ceiling on the period count alone does not close.
        if (!heloc_probe(100, 366, "1000000", "annual_rate=1,000,000 (100,000,000% APR)")) {
            return false;
        }
        // Non-vacuous control: an ordinary HELOC must still be answered. Without
        // this, refusing every ComputeHeloc call would pass the three probes
        // above and look like a fix.
        {
            sensen::finance::HelocRequest req;
            req.set_home_value("500000");
            req.set_current_mortgage_balance("300000");
            req.set_max_ltv_rate("0.8");
            req.set_drawn_amount("50000");
            req.set_annual_rate("0.07");
            req.set_repayment_term_years(15);
            req.set_payments_per_year(12);
            sensen::finance::HelocResponse res;
            const auto c = make_context();
            if (const auto s = stub.ComputeHeloc(c.get(), req, &res); !s.ok()) {
                std::cerr << "ComputeHeloc refused an ordinary 15-year/monthly HELOC: "
                          << s.error_message() << "\n";
                return false;
            }
        }
    }
    {
        // ComputePayoffTiming: annual_rate=-1 with payments_per_year=1 makes
        // the per-period rate exactly -100%. calculate_payoff_timing's own
        // numerator/denominator sign check does not catch this -- it
        // evaluates log(0)/log(1-1) down to a clean IEEE 0.0, so the RPC
        // returned original_months_remaining=0 ("already paid off") for a
        // loan that manifestly is not, instead of refusing. This is the
        // silent-zero hazard the six-RPC spec's own §1b flagged, just not at
        // the input the spec's own probe exercises.
        sensen::finance::PayoffTimingRequest req;
        req.set_current_loan_balance("300000");
        req.set_annual_rate("-1");
        req.set_current_monthly_payment("2000");
        req.set_payments_per_year(1);
        sensen::finance::PayoffTimingResponse res;
        const auto ctx = make_context();
        if (stub.ComputePayoffTiming(ctx.get(), req, &res).ok()) {
            std::cerr << "ComputePayoffTiming accepted a -100%-per-period rate rather than "
                         "refusing it, and " << (res.original_months_remaining() == 0
                                                      ? "returned the silent-zero \"already paid "
                                                        "off\" answer"
                                                      : "returned a real-looking answer")
                      << " for a loan that cannot amortize at that rate\n";
            return false;
        }
    }
    {
        // ComputeMortgageRecast: the same -100%-per-period rate. Before the
        // fix this returned new_monthly_payment="0.000...000" -- "the
        // recast pays this loan off entirely" -- which is the same
        // silent-zero shape, through pmt()'s BigDecimal pow(0) rather than
        // nper_fn's log().
        sensen::finance::MortgageRecastRequest req;
        req.set_current_loan_balance("300000");
        req.set_current_monthly_payment("2000");
        req.set_lump_sum_payment("0");
        req.set_annual_rate("-1");
        req.set_remaining_months(300);
        req.set_payments_per_year(1);
        sensen::finance::MortgageRecastResponse res;
        const auto ctx = make_context();
        if (stub.ComputeMortgageRecast(ctx.get(), req, &res).ok()) {
            std::cerr << "ComputeMortgageRecast accepted a -100%-per-period rate rather than "
                         "refusing it\n";
            return false;
        }
    }
    {
        // ComputeMortgageRecast: annual_rate=1,000,000 (100,000,000% APR)
        // with remaining_months at its cap (1200) overflows BigDecimal's
        // pow() -- measured before the fix as new_monthly_payment =
        // "-62493201672074.130046976086341595" for a $300,000 loan, a
        // wrong-but-plausible-looking number from the __int128 wraparound,
        // not an error.
        sensen::finance::MortgageRecastRequest req;
        req.set_current_loan_balance("300000");
        req.set_current_monthly_payment("2000");
        req.set_lump_sum_payment("0");
        req.set_annual_rate("1000000");
        req.set_remaining_months(1200);
        req.set_payments_per_year(12);
        sensen::finance::MortgageRecastResponse res;
        const auto ctx = make_context();
        if (stub.ComputeMortgageRecast(ctx.get(), req, &res).ok()) {
            std::cerr << "ComputeMortgageRecast accepted annual_rate=1,000,000 (100,000,000% "
                         "APR) rather than refusing a compounding factor its exact decimal "
                         "engine cannot represent\n";
            return false;
        }
    }
    {
        // ComputeHomeFutureValue: the same extreme-rate overflow, through
        // fv()'s BigDecimal pow(). Measured before the fix as
        // future_loan_balance = "111827623405189370487.15..." for a
        // $300,000 starting balance.
        sensen::finance::HomeFutureValueRequest req;
        req.set_current_property_value("400000");
        req.set_annual_appreciation_rate("0.03");
        req.set_current_loan_balance("300000");
        req.set_annual_mortgage_rate("1000000");
        req.set_current_monthly_payment("2000");
        req.set_target_years(100);
        req.set_payments_per_year(12);
        sensen::finance::HomeFutureValueResponse res;
        const auto ctx = make_context();
        if (stub.ComputeHomeFutureValue(ctx.get(), req, &res).ok()) {
            std::cerr << "ComputeHomeFutureValue accepted annual_mortgage_rate=1,000,000 rather "
                         "than refusing a compounding factor its exact decimal engine cannot "
                         "represent\n";
            return false;
        }
    }
    {
        // ComputeRefinance: a 200-digit current_loan_balance. BigDecimal is
        // an exact __int128 fixed-point type (scale 1e18) with a
        // representable ceiling around 1.7e20 -- about 20 integer digits --
        // and neither its string constructor nor its arithmetic detect an
        // overflow past that; they wrap. Measured before the fix as
        // new_loan_amount = "-75618303760208547436.42..." -- NEGATIVE --
        // from a 200-digit POSITIVE input.
        sensen::finance::RefinanceRequest req;
        req.set_current_loan_balance(std::string(200, '1'));
        req.set_current_monthly_payment("2000");
        req.set_current_annual_rate("0.06");
        req.set_current_remaining_months(300);
        req.set_property_value("400000");
        req.set_new_annual_rate("0.045");
        req.set_new_term_years(30);
        req.set_closing_costs("6000");
        req.set_payments_per_year(12);
        sensen::finance::RefinanceResponse res;
        const auto ctx = make_context();
        if (stub.ComputeRefinance(ctx.get(), req, &res).ok()) {
            std::cerr << "ComputeRefinance accepted a 200-digit current_loan_balance rather than "
                         "refusing a magnitude its exact decimal engine cannot represent (a "
                         "positive 200-digit input previously came back as a NEGATIVE "
                         "new_loan_amount)\n";
            return false;
        }
    }
    {
        // ComputeHomeNpv: holding_period_years = INT32_MAX. The CHARGE line
        // above this RPC's validation multiplies holding_period_years*12
        // BEFORE the <=100 check runs (CHARGE must run before validation --
        // see finance_service.cpp's own comment on why -- so it prices the
        // unvalidated request). INT32_MAX*12 overflows int32, which is
        // undefined behaviour regardless of what the validation two lines
        // later does with the result. This probe cannot observe the UB
        // directly through the RPC surface; what it pins is that the
        // request is refused cleanly, so a future change to the CHARGE
        // line's arithmetic cannot silently reopen it without a build
        // sanitizer (or this probe, under one) catching it.
        sensen::finance::HomeNpvRequest req;
        req.set_property_price("400000");
        req.set_down_payment("80000");
        req.set_closing_costs_buy("8000");
        req.set_loan_amount("320000");
        req.set_loan_annual_rate("0.06");
        req.set_loan_term_years(30);
        req.set_monthly_taxes_ins_hoa("500");
        req.set_monthly_maintenance("200");
        req.set_annual_appreciation_rate("0.03");
        req.set_selling_closing_cost_percent("0.06");
        req.set_monthly_rent_saved("2000");
        req.set_annual_rent_increase("0.03");
        req.set_annual_discount_rate("0.05");
        req.set_holding_period_years(2147483647);
        sensen::finance::HomeNpvResponse res;
        const auto ctx = make_context();
        if (stub.ComputeHomeNpv(ctx.get(), req, &res).ok()) {
            std::cerr << "ComputeHomeNpv accepted holding_period_years=INT32_MAX rather than "
                         "refusing it\n";
            return false;
        }
    }
    // -------------------------------------------------------------------
    // Absent/empty REQUIRED decimal fields must be refused, not silently
    // computed on as zero.
    //
    // Envoy's gRPC-JSON transcoder drops any JSON field it does not
    // recognise, and proto3 scalars have no wire presence -- so a mistyped
    // field name and a field the caller genuinely never set are BOTH just
    // "" on the wire, indistinguishable from each other and, before this
    // fix, from an explicit "$0"/"0%". Verified live against a running
    // engine before the fix existed:
    //
    //   POST ComputePayment {"present_value":"300000","rate":"0.005","periods":360}
    //     -> "-1798.651575458257198999"                          (correct)
    //   POST ComputePayment {"presentvalue":"300000","rate":"0.005","periods":360}
    //     -> "0.000000000000000000"           HTTP 200  (typo -> silent 0)
    //   POST ComputePayment {}
    //     -> "0.000000000000000000"           HTTP 200
    //   POST ComputePayment {"present_value":"","rate":"0.005","periods":360}
    //     -> "0.000000000000000000"           HTTP 200
    //   POST ComputePayment {"present_value":"300000","periods":360}
    //     -> "-833.333333333333333333"        HTTP 200  (absent rate -> 0%)
    //   POST ComputeAmortization with loan_amount mistyped
    //     -> empty schedule, all-zero summary, HTTP 200
    //
    // A gRPC C++ stub cannot itself mistype a JSON field name -- proto
    // setters are compile-checked -- but "the setter for this field was
    // never called" is EXACTLY what a mistyped JSON name produces once past
    // the transcoder (an unset string field, "" on the wire), so every case
    // below reproduces the bug at the point that matters: the handler
    // receiving an empty required field, regardless of how it got that way.
    // See finance_service.cpp's REQUIRE_DECIMAL / REQUIRE_DECIMAL_SAFE /
    // check_periods for the fix; each case here is pinned so none of them
    // can regress back to a silent zero.
    // -------------------------------------------------------------------
    {
        // Entirely empty request body -- the {} case above.
        sensen::finance::PaymentRequest req;
        sensen::finance::DecimalResponse res;
        const auto ctx = make_context();
        if (stub.ComputePayment(ctx.get(), req, &res).ok()) {
            std::cerr << "ComputePayment accepted an entirely empty request rather than "
                         "refusing it\n";
            return false;
        }
    }
    {
        // The exact "present_value typo'd to presentvalue" shape: rate and
        // periods are real, present_value's setter was simply never called.
        sensen::finance::PaymentRequest req;
        req.set_rate("0.005");
        req.set_periods(360);
        sensen::finance::DecimalResponse res;
        const auto ctx = make_context();
        if (stub.ComputePayment(ctx.get(), req, &res).ok()) {
            std::cerr << "ComputePayment accepted an absent present_value (the mistyped-"
                         "field-name shape) rather than refusing it\n";
            return false;
        }
    }
    {
        // The exact "omitted rate" request that measured -833.333... in
        // production: present_value and periods only, no rate at all.
        sensen::finance::PaymentRequest req;
        req.set_present_value("300000");
        req.set_periods(360);
        sensen::finance::DecimalResponse res;
        const auto ctx = make_context();
        if (stub.ComputePayment(ctx.get(), req, &res).ok()) {
            std::cerr << "ComputePayment accepted an absent rate rather than refusing it -- "
                         "this is the exact request that used to silently return "
                         "-833.333333333333333333 (principal/periods at an assumed 0%)\n";
            return false;
        }
    }
    {
        // periods=0 (never set) is the same silent-zero class through an
        // int32 default rather than an empty string: pmt()'s own BigDecimal
        // divide resolves 0/0 through value_or(0) instead of erroring.
        sensen::finance::PaymentRequest req;
        req.set_rate("0.005");
        req.set_present_value("300000");
        sensen::finance::DecimalResponse res;
        const auto ctx = make_context();
        if (stub.ComputePayment(ctx.get(), req, &res).ok()) {
            std::cerr << "ComputePayment accepted periods=0 (never set) rather than refusing "
                         "it\n";
            return false;
        }
    }
    {
        sensen::finance::PresentValueRequest req;
        req.set_periods(360);
        req.set_payment("-1798.651575458257198999");
        sensen::finance::DecimalResponse res;
        const auto ctx = make_context();
        if (stub.ComputePresentValue(ctx.get(), req, &res).ok()) {
            std::cerr << "ComputePresentValue accepted an absent rate rather than refusing it\n";
            return false;
        }
    }
    {
        sensen::finance::FutureValueRequest req;
        req.set_rate("0.005");
        req.set_periods(360);
        sensen::finance::DecimalResponse res;
        const auto ctx = make_context();
        if (stub.ComputeFutureValue(ctx.get(), req, &res).ok()) {
            std::cerr << "ComputeFutureValue accepted an absent payment rather than refusing "
                         "it\n";
            return false;
        }
    }
    {
        sensen::finance::FutureValueDetailedRequest req;
        req.set_years(10);
        req.set_annual_contribution("1000");
        req.set_compound_frequency(12);
        sensen::finance::FutureValueDetailedResponse res;
        const auto ctx = make_context();
        if (stub.ComputeFutureValueDetailed(ctx.get(), req, &res).ok()) {
            std::cerr << "ComputeFutureValueDetailed accepted an absent annual_rate rather "
                         "than refusing it\n";
            return false;
        }
    }
    {
        sensen::finance::PeriodPaymentRequest req;
        req.set_period(1);
        req.set_periods(360);
        req.set_present_value("300000");
        sensen::finance::DecimalResponse ires;
        sensen::finance::DecimalResponse pres;
        const auto c1 = make_context();
        const auto c2 = make_context();
        if (stub.ComputeInterestPayment(c1.get(), req, &ires).ok()) {
            std::cerr << "ComputeInterestPayment accepted an absent rate rather than refusing "
                         "it\n";
            return false;
        }
        if (stub.ComputePrincipalPayment(c2.get(), req, &pres).ok()) {
            std::cerr << "ComputePrincipalPayment accepted an absent rate rather than "
                         "refusing it\n";
            return false;
        }
    }
    {
        sensen::finance::RateRequest req;
        req.set_periods(360);
        req.set_present_value("300000");
        sensen::finance::DecimalResponse res;
        const auto ctx = make_context();
        if (stub.ComputeRate(ctx.get(), req, &res).ok()) {
            std::cerr << "ComputeRate accepted an absent payment rather than refusing it\n";
            return false;
        }
    }
    {
        sensen::finance::PeriodsRequest req;
        req.set_payment("-1798.651575458257198999");
        req.set_present_value("300000");
        sensen::finance::DecimalResponse res;
        const auto ctx = make_context();
        if (stub.ComputePeriods(ctx.get(), req, &res).ok()) {
            std::cerr << "ComputePeriods accepted an absent rate rather than refusing it\n";
            return false;
        }
    }
    {
        // ComputeAmortization: the exact "mistyped loan_amount" shape --
        // used to return an empty schedule and an all-zero summary at
        // HTTP 200.
        sensen::finance::AmortizationRequest req;
        req.set_annual_rate("0.06");
        req.set_term_months(360);
        sensen::finance::AmortizationResponse res;
        const auto ctx = make_context();
        if (stub.ComputeAmortization(ctx.get(), req, &res).ok()) {
            std::cerr << "ComputeAmortization accepted an absent loan_amount rather than "
                         "refusing it -- this is the exact request that used to return an "
                         "empty schedule and an all-zero summary\n";
            return false;
        }
    }
    {
        sensen::finance::AmortizationRequest req;
        req.set_loan_amount("300000");
        req.set_term_months(360);
        sensen::finance::AmortizationResponse res;
        const auto ctx = make_context();
        if (stub.ComputeAmortization(ctx.get(), req, &res).ok()) {
            std::cerr << "ComputeAmortization accepted an absent annual_rate rather than "
                         "refusing it -- an absent rate silently becomes a 0% loan\n";
            return false;
        }
    }
    {
        sensen::finance::DetailedAmortizationRequest req;
        req.set_annual_rate("0.06");
        req.set_term_months(360);
        sensen::finance::DetailedAmortizationResponse res;
        const auto ctx = make_context();
        if (stub.ComputeDetailedAmortization(ctx.get(), req, &res).ok()) {
            std::cerr << "ComputeDetailedAmortization accepted an absent loan_amount rather "
                         "than refusing it\n";
            return false;
        }
    }
    {
        sensen::finance::HelocRequest req;
        req.set_current_mortgage_balance("300000");
        req.set_max_ltv_rate("0.80");
        req.set_annual_rate("0.07");
        req.set_repayment_term_years(15);
        req.set_payments_per_year(12);
        sensen::finance::HelocResponse res;
        const auto ctx = make_context();
        if (stub.ComputeHeloc(ctx.get(), req, &res).ok()) {
            std::cerr << "ComputeHeloc accepted an absent home_value rather than refusing it\n";
            return false;
        }
    }
    {
        sensen::finance::HelocRequest req;
        req.set_home_value("500000");
        req.set_current_mortgage_balance("300000");
        req.set_max_ltv_rate("0.80");
        req.set_repayment_term_years(15);
        req.set_payments_per_year(12);
        sensen::finance::HelocResponse res;
        const auto ctx = make_context();
        if (stub.ComputeHeloc(ctx.get(), req, &res).ok()) {
            std::cerr << "ComputeHeloc accepted an absent annual_rate rather than refusing "
                         "it\n";
            return false;
        }
    }
    {
        sensen::finance::RefinanceRequest req;
        req.set_current_monthly_payment("2000");
        req.set_current_annual_rate("0.06");
        req.set_current_remaining_months(300);
        req.set_property_value("400000");
        req.set_new_annual_rate("0.045");
        req.set_new_term_years(30);
        req.set_closing_costs("6000");
        req.set_payments_per_year(12);
        sensen::finance::RefinanceResponse res;
        const auto ctx = make_context();
        if (stub.ComputeRefinance(ctx.get(), req, &res).ok()) {
            std::cerr << "ComputeRefinance accepted an absent current_loan_balance rather "
                         "than refusing it\n";
            return false;
        }
    }
    {
        // The new-loan rate absent: the same "confidently wrong, not
        // obviously broken" shape as the ComputePayment headline bug --
        // the refinance would silently price the new loan as interest-free.
        sensen::finance::RefinanceRequest req;
        req.set_current_loan_balance("300000");
        req.set_current_monthly_payment("2000");
        req.set_current_annual_rate("0.06");
        req.set_current_remaining_months(300);
        req.set_property_value("400000");
        req.set_new_term_years(30);
        req.set_closing_costs("6000");
        req.set_payments_per_year(12);
        sensen::finance::RefinanceResponse res;
        const auto ctx = make_context();
        if (stub.ComputeRefinance(ctx.get(), req, &res).ok()) {
            std::cerr << "ComputeRefinance accepted an absent new_annual_rate rather than "
                         "refusing it\n";
            return false;
        }
    }
    {
        sensen::finance::PayoffTimingRequest req;
        req.set_annual_rate("0.05");
        req.set_current_monthly_payment("1580");
        req.set_payments_per_year(12);
        sensen::finance::PayoffTimingResponse res;
        const auto ctx = make_context();
        if (stub.ComputePayoffTiming(ctx.get(), req, &res).ok()) {
            std::cerr << "ComputePayoffTiming accepted an absent current_loan_balance rather "
                         "than refusing it\n";
            return false;
        }
    }
    {
        sensen::finance::PayoffTimingRequest req;
        req.set_current_loan_balance("200000");
        req.set_current_monthly_payment("1580");
        req.set_payments_per_year(12);
        sensen::finance::PayoffTimingResponse res;
        const auto ctx = make_context();
        if (stub.ComputePayoffTiming(ctx.get(), req, &res).ok()) {
            std::cerr << "ComputePayoffTiming accepted an absent annual_rate rather than "
                         "refusing it\n";
            return false;
        }
    }
    {
        sensen::finance::MortgageRecastRequest req;
        req.set_current_monthly_payment("2000");
        req.set_lump_sum_payment("0");
        req.set_annual_rate("0.055");
        req.set_remaining_months(240);
        req.set_payments_per_year(12);
        sensen::finance::MortgageRecastResponse res;
        const auto ctx = make_context();
        if (stub.ComputeMortgageRecast(ctx.get(), req, &res).ok()) {
            std::cerr << "ComputeMortgageRecast accepted an absent current_loan_balance "
                         "rather than refusing it\n";
            return false;
        }
    }
    {
        sensen::finance::RentalRoiRequest req;
        req.set_total_cash_invested("100000");
        req.set_periodic_gross_rent("3000");
        req.set_periodic_operating_expenses("800");
        req.set_periods_per_year(12);
        sensen::finance::RentalRoiResponse res;
        const auto ctx = make_context();
        if (stub.ComputeRentalRoi(ctx.get(), req, &res).ok()) {
            std::cerr << "ComputeRentalRoi accepted an absent property_value rather than "
                         "refusing it\n";
            return false;
        }
    }
    {
        sensen::finance::HomeFutureValueRequest req;
        req.set_annual_appreciation_rate("0.03");
        req.set_current_loan_balance("250000");
        req.set_annual_mortgage_rate("0.045");
        req.set_current_monthly_payment("1266.71");
        req.set_target_years(10);
        req.set_payments_per_year(12);
        sensen::finance::HomeFutureValueResponse res;
        const auto ctx = make_context();
        if (stub.ComputeHomeFutureValue(ctx.get(), req, &res).ok()) {
            std::cerr << "ComputeHomeFutureValue accepted an absent current_property_value "
                         "rather than refusing it\n";
            return false;
        }
    }
    {
        sensen::finance::RentVsBuyRequest req;
        req.set_down_payment("80000");
        req.set_monthly_piti_and_maintenance("2200");
        req.set_annual_home_appreciation("0.03");
        req.set_current_monthly_rent("2000");
        req.set_annual_rent_increase("0.02");
        req.set_annual_investment_return("0.05");
        req.set_years(7);
        sensen::finance::RentVsBuyResponse res;
        const auto ctx = make_context();
        if (stub.ComputeRentVsBuy(ctx.get(), req, &res).ok()) {
            std::cerr << "ComputeRentVsBuy accepted an absent property_price rather than "
                         "refusing it\n";
            return false;
        }
    }
    {
        sensen::finance::HomeNpvRequest req;
        req.set_down_payment("70000");
        req.set_closing_costs_buy("5000");
        req.set_loan_amount("280000");
        req.set_loan_annual_rate("0.05");
        req.set_loan_term_years(30);
        req.set_monthly_taxes_ins_hoa("400");
        req.set_monthly_maintenance("150");
        req.set_annual_appreciation_rate("0.03");
        req.set_selling_closing_cost_percent("0.06");
        req.set_monthly_rent_saved("1800");
        req.set_annual_rent_increase("0.03");
        req.set_annual_discount_rate("0.06");
        req.set_holding_period_years(7);
        sensen::finance::HomeNpvResponse res;
        const auto ctx = make_context();
        if (stub.ComputeHomeNpv(ctx.get(), req, &res).ok()) {
            std::cerr << "ComputeHomeNpv accepted an absent property_price rather than "
                         "refusing it\n";
            return false;
        }
    }
    std::cout << "          refusals  malformed decimal, absent frequency, underspecified "
                 "bond, ragged batch, payoff-timing interest coverage, home-NPV zero holding/"
                 "loan term\n"
              << "          adversarial  payments_per_year DoS + overflow (refinance/payoff-"
                 "timing/recast/home-FV/heloc), -100%-per-period silent zero (payoff-timing/"
                 "recast), extreme-rate BigDecimal overflow (recast/home-FV/heloc), 200-digit "
                 "magnitude overflow (refinance), home-NPV INT32_MAX holding period, heloc "
                 "term-years overflow from the other operand -- with an ordinary HELOC still "
                 "answered as the non-vacuous control\n"
              << "          absent-required  every RPC with a required decimal field refuses "
                 "an empty request, a mistyped-field-name shape, and an absent rate/principal/"
                 "price -- never silently computes on a zero (optional fields -- future_value, "
                 "PMI, overpayments, cash_out_amount, drawn_amount, extra/lump payments -- "
                 "still default correctly when genuinely omitted)\n";
    return true;
}

/**
 * The LLM-backed strategy assistant -- calculator.assistant.StrategyAssistant,
 * a THIRD contract on this port, checked in the same spirit as check_finance:
 * against invariants that hold independently of any particular answer, never
 * against a string the model happened to produce.
 *
 * An LLM is not bit-deterministic the way a closed-form bond price is, so
 * this function never asserts an exact `<params>` payload or an exact
 * clarifying question. What it asserts on every call, regardless of which
 * branch of the ParseResponse oneof comes back:
 *
 *   - the oneof is actually SET. OUTCOME_NOT_SET is impossible under the
 *     server's own contract (interpret_model_output always populates one
 *     branch before returning OK), so seeing it here is this service
 *     failing to do its job, not the model phrasing something oddly.
 *   - if `params` is the branch that came back, `strategy` names something
 *     in the 47-entry catalogue CalculateStrategy actually knows how to
 *     price, `expiration_days` and `quantity` sit inside the sane ranges
 *     the service itself enforces, and the symbol is the one the utterance
 *     actually named -- never a different, invented one.
 *
 * kKnownStrategyIds and the two numeric ranges below are a fixed mirror of
 * strategy_catalogue.cppm's kCatalogue and assistant_service.cpp's own
 * kMin/kMaxExpirationDays and kMin/kMaxQuantity constants -- duplicated here
 * as reference data the same way check_finance writes out the PMT annuity
 * formula independently of the engine that also implements it, not derived
 * from anything this binary reads off the service at runtime. If the
 * catalogue is regenerated (scripts/generate_strategy_catalogue.py), this
 * list is regenerated with it.
 */
auto check_assistant(calculator::assistant::StrategyAssistant::Stub& stub) -> bool {
    static constexpr std::array<std::string_view, 47> kKnownStrategyIds{{
        "long_call", "bull_call_spread", "bull_put_spread", "call_backspread", "risk_reversal",
        "synthetic_long", "call_ratio_spread", "bull_call_ladder", "long_put", "bear_put_spread",
        "bear_call_spread", "put_backspread", "synthetic_short", "put_ratio_spread", "covered_put",
        "iron_condor", "condor", "call_butterfly", "put_butterfly", "iron_butterfly",
        "broken_wing_butterfly", "short_straddle", "short_strangle", "jade_lizard", "box_spread",
        "long_straddle", "long_strangle", "reverse_iron_condor", "long_guts", "strip", "strap",
        "calendar_spread", "diagonal_spread", "double_diagonal", "covered_call",
        "cash_secured_put", "protective_put", "collar", "pmcc", "futures_long", "futures_short",
        "futures_calendar", "spark_spread", "crush_spread", "cash_and_carry",
        "covered_futures_call", "min_variance_hedge",
    }};
    constexpr std::int64_t kMinExpirationDays = 0;
    constexpr std::int64_t kMaxExpirationDays = 3650;
    constexpr std::int64_t kMinQuantity = 1;
    constexpr std::int64_t kMaxQuantity = 100'000;
    constexpr std::size_t kMaxClarificationLength = 400;

    // Validates the invariants a StrategyParams must satisfy NO MATTER which
    // prompt produced it. Shared between the positive and negative cases
    // below so both hold the server to the identical bar.
    const auto validate_params = [&](const calculator::assistant::StrategyParams& p,
                                     const char* label) -> bool {
        if (std::find(kKnownStrategyIds.begin(), kKnownStrategyIds.end(), p.strategy()) ==
            kKnownStrategyIds.end()) {
            std::cerr << label << ": strategy \"" << p.strategy()
                      << "\" is not one of the 47 catalogued ids\n";
            return false;
        }
        if (p.asset_class() != "EQUITY" && p.asset_class() != "FUTURES" &&
            p.asset_class() != "CRYPTO") {
            std::cerr << label << ": asset_class \"" << p.asset_class() << "\" is none of "
                      << "EQUITY/FUTURES/CRYPTO\n";
            return false;
        }
        if (p.expiration_days() < kMinExpirationDays || p.expiration_days() > kMaxExpirationDays) {
            std::cerr << label << ": expiration_days " << p.expiration_days()
                      << " is outside [" << kMinExpirationDays << ", " << kMaxExpirationDays
                      << "]\n";
            return false;
        }
        if (p.quantity() < kMinQuantity || p.quantity() > kMaxQuantity) {
            std::cerr << label << ": quantity " << p.quantity() << " is outside ["
                      << kMinQuantity << ", " << kMaxQuantity << "]\n";
            return false;
        }
        return true;
    };

    // -- The positive case: a complete, unambiguous trade description ------
    //
    // Every fact CalculateStrategy needs is stated in plain language, so an
    // honest assistant has no unstated variable left to ask about. A
    // Clarification is still tolerated here (a model can reasonably want one
    // more fact confirmed even when everything was supplied -- that is a
    // quality question for a human reading transcripts, not something this
    // function can adjudicate), but a Refusal is not: refusing a request
    // this complete would be the assistant failing to do its job, not the
    // assistant declining to guess.
    calculator::assistant::ParseRequest positive;
    positive.set_utterance("Buy a bull call spread on NVDA expiring in 30 days, 2 contracts.");
    calculator::assistant::ParseResponse positive_res;
    const auto positive_ctx = make_context(kAssistantDeadline);
    const auto positive_status = stub.ParseStrategy(positive_ctx.get(), positive, &positive_res);
    if (!positive_status.ok()) {
        std::cerr << "ParseStrategy FAILED: " << positive_status.error_code() << " "
                  << positive_status.error_message() << "\n";
        return false;
    }

    // The model is an OPTIONAL subsystem: this container may legitimately
    // run with no MODEL_PATH set (or a load that failed), and the service's
    // own documented contract for that case is to degrade honestly rather
    // than fabricate a parse -- exactly the Refusal(MODEL_UNAVAILABLE) the
    // proto defines for it. Treating that as a SKIP rather than a FAIL is
    // the only choice that is not itself dishonest: failing the whole gate
    // over an optional model artifact this environment never claimed to
    // ship would block every build that does not bundle 639 MB of weights,
    // and silently passing without checking anything would let a genuinely
    // broken assistant hide behind "well, it's optional". Printing exactly
    // why it was skipped keeps it visible instead of silent.
    if (positive_res.outcome_case() == calculator::assistant::ParseResponse::kRefusal &&
        positive_res.refusal().reason() == calculator::assistant::Refusal::MODEL_UNAVAILABLE) {
        std::cout << "StrategyAssistant  MODEL_PATH unset (or load failed) -- the assistant is "
                     "correctly degraded rather than serving a guess. SKIPPING the \"llm\" suite: "
                     "this is an optional subsystem, and no environment is guaranteed to carry "
                     "the model artifact.\n";
        return true;
    }

    switch (positive_res.outcome_case()) {
        case calculator::assistant::ParseResponse::kParams: {
            const auto& p = positive_res.params();
            if (!validate_params(p, "positive case")) return false;
            std::string upper_symbol = p.symbol();
            std::transform(upper_symbol.begin(), upper_symbol.end(), upper_symbol.begin(),
                          [](unsigned char c) { return std::toupper(c); });
            if (upper_symbol != "NVDA") {
                std::cerr << "positive case: symbol \"" << p.symbol() << "\" was not the NVDA "
                          << "the utterance named -- the assistant invented a different one\n";
                return false;
            }
            std::cout << "StrategyAssistant  \"" << positive.utterance() << "\"\n"
                      << "  -> params  symbol=" << p.symbol() << "  asset_class="
                      << p.asset_class() << "  strategy=" << p.strategy()
                      << "  expiration_days=" << p.expiration_days()
                      << "  quantity=" << p.quantity() << "\n";
            break;
        }
        case calculator::assistant::ParseResponse::kClarification: {
            const auto& q = positive_res.clarification().question();
            if (q.empty() || q.size() > kMaxClarificationLength) {
                std::cerr << "positive case: clarification question is empty or implausibly "
                          << "long (" << q.size() << " chars)\n";
                return false;
            }
            std::cout << "StrategyAssistant  \"" << positive.utterance() << "\"\n"
                      << "  -> clarification (tolerated on a complete request): \"" << q
                      << "\"\n";
            break;
        }
        case calculator::assistant::ParseResponse::kRefusal:
            std::cerr << "positive case: a complete, unambiguous trade description was refused "
                      << "(reason " << positive_res.refusal().reason() << "): "
                      << positive_res.refusal().message() << "\n";
            return false;
        default:
            std::cerr << "ParseResponse.outcome was not set at all -- the oneof contract was "
                      << "violated\n";
            return false;
    }

    // -- The negative case: requests that must NEVER produce params --------
    //
    // This is the check that actually defends real-data-only policy on the
    // intent side, per assistant.proto's own file banner: a guess dressed up
    // as a parse is exactly as dishonest as a fabricated market quote. Two
    // different failure shapes are covered so the check does not hinge on
    // one prompt happening to land on the model's good side: something that
    // was never about a trade at all, and something that names no symbol,
    // strategy or expiration for the assistant to act on. Neither may EVER
    // come back as StrategyParams; a Clarification or a Refusal (any reason)
    // are both honest, acceptable answers to either one.
    const std::vector<std::string> out_of_bounds{
        "What's the weather like in Chicago tomorrow?",
        "Can you make me some money?",
    };
    for (const auto& utterance : out_of_bounds) {
        calculator::assistant::ParseRequest req;
        req.set_utterance(utterance);
        calculator::assistant::ParseResponse res;
        const auto ctx = make_context(kAssistantDeadline);
        const auto status = stub.ParseStrategy(ctx.get(), req, &res);
        if (!status.ok()) {
            std::cerr << "ParseStrategy FAILED on \"" << utterance << "\": "
                      << status.error_code() << " " << status.error_message() << "\n";
            return false;
        }
        if (res.outcome_case() == calculator::assistant::ParseResponse::kParams) {
            std::cerr << "\"" << utterance << "\" produced StrategyParams (symbol="
                      << res.params().symbol() << ", strategy=" << res.params().strategy()
                      << ") -- this was never a well-specified trade request, and guessing one "
                      << "is exactly the fabrication real-data-only policy forbids\n";
            return false;
        }
        if (res.outcome_case() != calculator::assistant::ParseResponse::kClarification &&
            res.outcome_case() != calculator::assistant::ParseResponse::kRefusal) {
            std::cerr << "\"" << utterance << "\" produced neither params, clarification nor "
                      << "refusal -- the oneof contract was violated\n";
            return false;
        }
        std::cout << "  \"" << utterance << "\"  ->  correctly declined to guess ("
                  << (res.outcome_case() == calculator::assistant::ParseResponse::kClarification
                          ? "clarification"
                          : "refusal")
                  << ")\n";
    }

    return true;
}

/**
 * Quota enforcement, when a policy is configured.
 *
 * Skipped entirely when QUOTA_POLICY is unset, because the correct behaviour
 * then is that nothing changes -- and the rest of this gate already proves
 * that by passing.
 *
 * Needs a tier small enough to exhaust in a few calls. Run with something like:
 *
 *   QUOTA_POLICY='{"tiers":{"anonymous":{"requests_per_minute":5,
 *                                        "compute_units_per_hour":50}}}'
 */
auto check_quota(sensen::finance::Finance::Stub& stub) -> bool {
    const char* policy = std::getenv("QUOTA_POLICY");
    if (policy == nullptr || *policy == '\0') {
        std::cout << "Quota    not configured (QUOTA_POLICY unset) -- enforcement not exercised\n";
        return true;
    }

    const auto call = [&stub](const char* api_key) -> grpc::StatusCode {
        sensen::finance::PaymentRequest req;
        req.set_rate("0.005");
        req.set_periods(360);
        req.set_present_value("300000");
        sensen::finance::DecimalResponse res;
        auto ctx = make_anonymous_context();
        if (api_key != nullptr) ctx->AddMetadata("x-api-key", api_key);
        return stub.ComputePayment(ctx.get(), req, &res).error_code();
    };

    // Everything below probes the ANONYMOUS bucket, which requires unkeyed
    // calls to reach the quota at all. With FINANCE_REQUIRE_KEY=enforce they
    // are refused a layer earlier as UNAUTHENTICATED and never do -- so in the
    // configuration this service is meant to ship in (auth enforced AND quotas
    // configured) these assertions cannot hold, and failing here would report a
    // defect that is really just the two layers doing their jobs in order.
    //
    // Detected by probing rather than by reading FINANCE_REQUIRE_KEY, because
    // the variable describes the CLIENT's environment and the mode that matters
    // belongs to the server, which may be a different machine entirely.
    if (call(nullptr) == grpc::StatusCode::UNAUTHENTICATED) {
        std::cout << "Quota    anonymous bucket unreachable -- auth refuses unkeyed calls first "
                     "(this is the intended production posture); per-key limits are checked "
                     "separately\n";
        return true;
    }

    // Spend until refused. A tier that never refuses is not a limit, and a
    // bounded loop keeps a misconfigured (huge) tier from hanging the gate.
    int allowed = 0;
    bool refused = false;
    for (int i = 0; i < 200; ++i) {
        const auto code = call(nullptr);
        if (code == grpc::StatusCode::RESOURCE_EXHAUSTED) {
            refused = true;
            break;
        }
        if (code != grpc::StatusCode::OK) {
            std::cerr << "quota probe got an unexpected status " << static_cast<int>(code) << "\n";
            return false;
        }
        ++allowed;
    }
    if (!refused) {
        std::cerr << "quota never refused after " << allowed
                  << " calls -- the configured tier is not a limit\n";
        return false;
    }
    std::cout << "Quota    anonymous tier refused after " << allowed << " calls\n";

    // THE bypass check. Buckets keyed on the raw header would let a caller mint
    // a fresh allowance per request by sending a different random key each
    // time, which defeats the limit entirely. Unrecognised keys must land in
    // the same exhausted bucket as no key at all.
    for (int i = 0; i < 5; ++i) {
        const std::string key = "unregistered-" + std::to_string(i);
        if (call(key.c_str()) != grpc::StatusCode::RESOURCE_EXHAUSTED) {
            std::cerr << "an unregistered api key got a fresh bucket -- the quota is bypassable "
                      << "by sending a random x-api-key per request\n";
            return false;
        }
    }
    std::cout << "         unregistered keys share the anonymous bucket, not a private one\n";

    // A call priced above a whole hour's allowance must be refused outright
    // rather than handed a retry-after it can never satisfy.
    sensen::finance::MonteCarloRequest mc;
    mc.set_spot(100.0);
    mc.set_strike(100.0);
    mc.set_rate(0.05);
    mc.set_volatility(0.2);
    mc.set_years_to_expiry(1.0);
    mc.set_paths(10000000);
    mc.set_steps(10000);
    sensen::finance::DoubleResponse mres;
    auto mctx = make_anonymous_context();
    const auto mstatus = stub.PriceOptionMonteCarlo(mctx.get(), mc, &mres);
    if (mstatus.error_code() != grpc::StatusCode::RESOURCE_EXHAUSTED) {
        std::cerr << "a 10^11-operation Monte Carlo was admitted; cost is not being charged\n";
        return false;
    }
    std::cout << "         a 10^11-operation Monte Carlo is refused on cost, not on count\n";
    return true;
}

/**
 * Authentication, exercised against every way it is supposed to refuse.
 *
 * Checking that a valid key works proves almost nothing on its own -- an
 * "authentication" layer that admits everything also admits the valid key. What
 * makes this a gate is the refusals: each case below is a distinct way in, and
 * a regression in any one of them would leave the API open in a way the happy
 * path cannot reveal.
 *
 * Driven by environment so the gate carries no credentials of its own:
 *
 *   SMOKE_AUTH_PK      a valid publishable key
 *   SMOKE_AUTH_SK      a valid secret key
 *   SMOKE_AUTH_ORIGIN  an origin registered against SMOKE_AUTH_PK
 */
auto check_auth(sensen::finance::Finance::Stub& stub) -> bool {
    const char* pk = std::getenv("SMOKE_AUTH_PK");
    if (pk == nullptr || *pk == '\0') {
        std::cout << "Auth     not configured (SMOKE_AUTH_PK unset) -- not exercised\n";
        return true;
    }
    const char* sk = std::getenv("SMOKE_AUTH_SK");
    const char* origin = std::getenv("SMOKE_AUTH_ORIGIN");

    // ComputePayment is the cheapest RPC, so these calls test admission rather
    // than incidentally testing the quota's compute budget.
    const auto call = [&stub](const char* api_key, const char* org) -> grpc::StatusCode {
        sensen::finance::PaymentRequest req;
        req.set_rate("0.005");
        req.set_periods(360);
        req.set_present_value("300000");
        sensen::finance::DecimalResponse res;
        auto ctx = make_anonymous_context();
        if (api_key != nullptr) ctx->AddMetadata("x-api-key", api_key);
        if (org != nullptr) ctx->AddMetadata("origin", org);
        return stub.ComputePayment(ctx.get(), req, &res).error_code();
    };

    // If a valid key is refused, auth is either off or misconfigured, and every
    // refusal below would then "pass" for the wrong reason. Establish this
    // first so the rest of the checks mean what they claim.
    if (const auto code = call(pk, origin); code != grpc::StatusCode::OK) {
        std::cerr << "a valid publishable key from its registered origin was refused (status "
                  << static_cast<int>(code) << ") -- auth is misconfigured, so the refusal checks "
                  << "below would pass vacuously\n";
        return false;
    }
    std::cout << "Auth     a valid publishable key from its registered origin is admitted\n";

    struct Case {
        const char* what;
        const char* key;
        const char* origin;
        grpc::StatusCode expect;
    };
    // pk_live_ + 43 base64url characters: correctly SHAPED but never issued, so
    // it separates "rejected for looking wrong" from "rejected for being
    // unknown". Without it, a shape check alone would look like authentication.
    static constexpr const char* kWellFormedUnknown =
        "pk_live_AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";

    const std::vector<Case> cases{
        {"no key at all", nullptr, nullptr, grpc::StatusCode::UNAUTHENTICATED},
        {"a malformed key", "not-a-key", nullptr, grpc::StatusCode::UNAUTHENTICATED},
        {"a well-formed but unissued key", kWellFormedUnknown, nullptr,
         grpc::StatusCode::UNAUTHENTICATED},
    };

    for (const auto& c : cases) {
        if (const auto code = call(c.key, c.origin); code != c.expect) {
            std::cerr << "expected " << c.what << " to be refused with status "
                      << static_cast<int>(c.expect) << ", got " << static_cast<int>(code) << "\n";
            return false;
        }
        std::cout << "         " << c.what << " is refused\n";
    }

    // Origin binding is the whole reason a publishable key can be public. If a
    // key works from any site, it is just a password printed in the customer's
    // HTML.
    if (origin != nullptr && *origin != '\0') {
        if (const auto code = call(pk, "https://not-registered.example");
            code != grpc::StatusCode::PERMISSION_DENIED) {
            std::cerr << "a publishable key worked from an unregistered origin (status "
                      << static_cast<int>(code)
                      << ") -- origin binding is not enforced, so the key is usable by any site "
                         "that copies it out of the page\n";
            return false;
        }
        std::cout << "         a publishable key is refused from an unregistered origin\n";
    }

    if (sk != nullptr && *sk != '\0') {
        if (const auto code = call(sk, nullptr); code != grpc::StatusCode::OK) {
            std::cerr << "a valid secret key was refused server-side (status "
                      << static_cast<int>(code) << ")\n";
            return false;
        }
        std::cout << "         a valid secret key is admitted server-side\n";

        // A secret key arriving with an Origin header can only have got there
        // by being pasted into client-side code. It is refused even though the
        // key itself is valid, because serving it would normalise a leak.
        if (const auto code = call(sk, "https://anywhere.example");
            code != grpc::StatusCode::PERMISSION_DENIED) {
            std::cerr << "a secret key presented from a browser was accepted (status "
                      << static_cast<int>(code) << ") -- a leaked sk_ would keep working\n";
            return false;
        }
        std::cout << "         a secret key presented from a browser is refused as leaked\n";
    }

    return true;
}

/**
 * The limit written on an individual key, which is what a business is sold.
 *
 * This is the check that matters most commercially: the number in the contract
 * has to be the number the engine enforces. It asserts the ceiling is real
 * (calls stop being served) AND that it is the RIGHT ceiling (they stop at
 * roughly the figure on the key, not at some tier default the key was silently
 * falling back to). A limit that refuses at the wrong number is as much a
 * billing defect as one that never refuses at all.
 *
 * SMOKE_KEY_LIMIT_KEY is a key issued with a known --rpm; SMOKE_KEY_LIMIT_RPM
 * is that figure. Skipped when unset.
 */
auto check_key_limit(sensen::finance::Finance::Stub& stub) -> bool {
    const char* key = std::getenv("SMOKE_KEY_LIMIT_KEY");
    const char* rpm_s = std::getenv("SMOKE_KEY_LIMIT_RPM");
    if (key == nullptr || *key == '\0' || rpm_s == nullptr || *rpm_s == '\0') {
        std::cout << "Key cap  not configured (SMOKE_KEY_LIMIT_KEY unset) -- not exercised\n";
        return true;
    }
    const int expected = std::atoi(rpm_s);
    if (expected <= 0) {
        std::cerr << "SMOKE_KEY_LIMIT_RPM must be a positive number\n";
        return false;
    }

    const auto call = [&stub, key]() -> grpc::StatusCode {
        sensen::finance::PaymentRequest req;
        req.set_rate("0.005");
        req.set_periods(360);
        req.set_present_value("300000");
        sensen::finance::DecimalResponse res;
        auto ctx = make_anonymous_context();
        ctx->AddMetadata("x-api-key", key);
        return stub.ComputePayment(ctx.get(), req, &res).error_code();
    };

    // Bounded well above the expected figure: if the key is NOT being metered
    // by its own limit, this must terminate and report that, not spin.
    const int ceiling = expected * 4 + 20;
    int allowed = 0;
    bool refused = false;
    for (int i = 0; i < ceiling; ++i) {
        const auto code = call();
        if (code == grpc::StatusCode::RESOURCE_EXHAUSTED) {
            refused = true;
            break;
        }
        if (code != grpc::StatusCode::OK) {
            std::cerr << "per-key limit probe got an unexpected status "
                      << static_cast<int>(code) << " after " << allowed << " calls\n";
            return false;
        }
        ++allowed;
    }

    if (!refused) {
        std::cerr << "a key issued with " << expected << " requests/minute served " << allowed
                  << " calls without refusing -- its limit is not being applied\n";
        return false;
    }

    // The bucket refills continuously, so a run that takes a measurable slice of
    // a minute legitimately serves slightly more than the capacity. Allow that,
    // but not the order-of-magnitude difference that would mean a DIFFERENT
    // limit (a tier default) was in force.
    if (allowed < expected || allowed > expected * 2) {
        std::cerr << "a key issued with " << expected << " requests/minute refused after "
                  << allowed << " calls -- the enforced ceiling is not the one on the key\n";
        return false;
    }
    std::cout << "Key cap  a key issued with " << expected << " requests/minute refused after "
              << allowed << " calls\n";
    return true;
}

/**
 * The Pro gate, checked where it actually has to hold, in BOTH directions.
 *
 * The point of this check is that the gate is SERVER-SIDE. The frontend is a
 * static export, so any limit it applies runs on the user's own machine and the
 * gRPC endpoint answers curl regardless. This calls the RPC directly, exactly
 * as someone bypassing the UI would.
 *
 * A gate verified in one direction is not verified. "Anonymous is refused" is
 * satisfied just as well by a gate that refuses EVERYONE -- including the
 * subscriber who paid -- and that failure is strictly worse than the gate not
 * existing, because it is indistinguishable from the site being broken. So
 * three things are asserted, not one:
 *
 *   1. single-leg, anonymous            -> OK          (the free tier is intact)
 *   2. multi-leg, anonymous             -> PERMISSION_DENIED
 *   3. multi-leg, Pro credential        -> OK          (the paying customer gets in)
 *
 * Skipped unless PRO_GATE_MODE=enforce, because with the gate Off or Warn
 * "not refused" is the correct answer and asserting a refusal would fail for
 * the right reason at the wrong time.
 */
auto check_pro_gate(calculator::OptionsCalculator::Stub& stub, const std::string& symbol,
                    double spot) -> bool {
    const char* mode = std::getenv("PRO_GATE_MODE");
    const bool enforcing =
        mode != nullptr && (std::string{mode} == "enforce" || std::string{mode} == "2");
    if (!enforcing) {
        std::cout << "Pro gate not enforcing (PRO_GATE_MODE not 'enforce') -- not exercised\n";
        return true;
    }

    // The two ways a Pro entitlement can arrive, kept separate on purpose.
    //
    // SMOKE_PRO_LICENCE is a signed subscription licence, the path a customer
    // who pasted their key takes. SMOKE_PRO_BEARER is a Supabase access token,
    // the path a signed-in subscriber takes -- the engine reads the tier from
    // its `app_metadata.tier` claim. They are DIFFERENT code paths in
    // KeyRegistry::authenticate, so proving one says nothing about the other,
    // and whichever are supplied are exercised independently.
    const char* pro_licence = std::getenv("SMOKE_PRO_LICENCE");
    const char* pro_bearer = std::getenv("SMOKE_PRO_BEARER");
    const bool have_licence = pro_licence != nullptr && *pro_licence != '\0';
    const bool have_bearer = pro_bearer != nullptr && *pro_bearer != '\0';

    // Deliberately fatal rather than skipped. Reporting "the gate holds" having
    // only ever seen it refuse would be the most dangerous possible outcome of
    // this check: it is exactly the evidence someone would cite before turning
    // enforcement on, and it does not support that conclusion.
    if (!have_licence && !have_bearer) {
        std::cerr << "PRO_GATE_MODE=enforce but neither SMOKE_PRO_LICENCE nor SMOKE_PRO_BEARER "
                     "is set, so the ALLOW direction cannot be exercised. A gate proven only to "
                     "refuse is indistinguishable from one that refuses the paying customer too "
                     "-- refusing to report a pass on half the evidence\n";
        return false;
    }

    /**
     * One CalculateStrategy call.
     *
     * `credential` is empty for the anonymous case, and that case must be
     * genuinely anonymous: make_context() attaches SMOKE_API_KEY and
     * SMOKE_BEARER, so using it here would silently hand the "anonymous" call
     * whatever identity the environment happened to carry -- and a run with a
     * Pro token in the environment would then report the gate as broken when
     * it was working perfectly.
     */
    const auto call = [&](int legs, std::string_view header, std::string_view credential)
        -> grpc::StatusCode {
        calculator::StrategyRequest req;
        req.set_underlying_symbol(symbol);
        req.set_current_price(spot);
        req.set_implied_volatility(0.20);
        req.set_risk_free_rate(0.04);
        for (int i = 0; i < legs; ++i) {
            auto& leg = *req.add_legs();
            leg.set_action(i % 2 == 0 ? calculator::Leg::BUY : calculator::Leg::SELL);
            leg.set_type(i % 2 == 0 ? calculator::Leg::CALL : calculator::Leg::PUT);
            // Strikes spread around spot so the legs form a real structure
            // rather than four copies of the same contract.
            leg.set_strike(spot * (0.95 + 0.025 * static_cast<double>(i)));
            leg.set_expiration_days(30);
            leg.set_quantity(1);
            leg.set_premium(spot * 0.03);
            // Zero IV means "not quoted" and the engine refuses, so a missing
            // one here would look like the gate firing when it had not.
            leg.set_implied_volatility(0.20);
            leg.set_contract_multiplier(100.0);
        }
        calculator::StrategyResponse res;
        const auto ctx = make_anonymous_context();
        if (!header.empty()) ctx->AddMetadata(std::string{header}, std::string{credential});
        return stub.CalculateStrategy(ctx.get(), req, &res).error_code();
    };

    // --- Direction 1: the free tier still works, and works for a stranger. ---
    if (const auto code = call(1, "", ""); code != grpc::StatusCode::OK) {
        std::cerr << "a SINGLE-leg strategy was refused for an anonymous caller (status "
                  << static_cast<int>(code)
                  << ") -- the free tier is broken, which is worse than the gate not working\n";
        return false;
    }
    std::cout << "Pro gate single-leg call stays free for an anonymous caller\n";

    // --- Direction 2: multi-leg is actually refused without an entitlement. ---
    if (const auto code = call(4, "", ""); code != grpc::StatusCode::PERMISSION_DENIED) {
        std::cerr << "a 4-leg strategy was computed without a Pro entitlement (status "
                  << static_cast<int>(code)
                  << ") -- the gate is not enforced server-side, so hiding it in the UI would "
                     "protect nothing\n";
        return false;
    }
    std::cout << "         a 4-leg strategy is refused for an anonymous caller\n";

    // --- Direction 3: the customer who paid gets what they paid for. ---
    //
    // Checked per credential rather than as "at least one worked", because the
    // licence path and the JWT path fail for entirely different reasons -- a
    // LICENCE_SIGNING_KEY that disagrees with the billing worker's, versus a
    // SUPABASE_JWT_SECRET missing from the engine's environment -- and
    // collapsing them would hide whichever one is broken behind the one that
    // is not.
    if (have_licence) {
        if (const auto code = call(4, "x-api-key", pro_licence); code != grpc::StatusCode::OK) {
            std::cerr << "a 4-leg strategy was REFUSED (status " << static_cast<int>(code)
                      << ") to a caller holding a Pro subscription licence -- either the engine's "
                         "LICENCE_SIGNING_KEY does not match the one the billing worker minted "
                         "with, or the licence has expired. A subscriber is being denied what "
                         "they paid for\n";
            return false;
        }
        std::cout << "         a 4-leg strategy is ALLOWED with a Pro subscription licence\n";
    } else {
        std::cout << "         (SMOKE_PRO_LICENCE unset -- the licence path was not exercised)\n";
    }

    if (have_bearer) {
        const auto bearer = std::string{"Bearer "} + pro_bearer;
        if (const auto code = call(4, "authorization", bearer); code != grpc::StatusCode::OK) {
            std::cerr << "a 4-leg strategy was REFUSED (status " << static_cast<int>(code)
                      << ") to a signed-in caller whose access token should carry "
                         "app_metadata.tier=pro -- either SUPABASE_JWT_SECRET is missing or wrong "
                         "in the engine's environment, the token has expired, or the billing "
                         "webhook never wrote the tier onto the account\n";
            return false;
        }
        std::cout << "         a 4-leg strategy is ALLOWED for a signed-in Pro account\n";
    } else {
        std::cout << "         (SMOKE_PRO_BEARER unset -- the signed-in path was not exercised)\n";
    }

    return true;
}

}  // namespace

auto main(int argc, char** argv) -> int {
    const std::string target = (argc > 1) ? argv[1] : "localhost:50051";
    const std::string symbol = (argc > 2) ? argv[2] : "SPY";
    // Which suite to run: "all" (default), "finance" for the parts that need
    // no market data, or "llm" for the strategy assistant alone. The
    // calculator checks assert on LIVE quotes, so they legitimately fail
    // when the market is shut -- which would otherwise make it impossible to
    // verify authentication on a Sunday. "llm" gets its own suite for a
    // related but distinct reason: it is the one suite whose deadline needs
    // to be minutes rather than seconds (see kAssistantDeadline), so a
    // caller who only wants to verify the assistant should not have to pay
    // that longer timeout budget on every other check by running "all".
    const std::string suite = (argc > 3) ? argv[3] : "all";

    std::cout << "Smoke test against " << target << " for " << symbol << " [suite: " << suite
              << "]\n\n";

    // TLS when SMOKE_TLS is set, so this gate can be pointed at the deployed
    // service and not only at a local build. Verifying production is the only
    // way to exercise the parts that depend on credentials the repository does
    // not hold -- live market data among them -- and a gate that can only run
    // against localhost cannot answer "is the thing customers call working".
    const char* tls = std::getenv("SMOKE_TLS");
    const bool use_tls = tls != nullptr && *tls != '\0' && std::string_view{tls} != "0";
    auto channel = use_tls ? grpc::CreateChannel(target, grpc::SslCredentials({}))
                           : grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
    if (use_tls) std::cout << "Transport: TLS\n";

    if (suite == "pro") {
        // A supplied spot, not a fetched one: the gate refuses before any
        // pricing happens, so this suite must not depend on the market being
        // open to prove the gate holds.
        auto calc = calculator::OptionsCalculator::NewStub(channel);
        if (!check_pro_gate(*calc, symbol, 500.0)) return 1;
        std::cout << "\nThe Pro gate holds at the RPC, which is where it has to hold -- it "
                     "refuses the stranger AND admits the subscriber.\n";
        return 0;
    }

    if (suite == "finance") {
        auto finance_only = sensen::finance::Finance::NewStub(channel);
        if (!check_finance(*finance_only)) return 1;
        if (!check_auth(*finance_only)) return 1;
        if (!check_quota(*finance_only)) return 1;
        if (!check_key_limit(*finance_only)) return 1;
        std::cout << "\nThe financial library's answers satisfy their independent identities, "
                     "and authentication refuses every way in it is supposed to.\n";
        return 0;
    }

    if (suite == "llm") {
        auto assistant = calculator::assistant::StrategyAssistant::NewStub(channel);
        if (!check_assistant(*assistant)) return 1;
        std::cout << "\nThe strategy assistant either produced a well-formed, in-catalogue "
                     "parse, or honestly declined to guess.\n";
        return 0;
    }

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

    if (!check_term_structure(*stub)) return 1;

    if (!check_futures_quote(*stub)) return 1;

    // The sensen financial library, on the same channel and the same port --
    // a separate contract, not a separate service to reach.
    auto finance = sensen::finance::Finance::NewStub(channel);
    if (!check_finance(*finance)) return 1;

    // Before the quota check, because that one deliberately exhausts the
    // anonymous allowance -- an auth refusal afterwards could not be told apart
    // from a quota refusal.
    if (!check_auth(*finance)) return 1;

    // Last, because it deliberately exhausts an allowance -- anything after it
    // would be refused for reasons that have nothing to do with what it tests.
    if (!check_quota(*finance)) return 1;
    if (!check_key_limit(*finance)) return 1;

    // The strategy assistant, on the same channel and port again -- a THIRD
    // contract. Run last of all: it is optional (skips honestly when no
    // model is loaded) and its deadline is minutes rather than seconds, so
    // it should never sit ahead of checks that are neither in a normal "all"
    // run.
    auto assistant = calculator::assistant::StrategyAssistant::NewStub(channel);
    if (!check_assistant(*assistant)) return 1;

    std::cout << "\nCalculator RPCs returned live data; the financial library's answers "
                 "satisfy their independent identities; the strategy assistant either parsed "
                 "in-catalogue or honestly declined to guess.\n";
    return 0;
}
