cmake_minimum_required(VERSION 3.28)
project(OptionsCalculatorEngine CXX)

# SENSEN_NO_IMPORT_STD is deliberately NOT defined here any more, which is why
# this comment survives the line it used to explain.
#
# It selected sensen's HEADER path (utils/sensen_std.h) over `import std;` -- a
# question about the module system, not about libc++ versus libstdc++. The
# argument for keeping it was that no target in this tree would then need a
# std.pcm, so the BMI-compatibility class of failure (a std module compiled with
# one set of flags being rejected by a consumer compiled with another) could not
# arise. That is true, and it is not worth what it cost: it also meant nothing in
# a C++23 codebase could say `import std;`, which is what this project's own code
# policy specifies.
#
# The failure it avoided needs TWO std modules built from two flag sets. This
# tree now builds exactly one -- sensen's std_module_precompile, from
# ${CMAKE_CXX_FLAGS}, which is CANONICAL_FLAGS below and therefore the union of
# what every subdirectory compiles with. SGEE imports that same file rather than
# building its own (SGEE_EXTERNAL_STD_PCM), and so does this directory. One BMI
# cannot disagree with itself.
#
# Nothing needs to be defined here to get the header path back: sensen re-adds
# SENSEN_NO_IMPORT_STD itself on Windows/MSVC, on Windows/Clang, and under
# -DSENSEN_DISABLE_IMPORT_STD=ON. On Linux + Clang none of those apply, which is
# why this one line was the whole switch.
#
# 262 of sensen's 263 module interface units already carry the
# `#if defined(SENSEN_NO_IMPORT_STD)` guard, so this flips them onto the import
# they were written for. The one that does not (src/gemm_fusion_tree_dp.cppm)
# keeps its textual headers and is unaffected -- a header-std BMI and an
# `import std;` consumer interoperate, because std::string from a textual header
# and from the std module are the same entity.

# libc++ tags most of its inline, header-only functions with
# __attribute__((__abi_tag__(<odr-signature>))) so that two objects built
# against different libc++ configurations cannot silently share a definition.
# The tag is part of the mangled name. That is exactly why this define has to be
# set HERE, at the top level, rather than left to whoever happens to set it:
# sensen/CMakeLists.txt:351 defines it unconditionally for everything in its own
# directory scope, so fastjson and the logger get it, while proto_lib,
# sensen_slim, calculator_engine, smoke_client, sgee and the whole FetchContent'd
# gRPC subtree would not. Half a binary mangling std:: inline functions with an
# ABI tag and half without is an ODR violation between objects that otherwise
# look interchangeable -- the linker either fails on a symbol that plainly
# should exist, or picks one definition arbitrarily and the mismatch survives to
# runtime. Under libstdc++ this define was inert, which is why it was previously
# safe to leave to sensen alone; that is no longer true.
add_compile_definitions(_LIBCPP_NO_ABI_TAG)

# Enforce C++23 (and prepare for C++26)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable C++23 modules scanning
set(CMAKE_EXPERIMENTAL_CXX_MODULE_CMAKE_API "2182bf5c-ef0d-489a-91da-49dbc3090d2a")
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

# Canonical build flags from config/cpp_details.txt
# NO fast-math, NO native march
#
# -stdlib=libc++ is the standard library for this ENTIRE binary, which is what
# sensen's own code policy (config/cpp_details.txt rule 50) has always required
# and what this tree previously could not give it. It belongs in CANONICAL_FLAGS
# rather than in an add_compile_options() call for a reason that is easy to get
# wrong: CMAKE_CXX_FLAGS is what CMake's configure-time probes
# (check_cxx_source_compiles, check_include_file_cxx, try_compile) are run with,
# and add_compile_options() is not. Setting it here means every feature the
# build detects is detected against the standard library the build will actually
# use. Put it anywhere else and configure can conclude a header exists, or a
# snippet compiles, under libstdc++ and then hand that answer to a libc++ build.
#
# It is also set BEFORE FetchContent_MakeAvailable(grpc) further down, which is
# the whole mechanism by which gRPC, protobuf, abseil, re2 and upb are built
# against libc++ too: CMAKE_CXX_FLAGS is inherited by every subdirectory added
# after it, so those ~3000 translation units need no per-target treatment.
#
# The flag deliberately does NOT go into the C flags. c-ares and zlib are C, and
# a C compilation has no standard library to select; clang would only warn that
# the argument went unused.
#
# -fexperimental-library was already here and is now load-bearing rather than
# inert: under libc++ it defines _LIBCPP_ENABLE_EXPERIMENTAL and links
# libc++experimental. Because it sits in the same global CMAKE_CXX_FLAGS as
# everything else, it is uniform across the C++23 side and the C++17 gRPC side,
# which is what matters -- an experimental-library split across a link boundary
# would be its own ABI hazard.
#
# _GLIBCXX_USE_TBB_PAR_BACKEND=0 used to be defined alongside these flags. It was
# a libstdc++ macro, telling libstdc++'s parallel algorithms not to route through
# TBB; with libstdc++ out of the picture it names nothing and has been dropped
# rather than left as a define that only looks meaningful.
set(CANONICAL_FLAGS "-stdlib=libc++ -fPIC -O3 -march=x86-64-v3 -mtune=generic -mavx -mavx2 -mfma -msse2 -msse3 -mssse3 -msse4.1 -msse4.2 -pthread -fstack-protector-strong -DNDEBUG -Wno-error=stringop-overflow -Wno-error=array-bounds -Wno-error=unused-variable -Wno-error=unused-parameter -Wno-error=unused-but-set-variable -Wno-error=pedantic -Wno-error=global-module -Wno-unknown-warning-option -fexperimental-library")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CANONICAL_FLAGS}")

# The linker needs to be told as well, and separately: -stdlib= is a driver
# option, and the driver only puts -lc++ (and the libc++abi it depends on) on the
# link line if it sees the flag at LINK time. Compiling every object against
# libc++ headers and then linking without it produces a link against libstdc++
# that fails on std::__1:: symbols nothing defines.
#
# -lc++abi is NOT listed explicitly. It does not need to be: libc++.so.1 carries
# libc++abi.so.1 as its own DT_NEEDED, so the dynamic linker pulls it in
# transitively -- verified directly by linking a trivial program with nothing but
# -stdlib=libc++ and reading its NEEDED list. Naming it by hand is how a build
# ends up ordering it wrongly on a static link.
#
# No -L or -Wl,-rpath to a libc++ directory either, deliberately. sensen's own
# CMakeLists auto-detects a libc++ lib path and bakes an rpath to it, which is
# right for a library that may be installed next to an unusual toolchain and
# wrong here: an absolute host path baked into the engine is a path that does not
# exist inside the deploy container. The toolchain's own default search path
# resolves libc++ in both places, and the container carries its libc++ the same
# way it already carries everything else the binary needs.
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -stdlib=libc++")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -stdlib=libc++")

set(gRPC_INSTALL OFF CACHE BOOL "" FORCE)
set(protobuf_INSTALL OFF CACHE BOOL "" FORCE)
set(ABSL_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
# ON, and it has to be: this is the switch that decides whether TBB is built
# from source at all. sensen guards its FetchContent TBB block with
# `if(ENABLE_PARALLEL_STL)` (sensen/CMakeLists.txt:708), so forcing this OFF --
# as this line used to -- leaves `find_package(TBB)` as the only way TBB can
# ever be found. That was survivable while everything here was libstdc++ and the
# builder installed libtbb-dev. It is not survivable now: the builder
# deliberately does not install libtbb-dev (a distro TBB is libstdc++-built and
# would silently half-link into a libc++ binary), so with this OFF the container
# has no TBB from either mechanism and configure dies at the sensen_slim TBB
# check below with "neither TBB::tbb nor TBB_LIB is defined".
#
# That failure did not reproduce on the machine this migration was done on,
# because that host happens to carry a from-source oneTBB at
# /usr/local/lib64/cmake/TBB which the find_package probe picked up. The
# container carries neither, which is the only environment whose answer counts.
# Found by building the image; it is not visible from the local build.
#
# It is decided by probing rather than hard-coded, because the right answer
# differs between the container and a developer machine and getting it wrong
# breaks one or the other. ON when no TBB is installed: sensen fetches and
# builds one from source under our flags, which is what the container needs and
# the only way it gets a TBB at all. OFF when a system TBB already exists:
# turning it ON there makes FetchContent create TBB::tbb a second time and
# configure dies with "add_library cannot create ALIAS target TBB::tbb because
# another target with the same name already exists".
#
# The deploy configuration is the ON branch, and it is the one that matters:
# backend/Dockerfile's builder deliberately installs no libtbb-dev, so the probe
# finds nothing and TBB is built from source under libc++ exactly as intended.
# The OFF branch exists so this still configures on a machine that happens to
# carry a system oneTBB -- but note that such a host links a TBB whose standard
# library was not chosen by this build, which is precisely why the container
# does not have one. `ldd` on the engine is the standing check either way: a
# libtbb.so.12 in NEEDED means a prebuilt copy won.
find_package(TBB CONFIG QUIET)
if(TBB_FOUND)
    message(STATUS "System TBB found; leaving ENABLE_PARALLEL_STL OFF to avoid a duplicate TBB::tbb")
    set(ENABLE_PARALLEL_STL OFF CACHE BOOL "" FORCE)
else()
    message(STATUS "No system TBB; enabling ENABLE_PARALLEL_STL so sensen builds one from source")
    set(ENABLE_PARALLEL_STL ON CACHE BOOL "" FORCE)
endif()

# A note on TBB, because it is the one dependency in this binary whose standard
# library can be wrong without anything saying so, and because the obvious fix
# for it is wrong.
#
# The engine must link the oneTBB built FROM SOURCE in this build tree (pinned
# v2022.3.0, compiled with CANONICAL_FLAGS and therefore with libc++), never a
# prebuilt system copy. A distro-packaged TBB is compiled against libstdc++, and
# the parts of TBB this tree uses -- parallel_for, blocked_range, task_arena,
# task_group, all confined to sensen/src/parallel.cpp so no importer's BMI ever
# carries a tbb:: type -- are header templates instantiated in our own
# translation units. That means a libstdc++ TBB does not fail to link against a
# libc++ binary. It links, it starts, and the two halves quietly disagree about
# the layout of every std:: type that crosses between them.
#
# Setting CMAKE_DISABLE_FIND_PACKAGE_TBB here looks like the way to guarantee
# that and IS NOT -- it was tried and it made the link strictly worse. sensen
# probes with `find_package(TBB QUIET GLOBAL)` (sensen/CMakeLists.txt:1090) and,
# only when that probe fails, falls through to a bare
# `find_library(TBB_LIB NAMES tbb ...)` (sensen/CMakeLists.txt:1093). Disabling
# the find_package therefore does not remove a system TBB from consideration; it
# demotes the search to the cruder of the two mechanisms, which found
# /usr/local/lib64/libtbb.so and put that shared object on calculator_engine's
# link line next to the from-source libtbb.a -- exactly the mixture the setting
# was meant to prevent, where before there had been only the static archive.
# Verified by diffing the generated link line against the pre-change build.
#
# So the guarantee is not made here, because it cannot be made here. It is made
# by not having a prebuilt TBB present for either mechanism to find: backend/
# Dockerfile's builder stage deliberately does not install libtbb-dev (there is a
# comment on that apt list saying so and why), which is what leaves the pinned
# FetchContent copy as the only TBB in the image.
#
# On a developer machine this is NOT guaranteed, and that difference is real
# rather than theoretical -- a host that happens to have a system oneTBB
# installed can still have it found by the find_package probe. It was verified by
# hand on the host this migration was done on that the generated link line for
# calculator_engine names only the from-source archive. `ldd` on the built engine
# is the standing check: a libtbb.so.12 in NEEDED is this going wrong, and it is
# the only symptom there will be.

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
# Safe to mix, and it stays safe now that both sides are libc++ rather than
# libstdc++. The property that matters is unchanged: Abseil's ABI-significant
# split is pre-C++17 vs C++17-and-later, and its options header
# (third_party/abseil-cpp/absl/base/options.h) sets ABSL_OPTION_USE_STD_STRING_VIEW,
# _OPTIONAL, _VARIANT and _ANY to 2 -- "alias the std:: type when the language
# level provides it". Both C++17 and C++23 provide all four, so absl::string_view
# is std::string_view on both sides of the boundary and the aliases agree.
# libc++ itself does not change the layout of std::string, std::vector or
# std::string_view with the -std level, so the dialect split costs nothing here.
# This is also the ordinary arrangement for any distro-packaged gRPC.
#
# The reason for pinning gRPC to C++17 was never about the standard library
# either, so moving to libc++ does not retire it: gRPC 1.62 is a 2024 codebase
# being compiled against a standard published after it, and building it as its
# own authors do avoids an unbounded sequence of one-off patches. The particular
# C++23 failure named above (std::bind_front's result no longer converting to
# std::function) was diagnosed against libstdc++ 14 and may well not reproduce
# under libc++ -- but "may well not" is not a reason to move a working
# dependency onto a dialect its authors never test.
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

# The sensen financial library's own contract, kept separate from
# calculator.proto: that one speaks this application's language (strategies,
# legs, payoff curves), this one exposes the general-purpose financial engine
# underneath so other services can call it without inheriting a calculator's
# vocabulary. Both are served by the same binary on the same port.
set(FINANCE_PROTO_FILE "${CMAKE_CURRENT_SOURCE_DIR}/proto/finance.proto")

add_custom_command(
    OUTPUT "${PROTO_GEN_DIR}/finance.pb.cc" "${PROTO_GEN_DIR}/finance.pb.h" "${PROTO_GEN_DIR}/finance.grpc.pb.cc" "${PROTO_GEN_DIR}/finance.grpc.pb.h"
    COMMAND $<TARGET_FILE:protoc>
    ARGS --cpp_out=${PROTO_GEN_DIR}
         --grpc_out=${PROTO_GEN_DIR}
         --plugin=protoc-gen-grpc=$<TARGET_FILE:grpc_cpp_plugin>
         -I${CMAKE_CURRENT_SOURCE_DIR}/proto
         ${FINANCE_PROTO_FILE}
    DEPENDS ${FINANCE_PROTO_FILE}
)

# The LLM-backed strategy assistant's contract: free-text trade descriptions
# in, structured strategy parameters (or a clarifying question, or a refusal)
# out. Kept separate from calculator.proto and finance.proto for the same
# reason those two are separate from each other -- it speaks its own
# vocabulary (utterances, clarifications, refusals) rather than either
# calculator's strategies/legs or the financial library's numeric functions.
# Same binary, same port, third contract.
#
# StrategyParams DOES import finance.proto for exactly two fields
# (exercise_type, asian_type) -- reusing sensen.finance.ExerciseType/AsianType
# verbatim rather than inventing a parallel vocabulary, so a param can reach
# sensen.finance.Finance/PriceOptionTree unchanged. FINANCE_PROTO_FILE is
# therefore listed in DEPENDS below too: protoc resolves the import from
# -I${CMAKE_CURRENT_SOURCE_DIR}/proto regardless, but without this the build
# would not know to regenerate assistant's headers when finance.proto changes.
set(ASSISTANT_PROTO_FILE "${CMAKE_CURRENT_SOURCE_DIR}/proto/assistant.proto")

add_custom_command(
    OUTPUT "${PROTO_GEN_DIR}/assistant.pb.cc" "${PROTO_GEN_DIR}/assistant.pb.h" "${PROTO_GEN_DIR}/assistant.grpc.pb.cc" "${PROTO_GEN_DIR}/assistant.grpc.pb.h"
    COMMAND $<TARGET_FILE:protoc>
    ARGS --cpp_out=${PROTO_GEN_DIR}
         --grpc_out=${PROTO_GEN_DIR}
         --plugin=protoc-gen-grpc=$<TARGET_FILE:grpc_cpp_plugin>
         -I${CMAKE_CURRENT_SOURCE_DIR}/proto
         ${ASSISTANT_PROTO_FILE}
    DEPENDS ${ASSISTANT_PROTO_FILE} ${FINANCE_PROTO_FILE}
)

# The MORTGAGE assistant's contract: free-text mortgage / time-value-of-money /
# cash-flow requests in, the NAME OF A sensen.finance.Finance RPC plus that
# RPC's own parameters (or a clarifying question, or a refusal) out. A separate
# file from assistant.proto rather than a second service inside it, because the
# two assistants are backed by two different fine-tuned models with two
# different label spaces -- see that file's own banner. Same binary, same port,
# fourth contract.
set(MORTGAGE_ASSISTANT_PROTO_FILE "${CMAKE_CURRENT_SOURCE_DIR}/proto/mortgage_assistant.proto")

add_custom_command(
    OUTPUT "${PROTO_GEN_DIR}/mortgage_assistant.pb.cc" "${PROTO_GEN_DIR}/mortgage_assistant.pb.h" "${PROTO_GEN_DIR}/mortgage_assistant.grpc.pb.cc" "${PROTO_GEN_DIR}/mortgage_assistant.grpc.pb.h"
    COMMAND $<TARGET_FILE:protoc>
    ARGS --cpp_out=${PROTO_GEN_DIR}
         --grpc_out=${PROTO_GEN_DIR}
         --plugin=protoc-gen-grpc=$<TARGET_FILE:grpc_cpp_plugin>
         -I${CMAKE_CURRENT_SOURCE_DIR}/proto
         ${MORTGAGE_ASSISTANT_PROTO_FILE}
    DEPENDS ${MORTGAGE_ASSISTANT_PROTO_FILE}
)

# One descriptor set covering ALL FOUR contracts, for Envoy's gRPC-JSON transcoder.
#
# Envoy resolves method names, field names and types from this at runtime, which
# is what lets a caller POST JSON without holding a .proto or running codegen.
# It is generated from the same files the C++ stubs are generated from, so the
# REST surface cannot describe a method the engine does not serve -- the usual
# failure of a hand-maintained REST gateway.
#
# --include_imports is required: without it the descriptor references types it
# does not contain, and Envoy rejects the whole configuration at startup.
set(PROTO_DESCRIPTOR "${PROTO_GEN_DIR}/api_descriptor.pb")
add_custom_command(
    OUTPUT ${PROTO_DESCRIPTOR}
    COMMAND $<TARGET_FILE:protoc>
    ARGS --include_imports
         --descriptor_set_out=${PROTO_DESCRIPTOR}
         -I${CMAKE_CURRENT_SOURCE_DIR}/proto
         ${PROTO_FILE} ${FINANCE_PROTO_FILE} ${ASSISTANT_PROTO_FILE} ${MORTGAGE_ASSISTANT_PROTO_FILE}
    DEPENDS ${PROTO_FILE} ${FINANCE_PROTO_FILE} ${ASSISTANT_PROTO_FILE} ${MORTGAGE_ASSISTANT_PROTO_FILE}
    COMMENT "Generating proto descriptor set for Envoy JSON transcoding"
)
add_custom_target(proto_descriptor ALL DEPENDS ${PROTO_DESCRIPTOR})

add_library(proto_lib STATIC
    "${PROTO_GEN_DIR}/calculator.pb.cc"
    "${PROTO_GEN_DIR}/calculator.grpc.pb.cc"
    "${PROTO_GEN_DIR}/finance.pb.cc"
    "${PROTO_GEN_DIR}/finance.grpc.pb.cc"
    "${PROTO_GEN_DIR}/assistant.pb.cc"
    "${PROTO_GEN_DIR}/assistant.grpc.pb.cc"
    "${PROTO_GEN_DIR}/mortgage_assistant.pb.cc"
    "${PROTO_GEN_DIR}/mortgage_assistant.grpc.pb.cc"
)
target_include_directories(proto_lib PUBLIC ${PROTO_GEN_DIR})
target_link_libraries(proto_lib PUBLIC grpc++ libprotobuf)

# Include sensen submodule
set(BUILD_PYTHON_BINDINGS OFF CACHE BOOL "Disable sensen python bindings" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "Disable sensen tests" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "Disable sensen examples" FORCE)
# ENABLE_CUDA -- propagated into sensen's cache, deliberately WITHOUT FORCE,
# unlike every other line in this block.
#
# sensen's own CMakeLists.txt (sensen/CMakeLists.txt:600-639) auto-detects
# ENABLE_CUDA from the CONFIGURING MACHINE (nvidia-smi, CUDA_PATH/CUDA_BIN_PATH,
# nvcc-in-PATH) whenever it is not already a defined cache variable by the time
# add_subdirectory(sensen) below reaches it. Left alone, that makes this
# engine's CUDA setting a property of whichever host happens to run `cmake`,
# not a decision anyone made -- exactly the class of non-reproducibility
# CLAUDE.md's "make the container image build reproducibly" fix (4f11e29)
# exists to prevent elsewhere in this same file. Declaring it HERE, explicitly,
# pins the default to OFF regardless of host, while still leaving it
# genuinely settable: `set(... CACHE BOOL ...)` without FORCE only seeds a
# cache entry that does not already exist, so a caller's own
# `-DENABLE_CUDA=ON` on the original `cmake` invocation (which lands in the
# cache before ANY CMakeLists.txt runs) passes through this line untouched and
# is what add_subdirectory(sensen) below -- and therefore sensen's own
# `option(ENABLE_CUDA ...)` -- actually sees. FORCE would instead clobber that
# override back to OFF every configure, defeating the entire point of a Wave-0
# GPU worker being able to opt in at all.
#
# By itself, this line does NOT make sensen_slim (below) CUDA-aware -- it
# only controls what sensen builds INSIDE its own
# add_subdirectory(sensen EXCLUDE_FROM_ALL) scope below (EXCLUDE_FROM_ALL, so
# nothing in there builds unless something outside this file explicitly links
# it). sensen_slim itself compiles a curated subset of sensen's .cppm/.cpp
# files directly, never sensen's own `sensen` CMake target, and three further
# things were needed before it could compile real CUDA kernels: (1)
# SENSEN_HAS_CUDA as a compile definition on the sensen_slim target -- of the
# 79 files sensen_slim curates, 28 contain `#ifdef SENSEN_HAS_CUDA` bodies
# that compile to an inert CPU-only surface without it; (2) CUDA enabled as a
# language for this CMake run (`project(OptionsCalculatorEngine CXX)` above
# is C++-only, but sensen's own `project(Sensen ... LANGUAGES CXX CUDA)`
# inside add_subdirectory(sensen EXCLUDE_FROM_ALL) below already does this
# WHENEVER ENABLE_CUDA=ON, and enable_language() is a whole-configure effect
# that reaches past that subdirectory's own scope); and (3) the actual .cu
# device-kernel translation units those 28 files' CUDA branches call into via
# extern "C" -- sensen_slim's own target_sources lists zero .cu files. All
# three are now wired up just after sensen_slim's target definition below
# (search this file for "GPU-capable sensen_slim"), which links sensen's own
# `sensen_cuda` target in rather than curating a second .cu file list --
# see that block's own comment for the full justification and the honest
# cost (a hard CUTLASS dependency once ENABLE_CUDA=ON) it was weighed against.
set(ENABLE_CUDA OFF CACHE BOOL "Enable CUDA GPU acceleration in the sensen subproject (propagated, not forced -- see this line's own comment)")
# sensen defaults BUILD_BENCHMARKS to ON and calls add_subdirectory(benchmarks)
# unconditionally when it is set. The deploy image has no use for them, and
# .dockerignore excludes backend/sensen/benchmarks from the build context, so
# leaving this on fails configure outright inside the container.
set(BUILD_BENCHMARKS OFF CACHE BOOL "Disable sensen benchmarks" FORCE)
set(SKIP_CLANG_TIDY ON CACHE BOOL "Disable clang-tidy due to c++23 module scanning bugs" FORCE)
# SENSEN_USE_LIBCXX is deliberately NOT forced here any more. It defaults ON in
# sensen's own CMakeLists (sensen/CMakeLists.txt:541), and that default is now
# the correct answer, so the right thing to do is stop overriding it.
#
# This line used to read `set(SENSEN_USE_LIBCXX OFF ... FORCE)`, and the reasoning
# behind it was sound but pointed the wrong way. The symptom was real: sensen
# forces -stdlib=libc++ across its own directory scope, so with everything else
# on libstdc++ the sensen subtree spoke libc++ while its consumers did not, and
# the errors that produced ("no viable conversion from
# 'std::__cxx11::basic_string<char>' to 'std::string'", or the same sentence with
# string_view on both sides) never mention the standard library at all. Two
# std::strings with different layouts is not a preference to reconcile later.
# The fix chosen at the time was to drag sensen down to libstdc++, because that
# was the smaller edit. It was still the wrong direction: it made this tree
# violate config/cpp_details.txt rule 50, which mandates
# `-std=c++23 -stdlib=libc++` for every module and shared object sensen builds,
# and it left the violation encoded as a FORCE that no amount of reading sensen's
# policy would explain.
#
# What made the other direction possible is that nothing in this binary is
# consumed prebuilt across a C++ ABI boundary. gRPC, protobuf, abseil, re2, upb,
# oneTBB, fastjson, the logger and SGEE are all compiled from source inside this
# build tree, so they follow CANONICAL_FLAGS onto libc++ without being asked.
# What remains is C -- OpenSSL, zlib, jemalloc, liburing, brotli, systemd -- and
# a C library has no standard library to disagree about. libpq and Z3 would have
# been the two genuine blockers, since the engine would have to speak to them
# through pqxx and z3++ respectively; neither is reachable from this binary at
# all, because the only consumer of z3++ is gp_ara, which sensen_slim excludes,
# and nothing here includes pqxx. `ldd` on the built engine confirms both: no
# libpq, no libz3.
add_subdirectory(sensen EXCLUDE_FROM_ALL)

# ============================================================================
# The one std module, for every target this directory declares
# ============================================================================
# src/modules/*.cppm say `import std;` rather than listing standard headers in a
# global module fragment, which is what a C++23 module is supposed to look like
# and what docs/PRD_OPTIONS_AND_FUTURES_CALCULATOR.md has always specified. The
# sensen sources compiled into sensen_slim just below say it too, now that
# SENSEN_NO_IMPORT_STD is gone from the top of this file.
#
# The BMI they import is the ONE sensen precompiles at
# ${CMAKE_BINARY_DIR}/std_modules/std.pcm. There is deliberately no second
# std.pcm for this directory: sensen builds that file from ${CMAKE_CXX_FLAGS},
# which at that point IS this file's CANONICAL_FLAGS, so a single BMI is
# compatible with every consumer here by construction. Two std modules built
# from two flag sets is the BMI-mismatch failure this project has hit before,
# and it is entirely self-inflicted -- the union is available, so use it.
#
# PLACEMENT IS THE WHOLE TRICK, and getting it wrong is not a warning.
# `add_compile_options` applies only to targets created AFTER it in this
# directory, so this block has to sit:
#
#   * AFTER add_subdirectory(sensen), because std_module_precompile is declared
#     in there and the guard below tests for it; and
#   * BEFORE add_library(sensen_slim), which is the first target here to compile
#     a sensen module. This block was originally ~500 lines further down, after
#     SGEE, and sensen_slim therefore missed it -- seven translation units died
#     on `fatal error: module 'std' not found` while every other target built
#     clean. The error names the module, never the missing flag.
#
# It stays above sensen_slim and below sensen for those two reasons, and it is
# still below FetchContent_MakeAvailable(grpc), which keeps the flag off gRPC's
# ~3000 translation units -- they neither import this BMI nor should rebuild for
# a flag they never use.
#
# COMPILE_LANGUAGE:CXX guards it because `-fmodule-file=` is not a C option and
# the llama.cpp and c-ares subtrees below compile C. The flag is lazy: clang
# loads the BMI only when a translation unit actually says `import std;`, so
# carrying it on one that does not is inert.
set(ENGINE_STD_PCM "${CMAKE_BINARY_DIR}/std_modules/std.pcm")
if(NOT TARGET std_module_precompile)
    message(FATAL_ERROR
        "std_module_precompile does not exist, so ${ENGINE_STD_PCM} will never be "
        "built and every `import std;` in this tree fails with "
        "\"module 'std' not found\". sensen declares that target only when "
        "SENSEN_USE_LIBCXX is ON and <libc++ prefix>/share/libc++/v1/std.cppm "
        "exists; its configure output prints both, and LIBCXX_MODULE_DIR is not "
        "readable from here because add_subdirectory gave sensen its own scope.")
endif()
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fmodule-file=std=${ENGINE_STD_PCM}>)

# --------------------------------------------------------------------------
# sensen_slim — the part of sensen this engine actually uses.
#
# We import exactly two modules, sensen.options and sensen.portfolio. Linking
# sensen's own `sensen` target compiled all 271 of its translation units and
# produced a 93 MB libsensen.so, because `add_subdirectory` + linking a target
# builds every module interface unit in its FILE_SET whether or not anything
# imports it. Headers do not behave this way, which is why the cost stayed
# invisible: nothing in our source referenced the other 253 modules.
#
# The price was not only build time. Any breakage anywhere in sensen — in a
# diffusion model, a CUDA kernel, an SMT solver binding — failed this deploy,
# and gp_ara dragged Z3 into the runtime image for code that is never called.
#
# The closure below is transitive and computed, not guessed:
#
#     python3 scripts/sensen_module_closure.py --check
#
# Nothing in the sensen submodule is modified. `add_subdirectory` above is
# already EXCLUDE_FROM_ALL, so its targets build only when something links
# them; linking sensen_slim instead of sensen is what leaves the other 263
# uncompiled. sensen's tree is still configured, which is deliberate — fastjson,
# logger and TBB come from it and are still linked.
#
# If a future import pulls in another sensen module, the build fails with
# "module 'sensen.x' not found" — a clear error naming the fix. Re-run the
# script above and add what it reports.
#
# One compile-flag difference is deliberate and is an improvement, not drift.
# sensen/CMakeLists.txt:573 appends `-march=native` to everything in its own
# directory scope. That lands AFTER this project's canonical
# `-march=x86-64-v3 -mtune=generic`, so it won, and every sensen module was
# built for whatever CPU happened to run the compiler — in the container, the
# Railway builder rather than the host that runs the engine. config/
# cpp_details.txt requires x86-64-v3/generic precisely to avoid that, for
# cross-host FP parity and durable-replay determinism. sensen_slim is declared
# in this directory scope, so it does not inherit the override and gets the
# canonical flags. AVX2 and FMA are still enabled explicitly in CANONICAL_FLAGS;
# only AVX-512 and host-tuning are given up, which no closure module uses.
#
# The other flags sensen_objects carries (SENSEN_HAS_AVX512F, HAS_LIBURING,
# SENSEN_HAS_LIBPQ, _LIBCPP_NO_ABI_TAG and the mm_malloc suppressions) were
# referenced by none of the eight files in the original (finance-only)
# closure — verified by grep before dropping them. That grep was redone for
# the llm_pipeline addendum below, against the full 68-module/73-file
# closure, not assumed still true at the larger size: HAS_LIBURING and
# SENSEN_HAS_LIBPQ are still referenced by none of the 73 files and stay
# dropped.
#
# _LIBCPP_NO_ABI_TAG was in that dropped list too, on the grounds that it is a
# libc++-only ABI-tag workaround and "sensen_slim never passes -stdlib=libc++,
# so it would be inert here even if added". That premise has expired: this whole
# binary is libc++ now, sensen_slim included. The define is therefore set once at
# the very top of this file, in the top-level directory scope, so that it covers
# sensen_slim and every other target here uniformly -- see the reasoning there.
# It is not listed as a sensen_slim-specific compile definition because it must
# NOT be one; a define that changes mangled names is only safe when nothing in
# the binary disagrees about it.
# SENSEN_HAS_AVX512F is NOT in that category any more: gguf_parser.cppm,
# autograd.cppm, mxfp4.cppm, and k_quant_dequant.cppm all gate real code on
# `#if defined(__AVX512F__) || defined(SENSEN_HAS_AVX512F)` — a per-function
# `[[gnu::target("avx512f,...")]]`-attributed AVX-512 tier of sensen's own
# runtime SIMD waterfall, selected at runtime by CPUID detection elsewhere,
# not something that requires (or should get) a whole-TU -mavx512f flag. The
# macro form only decides whether that tier's function bodies are compiled
# at all; CANONICAL_FLAGS deliberately does not pass -mavx512f (config/
# cpp_details.txt's x86-64-v3/generic baseline), so this changes nothing
# about the resulting binary's minimum-CPU requirement. Confirmed by the
# concrete symptom of leaving it out: gemm_kernels.cppm unconditionally
# assumes (via its own `!defined(_MSC_VER) || defined(__clang__) ||
# defined(__AVX512F__)` gate, true under any clang regardless of
# SENSEN_HAS_AVX512F) that k_quant_dequant.cppm's AVX-512 dequantizer is
# always visible under clang — without this define k_quant_dequant.cppm
# compiles that function out, and gemm_kernels.cppm fails with "no member
# named 'dequantize_q6_k_avx512_block' in namespace 'sensen::detail'". Set
# from the SAME cached auto-detection result sensen's own CMakeLists already
# computed (HAVE_AVX512F, a check_cxx_source_compiles() cache variable that
# survives past the add_subdirectory(sensen ...) call above into this
# scope) rather than re-running CPU-feature detection a second time.
if(HAVE_AVX512F)
    set(SENSEN_SLIM_NEEDS_AVX512F ON)
endif()
#
# sensen.llm_pipeline addendum (in-process LLM inference RPC):
#
# The assistant RPC needs to run sensen's own LLM inference pipeline
# in-process rather than shelling out, so `sensen.llm_pipeline` and its
# transitive closure were added to the list below the same way the original
# finance closure was derived: computed, not guessed, via
#
#     python3 scripts/sensen_module_closure.py --check
#
# `roots_from_our_sources()` in that script finds its roots by scanning
# backend/src for literal `import sensen.*;` lines, so it cannot discover a
# module that nothing in this tree imports yet. At the time this list was
# written, `src/modules/assistant_service.cppm` — the consumer that will
# `import sensen.llm_pipeline;` — did not exist (a parallel effort was
# writing it concurrently). The closure was computed anyway by temporarily
# dropping a throwaway TU containing only `import sensen.llm_pipeline;` into
# backend/src, running the script to get its BFS answer, then deleting that
# TU — the same transitive-closure algorithm the script always runs, just
# given a root it could not yet find on its own. Once assistant_service.cppm
# lands and genuinely imports sensen.llm_pipeline, `--check` will find this
# root itself and should pass with the list below unchanged; until then,
# `--check` legitimately reports this closure's files as EXTRA, which is
# expected, not a sign of drift.
#
# The closure pulls in far more than "one inference module" because
# sensen.llm_pipeline sits at the top of sensen's real LLM stack: it needs
# the GGUF loader family (parser, mmap/safetensors/pytorch/onnx weight
# loaders, k-quant dequantization), the transformer forward path (attention,
# rotary embeddings, RMSNorm, the MoE/Qwen3.5 expert-routing layers, KV
# cache), the trainable backbone used for LoRA/fine-tuning-aware inference,
# and sensen's CPU/GPU dispatch plumbing (cpu_backend-adjacent modules,
# autograd_cuda, cuda_backend, the Triton worker bridge). None of this is
# optional for a working text-generation call; it is the same 68-module,
# 73-file set the script computes for anyone who imports sensen.llm_pipeline,
# not something inflated for this engine specifically.
#
# CUDA-conditional code, CPU-only build: SENSEN_HAS_CUDA is never defined in
# this project (grep confirms no target anywhere passes it, and no CUDA
# language is enabled for this CMake project). Several modules in this
# closure have real CUDA code paths — cuda_backend.cppm, autograd_cuda.cppm,
# secondary_stream_prepass.cppm, batched_paged_attention.cppm — but each one
# was inspected, not assumed safe: every CUDA-only include, type, and
# function body in those files sits behind `#ifdef SENSEN_HAS_CUDA` (with a
# `#ifndef SENSEN_HAS_CUDA` no-op/error-returning stub section in
# cuda_backend.cppm specifically), so with the macro undefined they compile
# to their CPU-only surface and never reference a CUDA header or symbol. The
# five `.cpp` files below (fp64_reference_simd.cpp, llama_model.cpp,
# parallel.cpp, tokenizer_gpu.cpp, training_moe_impl.cpp) are module
# IMPLEMENTATION units, not interfaces — each has a plain `module
# sensen.x;` (no `export`) rather than `export module sensen.x;` — so they
# are listed under PRIVATE as plain sources, mirroring how sensen's own
# src/CMakeLists.txt treats them and how parallel.cpp was already handled
# here before this addendum; FILE_SET CXX_MODULES is for interface units
# only.
#
# Known limitation of scripts/sensen_module_closure.py's --check, not a
# drift in this list: sensen.utils.logger lives at sensen/src/utils/logger.cppm
# — the only closure module in a subdirectory of sensen/src — and the
# script's listed-file regex (`\$\{SENSEN_SRC\}/([\w.]+\.(?:cppm|cpp))`) only
# matches a bare filename directly after `${SENSEN_SRC}/`, so it never
# recognizes a nested path either as `utils/logger.cppm` or as `logger.cppm`.
# `--check` therefore reports `logger.cppm` as MISSING even though it is
# present below, correctly pathed, and required by the closure. Verified by
# hand: `sensen.utils.logger`'s only file is genuinely in the computed
# closure, and grepping the raw regex against this file confirms it matches
# neither spelling. Not fixed here since scripts/sensen_module_closure.py is
# shared, documented tooling outside this edit's scope; flagged so a future
# `--check` run isn't mistaken for real drift on this one entry.
# --------------------------------------------------------------------------
set(SENSEN_SRC "${CMAKE_CURRENT_SOURCE_DIR}/sensen/src")

add_library(sensen_slim STATIC)
target_sources(sensen_slim
    PUBLIC
        FILE_SET CXX_MODULES BASE_DIRS "${SENSEN_SRC}" FILES
            "${SENSEN_SRC}/activations.cppm"
            "${SENSEN_SRC}/amx_operations.cppm"
            "${SENSEN_SRC}/autograd.cppm"
            "${SENSEN_SRC}/autograd_cuda.cppm"
            "${SENSEN_SRC}/batched_paged_attention.cppm"
            "${SENSEN_SRC}/bigdecimal.cppm"
            "${SENSEN_SRC}/context_parallel.cppm"
            "${SENSEN_SRC}/cpu_features.cppm"
            "${SENSEN_SRC}/cuda_backend.cppm"
            "${SENSEN_SRC}/diffusion_core.cppm"
            "${SENSEN_SRC}/dlm.cppm"
            "${SENSEN_SRC}/dual_chunk_attention.cppm"
            "${SENSEN_SRC}/embedding.cppm"
            "${SENSEN_SRC}/financial.cppm"
            "${SENSEN_SRC}/float_types.cppm"
            "${SENSEN_SRC}/fp64_reference.cppm"
            "${SENSEN_SRC}/gemm_jit.cppm"
            "${SENSEN_SRC}/gemm_kernels.cppm"
            "${SENSEN_SRC}/gguf_parser.cppm"
            "${SENSEN_SRC}/gguf_weight_loader.cppm"
            "${SENSEN_SRC}/gp_ara_interfaces.cppm"
            "${SENSEN_SRC}/grammar.cppm"
            "${SENSEN_SRC}/hybrid_attention.cppm"
            "${SENSEN_SRC}/intrinsics.cppm"
            "${SENSEN_SRC}/k_quant_dequant.cppm"
            "${SENSEN_SRC}/kv_half.cppm"
            # kv_cache.cppm imports sensen.kv_lowbit as of the sub-8-bit KV work.
            # Missing here, sensen_slim fails with "module 'sensen.kv_lowbit' not
            # found" -- the slim list is hand-maintained, so every new module a
            # listed one imports has to be added with it.
            # Added 2026-08-27 from `scripts/sensen_module_closure.py --check`,
            # which is the authority here rather than hand-tracing imports. The
            # 90-commit upstream merge pulled the qwen38 serving modules into the
            # closure transitively -- CLAUDE.md's note that sensen_slim "excludes
            # every qwen38 module" predates that and is no longer true.
            "${SENSEN_SRC}/cpu_frozen_lowbit.cppm"
            "${SENSEN_SRC}/cuda_pinned_staging.cppm"
            "${SENSEN_SRC}/dispatch_ledger.cppm"
            "${SENSEN_SRC}/lowbit_tc_dispatch.cppm"
            "${SENSEN_SRC}/qwen38.cppm"
            "${SENSEN_SRC}/qwen38_gdn_state.cppm"
            "${SENSEN_SRC}/qwen38_kv_attn.cppm"
            "${SENSEN_SRC}/qwen38_mtp.cppm"
            "${SENSEN_SRC}/qwen38_serving_model.cppm"
            "${SENSEN_SRC}/speculative_decode.cppm"
            "${SENSEN_SRC}/utils/logger.cppm"
            # kv_half.cppm imports sensen.paged_kv_dispatch, which imports
            # sensen.dispatch_contract. Both are new with the paged-KV dispatch
            # work. Same hand-maintained-list trap as kv_lowbit above: nothing
            # derives this, so each new module a listed one imports must be added
            # alongside it, and the failure only appears when the pointer moves.
            "${SENSEN_SRC}/dispatch_contract.cppm"
            "${SENSEN_SRC}/paged_kv_dispatch.cppm"
            "${SENSEN_SRC}/kv_lowbit.cppm"
            "${SENSEN_SRC}/kv_q8.cppm"
            "${SENSEN_SRC}/kv_cache.cppm"
            "${SENSEN_SRC}/linear_algebra.cppm"
            "${SENSEN_SRC}/llama_model.cppm"
            "${SENSEN_SRC}/llama_model_kernels.cppm"
            "${SENSEN_SRC}/llama_model_types.cppm"
            "${SENSEN_SRC}/llm_interfaces.cppm"
            "${SENSEN_SRC}/llm_pipeline.cppm"
            "${SENSEN_SRC}/lora.cppm"
            "${SENSEN_SRC}/memory.cppm"
            "${SENSEN_SRC}/mmap_weight_loader.cppm"
            "${SENSEN_SRC}/model_config.cppm"
            "${SENSEN_SRC}/moe_layer.cppm"
            "${SENSEN_SRC}/multi_head_attention.cppm"
            "${SENSEN_SRC}/mxfp4.cppm"
            "${SENSEN_SRC}/nvfp4.cppm"
            "${SENSEN_SRC}/onnx_weight_loader.cppm"
            "${SENSEN_SRC}/options.cppm"
            "${SENSEN_SRC}/parallel.cppm"
            "${SENSEN_SRC}/portfolio.cppm"
            "${SENSEN_SRC}/pytorch_weight_loader.cppm"
            "${SENSEN_SRC}/qwen35moe.cppm"
            "${SENSEN_SRC}/qwen35moe_serving_model.cppm"
            # Qwen3.8 arrived with the 2026-08-19 sensen merge and is NOT listed
            # here, deliberately. The whole chain is CUDA-only --
            # Qwen38AttentionKvCache lives inside `#ifdef SENSEN_HAS_CUDA` and
            # qwen38_gdn_state.cppm names it unconditionally -- and this engine
            # builds ENABLE_CUDA=OFF. llm_pipeline.cppm now imports the adapter
            # under the same guard as the use site that already threw
            # "requires a CUDA build", so on a CPU build these modules are never
            # named and must not be compiled.
            #
            # A CUDA build DOES need them; add them back under a
            # `if(SENSEN_HAS_CUDA)` branch rather than unconditionally, or the
            # CPU build breaks again exactly as it did on the merge.
            "${SENSEN_SRC}/rms_norm.cppm"
            # Pulled in by autograd_cuda.cppm as of sensen 8ffda361, which
            # added `import sensen.rope_context_ext;` for RopeExtConfig::mscale()
            # (the YaRN attention-temperature correction on ringAttentionFusedYarn's
            # SIMT fallback). sensen_slim is a CURATED subset of sensen's modules,
            # not the whole tree, so a new import upstream is a build break here
            # until the module is listed -- which is exactly how this surfaced:
            # "fatal error: module 'sensen.rope_context_ext' not found".
            # It imports only `std`, so it adds no further closure.
            "${SENSEN_SRC}/rope_context_ext.cppm"
            "${SENSEN_SRC}/rotary_embedding.cppm"
            "${SENSEN_SRC}/safetensors_weight_loader.cppm"
            "${SENSEN_SRC}/secondary_stream_prepass.cppm"
            "${SENSEN_SRC}/sharded_training.cppm"
            "${SENSEN_SRC}/simd.cppm"
            "${SENSEN_SRC}/tokenizer.cppm"
            "${SENSEN_SRC}/trainable_backbone_model.cppm"
            "${SENSEN_SRC}/training_amp.cppm"
            "${SENSEN_SRC}/training_attention.cppm"
            "${SENSEN_SRC}/training_autocast.cppm"
            "${SENSEN_SRC}/training_cross_attention.cppm"
            "${SENSEN_SRC}/training_gated_attention.cppm"
            "${SENSEN_SRC}/training_gated_deltanet.cppm"
            "${SENSEN_SRC}/training_moe.cppm"
            "${SENSEN_SRC}/training_primitives.cppm"
            "${SENSEN_SRC}/training_rope.cppm"
            "${SENSEN_SRC}/training_transformer.cppm"
            "${SENSEN_SRC}/tensor_observer.cppm"
            "${SENSEN_SRC}/transformer_block.cppm"
            "${SENSEN_SRC}/triton_worker_bridge.cppm"
            "${SENSEN_SRC}/utils/logger.cppm"
            "${SENSEN_SRC}/weight_loader_factory.cppm"
            "${SENSEN_SRC}/weight_loader_interface.cppm"
    PRIVATE
        # Module implementation units. Each is a plain source, not a FILE_SET
        # entry, because each has a non-exported `module sensen.x;` purview
        # rather than an interface's `export module sensen.x;` — CMake's
        # module scanner still orders them correctly after their interface
        # units from this list alone, without needing FILE_SET to say so.
        #
        # parallel.cpp holds the <tbb/...> includes deliberately kept out of
        # sensen.parallel's interface so no importer's BMI carries tbb::
        # symbols (sensen/src/parallel.cppm:4-11) — already present before
        # this addendum, kept here unchanged.
        "${SENSEN_SRC}/parallel.cpp"
        # fp64_reference_simd.cpp / llama_model.cpp / tokenizer_gpu.cpp /
        # training_moe_impl.cpp are the out-of-line implementation units of
        # sensen.fp64_reference, sensen.llama_model, sensen.tokenizer, and
        # sensen.training_moe respectively — each keeps its own heavier
        # includes and out-of-line definitions off its module's BMI, the
        # same reason parallel.cpp is separate from parallel.cppm.
        "${SENSEN_SRC}/fp64_reference_simd.cpp"
        "${SENSEN_SRC}/llama_model.cpp"
        "${SENSEN_SRC}/tokenizer_gpu.cpp"
        "${SENSEN_SRC}/training_moe_impl.cpp"
        # numa_bind.cpp is reported EXTRA by scripts/sensen_module_closure.py and
        # is NOT extra. It defines the extern "C" symbols
        # sensen_available_cpu_count and sensen_numa_bind_local_node, which
        # gemm_kernels, parallel and llama_model call directly. The closure tool
        # parses module IMPORTS only, so a C-ABI implementation unit is invisible
        # to it -- dropping this on its advice fails at LINK, not at compile, and
        # the error names the symbol rather than the file that was removed.
        "${SENSEN_SRC}/paged_kv_dispatch_pool.cpp"
        "${SENSEN_SRC}/numa_bind.cpp"
        # numa_bind.cpp is not the implementation unit of any module, which is
        # why the closure script does not report it: it defines two plain
        # `extern "C"` helpers (sensen_available_cpu_count,
        # sensen_numa_bind_local_node) that gemm_kernels.cppm:108 and
        # llama_model.cppm:52 DECLARE for themselves rather than import. A
        # module-graph walk cannot see that edge, so the omission surfaces only
        # at link time as two undefined references, well after everything has
        # compiled cleanly.
)

set_target_properties(sensen_slim PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)

# sensen's sources include "utils/sensen_std.h" relative to its src root.
target_include_directories(sensen_slim PUBLIC $<BUILD_INTERFACE:${SENSEN_SRC}>)

# sensen.gemm_jit's global module fragment includes the vendored, header-only
# xbyak (an x86 JIT assembler) unconditionally -- it is what backs its
# runtime AVX-512 GEMM code generation. sensen_objects gets this same
# directory via its own target_include_directories (SYSTEM PRIVATE
# ${CMAKE_SOURCE_DIR}/external/xbyak in sensen/src/CMakeLists.txt); no other
# file in this closure needs it, so it is added PRIVATE here, not PUBLIC.
target_include_directories(sensen_slim SYSTEM PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/sensen/external/xbyak")

# sensen.safetensors_weight_loader (pulled in by the llm_pipeline closure, for
# reading safetensors-format checkpoint metadata) does `import fastjson;`.
# calculator_engine and test_runner already list `fastjson` on their own
# target_link_libraries lines, but that is not sufficient for C++23 module
# scanning: sensen_slim is the target that actually contains the translation
# unit doing the importing, so CMake's module dependency scanner needs a
# target-level edge FROM sensen_slim TO fastjson to know fastjson's BMI must
# be built first. Without it the two targets' modules are scanned/ordered
# independently and a parallel build can reach safetensors_weight_loader.cppm
# before fastjson's module interface has been compiled, failing with "module
# 'fastjson' not found" (order-dependent, not a per-file syntax error).
# PUBLIC because sensen_slim's own consumers keep linking fastjson too.
target_link_libraries(sensen_slim PUBLIC fastjson)

# See SENSEN_SLIM_NEEDS_AVX512F's derivation above: gguf_parser.cppm,
# autograd.cppm, mxfp4.cppm, and k_quant_dequant.cppm gate a
# [[gnu::target(...)]]-attributed AVX-512 SIMD-waterfall tier on this macro,
# and gemm_kernels.cppm unconditionally calls into it under clang. PRIVATE:
# it only affects which function bodies these sensen_slim TUs compile, not
# sensen_slim's public interface.
if(SENSEN_SLIM_NEEDS_AVX512F)
    target_compile_definitions(sensen_slim PRIVATE SENSEN_HAS_AVX512F)
endif()

# TBB is configured by sensen's own CMakeLists, whether found on the system or
# fetched. Only parallel.cpp needs it, but it is PUBLIC because consumers link
# through this target.
if(TARGET TBB::tbb)
    target_link_libraries(sensen_slim PUBLIC TBB::tbb)
elseif(DEFINED TBB_LIB AND TBB_LIB)
    target_link_libraries(sensen_slim PUBLIC ${TBB_LIB})
else()
    message(FATAL_ERROR "sensen_slim needs TBB for sensen.parallel, and neither "
                        "TBB::tbb nor TBB_LIB is defined after configuring sensen.")
endif()

# --------------------------------------------------------------------------
# GPU-capable sensen_slim: SENSEN_HAS_CUDA + the actual device kernels.
#
# The ENABLE_CUDA comment above (~line 409-447) already laid out exactly
# three things still needed, past propagating the cache variable, before
# sensen_slim itself becomes CUDA-aware: (1) SENSEN_HAS_CUDA as a compile
# definition on sensen_slim -- 28 of the 79 files this target curates
# already contain `#ifdef SENSEN_HAS_CUDA` bodies that compile to an inert
# CPU-only stub surface without it; (2) CUDA enabled as a language for this
# CMake run; (3) the .cu device-kernel translation units those 28 files'
# CUDA branches call into via extern "C" -- sensen_slim's own target_sources
# lists zero .cu files. This block closes all three.
#
# (2) needs no action here: sensen's own `project(Sensen ... LANGUAGES CXX
# CUDA)` (sensen/CMakeLists.txt:329, reached only when ENABLE_CUDA=ON) already
# ran inside the add_subdirectory(sensen EXCLUDE_FROM_ALL) call above, and
# enable_language() is a whole-configure effect, not scoped to that
# subdirectory -- CMAKE_CUDA_COMPILER and the CUDA::cudart imported target
# are already live by the time this line runs. sensen_slim compiles no .cu
# sources of its own, so it needs nothing further from the language itself;
# it only needs (1) and (3) below.
#
# MECHANISM CHOICE for (3): link sensen's OWN `sensen_cuda` target (built by
# sensen/src/cuda/CMakeLists.txt, ~150 .cu files compiled into one static
# archive, EXCLUDE_FROM_ALL so it costs nothing when ENABLE_CUDA=OFF) rather
# than curating a second, parallel .cu file list the way the FILE_SET above
# curates .cppm/.cpp files. Justification, weighed explicitly against the
# alternative:
#
#   - The curated-module-list precedent immediately above (this file, ~line
#     511) has ALREADY broken twice this session when sensen added an import
#     the list didn't have. Each break was recoverable only because the
#     configure error ("module 'sensen.x' not found") names the missing
#     entry outright. A hand-curated .cu list has no equivalent safety net:
#     a missing .cu surfaces at LINK time as an opaque "undefined reference
#     to sensen_cuda_foo_kernel", with nothing in the error naming which of
#     the 28 gated files' SENSEN_HAS_CUDA branch needed it, or which of
#     sensen's ~150 kernel files supplies it. A mechanism that tracks
#     sensen's own device-kernel list automatically -- by construction,
#     forever -- is strictly preferable to one that needs the same kind of
#     manual sync this file has already had to redo twice.
#   - sensen_cuda's own CUDA_SEPARABLE_COMPILATION is OFF (verified:
#     src/cuda/CMakeLists.txt:169,469,585,708 -- every .cu target it defines
#     sets this the same way). Each .cu compiles to a self-contained object
#     with its device code already resolved, with no cross-TU nvlink device-
#     link step. That means linking the WHOLE static archive behaves exactly
#     like linking any other static library: the host linker pulls in only
#     the .o members that resolve an actual undefined reference from
#     sensen_slim's 28 gated files. calculator_engine's binary size and link
#     time are governed by what it actually calls, not by the archive's
#     ~150-file breadth -- reuse costs nothing at link time that curation
#     would have saved.
#
#   The real, non-hypothetical cost of this choice, stated plainly rather
#   than glossed over: sensen_cuda unconditionally compiles cuTile/CUTLASS-
#   backed kernels, and its own CMakeLists FATAL_ERRORs at CONFIGURE time if
#   CUTLASS 3.x headers cannot be found anywhere (no explicit -DCUTLASS_DIR
#   override) -- see sensen/src/cuda/CMakeLists.txt:305-324. A curated .cu
#   subset could, in principle, dodge that requirement if none of the 28
#   gated sensen_slim files happen to route through a cuTile/CUTLASS kernel.
#   That possibility was judged not worth reintroducing the manual-sync
#   fragility documented above: -DENABLE_CUDA=ON is already an opt-in to
#   sensen's full GPU toolchain, and whoever sets it is already expected to
#   have that toolchain -- CUTLASS included -- available, the same way this
#   file's own gp_ara/Z3 pieces already expect their optional dependencies
#   to be present when the feature that needs them is turned on.
#
# UNVERIFIED locally -- this machine has no nvcc, see this task's own report
# for the full statement. Reproduction command for whoever runs this on the
# GPU box (the three non-obvious requirements below are already established
# by a prior successful sensen-alone CUDA build at commit f2615167, not
# reproven here):
#
#     SKIP_CLANG_TIDY=1 CC=/usr/bin/clang-22 CXX=/usr/bin/clang++-22 \
#         cmake -S backend -B backend/build-cuda -G Ninja \
#         -DENABLE_CUDA=ON -DCMAKE_BUILD_TYPE=Release
#     cmake --build backend/build-cuda --target calculator_engine
#
# (SKIP_CLANG_TIDY works as EITHER form now. It used to have to be an
# environment variable, because sensen checked only $ENV{SKIP_CLANG_TIDY} --
# which also meant the `set(SKIP_CLANG_TIDY ON CACHE BOOL ... FORCE)` this file
# does at line 458 was accepted and then ignored, so the disable this project
# thought it had was never in force. Fixed in sensen (`SENSEN_SKIP_TIDY` folds
# both forms into one predicate), so the cache variable above now does what it
# says and the env var below remains valid;
# compilers must be named explicitly, since auto-detection has been observed
# to fall through to GCC on this class of machine; submodules need
# `git submodule update --init --recursive` first if this is a fresh clone.)
if(ENABLE_CUDA)
    # PRIVATE: this only changes which function bodies compile inside
    # sensen_slim's own translation units. C++20/23 modules do not leak
    # preprocessor macros across the import boundary -- a consumer's BMI-
    # visible interface shape was already fixed when sensen_slim's modules
    # were compiled, so calculator_engine and test_runner need no matching
    # definition of their own to see the CUDA-enabled interface correctly.
    # This is a different situation from _LIBCPP_NO_ABI_TAG at the top of
    # this file, which changes name mangling and therefore genuinely must
    # agree, whole-binary, across an ODR boundary a compile definition alone
    # cannot express; SENSEN_HAS_CUDA has no such cross-TU ABI concern.
    target_compile_definitions(sensen_slim PRIVATE SENSEN_HAS_CUDA)

    # PUBLIC: sensen_slim's compiled objects call into sensen_cuda's
    # extern "C" kernels, so the *link* step -- not just sensen_slim's own
    # compile -- needs sensen_cuda's symbols resolved. Any consumer that
    # links sensen_slim (calculator_engine, test_runner, ...) must therefore
    # also link sensen_cuda transitively, exactly mirroring how sensen's own
    # sensen_objects links it (sensen/src/CMakeLists.txt:634-639) and for
    # the identical reason given there.
    target_link_libraries(sensen_slim PUBLIC sensen_cuda CUDA::cudart)

    # Cheap insurance, not strictly required: the 28 gated files already
    # #include their CUDA bridge headers via a "cuda/..." quoted path
    # relative to their own location in ${SENSEN_SRC}, which resolves
    # without this on every compiler tested so far. Added anyway because
    # sensen_objects adds the identical include directory for the same
    # files (sensen/src/CMakeLists.txt:641) -- keeping sensen_slim's search
    # path a superset of sensen_objects' avoids a future bridge header that
    # itself uses an unprefixed quoted include (relative to ITS OWN
    # location under .../cuda/) becoming a sensen_slim-only build break.
    target_include_directories(sensen_slim PRIVATE "${SENSEN_SRC}/cuda")

    message(STATUS "  sensen_slim: SENSEN_HAS_CUDA + sensen_cuda linked in "
                    "(GPU-capable calculator_engine build; UNVERIFIED on "
                    "this machine -- no nvcc here, see this file's own "
                    "comment above for the reproduction command)")
endif()

# Include StochasticGraphExecutionEngine for distributed execution.
#
# SGEE's own standard is `import std;` against libc++, with std.pcm built from
# libc++'s module sources — that is how it builds standalone and how it is
# documented. This setting keeps it on the header-shim half of that pair, and
# the reason has changed even though the setting has not.
#
# It used to be here because the rest of the binary was libstdc++ and SGEE's
# native path would have dragged libc++ in alongside it. That conflict is gone:
# everything here is libc++ now, so SGEE's standard LIBRARY is no longer in
# question — it follows CANONICAL_FLAGS onto libc++ like every other
# subdirectory, whichever std mechanism it uses.
#
# What SGEE_NO_IMPORT_STD selects is the std MODULE mechanism, which is a
# genuinely separate axis from the standard library. It is now OFF, so SGEE
# builds the way it documents and the way this project's own code policy
# specifies (docs/PRD_OPTIONS_AND_FUTURES_CALCULATOR.md §2): `import std;`.
#
# This setting was ON for a long time, and the reason it gave was avoiding "the
# whole class of BMI failures where a std module compiled under one set of flags
# is rejected by a consumer compiled under another". That risk is real, but
# avoiding it by having no std module at all was answering the wrong question.
# The right answer is ONE std module built from the UNION of the flags, which is
# what SGEE_EXTERNAL_STD_PCM below supplies:
#
#   * CANONICAL_FLAGS goes into CMAKE_CXX_FLAGS at the top of this file, BEFORE
#     every add_subdirectory, so it is a prefix of what sensen, SGEE and this
#     directory each compile with. It is already the union.
#   * sensen precompiles std.pcm from exactly ${CMAKE_CXX_FLAGS}, so that one
#     BMI is compatible with every consumer in this tree by construction.
#   * Pointing SGEE at that same file means no second std.pcm exists to
#     disagree with the first. A mismatch needs two, and there is one.
#
# The two subprojects still agree with each other, which was the other thing the
# old setting bought -- they now agree on `import std;` rather than on the shim.
set(SGEE_NO_IMPORT_STD OFF CACHE BOOL "Use SGEE's header shim rather than import std; the standard library is libc++ either way" FORCE)
set(SGEE_EXTERNAL_STD_PCM "${CMAKE_BINARY_DIR}/std_modules/std.pcm" CACHE FILEPATH
    "The single std.pcm this tree builds; SGEE imports it rather than compiling a second one" FORCE)
set(SGEE_USE_GRPC ON CACHE BOOL "Build the gRPC consensus transport backend for SGEE" FORCE)

# SGEE needs the directory holding the logger BMI when a parent already provides
# the `logger` target, and only this tree knows where that is. Previously this
# path was hardcoded inside SGEE's own CMakeLists, which made SGEE unbuildable
# for anyone whose layout differed.
#
# The directory is named after the REAL target, which is not `logger`.
# cpp23-logger builds "${CPP23_LOGGER_TARGET_PREFIX}logger" and makes `logger`
# an ALIAS onto it, so with sensen's "sgee_" prefix the BMIs land in
# sgee_logger.dir. Naming logger.dir here pointed SGEE at a directory that only
# exists as a leftover from an older configure -- so `import
# logger.code_generator` in SGEE's transpiler silently resolved against a
# six-day-old BMI compiled with different flags, and the errors it produced
# ("no known conversion from 'const std::string_view' to 'std::string_view'")
# name neither the module system nor the stale file. Derive it from the target
# name so it cannot drift out of step again.
if(NOT CPP23_LOGGER_TARGET_NAME)
    message(FATAL_ERROR
        "CPP23_LOGGER_TARGET_NAME is unset -- sensen's cpp23-logger did not configure. "
        "SGEE's logger BMI path is derived from it and would otherwise be guessed.")
endif()
set(SGEE_LOGGER_MODULE_DIR
    "${CMAKE_BINARY_DIR}/sensen/external/fastestjsoninthewest/external/cpp23-logger/CMakeFiles/${CPP23_LOGGER_TARGET_NAME}.dir"
    CACHE PATH "Directory containing the logger module BMI for SGEE" FORCE)

add_subdirectory(external/SGEE sgee EXCLUDE_FROM_ALL)

# SGEE imports the same BMI as everything else here (SGEE_EXTERNAL_STD_PCM
# above), so it needs the same ordering edge, and this is the first point at
# which its targets exist. SGEE already routes every one of its targets through
# sgee_std_module -- that is the whole purpose of the stub target it declares
# when it is not building a std module of its own -- so one edge here covers all
# of them without this file naming any SGEE target individually.
if(TARGET sgee_std_module AND TARGET std_module_precompile)
    add_dependencies(sgee_std_module std_module_precompile)
endif()

# Include logging and bindings submodules
# add_subdirectory(cpp23-logger EXCLUDE_FROM_ALL)

# Core engine binary
add_executable(calculator_engine src/main.cpp src/issue_key.cpp src/modules/calculator_service.cpp src/modules/finance_service.cpp src/modules/quota.cpp src/modules/api_key.cpp src/modules/assistant_service.cpp src/modules/mortgage_assistant_service.cpp src/modules/inference_queue.cpp src/modules/inference_admission.cpp src/modules/sgee_queue_client.cpp src/modules/strategy_store.cpp)
target_sources(calculator_engine PUBLIC FILE_SET CXX_MODULES FILES src/modules/calculator_service.cppm src/modules/market_data.cppm src/modules/finance_service.cppm src/modules/quota.cppm src/modules/api_key.cppm src/modules/strategy_catalogue.cppm src/modules/assistant_verification.cppm src/modules/assistant_service.cppm src/modules/mortgage_verification.cppm src/modules/mortgage_grammar.cppm src/modules/mortgage_assistant_service.cppm src/modules/pg.cppm src/modules/inference_queue.cppm src/modules/sgee_queue_client.cppm src/modules/inference_admission.cppm src/modules/strategy_store.cppm)
target_link_libraries(calculator_engine PRIVATE proto_lib sensen_slim fastjson logger httplib::httplib OpenSSL::SSL OpenSSL::Crypto sgee)
# PostgreSQL::PostgreSQL is linked into calculator_engine further down, right
# after find_package(PostgreSQL REQUIRED) runs (see that comment block) --
# CMake allows target_link_libraries() to be called more than once for the
# same target, so this does not need find_package() hoisted above this point.


# Python Bindings via nanobind, OFF by default.
#
# Two live production sites (optionsandfuturescalculator.com,
# mortgagefvcalculator.com) are served by this same CMakeLists.txt through
# `docker build -t options-backend backend/`, and that image build must not
# suddenly acquire a hard Python3-development dependency it never needed
# before. Gating the whole block behind an option, rather than putting
# find_package(Python ... REQUIRED) at top level, is what keeps that true:
# the option defaults OFF, so an ordinary configure -- including the
# Dockerfile's -- never evaluates this branch at all.
#
# find_package(Python ... Development.Module ...), not Development: nanobind
# 2.14's own nanobind-config.cmake (cmake/nanobind-config.cmake:4) asserts
# `find_package(Python COMPONENTS Interpreter Development REQUIRED)` was
# already called and reads Python_VERSION/Python_EXECUTABLE/Python_SOABI --
# the unversioned FindPython module, not FindPython3. The full Development
# component additionally requires a libpython*.so to link against, which a
# Python built without --enable-shared does not ship and an extension module
# does not need (CPython extension modules are loaded into an already-running
# interpreter, not linked against one); Development.Module is nanobind's own
# documented recommendation for exactly this reason and is enough to satisfy
# nanobind-config.cmake's checks.
#
# The bindings target itself only needs market_data.cppm -- see bindings.cpp's
# file banner for why calculator_service.cppm (gRPC-only exports) and
# pricing_engine.cppm (test-only, not part of calculator_engine's own
# FILE_SET) are not on this surface. market_data.cppm is compiled again here
# as this target's own FILE_SET source rather than reused from
# calculator_engine's build, the same way test_market_data_resilience below
# already recompiles it into ITS own target: CMake's C++ module support
# resolves a FILE_SET's BMI within the target that declares it, and sharing
# one across targets is exactly the cross-target BMI reuse config/cpp_details.txt
# rule 50 warns compiles-with-different-flags into a rejected PCM. Recompiling
# it as a second, self-contained FILE_SET sidesteps that whole class of
# failure at the cost of one extra compile of one file.
#
# Link line: fastjson, logger, httplib::httplib, OpenSSL::SSL/Crypto, sgee --
# exactly what test_market_data_resilience links for the same module, and for
# the same reason (see that target's own comment): proto_lib and sensen_slim
# are calculator_service's and pricing_engine's dependencies respectively, and
# market_data.cppm imports neither. The previous, commented-out version of
# this block named `sensen` on this line; no target by that name exists in
# this build -- sensen's own CMakeLists exports the curated subset actually
# used here as `sensen_slim` -- and market_data.cppm does not need even that.
# Named BUILD_ENGINE_PYTHON_BINDINGS, not BUILD_PYTHON_BINDINGS: sensen's own
# CMakeLists.txt already defines an option by that exact name, and line ~406
# above forces it to CACHE ... FORCE OFF before add_subdirectory(sensen ...)
# to keep sensen from building its own bindings. FORCE means that set() wins
# over anything else that touches the same cache variable -- an option()
# here under the same name, or a -DBUILD_PYTHON_BINDINGS=ON on the configure
# command line, would both be silently overwritten back to OFF by that FORCE,
# and the mismatch would surface as nothing louder than a build that quietly
# never enables this block. A different name sidesteps the collision instead
# of relying on remembering the ordering next to it.
option(BUILD_ENGINE_PYTHON_BINDINGS
    "Build the nanobind Python bindings (options_futures_engine) for the market-data engine surface. OFF by default: the production container build must not acquire a Python3 development dependency."
    OFF)

if(BUILD_ENGINE_PYTHON_BINDINGS)
    find_package(Python COMPONENTS Interpreter Development.Module REQUIRED)
    add_subdirectory(nanobind EXCLUDE_FROM_ALL)

    # PROTECT_STACK and NOMINSIZE: without them, nanobind_add_module's own
    # Release-build defaults add -fno-stack-protector and -Os
    # (nanobind-config.cmake's nanobind_disable_stack_protector/
    # nanobind_opt_size) to THIS target -- which market_data.cppm's own
    # FILE_SET compilation below is also part of, since FILE_SET sources
    # inherit their owning target's compile options like any other source.
    # market_data.cppm imports the fastjson/logger/sgee BMIs that
    # calculator_engine already built under CANONICAL_FLAGS
    # (-fstack-protector-strong -O3, config/cpp_details.txt rule 50), and
    # clang's module-file-config-mismatch check rejects a PCM built with a
    # different stack-protector mode or optimization level than the
    # compilation trying to import it -- confirmed by hitting exactly that
    # error ("stack protector mode differs in precompiled file ...
    # fastjson.pcm") before these two flags were added. This is the answer to
    # "does nanobind_add_module handle FILE_SET CXX_MODULES sources": it does,
    # but only once its own opinionated Release-mode flags are turned off for
    # any target that also imports a module compiled under this repo's
    # canonical flags.
    nanobind_add_module(
        options_futures_engine
        PROTECT_STACK
        NOMINSIZE
        src/bindings.cpp
    )
    target_sources(options_futures_engine
        PRIVATE
        FILE_SET CXX_MODULES FILES
            src/modules/market_data.cppm
    )
    target_link_libraries(options_futures_engine PRIVATE fastjson logger httplib::httplib OpenSSL::SSL OpenSSL::Crypto sgee)

    # enable_testing() has not run yet at this point in the file -- it is
    # called once, unconditionally, a little further down -- but add_test()
    # only registers a test with whatever CTest state exists at generate
    # time, so calling it here ahead of that line is fine; the CoreEngineTest
    # and MarketDataResilienceTest registrations later in this file behave
    # identically with respect to ordering, and depend on the same fact.
    add_test(
        NAME PythonBindingsTest
        COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_python_bindings.py
    )
    # LD_PRELOAD=libjemalloc: fastjson (sensen/external/fastestjsoninthewest)
    # links jemalloc (found via find_library(JEMALLOC_LIBRARY jemalloc), same
    # cache variable reused here) and pulls it transitively into
    # options_futures_engine. A normal executable links it at process start
    # and reserves static TLS space for it up front; a Python extension
    # module is dlopen()'d by the interpreter well after Python's own TLS
    # layout is fixed, and jemalloc's arenas want initial-exec TLS -- so
    # `import options_futures_engine` failed outright with "cannot allocate
    # memory in static TLS block" until jemalloc was preloaded instead,
    # which reserves its TLS slots before the interpreter (or anything it
    # loads) claims the rest. Verified directly: the bare
    # `python3 -c "import options_futures_engine"` reproduces the failure
    # without this and succeeds with it.
    set_tests_properties(PythonBindingsTest PROPERTIES
        ENVIRONMENT "PYTHONPATH=$<TARGET_FILE_DIR:options_futures_engine>;LD_PRELOAD=${JEMALLOC_LIBRARY}"
    )
endif()

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
target_link_libraries(test_runner PRIVATE proto_lib sensen_slim fastjson logger httplib::httplib OpenSSL::SSL OpenSSL::Crypto sgee)

add_test(NAME CoreEngineTest COMMAND test_runner)

# Retry-with-backoff + circuit-breaker orchestration around market_data.cppm's
# network fetches (fetch_with_resilience, built on SGEE's own
# sgee.runtime.resilience::BackoffPolicy / CircuitBreaker). Exercises the
# retry/backoff/circuit-breaker ORCHESTRATION against a fake FetchAttempt --
# no real socket -- so it needs the same module + link surface market_data.cppm
# itself needs (fastjson/logger/httplib/OpenSSL) plus sgee, but not proto_lib
# or sensen_slim, neither of which market_data.cppm depends on.
add_executable(test_market_data_resilience tests/test_market_data_resilience.cpp)
target_sources(test_market_data_resilience
    PRIVATE
    FILE_SET CXX_MODULES FILES
        src/modules/market_data.cppm
)
target_link_libraries(test_market_data_resilience PRIVATE fastjson logger httplib::httplib OpenSSL::SSL OpenSSL::Crypto sgee)

add_test(NAME MarketDataResilienceTest COMMAND test_market_data_resilience)

add_executable(test_option_chain_cache tests/test_option_chain_cache.cpp)
target_sources(test_option_chain_cache
    PRIVATE
    FILE_SET CXX_MODULES FILES
        src/modules/market_data.cppm
)
target_link_libraries(test_option_chain_cache PRIVATE fastjson logger httplib::httplib OpenSSL::SSL OpenSSL::Crypto sgee)

add_test(NAME OptionChainCacheTest COMMAND test_option_chain_cache)

# GP-ARA mandatory verification stage for the strategy assistant
# (assistant_verification.cppm). Deliberately proto- and gRPC-free (see that
# module's own file banner) AND deliberately does NOT link sensen_slim: the
# only sensen module this stage needs, sensen.gp_ara_interfaces, has zero
# dependencies beyond std (no TBB, no CUDA backend, no Z3 -- see that
# module's own imports), so it is compiled here directly as this target's
# own FILE_SET source, the same way calculator_engine already compiles
# strategy_catalogue.cppm as its own source rather than pulling it from a
# library. This keeps the test buildable and independently runnable even
# when unrelated modules elsewhere in the (large, actively-developed)
# sensen_slim closure are mid-edit and momentarily non-building -- exactly
# the situation this target was first built under.
add_executable(test_assistant_verification tests/test_assistant_verification.cpp)
target_sources(test_assistant_verification
    PRIVATE
    FILE_SET CXX_MODULES FILES
        sensen/src/gp_ara_interfaces.cppm
        src/modules/strategy_catalogue.cppm
        src/modules/assistant_verification.cppm
)

add_test(NAME AssistantVerificationTest COMMAND test_assistant_verification)

# GP-ARA mandatory verification stage for the MORTGAGE assistant. Same
# rationale as the options block above: proto- and gRPC-free, and deliberately
# NOT linked against sensen_slim -- the only sensen module it needs,
# sensen.gp_ara_interfaces, has no dependencies beyond std, so it is compiled
# here as this target's own FILE_SET source.
add_executable(test_mortgage_verification tests/test_mortgage_verification.cpp)
target_sources(test_mortgage_verification
    PRIVATE
    FILE_SET CXX_MODULES FILES
        sensen/src/gp_ara_interfaces.cppm
        src/modules/mortgage_verification.cppm
)
# The label-space drift check re-parses finance.proto at RUN time, so the test
# fails rather than silently skipping if the path is wrong. The test also falls
# back to $FINANCE_PROTO_PATH and to ../proto/finance.proto (which resolves when
# ctest runs from the build dir); this define is belt and braces.
target_compile_definitions(test_mortgage_verification PRIVATE
    MORTGAGE_FINANCE_PROTO_PATH="${CMAKE_CURRENT_SOURCE_DIR}/proto/finance.proto")

add_test(NAME MortgageVerificationTest COMMAND test_mortgage_verification)

# Proto-derived CONSTRAINED DECODING for the mortgage assistant
# (mortgage_grammar.cppm). Same rationale as the verification blocks above:
# proto- and gRPC-free, and deliberately NOT linked against sensen_slim. It
# needs two sensen modules -- sensen.gp_ara_interfaces (via
# mortgage_verification) and sensen.grammar, whose IGrammar contract this
# module implements -- and neither depends on anything beyond std, so both are
# compiled here as this target's own FILE_SET sources.
add_executable(test_mortgage_grammar tests/test_mortgage_grammar.cpp)
target_sources(test_mortgage_grammar
    PRIVATE
    FILE_SET CXX_MODULES FILES
        sensen/src/gp_ara_interfaces.cppm
        sensen/src/grammar.cppm
        src/modules/mortgage_verification.cppm
        src/modules/mortgage_grammar.cppm
)
# Both structural gates read a real file at RUN time -- the label-space drift
# check re-parses finance.proto, and the control set is the gold val.jsonl --
# so the test FAILS rather than silently skipping if a path is wrong.
target_compile_definitions(test_mortgage_grammar PRIVATE
    MORTGAGE_FINANCE_PROTO_PATH="${CMAKE_CURRENT_SOURCE_DIR}/proto/finance.proto"
    MORTGAGE_VAL_JSONL_PATH="${CMAKE_CURRENT_SOURCE_DIR}/../agent/dataset/data_mortgage/val.jsonl")

add_test(NAME MortgageGrammarTest COMMAND test_mortgage_grammar)

# Formal Z3-backed automated reasoning for SGEE workflow graphs
# (OptionsWorkflow, FinanceRequestLifecycle, StrategyAssistantWorkflow, MortgageAssistantWorkflow).
# Encodes architectural invariants (action bindings, reachability, termination, error routing,
# mortgage refusal isolation) as SMT-LIB2 formulas and discharges them via sensen GP-ARA Z3Reasoner.
add_executable(test_sgee_automated_reasoning tests/test_sgee_automated_reasoning.cpp)
target_sources(test_sgee_automated_reasoning
    PRIVATE
    FILE_SET CXX_MODULES FILES
        sensen/src/gp_ara_interfaces.cppm
)
target_link_libraries(test_sgee_automated_reasoning PRIVATE sgee ${Z3_LIBRARY})
target_compile_definitions(test_sgee_automated_reasoning PRIVATE SENSEN_HAS_Z3)
target_include_directories(test_sgee_automated_reasoning PRIVATE /usr/include/z3)

add_test(NAME SgeeAutomatedReasoningTest COMMAND test_sgee_automated_reasoning)

# Option EXERCISE STYLE (European/American/Bermudan) and ASIAN option coverage
# for the sensen.finance.Finance PriceOptionTree RPC, at the gRPC service
# layer -- i.e. through options_calculator::finance::RegisterFinanceService on
# a real in-process grpc::Server, not by calling sensen's pricer directly. The
# pricer itself (backend/sensen/src/options.cppm) is read-only for this test;
# what is under test is finance_service.cpp's request validation, defaulting,
# and marshalling around it. Needs the same module set finance_service.cpp
# itself imports (quota, api_key) as its own FILE_SET sources, exactly the
# way calculator_engine already builds them, and proto_lib/sensen_slim to
# link real sensen pricing code and the generated finance.pb.h stubs.
add_executable(test_option_pricing_service
    tests/test_option_pricing_service.cpp
    src/modules/finance_service.cpp
    src/modules/quota.cpp
    src/modules/api_key.cpp
)
target_sources(test_option_pricing_service
    PRIVATE
    FILE_SET CXX_MODULES FILES
        src/modules/finance_service.cppm
        src/modules/quota.cppm
        src/modules/api_key.cppm
)
target_link_libraries(test_option_pricing_service PRIVATE proto_lib sensen_slim fastjson logger httplib::httplib OpenSSL::SSL OpenSSL::Crypto sgee)

add_test(NAME OptionPricingServiceTest COMMAND test_option_pricing_service)

# Input-validation audit sweep of the REST of sensen.finance.Finance -- every
# RPC other than PriceOptionTree, which the commit above already covers.
# Same shape as test_option_pricing_service above: a real in-process
# grpc::Server through a real Finance::Stub, exercising finance_service.cpp's
# request validation rather than sensen's pricers/solvers directly (those are
# backend/sensen/**, pinned/read-only for this test).
add_executable(test_finance_service_validation
    tests/test_finance_service_validation.cpp
    src/modules/finance_service.cpp
    src/modules/quota.cpp
    src/modules/api_key.cpp
)
target_sources(test_finance_service_validation
    PRIVATE
    FILE_SET CXX_MODULES FILES
        src/modules/finance_service.cppm
        src/modules/quota.cppm
        src/modules/api_key.cppm
)
target_link_libraries(test_finance_service_validation PRIVATE proto_lib sensen_slim fastjson logger httplib::httplib OpenSSL::SSL OpenSSL::Crypto sgee)

add_test(NAME FinanceServiceValidationTest COMMAND test_finance_service_validation)

# CalculateStrategy coverage -- EVERY strategy calculation on
# optionsandfuturescalculator.com goes through this RPC and SGEE's graph
# interpreter, and until this target it had zero automated coverage (see
# tests/test_calculator_service.cpp's own header for the stale file this
# replaces and why it was worse than absent). Same shape as
# test_option_pricing_service and test_finance_service_validation above: a
# real in-process grpc::Server through a real OptionsCalculator::Stub, plus
# RegisterCalculatorServiceForTest (calculator_service.cppm/.cpp) for the
# discriminating section that proves interpreter.Run()'s postconditions
# actually catch a partial action registry, not just assert they exist.
# Needs market_data.cppm in addition to quota/api_key (unlike the finance
# tests) because calculator_service.cpp imports it directly for live quotes.
# strategy_store: this test compiles the INTERFACE unit plus
# tests/strategy_store_stub.cpp -- a test-only module implementation unit whose
# make_pg_strategy_store returns nullptr (see that file's own banner for why the
# stub cannot live in the test .cpp). src/modules/strategy_store.cpp, the
# pg-backed implementation, is
# deliberately NOT listed, which is what keeps this target free of
# PostgreSQL::PostgreSQL, exactly as the invariant block further down claims.
# The saved-scenario RPCs are exercised here against an in-memory fake store
# injected through RegisterCalculatorServiceForTest, so they get real coverage
# with no database and no libpq.
add_executable(test_calculator_service
    tests/test_calculator_service.cpp
    tests/strategy_store_stub.cpp
    src/modules/calculator_service.cpp
    src/modules/quota.cpp
    src/modules/api_key.cpp
)
target_sources(test_calculator_service
    PRIVATE
    FILE_SET CXX_MODULES FILES
        src/modules/calculator_service.cppm
        src/modules/market_data.cppm
        src/modules/quota.cppm
        src/modules/api_key.cppm
        src/modules/strategy_store.cppm
)
target_link_libraries(test_calculator_service PRIVATE proto_lib sensen_slim fastjson logger httplib::httplib OpenSSL::SSL OpenSSL::Crypto sgee)

add_test(NAME CalculatorServiceTest COMMAND test_calculator_service)

# Unit coverage for the entitlement gate itself (check_strategy_entitlement /
# check_assistant_entitlement) discriminating WHY an identity is not Pro --
# malformed key vs unknown key vs revoked key vs no key at all vs a genuinely
# free-tier identity -- rather than collapsing all of them into the same "is
# a Pro feature" message. See tests/test_api_key_entitlement.cpp's own header
# for the mortgagefvcalculator.com incident this fixes.
#
# Deliberately proto- and gRPC-service-free, unlike the three targets above:
# the gate functions under test take an Identity directly and return a
# grpc::Status with no I/O, so this needs only api_key.cppm/.cpp (for
# fastjson-backed FINANCE_API_KEYS parsing and OpenSSL-backed hashing, even
# though this file's hand-built Identity values never reach KeyRegistry) and
# grpc++ (for grpc::Status/StatusCode, which api_key.cppm's own interface
# exposes) -- not proto_lib, not sensen_slim, not quota.cppm, none of which
# the gate touches.
add_executable(test_api_key_entitlement
    tests/test_api_key_entitlement.cpp
    src/modules/api_key.cpp
)
target_sources(test_api_key_entitlement
    PRIVATE
    FILE_SET CXX_MODULES FILES
        src/modules/api_key.cppm
)
target_link_libraries(test_api_key_entitlement PRIVATE grpc++ fastjson logger OpenSSL::SSL OpenSSL::Crypto)

add_test(NAME ApiKeyEntitlementTest COMMAND test_api_key_entitlement)

# ---------------------------------------------------------------------------
# Quota refusal labelling.
#
# quota.cppm + quota.cpp only, plus grpc++ for the grpc::Status the enforcer
# returns and fastjson/logger for QUOTA_POLICY parsing and the one-shot
# undefined-tier warning. No api_key.cpp: this gate is scoped to what the
# enforcer does with a tier NAME it is handed, not to how an identity is
# authenticated into one.
add_executable(test_quota_tier_label
    tests/test_quota_tier_label.cpp
    src/modules/quota.cpp
)
target_sources(test_quota_tier_label
    PRIVATE
    FILE_SET CXX_MODULES FILES
        src/modules/quota.cppm
)
target_link_libraries(test_quota_tier_label PRIVATE grpc++ fastjson logger)

add_test(NAME QuotaTierLabelTest COMMAND test_quota_tier_label)

# ---------------------------------------------------------------------------
# Shared inference queue substrate (pg.cppm + inference_queue.cppm), and the
# admission layer built on top of it (inference_admission.cppm) -- the
# Postgres-backed queue that lets more than one engine replica share the two
# fine-tuned Qwen3-0.6B assistants as a pool of workers, wired into
# assistant_service.cpp/mortgage_assistant_service.cpp behind the
# INFERENCE_QUEUE env var (default `local`, unchanged behaviour;
# `postgres` opts in). THIS IS NO LONGER "a later, separate wave" -- this
# comment used to say wiring a caller/worker was future work; that wave is
# this commit, and calculator_engine (below, via its own
# `target_link_libraries(calculator_engine PRIVATE PostgreSQL::PostgreSQL)`
# right after find_package() runs) DOES now link PostgreSQL::PostgreSQL,
# because assistant_service.cpp and mortgage_assistant_service.cpp both
# `import inference_admission;` (which itself imports `inference_queue`,
# which imports `pg`).
#
# WHAT STILL HOLDS, UNCHANGED: finance_service.cpp/finance_service.cppm and
# calculator_service.cpp/calculator_service.cppm still do not, and must not,
# `import pg;`/`import inference_queue;`/`import inference_admission;`,
# directly or transitively.
#
# calculator_service.cpp DOES now `import strategy_store;`, for the
# SaveStrategy/ListStrategies/DeleteStrategy RPCs, and that does NOT weaken the
# claim -- it is the reason strategy_store is split into two units at all.
# strategy_store.cppm (the INTERFACE) names no pg type and imports only `std`;
# `import pg;` appears solely in strategy_store.cpp (the module IMPLEMENTATION
# unit), whose imports are not re-exported. A naive single-file module would
# have put pg in the interface and silently destroyed this invariant for every
# RPC in calculator_service.cpp, pricing ones included.
#
# Verified mechanically rather than by reading, on the built objects:
#   nm -uC calculator_service.cpp.o | grep -c 'pg::'   -> 0
#   nm -uC strategy_store.cpp.o     | grep -c 'pg::'   -> 11
#   nm -uC pg.cppm.o                | grep -c 'PQ'     -> 18
# That is the boundary, and it is where it is supposed to be. Re-run those
# three commands if this ever needs re-proving; a change that moves the first
# number off zero has broken the invariant regardless of what any comment says.
#
# Neither file is listed in ANY of
# test_finance_service_validation's, test_option_pricing_service's or
# test_calculator_service's sources/FILE_SETs below, none of which gained
# PostgreSQL::PostgreSQL either. That is what makes "the Finance/Calculator
# RPCs are structurally incapable of touching libpq" true of those two
# services' own translation units specifically -- calculator_engine as a
# WHOLE binary links libpq now (for the assistant services), which is
# expected and does not weaken that claim: C++20 modules only expose what a
# translation unit explicitly imports, so finance_service.cpp/
# calculator_service.cpp have zero reachable symbols from pg/inference_queue/
# inference_admission regardless of what else shares their final link line.
#
# libpq is a C API (see pg.cppm's own banner for why that means no libc++/
# libstdc++ ABI concern), so find_package(PostgreSQL) here needs no special
# handling for this otherwise-libc++ build -- unlike SGEE's own gated
# SGEE_USE_POSTGRES path (external/SGEE/CMakeLists.txt ~692-699), this one is
# unconditional: libpq-dev/libpq5 are already in the builder/runtime images
# (Dockerfile:401,683) specifically in anticipation of this substrate.
#
# ORDERING: find_package(PostgreSQL) runs here, AFTER calculator_engine's own
# add_executable/target_sources/first target_link_libraries call far above.
# That is fine -- CMake resolves target_link_libraries() at generate time, not
# strictly in script order, and permits calling it more than once for the same
# target -- so calculator_engine's OWN `target_link_libraries(calculator_engine
# PRIVATE PostgreSQL::PostgreSQL)` is issued immediately below rather than
# hoisting this find_package() call earlier in the file.
find_package(PostgreSQL REQUIRED)

target_link_libraries(calculator_engine PRIVATE PostgreSQL::PostgreSQL)

# ---------------------------------------------------------------------------
# StrategyAssistant / MortgageAssistant SGEE-graph coverage.
#
# ParseStrategy and ParseOperation are now real SGEE workflow graphs (see the
# "SGEE execution context and graph actions" banner in each .cpp) instead of a
# chain of hand-rolled `if (...) return ...;` checks. Same shape as
# test_calculator_service just above: a real in-process grpc::Server through a
# real Stub, plus RegisterAssistantServiceForTest / RegisterMortgageAssistant
# ServiceForTest for the discriminating section that proves interpreter.Run()'s
# postconditions actually catch a partial action registry.
#
# Needs the same closure calculator_engine itself needs for these two files:
# sensen_slim (LLMPipeline/cuda_backend), market_data (assistant only --
# probe_symbol's live-quote check), quota/api_key (admission and the Pro
# gate), strategy_catalogue (assistant only), assistant_verification (both --
# shared prompt-injection/advice-request guards), mortgage_verification/
# mortgage_grammar (mortgage only), and the inference_admission/inference_
# queue/pg substrate both assistants sit on top of -- hence PostgreSQL::
# PostgreSQL, linked here (after find_package(PostgreSQL) above) for the same
# reason calculator_engine's own PostgreSQL link is deferred to this point
# rather than its initial declaration.
add_executable(test_assistant_service
    tests/test_assistant_service.cpp
    src/modules/assistant_service.cpp
    src/modules/quota.cpp
    src/modules/api_key.cpp
    src/modules/inference_queue.cpp
    src/modules/sgee_queue_client.cpp
    src/modules/inference_admission.cpp
)
target_sources(test_assistant_service
    PRIVATE
    FILE_SET CXX_MODULES FILES
        src/modules/assistant_service.cppm
        src/modules/market_data.cppm
        src/modules/quota.cppm
        src/modules/api_key.cppm
        src/modules/strategy_catalogue.cppm
        src/modules/assistant_verification.cppm
        src/modules/pg.cppm
        src/modules/inference_queue.cppm
        # inference_admission.cppm imports sgee_queue_client, so every target
        # that compiles it needs the module in its OWN file set -- a C++23
        # module is not a header and does not come along with the .cpp.
        src/modules/sgee_queue_client.cppm
        src/modules/inference_admission.cppm
)
target_link_libraries(test_assistant_service PRIVATE proto_lib sensen_slim fastjson logger httplib::httplib OpenSSL::SSL OpenSSL::Crypto sgee PostgreSQL::PostgreSQL)

add_test(NAME AssistantServiceTest COMMAND test_assistant_service)

add_executable(test_mortgage_assistant_service
    tests/test_mortgage_assistant_service.cpp
    src/modules/mortgage_assistant_service.cpp
    src/modules/quota.cpp
    src/modules/api_key.cpp
    src/modules/inference_queue.cpp
    src/modules/sgee_queue_client.cpp
    src/modules/inference_admission.cpp
)
target_sources(test_mortgage_assistant_service
    PRIVATE
    FILE_SET CXX_MODULES FILES
        src/modules/mortgage_assistant_service.cppm
        src/modules/mortgage_verification.cppm
        src/modules/mortgage_grammar.cppm
        src/modules/strategy_catalogue.cppm
        src/modules/assistant_verification.cppm
        src/modules/quota.cppm
        src/modules/api_key.cppm
        src/modules/pg.cppm
        src/modules/inference_queue.cppm
        # See test_assistant_service above: inference_admission.cppm's import of
        # sgee_queue_client must be satisfied per target.
        src/modules/sgee_queue_client.cppm
        src/modules/inference_admission.cppm
)
target_link_libraries(test_mortgage_assistant_service PRIVATE proto_lib sensen_slim fastjson logger httplib::httplib OpenSSL::SSL OpenSSL::Crypto sgee PostgreSQL::PostgreSQL)

add_test(NAME MortgageAssistantServiceTest COMMAND test_mortgage_assistant_service)

# sgee (not sensen_slim) for the same reason test_market_data_resilience links
# it above: the only sensen-family module this needs is
# sgee.runtime.resilience::CircuitBreaker, which is part of the standalone
# SGEE library target, not the 271-module sensen closure.
# Real-Postgres gate for the saved-scenario SQL. NOT registered via add_test()
# -- like test_inference_queue_pg below, it needs a live database, and a test
# that passes vacuously when one is absent is worse than no test. It returns 77
# (ctest's "skipped") when DATABASE_URL is unset. Run it explicitly:
#   DATABASE_URL="postgresql://postgres@127.0.0.1:55432/postgres" \
#     ./test_strategy_store_pg
add_executable(test_strategy_store_pg
    tests/test_strategy_store_pg.cpp
    src/modules/strategy_store.cpp
)
target_sources(test_strategy_store_pg
    PRIVATE
    FILE_SET CXX_MODULES FILES
        src/modules/pg.cppm
        src/modules/strategy_store.cppm
)
target_link_libraries(test_strategy_store_pg PRIVATE PostgreSQL::PostgreSQL logger sgee)

add_executable(test_inference_queue_pg
    tests/test_inference_queue_pg.cpp
    src/modules/inference_queue.cpp
)
target_sources(test_inference_queue_pg
    PRIVATE
    FILE_SET CXX_MODULES FILES
        src/modules/pg.cppm
        src/modules/inference_queue.cppm
)
target_link_libraries(test_inference_queue_pg PRIVATE PostgreSQL::PostgreSQL logger sgee)

# Not registered via add_test(): this suite needs a real, reachable Postgres
# (DATABASE_URL) and exercises real concurrency/timing (leases, sweeps, a
# killed connection), which makes it a poor fit for an unconditional `ctest`
# run on a machine that may have neither Postgres nor time to spare. Run it
# directly: DATABASE_URL=postgresql://... ./test_inference_queue_pg

# inference_admission.cppm's `local`-mode admission queue -- QueuedBackend's
# submit()/take_jobs()/drain_and_fail() -- against a trivial concrete
# subclass standing in for SensenBackend/LlamaCppBackend's decode loop. Needs
# NO Postgres: this is exactly the "lease_source_ stays null, behaviour is
# unchanged" proof the task's own testing brief asks for, so it IS registered
# via add_test() and runs on every `ctest` invocation.
add_executable(test_inference_admission
    tests/test_inference_admission.cpp
)
target_sources(test_inference_admission
    PRIVATE
    FILE_SET CXX_MODULES FILES
        src/modules/pg.cppm
        src/modules/inference_queue.cppm
        src/modules/sgee_queue_client.cppm
        src/modules/inference_admission.cppm
)
target_sources(test_inference_admission PRIVATE src/modules/inference_queue.cpp src/modules/sgee_queue_client.cpp src/modules/inference_admission.cpp)
target_link_libraries(test_inference_admission PRIVATE PostgreSQL::PostgreSQL fastjson logger sgee)

add_test(NAME InferenceAdmissionTest COMMAND test_inference_admission)

add_executable(test_sgee_queue_client
    tests/test_sgee_queue_client.cpp
)
target_sources(test_sgee_queue_client
    PRIVATE
    FILE_SET CXX_MODULES FILES
        src/modules/sgee_queue_client.cppm
)
target_sources(test_sgee_queue_client PRIVATE src/modules/sgee_queue_client.cpp)
target_link_libraries(test_sgee_queue_client PRIVATE logger sgee)

add_test(NAME SgeeQueueClientTest COMMAND test_sgee_queue_client)

# The queue-node entrypoint decides whether the three nodes come up on mutual
# TLS, on plaintext, or not at all. It is shell, it ships inside the queue-node
# image, and nothing else covers it -- a half-set trio of SGEE_TLS_*_B64 must
# refuse rather than quietly serve plaintext. Needs no node, no container and no
# network: it rewrites the script's exec and staging directory and asserts the
# DECISION.
add_test(NAME QueueNodeEntrypointTest
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/queue_node_entrypoint_test.sh)

# The Postgres-backed half of the admission layer: PostgresLeaseSource and
# PostgresAdmission, exercised through two separate process-local Queue/Pool
# instances sharing one real Postgres (one submits only, one leases only --
# see this file for the cross-worker scenario), plus the mandatory
# fallback-to-local path and fenced-completion discard, both proven THROUGH
# this admission layer rather than only at the raw Queue API level (which
# test_inference_queue_pg above already covers). Same reasoning as
# test_inference_queue_pg for why this is NOT registered via add_test():
# needs a real, reachable Postgres (DATABASE_URL). Run it directly:
# DATABASE_URL="host=... port=... dbname=... user=postgres" ./test_inference_admission_pg
add_executable(test_inference_admission_pg
    tests/test_inference_admission_pg.cpp
    src/modules/inference_queue.cpp
    src/modules/sgee_queue_client.cpp
    src/modules/inference_admission.cpp
)
target_sources(test_inference_admission_pg
    PRIVATE
    FILE_SET CXX_MODULES FILES
        src/modules/pg.cppm
        src/modules/inference_queue.cppm
        src/modules/sgee_queue_client.cppm
        src/modules/inference_admission.cppm
)
target_link_libraries(test_inference_admission_pg PRIVATE PostgreSQL::PostgreSQL fastjson logger sgee)

# End-to-end smoke client. Verifies a running engine returns live data on all
# three RPCs; used as the deploy gate.
add_executable(smoke_client src/smoke_client.cpp)
target_link_libraries(smoke_client PRIVATE proto_lib)

# assistant_load_probe: throwaway concurrency harness (see its source header).
add_executable(assistant_load_probe src/assistant_load_probe.cpp)
target_link_libraries(assistant_load_probe PRIVATE proto_lib)

# adv_client: throwaway adversarial-testing client for the strategy assistant
# security review (see its source header). Not part of the deploy gate.
add_executable(adv_client src/adv_client.cpp)
target_link_libraries(adv_client PRIVATE proto_lib)

# --------------------------------------------------------------------------
# llama.cpp / ggml -- upstream (ggml-org/llama.cpp), fetched and built inside
# this tree to prove, in this exact build with this exact toolchain, that it
# links and runs before anything here is asked to depend on it.
#
# This is the SECOND inference backend behind the assistant RPC's
# InferenceBackend interface (backend/src/modules/assistant_service.cpp), not
# a test fixture. sensen's own in-process stack -- import sensen.llm_pipeline;,
# see the "sensen.llm_pipeline addendum" comment above sensen_slim -- remains
# the production default, selected when ASSISTANT_BACKEND is unset. llama.cpp
# is what ASSISTANT_BACKEND=llamacpp selects instead.
#
# WHY CARRY A SECOND ENGINE AT ALL, given the first one works: because the
# first one's CPU inference path has already produced confidently wrong output
# for this exact model once, and what established that the model, the
# quantisation and the prompt were all fine -- and therefore that the defect
# was in the caller -- was upstream llama.cpp decoding the same GGUF
# correctly. A second, independently-implemented engine reachable by changing
# one environment variable turns that class of incident from an outage into a
# restart, and gives whoever is diagnosing the next one a reference answer
# rather than a hypothesis.
#
# ENABLE_LLAMACPP_BACKEND defaults ON, and when it is ON the engine itself
# links llama and is compiled with ASSISTANT_HAVE_LLAMACPP defined (see the
# bottom of this block). The option exists so the dependency can be dropped
# deliberately -- a configure with one fewer git clone -- and the service
# handles that honestly at runtime: asking for ASSISTANT_BACKEND=llamacpp in a
# binary built with this OFF logs that the build lacks the backend and leaves
# the assistant unavailable, rather than silently answering with sensen.
option(ENABLE_LLAMACPP_BACKEND "Fetch and build upstream llama.cpp (ggml-org/llama.cpp) as the selectable second assistant inference backend" ON)

if(ENABLE_LLAMACPP_BACKEND)
    # FetchContent over vendoring, for the same reason grpc's own FetchContent
    # block above gives: llama.cpp is a fast-moving upstream project with its
    # own CMake build already tuned for the kind of options this block needs
    # to override (GGML_NATIVE, GGML_OPENMP, LLAMA_BUILD_*), and vendoring a
    # copy into this repo would mean hand-tracking every upstream
    # CMakeLists.txt change -- new source files, new options -- on every
    # bump, which is exactly the maintenance burden FetchContent avoids.
    #
    # Pinning by GIT_TAG alone is not enough for a build that has to be
    # reproducible from a bare clone months or years later: a tag is a
    # mutable ref upstream can force-move (delete and re-push) without this
    # repository's own history recording it. GIT_TAG accepts a commit hash
    # directly, so the exact commit b6963 currently points to is pinned
    # alongside the human-readable tag in this comment -- a future re-tag of
    # b6963 upstream cannot silently change what this build fetches, only an
    # edit to this line can.
    FetchContent_Declare(
        llamacpp
        GIT_REPOSITORY https://github.com/ggml-org/llama.cpp.git
        GIT_TAG 6db3d1ffe6d34e8becfe95cf2f109198a7154db3 # b6963
    )

    # Build llama.cpp/ggml at C++17, the standard its own CMakeLists.txt pins
    # per-target (src/CMakeLists.txt: target_compile_features(llama PRIVATE
    # cxx_std_17)) rather than at this project's global C++23 -- the same
    # reasoning and the same mechanism as the SAVED_CXX_STANDARD save/restore
    # around grpc's FetchContent_MakeAvailable above. It matters here for the
    # same performance reason it mattered there: CMAKE_CXX_SCAN_FOR_MODULES
    # is ON globally, and a C++20-or-later effective standard is what makes
    # CMake's Ninja generator dependency-scan a target for `import`/`export
    # module` at all. Saving/restoring around this call keeps ~200
    # llama.cpp/ggml C++ sources -- none of which use C++20 modules -- out of
    # that scan.
    set(SAVED_CXX_STANDARD ${CMAKE_CXX_STANDARD})
    set(CMAKE_CXX_STANDARD 17)

    # llama.cpp's own top-level CMakeLists.txt calls `option(BUILD_SHARED_LIBS
    # ...)` with a platform default that is ON on Linux (the non-EMSCRIPTEN,
    # non-MINGW branch) -- left alone, fetching llama.cpp would silently
    # start building libggml*.so/libllama.so as shared objects.
    # BUILD_SHARED_LIBS is a CACHE variable, not a directory-scoped one: once
    # llama.cpp's option() call creates it, it is visible for the rest of
    # THIS configure run and, more importantly, to every add_library() call
    # on the NEXT configure that reuses this build's CMakeCache.txt --
    # including sensen's and gRPC's own, several of which do not specify
    # STATIC/SHARED and rely on this same variable's default. Forcing it OFF
    # here, matching the STATIC default every other target in this tree
    # already gets from BUILD_SHARED_LIBS being simply unset, is what keeps a
    # second, cached configure from silently changing what kind of library
    # sensen or gRPC produce depending on whether llama.cpp's CMakeLists
    # happened to run first that time. It also answers deliverable (2) on its
    # own terms: llamacpp_probe links llama/ggml directly into its own
    # executable, so a static archive needs no new shared object copied into
    # backend/Dockerfile's runtime stage -- a libllama.so or libggml*.so
    # would need exactly that, and the ldd-driven lib/-copy step in that
    # Dockerfile only greps for libc++/libunwind entries today.
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

    # ggml/src static objects are linked into calculator_engine-adjacent
    # executables the same way sensen_slim's are, and unlike sensen_slim (which
    # sets POSITION_INDEPENDENT_CODE ON per target, sensen_slim above) llama.cpp
    # only turns PIC on for itself `if (BUILD_SHARED_LIBS)` -- which is now
    # forced OFF, so without this, ggml's plain-C sources (ggml.c,
    # ggml-cpu.c, ...) would compile without -fPIC while lld links them into
    # a PIE executable (Ubuntu 24.04 clang's default), which is a real link
    # failure (`relocation R_X86_64_32 ... can not be used when making a PIE
    # object`), not a hypothetical one. CANONICAL_FLAGS's own -fPIC only
    # reaches CMAKE_CXX_FLAGS, never CMAKE_C_FLAGS (see that variable's
    # comment at the top of this file), so ggml's C sources get no PIC from
    # this project's own flags either. Setting the global CMake abstraction
    # instead of hand-adding -fPIC to CMAKE_C_FLAGS is deliberate: it is a
    # plain (non-CACHE) variable, so unlike BUILD_SHARED_LIBS above it does
    # not persist into a future configure's CMakeCache.txt, and it is
    # restored to its prior value immediately after this FetchContent call so
    # it cannot leak into any target defined after this block.
    set(SAVED_POSITION_INDEPENDENT_CODE ${CMAKE_POSITION_INDEPENDENT_CODE})
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)

    # GGML_NATIVE defaults ON upstream whenever the build is not
    # cross-compiling (ggml/CMakeLists.txt's GGML_NATIVE_DEFAULT), and when ON
    # it appends -march=native to every ggml/ggml-cpu compile
    # (ggml/src/ggml-cpu/CMakeLists.txt: `if (GGML_NATIVE) list(APPEND
    # ARCH_FLAGS -march=native)`). That is precisely the failure mode the long
    # comment above sensen_slim (this file, ~line 445) already documents for
    # sensen's own former -march=native: a binary tuned for whichever CPU
    # happens to run the compiler -- the Railway BUILDER, not the host that
    # runs the engine -- rather than the x86-64-v3/generic baseline
    # config/cpp_details.txt requires for cross-host FP parity and
    # durable-replay determinism. Forcing it OFF here applies the same fix to
    # a second subproject for the same reason.
    #
    # This does not leave ggml untuned. With GGML_NATIVE off and this a
    # native (non-cross) configure, ggml/CMakeLists.txt's own INS_ENB logic
    # still defaults GGML_SSE42, GGML_AVX, GGML_AVX2, GGML_FMA, GGML_F16C and
    # GGML_BMI2 all ON (GGML_AVX512 stays OFF regardless of GGML_NATIVE) --
    # exactly the instruction-set floor the x86-64-v3 microarchitecture level
    # names, applied through ggml's own explicit -m flags
    # (ggml/src/ggml-cpu/CMakeLists.txt's ARCH_FLAGS list) rather than
    # inherited from this project's CANONICAL_FLAGS, which never reaches
    # ggml's plain-C sources (CANONICAL_FLAGS only sets CMAKE_CXX_FLAGS, not
    # CMAKE_C_FLAGS -- see that variable's own comment above). Verified
    # directly against this checkout's ggml/src/ggml-cpu/CMakeLists.txt
    # rather than assumed from upstream documentation.
    set(GGML_NATIVE OFF CACHE BOOL "" FORCE)

    # GGML_OPENMP defaults ON upstream and, when a system OpenMP is found,
    # links libomp.so -- confirmed against the scratchpad prototype's own
    # build_libcxx/CMakeCache.txt, which measured its 4.4x parallel-decode
    # throughput figure with GGML_OPENMP=ON on this workstation, which
    # happens to have libomp installed (LLVM 22's own libomp package).
    # backend/Dockerfile's builder stage does not install libomp-*-dev, and
    # ggml/src/ggml-cpu/CMakeLists.txt's find_package(OpenMP) is not
    # REQUIRED, so a container configure would not fail outright -- it would
    # silently fall back to GGML_OPENMP_ENABLED=OFF with a warning. That
    # silent divergence is worse than an explicit choice: it means a
    # developer's workstation build and a container build would run
    # different ggml threading backends without either configure saying so
    # in a way anyone would notice.
    #
    # Turned OFF here instead, deliberately, matching what the answer would
    # be inside the container anyway, and matching how the rest of this
    # binary already parallelizes: CANONICAL_FLAGS already carries -pthread
    # globally, and sensen's own sensen.parallel module (see this file's TBB
    # comments above) uses TBB, not OpenMP, for its CPU parallelism -- one
    # fewer threading runtime this image has to carry, and one fewer .so for
    # backend/Dockerfile's ldd-driven lib/-copy step to have to widen its
    # grep for. Without OpenMP, ggml-cpu falls back to its own pthread-based
    # threadpool -- the same code path llama_context_params's own
    # n_threads/n_threads_batch (set directly by llamacpp_probe.cpp) already
    # configure -- so no parallelism capability is actually lost; confirmed
    # by building and running llamacpp_probe below with GGML_OPENMP off.
    set(GGML_OPENMP OFF CACHE BOOL "" FORCE)

    # Everything below disables upstream build products this project has no
    # use for. llamacpp_probe links only against llama.h's plain C API; it
    # needs neither llama.cpp's own example binaries, its test suite, its
    # HTTP server, its CLI tools, nor its "common" helper library (whose only
    # consumers are those examples/tools), and building any of them costs
    # real configure/build time for output nothing here runs.
    set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(LLAMA_BUILD_COMMON OFF CACHE BOOL "" FORCE)
    # LLAMA_CURL defaults ON upstream and, unlike GGML_OPENMP above, its
    # find_package(CURL) IS required when the option is on -- a missing
    # libcurl-dev fails configure outright rather than degrading gracefully.
    # This project's model is fetched and sha256-verified by
    # backend/Dockerfile's own `model` stage before the engine ever runs (see
    # that stage's comment); nothing needs llama.cpp's built-in "download a
    # GGUF from a URL" convenience feature, so there is no reason for this
    # build to depend on libcurl-dev being present at all.
    set(LLAMA_CURL OFF CACHE BOOL "" FORCE)
    # ccache speeds up a developer's repeat builds; it is not installed in
    # the Railway builder image (backend/Dockerfile's apt-get list), and a
    # CMake option silently probing for a tool the reproducible build does
    # not promise to provide is the kind of environment-dependent branch
    # CANONICAL_FLAGS's own comments (this file's top) already go out of
    # their way to avoid elsewhere.
    set(GGML_CCACHE OFF CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(llamacpp)

    set(CMAKE_CXX_STANDARD ${SAVED_CXX_STANDARD})
    set(CMAKE_POSITION_INDEPENDENT_CODE ${SAVED_POSITION_INDEPENDENT_CODE})

    # Standalone probe: proves llama.cpp links and runs in this build tree.
    # Deliberately a single, dependency-free translation unit against
    # llama.h's C API only -- no sensen, no proto, no grpc -- so a build or
    # run failure here can only mean upstream llama.cpp itself, never an
    # interaction with the rest of this codebase.
    add_executable(llamacpp_probe src/llamacpp_probe.cpp)
    target_link_libraries(llamacpp_probe PRIVATE llama)

    # THROWAWAY numeric-correctness audit harness (not product code). Loads the
    # same GGUF through BOTH upstream llama.cpp and sensen's LlamaModel CPU path
    # in one process and compares full-vocab logits token-for-token.
    add_executable(numeric_audit_probe src/numeric_audit_probe.cpp)
    target_link_libraries(numeric_audit_probe PRIVATE llama sensen_slim)

    add_executable(gguf_requantize_probe src/gguf_requantize_probe.cpp)
    target_link_libraries(gguf_requantize_probe PRIVATE llama)

    add_executable(dequant_parity_probe src/dequant_parity_probe.cpp)
    target_link_libraries(dequant_parity_probe PRIVATE llama sensen_slim)

    add_executable(tokenizer_parity_probe src/tokenizer_parity_probe.cpp)
    target_link_libraries(tokenizer_parity_probe PRIVATE llama sensen_slim)

    # LAYER-BY-LAYER, PRIMITIVE-BY-PRIMITIVE parity harness. Drives both engines
    # in one process on identical token ids and joins their intermediate tensors
    # by name: llama.cpp through `llama_context_params::cb_eval`, sensen through
    # `sensen::observe::setObserver` (sensen/src/tensor_observer.cppm). Links
    # ggml directly as well as llama because it reads tensor payloads via
    # ggml_backend_tensor_get / ggml_fp16_to_fp32.
    add_executable(layer_parity_probe src/layer_parity_probe.cpp)
    target_link_libraries(layer_parity_probe PRIVATE llama ggml sensen_slim)

    # Cost of the tensor-observer hook. Deliberately does NOT link llama.cpp:
    # it measures sensen alone, so reference-engine work cannot dilute the
    # number. Used to justify shipping the hook always-on.
    add_executable(observer_cost_probe src/observer_cost_probe.cpp)
    target_link_libraries(observer_cost_probe PRIVATE sensen_slim)

    # End-to-end generation throughput of the SERVING path, not a kernel
    # microbenchmark: same LLMPipeline construction assistant_service.cpp uses,
    # so the number includes tokenisation, prefill, sampler and KV cache -- what
    # a trader actually waits on. Sweeps thread count, and --gpu-layers measures
    # the GPU path through the identical loop on a CUDA-enabled build. Links
    # sensen alone; it measures this engine rather than comparing two.
    add_executable(assistant_throughput_probe src/assistant_throughput_probe.cpp)
    target_link_libraries(assistant_throughput_probe PRIVATE sensen_slim)

    # DIAGNOSTIC, not a shipped feature -- see the probe's own header comment.
    # Reproduces, in isolation, sensen's CPU/Q8_0 batched quantized-matvec
    # batch-size dependence (GEMM::matvecQuantizedBatch's
    # matvecQ8_0_Int8BatchSlice groups sequence rows by offset-from-batch-
    # start, not by absolute row index) that makes a chunked forward pass
    # (schedulerPrefill + schedulerPrefillContinue) numerically diverge from
    # an equivalent one-shot schedulerPrefill on this build -- the reason a
    # system-prompt KV prefix-reuse fast path was investigated and NOT
    # shipped for the strategy/mortgage assistants. Links sensen alone; it
    # compares two code paths within one process, not two engines.
    add_executable(prefix_cache_verify_probe src/prefix_cache_verify_probe.cpp)
    target_link_libraries(prefix_cache_verify_probe PRIVATE sensen_slim)

    # Diagnostic-only: real per-token cost of MortgageParamsGrammar::accept()
    # (recompute() over the real tokenizer vocabulary) against a REAL model's
    # tokenizer, measured on this machine, rather than the unspecified-machine
    # figure quoted in mortgage_grammar.cppm's own header comment. Needs
    # sensen_slim (for the tokenizer via LLMPipeline) PLUS the app's own
    # mortgage_verification/mortgage_grammar modules -- gp_ara_interfaces and
    # sensen.grammar are already provided by sensen_slim (see its own FILE_SET
    # a few hundred lines up), so they are NOT re-added here to avoid a
    # duplicate-PCM conflict.
    add_executable(bench_mortgage_grammar_timing src/bench_mortgage_grammar_timing.cpp)
    target_sources(bench_mortgage_grammar_timing
        PRIVATE
        FILE_SET CXX_MODULES FILES
            src/modules/mortgage_verification.cppm
            src/modules/mortgage_grammar.cppm
    )
    target_compile_definitions(bench_mortgage_grammar_timing PRIVATE
        MORTGAGE_FINANCE_PROTO_PATH="${CMAKE_CURRENT_SOURCE_DIR}/proto/finance.proto")
    target_link_libraries(bench_mortgage_grammar_timing PRIVATE sensen_slim)

    # Bit-exactness gate for the decode path. Captures the exact token-id stream
    # for a fixed prompt battery under greedy deterministic decoding, and later
    # re-checks it. Every change that claims to be numerically identical -- a
    # weight repacking, a new SIMD dispatch tier, a replayed CUDA graph -- must
    # reproduce it exactly. Takes --gpu-layers, so the CPU layout work and the
    # GPU graph work are held to ONE definition of correct rather than each
    # inventing its own tolerance.
    add_executable(decode_golden_probe src/decode_golden_probe.cpp)
    target_link_libraries(decode_golden_probe PRIVATE sensen_slim)

    # Long-context decode cost, prefill excluded. The KV cache is the only part
    # of the per-token read stream that grows with position, so a fixed short
    # prompt cannot measure a KV-width change at all; this sweeps the context
    # length instead. Like observer_cost_probe it does NOT link llama.cpp --
    # it is measuring this engine, not comparing two.
    add_executable(kv_bench_probe src/kv_bench_probe.cpp)
    target_link_libraries(kv_bench_probe PRIVATE sensen_slim)

    # PAGED-vs-LINEAR KV equivalence. Both existing parity harnesses construct
    # their session with KVCacheStrategy::FULL, so neither covers the paged
    # kernel -- which is the one production runs, since AgentSession defaults to
    # PAGED and assistant_service never overrides it. This closes that hole by
    # transitivity: paged==linear here, linear==llama.cpp there.
    add_executable(kv_equiv_probe src/kv_equiv_probe.cpp)
    target_link_libraries(kv_equiv_probe PRIVATE sensen_slim)

    # Does the KV cache need BF16's exponent range, or F16's mantissa? Measures
    # the actual dynamic range of the tensors that enter the cache, and separately
    # checks our f32->f16 narrowing against ggml's round-to-nearest-even bit for
    # bit. Links ggml for that second comparison.
    add_executable(kv_range_probe src/kv_range_probe.cpp)
    target_link_libraries(kv_range_probe PRIVATE ggml sensen_slim)

    # 8-bit KV design measurement: per-channel vs per-token absmax structure of
    # Kcur_rope / Vcur, outlier-channel persistence, skew, and simulated int8
    # error under the candidate scale-granularity schemes.
    add_executable(kv_gran_probe src/kv_gran_probe.cpp)
    target_link_libraries(kv_gran_probe PRIVATE sensen_slim)

    # Validates sensen.kv_q8's ACTUAL quantise_group_q8/dequantise_row_q8
    # round trip (not a hand-rolled affine simulation) against REAL captured
    # Kcur_rope/Vcur, in the real [n_tokens][dim] token-major layout with
    # BLOCK_SIZE=16 -- the accuracy gate for the 8-bit KV cache work package,
    # complementing test_kv_q8's synthetic unit gate on the math/edge cases.
    add_executable(kv_q8_real_probe src/kv_q8_real_probe.cpp)
    target_link_libraries(kv_q8_real_probe PRIVATE sensen_slim)

    # The engine itself. `calculator_engine` is declared far above this block,
    # which is fine and is why the wiring is here rather than there: CMake
    # resolves target_link_libraries/target_compile_definitions against a
    # target that already exists, so keeping every llama.cpp-conditional line
    # inside the one `if(ENABLE_LLAMACPP_BACKEND)` guard is what makes the
    # OFF configuration provably complete -- there is no second place to
    # remember to guard.
    #
    # ASSISTANT_HAVE_LLAMACPP is what assistant_service.cpp keys its
    # `#include "llama.h"` and its whole LlamaCppBackend class off. It is
    # PRIVATE because nothing else in this build includes that header, and
    # because a define that changes which classes a translation unit declares
    # must not leak to targets that were not compiled with it.
    target_link_libraries(calculator_engine PRIVATE llama)
    target_compile_definitions(calculator_engine PRIVATE ASSISTANT_HAVE_LLAMACPP)
endif()

# ============================================================================
# Ordering: nothing may compile `import std;` before std.pcm exists
# ============================================================================
# The -fmodule-file= flag added above tells clang WHERE the std BMI is; it does
# not tell ninja to build it first. std_module_precompile is declared ALL, which
# puts it in the default build but establishes no edge to anything here -- so a
# parallel build is free to schedule a module compile before the BMI is written
# and fail with "module file not found", intermittently and only under -j.
#
# The list is derived rather than written out because every one of this file's
# ~20 executables compiles at least one src/modules/*.cppm, and a hand-curated
# list is a second place to remember. BUILDSYSTEM_TARGETS covers exactly the
# targets created in THIS directory, which is the same set the add_compile_options
# above applies to.
#
# The type filter is required, not defensive: add_dependencies() is an error on
# an INTERFACE or IMPORTED target, and this directory defines both.
get_property(_engine_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
foreach(_t IN LISTS _engine_targets)
    get_target_property(_type ${_t} TYPE)
    if(_type STREQUAL "EXECUTABLE" OR _type STREQUAL "STATIC_LIBRARY"
       OR _type STREQUAL "SHARED_LIBRARY" OR _type STREQUAL "MODULE_LIBRARY"
       OR _type STREQUAL "OBJECT_LIBRARY")
        add_dependencies(${_t} std_module_precompile)
    endif()
endforeach()

# ============================================================================
# build_tests: the target that must precede ctest
# ============================================================================
# `ninja && ctest` does NOT build every binary ctest runs, and the gap is silent
# in the dangerous direction. SGEE is embedded with EXCLUDE_FROM_ALL, so its ~90
# test executables are absent from the default build while `ctest` still runs
# whatever copy is on disk -- a stale PASS looks exactly like a real one.
#
# `ninja build_tests && ctest` is the command. This target covers this
# directory's own test executables (which ARE in `all`, listed here anyway so
# the target means what its name says rather than being true by accident) plus
# SGEE's, via the aggregate that directory defines for the same reason.
get_property(_bt_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
add_custom_target(build_tests
    COMMENT "Building every binary ctest will run, including SGEE's EXCLUDE_FROM_ALL tests")
foreach(_t IN LISTS _bt_targets)
    get_target_property(_bt_type ${_t} TYPE)
    if(_bt_type STREQUAL "EXECUTABLE" AND ${_t} MATCHES "^test_")
        add_dependencies(build_tests ${_t})
    endif()
endforeach()
if(TARGET sgee_build_tests)
    add_dependencies(build_tests sgee_build_tests)
endif()
