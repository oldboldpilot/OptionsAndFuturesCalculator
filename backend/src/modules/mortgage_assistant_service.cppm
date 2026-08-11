module;
#include <grpcpp/grpcpp.h>
#include <span>
#include <string_view>

export module mortgage_assistant_service;

export namespace options_calculator::mortgage_assistant {

/**
 * Registers the LLM-backed mortgage / time-value-of-money assistant on a gRPC
 * server builder.
 *
 * @author Olumuyiwa Oluwasanmi
 *
 * Mirrors RegisterAssistantService's shape exactly, and for the same reason:
 * the builder is taken by reference rather than type-erased, because the
 * implementation needs gRPC regardless (config/cpp_details.txt rule 3).
 *
 * This is a FOURTH, independent contract sharing the port with
 * calculator.OptionsCalculator, sensen.finance.Finance and
 * calculator.assistant.StrategyAssistant. It is the sibling of that last one,
 * not a mode of it: a SECOND fine-tuned model, with its own weights, its own
 * training system prompt, its own label space (the 26 in-scope RPCs of
 * sensen.finance.Finance rather than 48 option/futures strategies), its own
 * pipeline instance and -- load-bearing -- its own owner thread. See
 * proto/mortgage_assistant.proto's file banner for why the two assistants are
 * separate contracts, and for why a clarifying question and a refusal are both
 * OK outcomes of this RPC rather than errors.
 *
 * Like the strategy assistant, this service is OPTIONAL and must never prevent
 * the process from starting: with MORTGAGE_MODEL_PATH unset, or with a model
 * that fails to load, it registers anyway and answers every call with a
 * MODEL_UNAVAILABLE refusal. A calculator that stops serving payoff curves
 * because an assistant could not find its weights would be trading the product
 * for a feature.
 */
auto RegisterMortgageAssistantService(grpc::ServerBuilder& builder) -> void;

/**
 * TEST-ONLY registration hook, mirroring RegisterAssistantServiceForTest
 * (assistant_service.cppm) and RegisterCalculatorServiceForTest
 * (calculator_service.cppm) exactly: builds and registers the SAME
 * MortgageAssistantWorkflow graph as RegisterMortgageAssistantService above,
 * but binds only the action names listed in `bound_action_names` ("Admission",
 * "CheckModel", "Generate", "ParseAndVerify") instead of always binding the
 * full set. Exists so a test can reproduce, through the real ParseOperation
 * RPC, the silent-halt failure mode a partial or renamed action registry
 * produces. Production never constructs one of these with a partial set.
 */
auto RegisterMortgageAssistantServiceForTest(grpc::ServerBuilder& builder,
                                             std::span<const std::string_view> bound_action_names)
    -> void;

}  // namespace options_calculator::mortgage_assistant
