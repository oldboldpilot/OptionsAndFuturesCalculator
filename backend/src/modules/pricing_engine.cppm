export module calculator.engine;
import std;

export namespace calculator {

    enum class Action { BUY = 0, SELL = 1 };
    enum class Type { CALL = 0, PUT = 1 };

    struct Leg {
        Action action;
        Type type;
        double strike;
        double expiration_days;
        int32_t quantity;
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

    struct StrategyResponse {
        double max_profit;
        double max_loss;
        double break_even;
        double expected_value;
        double pop;
        Greeks net_greeks;
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

    // Stub for sensen SIMD functions
    namespace sensen::simd {
        double add(double a, double b) { return a + b; }
        double sub(double a, double b) { return a - b; }
        double mul(double a, double b) { return a * b; }
        double div(double a, double b) { return a / b; }
        double exp(double a) { return std::exp(a); }
        double log(double a) { return std::log(a); }
        double sqrt(double a) { return std::sqrt(a); }
        double norm_cdf(double a) { return 0.5 * std::erfc(-a * std::numbers::sqrt1_2); }
    }

    std::expected<double, EngineError> calculate_d1(double S, double K, double T, double r, double sigma) {
        if (S <= 0.0) return std::unexpected(EngineError{ErrorCode::INVALID_PRICE, "Price must be > 0"});
        if (sigma <= 0.0) return std::unexpected(EngineError{ErrorCode::INVALID_VOLATILITY, "Volatility must be > 0"});
        if (T <= 0.0) return std::unexpected(EngineError{ErrorCode::INVALID_DAYS, "Time must be > 0"});

        using namespace sensen::simd;
        double term1 = log(div(S, K));
        double term2 = mul(add(r, div(mul(sigma, sigma), 2.0)), T);
        double num = add(term1, term2);
        double den = mul(sigma, sqrt(T));
        return div(num, den);
    }

    std::expected<double, EngineError> calculate_d2(double d1, double sigma, double T) {
        using namespace sensen::simd;
        return sub(d1, mul(sigma, sqrt(T)));
    }

    std::expected<double, EngineError> calculate_call_price(double S, double K, double T, double r, double sigma) {
        auto d1_res = calculate_d1(S, K, T, r, sigma);
        if (!d1_res) return std::unexpected(d1_res.error());
        
        auto d2_res = calculate_d2(*d1_res, sigma, T);
        if (!d2_res) return std::unexpected(d2_res.error());

        using namespace sensen::simd;
        double term1 = mul(S, norm_cdf(*d1_res));
        double term2 = mul(mul(K, exp(mul(-r, T))), norm_cdf(*d2_res));
        return sub(term1, term2);
    }
    
    std::expected<double, EngineError> calculate_put_price(double S, double K, double T, double r, double sigma) {
        auto d1_res = calculate_d1(S, K, T, r, sigma);
        if (!d1_res) return std::unexpected(d1_res.error());
        
        auto d2_res = calculate_d2(*d1_res, sigma, T);
        if (!d2_res) return std::unexpected(d2_res.error());

        using namespace sensen::simd;
        double term1 = mul(mul(K, exp(mul(-r, T))), norm_cdf(-*d2_res));
        double term2 = mul(S, norm_cdf(-*d1_res));
        return sub(term1, term2);
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
        
        for (const auto& leg : req.legs) {
            double T = leg.expiration_days / 365.0; 
            double S = req.current_price;
            double K = leg.strike;
            double r = req.risk_free_rate;
            double sigma = req.implied_volatility;

            std::expected<double, EngineError> price_res;
            if (leg.type == Type::CALL) {
                price_res = calculate_call_price(S, K, T, r, sigma);
            } else {
                price_res = calculate_put_price(S, K, T, r, sigma);
            }
            if (!price_res) return std::unexpected(price_res.error());
        }
        
        return resp;
    }
}
