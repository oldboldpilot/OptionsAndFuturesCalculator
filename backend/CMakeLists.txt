cmake_minimum_required(VERSION 3.28)
project(OptionsCalculatorEngine CXX)

# Stale module BMIs: a BMI records its compiler and clang refuses one from another,
# while CMake does not notice because the compiler PATH is unchanged. One guard in the
# TOP-LEVEL project sweeps the whole tree, embedded submodules included. Shared file,
# byte-identical across every top-level project here, and that identity is gated.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ToolchainBMIGuard.cmake)

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
# ${CMAKE_CXX_FLAGS}, which is CANONICAL_FLAGS (sensen_canonical_flags(), below) and therefore the union of
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

# The canonical compile flags -- libc++, the x86-64-v3 floor, the ABI-tag define, C++23, module
# scanning, the linker's -stdlib -- are sensen's, not this file's: one copy for every service built
# on this engine's tree, with the reasons for each flag beside them. Every translation unit in this
# build (gRPC, protobuf, abseil, sensen, SGEE, this engine) must agree on them, because a BMI records
# its flags and clang refuses a consumer whose flags differ. Called HERE, before any target or probe,
# so CMAKE_CXX_FLAGS reaches every directory added below.
include(${CMAKE_CURRENT_SOURCE_DIR}/sensen/cmake/SensenCanonicalFlags.cmake)
sensen_canonical_flags()

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

# gRPC, built from source with the pin, the options and the clang-20 conformance
# patch that make 1.62 build and link with this toolchain. They are sensen's
# (cmake/SensenGrpc.cmake) so every service that serves gRPC builds it one way; the
# TLS provider (system OpenSSL for the whole binary, never gRPC's vendored
# BoringSSL), the OpenSSL 3.5 ENGINE shim and the C++17 dialect for gRPC itself are
# explained there. It runs here, before add_subdirectory(sensen), exactly where the
# inline block used to.
include(${CMAKE_CURRENT_SOURCE_DIR}/sensen/cmake/SensenGrpc.cmake)
sensen_fetch_grpc()

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
#
# SENSEN_USE_SGEE OFF BECAUSE THIS ENGINE ALREADY HAS ONE, AND TWO WOULD NOT
# EVEN CONFIGURE. sensen consumes SGEE for Raft and adds it as a subproject;
# this engine has embedded SGEE at backend/external/SGEE since the inference
# queue was built. Both call add_library(sgee), so leaving sensen's copy on dies
# at configure time, before a single file compiles:
#
#   add_library cannot create target "sgee" because another target with the same
#   name already exists ... CMP0002
#
# and the same for sgee_capi and sgee-gen. Adopting ONE target for both is not
# free either -- the versions skew, this engine at b241317e against the commit
# sensen expects -- so the cheap, correct answer is that the engine keeps its
# own SGEE and sensen's is switched off. Nothing here consumes sensen's Raft.
#
# FORCE is load-bearing, like every other line in this block: a set(... CACHE)
# without it is sticky, so an option that once landed wrong stays wrong for the
# build directory's life and a fresh configure hides it.
set(SENSEN_USE_SGEE OFF CACHE BOOL "This engine embeds its own SGEE; two add_library(sgee) cannot coexist" FORCE)
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
# The one std module, and the slim sensen library
# ============================================================================
# src/modules/*.cppm say `import std;` rather than listing standard headers in a
# global module fragment, which is what a C++23 module is supposed to look like
# and what docs/PRD_OPTIONS_AND_FUTURES_CALCULATOR.md has always specified. The
# sensen sources compiled into sensen_slim say it too, now that
# SENSEN_NO_IMPORT_STD is gone from the top of this file.
#
# The BMI they import is the ONE sensen precompiles. There is deliberately no
# second std.pcm for this directory: sensen builds it from ${CMAKE_CXX_FLAGS},
# which at that point IS sensen_canonical_flags()'s CANONICAL_FLAGS, so a single BMI is
# compatible with every consumer here by construction.
#
# sensen_use_shared_std_module() points every C++ target declared AFTER it in this
# directory at that BMI, and its placement is the one rule that stays HERE because
# it depends on this file's order: it must sit AFTER add_subdirectory(sensen)
# (which declares std_module_precompile) and BEFORE the first target that compiles
# a sensen module -- sensen_slim, just below. It stays under
# FetchContent_MakeAvailable(grpc) so the flag never reaches gRPC's ~3000
# translation units. sensen_add_slim_library() refuses to run if it was not called
# first, so getting the order wrong is an error that names the cause and not
# "module 'std' not found" on seven files.
#
# sensen_slim is the part of sensen this engine actually uses. Linking sensen's own
# target compiles every module interface unit in its file set whether or not
# anything imports it: 271 translation units and a 93 MB library for the handful
# of modules this tree names. The module list is sensen's, as NAMED GROUPS
# (sensen/cmake/SensenSlim.cmake). This engine takes all of them because it serves
# the decoder assistant, the finance RPCs and the pricing engine; a service that
# needs only the encoder chain takes FOUNDATION, NUMERIC, RUNTIME and ENCODER.
#
# Keep the list honest after every sensen bump:
#
#     python3 scripts/sensen_module_closure.py --check
#
# It reads `import sensen.*;` from backend/src, computes the closure and compares
# it with the groups. Its standing output is three DECLARED exceptions, printed as
# declared and not as complaints: the CPU-excluded text_encoder_cuda.cpp and
# text_encoder_triton.cpp, the C-ABI numa_bind.cpp, and the retained, unreachable
# special/foundation.cppm.
#
# One compile-flag difference from sensen's own in-tree build is deliberate and is
# an improvement, not drift. sensen/CMakeLists.txt appends `-march=native` to
# everything in ITS directory scope, which lands after this project's canonical
# `-march=x86-64-v3 -mtune=generic` and wins, so every sensen module built in-tree
# is tuned for whatever CPU ran the compiler -- in the container, the Railway
# builder rather than the host that runs the engine. config/cpp_details.txt
# requires x86-64-v3/generic precisely to avoid that, for cross-host FP parity and
# durable-replay determinism. sensen_slim is declared in THIS directory scope, so
# it gets the canonical flags. AVX2 and FMA are enabled explicitly in
# CANONICAL_FLAGS; only AVX-512 and host-tuning are given up, which nothing in the
# closure uses, and the runtime SIMD waterfall still reaches AVX-512 by CPUID.
include(${CMAKE_CURRENT_SOURCE_DIR}/sensen/cmake/SensenSlim.cmake)
sensen_use_shared_std_module()
sensen_add_slim_library(sensen_slim GROUPS ALL)

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
#   * sensen_canonical_flags() puts CANONICAL_FLAGS into CMAKE_CXX_FLAGS at the top of this file, BEFORE
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

# THE DIRECTORY IS NOW DERIVED TOO, not just its last component. The version
# above fixed the TARGET NAME half -- correct, and recorded for the right
# reason -- while still hardcoding the PARENT path as
#   ${CMAKE_BINARY_DIR}/sensen/external/fastestjsoninthewest/external/cpp23-logger/...
# which is one of several places sensen may build `logger`, and is not the one
# it uses once sensen prefers its OWN external/cpp23-logger submodule (the
# latest pin, c60d3c95, and the only copy carrying the EXTERNAL_STD_PCM branch;
# fastjson vendors an older one as checked-in files). Half-derived is the worst
# of both: it reads as settled and still encodes which call ran.
#
# `BINARY_DIR` is the target's own answer, so there is nothing left to go stale
# whichever source sensen chose. Same reasoning as deriving the target name
# from CPP23_LOGGER_TARGET_NAME, carried one level up -- and the same reasoning
# as `pending_enqueues_in_log()` replacing a cached counter.
if(NOT TARGET ${CPP23_LOGGER_TARGET_NAME})
    message(FATAL_ERROR
        "CPP23_LOGGER_TARGET_NAME is `${CPP23_LOGGER_TARGET_NAME}` but no such target exists, "
        "so its BINARY_DIR cannot be read and SGEE's logger BMI path would have to be guessed.")
endif()
get_target_property(_ofc_logger_bindir ${CPP23_LOGGER_TARGET_NAME} BINARY_DIR)
set(SGEE_LOGGER_MODULE_DIR
    "${_ofc_logger_bindir}/CMakeFiles/${CPP23_LOGGER_TARGET_NAME}.dir"
    CACHE PATH "Directory containing the logger module BMI for SGEE" FORCE)
message(STATUS "SGEE: logger BMIs -> ${SGEE_LOGGER_MODULE_DIR} "
               "(target ${CPP23_LOGGER_TARGET_NAME}, derived from its BINARY_DIR)")
unset(_ofc_logger_bindir)

# SGEE'S TESTS ARE REGISTERED WITH ctest, SO THEY MUST BE BUILDABLE HERE.
#
# Measured 2026-10-04: this build directory's cache carried
# `SGEE_BUILD_TESTS:BOOL=OFF` while SGEE's own default is ON, so
# `external/SGEE/tests` was NOT configured -- and a
# `sgee/tests/CTestTestfile.cmake` from a configure four days earlier SURVIVED.
# ctest reads that file, so **115 of the 152 tests it ran came from it**, each
# invoking a binary by absolute path that nothing in the current graph could
# rebuild: `ninja sgee_queue_node_client` answered "unknown target".
#
# The failure mode is the dangerous direction. SGEE's integration tests are
# SHELL scripts read from the source tree at RUN time, so they tracked the
# merge immediately, while the C++ client binary they drive stayed at
# 2026-09-30 -- no `write_hlc`, no `read-after`. A new script meeting an old
# binary reported `enqueue ack carried no write_hlc -- the node's HLC is not
# wired`, which reads as a product defect and is a stale object file. The HLC
# is wired (tools/sgee_queue_node.cpp passes hlc_now/hlc_observe).
#
# FORCE is required, not stylistic: `option()` and a plain `set(... CACHE)` BOTH
# defer to an existing cache entry, so without it the wrong value persists for
# the directory's life and a fresh configure hides the whole problem.
#
# This does cost SGEE's test binaries in this tree. EXCLUDE_FROM_ALL still keeps
# them off a plain `ninja`; what changes is that `ctest` no longer reports
# un-rebuildable tests as passes.
set(SGEE_BUILD_TESTS ON CACHE BOOL "Build SGEE's tests: ctest registers them, so they must be buildable" FORCE)

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
add_executable(calculator_engine src/main.cpp src/issue_key.cpp src/modules/calculator_service.cpp src/modules/finance_service.cpp src/modules/quota.cpp src/modules/api_key.cpp src/modules/assistant_service.cpp src/modules/mortgage_assistant_service.cpp src/modules/inference_queue.cpp src/modules/inference_admission.cpp src/modules/sgee_queue_client.cpp src/modules/strategy_store.cpp src/modules/state_refresh.cpp)
target_sources(calculator_engine PUBLIC FILE_SET CXX_MODULES FILES src/modules/calculator_service.cppm src/modules/market_data.cppm src/modules/finance_service.cppm src/modules/quota.cppm src/modules/api_key.cppm src/modules/fips_mode.cppm src/modules/state_refresh.cppm src/modules/strategy_catalogue.cppm src/modules/assistant_verification.cppm src/modules/assistant_service.cppm src/modules/mortgage_verification.cppm src/modules/mortgage_explanation.cppm src/modules/mortgage_derivation.cppm src/modules/mortgage_grammar.cppm src/modules/mortgage_assistant_service.cppm src/modules/pg.cppm src/modules/inference_queue.cppm src/modules/sgee_queue_client.cppm src/modules/inference_admission.cppm src/modules/encoder_queue.cppm src/modules/assistant_runtime.cppm src/modules/strategy_store.cppm src/modules/llq_weight_store.cppm)
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

# CorpusInvariantsTest: deterministic properties of the assistant's TRAINING
# DATA, asserted without a model, a checkpoint or a GPU.
#
# Every corpus defect this project has paid for was visible in the data before
# any model was trained, and every one was found only after a retrain measured
# worse -- `phrase_money` rounding away the cents its label kept (0/98 on eight
# operations), `prepaid_interest_days` labelling a convention the utterance
# never states (21 of 26 closing-cost failures), and the dated cash-flow grid
# generated at ~365-day intervals and labelled ComputeXnpv, whose entire
# distinction from ComputeNpv is IRREGULAR spacing (8 of 9 held-out rows
# answered as the wrong operation). The generator is a pure function of
# (seed, n, val_frac, its own source), so all of it is checkable in a second.
#
# Plain `python3`, deliberately NOT ${Python_EXECUTABLE}: that variable only
# exists when BUILD_PYTHON_BINDINGS is ON, which the production container build
# must never require. Registered only if an interpreter is present, so a host
# without one skips the test rather than failing it.
find_program(PYTHON3_FOR_CORPUS_TEST NAMES python3)
if(PYTHON3_FOR_CORPUS_TEST)
    add_test(
        NAME CorpusInvariantsTest
        COMMAND ${PYTHON3_FOR_CORPUS_TEST}
                ${CMAKE_CURRENT_SOURCE_DIR}/../agent/dataset/test_corpus_invariants.py
    )
else()
    message(STATUS "python3 not found -- CorpusInvariantsTest not registered")
endif()

# DerivationCorpusSweepTest: prove the derivation layer never CORRUPTS a correct
# answer, against a corpus generated from the generator itself.
#
# Accuracy cannot see this failure. The sweep hands the layer the GOLD params as
# though the model had emitted them perfectly and reports every field the layer
# then REWRITES -- and rewriting a correct value is a served regression by
# construction, because the row still scores raw-exact and serves wrong. A
# recency rule keyed on percent alone cost 19 holdout rows exactly that way: a
# HELOC reply of "75%" is a max-LTV, and letting it supersede the stated 8.33%
# derived annual_rate = 0.7500 on rows the model had answered perfectly.
# `raw_exact` was byte-identical across that regression; only served_exact moved.
#
# The corpus is GENERATED rather than checked in, for the reason
# CorpusInvariantsTest gives: it is a pure function of (seed, n, the generator),
# so the gate re-derives itself whenever the corpus changes and cannot go stale
# beside it.
if(PYTHON3_FOR_CORPUS_TEST)
    add_test(
        NAME DerivationCorpusSweepTest
        COMMAND ${PYTHON3_FOR_CORPUS_TEST}
                ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/sweep_corpus.py
                --gate $<TARGET_FILE:dbg_derivation> 500
    )
endif()

# GroundingCorpusSweepTest: the same sweep, one layer down -- prove the GROUNDING
# gate never refuses a label the corpus itself teaches.
#
# `mortgage_verification.cppm` has claimed this property in a comment since
# 2026-08-05 (the misuse spec's WU-M2 criterion) and nothing ever ran it. The
# criterion was checked once by hand and the corpus then moved underneath it.
#
# What that cost, measured 2026-09-16: `ComputePaybackPeriod` teaches
# `rate: 0.0` for an UNDISCOUNTED payback -- the convention meaning "no
# discounting", which the engine never reads -- and grounding refused it as an
# ungrounded zero. 17 of 28 holdout rows, with the model's output byte-identical
# to the gold. `raw_exact` scored every one of them correct.
if(PYTHON3_FOR_CORPUS_TEST)
    add_test(
        NAME GroundingCorpusSweepTest
        COMMAND ${PYTHON3_FOR_CORPUS_TEST}
                ${CMAKE_CURRENT_SOURCE_DIR}/../scripts/sweep_corpus.py
                --gate $<TARGET_FILE:dbg_grounding> 500
    )
endif()

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
        # assistant_verification imports ascii_text; the guards it no longer owns are
        # sensen.utterance_guards, which this test exercises directly. Both import only std.
        sensen/src/ascii_text.cppm
        sensen/src/utterance_guards.cppm
        src/modules/strategy_catalogue.cppm
        src/modules/assistant_verification.cppm
)

add_test(NAME AssistantVerificationTest COMMAND test_assistant_verification)

# GP-ARA mandatory verification stage for the MORTGAGE assistant. Same
# rationale as the options block above: proto- and gRPC-free, and deliberately
# NOT linked against sensen_slim -- the only sensen module it needs,
# sensen.gp_ara_interfaces, has no dependencies beyond std, so it is compiled
# here as this target's own FILE_SET source.
# mortgage_derivation: the Horn-clause derivation layer. Links sensen_slim
# because it imports sensen.logic and sensen.logic_parser, which the engine now
# carries -- see the logic.cppm entry in the slim list for why that is the right
# engine for this and z3 is not.
add_executable(bench_derivation tests/bench_derivation.cpp)
target_sources(bench_derivation PRIVATE FILE_SET CXX_MODULES FILES
    sensen/src/gp_ara_interfaces.cppm
    src/modules/mortgage_verification.cppm
    src/modules/mortgage_derivation.cppm)
target_link_libraries(bench_derivation PRIVATE sensen_slim fastjson logger)

# bench_finance_compute: raw kernel latency with no gRPC and no network. It
# needs no module FILE_SET of its own -- every symbol it times is exported by
# sensen_slim's financial/bigdecimal modules, which it imports directly.
add_executable(bench_finance_compute tests/bench_finance_compute.cpp)
target_link_libraries(bench_finance_compute PRIVATE sensen_slim fastjson logger)

add_executable(dbg_grounding tests/dbg_grounding.cpp)
target_sources(dbg_grounding PRIVATE FILE_SET CXX_MODULES FILES
    sensen/src/gp_ara_interfaces.cppm
    src/modules/mortgage_verification.cppm)
target_link_libraries(dbg_grounding PRIVATE sensen_slim fastjson logger)

add_executable(dbg_derivation tests/dbg_derivation.cpp)
target_sources(dbg_derivation PRIVATE FILE_SET CXX_MODULES FILES
    sensen/src/gp_ara_interfaces.cppm
    src/modules/mortgage_verification.cppm
    src/modules/mortgage_derivation.cppm)
target_link_libraries(dbg_derivation PRIVATE sensen_slim fastjson logger)

# Two consistency checkers over rule systems this project spreads across files
# no compiler reads together -- the mortgage assistant's per-operation coverage,
# and mortgagefvcalculator.com's monetization workflow. Both ask the Horn-clause
# engine "which member of this family has no rule", which is the defect shape
# every coverage miss recorded in CLAUDE.md has taken.
#
# THEY LIVE HERE BECAUSE THIS IS THE ONLY TREE THAT CAN BUILD THEM. The facts
# they consume are GENERATED -- scripts/emit_assistant_facts.py in this repo,
# nest-egg-loan/scripts/emit-workflow-facts.mjs in the other -- and both
# emitters were committed while their readers existed only in an untracked
# worktree. A generator whose only consumer is uncommitted is a generator
# nothing gates.
#
# Deliberately NOT registered with add_test: each needs a generated facts file,
# and a check that passes because its input was missing is worse than no check.
add_executable(assistant_check tests/assistant_check.cpp)
target_link_libraries(assistant_check PRIVATE sensen_slim fastjson logger)

add_executable(workflow_check tests/workflow_check.cpp)
target_link_libraries(workflow_check PRIVATE sensen_slim fastjson logger)

add_executable(test_mortgage_derivation tests/test_mortgage_derivation.cpp)
target_sources(test_mortgage_derivation
    PRIVATE
    FILE_SET CXX_MODULES FILES
        sensen/src/gp_ara_interfaces.cppm
        src/modules/mortgage_verification.cppm
        src/modules/mortgage_derivation.cppm
)
target_link_libraries(test_mortgage_derivation PRIVATE sensen_slim fastjson logger)
target_compile_definitions(test_mortgage_derivation PRIVATE
    MORTGAGE_FINANCE_PROTO_PATH="${CMAKE_CURRENT_SOURCE_DIR}/proto/finance.proto")
add_test(NAME MortgageDerivationTest COMMAND test_mortgage_derivation)

# mortgage_explanation: the PMI scenario put into words by the SAME Horn-clause
# solver the derivation layer uses. It needs no verifier and no proto: it reasons
# over FACTS the amortisation already produced, which is the property that keeps
# a number out of the prose unless the engine computed it.
add_executable(test_mortgage_explanation tests/test_mortgage_explanation.cpp)
target_sources(test_mortgage_explanation
    PRIVATE
    FILE_SET CXX_MODULES FILES
        src/modules/mortgage_explanation.cppm
)
target_link_libraries(test_mortgage_explanation PRIVATE sensen_slim fastjson logger)
add_test(NAME MortgageExplanationTest COMMAND test_mortgage_explanation)

# test_result_explanation: the OUTPUT-driven explainer. It asserts the two
# things that decide whether "skip the empty columns" is safe -- that an INPUT
# zero is silence and a COMPUTED zero is a finding -- plus the transitive
# suppression that stops a HELOC's interest rate being explained when there is
# no HELOC, which per-field filtering keeps because a rate is an ordinary
# non-zero number.
add_executable(test_result_explanation tests/test_result_explanation.cpp)
target_sources(test_result_explanation
    PRIVATE
    FILE_SET CXX_MODULES FILES
        src/modules/mortgage_explanation.cppm
)
target_link_libraries(test_result_explanation PRIVATE sensen_slim fastjson logger)
add_test(NAME ResultExplanationTest COMMAND test_result_explanation)

# test_carrying_costs: repairs and the owner's own insurance on an ordinary
# mortgage. Its first assertion is the one that matters -- the loan schedule is
# byte-identical when the costs are not asked for, so an optional feature is
# proven optional rather than assumed to be.
add_executable(test_carrying_costs tests/test_carrying_costs.cpp)
target_link_libraries(test_carrying_costs PRIVATE sensen_slim fastjson logger)
add_test(NAME CarryingCostsTest COMMAND test_carrying_costs)

# test_rent_vs_buy_costs: repairs, PMI and its cancellation, an overpayment
# that pulls that cancellation forward, and a HELOC-funded deposit. Its
# sharpest assertion is section 5 -- the renter's portfolio must open with the
# buyer's OWN cash, because investing the borrowed part hands the renter money
# nobody had and makes buying look worse with every figure still plausible.
add_executable(test_rent_vs_buy_costs tests/test_rent_vs_buy_costs.cpp)
target_link_libraries(test_rent_vs_buy_costs PRIVATE sensen_slim fastjson logger)
add_test(NAME RentVsBuyCostsTest COMMAND test_rent_vs_buy_costs)

# test_label_space_drift: the FOURTH table, which had no check at all and was
# the one that drifted. It reads `kOperations` out of the service .cpp as text
# -- the table is not in a module interface, and a derived check against the
# real artifact beats a hand-written list that can drift exactly as the thing
# it checks just did. The source root is passed so it does not depend on cwd.
add_executable(test_label_space_drift tests/test_label_space_drift.cpp)
target_sources(test_label_space_drift
    PRIVATE
    FILE_SET CXX_MODULES FILES
        src/modules/mortgage_verification.cppm
)
target_link_libraries(test_label_space_drift PRIVATE sensen_slim fastjson logger)
add_test(NAME LabelSpaceDriftTest
         COMMAND test_label_space_drift ${CMAKE_CURRENT_SOURCE_DIR})

# test_pmi_policy: the byte-identity gate over the PMI cancellation refactor,
# plus the new basis/threshold behaviour. It links sensen_slim and calls
# sensen::calculate_mortgage_amortization DIRECTLY rather than through the
# service, because what is being gated is the engine's arithmetic and a service
# round trip would only add ways for the comparison to be about something else.
add_executable(test_pmi_policy tests/test_pmi_policy.cpp)
target_link_libraries(test_pmi_policy PRIVATE sensen_slim fastjson logger)
add_test(NAME PmiPolicyTest COMMAND test_pmi_policy)

# tax_deduction: what the mortgage interest deduction is WORTH, per tax year.
# It imports only std -- the statutory limits are arithmetic over a table, not
# finance -- so it links no sensen module and carries no libpq.
add_executable(test_tax_deduction tests/test_tax_deduction.cpp)
target_sources(test_tax_deduction PRIVATE FILE_SET CXX_MODULES FILES
    src/modules/tax_deduction.cppm)
target_link_libraries(test_tax_deduction PRIVATE fastjson logger)
add_test(NAME TaxDeductionTest COMMAND test_tax_deduction)

# test_rental_cash_flow: the investor view -- vacancy, itemised expenses and a
# HELOC that funded the deposit. Calls sensen directly, like test_pmi_policy.
add_executable(test_rental_cash_flow tests/test_rental_cash_flow.cpp)
target_link_libraries(test_rental_cash_flow PRIVATE sensen_slim fastjson logger)
add_test(NAME RentalCashFlowTest COMMAND test_rental_cash_flow)

# rental_tax: Schedule E, depreciation and the passive-activity limits. Imports
# tax_deduction for the brackets, so both modules compile into this target.
add_executable(test_rental_tax tests/test_rental_tax.cpp)
target_sources(test_rental_tax PRIVATE FILE_SET CXX_MODULES FILES
    src/modules/tax_deduction.cppm
    src/modules/rental_tax.cppm)
target_link_libraries(test_rental_tax PRIVATE fastjson logger)
add_test(NAME RentalTaxTest COMMAND test_rental_tax)

add_executable(test_mortgage_verification tests/test_mortgage_verification.cpp)
target_sources(test_mortgage_verification
    PRIVATE
    FILE_SET CXX_MODULES FILES
        sensen/src/gp_ara_interfaces.cppm
        # The lexer and Decimal the verifier grounds against live in sensen now. The lexer
        # imports ascii_text and float_types (which imports cpu_features) and nothing else, so
        # the four are compiled here as this target's own sources, as gp_ara is.
        sensen/src/ascii_text.cppm
        sensen/src/cpu_features.cppm
        sensen/src/float_types.cppm
        sensen/src/numeric_lexer.cppm
        src/modules/mortgage_verification.cppm
)
# The label-space drift check re-parses finance.proto at RUN time, so the test
# fails rather than silently skipping if the path is wrong. The test also falls
# back to $FINANCE_PROTO_PATH and to ../proto/finance.proto (which resolves when
# ctest runs from the build dir); this define is belt and braces.
target_compile_definitions(test_mortgage_verification PRIVATE
    MORTGAGE_FINANCE_PROTO_PATH="${CMAKE_CURRENT_SOURCE_DIR}/proto/finance.proto")

add_test(NAME MortgageVerificationTest COMMAND test_mortgage_verification)

# PARITY PROBE for encoder_reconstruct.cppm -- the C++ port of Python's
# reconstruct() (agent/train/encoder_corpus.py). The small-encoder assistants
# never generate digits: the model names an operation, points at numeric spans
# and names a MAP, and every parameter value is then computed on the serving
# side. So the whole arithmetic surface of those assistants lives in this one
# module, and a drift between it and the trainer's own reconstruct() is a wrong
# answer that parses -- the documented dangerous-failure class.
#
# It takes a schema JSON and a fixture JSON on the command line and prints one
# canonical line per row, so it is a PROBE rather than a ctest test for the same
# reason smoke_client is not one: its inputs are generated artifacts, and a test
# that fabricates them would be comparing this module against itself. The
# comparison is driven by scripts/check_encoder_reconstruct_parity.py, which
# holds the oracle (Python's own reconstruct on the same fixture).
#
# Named test_* deliberately: `build_tests` sweeps BUILDSYSTEM_TARGETS matching
# ^test_, so the probe keeps COMPILING even though nothing runs it in ctest. A
# probe that stops building is indistinguishable from one that was never written.
add_executable(test_encoder_reconstruct_parity tests/encoder_reconstruct_parity.cpp)
# sensen_slim carries sensen.encoder_reconstruct itself, sensen.bigdecimal (the exact
# fixed-point type every reconstructed money value is computed in) and PUBLIC-links
# fastjson, whose BMI the module imports.
target_link_libraries(test_encoder_reconstruct_parity PRIVATE sensen_slim fastjson)

# GATE for the per-operation mask both model heads are decoded through. Unlike the parity
# probe above this one needs no fixture, no model and no engine -- the rules are properties
# of the schema -- so it is REGISTERED with ctest and runs on every build. That matters
# because the defect it pins (the convention heads decoded unmasked) was invisible to every
# accuracy number the project had: the mortgage corpus's masks are all no-ops, so it served
# 560/560 with the bug, and only the strategy corpus could exhibit it.
add_executable(test_encoder_operation_decode tests/test_encoder_operation_decode.cpp)
target_link_libraries(test_encoder_operation_decode PRIVATE sensen_slim fastjson)
add_test(NAME EncoderOperationDecodeTest COMMAND test_encoder_operation_decode)

# PARITY PROBE for sensen's WordPiece tokenizer against the encoder trainer's.
#
# The encoder's per-literal heads point AT a numeric literal, and a literal is matched
# to tokens by SPAN -- so a tokenizer disagreement is not a graceful degradation. Either
# the model reads a token sequence it was never trained on, or the literal's span stops
# lining up with token boundaries. sensen makes the second case loud (`tokensInSpan`
# refuses with TokenStraddlesLiteral unless every overlapping token lies WHOLLY inside
# the literal) and the first is silent, which is why it gets a gate.
#
# Driven by scripts/check_encoder_tokenizer_parity.py, which holds the one piece of
# reasoning neither side can: HuggingFace reports CHARACTER offsets and sensen reports
# UTF-8 BYTE offsets, so the comparator converts rather than relying on this holdout
# happening to be 100% ASCII.
#
# Needs only sensen.tokenizer and fastjson. A probe, not a ctest test, for the same
# reason as the reconstruct one above.
add_executable(test_encoder_tokenizer_parity tests/encoder_tokenizer_parity.cpp)
target_link_libraries(test_encoder_tokenizer_parity PRIVATE sensen_slim fastjson)

# PARITY PROBE for the DEPLOYED numeric lexer against the encoder trainer's lex().
# The pair head is indexed BY LITERAL POSITION, so the engine's literal list must agree
# with the trainer's index for index or the model's prediction lands on a different
# number. Compares order, count, span and value -- see the file's own header for why the
# TAG is deliberately not load-bearing, and for the two representation differences that
# are conversions rather than disagreements. Driven by
# scripts/check_encoder_lexer_parity.py. A probe, not a ctest test: generated input.
add_executable(test_encoder_lexer_parity tests/encoder_lexer_parity.cpp)
# The lexer is sensen.numeric_lexer now (it imports sensen.ascii_text and sensen.float_types, which
# imports sensen.cpu_features), so this probe compiles those four as its own sources rather than
# linking the whole of sensen_slim -- the same light shape it had when the lexer lived inside
# mortgage_verification.
target_sources(test_encoder_lexer_parity PRIVATE FILE_SET CXX_MODULES FILES
    sensen/src/ascii_text.cppm sensen/src/cpu_features.cppm sensen/src/float_types.cppm
    sensen/src/numeric_lexer.cppm)
target_link_libraries(test_encoder_lexer_parity PRIVATE fastjson)

# The small-encoder mortgage assistant, end to end (lex -> tokenize -> encode -> mask ->
# reconstruct). A probe that runs the WHOLE chain over the 600-row holdout IN PROCESS: it is
# the first thing that exercises the four gated pieces together rather than one at a time,
# and it isolates "does the chain work" from "does the RPC plumbing work". Needs the sensen
# encoder and tokenizer (sensen_slim), the reconstruct port and the deployed lexer.
add_executable(test_encoder_assistant_probe tests/encoder_assistant_probe.cpp)
target_link_libraries(test_encoder_assistant_probe PRIVATE sensen_slim fastjson)

# Proto-derived CONSTRAINED DECODING for the mortgage assistant
# (mortgage_grammar.cppm). Same rationale as the verification blocks above:
# proto- and gRPC-free, and deliberately NOT linked against sensen_slim. It
# needs two sensen modules -- sensen.gp_ara_interfaces (via
# mortgage_verification) and sensen.grammar, whose IGrammar contract this
# module implements -- and neither depends on anything beyond std, so both are
# compiled here as this target's own FILE_SET sources.
# Pure text comparison of two files -- no engine, no protobuf, no links.
# Gates the ACS validator directly. No engine, no network, no database -- the
# validator is split out of the service precisely so it can be tested here.
# INTERFACE ONLY. The pure validator lives in state_refresh.cppm precisely so
# this gate needs no libpq, no SGEE and no engine -- see that file's `detail`
# namespace comment for why the first attempt failed to build.
add_executable(test_state_refresh tests/test_state_refresh.cpp)
target_sources(test_state_refresh
    PRIVATE
    FILE_SET CXX_MODULES FILES
        src/modules/state_refresh.cppm)
add_test(NAME StateRefreshTest COMMAND test_state_refresh)

# The LLQ weight store: a Q8_0 weight held as an LLQ image and served from it by
# sensen's own kernels. Links sensen_slim (GEMM and sensen.lossless_quant) and
# nothing else -- no libpq, no engine.
add_executable(test_llq_weight_store tests/test_llq_weight_store.cpp)
target_sources(test_llq_weight_store
    PRIVATE
    FILE_SET CXX_MODULES FILES
        src/modules/llq_weight_store.cppm)
target_link_libraries(test_llq_weight_store PRIVATE sensen_slim)
add_test(NAME LlqWeightStoreTest COMMAND test_llq_weight_store)

# THE LADDER: which quantised types an LLQ image can hold exactly, which cannot,
# and why -- per type, DERIVED from sensen's own per-QType algebra
# (GEMM::quantIsSymmetric / quantCodeBits / quantScaleGranularity) and the
# codec's own floors, never from a recorded table of verdicts. Prints the table
# as well as asserting its properties, because the table is the useful artefact
# and a printed one that nothing checks is a recorded answer waiting to go
# stale. Also gates the 4- and 5-bit rungs' GGUF round trip and the derived
# bound on their fused arm. Same link shape as the store's own test: sensen_slim
# and nothing else.
add_executable(test_llq_ladder tests/test_llq_ladder.cpp)
target_sources(test_llq_ladder
    PRIVATE
    FILE_SET CXX_MODULES FILES
        src/modules/llq_weight_store.cppm)
target_link_libraries(test_llq_ladder PRIVATE sensen_slim)
add_test(NAME LlqLadderTest COMMAND test_llq_ladder)

# THE TRAINING LADDER, which is where the 6-bit rung actually lives. The serving
# ladder refuses 6-bit because GGUF's only 6-bit weight format is Q6_K and its
# scale granularity is 16, below the codec's floor of 32 -- a fact about Q6_K,
# not about six bits. sensen's OTHER low-bit ladder (QuantPrecision, eleven
# rungs including INT6 and both FP6s) is what a frozen QLoRA base is held in,
# its scale granularity is 64, and it clears the floor. This measures whether an
# LLQ image pays at each rung and asserts the route is BIT-IDENTICAL to
# CpuFrozenLowBitWeight::dequantiseInto -- the one function the QLoRA forward
# and backward both go through. Needs no model, no GPU and no engine.
add_executable(test_llq_training_ladder tests/test_llq_training_ladder.cpp)
target_link_libraries(test_llq_training_ladder PRIVATE sensen_slim)
add_test(NAME LlqTrainingLadderTest COMMAND test_llq_training_ladder)

add_executable(test_vendored_proto_drift tests/test_vendored_proto_drift.cpp)
target_compile_definitions(test_vendored_proto_drift PRIVATE
    kBackendDir="${CMAKE_CURRENT_SOURCE_DIR}"
    kRepoRoot="${CMAKE_CURRENT_SOURCE_DIR}/..")
add_test(NAME VendoredProtoDriftTest COMMAND test_vendored_proto_drift)

add_executable(test_mortgage_grammar tests/test_mortgage_grammar.cpp)
target_sources(test_mortgage_grammar
    PRIVATE
    FILE_SET CXX_MODULES FILES
        sensen/src/gp_ara_interfaces.cppm
        sensen/src/grammar.cppm
        sensen/src/ascii_text.cppm
        sensen/src/cpu_features.cppm
        sensen/src/float_types.cppm
        sensen/src/numeric_lexer.cppm
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
        src/modules/state_refresh.cppm
        src/modules/mortgage_explanation.cppm
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
    src/modules/api_key.cpp)
target_sources(test_finance_service_validation
    PRIVATE
    FILE_SET CXX_MODULES FILES
        src/modules/state_refresh.cppm
        src/modules/mortgage_explanation.cppm
        src/modules/finance_service.cppm
        src/modules/quota.cppm
        src/modules/api_key.cppm
)
target_link_libraries(test_finance_service_validation PRIVATE proto_lib sensen_slim fastjson logger httplib::httplib OpenSSL::SSL OpenSSL::Crypto sgee)

add_test(NAME FinanceServiceValidationTest COMMAND test_finance_service_validation)

# The write gate on RefreshStateAssumptions, in both directions.
#
# A SEPARATE binary from the sweep above, and not by preference: KeyRegistry is
# a Meyers singleton that reads FINANCE_API_KEYS once at first use, so one
# process holds one key configuration for its whole life. The sweep calls
# anonymously ~200 times; this one needs keys configured before the first call.
add_executable(test_state_assumptions_gate
    tests/test_state_assumptions_gate.cpp
    src/modules/finance_service.cpp
    src/modules/quota.cpp
    src/modules/api_key.cpp)
target_sources(test_state_assumptions_gate
    PRIVATE
    FILE_SET CXX_MODULES FILES
        src/modules/state_refresh.cppm
        src/modules/mortgage_explanation.cppm
        src/modules/finance_service.cppm
        src/modules/quota.cppm
        src/modules/api_key.cppm
)
target_link_libraries(test_state_assumptions_gate PRIVATE proto_lib sensen_slim fastjson logger httplib::httplib OpenSSL::SSL OpenSSL::Crypto sgee)

add_test(NAME StateAssumptionsGateTest COMMAND test_state_assumptions_gate)

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
        # The service imports it to honour *_WEIGHT_STORE; inert unless that is set.
        src/modules/llq_weight_store.cppm
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
        # The service routes its encoder through the shared queue via this module.
        src/modules/encoder_queue.cppm
        # And joins the shared queue, as the other assistant does, through this one.
        src/modules/assistant_runtime.cppm
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
        # The service imports it to honour *_WEIGHT_STORE; inert unless that is set.
        src/modules/llq_weight_store.cppm
        src/modules/mortgage_verification.cppm
        # Graph C imports this; a target compiling the service must carry it.
        src/modules/mortgage_derivation.cppm
        src/modules/mortgage_grammar.cppm
        src/modules/quota.cppm
        src/modules/api_key.cppm
        src/modules/pg.cppm
        src/modules/inference_queue.cppm
        # See test_assistant_service above: inference_admission.cppm's import of
        # sgee_queue_client must be satisfied per target.
        src/modules/sgee_queue_client.cppm
        src/modules/inference_admission.cppm
        src/modules/encoder_queue.cppm
        src/modules/assistant_runtime.cppm
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

# The encoder assistants on the shared queue: the wire codec, EncoderBackend, answer() and
# LeaseRunner, against a stub chain and an in-memory double of the queue. No model, no cluster,
# no Postgres -- what needs a real broker (the surface filter, work-sharing across processes) is
# tests/integration/encoder_queue_cluster_test.sh.
add_executable(test_encoder_queue
    tests/test_encoder_queue.cpp
)
target_sources(test_encoder_queue
    PRIVATE
    FILE_SET CXX_MODULES FILES
        src/modules/pg.cppm
        src/modules/inference_queue.cppm
        src/modules/sgee_queue_client.cppm
        src/modules/inference_admission.cppm
        src/modules/encoder_queue.cppm
        src/modules/assistant_runtime.cppm
)
target_sources(test_encoder_queue PRIVATE src/modules/inference_queue.cpp src/modules/sgee_queue_client.cpp src/modules/inference_admission.cpp)
target_link_libraries(test_encoder_queue PRIVATE sensen_slim PostgreSQL::PostgreSQL fastjson logger sgee)

add_test(NAME EncoderQueueTest COMMAND test_encoder_queue)
# A regression must FAIL this test, not hang it: the busy-replica cases hold chains on latches.
set_tests_properties(EncoderQueueTest PROPERTIES TIMEOUT 120)

# The probe's `answered` count, which the bench reads for arm L. Plain python3, registered only when an
# interpreter is present; exits 77 (skipped) without grpc.
find_program(PYTHON3_FOR_PROBE_TEST NAMES python3)
if(PYTHON3_FOR_PROBE_TEST)
    add_test(NAME EncoderQueueProbeTest COMMAND ${PYTHON3_FOR_PROBE_TEST} -B ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_encoder_queue_probe.py)
    set_tests_properties(EncoderQueueProbeTest PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
endif()

# The same claims against a REAL three-node SGEE cluster and REAL engines holding the real encoder
# weights: encoders go through the queue, work is shared between engines, the surface partition
# holds, served answers are byte-identical to local mode over the 272-row visitor regression and
# the 1500-row strategy holdout, and a vanished cluster costs only the queue. Exit 77 (a named
# skip) when the weights or python gRPC are absent -- the encoder GGUFs are not committed.
add_test(NAME EncoderQueueClusterTest
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/encoder_queue_cluster_test.sh
                 $<TARGET_FILE:sgee_queue_node> $<TARGET_FILE:calculator_engine>
                 ${CMAKE_CURRENT_SOURCE_DIR}/..)
set_tests_properties(EncoderQueueClusterTest PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 900)

# An engine that cannot bind its listener must exit with a message, not SEGV in RunServer().
add_test(NAME EngineBindFailureTest
         COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/engine_bind_failure_test.sh
                 $<TARGET_FILE:calculator_engine>)
set_tests_properties(EngineBindFailureTest PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)

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
    # THE PATCH IS PART OF THE PIN, and it has to be, because libc++ 23 broke
    # this exact commit.
    #
    # libc++ removes transitive includes every release: headers that used to
    # drag in <cstdlib> and <algorithm> no longer do. Thirteen sites across
    # seven llama.cpp files then failed with `use of undeclared identifier
    # 'getenv'`, `'atoi'`, `'strtol'` and `no member named 'max' in namespace
    # 'std'` -- ordinary missing-include bugs that libstdc++ and older libc++
    # were hiding. Nothing about them is llama.cpp-specific; they are what a
    # stricter standard library finds.
    #
    # Carried as a patch rather than by bumping GIT_TAG, deliberately: the pin
    # is what makes the parity probes meaningful (see the strategy-assistant
    # section of CLAUDE.md -- llama.cpp is here to be an INDEPENDENT
    # implementation, and moving it is changing the reference). A patch that
    # only adds seven #include lines changes no behaviour and no number, which
    # a version bump cannot promise.
    #
    # The command is idempotent by construction: `git apply` succeeds on a
    # clean tree, and the `--reverse --check` fallback succeeds when the patch
    # is ALREADY applied, so a re-configure over a populated source directory
    # is not an error. Anything else -- a genuinely conflicting tree -- still
    # fails the configure, which is the direction to fail in.
    set(LLAMACPP_PATCH "${CMAKE_CURRENT_SOURCE_DIR}/patches/llamacpp-b6963-libcxx23-includes.patch")
    FetchContent_Declare(
        llamacpp
        GIT_REPOSITORY https://github.com/ggml-org/llama.cpp.git
        GIT_TAG 6db3d1ffe6d34e8becfe95cf2f109198a7154db3 # b6963
        PATCH_COMMAND git apply --whitespace=nowarn "${LLAMACPP_PATCH}"
                   || git apply --reverse --check "${LLAMACPP_PATCH}"
    )

    # Build llama.cpp/ggml at C++23 as of 2026-08-29, up from C++17. The
    # paragraph below is the ORIGINAL reasoning for the C++17 pin; it was right
    # about the mechanism and is kept because the cost it names is real and was
    # measured rather than dismissed:
    #
    #   C++23 turns CMake's Ninja module dependency scan on for these sources.
    #   Counted in build.ninja: llama/ggml scan steps go 1 -> 486, and the
    #   tree's total goes 3,639 -> 4,124 (+13%), for ~200 translation units
    #   that contain no `import` and no `export module` at all.
    #
    # Accepted knowingly. It buys one standard across everything this repo
    # compiles, which is what cpp_details.txt rule 50 asks for, and the cost
    # lands on incremental configure/scan rather than on the deploy image --
    # ENABLE_LLAMACPP_BACKEND is OFF there, so production compiles none of it.
    # Gated: full tree + ctest 103/103 at C++23 on clang 23, and the knob below
    # makes reverting a one-flag change rather than an edit.
    #
    # WHAT WAS NOT DONE, and deliberately: llama.cpp's SOURCES are not rewritten
    # to this repo's house rules -- trailing return types, [[nodiscard]],
    # std::expected, import std, no raw pointers. Those rules govern code we
    # own. llama.cpp is here to be an INDEPENDENT implementation for the parity
    # probes, and CLAUDE.md is explicit that "a reference that shared sensen's
    # code would prove nothing". Restyling ~200k lines of upstream would erode
    # precisely the independence that justifies vendoring it, and would have to
    # be redone on every bump. Compiling it at our standard is the part of
    # "C++23 compliance" that is both meaningful and free of that cost.
    #
    # --- original C++17 reasoning, retained ---
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
    # The standard is a knob so the choice can be MEASURED rather than argued.
    # 23 is the default. `-DLLAMACPP_CXX_STANDARD=17` restores upstream's own
    # pin in one flag, which is what makes the +485-scan-step cost above a
    # reversible decision rather than an edit somebody has to find.
    set(LLAMACPP_CXX_STANDARD 23 CACHE STRING
        "C++ standard for the vendored llama.cpp/ggml sources (23 = this repo's; 17 = upstream's own pin)")
    set(SAVED_CXX_STANDARD ${CMAKE_CXX_STANDARD})
    set(CMAKE_CXX_STANDARD ${LLAMACPP_CXX_STANDARD})

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
    # CANONICAL_FLAGS's own comments (cmake/SensenCanonicalFlags.cmake) already go out of
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

endif()  # ENABLE_LLAMACPP_BACKEND -- everything above here needs llama or ggml

# ── probes that do NOT need llama.cpp, and must not be gated on it ──────────
#
# These link `sensen_slim` and nothing else. They sat inside the
# `if(ENABLE_LLAMACPP_BACKEND)` block above until 2026-10-04, and three of them
# say in their own comments that they deliberately do not link llama.cpp -- so
# the guard contradicted the targets it contained.
#
# THE CONSEQUENCE WAS NOT COSMETIC: the production image is built
# `-DENABLE_LLAMACPP_BACKEND=OFF`, so in the configuration that actually ships,
# `llq_throughput_probe` -- the harness every LLQ weight-store figure in this
# repository comes from -- COULD NOT BE BUILT AT ALL. A target does not fail in
# that state, it VANISHES, which is indistinguishable from one that was never
# written and is the same defect class as a ctest target that disappears
# instead of skipping. Worse, a sweep script pointed at a build directory
# configured that way would run whatever older binary happened to be on disk.
#
# The guard above is still right for what remains inside it, and its own
# comment gives the reason: every llama.cpp-CONDITIONAL line belongs in one
# place so the OFF configuration is provably complete. That argument is about
# conditional lines. These targets are not conditional on llama.cpp; they were
# only sitting next to lines that are.
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

    # The same measurement, per WEIGHT STORE (dense | llq | llq-fused), with the
    # model's sha256 and quantisation recorded and an aggregate hash over every
    # generated token id so two arms can be compared for identity. See the
    # probe's own header. Needs the llq_weight_store module and OpenSSL (sha256).
    add_executable(llq_throughput_probe src/llq_throughput_probe.cpp)
    target_sources(llq_throughput_probe
        PRIVATE
        FILE_SET CXX_MODULES FILES
            src/modules/llq_weight_store.cppm
    )
    target_link_libraries(llq_throughput_probe PRIVATE sensen_slim OpenSSL::Crypto)

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
    if(ENABLE_LLAMACPP_BACKEND)  # ggml comes from the llama.cpp fetch
        add_executable(kv_range_probe src/kv_range_probe.cpp)
        target_link_libraries(kv_range_probe PRIVATE ggml sensen_slim)
    endif()

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

if(ENABLE_LLAMACPP_BACKEND)
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
# DerivationCorpusSweepTest runs `dbg_derivation`, which does NOT match the
# `^test_` filter above -- so without this the gate would run whatever binary
# happened to be on disk. That is precisely the stale-binary trap this file
# records against `ninja && ctest`, and a gate reading a stale binary passes
# while proving nothing.
if(TARGET dbg_derivation)
    add_dependencies(build_tests dbg_derivation)
endif()

# EncoderQueueClusterTest runs the real engine and a real queue node, neither of which matches the
# `^test_` filter -- so without this it would exercise whatever binaries happened to be on disk,
# which is the stale-binary trap again and the one that passes while proving nothing.
#
# `causality_bench` is the same shape one directory over: CausalityBenchGate and
# CausalityBenchGateCanFail run `bin/causality_bench`, SGEE builds it EXCLUDE_FROM_ALL, and
# `sgee_build_tests` does not list it -- so after `ninja build_tests` both read "Not Run", which is
# indistinguishable from two failures. The root fix belongs in SGEE's aggregate; until it lands
# here, a gate that ends in two permanent "Not Run" lines trains its reader to skip the summary.
foreach(_t calculator_engine sgee_queue_node causality_bench)
    if(TARGET ${_t})
        add_dependencies(build_tests ${_t})
    endif()
endforeach()

# assistant_check / workflow_check are not ctest targets -- each needs a
# GENERATED facts file, and a check that passes because its input was missing is
# worse than no check. They are still built here, because a binary nothing
# compiles rots exactly the way a binary nothing commits does: both of these
# sat untracked in a worktree while their generators sat in version control,
# gated by nothing.
foreach(_t assistant_check workflow_check)
    if(TARGET ${_t})
        add_dependencies(build_tests ${_t})
    endif()
endforeach()
