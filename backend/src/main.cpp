#include <iostream>
#include <memory>
#include <string>
#include <grpcpp/grpcpp.h>


import calculator_service;
import finance_service;

auto RunServer() -> void {
    std::string server_address("0.0.0.0:50051");
    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());

    options_calculator::service::RegisterCalculatorService(builder);

    // The sensen financial library, on the same port under its own contract.
    // Two services, one process: a caller that wants amortization schedules or
    // bond analytics does not have to know this application exists, and this
    // application does not have to grow a second deployment to offer them.
    options_calculator::finance::RegisterFinanceService(builder);

    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    std::cout << "Server listening on " << server_address << std::endl;

    server->Wait();
}

auto main(int argc, char** argv) -> int {
    try {
        RunServer();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
}
