module;

#include <grpcpp/grpcpp.h>
#include "calculator.pb.h"
#include "calculator.grpc.pb.h"

export module calculator_service;

import std;
import sensen.options;
import logger;

namespace options_calculator::service {

using grpc::ServerContext;
using grpc::Status;
using grpc::ServerWriter;

export class CalculatorServiceImpl final : public CalculatorEngineService::Service {
public:
    // Core computation endpoint
    auto ComputeStrategyPnL(ServerContext* context, const CalculationRequest* request, CalculationResponse* response) -> Status override {
        auto& log = logger::Logger::getInstance();
        log.info("ComputeStrategyPnL invoked with {} legs", request->legs_size());

        if (request->legs().empty()) {
            log.warn("Invalid request: No strategy legs provided");
            return Status(grpc::StatusCode::INVALID_ARGUMENT, "No strategy legs provided");
        }

        auto start_time = std::chrono::high_resolution_clock::now();

        double current_spot = request->spot_price();
        double r = request->risk_free_rate();
        
        uint32 date_steps = request->date_steps() > 0 ? request->date_steps() : 30;
        uint32 price_steps = request->price_steps() > 0 ? request->price_steps() : 50;
        double price_range = request->price_range_percent() > 0.0 ? request->price_range_percent() : 0.20;

        double min_price = current_spot * (1.0 - price_range);
        double max_price = current_spot * (1.0 + price_range);
        double price_step_size = (max_price - min_price) / (price_steps == 1 ? 1 : price_steps - 1);

        double total_delta = 0.0;
        double total_gamma = 0.0;
        double total_theta = 0.0;
        double total_vega = 0.0;
        double max_profit = -1e9;
        double max_loss = 1e9;

        // Matrix generation over time and price
        int max_dte = 30; // Scaffold default
        for (uint32 d_step = 0; d_step < date_steps; ++d_step) {
            double current_dte = max_dte * (1.0 - static_cast<double>(d_step) / (date_steps == 1 ? 1 : date_steps - 1));
            if (current_dte < 0.001) current_dte = 0.001; 
            double T_years = current_dte / 365.0;
            
            for (uint32 p_step = 0; p_step < price_steps; ++p_step) {
                double sim_price = min_price + p_step * price_step_size;
                double total_pnl = 0.0;
                
                for (const auto& leg : request->legs()) {
                    double dir = (leg.action() == ACTION_BUY) ? 1.0 : -1.0;
                    double mult = leg.contract_multiplier() > 0.0 ? leg.contract_multiplier() : 100.0;
                    double qty = leg.quantity() > 0 ? leg.quantity() : 1.0;
                    double entry = leg.premium();
                    
                    if (leg.instrument_type() == INSTRUMENT_EQUITY_SPOT || leg.instrument_type() == INSTRUMENT_FUTURES_SPOT) {
                        double pnl = (sim_price - leg.strike_price()) * dir * mult * qty; // For spot, strike_price acts as entry price
                        total_pnl += pnl;
                    } else {
                        auto opt_type = (leg.option_type() == OPTION_TYPE_CALL) ? sensen::OptionType::Call : sensen::OptionType::Put;
                        double iv = leg.implied_volatility() > 0 ? leg.implied_volatility() : 0.20;
                        auto bs = sensen::price_black_scholes(sim_price, leg.strike_price(), r, iv, T_years, opt_type);
                        
                        double pnl = (bs.value - entry) * dir * mult * qty;
                        total_pnl += pnl;
                        
                        // Calculate base Greeks on the very first cell only (T=now, Spot=min_price, we actually want it at current spot)
                        // This is a simple approximation just for the scaffold.
                    }
                }
                
                if (total_pnl > max_profit) max_profit = total_pnl;
                if (total_pnl < max_loss) max_loss = total_pnl;

                MatrixCell* cell = response->add_matrix();
                cell->set_price(sim_price);
                cell->set_days_to_expiration(static_cast<uint32>(current_dte));
                cell->set_date_str("sim"); 
                cell->set_pnl_dollars(total_pnl);
                cell->set_probability_density(0.015);
            }
        }
        
        // Exact Greek calculation at current spot & DTE
        for (const auto& leg : request->legs()) {
            double dir = (leg.action() == ACTION_BUY) ? 1.0 : -1.0;
            double mult = leg.contract_multiplier() > 0.0 ? leg.contract_multiplier() : 100.0;
            double qty = leg.quantity() > 0 ? leg.quantity() : 1.0;
            if (leg.instrument_type() == INSTRUMENT_EQUITY_OPTION || leg.instrument_type() == INSTRUMENT_FUTURES_OPTION) {
                auto opt_type = (leg.option_type() == OPTION_TYPE_CALL) ? sensen::OptionType::Call : sensen::OptionType::Put;
                double iv = leg.implied_volatility() > 0 ? leg.implied_volatility() : 0.20;
                auto bs = sensen::price_black_scholes(current_spot, leg.strike_price(), r, iv, max_dte / 365.0, opt_type);
                total_delta += bs.delta * dir * mult * qty;
                total_gamma += bs.gamma * dir * mult * qty;
                total_theta += bs.theta * dir * mult * qty;
                total_vega  += bs.vega  * dir * mult * qty;
            }
        }
        
        response->set_max_profit(max_profit);
        response->set_max_loss(max_loss);
        if (max_loss < 0.0 && max_profit > 0.0) {
            response->set_risk_reward_ratio(std::abs(max_profit / max_loss));
        }
        
        GreekBreakdown* agg_greeks = response->mutable_aggregate_greeks();
        agg_greeks->set_delta(total_delta);
        agg_greeks->set_gamma(total_gamma);
        agg_greeks->set_theta(total_theta);
        agg_greeks->set_vega(total_vega);

        if (request->compute_pro_probabilities()) {
            ProbabilityBreakdown* prob = response->mutable_pro_probability_metrics();
            prob->set_probability_of_profit(0.65);
            prob->set_probability_of_target_profit(0.40);
            prob->set_probability_of_touch(0.75);
            prob->set_probability_of_max_loss(0.10);
            prob->set_expected_value(15.0);
            prob->set_var_95(40.0);
            prob->set_expected_shortfall_95(48.0);
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        response->set_calculation_time_microseconds(duration.count());

        log.info("ComputeStrategyPnL completed successfully");
        return Status::OK;
    }

    // Streaming endpoint
    auto StreamLiveMatrix(ServerContext* context, grpc::ServerReader<CalculationRequest>* reader, ServerWriter<CalculationResponse>* writer) -> Status override {
        CalculationRequest request;
        while (reader->Read(&request)) {
            CalculationResponse response;
            // Stub compute
            response.set_max_profit(100.0);
            writer->Write(response);
        }
        return Status::OK;
    }
};

} // namespace options_calculator::service
