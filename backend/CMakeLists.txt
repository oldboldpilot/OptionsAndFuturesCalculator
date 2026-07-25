cmake_minimum_required(VERSION 3.28)
project(OptionsCalculatorEngine CXX)
add_compile_definitions(SENSEN_NO_IMPORT_STD)
add_compile_definitions(_GLIBCXX_USE_TBB_PAR_BACKEND=0)

# Enforce C++23 (and prepare for C++26)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable C++23 modules scanning
set(CMAKE_EXPERIMENTAL_CXX_MODULE_CMAKE_API "2182bf5c-ef0d-489a-91da-49dbc3090d2a")
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

# Canonical build flags from config/cpp_details.txt
# NO fast-math, NO native march
set(CANONICAL_FLAGS "-fPIC -O3 -march=x86-64-v3 -mtune=generic -mavx -mavx2 -mfma -msse2 -msse3 -mssse3 -msse4.1 -msse4.2 -pthread -fstack-protector-strong -DNDEBUG -Wno-error=stringop-overflow -Wno-error=array-bounds -Wno-error=unused-variable -Wno-error=unused-parameter -Wno-error=unused-but-set-variable -Wno-error=pedantic -Wno-error=global-module -Wno-unknown-warning-option -fexperimental-library")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CANONICAL_FLAGS}")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}")

set(gRPC_INSTALL OFF CACHE BOOL "" FORCE)
set(protobuf_INSTALL OFF CACHE BOOL "" FORCE)
set(ABSL_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(gRPC_SSL_PROVIDER "module" CACHE STRING "" FORCE)
set(ENABLE_PARALLEL_STL OFF CACHE BOOL "" FORCE)

include(FetchContent)
FetchContent_Declare(
  grpc
  GIT_REPOSITORY https://github.com/grpc/grpc
  GIT_TAG v1.62.0
)
set(FETCHCONTENT_QUIET OFF)
FetchContent_MakeAvailable(grpc)

# Generate gRPC and Protobuf C++ code
set(PROTO_FILE "${CMAKE_CURRENT_SOURCE_DIR}/proto/calculator.proto")
set(PROTO_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY ${PROTO_GEN_DIR})

add_custom_command(
    OUTPUT "${PROTO_GEN_DIR}/calculator.pb.cc" "${PROTO_GEN_DIR}/calculator.pb.h" "${PROTO_GEN_DIR}/calculator.grpc.pb.cc" "${PROTO_GEN_DIR}/calculator.grpc.pb.h"
    COMMAND $<TARGET_FILE:protoc>
    ARGS --cpp_out=${PROTO_GEN_DIR}
         --grpc_out=${PROTO_GEN_DIR}
         --plugin=protoc-gen-grpc=$<TARGET_FILE:grpc_cpp_plugin>
         -I${CMAKE_CURRENT_SOURCE_DIR}/proto
         ${PROTO_FILE}
    DEPENDS ${PROTO_FILE}
)

add_library(proto_lib STATIC
    "${PROTO_GEN_DIR}/calculator.pb.cc"
    "${PROTO_GEN_DIR}/calculator.grpc.pb.cc"
)
target_include_directories(proto_lib PUBLIC ${PROTO_GEN_DIR})
target_link_libraries(proto_lib PUBLIC grpc++ libprotobuf)

# Include sensen submodule
set(BUILD_PYTHON_BINDINGS OFF CACHE BOOL "Disable sensen python bindings" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "Disable sensen tests" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "Disable sensen examples" FORCE)
set(SKIP_CLANG_TIDY ON CACHE BOOL "Disable clang-tidy due to c++23 module scanning bugs" FORCE)
add_subdirectory(sensen EXCLUDE_FROM_ALL)

# Include StochasticGraphExecutionEngine for distributed execution
add_subdirectory(external/SGEE sgee EXCLUDE_FROM_ALL)

# Include logging and bindings submodules
# add_subdirectory(cpp23-logger EXCLUDE_FROM_ALL)
# find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
# add_subdirectory(nanobind EXCLUDE_FROM_ALL)

# Core engine binary
add_executable(calculator_engine src/main.cpp src/modules/calculator_service.cpp)
target_sources(calculator_engine PUBLIC FILE_SET CXX_MODULES FILES src/modules/calculator_service.cppm src/modules/market_data.cppm)
target_link_libraries(calculator_engine PRIVATE proto_lib sensen fastjson logger httplib::httplib OpenSSL::SSL OpenSSL::Crypto sgee)

# Python Bindings via nanobind
# nanobind_add_module(
#     options_futures_engine
#     src/modules/calculator_service.cppm
#     src/modules/market_data.cppm
#     src/bindings.cpp
# )
# target_link_libraries(options_futures_engine PRIVATE proto_lib sensen fastjson logger)

# Setup testing
enable_testing()

# Setup minimal C++ HTTP Client (cpp-httplib) for native nanosecond-latency HTTP
include(FetchContent)
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.15.3
)
FetchContent_MakeAvailable(httplib)
find_package(OpenSSL REQUIRED)

add_executable(test_runner 
    src/test_main.cpp
)
target_sources(test_runner
    PRIVATE
    FILE_SET CXX_MODULES FILES
        src/modules/testing_framework.cppm
        src/modules/market_data.cppm
        src/modules/pricing_engine.cppm
)
target_link_libraries(test_runner PRIVATE proto_lib sensen fastjson logger httplib::httplib OpenSSL::SSL OpenSSL::Crypto)

add_test(NAME CoreEngineTest COMMAND test_runner)
