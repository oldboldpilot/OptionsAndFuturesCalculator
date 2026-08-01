# syntax=docker/dockerfile:1
#
# Build context is the REPOSITORY ROOT, not backend/.
#
# railway.json sets `dockerfilePath: backend/Dockerfile`, which leaves the
# context at the repo root. Every path here is therefore repo-relative. The
# previous version used `COPY envoy.yaml` and `COPY start.sh`, which only
# resolve when the context is backend/ — so the image could never build on
# Railway at all.

# MODEL_URL and MODEL_SHA256 are declared once, up here, before any FROM, so
# that both places that need them below -- the fetch stage immediately
# following, and the runtime stage's MODEL_PATH derivation near the bottom --
# redeclare (via a bare `ARG MODEL_URL`) the SAME global argument instead of
# silently drifting into two different ideas of what "the model" is. Neither
# has a real default: an empty MODEL_URL is a deliberate, supported build mode
# (see the comment on the `model` stage below for why), not a placeholder
# waiting to be filled in.
ARG MODEL_URL=""
ARG MODEL_SHA256=""

# --------------------------------------------------------------------------
# Model fetch: the fine-tuned Qwen3-0.6B GGUF (Q8_0, ~639 MB) that
# calculator.assistant.StrategyAssistant (backend/proto/assistant.proto) runs
# at inference time.
# --------------------------------------------------------------------------
#
# This is its own stage, and it is deliberately the FIRST stage in this file
# -- before the builder stage, before `WORKDIR /src` and `COPY backend/
# /src/backend/` below -- for a caching reason that actually matters, not a
# stylistic one. Docker's build cache keys a stage by that stage's own
# instructions, ARGs and base image, not by its position in the file; a
# genuinely separate `FROM ... AS model` stage is already immune to
# invalidation from source edits no matter where its text sits. What is NOT
# immune is a future shortcut that folds this wget-and-verify step into the
# BUILDER stage's own RUN chain, after `COPY backend/`. The instant that
# happens, the download's cache layer sits downstream of the source copy, and
# every code edit -- one leg's payoff formula, a comment -- invalidates it
# too, re-pulling 639 MB for a one-line diff. `backend/.railwayignore`'s
# comment two directories up documents `railway up`'s own ~30 s upload
# deadline for exactly this reason: this file is roughly ten times the entire
# payload that deadline already fails to carry, so it cannot travel through
# `railway up` at all and has to arrive here, during the BUILD, from outside
# the git tree. Keeping its fetch as an independent stage, positioned before
# the builder stage exists to tempt anyone into inlining it there, is what
# keeps that mistake from being possible by construction instead of relying
# on someone re-reading this comment first.
#
# MODEL_URL and MODEL_SHA256 are build ARGs, never literals baked into this
# file, for two separate reasons:
#
#   - ZERO HARDCODED SECRETS is binding project policy. A URL that serves a
#     639 MB artifact is realistically signed or otherwise access-controlled
#     -- an unauthenticated public host for that many bytes is its own
#     liability -- so it belongs with the rest of this project's secrets:
#     supplied at build time (Railway's configured build arguments, the same
#     mechanism that already supplies nothing sensitive for CMAKE_VERSION or
#     LLVM_VERSION above, but exists precisely for values like this one), and
#     never committed.
#   - The checksum has to travel WITH the URL, supplied by whoever minted it,
#     not derived from it here -- there is no way to know a download matches
#     the model it claims to be without an independent expectation to check
#     it against.
#
# A checksum mismatch FAILS THE BUILD. It does not fall back to "no model" and
# it does not ship the file anyway. A truncated or substituted 639 MB GGUF can
# still load successfully and then generate fluent, confident, wrong text --
# there is no crash to notice in production, just a StrategyAssistant quietly
# handing traders garbage that looks like an answer. That is a strictly worse
# outcome than a red build, so the build is what fails.
#
# An EMPTY MODEL_URL, by contrast, is not an error, and this is a deliberate
# product decision, not an oversight: a local developer build, a CI check, or
# a build made before the model has been uploaded anywhere yet must all still
# produce a working image. The options and futures calculator
# (calculator.OptionsCalculator) and the sensen finance surface
# (sensen.finance.Finance) are this application's actual product; neither
# depends on the assistant, and failing the entire image over an unset,
# genuinely optional feature would take real, working functionality down for
# a reason that has nothing to do with it. assistant.proto's own Refusal
# message already has a Reason for this exact situation --
# MODEL_UNAVAILABLE -- and the service is written to return it honestly
# instead of crashing or fabricating a response when it has no model to run.
# Building with no MODEL_URL simply exercises that path from container
# startup rather than from a later network failure; it is not a degraded or
# unsupported build, just one without the optional LLM feature in it. What
# DOES fail the build loudly is MODEL_URL being set without a working,
# verified download landing at the end of it -- at that point a model was
# promised and the build could not deliver it, which is an operator error
# worth stopping for, not something to paper over by quietly falling back to
# "no model" and shipping an image that merely looks fine.
FROM docker.io/library/ubuntu:24.04 AS model

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates wget \
    && rm -rf /var/lib/apt/lists/*

ARG MODEL_URL
ARG MODEL_SHA256
# Declared so the ${MODEL_TOKEN:-} fallback below can actually see a
# --build-arg. Without this ARG the variable is simply unset inside the stage
# and the fallback silently does nothing, which is how the first version of
# this failed: the error said "supply a token" while ignoring the one supplied.
ARG MODEL_TOKEN

# /model/strategy-assistant.gguf is the one place this filename is spelled
# out; the runtime stage's COPY --from=model and its MODEL_PATH both refer
# back to this same path rather than re-deriving it, so the three cannot
# drift apart.
# `--mount=type=secret` is what actually makes the secret appear at
# /run/secrets/model_token. Passing `--secret id=model_token,...` on the build
# command line alone is NOT enough -- both BuildKit and podman require the RUN
# that consumes a secret to declare it, and without this line the file simply
# never exists, the token check falls through, and the fetch 401s against a
# private repository while the error text insists a token was needed.
# `required=false` keeps a public MODEL_URL (and a plain MODEL_TOKEN build-arg)
# working on builders that pass no secret at all.
RUN --mount=type=secret,id=model_token,required=false \
    set -eu; \
    mkdir -p /model; \
    if [ -z "${MODEL_URL}" ]; then \
        echo "MODEL_URL not set -- building without calculator.assistant's model."; \
        echo "The calculator and finance services are unaffected; the assistant will answer MODEL_UNAVAILABLE at runtime instead of parsing trades."; \
    else \
        if [ -z "${MODEL_SHA256}" ]; then \
            echo "FATAL: MODEL_URL is set but MODEL_SHA256 is not." >&2; \
            echo "Shipping a 639 MB model with no integrity check is worse than refusing to build: a truncated or substituted GGUF can still load and then emit fluent, confident, wrong output -- there is nothing that crashes in production to reveal it. Supply the sha256 computed against the exact file MODEL_URL serves." >&2; \
            exit 1; \
        fi; \
        echo "Fetching model from MODEL_URL (checksum will be verified before it is used for anything)..."; \
        AUTH_HEADER=""; \
        if [ -f /run/secrets/model_token ]; then \
            AUTH_HEADER="Authorization: Bearer $(cat /run/secrets/model_token)"; \
            echo "Using the bearer token mounted at /run/secrets/model_token."; \
        elif [ -n "${MODEL_TOKEN:-}" ]; then \
            AUTH_HEADER="Authorization: Bearer ${MODEL_TOKEN}"; \
            echo "Using MODEL_TOKEN from the build environment."; \
        fi; \
        if [ -n "${AUTH_HEADER}" ]; then \
            wget --tries=3 --timeout=60 --header="${AUTH_HEADER}" \
                 -qO /model/strategy-assistant.gguf "${MODEL_URL}"; \
        else \
            wget --tries=3 --timeout=60 \
                 -qO /model/strategy-assistant.gguf "${MODEL_URL}"; \
        fi || { \
            echo "FATAL: could not fetch MODEL_URL." >&2; \
            echo "If the model lives in a PRIVATE repository this is almost certainly a 401: wget sends no credentials by default. Supply a token either as a BuildKit secret (--secret id=model_token,env=HF_TOKEN, which never enters the image or its history) or, failing that, as a MODEL_TOKEN build argument. A public MODEL_URL needs neither." >&2; \
            exit 1; \
        }; \
        if ! echo "${MODEL_SHA256}  /model/strategy-assistant.gguf" | sha256sum -c -; then \
            echo "FATAL: downloaded model failed sha256 verification against MODEL_SHA256." >&2; \
            echo "This means the bytes at MODEL_URL do not match what was expected -- a truncated download, a URL that now serves something else, or a stale checksum. Refusing to ship it rather than gamble that a corrupt GGUF happens to still work." >&2; \
            rm -f /model/strategy-assistant.gguf; \
            exit 1; \
        fi; \
        echo "Model verified against MODEL_SHA256."; \
    fi

# --------------------------------------------------------------------------
# Builder
# --------------------------------------------------------------------------
FROM docker.io/library/ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

# libtbb-dev is deliberately NOT in this list, and must not be added back.
#
# sensen probes for a system oneTBB with `find_package(TBB QUIET GLOBAL)`
# (backend/sensen/CMakeLists.txt:1090) and only falls through to the pinned
# FetchContent copy -- oneTBB v2022.3.0, compiled from source inside the build
# tree with this project's own flags -- when that probe finds nothing. Ubuntu's
# libtbb-dev ships a TBBConfig.cmake, so having it installed is enough to make
# the probe succeed and hand the build a TBB that Ubuntu compiled against
# libstdc++.
#
# While the engine was itself libstdc++ that was at worst a redundancy. Now that
# it is libc++ it is the one failure mode in this migration that does not
# announce itself. The parts of TBB this tree uses -- parallel_for,
# blocked_range, task_arena, task_group, all confined to sensen's parallel.cpp
# so no importer's BMI carries a tbb:: type -- are header templates instantiated
# in our own translation units, so a libstdc++-built TBB does not fail to link
# against a libc++ binary and does not fail to load. It links, the container
# starts, and the two halves disagree about the layout of every std:: type that
# crosses between them.
#
# Removing the package rather than trying to out-argue the probe in CMake is
# deliberate: `CMAKE_DISABLE_FIND_PACKAGE_TBB` was tried on the CMake side and
# made things strictly worse, because sensen falls back to a bare
# `find_library(TBB_LIB NAMES tbb ...)` when the probe fails, which then found a
# system shared object and put it on the link line anyway. Not having a prebuilt
# TBB present is the only version of this that cannot be worked around by a
# second search mechanism. `ldd` on the built binaries is the check: a
# libtbb.so.12 in there means this went wrong.
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates wget gnupg lsb-release software-properties-common \
        ninja-build git pkg-config make \
        libssl-dev liburing-dev libjemalloc-dev libpq-dev \
        libevent-dev libunwind-dev libbrotli-dev zlib1g-dev \
        libkrb5-dev libldap2-dev libsasl2-dev libsystemd-dev libcap-dev \
        libpcre2-dev libatomic1 \
        libstdc++-14-dev \
    && rm -rf /var/lib/apt/lists/*

# This project moved from libstdc++ to libc++ (backend/CMakeLists.txt adds
# -stdlib=libc++ globally), which reverses the second half of what this
# comment used to say, and that is worth spelling out rather than quietly
# deleting. TBB, which sensen uses throughout, WAS packaged here built against
# libstdc++, and mixing standard libraries across that boundary WAS an ABI
# mismatch — but as of the sensen_slim trim (see the runtime-stage comment
# below) the engine no longer links libsensen, or the TBB it bundled, at all,
# so that boundary no longer exists for this binary to cross. Whatever
# constraint TBB once placed on this decision is gone with it.
#
# What has NOT reversed, and is why libstdc++-14-dev is still installed and
# --gcc-install-dir still points into it, is the original half of this
# comment: Clang still needs a real GCC installation on Linux even when the
# C++ standard library it links against is libc++. -stdlib=libc++ only changes
# which headers and which libc++.so/libc++abi.so Clang reaches for; it does
# not make Clang self-sufficient for the low-level startup objects and runtime
# support library every linked binary still needs — crtbegin.o/crtend.o and
# libgcc.a/libgcc_s.so — unless the build also switches to compiler-rt and
# lld's own builtins in place of those, which this one does not. Those files
# live under /usr/lib/gcc/x86_64-linux-gnu/14/, and — verified by extracting
# Ubuntu noble's actual .deb packages rather than assumed — despite its name,
# libstdc++-14-dev does not put them there itself: it is a near-empty package
# whose only relevant job is depending on libgcc-14-dev, and libgcc-14-dev is
# what actually installs crtbegin.o, crtend.o, libgcc.a and libgcc_s.so into
# that directory. Removing libstdc++-14-dev would remove the GCC installation
# Clang links against, not merely an unused C++ standard library.
#
# The <print> problem this comment originally described — Ubuntu 24.04's
# default GCC 13 predates that header, and sensen_std.h includes it
# unconditionally — is moot now for a different reason: under -stdlib=libc++,
# <print> resolves against libc++'s own header, not GCC's, so GCC 13 vs. 14 no
# longer matters for that include specifically. The toolchain directory stays
# pinned explicitly regardless, for the reason it always was: leaving it to
# Clang's newest-GCC-wins probe means adding a different GCC to this image
# later could silently move the runtime support objects out from under the
# build.
ENV GCC_TOOLCHAIN_FLAGS="--gcc-install-dir=/usr/lib/gcc/x86_64-linux-gnu/14"

# CMake from Kitware, not the archive: sensen/CMakeLists.txt requires 4.1+ and
# Ubuntu 24.04 ships 3.28.3, so the distro package cannot configure the build.
ARG CMAKE_VERSION=4.2.7
RUN wget -qO /tmp/cmake.tar.gz \
        "https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-linux-x86_64.tar.gz" \
    && tar -xzf /tmp/cmake.tar.gz -C /opt \
    && ln -sf /opt/cmake-${CMAKE_VERSION}-linux-x86_64/bin/cmake /usr/local/bin/cmake \
    && ln -sf /opt/cmake-${CMAKE_VERSION}-linux-x86_64/bin/ctest /usr/local/bin/ctest \
    && rm /tmp/cmake.tar.gz \
    && cmake --version

# Pinned Clang. The previous `./llvm.sh all` pulled every LLVM package —
# several gigabytes and many minutes — when the build needs one toolchain.
#
# Version 22 is not arbitrary: it is the toolchain this codebase is actually
# developed and verified against. Clang 20 rejects SGEE's
# ReplicatedTaskBroker::apply_token_op — a member template called from a
# generic lambda earlier in the same class — with "used but not defined in this
# translation unit", although the definition sits a few lines below in that very
# class. Legal C++, and Clang 22 compiles it. Pinning the container below the
# development toolchain means the deploy target keeps diverging from what is
# tested, so it tracks it instead.
ARG LLVM_VERSION=22
RUN wget -qO /tmp/llvm.sh https://apt.llvm.org/llvm.sh \
    && chmod +x /tmp/llvm.sh \
    && /tmp/llvm.sh ${LLVM_VERSION} \
    && apt-get update && apt-get install -y --no-install-recommends \
        libc++-${LLVM_VERSION}-dev libc++abi-${LLVM_VERSION}-dev lld-${LLVM_VERSION} \
    && rm -rf /var/lib/apt/lists/* /tmp/llvm.sh \
    && ln -sf /usr/bin/clang-${LLVM_VERSION} /usr/bin/clang \
    && ln -sf /usr/bin/clang++-${LLVM_VERSION} /usr/bin/clang++

WORKDIR /src

# Only the backend tree participates in the build; copying the repo root would
# drag in the frontend and its node_modules.
COPY backend/ /src/backend/

# cpp23-logger builds the `std` module from libc++'s own sources and only looks
# in /usr/share/libc++/v1 and /usr/local. The LLVM apt packages install them
# per-version instead, so the location has to be passed in explicitly.
RUN cmake -S /src/backend -B /src/build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER=clang \
        -DCMAKE_CXX_COMPILER=clang++ \
        -DCMAKE_CXX_FLAGS="${GCC_TOOLCHAIN_FLAGS}" \
        -DLIBCXX_MODULES_PATH=/usr/lib/llvm-${LLVM_VERSION}/share/libc++/v1 \
        -DCPP23_LOGGER_LIBCXX_MODULES_PATH=/usr/lib/llvm-${LLVM_VERSION}/share/libc++/v1 \
    && cmake --build /src/build --target calculator_engine smoke_client proto_descriptor

# calculator_engine and smoke_client now link against libc++ instead of
# libstdc++6, which is a problem the runtime stage below cannot solve out of
# its own apt sources: Ubuntu 24.04 ships its OWN libc++1 package, and that is
# LLVM 18 — one major version behind the LLVM 22 this binary is actually
# linked against. Installing it would be version skew wearing the same
# soname, which is worse than installing nothing: the container would start,
# then fail unpredictably deep inside libc++ instead of refusing to launch.
# The only libc++ this binary can safely run against is the one that just
# built it, sitting in this stage's own apt cache.
#
# Rather than hardcode a literal path under /usr/lib and hope it matches
# whatever package layout apt.llvm.org happens to use for LLVM 22 on Ubuntu
# 24.04, this step asks the freshly linked binaries themselves via `ldd` —
# the same authority the runtime-stage comment below already treats as the
# source of truth for the rest of this image's shared-object list — and
# copies back exactly what they name. That also settles, empirically and
# per-build rather than by assumption, whether this particular libc++abi
# build unwinds through libunwind.so.1 or through libgcc_s.so.1: whichever it
# is shows up in `ldd`'s output and gets copied; nothing here hardcodes an
# answer to that question, so a future LLVM version that unwinds differently
# is handled correctly without anyone having to notice and update this file.
#
# `cp -L` dereferences the symlink chain apt.llvm.org's packages actually use
# (a versioned compatibility symlink under /usr/lib/llvm-22/lib pointing at an
# unversioned real file elsewhere) down to a single flat regular file, so what
# lands in /src/build/lib/ needs no symlink target to also be present
# alongside it. A missing or unresolved entry FAILS THE BUILD here, loudly,
# rather than producing an image that builds clean and then dies at container
# start on a missing shared object — exactly the failure mode the
# runtime-stage comment below was written to warn about.
RUN set -eu; \
    mkdir -p /src/build/lib; \
    NEEDED_LIBCXX="$(ldd /src/build/calculator_engine /src/build/smoke_client | grep -E 'libc\+\+|libunwind' || true)"; \
    echo "libc++/libunwind entries ldd found across calculator_engine and smoke_client:"; \
    echo "${NEEDED_LIBCXX}"; \
    if [ -z "${NEEDED_LIBCXX}" ]; then \
        echo "FATAL: ldd named no libc++/libunwind shared object for either binary." >&2; \
        echo "Under -stdlib=libc++ this build must depend on at least libc++.so.1. Either the link did not actually happen against libc++, or ldd's output format no longer matches the grep filter this step relies on -- investigate, do not assume libstdc++ is fine." >&2; \
        exit 1; \
    fi; \
    if echo "${NEEDED_LIBCXX}" | grep -q "not found"; then \
        echo "FATAL: ldd could not resolve one of these dependencies inside the builder itself, immediately after the binaries that need it were linked:" >&2; \
        echo "${NEEDED_LIBCXX}" >&2; \
        exit 1; \
    fi; \
    for lib in $(echo "${NEEDED_LIBCXX}" | awk '{print $3}' | sort -u); do \
        cp -Lv "${lib}" /src/build/lib/; \
    done; \
    ls -l /src/build/lib

# --------------------------------------------------------------------------
# Runtime
# --------------------------------------------------------------------------
#
# Envoy is not in the Ubuntu archives — `apt-get install envoy` fails — so the
# proxy binary is lifted out of the official image rather than starting FROM it.
#
# The direction matters. envoyproxy/envoy:v1.33 is built on Ubuntu 22.04, while
# the engine is built on 24.04 against glibc 2.39. (Which C++ standard library
# the engine links — libstdc++ before this codebase moved to libc++, libc++
# since — doesn't change this argument: glibc is the shared floor both images
# actually sit on.) Basing the runtime on the Envoy image puts a 2.39-linked
# binary on a 2.35 system, which cannot load. glibc guarantees backward compatibility, not
# forward: an older binary runs on a newer system, never the reverse. Envoy's
# own binary needs only libc/libm/libpthread/libdl/librt — no libstdc++ — so it
# is the one that travels safely. Hence a 24.04 runtime with Envoy copied in.
FROM docker.io/library/ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# Runtime shared objects only — no -dev packages, no toolchain. libstdc++6 is
# named explicitly rather than relied on transitively, since the base image
# does not ship it. Before this project moved the engine to libc++
# (backend/CMakeLists.txt), the engine was the only thing here that needed
# libstdc++6, and this comment said so. That specific claim is now EXPECTED to
# be false — a libc++-linked calculator_engine has no reason to also need
# libstdc++.so.6 — but the package still cannot be dropped, and this has been
# checked, not assumed: libtbb12, a few lines below in this same list, is
# Ubuntu's own package for the TBB runtime, and Ubuntu's libtbb12 itself
# Depends on libstdc++6 (>= 13.1) at the package-manager level, independent of
# anything this engine links. As long as libtbb12 is installed, libstdc++6 has
# to be too, whether or not the engine still touches it directly.
#
# libz3-4 was here because libsensen linked Z3 for gp_ara's SMT reasoning. It
# was never an explicit build dependency: the LLVM apt packages pull it in for
# Clang's static analyzer, so the builder had it by accident and the link
# silently succeeded. Nothing carried that accident to the runtime stage, and
# apt cannot notice — the image builds clean and the engine then dies at process
# start on a missing shared object. `ldd` against the built binaries is what
# catches this class, so treat that check as part of the build, not an extra.
#
# As of the sensen_slim trim (backend/CMakeLists.txt) the engine no longer links
# libsensen, so the packages that existed only for it are no longer reachable
# through that path. THE WHOLE LIST IS NEVERTHELESS LEFT INTACT, deliberately.
#
# Correcting an earlier claim in this file, because acting on it would break the
# image: liburing2 is NOT vestigial. `ldd /app/calculator_engine` inside the
# built image lists liburing.so.2, and it arrives via SGEE, not sensen —
# libsgee.a carries io_uring symbols. Dropping it would produce an image that
# fails at process start, which is exactly the failure this comment block was
# written to warn about.
#
# For the record, the sonames the three shipped binaries (calculator_engine,
# smoke_client, envoy) actually resolved to, before this project moved the
# engine to libc++, were:
#
#   libbrotlicommon/dec/enc.so.1  libcap.so.2   libcrypto.so.3  libc.so.6
#   libdl.so.2   libgcc_s.so.1   libgcrypt.so.20   libgpg-error.so.0
#   libjemalloc.so.2   liblz4.so.1   liblzma.so.5   libm.so.6
#   libpthread.so.0   librt.so.1   libssl.so.3   libstdc++.so.6
#   libsystemd.so.0   liburing.so.2   libz.so.1   libzstd.so.1
#
# The libc++ migration (backend/CMakeLists.txt adding -stdlib=libc++ globally)
# is EXPECTED to add libc++.so.1 and libc++abi.so.1 to that list and remove
# libstdc++.so.6 from it — but flagging that honestly rather than stating it
# as fact: this list has NOT been re-derived from an actual built image as
# part of this change, because no image was built to check it against. What
# HAS been independently verified, by extracting apt.llvm.org's actual .deb
# packages for LLVM 22 on Ubuntu 24.04 (noble) amd64 — the exact combination
# this Dockerfile's builder stage installs — and reading their ELF dynamic
# sections directly with `readelf -d`, rather than assuming: libc++.so.1's own
# NEEDED entries are libc++abi.so.1, libc.so.6, libm.so.6 and libgcc_s.so.1,
# and libc++abi.so.1's are libc.so.6, ld-linux-x86-64.so.2, libm.so.6 and
# libgcc_s.so.1. Neither names libunwind.so.1. That answers, for this specific
# upstream build, a question this comment would otherwise have to leave open:
# this libc++abi unwinds through libgcc_s, not libunwind, so libunwind8's
# presence in the apt-get list below remains what it already was before this
# migration — installed, but not exercised by anything that actually needs
# it. None of that lets this comment off the hook, though: the mechanism that
# actually populates /app/lib/ (the builder-stage `ldd`-driven step, see the
# comment above `cmake --build`) does not take this paragraph's word for any
# of it — it reads calculator_engine's and smoke_client's real NEEDED closures
# at build time and copies whatever is actually there, so a future LLVM
# version that unwinds differently, or a build that turns out to still need
# libstdc++.so.6 for some reason not accounted for above, is handled correctly
# regardless of whether anyone remembers to update this paragraph to match.
#
# libz3 and libpq do not appear in the pre-migration list above, and TBB is
# linked statically. Trimming unused packages would save a little image size;
# it is not being done, because the saving is small and the cost of getting it
# wrong is a container that will not start. Anyone revisiting this should
# re-derive the list above from the built image rather than from this comment
# — `ldd` on the binaries is the source of truth, and reasoning about which
# subproject "owns" a library is how the wrong answer gets reached.
RUN apt-get update && apt-get install -y --no-install-recommends \
        libstdc++6 libssl3t64 libtbb12 liburing2 libjemalloc2 libpq5 \
        libevent-2.1-7t64 libunwind8 libbrotli1 zlib1g libgssapi-krb5-2 \
        libldap2 libsasl2-2 libsystemd0 libcap2 libpcre2-8-0 libatomic1 \
        libz3-4 ca-certificates \
    && rm -rf /var/lib/apt/lists/*

COPY --from=docker.io/envoyproxy/envoy:v1.33-latest \
     /usr/local/bin/envoy /usr/local/bin/envoy

WORKDIR /app

# The engine links libsensen as a shared object. CMAKE_BUILD_RPATH_USE_ORIGIN
# makes its RPATH $ORIGIN-relative (cpp_details.txt rule 54), so preserving the
# binary-next-to-lib/ layout is what lets it resolve. As of the libc++
# migration, this same /src/build/lib/ is also where the builder-stage
# `ldd`-driven step above (see the comment near `cmake --build`) deposits
# libc++.so.1 and libc++abi.so.1 -- this COPY carries those into /app/lib/
# wholesale along with whatever else CMake put there, with no separate COPY
# instruction needed for them.
COPY --from=builder /src/build/calculator_engine /app/calculator_engine
COPY --from=builder /src/build/smoke_client      /app/smoke_client
COPY --from=builder /src/build/lib/              /app/lib/

COPY backend/envoy.yaml /etc/envoy/envoy.yaml
COPY backend/start.sh   /app/start.sh

# The descriptor set Envoy's JSON transcoder resolves methods and types from.
# Taken from the builder rather than committed, so it is regenerated from the
# .proto files on every build and cannot fall out of step with the stubs the
# engine was compiled against.
COPY --from=builder /src/build/generated/api_descriptor.pb /etc/envoy/api_descriptor.pb

# calculator.assistant.StrategyAssistant's model, carried over from the
# `model` stage at the top of this file, which already verified it against
# MODEL_SHA256 (or deliberately fetched nothing -- see that stage's comment
# for why an empty MODEL_URL is a supported build, not an error). This COPY
# always succeeds, whether or not a model was fetched: when MODEL_URL was
# empty, /model/ in the source stage is simply an empty directory, and
# copying zero files into /app/model/ is not a failure. The COPY itself is
# therefore not the signal the rest of the system acts on -- MODEL_PATH,
# right below, is.
COPY --from=model /model/ /app/model/

# Redeclared: ARG scope is per-stage, so the global MODEL_URL declared before
# the `model` stage above does not automatically carry into this later FROM
# without this line. Its only use here is the ${MODEL_URL:+...} test in the
# ENV instruction below -- the model's actual bytes, if any, already crossed
# stages via the COPY immediately above, and do not depend on this ARG at
# runtime.
ARG MODEL_URL

# Docker's ENV instruction cannot conditionally omit itself, so a build with
# no MODEL_URL leaves MODEL_PATH set to an EMPTY STRING here rather than
# truly absent from the container's environment -- the closest a single,
# static runtime stage can get to "unset" without duplicating this entire
# stage behind a second, build-arg-selected target purely to vary one
# variable. calculator.assistant.StrategyAssistant is written to treat an
# empty MODEL_PATH exactly like a missing one: neither names a usable file,
# so both mean "no model is available," and it answers MODEL_UNAVAILABLE
# rather than trying to open "" and failing in some less legible way.
ENV MODEL_PATH=${MODEL_URL:+/app/model/strategy-assistant.gguf}

USER root
RUN chmod +x /app/start.sh /app/calculator_engine /app/smoke_client

ENV LD_LIBRARY_PATH=/app/lib

# Envoy serves gRPC-Web on 8080; the engine listens on 50051 behind it.
EXPOSE 8080 50051

ENTRYPOINT []
CMD ["/app/start.sh"]
