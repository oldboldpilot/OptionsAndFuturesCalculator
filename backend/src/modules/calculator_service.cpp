module;
#include <algorithm>
#include <array>
#include <span>
#include <string_view>
#include <utility>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <expected>
#include <format>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <string>
#include <vector>

#include <cstdlib>

#include <grpcpp/grpcpp.h>
#include "calculator.pb.h"
#include "calculator.grpc.pb.h"
// Lossless StrategyRequest <-> JSON, so the stored document IS the wire
// contract rather than a second hand-written description of it.
#include <google/protobuf/util/json_util.h>

module calculator_service;

import sensen.options;
import logger;
import market_data;
import api_key;
import quota;
import strategy_store;

import sgee.builder.fluent;
import sgee.runtime.context;
import sgee.runtime.interpreter;
import sgee.runtime.action_registry;
import sgee.core.blueprint;
import sgee.core.types;

namespace options_calculator::service {

using grpc::ServerContext;
using grpc::Status;

namespace md = options_calculator::market_data;

// --------------------------------------------------------------------------
// Futures underlying resolution
// --------------------------------------------------------------------------

/**
 * How a futures root gets a price, when the quote feed only knows equities.
 *
 * Futures roots collide with listed equity tickers. "ES" is the E-mini S&P root
 * AND Eversource Energy's NYSE symbol; ask Alpaca for "ES" and it answers ~71,
 * which is a real quote for the wrong instrument. Arithmetic downstream is then
 * flawless and the answer is nonsense — the worst failure mode available,
 * because nothing looks broken.
 *
 * Where a documented, stable relationship to a tradeable proxy exists, use it
 * and carry the multiple. Where none does, REFUSE. A wrong level is worse than
 * an absent one: absence prompts a question, a wrong level gets traded on.
 *
 * This lives at file scope because BOTH the quote path and the term structure
 * need it. It previously existed only inside build_term_structure, which meant
 * the curve was index-level while the spot in the header — and the spot that
 * priced every leg — was the utility company's. One symbol, two instruments,
 * on the same screen.
 */
struct FuturesProxy {
    std::string_view root;
    std::string_view quote_symbol;
    double multiple;
};

static constexpr std::array<FuturesProxy, 2> kFuturesProxies{{
    {"ES", "SPY", 10.0},   // E-mini S&P 500 vs SPY, which tracks SPX/10
    {"NQ", "QQQ", 41.0},   // E-mini Nasdaq-100 vs QQQ, which tracks NDX/41
}};

[[nodiscard]] auto futures_proxy_for(std::string_view root) noexcept
    -> std::optional<FuturesProxy> {
    for (const auto& p : kFuturesProxies) {
        if (root == p.root) return p;
    }
    return std::nullopt;
}

/** The refusal, worded identically wherever a root has no priceable proxy. */
[[nodiscard]] auto no_futures_source(const std::string& symbol) -> Status {
    return Status(grpc::StatusCode::UNIMPLEMENTED,
                  "No futures quote source for " + symbol +
                      ". The root resolves to a listed equity of the same name, "
                      "which is a different instrument, so nothing is returned "
                      "rather than a price at the wrong level.");
}

// --------------------------------------------------------------------------
// Pricing helpers
// --------------------------------------------------------------------------

/** Contract multiplier, defaulting to the listed-equity-option convention. */
[[nodiscard]] auto multiplier_of(const calculator::Leg& leg) noexcept -> double {
    if (leg.contract_multiplier() > 0.0) return leg.contract_multiplier();
    return (leg.type() == calculator::Leg::CALL || leg.type() == calculator::Leg::PUT) ? 100.0 : 1.0;
}

[[nodiscard]] auto direction_of(const calculator::Leg& leg) noexcept -> double {
    return (leg.action() == calculator::Leg::BUY) ? 1.0 : -1.0;
}

[[nodiscard]] auto quantity_of(const calculator::Leg& leg) noexcept -> double {
    return (leg.quantity() > 0) ? static_cast<double>(leg.quantity()) : 1.0;
}

/** True when this leg carries an averaging style, i.e. it is an Asian option. */
[[nodiscard]] auto is_asian(const calculator::Leg& leg) noexcept -> bool {
    return leg.asian_type() != calculator::Leg::NOT_ASIAN;
}

/** Intrinsic value of a settled option leg, per share. */
[[nodiscard]] auto intrinsic_of(const calculator::Leg& leg, double price) noexcept -> double {
    return (leg.type() == calculator::Leg::CALL) ? std::max(0.0, price - leg.strike())
                                                 : std::max(0.0, leg.strike() - price);
}

/**
 * Black-Scholes-Merton: Black-Scholes with a continuous dividend yield `q`.
 *
 * sensen's price_black_scholes has no dividend term (options.cppm:550-591), and
 * this repo does not modify that submodule. It does not need to. Merton's model
 * is Black-Scholes evaluated at a discounted spot:
 *
 *     d1 = [ln(S/K) + (r - q + sigma^2/2)T] / (sigma*sqrt(T))
 *
 * and substituting S' = S*exp(-qT) into the plain Black-Scholes d1 gives
 * ln(S/K) - qT + (r + sigma^2/2)T, which is the same expression. So d1, d2 and
 * the option value come out exactly right with no change to the library — the
 * value needs no correction at all.
 *
 * The Greeks do, because they are derivatives with respect to S and T while the
 * substitution itself depends on both. Each factor below is derived, not
 * approximated, and every one collapses to 1 (or to zero correction) at q = 0,
 * so the dividend-free path is bit-for-bit what it was:
 *
 *   value  exact               V_M = V_B(S')
 *   delta  x exp(-qT)          dV/dS  = dV/dS' * dS'/dS
 *   gamma  x exp(-2qT)         chain rule applied twice
 *   vega   exact               vega_B(S') = S'*sqrt(T)*phi(d1) is already Merton's
 *   rho    exact               d2 is unchanged, and rho depends on d2 alone
 *   theta  + q*S'*delta_B      the dividend stream the holder does not receive
 *   vanna  x exp(-qT)          d(vega)/dS, one spot factor
 *   volga  exact               d(vega)/dsigma, no spot factor
 *   charm  see below
 *
 * charm is the one that has to be done carefully. sensen computes
 *     charm_B = -phi(d1) * (r/(sigma*sqrt(T)) - d2/(2T))
 * which is precisely Merton's charm at q = 0. Merton's general form replaces r
 * with (r - q) inside the bracket and adds a q*exp(-qT)*N(d1) term, so
 *     charm_M = exp(-qT) * [ charm_B(S') + q*delta_B(S') + q*phi(d1)/(sigma*sqrt(T)) ]
 * and phi(d1) is recovered as vega_B(S')/(S'*sqrt(T)). The same expression holds
 * for puts, because the put's delta carries the -1 that Merton's put charm needs.
 */
[[nodiscard]] auto price_with_dividend(double spot, double strike, double r, double q, double sigma,
                                       double years, sensen::OptionType type) noexcept
    -> sensen::BlackScholesResult {
    if (q == 0.0) return sensen::price_black_scholes(spot, strike, r, sigma, years, type);

    const double discount = std::exp(-q * years);
    const double adjusted_spot = spot * discount;
    auto bs = sensen::price_black_scholes(adjusted_spot, strike, r, sigma, years, type);

    // T <= 0 returns intrinsic with all Greeks zero; there is nothing to adjust,
    // and dividing by `years` below would not be defined.
    if (years <= 0.0) return bs;

    // phi(d1), recovered from vega before delta is overwritten below.
    const double pdf_d1 = (adjusted_spot > 0.0) ? bs.vega / (adjusted_spot * std::sqrt(years)) : 0.0;
    const double delta_b = bs.delta;

    bs.theta += q * adjusted_spot * delta_b;
    bs.charm = discount * (bs.charm + q * delta_b + q * pdf_d1 / (sigma * std::sqrt(years)));
    bs.delta = discount * delta_b;
    bs.gamma *= discount * discount;
    bs.vanna *= discount;
    // value, vega, rho and volga are already Merton's; see the table above.
    return bs;
}

/**
 * Position P&L at `days_elapsed` from now, for a given underlying price.
 *
 * Every leg is on its OWN clock. A leg with time left is re-priced by
 * Black-Scholes at its own remaining maturity; a leg whose expiry has already
 * passed is carried at intrinsic. Linear legs carry the move from their entry
 * and do not decay. Premium is the price actually paid or received, which the
 * client sends per leg.
 *
 * The single shared clock this replaced was correct only while every leg
 * expired on the same day. Given two expiries it priced the near leg with the
 * far leg's maturity, so a same-strike calendar spread had identical
 * `(S, K, sigma, T)` on both legs with opposite direction: the option values
 * cancelled exactly and the whole position collapsed to a flat line at the net
 * debit, at every price and every date. `max_profit == max_loss == -premium`
 * is the signature of that bug.
 *
 * One assumption is unavoidable and worth stating: past a leg's expiry we use
 * the underlying price at the EVALUATION date, not the (unknowable) price on
 * the day that leg actually settled. Every payoff diagram of this kind makes
 * the same single-path assumption — "the underlying arrives here and stays" —
 * and it is exactly why `curve_days_to_expiration` is reported: the further
 * past a settled leg you read, the more the picture leans on it.
 */
[[nodiscard]] auto value_at_elapsed(const calculator::StrategyRequest& req, double price,
                                    double days_elapsed, double r, double q, double sigma,
                                    double horizon) noexcept -> double {
    double total = 0.0;
    for (const auto& leg : req.legs()) {
        const double dir = direction_of(leg);
        const double mult = multiplier_of(leg);
        const double qty = quantity_of(leg);

        if (leg.type() != calculator::Leg::CALL && leg.type() != calculator::Leg::PUT) {
            // Linear legs: entry is the premium when supplied, otherwise the
            // strike field carries the entry level.
            const double entry = (leg.premium() > 0.0) ? leg.premium() : leg.strike();
            total += (price - entry) * dir * mult * qty;
            continue;
        }

        // A leg that arrives without an expiry falls back to the position
        // horizon — the same treatment action_greeks gives it — rather than to
        // zero, which would silently settle a live leg.
        const double leg_days = (leg.expiration_days() > 0.0) ? leg.expiration_days() : horizon;
        const double remaining = leg_days - days_elapsed;

        // Fall back to the quantity-weighted position IV, the same figure the
        // Greeks use. This previously fell back to the request-level IV while
        // the Greeks fell back to the position IV, so one position could be
        // priced two ways within a single response.
        const double iv = (leg.implied_volatility() > 0.0) ? leg.implied_volatility() : sigma;

        // Settled, or unpriceable for want of a volatility: carry it at
        // intrinsic. There is no time value to add without a sigma, and
        // intrinsic is the one figure that is right regardless.
        if (remaining <= 1e-9 || iv <= 0.0) {
            total += (intrinsic_of(leg, price) - leg.premium()) * dir * mult * qty;
            continue;
        }

        const auto type = (leg.type() == calculator::Leg::CALL) ? sensen::OptionType::Call
                                                                : sensen::OptionType::Put;
        const auto bs = price_with_dividend(price, leg.strike(), r, q, iv, remaining / 365.0, type);
        total += (bs.value - leg.premium()) * dir * mult * qty;
    }
    return total;
}

/** Longest-dated leg, in days. The horizon the structure is measured to. */
[[nodiscard]] auto horizon_days(const calculator::StrategyRequest& req) noexcept -> double {
    double days = 0.0;
    for (const auto& leg : req.legs()) {
        days = std::max(days, leg.expiration_days());
    }
    return days;
}

/**
 * Earliest-dated leg, in days: the date the payoff curve is drawn at.
 *
 * For a single-expiry structure this equals horizon_days() and nothing about
 * the curve changes. For a calendar spread it is the near expiry — the date
 * the structure has a shape worth plotting, rather than the far expiry where
 * both legs are intrinsic and the diagram is a flat line.
 *
 * Legs with no expiry (stock, futures) are skipped: they never settle, so they
 * cannot set the date, and a position of nothing but linear legs falls back to
 * the horizon.
 */
[[nodiscard]] auto curve_days(const calculator::StrategyRequest& req) noexcept -> double {
    double days = std::numeric_limits<double>::infinity();
    for (const auto& leg : req.legs()) {
        if (leg.expiration_days() > 0.0) days = std::min(days, leg.expiration_days());
    }
    return std::isfinite(days) ? days : horizon_days(req);
}

/** Quantity-weighted position IV, falling back to the request-level figure. */
[[nodiscard]] auto position_iv(const calculator::StrategyRequest& req) noexcept -> double {
    double weighted = 0.0;
    double weight = 0.0;
    for (const auto& leg : req.legs()) {
        if (leg.implied_volatility() > 0.0) {
            const double q = quantity_of(leg);
            weighted += leg.implied_volatility() * q;
            weight += q;
        }
    }
    if (weight > 0.0) return weighted / weight;
    return req.implied_volatility();
}

// --------------------------------------------------------------------------
// Probability
// --------------------------------------------------------------------------

/**
 * Terminal-price distribution under GBM, sampled on the price grid.
 *
 * Mirrors sensen::OptionStrategyBuilder::probability_of_profit: log(S_T) is
 * normal with mean log(S0) + (r - sigma^2/2)T and standard deviation
 * sigma*sqrt(T). The frontend draws the same density from the same inputs, so
 * the shaded region and these figures describe one distribution, not two.
 */
struct Distribution {
    std::vector<double> price;
    std::vector<double> density;  // Normalised so the weights sum to 1.
};

[[nodiscard]] auto lognormal_over(const std::vector<double>& grid, double spot, double sigma,
                                  double years, double r, double q) -> Distribution {
    Distribution dist;
    dist.price = grid;
    dist.density.assign(grid.size(), 0.0);
    if (grid.size() < 2 || spot <= 0.0 || sigma <= 0.0 || years <= 0.0) return dist;

    const double sd = sigma * std::sqrt(years);
    // Risk-neutral drift is (r - q): a dividend-paying underlying has a lower
    // forward, so the same option is worth less and PoP shifts with it. Using r
    // alone would shade a distribution the engine did not price against.
    const double mu = std::log(spot) + (r - q - 0.5 * sigma * sigma) * years;

    double total = 0.0;
    for (std::size_t i = 0; i < grid.size(); ++i) {
        const double x = grid[i];
        if (x <= 0.0) continue;
        const double z = (std::log(x) - mu) / sd;
        const double pdf = std::exp(-0.5 * z * z) / (x * sd * std::sqrt(2.0 * M_PI));
        dist.density[i] = pdf;
        total += pdf;
    }
    if (total > 0.0) {
        for (auto& d : dist.density) d /= total;
    }
    return dist;
}

struct RiskFigures {
    double pop{0.0};
    double expected_value{0.0};
    double var95{0.0};
    double var99{0.0};
    double cvar95{0.0};
    double cvar99{0.0};
    double probability_of_target{0.0};
};

/**
 * Probability-weighted risk figures.
 *
 * VaR and CVaR are read off the P&L distribution induced by the terminal
 * price distribution: sort outcomes by P&L, walk the cumulative probability
 * to the quantile, and average the tail beyond it for the conditional figure.
 */
[[nodiscard]] auto risk_figures(const Distribution& dist, const std::vector<double>& pnl,
                                double max_profit) -> RiskFigures {
    RiskFigures out;
    if (dist.density.empty() || pnl.size() != dist.density.size()) return out;

    const double target = max_profit * 0.5;
    for (std::size_t i = 0; i < pnl.size(); ++i) {
        const double w = dist.density[i];
        out.expected_value += pnl[i] * w;
        if (pnl[i] > 0.0) out.pop += w;
        if (max_profit > 0.0 && pnl[i] >= target) out.probability_of_target += w;
    }

    std::vector<std::size_t> order(pnl.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) { return pnl[a] < pnl[b]; });

    const auto quantile = [&](double alpha, double& var, double& cvar) {
        double cumulative = 0.0;
        double tail_pnl = 0.0;
        double tail_weight = 0.0;
        bool crossed = false;
        for (const auto idx : order) {
            const double w = dist.density[idx];
            if (!crossed) {
                tail_pnl += pnl[idx] * w;
                tail_weight += w;
            }
            cumulative += w;
            if (!crossed && cumulative >= alpha) {
                var = pnl[idx];
                crossed = true;
            }
        }
        cvar = (tail_weight > 0.0) ? tail_pnl / tail_weight : var;
    };

    quantile(0.05, out.var95, out.cvar95);
    quantile(0.01, out.var99, out.cvar99);
    return out;
}

// --------------------------------------------------------------------------
// Grid sizing
// --------------------------------------------------------------------------

/**
 * Upper bounds on the P&L matrix a caller can ask CalculateStrategy to build.
 *
 * The frontend never sets price_steps or date_steps -- every request that
 * reaches this service from the UI carries zero on both (see
 * useCalculatorStore.ts's buildStrategyRequest, which never calls
 * setPriceSteps/setDateSteps), and action_initialize below turns zero into
 * the defaults 81 and 12: a 972-cell matrix. A caller going directly at the
 * gRPC surface, unauthenticated, can instead put anything a uint32 holds on
 * either field. Every cell in the matrix reprices every leg
 * (action_matrix, action_expiry_curve), so the work is
 * O(price_steps * date_steps * legs) -- and until this guard existed, it ran
 * uncharged and unbounded: a single request could allocate a
 * hundred-megabyte response and burn the shared TBB pool for every other
 * caller behind it, including every other anonymous caller sharing the one
 * site-wide bucket quota.cpp documents.
 *
 * kMaxPriceSteps and kMaxDateSteps are each generous relative to what the UI
 * itself ever asks for, so a legitimate API consumer wanting a finer grid
 * than the UI's own resolution is not truncated to it. kMaxGridCells
 * additionally bounds the PRODUCT: a per-dimension clamp alone would still
 * wave through price_steps=20000, date_steps=1, which is exactly as
 * expensive as a 200x100 grid and touches neither individual cap.
 *
 * The request degrades to the maximum rather than being refused -- a
 * request this cheap to satisfy at the cap does not need a hard error, and
 * an over-large date_steps or price_steps is far more likely to be a caller
 * probing limits or integrating carelessly than an attack that a refusal
 * would meaningfully deter.
 */
inline constexpr std::uint32_t kMaxPriceSteps = 500;   // UI default: 81
inline constexpr std::uint32_t kMaxDateSteps = 180;    // UI default: 12
inline constexpr std::uint64_t kMaxGridCells = 20000;  // UI default: 972

/**
 * Upper bound on how many legs a single StrategyRequest may carry.
 *
 * legs_size() is not folded into kMaxGridCells above, because it multiplies a
 * different axis of the same work. That cap bounds price_steps * date_steps
 * -- the shape of the P&L matrix -- and says nothing about how many legs are
 * repriced in EVERY cell of it. cost_strategy_grid already multiplies its
 * price by legs directly (p * d * l / 1000), and so does the actual work in
 * action_expiry_curve, action_matrix and action_greeks, so an uncapped
 * legs_size() multiplies both the charge and the real cost by whatever a raw
 * gRPC caller puts in a repeated field -- unboundedly, since nothing here
 * reads it before building the response.
 *
 * The richest structure the UI's own catalogue offers is 4 legs (iron_condor,
 * condor, iron_butterfly, box_spread, reverse_iron_condor and
 * double_diagonal all tie at 4 -- see frontend/src/components/
 * StrategySelector.tsx's strategy list), so kMaxLegs is set at 5x that:
 * enough headroom for a legitimate custom structure built directly against
 * the API well beyond anything the catalogue ships (stacking two condors,
 * say), while keeping the legs multiplier on both the cost formula and the
 * per-cell work bounded to a small constant rather than to whatever a caller
 * chooses to send.
 *
 * Rejected outright rather than clamped -- deliberately UNLIKE price_steps
 * and date_steps above. Clamping a grid dimension changes the RESOLUTION of
 * an answer to the question that was actually asked: a coarser payoff curve
 * for the same position is still an answer about that position. Clamping
 * legs_size() would change the QUESTION -- silently dropping legs past the
 * cap prices a different structure than the one the caller specified, and
 * returns it with nothing in the response to say a leg was discarded. That is
 * exactly the failure action_initialize's own validation exists to prevent a
 * few lines below ("Cannot price this position" rather than defaulting a
 * missing spot or expiry): a wrong position confidently priced is worse than
 * a request refused, because nothing about a truncated response looks
 * incomplete.
 */
inline constexpr int kMaxLegs = 20;

struct GridSteps {
    std::uint32_t price_steps;
    std::uint32_t date_steps;
};

/**
 * Resolves the requested (possibly zero, possibly absurd) grid dimensions
 * into what the engine will actually build.
 *
 * Zero-substitution and clamping live in one place so CalculateStrategy can
 * price the call from the SAME numbers action_initialize below will use to
 * build it -- pricing an unclamped request would charge for work the engine
 * was never going to do.
 *
 * When both dimensions are within their own caps but the product still
 * exceeds kMaxGridCells, date_steps gives way rather than price_steps: the
 * expiry P&L curve -- the strategy's headline payoff diagram -- is drawn
 * from price_steps alone (action_expiry_curve), while date_steps only adds
 * depth to the matrix behind it. Shrinking the less load-bearing axis keeps
 * the primary payoff curve at full requested resolution.
 */
[[nodiscard]] auto resolve_grid_steps(std::uint32_t requested_price_steps,
                                      std::uint32_t requested_date_steps) noexcept -> GridSteps {
    std::uint32_t price_steps = requested_price_steps > 0 ? requested_price_steps : 81;
    std::uint32_t date_steps = requested_date_steps > 0 ? requested_date_steps : 12;

    price_steps = std::min(price_steps, kMaxPriceSteps);
    date_steps = std::min(date_steps, kMaxDateSteps);

    const std::uint64_t cells =
        static_cast<std::uint64_t>(price_steps) * static_cast<std::uint64_t>(date_steps);
    if (cells > kMaxGridCells) {
        date_steps = static_cast<std::uint32_t>(
            std::max<std::uint64_t>(1, kMaxGridCells / price_steps));
    }
    return {price_steps, date_steps};
}

// --------------------------------------------------------------------------
// SGEE execution context
// --------------------------------------------------------------------------

struct ComputeContext {
    calculator::StrategyRequest request;
    calculator::StrategyResponse response;
    grpc::Status status{grpc::Status::OK};

    double spot{0.0};
    double r{0.0};
    double q{0.0};      // Continuous dividend yield.
    double sigma{0.0};
    double horizon{0.0};    // Latest leg expiry: the date axis of the matrix.
    double curve{0.0};      // Earliest leg expiry: the date the payoff curve is drawn at.
    std::uint32_t date_steps{0};
    std::uint32_t price_steps{0};
    std::vector<double> price_grid;
    std::vector<double> expiry_pnl;
};
using Ctx = std::shared_ptr<ComputeContext>;
using ActionRegistry = sgee::runtime::ActionRegistry<Ctx>;

// --------------------------------------------------------------------------
// Graph actions
//
// These are registered by name in an ActionRegistry and invoked by the SGEE
// interpreter as each entity transitions through the state machine.
//
// They are free functions rather than lambdas passed to the builder because
// `sgee::Builder::Execute(F&&)` *discards the callable* — it only allocates a
// generated name ("lambda_action_N") for it. Actions run only when an
// ActionRegistry is handed to the Interpreter and the names match. The
// previous implementation passed lambdas and constructed the interpreter with
// no registry, so `action_registry_` was null, the guard in interpreter.cppm
// skipped every action, and the entire P&L computation was dead code: the
// graph walked its states and computed nothing.
//
// Control flow is linear for the same class of reason. The batch interpreter
// does not evaluate predicates on deterministic Branch nodes — it takes
// `node.branches[0]` unconditionally — so an OnTrue/OnFalse pair would always
// have taken the OnTrue edge regardless of the predicate. Validation therefore
// happens inside the actions, and each one returns early if an earlier stage
// already failed.
// --------------------------------------------------------------------------

using sgee::ExecutionResult;

/**
 * The five action names the OptionsWorkflow graph defines, in graph order.
 * Shared between the production constructor (which always binds all five)
 * and the test-only constructor overload below (which binds a caller-chosen
 * subset) so the two paths cannot drift apart into two different ideas of
 * "the full set".
 */
inline constexpr std::array<std::string_view, 5> kAllActionNames{
    "Initialize", "ComputeExpiryCurve", "ComputeMatrix", "ComputeGreeks",
    "ComputeProbabilities"};

[[nodiscard]] auto action_initialize(Ctx& ctx) -> ExecutionResult<> {
    ctx->spot = ctx->request.current_price();
    ctx->r = ctx->request.risk_free_rate();
    ctx->q = ctx->request.dividend_yield();
    ctx->sigma = position_iv(ctx->request);
    ctx->horizon = horizon_days(ctx->request);
    ctx->curve = curve_days(ctx->request);

    // Refuse rather than answer a question we were not asked. Each of these
    // was previously defaulted server-side, so the engine always produced a
    // confident answer to a fabricated question.
    std::string why;
    if (ctx->request.legs_size() == 0) why += " no legs;";
    if (ctx->spot <= 0.0) why += " no spot price;";
    if (ctx->horizon <= 0.0) why += " no expiration on any leg;";
    if (!why.empty()) {
        ctx->status = grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                                   "Cannot price this position:" + why);
        return std::unexpected(sgee::ExecutionError::ActionFailed);
    }

    /*
     * An Asian leg is refused here, as a WHOLE-RESPONSE refusal with its own
     * status code, and both halves of that are deliberate.
     *
     * WHY REFUSED AT ALL. Every figure this service produces past this point
     * is a function of TERMINAL SPOT: value_at_elapsed() walks the price grid,
     * and max_profit, max_loss, the breakevens, the matrix, the surface, POP
     * and the VaR figures are all read off it. An average-price Asian pays
     * max(A - K, 0) on the realized AVERAGE, and Var(A) < Var(S_T) for the same
     * sigma -- a different random variable, not a nearby one. Walking an Asian
     * leg across the spot grid does not approximate its payoff; it prices a
     * vanilla and labels it Asian.
     *
     * WHY THE WHOLE RESPONSE, rather than filling in the fields we CAN compute
     * and leaving the rest out. proto3 has no absent: an unset max_profit is
     * indistinguishable on the wire from a genuine 0.0, so a partial response
     * would render "max profit $0" to anyone who did not know to check a note
     * field first. This repository has already shipped that exact failure once,
     * when a missing expiry lookup became `?? 0` and four analytics panels
     * priced a fabricated same-day expiry. A refusal cannot be misread.
     *
     * WHY FAILED_PRECONDITION specifically. It is a MODELLING limit, not bad
     * input -- the request is well formed and the instrument is real. The
     * client discriminates on the CODE, never the text, exactly as the Pro gate
     * does with PERMISSION_DENIED (7): this message has already been reworded
     * once and text-matching would break silently the next time.
     *
     * The averaging bounds are checked FIRST so a malformed Asian is named as
     * malformed rather than as unsupported.
     */
    for (int i = 0; i < ctx->request.legs_size(); ++i) {
        const auto& leg = ctx->request.legs(i);
        if (!is_asian(leg)) continue;

        // averaging_states < 2 is a CRASH, not a bad answer: it discretizes a
        // continuous running-average range into that many grid states, and at
        // 1 the grid has no width -- price_option_double then subscripts a
        // std::vector<double> out of bounds. finance_service refuses the same
        // value at its own boundary for the same reason; this leg never
        // reaches a pricer, but the bound is enforced here so the field cannot
        // become a live crash the day Asian legs are priced.
        const std::int32_t states = leg.averaging_states();
        if (states != 0 && (states < 2 || states > 200)) {
            ctx->status = grpc::Status(
                grpc::StatusCode::INVALID_ARGUMENT,
                "Leg " + std::to_string(i) + ": averaging_states must be 0 (engine default) "
                "or between 2 and 200.");
            return std::unexpected(sgee::ExecutionError::ActionFailed);
        }

        ctx->status = grpc::Status(
            grpc::StatusCode::FAILED_PRECONDITION,
            "This position contains an Asian option, which pays on the average price over its "
            "averaging window rather than the price at expiry. The payoff, P&L and probability "
            "views are all drawn against the price at expiry, so they cannot describe it. "
            "Price an Asian contract on its own in the Exercise & Averaging panel.");
        return std::unexpected(sgee::ExecutionError::ActionFailed);
    }

    const auto grid = resolve_grid_steps(ctx->request.price_steps(), ctx->request.date_steps());
    ctx->price_steps = grid.price_steps;
    ctx->date_steps = grid.date_steps;

    const double range =
        ctx->request.price_range_percent() > 0.0 ? ctx->request.price_range_percent() : 0.25;
    const double lo = std::max(0.01, ctx->spot * (1.0 - range));
    const double hi = ctx->spot * (1.0 + range);
    const double step =
        (hi - lo) / static_cast<double>(ctx->price_steps > 1 ? ctx->price_steps - 1 : 1);

    ctx->price_grid.clear();
    ctx->price_grid.reserve(ctx->price_steps);
    for (std::uint32_t i = 0; i < ctx->price_steps; ++i) {
        ctx->price_grid.push_back(lo + step * static_cast<double>(i));
    }
    return {};
}

[[nodiscard]] auto action_expiry_curve(Ctx& ctx) -> ExecutionResult<> {
    if (!ctx->status.ok()) return {};
    const auto start = std::chrono::high_resolution_clock::now();

    ctx->expiry_pnl.clear();
    ctx->expiry_pnl.reserve(ctx->price_grid.size());

    double max_profit = -std::numeric_limits<double>::infinity();
    double max_loss = std::numeric_limits<double>::infinity();

    // Drawn at the EARLIEST leg expiry, not the latest. See curve_days().
    ctx->response.set_curve_days_to_expiration(ctx->curve);

    for (const double price : ctx->price_grid) {
        const double pnl =
            value_at_elapsed(ctx->request, price, ctx->curve, ctx->r, ctx->q, ctx->sigma, ctx->horizon);
        ctx->expiry_pnl.push_back(pnl);
        max_profit = std::max(max_profit, pnl);
        max_loss = std::min(max_loss, pnl);

        auto& point = *ctx->response.add_pnl_matrix();
        point.set_underlying_price(price);
        point.set_pnl(pnl);
    }

    ctx->response.set_max_profit(max_profit);
    ctx->response.set_max_loss(max_loss);
    if (max_loss < 0.0) {
        ctx->response.set_risk_reward_ratio(std::abs(max_profit / max_loss));
    }

    // Every sign change is a break-even. A condor has two; the single
    // `break_even` field could only ever describe one of them.
    for (std::size_t i = 1; i < ctx->expiry_pnl.size(); ++i) {
        const double a = ctx->expiry_pnl[i - 1];
        const double b = ctx->expiry_pnl[i];
        if (a == 0.0 || a * b < 0.0) {
            const double t = std::abs(a) / (std::abs(a) + std::abs(b));
            ctx->response.add_breakeven_prices(
                ctx->price_grid[i - 1] + t * (ctx->price_grid[i] - ctx->price_grid[i - 1]));
        }
    }
    if (ctx->response.breakeven_prices_size() > 0) {
        ctx->response.set_break_even(ctx->response.breakeven_prices(0));
    }

    const auto end = std::chrono::high_resolution_clock::now();
    ctx->response.set_calculation_time_microseconds(static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(end - start).count()));
    return {};
}

[[nodiscard]] auto action_matrix(Ctx& ctx) -> ExecutionResult<> {
    if (!ctx->status.ok()) return {};

    const auto now = std::chrono::system_clock::now();
    const double risk = std::abs(ctx->response.max_loss());

    for (std::uint32_t d = 0; d < ctx->date_steps; ++d) {
        const double frac = (ctx->date_steps > 1)
                                ? static_cast<double>(d) / static_cast<double>(ctx->date_steps - 1)
                                : 1.0;
        // The axis runs from today (d = 0, full time remaining) to the latest
        // leg's expiry. `dte` counts down; `elapsed` counts up.
        const double dte = ctx->horizon * (1.0 - frac);
        const double elapsed = std::max(0.0, ctx->horizon - dte);

        // Previously `now + dte`, which ran the calendar column BACKWARDS
        // against the days-to-expiration column beside it: the first cell was
        // labelled "today's DTE" and dated at the expiry, the last was
        // labelled "0 DTE" and dated today. Every date in the grid named the
        // wrong day except the midpoint.
        const auto day = std::chrono::floor<std::chrono::days>(now) +
                         std::chrono::days{static_cast<int>(std::llround(elapsed))};
        const std::chrono::year_month_day ymd{day};

        // std::format, not a stream, and deliberately so. A default-constructed
        // ostringstream carries the GLOBAL locale, and logger's initialisation
        // sets that to en_US.UTF-8 (cpp23-logger/logger.cppm:2007) for the sake
        // of UTF-8 console output. That locale groups thousands, so the year
        // came out as "2,026" and every cell in the grid carried a date string
        // no client could parse — the frontend reads this field straight into
        // its date axis. std::format is locale-independent unless you ask it
        // otherwise, which is the property this field needs: it is a wire
        // value in ISO-8601, not display text.
        const auto date_text = std::format("{:04}-{:02}-{:02}", static_cast<int>(ymd.year()),
                                           static_cast<unsigned>(ymd.month()),
                                           static_cast<unsigned>(ymd.day()));

        for (const double price : ctx->price_grid) {
            const double pnl =
                value_at_elapsed(ctx->request, price, elapsed, ctx->r, ctx->q, ctx->sigma, ctx->horizon);
            auto& cell = *ctx->response.add_matrix();
            cell.set_price(price);
            cell.set_days_to_expiration(static_cast<std::uint32_t>(std::llround(dte)));
            cell.set_date_str(date_text);
            cell.set_pnl_dollars(pnl);
            if (risk > 1e-9) {
                cell.set_return_on_risk_percent(pnl / risk * 100.0);
            }
        }
    }
    return {};
}

[[nodiscard]] auto action_greeks(Ctx& ctx) -> ExecutionResult<> {
    if (!ctx->status.ok()) return {};

    double delta = 0.0, gamma = 0.0, theta = 0.0, vega = 0.0, rho = 0.0;
    double vanna = 0.0, volga = 0.0, charm = 0.0;

    // Per-leg risk is accumulated in the same pass and from the same numbers as
    // the aggregate, rather than recomputed afterwards. Two passes would be two
    // chances to diverge, and the guarantee that matters to a reader is that the
    // legs sum to the net exactly.
    int leg_index = -1;
    for (const auto& leg : ctx->request.legs()) {
        ++leg_index;
        const double dir = direction_of(leg);
        const double mult = multiplier_of(leg);
        const double qty = quantity_of(leg);

        if (leg.type() == calculator::Leg::CALL || leg.type() == calculator::Leg::PUT) {
            const double iv = leg.implied_volatility() > 0.0 ? leg.implied_volatility() : ctx->sigma;
            if (iv <= 0.0) continue;

            // Every leg used to be priced at the horizon of the LONGEST-dated
            // leg (ctx->horizon, set in action_initialize from horizon_days(),
            // which is a max over the legs), so a calendar spread's near leg
            // was priced with the far leg's time to expiry and its delta,
            // gamma and theta were simply wrong. Each leg carries its own
            // expiration_days; use it. A leg that arrives without one falls
            // back to the position horizon — what it was already getting —
            // rather than to zero, because sensen returns all-zero Greeks for
            // T <= 0 (options.cppm:558) and the leg would silently vanish out
            // of the aggregate instead of being visibly absent.
            const double leg_days =
                (leg.expiration_days() > 0.0) ? leg.expiration_days() : ctx->horizon;
            const double years = leg_days / 365.0;

            const auto type = (leg.type() == calculator::Leg::CALL) ? sensen::OptionType::Call
                                                                   : sensen::OptionType::Put;
            const auto bs =
                price_with_dividend(ctx->spot, leg.strike(), ctx->r, ctx->q, iv, years, type);
            const double scale = dir * mult * qty;

            // Unit conversion belongs here, at the presentation boundary, not
            // in sensen. sensen returns textbook Black-Scholes derivatives:
            // theta and charm are per YEAR because T is in years
            // (options.cppm:582), and vega, vanna, volga and rho are per 1.00
            // of vol or rate (options.cppm:568, 583) — there is no /365 and no
            // /100 anywhere in the library, which is correct for a maths
            // library and is left alone. A trader, however, reads theta as
            // dollars per calendar day and vega as dollars per one IV point,
            // so a per-year theta scaled by the 100x contract multiplier
            // rendered as -21251 for a spread whose entire risk was $861: the
            // right number in a unit nobody reads it in. The proto documents
            // these units so the next client inherits the convention instead
            // of the trap.
            delta += bs.delta * scale;                // position share-equivalents
            gamma += bs.gamma * scale;                // change in delta per $1 of spot
            theta += (bs.theta / 365.0) * scale;      // $ per calendar day
            vega  += (bs.vega / 100.0) * scale;       // $ per 1 IV point
            rho   += (bs.rho / 100.0) * scale;        // $ per 1 rate point
            // Second order, converted to match so the whole message is one
            // system of units: vanna is d(delta)/d(vol) so it carries one vol
            // factor, volga is d(vega)/d(vol) so it carries two, and charm is
            // d(delta)/d(time) so it converts on the day count.
            vanna += (bs.vanna / 100.0) * scale;
            volga += (bs.volga / 10000.0) * scale;
            charm += (bs.charm / 365.0) * scale;

            auto& lr = *ctx->response.add_leg_risk();
            lr.set_leg_index(leg_index);
            lr.set_model_price(bs.value);
            lr.set_open_pnl((bs.value - leg.premium()) * scale);
            auto& lg = *lr.mutable_greeks();
            lg.set_delta(bs.delta * scale);
            lg.set_gamma(bs.gamma * scale);
            lg.set_theta((bs.theta / 365.0) * scale);
            lg.set_vega((bs.vega / 100.0) * scale);
            lg.set_rho((bs.rho / 100.0) * scale);
            lg.set_vanna((bs.vanna / 100.0) * scale);
            lg.set_volga((bs.volga / 10000.0) * scale);
            lg.set_charm((bs.charm / 365.0) * scale);
        } else {
            // A linear leg is one delta per unit and nothing else: no gamma, no
            // decay, no vol sensitivity. Its P&L against entry is spot minus
            // entry, which is the same convention value_at_elapsed uses.
            const double scale = dir * mult * qty;
            delta += scale;

            const double entry = (leg.premium() > 0.0) ? leg.premium() : leg.strike();
            auto& lr = *ctx->response.add_leg_risk();
            lr.set_leg_index(leg_index);
            lr.set_model_price(0.0);
            lr.set_open_pnl((ctx->spot - entry) * scale);
            lr.mutable_greeks()->set_delta(scale);
        }
    }

    auto& g = *ctx->response.mutable_net_greeks();
    g.set_delta(delta);
    g.set_gamma(gamma);
    g.set_theta(theta);
    g.set_vega(vega);
    g.set_rho(rho);
    g.set_vanna(vanna);
    g.set_volga(volga);
    g.set_charm(charm);
    return {};
}

[[nodiscard]] auto action_probabilities(Ctx& ctx) -> ExecutionResult<> {
    if (!ctx->status.ok()) return {};

    // Without a volatility there is no distribution. The probability fields
    // stay at zero rather than being filled from a default vol, which the
    // caller could not tell apart from a real answer.
    if (ctx->sigma <= 0.0 || ctx->curve <= 0.0) {
        logger::Logger::getInstance().warn(
            "No implied volatility on the position; probability metrics omitted");
        return {};
    }

    // Over ctx->curve, not ctx->horizon: risk_figures pairs each density with
    // the P&L at the same grid point, and ctx->expiry_pnl is now evaluated at
    // the curve date. A distribution on a different clock would weight the
    // right P&Ls by the wrong probabilities.
    const auto dist =
        lognormal_over(ctx->price_grid, ctx->spot, ctx->sigma, ctx->curve / 365.0, ctx->r, ctx->q);
    const auto figures = risk_figures(dist, ctx->expiry_pnl, ctx->response.max_profit());

    ctx->response.set_pop(figures.pop);
    ctx->response.set_expected_value(figures.expected_value);
    ctx->response.set_probability_of_target_profit(figures.probability_of_target);

    auto& rm = *ctx->response.mutable_risk_metrics();
    rm.set_var_parametric_95(figures.var95);
    rm.set_var_parametric_99(figures.var99);
    rm.set_cvar_parametric_95(figures.cvar95);
    rm.set_cvar_parametric_99(figures.cvar99);

    // Probability of touching the nearest break-even at any point before
    // expiry, via the reflection principle.
    if (ctx->response.breakeven_prices_size() > 0) {
        double nearest = ctx->response.breakeven_prices(0);
        for (const double be : ctx->response.breakeven_prices()) {
            if (std::abs(be - ctx->spot) < std::abs(nearest - ctx->spot)) nearest = be;
        }
        const double sd = ctx->sigma * std::sqrt(ctx->curve / 365.0);
        if (sd > 0.0 && nearest > 0.0) {
            const double z = std::abs(std::log(nearest / ctx->spot)) / sd;
            const double touch = 2.0 * (1.0 - 0.5 * (1.0 + std::erf(z / std::sqrt(2.0))));
            ctx->response.set_probability_of_touch(std::min(1.0, touch));
        }
    }
    return {};
}

// --------------------------------------------------------------------------
// Quota admission for the market-data RPCs
//
// GetMarketQuote, GetRiskFreeRate and GetMarketChain each reach a live
// external feed through the ONE shared market-data connection -- see
// market_data.cppm's client_for(), one keep-alive httplib::Client per
// (thread, host) -- and until now none of them charged anything at all: no
// admit_identity call, unlike every RPC in finance_service.cpp and unlike
// CalculateStrategy below. An unmetered caller could hammer that shared
// connection past the vendor's own upstream rate limit and degrade market
// data for every other caller of this service, including callers correctly
// staying inside their own compute budget -- anonymous callers all share one
// site-wide bucket (quota.cppm's module comment), so there is no per-caller
// isolation to fall back on.
//
// This reuses CalculateStrategy's own authenticate-then-charge shape exactly:
// KeyRegistry::instance().authenticate() resolves an Identity (falling back
// to the unauthenticated identity when no key is configured, which is what
// makes this a no-op today with FINANCE_REQUIRE_KEY/PRO_GATE_MODE unset, same
// as CalculateStrategy), then QuotaEnforcer::instance().admit_identity() with
// the same TierLimits construction. Factored into one function because three
// RPCs need the identical block and a third hand-copied instance is exactly
// how a fourth new market-data RPC ends up forgetting it -- not because the
// shape itself is anything other than what CalculateStrategy already does
// inline, which is left untouched.
[[nodiscard]] auto authorize_and_charge(ServerContext* context, std::string_view method,
                                        double compute_units,
                                        ::options_calculator::auth::Identity& identity) -> Status {
    if (context != nullptr) {
        if (auto s = ::options_calculator::auth::KeyRegistry::instance().authenticate(
                *context, "calculator", method, identity);
            !s.ok()) {
            return s;
        }
    }
    ::options_calculator::quota::TierLimits limits{identity.requests_per_minute,
                                                   identity.compute_units_per_hour};
    return ::options_calculator::quota::QuotaEnforcer::instance().admit_identity(
        identity.id, identity.tier, method, compute_units,
        identity.has_limits ? &limits : nullptr);
}

class CalculatorServiceImpl final : public calculator::OptionsCalculator::Service {
private:
    std::shared_ptr<ActionRegistry> actions_;
    std::shared_ptr<const sgee::GraphBlueprint> graph_;
    // The graph's terminal node id, resolved once at construction rather than
    // looked up by name on every RPC -- see the postcondition in
    // CalculateStrategy below, which is on the hot path and must stay cheap.
    std::uint16_t done_node_id_{0};

    /**
     * Saved-scenario storage, or null when this deployment has none.
     *
     * Null is a SUPPORTED configuration, not a broken one -- a local build with
     * no DATABASE_URL gets it, and every other RPC in this service is
     * unaffected. The three saved-scenario RPCs answer FAILED_PRECONDITION;
     * they never crash and never silently pretend to have saved something.
     */
    std::shared_ptr<::options_calculator::store::IStrategyStore> store_;

public:
    /**
     * `bound_action_names` defaults to every action the graph defines, which
     * is the only thing production ever constructs (see
     * RegisterCalculatorService at the bottom of this file). The parameter
     * exists so RegisterCalculatorServiceForTest can build a graph with a
     * DELIBERATELY incomplete registry -- reproducing the exact silent-halt
     * failure mode described in the block comment above (a null registry, an
     * action that fails without setting ctx->status, or an unregistered
     * action id) so the postconditions below can be proven to catch it.
     */
    explicit CalculatorServiceImpl(
        std::span<const std::string_view> bound_action_names = kAllActionNames,
        std::shared_ptr<::options_calculator::store::IStrategyStore> store = nullptr)
        : actions_{std::make_shared<ActionRegistry>()}, store_{std::move(store)} {
        auto& log = logger::Logger::getInstance();

        auto graph_result = sgee::Builder<Ctx>("OptionsWorkflow")
            .Node("Initialize")
                .Execute("Initialize")
                .Next("ComputeExpiryCurve")
            .Node("ComputeExpiryCurve")
                .Execute("ComputeExpiryCurve")
                .Next("ComputeMatrix")
            .Node("ComputeMatrix")
                .Execute("ComputeMatrix")
                .Next("ComputeGreeks")
            .Node("ComputeGreeks")
                .Execute("ComputeGreeks")
                .Next("ComputeProbabilities")
            .Node("ComputeProbabilities")
                .Execute("ComputeProbabilities")
                .Next("Done")
            .Node("Done")
                .IsTerminal()
            .Build();

        if (!graph_result) {
            log.error("Failed to build SGEE graph: {}", graph_result.error());
            return;
        }
        graph_ = graph_result.value();
        done_node_id_ = graph_->GetNodeId("Done");

        /*
         * Bind each action by the ID the builder assigned, not by name.
         *
         * ActionRegistry::Register hashes the name (`hash(name) % 10000`)
         * while Builder::Execute assigns sequential IDs from its own name
         * table. Those two numbering schemes never agree, so registering by
         * name compiles and runs but every lookup in the interpreter misses
         * and the action silently does not execute. GetActionId() is the only
         * thing that bridges them.
         */
        const std::array<std::pair<std::string_view, ActionRegistry::ActionFunction>, 5> bindings{{
            {"Initialize", action_initialize},
            {"ComputeExpiryCurve", action_expiry_curve},
            {"ComputeMatrix", action_matrix},
            {"ComputeGreeks", action_greeks},
            {"ComputeProbabilities", action_probabilities},
        }};

        const auto is_bound = [&](std::string_view name) {
            return std::ranges::find(bound_action_names, name) != bound_action_names.end();
        };

        for (const auto& [name, fn] : bindings) {
            if (!is_bound(name)) {
                // Test-only path: bound_action_names deliberately omitted
                // this one. Production never reaches this branch -- the
                // default argument is the full set, so is_bound is true for
                // every name calculator_engine ever constructs with.
                log.warn("Action '{}' intentionally left unbound (test configuration); "
                         "any entity reaching its node will halt without a status",
                         name);
                continue;
            }
            const auto id = graph_->GetActionId(name);
            if (!id) {
                log.error("Action '{}' is not present in the graph; refusing to start", name);
                graph_.reset();
                return;
            }
            actions_->RegisterById(*id, fn);
        }

        // No pipeline, no worker pool. CalculatorServiceImpl extends gRPC's
        // SYNCHRONOUS service base and the server is built with no callback
        // API and no SetSyncServerOption, so gRPC already gives every
        // in-flight RPC its own thread -- computing the graph inline on that
        // thread is already parallel across requests. The 16-worker pool this
        // replaced computed nothing (TransformedPipeline::push() runs the
        // interpreter INLINE before the context is ever enqueued; the workers
        // only relayed a std::promise it had already fulfilled) and cost real
        // latency getting there: nothing notifies a worker on enqueue, so
        // pickup relied on a 1ms poll, roughly doubling the ~0.25ms the
        // UI-default grid takes to compute.
        log.info("SGEE graph initialized: {} registered actions; execution runs inline on the "
                 "gRPC request thread",
                 bindings.size());
    }

    ~CalculatorServiceImpl() override = default;

    /*
     * The three RPC signatures below are fixed by the protoc-generated base
     * class and cannot be changed: the parameters must match the virtual
     * exactly or the override does not bind. They are non-owning views onto
     * memory gRPC owns and reuses across calls, so wrapping them in a smart
     * pointer would introduce a double free rather than satisfy the policy.
     *
     * The rule is honoured by not letting them travel: each is null-checked
     * and bound to a reference on the first line, and no raw pointer appears
     * anywhere in the bodies. See the deviation register in the spec.
     */
    auto CalculateStrategy(ServerContext* context, const calculator::StrategyRequest* request,
                           calculator::StrategyResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        const auto& req = *request;
        auto& res = *response;

        auto& log = logger::Logger::getInstance();
        log.info("CalculateStrategy: {} with {} legs", req.underlying_symbol(), req.legs_size());

        // Refused outright, before authentication or quota even run -- an
        // invalid request is invalid regardless of who is asking, and this
        // check needs no identity to decide. See kMaxLegs above for why this
        // is a rejection rather than the clamp price_steps/date_steps get
        // below.
        if (req.legs_size() > kMaxLegs) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "Too many legs (" + std::to_string(req.legs_size()) +
                              "); this service prices at most " + std::to_string(kMaxLegs) +
                              " legs in one request");
        }

        // The Pro gate, server-side and before any work is done.
        //
        // It has to be here rather than in the UI: the frontend is a static
        // export, so a check there runs on the user's own machine, and this
        // endpoint answers curl. Gating in the UI would hide the button, not
        // the feature.
        //
        // Identity is resolved through the key registry, which returns an
        // unauthenticated identity when no key is configured -- so with
        // PRO_GATE_MODE unset this whole block is inert and every strategy
        // stays free, which is the behaviour that exists today.
        ::options_calculator::auth::Identity identity;
        if (context != nullptr) {
            if (auto s = ::options_calculator::auth::KeyRegistry::instance().authenticate(
                    *context, "calculator", "CalculateStrategy", identity);
                !s.ok()) {
                return s;
            }
        }
        if (auto s = ::options_calculator::auth::check_strategy_entitlement(identity,
                                                                           req.legs_size());
            !s.ok()) {
            return s;
        }

        // Charged from the CLAMPED grid, not the requested one: the point of
        // resolve_grid_steps is that the engine never builds more than this,
        // so pricing the raw request would charge for work that was never
        // going to happen. Priced after the Pro gate and before the request
        // reaches the execution queue, matching the finance and assistant
        // services' own admission order -- a call refused above never
        // touches quota; a call that would exceed it is stopped here rather
        // than after paying for the SGEE pipeline's own work.
        const auto grid = resolve_grid_steps(req.price_steps(), req.date_steps());
        ::options_calculator::quota::TierLimits strategy_limits{identity.requests_per_minute,
                                                                 identity.compute_units_per_hour};
        if (auto s = ::options_calculator::quota::QuotaEnforcer::instance().admit_identity(
                identity.id, identity.tier, "CalculateStrategy",
                ::options_calculator::quota::cost_strategy_grid(
                    static_cast<int>(grid.price_steps), static_cast<int>(grid.date_steps),
                    req.legs_size()),
                identity.has_limits ? &strategy_limits : nullptr);
            !s.ok()) {
            return s;
        }

        if (!graph_) {
            return Status(grpc::StatusCode::INTERNAL, "Execution graph not initialized");
        }

        auto ctx = std::make_shared<ComputeContext>();
        ctx->request = req;

        // Inline, on this RPC's own thread -- see the comment in the
        // constructor for why that is correct rather than a regression.
        sgee::runtime::EngineContext<Ctx> engine;
        std::vector<Ctx> entities{ctx};
        engine.Load(entities);

        // Sequential: each request is a single entity, so batch parallelism
        // buys nothing.
        sgee::runtime::Interpreter<Ctx> interpreter(
            graph_, sgee::runtime::ParallelismLevel::Sequential, actions_.get());
        interpreter.Run(engine);

        if (!ctx->status.ok()) return ctx->status;

        // --- Postconditions -------------------------------------------------
        //
        // interpreter.Run() returns void. Success and a silent partial halt
        // are otherwise indistinguishable from here: a null action registry
        // (interpreter.cppm:151), an action that fails without setting
        // ctx->status (interpreter.cppm:193-197), and an unregistered action
        // id (action_registry.cppm's Execute() returns ActionFailed, which
        // reaches the exact same silent-halt branch) all leave this RPC about
        // to return Status::OK over whatever ctx->response happened to
        // contain when the entity stopped advancing -- a P&L curve with no
        // Greeks, or all zeros. ctx->status alone cannot catch any of the
        // three: none of them sets it.
        //
        // Both checks below are single reads off state the run above already
        // produced -- no extra pass over legs or the price grid -- so neither
        // adds measurable cost to this RPC's hot path.

        // 1. TERMINAL-STATE: did the one entity in this run actually reach
        //    the graph's Done node? If it stalled anywhere else, one of the
        //    three failure modes above occurred.
        const auto& state_ids = engine.GetStateIds();
        if (state_ids.empty() || state_ids[0] != done_node_id_) {
            const std::string stalled_at =
                state_ids.empty() ? std::string{"<no entity>"}
                                  : std::string{graph_->GetNodeName(state_ids[0])};
            log.error("CalculateStrategy: graph halted at '{}' instead of reaching Done -- an "
                      "action failed without setting status, an action id was not registered, "
                      "or the action registry was never wired up",
                      stalled_at);
            return Status(grpc::StatusCode::INTERNAL,
                          "Strategy computation halted before completion (stalled at '" +
                              stalled_at +
                              "'); this is a server-side defect, not a problem with the "
                              "request");
        }

        // 2. DID-COMPUTE: reaching Done proves every state was walked, not
        //    that any action actually wrote a payload -- a bound action that
        //    is itself a no-op would satisfy check 1 while returning zeros.
        //    Assert the one property the historical bug at this file's
        //    "Action registration by ID" comment above (and the dead-lambda
        //    regression it documents) violated: the P&L curve has exactly as
        //    many points as were requested, not zero and not some other
        //    count.
        if (static_cast<std::uint32_t>(ctx->response.pnl_matrix_size()) != ctx->price_steps) {
            log.error("CalculateStrategy: graph reached Done but pnl_matrix has {} points, not "
                      "the requested {} -- the graph walked its states without computing the "
                      "payoff curve",
                      ctx->response.pnl_matrix_size(), ctx->price_steps);
            return Status(grpc::StatusCode::INTERNAL,
                          "Strategy computation reached completion without producing a payoff "
                          "curve of the requested size; this is a server-side defect, not a "
                          "problem with the request");
        }

        res = ctx->response;
        return Status::OK;
    }

    /**
     * Live underlying quote.
     *
     * A failure is reported as a failure. The previous implementation logged
     * the error and then returned price 100.0 with `Status::OK`, so the client
     * could not tell a real quote from a fabricated one.
     *
     * A FUTURES request is not the same question as an EQUITY request even when
     * the ticker is identical, so it does not take the same path. "ES" asked as
     * an equity is Eversource Energy and the quote is returned as-is; "ES" asked
     * as a future is the E-mini S&P, which this feed does not carry, so it is
     * answered through the index proxy or not at all.
     *
     * This is where the level for the whole application is set — the header, the
     * strike ladder, the distribution and every leg's price all read this one
     * number. It served the equity to a futures symbol for as long as the term
     * structure resolved the proxy privately, which put a 7500 curve directly
     * above a 71 spot. Both were real quotes. Only one was the right instrument.
     */
    auto GetMarketQuote(ServerContext* context, const calculator::QuoteRequest* request,
                        calculator::QuoteResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        const auto& req = *request;
        auto& res = *response;

        auto& log = logger::Logger::getInstance();
        const auto symbol = req.symbol();
        const bool as_futures = (req.asset_class() == "FUTURES");
        log.info("GetMarketQuote: {} as {}", symbol,
                 req.asset_class().empty() ? "EQUITY (unstated)" : req.asset_class());

        // See the "Quota admission for the market-data RPCs" comment above
        // for why this exists and cost_market_quote's doc comment in
        // quota.cppm for why it is priced as one upstream call.
        ::options_calculator::auth::Identity identity;
        if (auto s = authorize_and_charge(context, "GetMarketQuote",
                                          ::options_calculator::quota::cost_market_quote(),
                                          identity);
            !s.ok()) {
            return s;
        }

        // What to actually ask the feed for, and what to scale its answer by.
        std::string quote_symbol = symbol;
        double multiple = 1.0;
        if (as_futures) {
            const auto proxy = futures_proxy_for(symbol);
            if (!proxy) return no_futures_source(symbol);
            quote_symbol = std::string(proxy->quote_symbol);
            multiple = proxy->multiple;
        }

        const auto quote = md::fetch_quote(quote_symbol);
        if (!quote) {
            log.error("Quote unavailable for {}: {}", quote_symbol, quote.error().message());
            return Status(grpc::StatusCode::UNAVAILABLE,
                          "No quote for " + quote_symbol + ": " + quote.error().message());
        }

        // The symbol the caller asked about, not the one that was priced. The
        // provenance below is what says they differ.
        res.set_symbol(symbol);
        res.set_price(quote->price * multiple);
        res.set_previous_close(quote->previous_close * multiple);
        res.set_asset_class(as_futures ? "FUTURES" : "EQUITY");
        // A derived level must not be attributable to a plain feed lookup. The
        // client shows this string, so it has to carry the derivation.
        res.set_provider(multiple == 1.0
                             ? "alpaca"
                             : std::format("alpaca:{}x{:g} (index proxy)", quote_symbol, multiple));
        res.set_quote_timestamp(quote->timestamp);
        // Forward P/E and a single underlying IV are not on this feed. They
        // stay at zero, which the client renders as "unknown".
        return Status::OK;
    }

    /**
     * The risk-free rate, measured.
     *
     * Its own RPC because the rate is a property of the market, not of an
     * instrument; a field on QuoteResponse would refetch a global datum on
     * every symbol change and attribute its provenance to whichever ticker
     * asked. It replaces a hardcoded 0.05 in the browser that nevertheless
     * shaped expected value, probability of profit and the distribution curve.
     *
     * A failure is reported as a failure, exactly as in GetMarketQuote. There
     * is no sentinel: 0.0 in a `double rate` cannot be told apart from a
     * genuine zero rate — historically a real observation — and that is the
     * class of fabrication spec §3.4 exists to prevent. The client then states
     * the rate is unavailable and lets the user supply one, labelled as an
     * assumption because that is what it is.
     */
    auto GetRiskFreeRate(ServerContext* context, const calculator::RiskFreeRateRequest* request,
                         calculator::RiskFreeRateResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        auto& log = logger::Logger::getInstance();

        // See the "Quota admission for the market-data RPCs" comment above
        // GetMarketQuote for why this exists and cost_risk_free_rate's doc
        // comment in quota.cppm for why it is priced the same as one Alpaca
        // quote even though this feed is Treasury, not Alpaca.
        ::options_calculator::auth::Identity identity;
        if (auto s = authorize_and_charge(context, "GetRiskFreeRate",
                                          ::options_calculator::quota::cost_risk_free_rate(),
                                          identity);
            !s.ok()) {
            return s;
        }

        const auto rate = md::fetch_risk_free_rate();
        if (!rate) {
            log.error("Risk-free rate unavailable: {}", rate.error().message());
            return Status(grpc::StatusCode::UNAVAILABLE,
                          "Risk-free rate unavailable: " + rate.error().message());
        }

        auto& res = *response;
        res.set_rate(rate->rate);
        res.set_rate_published(rate->rate_published);
        res.set_tenor(rate->tenor);
        res.set_as_of_date(rate->as_of_date);
        res.set_source(rate->source);
        res.set_fetched_at(rate->fetched_at);
        for (const auto& point : rate->curve) {
            auto& out = *res.add_curve();
            out.set_tenor(point.tenor);
            out.set_days(point.days);
            out.set_rate_bey(point.rate_bey);
            out.set_rate_continuous(point.rate_continuous);
        }

        log.info("GetRiskFreeRate: {} {:.5f} continuous as of {}", rate->tenor, rate->rate,
                 rate->as_of_date);
        return Status::OK;
    }

    /**
     * Futures term structure, derived — and labelled as such.
     *
     * No vendor wired into this engine publishes a futures forward curve, which
     * spec section 2 accepts explicitly: the term structure is modelled, not
     * live. That is not licence to invent one. A previous implementation
     * emitted nine hardcoded months priced off a fixed 4.5% carry with
     * fabricated volume and open interest, and it was deleted for exactly that
     * reason.
     *
     * The difference here is where the numbers come from. Forward price is
     * cost-of-carry, F = S * exp((r - q) * T), evaluated from a MEASURED spot
     * (the Alpaca quote) and a MEASURED risk-free rate (the Treasury CMT curve,
     * with its own observation date). Those are the same two inputs the option
     * pricer uses. Nothing is asserted that was not either observed or derived
     * from something observed by a stated formula.
     *
     * What is NOT modelled stays empty rather than plausible: bid, ask, volume
     * and open interest are order-book facts, and no formula produces them. They
     * are left at zero and the UI renders an em dash. Filling them with
     * something reasonable-looking is precisely the failure spec section 3.4
     * exists to prevent.
     *
     * Every contract carries state = "MODELLED" so a client cannot present this
     * as a quote by accident.
     *
     * The LEVEL is as much a derivation as the shape. This feed carries no
     * futures, so the spot comes from an index proxy through
     * `futures_proxy_for` — the same resolution GetMarketQuote applies, which is
     * what keeps the curve and the spot above it describing one instrument. A
     * root with no proxy gets no curve; see that helper for why refusing beats
     * answering at the wrong level.
     */
    auto build_term_structure(const std::string& symbol, calculator::ChainResponse& res) -> Status {
        auto& log = logger::Logger::getInstance();

        const auto proxy = futures_proxy_for(symbol);
        if (!proxy) return no_futures_source(symbol);
        const auto quote_symbol = std::string(proxy->quote_symbol);
        const double multiple = proxy->multiple;

        const auto quote = md::fetch_quote(quote_symbol);
        if (!quote) {
            return Status(grpc::StatusCode::UNAVAILABLE,
                          "No spot for " + quote_symbol + ": " + quote.error().message());
        }
        // Refuse rather than assume a carry rate. Without a measured rate the
        // forward curve would be a guess wearing a formula.
        const auto rate = md::fetch_risk_free_rate();
        if (!rate) {
            return Status(grpc::StatusCode::UNAVAILABLE,
                          "Term structure needs a measured risk-free rate: " +
                              rate.error().message());
        }

        const double spot = quote->price * multiple;
        const double r = rate->rate;

        res.set_symbol(symbol);
        res.set_spot_price(spot);
        res.set_fetched_at(md::rfc3339_now());

        // The quarterly cycle (March, June, September, December) is the listed
        // convention for index futures, and the single-letter month codes are
        // the CME's. This is reference data about how contracts are named, not
        // a market observation.
        static constexpr std::array<std::pair<unsigned, char>, 4> kCycle{
            {{3, 'H'}, {6, 'M'}, {9, 'U'}, {12, 'Z'}}};

        const auto today = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now());
        const std::chrono::year_month_day now_ymd{today};
        int year = static_cast<int>(now_ymd.year());
        const auto month_now = static_cast<unsigned>(now_ymd.month());

        int emitted = 0;
        for (int y = year; y <= year + 2 && emitted < 8; ++y) {
            for (const auto& [m, code] : kCycle) {
                if (emitted >= 8) break;
                if (y == year && m <= month_now) continue;

                // Listed index futures settle on the third Friday of the
                // delivery month.
                const std::chrono::year_month_day settle{
                    std::chrono::year{y} / std::chrono::month{m} /
                    std::chrono::Friday[3]};
                const auto settle_days = std::chrono::sys_days{settle};
                const auto dte = (settle_days - today).count();
                if (dte <= 0) continue;

                const double years = static_cast<double>(dte) / 365.0;
                const double forward = spot * std::exp(r * years);

                auto& c = *res.add_futures_contracts();
                c.set_code(std::format("{}{}{:02}", symbol, code, y % 100));
                c.set_delivery_month(std::format("{:04}-{:02}", y, m));
                c.set_days_to_expiry(static_cast<int>(dte));
                c.set_futures_price(forward);
                c.set_basis(forward - spot);
                // Cost of carry as an annualised rate: exactly the r that was
                // measured, restated as the yield the basis implies.
                c.set_annualized_yield(r);
                c.set_state("MODELLED");
                // bid, ask, volume, open_interest deliberately left at zero.
                ++emitted;
            }
        }

        log.info("Term structure for {}: {} contracts from {} x{:.0f} = {:.2f} and r {:.5f} as of {}",
                 symbol, emitted, quote_symbol, multiple, spot, r, rate->as_of_date);
        return Status::OK;
    }

    /**
     * Live option chain.
     *
     * Everything here is provider data joined across two Alpaca endpoints. The
     * previous implementation generated strikes from a step function around a
     * spot that defaulted to 100.0, filled volume/OI/IV with the constants
     * 1200/3400/0.22, computed "delta" from a linear moneyness expression, and
     * emitted nine hardcoded futures months priced off a fixed 4.5% carry.
     * None of that was market data.
     */
    auto GetMarketChain(ServerContext* context, const calculator::ChainRequest* request,
                        calculator::ChainResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        const auto& req = *request;
        auto& res = *response;

        auto& log = logger::Logger::getInstance();
        const auto symbol = req.symbol();
        log.info("GetMarketChain: {} expiration='{}'", symbol, req.expiration_date());

        // Upstream-call count for the branch this request is about to take,
        // read off asset_class before any network call is made -- see
        // cost_market_chain's doc comment in quota.cppm for exactly which
        // calls each branch below issues.
        int upstream_calls = 4;  // EQUITY: fetch_chain (quote, expirations, snapshot, open interest)
        if (req.asset_class() == "FUTURES") {
            upstream_calls = 2;  // build_term_structure: quote + risk-free rate
        } else if (req.asset_class() == "CRYPTO") {
            upstream_calls = 0;  // refused locally below, no network call at all
        }

        ::options_calculator::auth::Identity identity;
        if (auto s = authorize_and_charge(
                context, "GetMarketChain",
                ::options_calculator::quota::cost_market_chain(upstream_calls), identity);
            !s.ok()) {
            return s;
        }

        if (req.asset_class() == "FUTURES") {
            return build_term_structure(symbol, res);
        }
        if (req.asset_class() == "CRYPTO") {
            return Status(grpc::StatusCode::UNIMPLEMENTED,
                          "No listed option chain is available for " + req.asset_class() +
                              " from the configured provider");
        }

        const auto chain = md::fetch_chain(symbol, req.expiration_date());
        if (!chain) {
            log.error("Chain unavailable for {}: {}", symbol, chain.error().message());
            return Status(grpc::StatusCode::UNAVAILABLE,
                          "No chain for " + symbol + ": " + chain.error().message());
        }

        res.set_symbol(chain->symbol);
        res.set_spot_price(chain->spot);
        res.set_selected_expiration_date(chain->selected_expiration);
        res.set_provider("alpaca");
        res.set_fetched_at(chain->fetched_at);

        for (const auto& e : chain->expirations) {
            auto& exp = *res.add_available_expirations();
            exp.set_date_str(e.date);
            exp.set_days_to_expiry(e.days);
            exp.set_label(e.label);
        }

        // The at-the-money row is the listed strike closest to spot, not a
        // rounded guess at where it ought to be.
        double atm = 0.0;
        double best = std::numeric_limits<double>::max();
        for (const auto& row : chain->strikes) {
            const double d = std::abs(row.strike - chain->spot);
            if (d < best) { best = d; atm = row.strike; }
        }

        for (const auto& row : chain->strikes) {
            auto& s = *res.add_option_strikes();
            s.set_strike(row.strike);
            s.set_is_atm(row.strike == atm);
            s.set_expiration_date(chain->selected_expiration);

            s.set_call_bid(row.call.bid);
            s.set_call_ask(row.call.ask);
            s.set_call_delta(row.call.delta);
            s.set_call_gamma(row.call.gamma);
            s.set_call_theta(row.call.theta);
            s.set_call_vega(row.call.vega);
            s.set_call_iv(row.call.iv);
            s.set_call_volume(static_cast<std::int32_t>(row.call.volume));
            s.set_call_open_interest(static_cast<std::int32_t>(row.call.open_interest));

            s.set_put_bid(row.put.bid);
            s.set_put_ask(row.put.ask);
            s.set_put_delta(row.put.delta);
            s.set_put_gamma(row.put.gamma);
            s.set_put_theta(row.put.theta);
            s.set_put_vega(row.put.vega);
            s.set_put_iv(row.put.iv);
            s.set_put_volume(static_cast<std::int32_t>(row.put.volume));
            s.set_put_open_interest(static_cast<std::int32_t>(row.put.open_interest));
        }

        log.info("GetMarketChain: {} returned {} strikes, {} expirations", symbol,
                 res.option_strikes_size(), res.available_expirations_size());
        return Status::OK;
    }

    // ----------------------------------------------------------------------
    // Saved scenarios
    //
    // Every one of the three follows the same order, and the order is the
    // point: resolve identity -> entitlement (which also proves a per-user
    // subject exists) -> availability -> do the work. Nothing touches storage
    // before a verified subject is in hand, so there is no path that can write
    // a row without knowing whose it is.
    // ----------------------------------------------------------------------

    auto SaveStrategy(ServerContext* context, const calculator::SaveStrategyRequest* request,
                      calculator::SaveStrategyResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "null request or response");
        }

        ::options_calculator::auth::Identity identity;
        if (auto s = resolve_identity(context, "SaveStrategy", identity); !s.ok()) return s;
        if (auto s = ::options_calculator::auth::check_saved_scenarios_entitlement(identity,
                                                                                   "SaveStrategy");
            !s.ok()) {
            return s;
        }
        if (auto s = require_store(); !s.ok()) return s;

        // Trimmed before length-checking, so a name of only spaces is refused
        // as empty rather than stored as a blank label the list cannot show.
        const std::string name = trimmed(request->name());
        if (name.empty()) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT, "A scenario needs a name.");
        }
        if (name.size() > ::options_calculator::store::kMaxNameBytes) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          std::format("A scenario name may be at most {} characters.",
                                      ::options_calculator::store::kMaxNameBytes));
        }
        // Refused rather than stored: a scenario with no legs cannot be priced
        // when reopened, so accepting it would trade an error now for a
        // confusing one later.
        if (request->request().legs_size() == 0) {
            return Status(grpc::StatusCode::INVALID_ARGUMENT,
                          "A scenario needs at least one leg.");
        }

        std::string payload_json;
        google::protobuf::json::PrintOptions print_opts;
        // Zeros are written explicitly. Proto3 would reconstruct them on read
        // either way, so this is not needed for correctness -- it is so that a
        // stored document read in psql shows every field it was saved with,
        // rather than only the ones that happened to be non-default.
        print_opts.always_print_primitive_fields = true;
        if (const auto st = google::protobuf::json::MessageToJsonString(
                request->request(), &payload_json, print_opts);
            !st.ok()) {
            logger::Logger::getInstance().error("SaveStrategy: serialize failed: {}",
                                                std::string{st.message()});
            return Status(grpc::StatusCode::INTERNAL, "Could not encode this scenario.");
        }

        auto saved = store_->save(identity.subject, name, request->request().underlying_symbol(),
                                  payload_json);
        if (!saved) return store_error_status(saved.error(), "save");

        if (!to_proto(saved->row, response->mutable_strategy())) {
            return Status(grpc::StatusCode::INTERNAL, "Could not decode the stored scenario.");
        }
        response->set_replaced_existing(saved->replaced_existing);

        logger::Logger::getInstance().info("SaveStrategy: user={} name=\"{}\" replaced={}",
                                           identity.subject, name, saved->replaced_existing);
        return Status::OK;
    }

    auto ListStrategies(ServerContext* context, const calculator::ListStrategiesRequest* request,
                        calculator::ListStrategiesResponse* response) -> Status override {
        (void)request;  // deliberately empty -- the caller is their token, not a field
        if (response == nullptr) return Status(grpc::StatusCode::INTERNAL, "null response");

        ::options_calculator::auth::Identity identity;
        if (auto s = resolve_identity(context, "ListStrategies", identity); !s.ok()) return s;
        if (auto s = ::options_calculator::auth::check_saved_scenarios_entitlement(
                identity, "ListStrategies");
            !s.ok()) {
            return s;
        }
        if (auto s = require_store(); !s.ok()) return s;

        auto rows = store_->list(identity.subject);
        if (!rows) return store_error_status(rows.error(), "list");

        for (const auto& row : *rows) {
            // A row whose stored JSON no longer parses is SKIPPED, not fatal.
            // One unreadable scenario must not make the whole list unopenable;
            // it is logged so it can be found, and the user sees the rest.
            if (!to_proto(row, response->add_strategies())) {
                response->mutable_strategies()->RemoveLast();
                logger::Logger::getInstance().error(
                    "ListStrategies: skipping unreadable row id={} user={}", row.id,
                    identity.subject);
            }
        }
        return Status::OK;
    }

    auto DeleteStrategy(ServerContext* context, const calculator::DeleteStrategyRequest* request,
                        calculator::DeleteStrategyResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "null request or response");
        }

        ::options_calculator::auth::Identity identity;
        if (auto s = resolve_identity(context, "DeleteStrategy", identity); !s.ok()) return s;
        if (auto s = ::options_calculator::auth::check_saved_scenarios_entitlement(
                identity, "DeleteStrategy");
            !s.ok()) {
            return s;
        }
        if (auto s = require_store(); !s.ok()) return s;

        auto removed = store_->remove(identity.subject, request->id());
        if (!removed) return store_error_status(removed.error(), "delete");

        // A miss is OK with deleted=false, not NOT_FOUND. Delete is idempotent
        // from the client's side, and returning an error for "already gone"
        // would make a retried delete look like a failure.
        response->set_deleted(*removed);
        return Status::OK;
    }

private:
    /** Shared prologue: the same authenticate() call every other RPC makes. */
    [[nodiscard]] static auto resolve_identity(ServerContext* context, std::string_view rpc,
                                               ::options_calculator::auth::Identity& out)
        -> Status {
        if (context == nullptr) return Status::OK;
        return ::options_calculator::auth::KeyRegistry::instance().authenticate(
            *context, "calculator", rpc, out);
    }

    [[nodiscard]] auto require_store() const -> Status {
        if (store_ != nullptr) return Status::OK;
        // FAILED_PRECONDITION, not INTERNAL: nothing is broken and nothing the
        // caller sends will help. This deployment simply has no database.
        return Status(grpc::StatusCode::FAILED_PRECONDITION,
                      "Saved scenarios are not available on this deployment.");
    }

    /** Maps a storage failure to the status that describes it to a caller. */
    [[nodiscard]] static auto store_error_status(::options_calculator::store::StoreError err,
                                                 std::string_view op) -> Status {
        using ::options_calculator::store::StoreError;
        switch (err) {
            case StoreError::AtCapacity:
                return Status(grpc::StatusCode::RESOURCE_EXHAUSTED,
                              std::format("You have reached the limit of {} saved scenarios. "
                                          "Delete one to save another.",
                                          ::options_calculator::store::kMaxPerUser));
            case StoreError::Invalid:
                return Status(grpc::StatusCode::INVALID_ARGUMENT,
                              "That scenario request was not valid.");
            case StoreError::UnknownUser:
                // UNAUTHENTICATED, not NOT_FOUND: the token is genuinely valid
                // and the account behind it is not. Signing in again is the
                // only thing that resolves it, and that is what this code tells
                // a client to do.
                return Status(grpc::StatusCode::UNAUTHENTICATED,
                              "This account no longer exists. Sign in again.");
            case StoreError::Unavailable:
                return Status(grpc::StatusCode::UNAVAILABLE,
                              "Saved scenarios are temporarily unavailable. Try again shortly.");
            case StoreError::Internal:
                break;
        }
        logger::Logger::getInstance().error("saved-scenarios {}: {}", op,
                                            ::options_calculator::store::to_string(err));
        return Status(grpc::StatusCode::INTERNAL, "Saved scenarios failed unexpectedly.");
    }

    /** Stored row -> wire message. False when the stored JSON no longer parses. */
    [[nodiscard]] static auto to_proto(const ::options_calculator::store::SavedRow& row,
                                       calculator::SavedStrategy* out) -> bool {
        out->set_id(row.id);
        out->set_name(row.name);
        out->set_created_at(row.created_at);
        out->set_updated_at(row.updated_at);

        google::protobuf::json::ParseOptions parse_opts;
        // A document written by a LATER build that added a field must still
        // load on THIS one. Failing instead would make every rollback of a
        // proto change silently unopen a user's saved scenarios.
        parse_opts.ignore_unknown_fields = true;
        const auto st = google::protobuf::json::JsonStringToMessage(
            row.payload_json, out->mutable_request(), parse_opts);
        return st.ok();
    }

    [[nodiscard]] static auto trimmed(std::string_view s) -> std::string {
        constexpr std::string_view kWs = " \t\n\r\f\v";
        const auto begin = s.find_first_not_of(kWs);
        if (begin == std::string_view::npos) return {};
        const auto end = s.find_last_not_of(kWs);
        return std::string{s.substr(begin, end - begin + 1)};
    }
};

auto RegisterCalculatorService(grpc::ServerBuilder& builder) -> void {
    // DATABASE_URL unset is a SUPPORTED configuration, not a failure: the
    // factory returns null, the three saved-scenario RPCs answer
    // FAILED_PRECONDITION, and every other RPC in this service is untouched.
    // A local build with no database therefore still serves the calculator.
    //
    // Construction does not connect -- pg::Pool opens lazily -- so a database
    // that happens to be down at boot cannot stop the engine from starting.
    const char* const database_url = std::getenv("DATABASE_URL");
    auto store = ::options_calculator::store::make_pg_strategy_store(
        database_url != nullptr ? std::string_view{database_url} : std::string_view{});

    auto& log = logger::Logger::getInstance();
    if (store == nullptr) {
        log.warn(
            "DATABASE_URL is unset -- saved scenarios are disabled on this engine. Every other "
            "calculator RPC is unaffected.");
    } else {
        log.info("Saved scenarios enabled (Postgres).");
    }

    // Static storage duration so the service outlives the builder and the
    // server without either owning it. RegisterService takes the address by
    // gRPC's own contract; ownership stays here.
    static CalculatorServiceImpl service{kAllActionNames, std::move(store)};
    builder.RegisterService(&service);
}

auto RegisterCalculatorServiceForTest(grpc::ServerBuilder& builder,
                                      std::span<const std::string_view> bound_action_names,
                                      std::shared_ptr<store::IStrategyStore> store) -> void {
    // Deliberately leaked, unlike the function-local static above. A static
    // here would fix the bound action set at the FIRST call for the rest of
    // the process, which is wrong for this hook: a discriminating test needs
    // to register services with DIFFERENT subsets of actions in the same
    // test binary (one with the full set as a control, one missing an
    // action). Each caller is a short-lived test process that builds one
    // in-process grpc::Server per case and exits soon after, so the leak is
    // bounded by the test's own lifetime -- the same trade the rest of this
    // file's test siblings (ServiceFixture in test_option_pricing_service.cpp
    // and test_finance_service_validation.cpp) make implicitly by never
    // tearing down what BuildAndStart() allocates.
    auto* service = new CalculatorServiceImpl(bound_action_names, std::move(store));  // NOLINT(cppcoreguidelines-owning-memory) -- see comment above: gRPC's RegisterService does NOT take ownership and the service must outlive the server, while a function-local static would freeze the bound action set at the first call and break the discriminating tests that register different action subsets in one binary.
    builder.RegisterService(service);
}

}  // namespace options_calculator::service
