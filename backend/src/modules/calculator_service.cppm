module;
#include <grpcpp/grpcpp.h>
#include <span>
#include <string_view>

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

/**
 * TEST-ONLY registration hook. Builds and registers the SAME OptionsWorkflow
 * graph as RegisterCalculatorService above, but binds only the action names
 * listed in `bound_action_names` instead of always binding the full set
 * ("Initialize", "ComputeExpiryCurve", "ComputeMatrix", "ComputeGreeks",
 * "ComputeProbabilities").
 *
 * Exists so a test can reproduce, through the real CalculateStrategy RPC,
 * exactly the failure mode a partial or renamed action registry produces --
 * e.g. a submodule bump in SGEE that drops or renames one action -- and
 * prove that the postconditions after interpreter.Run() actually catch it,
 * rather than merely asserting they exist. RegisterCalculatorService is
 * never implemented in terms of this function and always binds every
 * action; production code has no path that can call this with a partial
 * set.
 *
 * Deliberately leaks the service instance it constructs (see the .cpp for
 * why): this is only ever called from short-lived test binaries that build
 * one in-process grpc::Server per test case and exit soon after.
 */
auto RegisterCalculatorServiceForTest(grpc::ServerBuilder& builder,
                                      std::span<const std::string_view> bound_action_names)
    -> void;

}  // namespace options_calculator::service
