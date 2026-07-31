module;
#include <grpcpp/grpcpp.h>

export module finance_service;

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
auto RegisterFinanceService(grpc::ServerBuilder& builder) -> void;

}  // namespace options_calculator::finance
