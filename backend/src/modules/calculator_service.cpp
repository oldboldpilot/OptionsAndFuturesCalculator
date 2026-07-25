module;
#include <string>
#include <vector>
#include <memory>
#include <iostream>
#include <cmath>
#include <chrono>
#include <thread>
#include <variant>
#include <map>
#include <unordered_map>

#include <grpcpp/grpcpp.h>
#include "calculator.pb.h"
#include "calculator.grpc.pb.h"

#include <future>
#include <expected>
#include <string_view>
#include <sstream>

module calculator_service;

import sensen.options;
import logger;

import sgee.builder.fluent;
import sgee.runtime.context;
import sgee.runtime.interpreter;
import sgee.runtime.pipeline;
import sgee.core.types;


namespace options_calculator::service {

using grpc::ServerContext;
using grpc::Status;
using grpc::ServerWriter;

struct ComputeContext {
    CalculationRequest request;
    CalculationResponse response;
    grpc::Status status{grpc::Status::OK};
    std::shared_ptr<std::promise<void>> promise;

    // Intermediate state
    double current_spot{0.0};
    double r{0.0};
    uint32_t date_steps{0};
    uint32_t price_steps{0};
    double price_range{0.0};
    double min_price{0.0};
    double max_price{0.0};
    double price_step_size{0.0};
    int max_dte{30};

    // Workflow Control
    int retry_count{0};
    const int max_retries{3};
};

using PipelineType = sgee::runtime::TransformedPipeline<std::shared_ptr<ComputeContext>, std::shared_ptr<ComputeContext>>;

class CalculatorServiceImpl final : public CalculatorEngineService::Service {
private:
    std::unique_ptr<PipelineType> execution_engine_;

public:
    CalculatorServiceImpl() {
        auto graph_result = sgee::Builder<std::shared_ptr<ComputeContext>>("OptionsWorkflow")
            .Node("Initialize")
                .Execute([](std::shared_ptr<ComputeContext>& ctx) {
                    ctx->current_spot = ctx->request.spot_price();
                    ctx->r = ctx->request.risk_free_rate();
                    ctx->date_steps = ctx->request.date_steps() > 0 ? ctx->request.date_steps() : 30;
                    ctx->price_steps = ctx->request.price_steps() > 0 ? ctx->request.price_steps() : 50;
                    ctx->price_range = ctx->request.price_range_percent() > 0.0 ? ctx->request.price_range_percent() : 0.20;
                    ctx->min_price = ctx->current_spot * (1.0 - ctx->price_range);
                    ctx->max_price = ctx->current_spot * (1.0 + ctx->price_range);
                    ctx->price_step_size = (ctx->max_price - ctx->min_price) / (ctx->price_steps == 1 ? 1 : ctx->price_steps - 1);
                    ctx->status = grpc::Status::OK;
                })
                .Branch([](const std::shared_ptr<ComputeContext>& ctx) {
                    return ctx->request.legs_size() > 0;
                })
                    .OnTrue("ComputePnL")
                    .OnFalse("FailedValidation")
            
            .Node("FailedValidation")
                .Execute([](std::shared_ptr<ComputeContext>& ctx) {
                    ctx->status = grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "No strategy legs provided");
                })
                .Next("Failed")

            .Node("ComputePnL")
                .Execute([](std::shared_ptr<ComputeContext>& ctx) {
                    try {
                        auto start_time = std::chrono::high_resolution_clock::now();
                        double max_profit = -1e9;
                        double max_loss = 1e9;

                        struct SimulationContext {
                            double sim_price{0.0};
                            double current_dte{0.0};
                            double T_years{0.0};
                            double min_price{0.0};
                            const CalculationRequest* request{nullptr};
                            double current_spot{0.0};
                            double r{0.0};
                            double total_pnl{0.0};
                            uint32_t d_step{0};
                            uint32_t p_step{0};
                        };

                        auto compute_pnl = [&](SimulationContext& sim_ctx) -> std::expected<double, std::string> {
                            double total_pnl = 0.0;
                            for (const auto& leg : sim_ctx.request->legs()) {
                                double dir = (leg.action() == ACTION_BUY) ? 1.0 : -1.0;
                                double mult = leg.contract_multiplier() > 0.0 ? leg.contract_multiplier() : 100.0;
                                double qty = leg.quantity() > 0 ? leg.quantity() : 1.0;
                                double entry = leg.premium();
                                
                                if (leg.instrument_type() == INSTRUMENT_EQUITY_SPOT || leg.instrument_type() == INSTRUMENT_FUTURES_SPOT) {
                                    double pnl = (sim_ctx.sim_price - leg.strike_price()) * dir * mult * qty;
                                    total_pnl += pnl;
                                } else {
                                    auto opt_type = (leg.option_type() == OPTION_TYPE_CALL) ? sensen::OptionType::Call : sensen::OptionType::Put;
                                    double iv = leg.implied_volatility() > 0 ? leg.implied_volatility() : 0.20;
                                    
                                    // Simulated failure to demonstrate retry logic
                                    if (iv < 0.001) {
                                        return std::unexpected("Implied volatility too low, numerical instability");
                                    }

                                    auto bs = sensen::price_black_scholes(sim_ctx.sim_price, leg.strike_price(), sim_ctx.r, iv, sim_ctx.T_years, opt_type);
                                    double pnl = (bs.value - entry) * dir * mult * qty;
                                    total_pnl += pnl;
                                }
                            }
                            return total_pnl;
                        };

                        std::vector<SimulationContext> entities;
                        entities.reserve(ctx->date_steps * ctx->price_steps);

                        for (uint32_t d_step = 0; d_step < ctx->date_steps; ++d_step) {
                            double current_dte = ctx->max_dte * (1.0 - static_cast<double>(d_step) / (ctx->date_steps == 1 ? 1 : ctx->date_steps - 1));
                            if (current_dte < 0.001) current_dte = 0.001; 
                            double T_years = current_dte / 365.0;
                            
                            for (uint32_t p_step = 0; p_step < ctx->price_steps; ++p_step) {
                                double sim_price = ctx->min_price + p_step * ctx->price_step_size;
                                entities.push_back(SimulationContext{
                                    .sim_price = sim_price,
                                    .current_dte = current_dte,
                                    .T_years = T_years,
                                    .min_price = ctx->min_price,
                                    .request = &ctx->request,
                                    .current_spot = ctx->current_spot,
                                    .r = ctx->r,
                                    .total_pnl = 0.0,
                                    .d_step = d_step,
                                    .p_step = p_step
                                });
                            }
                        }

                        for (auto& sim_ctx : entities) {
                            auto result = compute_pnl(sim_ctx);
                            if (!result) {
                                throw std::runtime_error(result.error());
                            }
                            sim_ctx.total_pnl = result.value();
                        }

                        for (const auto& sim_ctx : entities) {
                            if (sim_ctx.total_pnl > max_profit) max_profit = sim_ctx.total_pnl;
                            if (sim_ctx.total_pnl < max_loss) max_loss = sim_ctx.total_pnl;

                            MatrixCell* cell = ctx->response.add_matrix();
                            cell->set_price(sim_ctx.sim_price);
                            cell->set_days_to_expiration(static_cast<uint32_t>(sim_ctx.current_dte));
                            cell->set_date_str("sim"); 
                            cell->set_pnl_dollars(sim_ctx.total_pnl);
                            cell->set_probability_density(0.015);
                        }

                        ctx->response.set_max_profit(max_profit);
                        ctx->response.set_max_loss(max_loss);
                        if (max_loss < 0.0 && max_profit > 0.0) {
                            ctx->response.set_risk_reward_ratio(std::abs(max_profit / max_loss));
                        }
                        
                        auto end_time = std::chrono::high_resolution_clock::now();
                        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
                        ctx->response.set_calculation_time_microseconds(duration.count());

                        ctx->status = grpc::Status::OK;
                    } catch (const std::exception& e) {
                        ctx->status = grpc::Status(grpc::StatusCode::INTERNAL, e.what());
                    }
                })
                .Branch([](const std::shared_ptr<ComputeContext>& ctx) {
                    return ctx->status.ok();
                })
                    .OnTrue("ComputeGreeks")
                    .OnFalse("RetryStrategy")

            .Node("RetryStrategy")
                .Execute([](std::shared_ptr<ComputeContext>& ctx) {
                    ctx->retry_count++;
                    logger::Logger::getInstance().warn("Retrying computation. Attempt {}/{}", ctx->retry_count, ctx->max_retries);
                    ctx->response.clear_matrix();
                })
                .Branch([](const std::shared_ptr<ComputeContext>& ctx) {
                    return ctx->retry_count < ctx->max_retries;
                })
                    .OnTrue("ComputePnL")
                    .OnFalse("Failed")

            .Node("ComputeGreeks")
                .Execute([](std::shared_ptr<ComputeContext>& ctx) {
                    double total_delta = 0.0;
                    double total_gamma = 0.0;
                    double total_theta = 0.0;
                    double total_vega = 0.0;
                    
                    for (const auto& leg : ctx->request.legs()) {
                        double dir = (leg.action() == ACTION_BUY) ? 1.0 : -1.0;
                        double mult = leg.contract_multiplier() > 0.0 ? leg.contract_multiplier() : 100.0;
                        double qty = leg.quantity() > 0 ? leg.quantity() : 1.0;
                        if (leg.instrument_type() == INSTRUMENT_EQUITY_OPTION || leg.instrument_type() == INSTRUMENT_FUTURES_OPTION) {
                            auto opt_type = (leg.option_type() == OPTION_TYPE_CALL) ? sensen::OptionType::Call : sensen::OptionType::Put;
                            double iv = leg.implied_volatility() > 0 ? leg.implied_volatility() : 0.20;
                            auto bs = sensen::price_black_scholes(ctx->current_spot, leg.strike_price(), ctx->r, iv, ctx->max_dte / 365.0, opt_type);
                            total_delta += bs.delta * dir * mult * qty;
                            total_gamma += bs.gamma * dir * mult * qty;
                            total_theta += bs.theta * dir * mult * qty;
                            total_vega  += bs.vega  * dir * mult * qty;
                        } else if (leg.instrument_type() == INSTRUMENT_EQUITY_SPOT || leg.instrument_type() == INSTRUMENT_FUTURES_SPOT) {
                            total_delta += 1.0 * dir * mult * qty;
                        }
                    }
                    
                    GreekBreakdown* agg_greeks = ctx->response.mutable_aggregate_greeks();
                    agg_greeks->set_delta(total_delta);
                    agg_greeks->set_gamma(total_gamma);
                    agg_greeks->set_theta(total_theta);
                    agg_greeks->set_vega(total_vega);
                })
                .Next("Success")

            .Node("Success")
                .IsTerminal()
            
            .Node("Failed")
                .IsTerminal()
            
            .Build();

        if (!graph_result) {
            logger::Logger::getInstance().error("Failed to build SGEE Graph: {}", graph_result.error());
            return;
        }

        auto graph = graph_result.value();

        // Initialize SGEE Pipeline Queue with Backpressure
        auto builder = PipelineType::create()
            .withTransform([graph](const std::shared_ptr<ComputeContext>& ctx) -> std::shared_ptr<ComputeContext> {
                // SGEE Context Engine instantiation per request batch
                sgee::runtime::EngineContext<std::shared_ptr<ComputeContext>> engine;
                std::vector<std::shared_ptr<ComputeContext>> entities{ctx};
                engine.Load(entities);

                sgee::runtime::Interpreter<std::shared_ptr<ComputeContext>> interpreter(graph);
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
            execution_engine_->startWorkers([](std::shared_ptr<ComputeContext>& ctx) {
                ctx->promise->set_value();
            });
            logger::Logger::getInstance().info("SGEE Pipeline Execution Engine initialized with 16 workers and backpressure policy: Block");
        } else {
            logger::Logger::getInstance().error("Failed to build SGEE Pipeline: {}", builder.error());
        }
    }

    ~CalculatorServiceImpl() {
        if (execution_engine_) {
            execution_engine_->stop();
        }
    }

    // Core computation endpoint
    auto ComputeStrategyPnL(ServerContext* context, const CalculationRequest* request, CalculationResponse* response) -> Status override {
        auto& log = logger::Logger::getInstance();
        log.info("ComputeStrategyPnL invoked with {} legs", request->legs_size());

        // We no longer validate here, the state machine handles it
        // The gRPC method only pushes to the engine and blocks

        if (!execution_engine_) {
            return Status(grpc::StatusCode::INTERNAL, "Execution engine not initialized");
        }

        auto ctx = std::make_shared<ComputeContext>();
        ctx->request = *request;
        ctx->promise = std::make_shared<std::promise<void>>();
        auto future = ctx->promise->get_future();

        // Enqueue the request to SGEE pipeline
        bool queued = execution_engine_->push(ctx);
        if (!queued) {
            log.warn("Backpressure applied: Execution Engine Queue Full");
            return Status(grpc::StatusCode::RESOURCE_EXHAUSTED, "Server is currently overloaded. Backpressure applied.");
        }

        // Wait for asynchronous processing by SGEE worker pool
        future.wait();

        if (!ctx->status.ok()) {
            return ctx->status;
        }

        *response = ctx->response;
        log.info("ComputeStrategyPnL completed via SGEE Engine");
        return Status::OK;
    }

    // Streaming endpoint
    auto StreamLiveMatrix(ServerContext* context, grpc::ServerReaderWriter<CalculationResponse, CalculationRequest>* stream) -> Status override {
        CalculationRequest request;
        while (stream->Read(&request)) {
            CalculationResponse response;
            // Stub compute
            response.set_max_profit(100.0);
            stream->Write(response);
        }
        return Status::OK;
    }
};

void RegisterCalculatorService(void* builder_ptr) {
    auto builder = static_cast<grpc::ServerBuilder*>(builder_ptr);
    // Allocate the service statically so it lives for the lifetime of the server
    static CalculatorServiceImpl service;
    builder->RegisterService(&service);
}

} // namespace options_calculator::service
