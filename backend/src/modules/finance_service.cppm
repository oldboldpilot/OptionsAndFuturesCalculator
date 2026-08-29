module;
#include <grpcpp/grpcpp.h>

export module finance_service;

import std;
import state_refresh;

export namespace options_calculator::finance {

/**
 * Registers the sensen financial service on a gRPC server builder.
 *
 * Mirrors RegisterCalculatorService exactly, and for the same reason: the
 * builder is taken by reference rather than type-erased, because the
 * implementation needs gRPC regardless and casting it back inside would defeat
 * the type system for nothing (config/cpp_details.txt rule 3).
 *
 * Both services live on one server and one port. They are separate CONTRACTS,
 * not separate processes -- calculator.proto speaks this application's language
 * and finance.proto exposes the general-purpose library underneath, which is a
 * reason to keep the protos apart, not the binaries.
 */
/**
 * The state-assumptions job, injected rather than linked.
 *
 * Empty hooks leave those two RPCs answering FAILED_PRECONDITION, which is what
 * the pricing test targets pass -- they carry no libpq, and merely TAKING THE
 * ADDRESS of the real implementation inside finance_service.cpp is enough to
 * break their link. Same reason calculator_service takes an IStrategyStore.
 */
struct StateRefreshHooks {
    std::function<std::expected<state_refresh::Outcome, state_refresh::Refusal>(bool, std::int32_t)>
        refresh;
    std::function<
        std::expected<std::vector<state_refresh::StoredAssumption>, state_refresh::Refusal>(
            std::string_view)>
        read;
};

auto RegisterFinanceService(grpc::ServerBuilder& builder, StateRefreshHooks hooks = {}) -> void;

}  // namespace options_calculator::finance
