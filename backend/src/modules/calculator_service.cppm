module;
#include <grpcpp/grpcpp.h>

export module calculator_service;

export namespace options_calculator::service {

/**
 * Registers the calculator service on a gRPC server builder.
 *
 * Takes a reference, not a `void*`. The previous signature type-erased the
 * builder to avoid pulling gRPC into this interface and cast it back inside —
 * which defeated the type system for no benefit, since the implementation
 * needs gRPC anyway. Per config/cpp_details.txt rule 3, an interface we own
 * does not traffic in raw pointers.
 */
auto RegisterCalculatorService(grpc::ServerBuilder& builder) -> void;

}  // namespace options_calculator::service
