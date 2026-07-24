cmake_minimum_required(VERSION 3.28)
project(OptionsCalculatorEngine CXX)

# Enforce C++23 (and prepare for C++26)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable C++23 modules scanning
set(CMAKE_EXPERIMENTAL_CXX_MODULE_CMAKE_API "2182bf5c-ef0d-489a-91da-49dbc3090d2a")
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

# Canonical build flags from config/cpp_details.txt
# NO fast-math, NO native march
set(CANONICAL_FLAGS "-stdlib=libc++ -fPIC -O3 -march=x86-64-v3 -mtune=generic -mavx -mavx2 -mfma -pthread -fstack-protector-strong -DNDEBUG -D_LIBCPP_ENABLE_EXPERIMENTAL -fexperimental-library")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CANONICAL_FLAGS}")

# Find gRPC & Protobuf
find_package(gRPC CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)

# Generate gRPC and Protobuf C++ code
set(PROTO_FILE "${CMAKE_CURRENT_SOURCE_DIR}/proto/calculator.proto")
set(PROTO_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY ${PROTO_GEN_DIR})

add_custom_command(
    OUTPUT "${PROTO_GEN_DIR}/calculator.pb.cc" "${PROTO_GEN_DIR}/calculator.pb.h" "${PROTO_GEN_DIR}/calculator.grpc.pb.cc" "${PROTO_GEN_DIR}/calculator.grpc.pb.h"
    COMMAND $<TARGET_FILE:protobuf::protoc>
    ARGS --cpp_out=${PROTO_GEN_DIR}
         --grpc_out=${PROTO_GEN_DIR}
         --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
         -I${CMAKE_CURRENT_SOURCE_DIR}/proto
         ${PROTO_FILE}
    DEPENDS ${PROTO_FILE}
)

add_library(proto_lib STATIC
    "${PROTO_GEN_DIR}/calculator.pb.cc"
    "${PROTO_GEN_DIR}/calculator.grpc.pb.cc"
)
target_include_directories(proto_lib PUBLIC ${PROTO_GEN_DIR})
target_link_libraries(proto_lib PUBLIC gRPC::grpc++ protobuf::libprotobuf)

# Include sensen submodule
set(BUILD_PYTHON_BINDINGS OFF CACHE BOOL "Disable sensen python bindings" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "Disable sensen tests" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "Disable sensen examples" FORCE)
add_subdirectory(sensen EXCLUDE_FROM_ALL)

# Include StochasticGraphExecutionEngine for distributed execution
add_subdirectory(StochasticGraphExecutionEngine EXCLUDE_FROM_ALL)

# Include logging and bindings submodules
add_subdirectory(cpp23-logger EXCLUDE_FROM_ALL)
add_subdirectory(nanobind EXCLUDE_FROM_ALL)

# Core engine binary
add_executable(calculator_engine src/main.cpp src/modules/calculator_service.cppm src/modules/market_data.cppm)
target_link_libraries(calculator_engine PRIVATE proto_lib sensen fastjson logger sgee)

# Python Bindings via nanobind
nanobind_add_module(
    options_futures_engine
    src/modules/calculator_service.cppm
    src/modules/market_data.cppm
    src/bindings.cpp
)
target_link_libraries(options_futures_engine PRIVATE proto_lib sensen fastjson logger sgee)

# Setup testing
enable_testing()
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(calculator_tests tests/test_calculator_service.cpp src/modules/calculator_service.cppm)
target_link_libraries(calculator_tests PRIVATE proto_lib sensen gtest_main)
include(GoogleTest)
gtest_discover_tests(calculator_tests)
