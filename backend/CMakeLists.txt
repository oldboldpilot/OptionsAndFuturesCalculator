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
set(ENABLE_PARALLEL_STL OFF CACHE BOOL "" FORCE)

# TLS provider: system OpenSSL for every target in the build.
#
# This was "module", which makes gRPC compile and statically link its vendored
# BoringSSL. cpp-httplib links system OpenSSL. Both export SSL_CTX_new and the
# other libssl symbols, so httplib's calls resolved into BoringSSL's
# implementation, whose structures have a different layout — every outbound
# HTTPS request segfaulted inside SSL_CTX_new. One TLS library per binary.
find_package(OpenSSL REQUIRED)
set(gRPC_SSL_PROVIDER "package" CACHE STRING "" FORCE)

# OpenSSL 3.5 removed the long-deprecated ENGINE API along with its header.
# gRPC includes <openssl/engine.h> unconditionally even though every use of the
# API sits behind `#if !defined(OPENSSL_IS_BORINGSSL) && !defined(OPENSSL_NO_ENGINE)`,
# so on such a system the include is the only thing that fails. Defining
# OPENSSL_NO_ENGINE compiles the whole path out, and an empty shim header
# satisfies the include. Ubuntu 24.04 (the deploy image) still ships engine.h,
# so this only engages on newer hosts and keeps local and CI builds identical.
include(CheckIncludeFileCXX)
set(CMAKE_REQUIRED_INCLUDES "${OPENSSL_INCLUDE_DIR}")
check_include_file_cxx("openssl/engine.h" HAVE_OPENSSL_ENGINE_H)
unset(CMAKE_REQUIRED_INCLUDES)
if(NOT HAVE_OPENSSL_ENGINE_H)
    message(STATUS "openssl/engine.h absent (OpenSSL >= 3.5); shimming and defining OPENSSL_NO_ENGINE")
    set(OPENSSL_COMPAT_DIR "${CMAKE_CURRENT_BINARY_DIR}/openssl-compat")
    file(WRITE "${OPENSSL_COMPAT_DIR}/openssl/engine.h"
         "#pragma once\n"
         "/* Generated shim. The ENGINE API was removed in OpenSSL 3.5; every\n"
         "   consumer of it in this build is compiled out by OPENSSL_NO_ENGINE. */\n")
    add_compile_definitions(OPENSSL_NO_ENGINE)
    include_directories(SYSTEM "${OPENSSL_COMPAT_DIR}")
endif()

# CMake 4 removed compatibility with `cmake_minimum_required(VERSION < 3.5)`,
# and gRPC 1.62 vendors a c-ares that still declares 3.0. Without this a fresh
# configure fails outright; an existing build tree only kept working because
# the subproject had been configured by an older CMake and cached.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Floor for vendored subprojects" FORCE)

# gRPC 1.62 (Feb 2024) predates Clang 20's stricter C++ conformance and does not
# compile with it unpatched. Three genuine defects, all fatal here:
#
#   basic_seq.h              `Traits::template CallSeqFactory(...)` — the
#                            `template` keyword with no argument list, a
#                            default-error diagnostic since Clang 19.
#   server.cc                RegisteredMethod's constructor/destructor defined
#   health_check_client.cc   in-class while referencing a nested type that is
#                            not complete until later in the file.
#
# The patch moves the two definitions out-of-line and drops the stray keyword.
# It MUST live in the repo: a local build tree that happens to be patched by
# hand still builds, but every fresh clone — including the Railway container —
# re-fetches pristine sources and fails. Applied idempotently so re-running
# configure against an already-patched tree is a no-op rather than an error.
set(GRPC_PATCH "${CMAKE_CURRENT_SOURCE_DIR}/patches/grpc-1.62-clang20-conformance.patch")

include(FetchContent)
FetchContent_Declare(
  grpc
  GIT_REPOSITORY https://github.com/grpc/grpc
  GIT_TAG v1.62.0
  PATCH_COMMAND sh -c "git apply --reverse --check '${GRPC_PATCH}' 2>/dev/null || git apply '${GRPC_PATCH}'"
)
set(FETCHCONTENT_QUIET OFF)

# Build gRPC at C++17 — the standard it actually supports and tests — rather
# than inheriting this project's C++23.
#
# Under C++23 the xds certificate provider stops compiling: std::bind_front's
# result is no longer convertible to std::function, because C++23 constrains the
# call wrapper's call operator in a way libstdc++ 14 does not satisfy through
# std::function's const operator(). That is one instance of a general problem —
# a 2024 codebase compiled against a standard released after it. Chasing each
# occurrence with another patch is unbounded; building it as its own authors do
# is not.
#
# Safe to mix: both sides use the same libstdc++, and Abseil's ABI-significant
# split is pre-C++17 vs C++17-and-later, so 17 and 23 agree on the aliases
# (string_view, optional, variant) that cross the boundary. This is also the
# ordinary arrangement for any distro-packaged gRPC.
#
# Side benefit: C++17 targets are not scanned for modules, so the global
# CXX_SCAN_FOR_MODULES no longer walks ~3000 gRPC sources looking for imports
# none of them have.
set(SAVED_CXX_STANDARD ${CMAKE_CXX_STANDARD})
set(CMAKE_CXX_STANDARD 17)
FetchContent_MakeAvailable(grpc)
set(CMAKE_CXX_STANDARD ${SAVED_CXX_STANDARD})

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
# sensen defaults BUILD_BENCHMARKS to ON and calls add_subdirectory(benchmarks)
# unconditionally when it is set. The deploy image has no use for them, and
# .dockerignore excludes backend/sensen/benchmarks from the build context, so
# leaving this on fails configure outright inside the container.
set(BUILD_BENCHMARKS OFF CACHE BOOL "Disable sensen benchmarks" FORCE)
set(SKIP_CLANG_TIDY ON CACHE BOOL "Disable clang-tidy due to c++23 module scanning bugs" FORCE)
add_subdirectory(sensen EXCLUDE_FROM_ALL)

# Include StochasticGraphExecutionEngine for distributed execution.
#
# SGEE's own standard is `import std;` against libc++, with std.pcm built from
# libc++'s module sources — that is how it builds standalone and how it is
# documented. This tree cannot use that: sensen is built against libstdc++ (see
# SENSEN_NO_IMPORT_STD at the top of this file), and linking libc++ and libstdc++
# into one binary is an ABI conflict rather than a matter of taste. So SGEE is
# switched to its header-shim path here, which leaves the standard library
# selection alone. The compiler is clang either way; only the standard library
# and the std-module mechanism differ.
set(SGEE_NO_IMPORT_STD ON CACHE BOOL "Build SGEE against libstdc++ via its header shim" FORCE)

# SGEE needs the directory holding the logger BMI when a parent already provides
# the `logger` target, and only this tree knows where that is. Previously this
# path was hardcoded inside SGEE's own CMakeLists, which made SGEE unbuildable
# for anyone whose layout differed.
set(SGEE_LOGGER_MODULE_DIR
    "${CMAKE_BINARY_DIR}/sensen/external/fastestjsoninthewest/external/cpp23-logger/CMakeFiles/logger.dir"
    CACHE PATH "Directory containing the logger module BMI for SGEE" FORCE)

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

# End-to-end smoke client. Verifies a running engine returns live data on all
# three RPCs; used as the deploy gate.
add_executable(smoke_client src/smoke_client.cpp)
target_link_libraries(smoke_client PRIVATE proto_lib)
