module;
#include <grpcpp/grpcpp.h>

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

}  // namespace options_calculator::mortgage_assistant
