module;
#include <grpcpp/grpcpp.h>
#include <span>
#include <string_view>

export module assistant_service;

export namespace options_calculator::assistant {

/**
 * Registers the LLM-backed strategy assistant on a gRPC server builder.
 *
 * Mirrors RegisterFinanceService's shape exactly, and for the same reason:
 * the builder is taken by reference rather than type-erased, because the
 * implementation needs gRPC regardless (config/cpp_details.txt rule 3).
 *
 * This is a THIRD, independent contract sharing the port with
 * calculator.OptionsCalculator and sensen.finance.Finance. Those two speak in
 * deterministic, auditable arithmetic; this one wraps a fine-tuned language
 * model that turns a trader's free-text utterance into the structured
 * parameters CalculateStrategy actually needs. See proto/assistant.proto's
 * file banner for why a clarifying question and a refusal are both OK
 * outcomes of this RPC rather than errors, and why real-data-only policy
 * extends to intent here just as it does to market quotes everywhere else in
 * this codebase.
 */
auto RegisterAssistantService(grpc::ServerBuilder& builder) -> void;

/**
 * TEST-ONLY registration hook. Builds and registers the SAME
 * StrategyAssistantWorkflow graph as RegisterAssistantService above, but
 * binds only the action names listed in `bound_action_names` instead of
 * always binding the full set ("Admission", "CheckModel", "Generate",
 * "ParseAndVerify").
 *
 * Mirrors RegisterCalculatorServiceForTest (calculator_service.cppm) exactly,
 * and for the identical reason: it lets a test reproduce, through the real
 * ParseStrategy RPC, the silent-halt failure mode a partial or renamed
 * action registry produces, and prove that the postcondition after
 * Interpreter::Run() actually catches it. Production never constructs one of
 * these with a partial set.
 */
auto RegisterAssistantServiceForTest(grpc::ServerBuilder& builder,
                                     std::span<const std::string_view> bound_action_names)
    -> void;

}  // namespace options_calculator::assistant
