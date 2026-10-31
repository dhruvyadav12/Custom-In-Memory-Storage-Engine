# Multi-stage Dockerfile for Custom In-Memory Storage Engine
# Stage 1: Build Environment
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    g++ \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy project files
COPY . .

# Build with CMake
RUN mkdir -p build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release && \
    make -j$(nproc) && \
    ctest --output-on-failure

# Stage 2: Runtime Environment
FROM ubuntu:22.04 AS runtime

WORKDIR /app

# Copy built binaries from builder stage
COPY --from=builder /app/build/storage_server /app/storage_server
COPY --from=builder /app/build/storage_client /app/storage_client
COPY --from=builder /app/build/bench_storage /app/bench_storage

EXPOSE 6379

ENTRYPOINT ["/app/storage_server"]
CMD ["--host", "0.0.0.0", "--port", "6379", "--threads", "4"]
