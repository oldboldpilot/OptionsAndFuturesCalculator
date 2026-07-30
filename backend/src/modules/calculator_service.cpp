module;
#include <algorithm>
#include <array>
#include <string_view>
#include <utility>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <expected>
#include <future>
#include <limits>
#include <memory>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

#include <grpcpp/grpcpp.h>
#include "calculator.pb.h"
#include "calculator.grpc.pb.h"

module calculator_service;

import sensen.options;
import logger;
import market_data;

import sgee.builder.fluent;
import sgee.runtime.context;
import sgee.runtime.interpreter;
import sgee.runtime.action_registry;
import sgee.runtime.pipeline;
import sgee.core.types;

namespace options_calculator::service {

using grpc::ServerContext;
using grpc::Status;

namespace md = options_calculator::market_data;

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

/**
 * Position P&L at expiration for a given underlying price.
 *
 * Options settle to intrinsic value; linear instruments settle to the move
 * from their entry. Premium is the price actually paid or received, which the
 * client now sends per leg — before the contract carried no premium field, so
 * every structure was implicitly free and every payoff was wrong.
 */
[[nodiscard]] auto payoff_at_expiry(const calculator::StrategyRequest& req, double price) noexcept -> double {
    double total = 0.0;
    for (const auto& leg : req.legs()) {
        const double dir = direction_of(leg);
        const double mult = multiplier_of(leg);
        const double qty = quantity_of(leg);

        switch (leg.type()) {
            case calculator::Leg::CALL:
                total += (std::max(0.0, price - leg.strike()) - leg.premium()) * dir * mult * qty;
                break;
            case calculator::Leg::PUT:
                total += (std::max(0.0, leg.strike() - price) - leg.premium()) * dir * mult * qty;
                break;
            default: {
                // Linear legs: entry is the premium when supplied, otherwise
                // the strike field carries the entry level.
                const double entry = (leg.premium() > 0.0) ? leg.premium() : leg.strike();
                total += (price - entry) * dir * mult * qty;
                break;
            }
        }
    }
    return total;
}

/**
 * Position P&L at an intermediate date, with every option re-priced by
 * Black-Scholes at the remaining time. This is what makes the price × date
 * matrix meaningful rather than a repeated expiry column.
 */
[[nodiscard]] auto value_at(const calculator::StrategyRequest& req, double price,
                            double years_remaining, double r) noexcept -> double {
    if (years_remaining <= 1e-9) return payoff_at_expiry(req, price);

    // One clock for every leg. Correct for the single-expiry structures the UI
    // builds today; for a genuine calendar spread the near leg should expire
    // partway along this axis and be carried at intrinsic thereafter. That is a
    // change to the whole matrix, the expiry curve and every metric derived
    // from them, so it is tracked separately rather than smuggled in here.
    double total = 0.0;
    for (const auto& leg : req.legs()) {
        const double dir = direction_of(leg);
        const double mult = multiplier_of(leg);
        const double qty = quantity_of(leg);

        if (leg.type() == calculator::Leg::CALL || leg.type() == calculator::Leg::PUT) {
            const double iv = (leg.implied_volatility() > 0.0)
                                  ? leg.implied_volatility()
                                  : req.implied_volatility();
            if (iv <= 0.0) return payoff_at_expiry(req, price);

            const auto type = (leg.type() == calculator::Leg::CALL) ? sensen::OptionType::Call
                                                                   : sensen::OptionType::Put;
            const auto bs = sensen::price_black_scholes(price, leg.strike(), r, iv, years_remaining, type);
            total += (bs.value - leg.premium()) * dir * mult * qty;
        } else {
            const double entry = (leg.premium() > 0.0) ? leg.premium() : leg.strike();
            total += (price - entry) * dir * mult * qty;
        }
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
                                  double years, double r) -> Distribution {
    Distribution dist;
    dist.price = grid;
    dist.density.assign(grid.size(), 0.0);
    if (grid.size() < 2 || spot <= 0.0 || sigma <= 0.0 || years <= 0.0) return dist;

    const double sd = sigma * std::sqrt(years);
    const double mu = std::log(spot) + (r - 0.5 * sigma * sigma) * years;

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
// SGEE execution context
// --------------------------------------------------------------------------

struct ComputeContext {
    calculator::StrategyRequest request;
    calculator::StrategyResponse response;
    grpc::Status status{grpc::Status::OK};
    std::shared_ptr<std::promise<void>> promise;

    double spot{0.0};
    double r{0.0};
    double sigma{0.0};
    double horizon{0.0};
    std::uint32_t date_steps{0};
    std::uint32_t price_steps{0};
    std::vector<double> price_grid;
    std::vector<double> expiry_pnl;
};
using Ctx = std::shared_ptr<ComputeContext>;
using PipelineType = sgee::runtime::TransformedPipeline<Ctx, Ctx>;
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

[[nodiscard]] auto action_initialize(Ctx& ctx) -> ExecutionResult<> {
    ctx->spot = ctx->request.current_price();
    ctx->r = ctx->request.risk_free_rate();
    ctx->sigma = position_iv(ctx->request);
    ctx->horizon = horizon_days(ctx->request);

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

    ctx->date_steps = ctx->request.date_steps() > 0 ? ctx->request.date_steps() : 12;
    ctx->price_steps = ctx->request.price_steps() > 0 ? ctx->request.price_steps() : 81;

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

    for (const double price : ctx->price_grid) {
        const double pnl = payoff_at_expiry(ctx->request, price);
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
        const double dte = ctx->horizon * (1.0 - frac);
        const double years = std::max(0.0, dte / 365.0);

        const auto day = std::chrono::floor<std::chrono::days>(now) +
                         std::chrono::days{static_cast<int>(std::llround(dte))};
        const std::chrono::year_month_day ymd{day};
        std::ostringstream date_str;
        date_str << static_cast<int>(ymd.year()) << '-'
                 << (static_cast<unsigned>(ymd.month()) < 10 ? "0" : "")
                 << static_cast<unsigned>(ymd.month()) << '-'
                 << (static_cast<unsigned>(ymd.day()) < 10 ? "0" : "")
                 << static_cast<unsigned>(ymd.day());
        const auto date_text = date_str.str();

        for (const double price : ctx->price_grid) {
            const double pnl = value_at(ctx->request, price, years, ctx->r);
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

    for (const auto& leg : ctx->request.legs()) {
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
                sensen::price_black_scholes(ctx->spot, leg.strike(), ctx->r, iv, years, type);
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
        } else {
            delta += dir * mult * qty;
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
    if (ctx->sigma <= 0.0 || ctx->horizon <= 0.0) {
        logger::Logger::getInstance().warn(
            "No implied volatility on the position; probability metrics omitted");
        return {};
    }

    const auto dist =
        lognormal_over(ctx->price_grid, ctx->spot, ctx->sigma, ctx->horizon / 365.0, ctx->r);
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
        const double sd = ctx->sigma * std::sqrt(ctx->horizon / 365.0);
        if (sd > 0.0 && nearest > 0.0) {
            const double z = std::abs(std::log(nearest / ctx->spot)) / sd;
            const double touch = 2.0 * (1.0 - 0.5 * (1.0 + std::erf(z / std::sqrt(2.0))));
            ctx->response.set_probability_of_touch(std::min(1.0, touch));
        }
    }
    return {};
}

class CalculatorServiceImpl final : public calculator::OptionsCalculator::Service {
private:
    std::shared_ptr<ActionRegistry> actions_;
    std::unique_ptr<PipelineType> execution_engine_;

public:
    CalculatorServiceImpl() : actions_{std::make_shared<ActionRegistry>()} {
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
        auto graph = graph_result.value();

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

        for (const auto& [name, fn] : bindings) {
            const auto id = graph->GetActionId(name);
            if (!id) {
                log.error("Action '{}' is not present in the graph; refusing to start", name);
                return;
            }
            actions_->RegisterById(*id, fn);
        }

        auto actions = actions_;

        auto builder = PipelineType::create()
            .withTransform([graph, actions](const Ctx& ctx) -> Ctx {
                sgee::runtime::EngineContext<Ctx> engine;
                std::vector<Ctx> entities{ctx};
                engine.Load(entities);

                // Sequential: each request is a single entity, so batch
                // parallelism buys nothing and the pipeline already runs
                // requests concurrently across its worker pool.
                sgee::runtime::Interpreter<Ctx> interpreter(
                    graph, sgee::runtime::ParallelismLevel::Sequential, actions.get());
                interpreter.Run(engine);
                return ctx;
            })
            .withQueue(sgee::QueueType::FIFO)
            .withBackpressure(sgee::BackpressurePolicy::Block)
            .withAsync(sgee::AsyncConfig{
                .workers = 16,
                .batch_size = 1,
                .poll_interval_ms = 1
            })
            .build();

        if (builder) {
            execution_engine_ = std::make_unique<PipelineType>(std::move(*builder));
            execution_engine_->startWorkers([](Ctx& ctx) { ctx->promise->set_value(); });
            log.info("SGEE pipeline initialized: 16 workers, {} registered actions", bindings.size());
        } else {
            log.error("Failed to build SGEE pipeline: {}", builder.error());
        }
    }

    ~CalculatorServiceImpl() override {
        if (execution_engine_) {
            execution_engine_->stop();
        }
    }

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
    auto CalculateStrategy(ServerContext*, const calculator::StrategyRequest* request,
                           calculator::StrategyResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        const auto& req = *request;
        auto& res = *response;

        auto& log = logger::Logger::getInstance();
        log.info("CalculateStrategy: {} with {} legs", req.underlying_symbol(), req.legs_size());

        if (!execution_engine_) {
            return Status(grpc::StatusCode::INTERNAL, "Execution engine not initialized");
        }

        auto ctx = std::make_shared<ComputeContext>();
        ctx->request = req;
        ctx->promise = std::make_shared<std::promise<void>>();
        auto future = ctx->promise->get_future();

        if (!execution_engine_->push(ctx)) {
            log.warn("Backpressure applied: execution queue full");
            return Status(grpc::StatusCode::RESOURCE_EXHAUSTED,
                          "Server is currently overloaded. Backpressure applied.");
        }

        future.wait();

        if (!ctx->status.ok()) return ctx->status;

        res = ctx->response;
        return Status::OK;
    }

    /**
     * Live underlying quote.
     *
     * A failure is reported as a failure. The previous implementation logged
     * the error and then returned price 100.0 with `Status::OK`, so the client
     * could not tell a real quote from a fabricated one.
     */
    auto GetMarketQuote(ServerContext*, const calculator::QuoteRequest* request,
                        calculator::QuoteResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        const auto& req = *request;
        auto& res = *response;

        auto& log = logger::Logger::getInstance();
        const auto symbol = req.symbol();
        log.info("GetMarketQuote: {}", symbol);

        const auto quote = md::fetch_quote(symbol);
        if (!quote) {
            log.error("Quote unavailable for {}: {}", symbol, quote.error().message());
            return Status(grpc::StatusCode::UNAVAILABLE,
                          "No quote for " + symbol + ": " + quote.error().message());
        }

        res.set_symbol(quote->symbol);
        res.set_price(quote->price);
        res.set_previous_close(quote->previous_close);
        res.set_provider("alpaca");
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
    auto GetRiskFreeRate(ServerContext*, const calculator::RiskFreeRateRequest* request,
                         calculator::RiskFreeRateResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        auto& log = logger::Logger::getInstance();

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
     * Live option chain.
     *
     * Everything here is provider data joined across two Alpaca endpoints. The
     * previous implementation generated strikes from a step function around a
     * spot that defaulted to 100.0, filled volume/OI/IV with the constants
     * 1200/3400/0.22, computed "delta" from a linear moneyness expression, and
     * emitted nine hardcoded futures months priced off a fixed 4.5% carry.
     * None of that was market data.
     */
    auto GetMarketChain(ServerContext*, const calculator::ChainRequest* request,
                        calculator::ChainResponse* response) -> Status override {
        if (request == nullptr || response == nullptr) {
            return Status(grpc::StatusCode::INTERNAL, "Null request or response from transport");
        }
        const auto& req = *request;
        auto& res = *response;

        auto& log = logger::Logger::getInstance();
        const auto symbol = req.symbol();
        log.info("GetMarketChain: {} expiration='{}'", symbol, req.expiration_date());

        if (req.asset_class() == "FUTURES" || req.asset_class() == "CRYPTO") {
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
};

auto RegisterCalculatorService(grpc::ServerBuilder& builder) -> void {
    // Static storage duration so the service outlives the builder and the
    // server without either owning it. RegisterService takes the address by
    // gRPC's own contract; ownership stays here.
    static CalculatorServiceImpl service;
    builder.RegisterService(&service);
}

}  // namespace options_calculator::service
