# syntax=docker/dockerfile:1
#
# Build context is the REPOSITORY ROOT, not backend/.
#
# railway.json sets `dockerfilePath: backend/Dockerfile`, which leaves the
# context at the repo root. Every path here is therefore repo-relative. The
# previous version used `COPY envoy.yaml` and `COPY start.sh`, which only
# resolve when the context is backend/ — so the image could never build on
# Railway at all.

# --------------------------------------------------------------------------
# Builder
# --------------------------------------------------------------------------
FROM docker.io/library/ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates wget gnupg lsb-release software-properties-common \
        ninja-build git pkg-config make \
        libssl-dev libtbb-dev liburing-dev libjemalloc-dev libpq-dev \
        libevent-dev libunwind-dev libbrotli-dev zlib1g-dev \
        libkrb5-dev libldap2-dev libsasl2-dev libsystemd-dev libcap-dev \
        libpcre2-dev libatomic1 \
        libstdc++-14-dev \
    && rm -rf /var/lib/apt/lists/*

# Clang defaults to libstdc++ on Linux, and Ubuntu 24.04's default is GCC 13's,
# which has no <print> — that header arrived in GCC 14. sensen_std.h includes it
# unconditionally, so the whole library fails to compile against the distro
# default. libstdc++-14-dev supplies it. The toolchain directory is pinned
# explicitly rather than left to Clang's newest-GCC-wins probe, so adding a
# different GCC to this image later cannot silently move the standard library
# out from under the build. Not libc++: TBB, which sensen uses throughout, is
# packaged here built against libstdc++, and mixing the two across that boundary
# is an ABI mismatch.
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
    && cmake --build /src/build --target calculator_engine smoke_client

# --------------------------------------------------------------------------
# Runtime
# --------------------------------------------------------------------------
#
# Envoy is not in the Ubuntu archives — `apt-get install envoy` fails — so the
# proxy binary is lifted out of the official image rather than starting FROM it.
#
# The direction matters. envoyproxy/envoy:v1.33 is built on Ubuntu 22.04, while
# the engine is built on 24.04 against glibc 2.39 and GCC 14's libstdc++.
# Basing the runtime on the Envoy image puts a 2.39-linked binary on a 2.35
# system, which cannot load. glibc guarantees backward compatibility, not
# forward: an older binary runs on a newer system, never the reverse. Envoy's
# own binary needs only libc/libm/libpthread/libdl/librt — no libstdc++ — so it
# is the one that travels safely. Hence a 24.04 runtime with Envoy copied in.
FROM docker.io/library/ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# Runtime shared objects only — no -dev packages, no toolchain. libstdc++6 is
# named explicitly rather than relied on transitively, since the engine is the
# only thing that needs it and the base image does not ship it.
#
# libz3-4 is here because libsensen links Z3 for gp_ara's SMT reasoning. It was
# never an explicit build dependency: the LLVM apt packages pull it in for
# Clang's static analyzer, so the builder had it by accident and the link
# silently succeeded. Nothing carried that accident to the runtime stage, and
# apt cannot notice — the image builds clean and the engine then dies at process
# start on a missing shared object. `ldd` against the built binaries is what
# catches this class, so treat that check as part of the build, not an extra.
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
# binary-next-to-lib/ layout is what lets it resolve.
COPY --from=builder /src/build/calculator_engine /app/calculator_engine
COPY --from=builder /src/build/smoke_client      /app/smoke_client
COPY --from=builder /src/build/lib/              /app/lib/

COPY backend/envoy.yaml /etc/envoy/envoy.yaml
COPY backend/start.sh   /app/start.sh

USER root
RUN chmod +x /app/start.sh /app/calculator_engine /app/smoke_client

ENV LD_LIBRARY_PATH=/app/lib

# Envoy serves gRPC-Web on 8080; the engine listens on 50051 behind it.
EXPOSE 8080 50051

ENTRYPOINT []
CMD ["/app/start.sh"]
