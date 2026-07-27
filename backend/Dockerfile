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
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates wget gnupg lsb-release software-properties-common \
        ninja-build git pkg-config make \
        libssl-dev libtbb-dev liburing-dev libjemalloc-dev libpq-dev \
        libevent-dev libunwind-dev libbrotli-dev zlib1g-dev \
        libkrb5-dev libldap2-dev libsasl2-dev libsystemd-dev libcap-dev \
        libpcre2-dev libatomic1 \
    && rm -rf /var/lib/apt/lists/*

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
ARG LLVM_VERSION=20
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

RUN cmake -S /src/backend -B /src/build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_C_COMPILER=clang \
        -DCMAKE_CXX_COMPILER=clang++ \
    && cmake --build /src/build --target calculator_engine smoke_client

# --------------------------------------------------------------------------
# Runtime
# --------------------------------------------------------------------------
#
# Envoy is not packaged in the Ubuntu archives — `apt-get install envoy` on
# ubuntu:24.04 fails, which is why the previous image never produced a working
# container. Starting from the official Envoy image gives a known-good proxy on
# the same Ubuntu base the engine was built against.
FROM envoyproxy/envoy:v1.33-latest

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        libssl3t64 libtbb12 liburing2 libjemalloc2 libpq5 libevent-2.1-7t64 \
        libunwind8 libbrotli1 zlib1g libgssapi-krb5-2 libldap-2.5-0 \
        libsasl2-2 libsystemd0 libcap2 libpcre2-8-0 libatomic1 \
        libc++1-18 libc++abi1-18 ca-certificates \
    && rm -rf /var/lib/apt/lists/*

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
