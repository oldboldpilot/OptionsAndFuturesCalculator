#include <gtest/gtest.h>
#include <grpcpp/grpcpp.h>
#include "calculator.pb.h"
#include "calculator.grpc.pb.h"

// We need to import the C++23 module where the service is defined
import calculator_service;

using namespace options_calculator;
using namespace options_calculator::service;
using grpc::ServerContext;

class CalculatorServiceTest : public ::testing::Test {
protected:
    CalculatorServiceImpl service;
    ServerContext context;
    CalculationRequest request;
    CalculationResponse response;

    void SetUp() override {
        request.set_symbol("SPY");
        request.set_spot_price(500.0);
        request.set_risk_free_rate(0.05);
        request.set_price_range_percent(0.10);
        request.set_price_steps(5);
        request.set_date_steps(2);
        request.set_compute_pro_probabilities(true);
    }
};

TEST_F(CalculatorServiceTest, ComputeIronCondorPnL) {
    // Construct an Iron Condor
    // Buy Put 480
    auto leg1 = request.add_legs();
    leg1->set_id("L1");
    leg1->set_instrument_type(INSTRUMENT_EQUITY_OPTION);
    leg1->set_option_type(OPTION_TYPE_PUT);
    leg1->set_action(ACTION_BUY);
    leg1->set_strike_price(480.0);
    leg1->set_premium(2.00);
    leg1->set_quantity(1);
    leg1->set_contract_multiplier(100.0);
    leg1->set_implied_volatility(0.20);

    // Sell Put 490
    auto leg2 = request.add_legs();
    leg2->set_id("L2");
    leg2->set_instrument_type(INSTRUMENT_EQUITY_OPTION);
    leg2->set_option_type(OPTION_TYPE_PUT);
    leg2->set_action(ACTION_SELL);
    leg2->set_strike_price(490.0);
    leg2->set_premium(5.00);
    leg2->set_quantity(1);
    leg2->set_contract_multiplier(100.0);
    leg2->set_implied_volatility(0.20);

    // Sell Call 510
    auto leg3 = request.add_legs();
    leg3->set_id("L3");
    leg3->set_instrument_type(INSTRUMENT_EQUITY_OPTION);
    leg3->set_option_type(OPTION_TYPE_CALL);
    leg3->set_action(ACTION_SELL);
    leg3->set_strike_price(510.0);
    leg3->set_premium(4.50);
    leg3->set_quantity(1);
    leg3->set_contract_multiplier(100.0);
    leg3->set_implied_volatility(0.20);

    // Buy Call 520
    auto leg4 = request.add_legs();
    leg4->set_id("L4");
    leg4->set_instrument_type(INSTRUMENT_EQUITY_OPTION);
    leg4->set_option_type(OPTION_TYPE_CALL);
    leg4->set_action(ACTION_BUY);
    leg4->set_strike_price(520.0);
    leg4->set_premium(1.50);
    leg4->set_quantity(1);
    leg4->set_contract_multiplier(100.0);
    leg4->set_implied_volatility(0.20);

    // Execute the service calculation
    auto status = service.ComputeStrategyPnL(&context, &request, &response);
    
    ASSERT_TRUE(status.ok());
    
    // Matrix dimensions should be date_steps * price_steps = 2 * 5 = 10
    EXPECT_EQ(response.matrix_size(), 10);
    
    // Validate Pro Probabilities are populated
    EXPECT_TRUE(response.has_pro_probability_metrics());
    auto& prob = response.pro_probability_metrics();
    EXPECT_DOUBLE_EQ(prob.probability_of_profit(), 0.65);
    EXPECT_DOUBLE_EQ(prob.expected_value(), 15.0);

    // Check Greeks aggregation (smoke test to ensure it didn't crash and returns non-zero values)
    EXPECT_TRUE(response.has_aggregate_greeks());
    auto& greeks = response.aggregate_greeks();
    // Iron condor delta should be roughly neutral near the center
    EXPECT_NE(greeks.gamma(), 0.0);

    // Risk reward should be calculated (since Iron condor has capped profit and loss)
    EXPECT_GT(response.max_profit(), 0.0);
    EXPECT_LT(response.max_loss(), 0.0);
    EXPECT_GT(response.risk_reward_ratio(), 0.0);
}

TEST_F(CalculatorServiceTest, ComputeSpotPosition) {
    auto leg1 = request.add_legs();
    leg1->set_id("SPOT1");
    leg1->set_instrument_type(INSTRUMENT_EQUITY_SPOT);
    leg1->set_action(ACTION_BUY);
    leg1->set_strike_price(500.0); // Entry price
    leg1->set_quantity(100);
    leg1->set_contract_multiplier(1.0);

    auto status = service.ComputeStrategyPnL(&context, &request, &response);
    ASSERT_TRUE(status.ok());
    
    // Spot position has infinite max profit technically, or bounded by price range simulation
    EXPECT_GT(response.max_profit(), 0.0);
    
    // Check specific matrix point for linear payout
    bool found_upside = false;
    for (const auto& cell : response.matrix()) {
        if (cell.price() > 549.0) { // e.g. 550 price target
            // PnL should be roughly (550 - 500) * 100 = 5000
            EXPECT_NEAR(cell.pnl_dollars(), 5000.0, 1.0);
            found_upside = true;
        }
    }
    EXPECT_TRUE(found_upside);
}
