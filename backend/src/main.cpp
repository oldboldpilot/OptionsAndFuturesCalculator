#include <iostream>
#include <memory>
#include <string>
#include <grpcpp/grpcpp.h>

import std;
import calculator_service;

auto RunServer() -> void {
    std::string server_address("0.0.0.0:50051");
    options_calculator::service::CalculatorServiceImpl service;

    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

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
