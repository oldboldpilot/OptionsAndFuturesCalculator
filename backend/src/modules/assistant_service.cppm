module;
#include <grpcpp/grpcpp.h>

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

}  // namespace options_calculator::assistant
