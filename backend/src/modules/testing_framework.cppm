export module calculator.testing;
import std;
import calculator.engine;

export namespace calculator::testing {

    struct TestResult {
        bool passed;
        std::string message;
    };

    TestResult assert_true(bool condition, std::string_view msg) {
        if (condition) {
            return {true, std::format("PASS: {}", msg)};
        } else {
            return {false, std::format("FAIL: {}", msg)};
        }
    }

    TestResult run_all_tests() {
        calculator::StrategyRequest req;
        req.underlying_symbol = "AAPL";
        req.current_price = 150.0;
        req.implied_volatility = 0.2;
        req.risk_free_rate = 0.05;
        
        calculator::Leg leg;
        leg.action = calculator::Action::BUY;
        leg.type = calculator::Type::CALL;
        leg.strike = 155.0;
        leg.expiration_days = 30.0;
        leg.quantity = 1;
        
        req.legs.push_back(leg);

        auto result = calculator::calculate_strategy(req);
        if (result) {
            return assert_true(true, "Strategy calculated successfully using ROP");
        } else {
            return assert_true(false, std::format("Strategy calculation failed: {}", result.error().message));
        }
    }
}
