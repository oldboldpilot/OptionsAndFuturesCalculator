FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

# Install build dependencies and Envoy
RUN apt-get update && apt-get install -y \
    wget \
    gnupg \
    lsb-release \
    software-properties-common \
    cmake \
    ninja-build \
    git \
    libssl-dev \
    envoy \
    && rm -rf /var/lib/apt/lists/*

# Install latest Clang (simulating Clang 22 in this environment)
RUN wget https://apt.llvm.org/llvm.sh && \
    chmod +x llvm.sh && \
    ./llvm.sh all && \
    rm llvm.sh

# Link the installed clang to default clang
RUN ln -sf /usr/bin/clang-* /usr/bin/clang && \
    ln -sf /usr/bin/clang++-* /usr/bin/clang++

WORKDIR /app

# Copy project files
COPY . .

# Configure and build the backend engine
RUN mkdir -p build && cd build && \
    CXX=clang++ CC=clang cmake -G Ninja -DCMAKE_BUILD_TYPE=Release .. && \
    ninja calculator_engine

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    libssl-dev \
    envoy \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy Envoy configuration
COPY envoy.yaml /etc/envoy/envoy.yaml

# Copy the built engine
COPY --from=builder /app/build/calculator_engine /app/build/calculator_engine
COPY start.sh /app/start.sh
RUN chmod +x /app/start.sh

# Expose Envoy proxy port
EXPOSE 8080
# Expose backend port (optional, mostly for debugging)
EXPOSE 50051

CMD ["/app/start.sh"]
