export module calculator.engine;
import std;

import sensen.options;
import sensen.portfolio;

export namespace calculator {

    enum class Action { BUY = 0, SELL = 1 };
    enum class Type { CALL = 0, PUT = 1, FUTURE = 2, STOCK = 3 };

    struct Leg {
        Action action;
        Type type;
        double strike;
        double expiration_days;
        std::int32_t quantity;
    };

    struct StrategyRequest {
        std::string underlying_symbol;
        double current_price;
        double implied_volatility;
        double risk_free_rate;
        std::vector<Leg> legs;
    };

    struct Greeks {
        double delta;
        double gamma;
        double theta;
        double vega;
        double rho;
    };

    struct PnLPoint {
        double underlying_price;
        double pnl;
    };

    struct RiskMetrics {
        double var_parametric_95;
        double var_parametric_99;
        double cvar_parametric_95;
        double cvar_parametric_99;
    };

    struct StrategyResponse {
        double max_profit;
        double max_loss;
        double break_even;
        double expected_value;
        double pop; // Probability of Profit
        Greeks net_greeks;
        RiskMetrics risk_metrics;
        std::vector<PnLPoint> pnl_matrix;
    };

    enum class ErrorCode {
        INVALID_PRICE,
        INVALID_VOLATILITY,
        INVALID_RATE,
        INVALID_DAYS,
        SIMD_COMPUTATION_ERROR,
        UNKNOWN_ERROR
    };

    struct EngineError {
        ErrorCode code;
        std::string message;
    };

    // Calculate parametric VaR based on log-normal assumption of underlying
    RiskMetrics calculate_risk(double strategy_cost, double sigma, double expected_return = 0.0) {
        // Z-scores for 95% and 99%
        constexpr double Z_95 = 1.64485;
        constexpr double Z_99 = 2.32635;
        
        // Parametric VaR = Cost * (Z * sigma - mu)
        // Simplified assuming mu=0 for VaR timeframe if not provided
        RiskMetrics metrics{};
        metrics.var_parametric_95 = strategy_cost * (Z_95 * sigma - expected_return);
        metrics.var_parametric_99 = strategy_cost * (Z_99 * sigma - expected_return);
        
        // Parametric CVaR = Cost * (phi(Z)/(1-alpha) * sigma - mu)
        // phi(Z_95) = 0.103135, phi(Z_99) = 0.026652
        metrics.cvar_parametric_95 = strategy_cost * ((0.103135 / 0.05) * sigma - expected_return);
        metrics.cvar_parametric_99 = strategy_cost * ((0.026652 / 0.01) * sigma - expected_return);
        
        return metrics;
    }

    std::expected<StrategyResponse, EngineError> calculate_strategy(const StrategyRequest& req) {
        if (req.current_price <= 0.0) return std::unexpected(EngineError{ErrorCode::INVALID_PRICE, "Current price must be > 0"});
        if (req.implied_volatility <= 0.0) return std::unexpected(EngineError{ErrorCode::INVALID_VOLATILITY, "Volatility must be > 0"});
        
        StrategyResponse resp{};
        resp.max_profit = 0.0;
        resp.max_loss = 0.0;
        resp.break_even = 0.0;
        resp.expected_value = 0.0;
        resp.pop = 0.0;
        resp.net_greeks = {0.0, 0.0, 0.0, 0.0, 0.0};
        
        double total_strategy_cost = 0.0;

        for (const auto& leg : req.legs) {
            double T = leg.expiration_days / 365.0; 
            double S = req.current_price;
            double K = leg.strike;
            double r = req.risk_free_rate;
            double sigma = req.implied_volatility;
            
            int sign = (leg.action == Action::BUY) ? 1 : -1;
            int q = leg.quantity;
            
            if (leg.type == Type::CALL || leg.type == Type::PUT) {
                sensen::OptionType opt_type = (leg.type == Type::CALL) ? sensen::OptionType::Call : sensen::OptionType::Put;
                auto bsm_result = sensen::price_black_scholes(S, K, r, sigma, T, opt_type);
                
                double leg_cost = bsm_result.value * sign * q * 100.0; // Assuming standard 100 multiplier
                total_strategy_cost += leg_cost;

                resp.net_greeks.delta += bsm_result.delta * sign * q;
                resp.net_greeks.gamma += bsm_result.gamma * sign * q;
                resp.net_greeks.theta += bsm_result.theta * sign * q;
                resp.net_greeks.vega  += bsm_result.vega * sign * q;
                resp.net_greeks.rho   += bsm_result.rho * sign * q;
            } else if (leg.type == Type::STOCK) {
                double entry_price = (K > 0.0) ? K : S;
                double leg_cost = entry_price * sign * q; // 1 share per quantity
                total_strategy_cost += leg_cost;
                resp.net_greeks.delta += 1.0 * sign * q; // Delta is 1 for stock
            } else if (leg.type == Type::FUTURE) {
                double entry_price = (K > 0.0) ? K : S;
                // Futures usually don't have upfront cost (only margin), but delta is 1
                resp.net_greeks.delta += 1.0 * sign * q; 
            }
        }
        
        // Risk matrix calculations using parametric assumptions
        resp.risk_metrics = calculate_risk(std::abs(total_strategy_cost), req.implied_volatility);
        
        // Generate PnL Matrix (Simulation over underlying price ranges)
        double range = req.current_price * req.implied_volatility * 2.0; // +/- 2 std devs
        double min_p = std::max(0.1, req.current_price - range);
        double max_p = req.current_price + range;
        int steps = 50;
        double step_size = (max_p - min_p) / steps;
        
        double peak_pnl = -1e9;
        double trough_pnl = 1e9;
        int profitable_scenarios = 0;

        for (int i = 0; i <= steps; ++i) {
            double simulated_price = min_p + i * step_size;
            double simulated_pnl = 0.0;
            
            for (const auto& leg : req.legs) {
                int sign = (leg.action == Action::BUY) ? 1 : -1;
                if (leg.type == Type::CALL || leg.type == Type::PUT) {
                    double T = leg.expiration_days / 365.0;
                    sensen::OptionType opt_type = (leg.type == Type::CALL) ? sensen::OptionType::Call : sensen::OptionType::Put;
                    
                    // Assuming terminal payoff for P&L matrix at expiration (T=0)
                    auto payoff = sensen::price_black_scholes(simulated_price, leg.strike, req.risk_free_rate, req.implied_volatility, 0.0, opt_type);
                    
                    // subtract initial cost
                    auto initial = sensen::price_black_scholes(req.current_price, leg.strike, req.risk_free_rate, req.implied_volatility, T, opt_type);
                    
                    simulated_pnl += (payoff.value - initial.value) * sign * leg.quantity * 100.0;
                } else if (leg.type == Type::STOCK) {
                    double entry_price = (leg.strike > 0.0) ? leg.strike : req.current_price;
                    simulated_pnl += (simulated_price - entry_price) * sign * leg.quantity;
                } else if (leg.type == Type::FUTURE) {
                    double entry_price = (leg.strike > 0.0) ? leg.strike : req.current_price;
                    double multiplier = 100.0; // assuming future multiplier matches options for consistency
                    simulated_pnl += (simulated_price - entry_price) * sign * leg.quantity * multiplier;
                }
            }
            
            resp.pnl_matrix.push_back({simulated_price, simulated_pnl});
            
            if (simulated_pnl > peak_pnl) peak_pnl = simulated_pnl;
            if (simulated_pnl < trough_pnl) trough_pnl = simulated_pnl;
            if (simulated_pnl > 0) profitable_scenarios++;
        }
        
        resp.max_profit = peak_pnl;
        resp.max_loss = trough_pnl;
        resp.pop = static_cast<double>(profitable_scenarios) / (steps + 1);

        return resp;
    }
}
