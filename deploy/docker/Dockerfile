# syntax=docker/dockerfile:1.4
# ==============================================================================
# CeeBase Production Container: Multi-Stage Build
# Stage 1: Build C++ kernel, Rust CLI, and Python runtime packages
# Stage 2: Hardened, non-root minimal runtime image
# ==============================================================================

FROM debian:bookworm-slim AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    ninja-build \
    python3 \
    python3-dev \
    python3-pip \
    curl \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Install Rust toolchain for CLI
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
ENV PATH="/root/.cargo/bin:${PATH}"

WORKDIR /workspace
COPY . .

# 1. Build C++ Kernel & C API Shared Library
RUN cmake -B build -S . -G Ninja -DCMAKE_BUILD_TYPE=Release -DCEEBASE_BUILD_TESTS=OFF \
    && cmake --build build --target ceebase

# 2. Build Rust CLI
RUN cargo build --release --manifest-path tools/cli/Cargo.toml

# ==============================================================================
# Stage 2: Final Minimal Runtime Image
# ==============================================================================
FROM python:3.11-slim-bookworm AS runtime

LABEL org.opencontainers.image.title="CeeBase Database Server" \
      org.opencontainers.image.description="High-performance C++20 database engine with ACID transactions & persistent catalog" \
      org.opencontainers.image.version="1.0.0" \
      org.opencontainers.image.authors="MackenzieCampbelAndross <mackenzieandross@gmail.com>" \
      org.opencontainers.image.licenses="Apache-2.0"

# Install curl for HEALTHCHECK
RUN apt-get update && apt-get install -y --no-install-recommends \
    curl \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Security: Non-root dedicated service user
RUN groupadd -g 10001 ceebase && \
    useradd -u 10001 -g ceebase -m -s /bin/bash ceebase

# Create application directories
WORKDIR /var/lib/ceebase
RUN mkdir -p /var/lib/ceebase/data /var/lib/ceebase/backups /var/lib/ceebase/bin /var/lib/ceebase/lib && \
    chown -R ceebase:ceebase /var/lib/ceebase

# Copy compiled binaries and libraries from builder
COPY --from=builder /workspace/build/libceebase.so* /usr/local/lib/
COPY --from=builder /workspace/tools/cli/target/release/ceebase /usr/local/bin/ceebase
RUN ldconfig

# Copy application source & bindings
COPY --chown=ceebase:ceebase bindings /var/lib/ceebase/bindings
COPY --chown=ceebase:ceebase tools /var/lib/ceebase/tools
COPY --chown=ceebase:ceebase src /var/lib/ceebase/src
COPY --chown=ceebase:ceebase mcp /var/lib/ceebase/mcp

# Install python bindings in editable mode
RUN pip install --no-cache-dir -e /var/lib/ceebase/bindings/python

# Environment defaults
ENV CEEBASE_PORT=8080 \
    CEEBASE_HOST=0.0.0.0 \
    CEEBASE_DATABASE=/var/lib/ceebase/data/ceebase.cbdb \
    PYTHONUNBUFFERED=1

# Expose HTTP REST / Web Studio Port
EXPOSE 8080

# Persistent volume for database files and WAL
VOLUME ["/var/lib/ceebase/data", "/var/lib/ceebase/backups"]

# Switch to non-root user
USER ceebase

# Healthcheck
HEALTHCHECK --interval=10s --timeout=3s --start-period=5s --retries=3 \
    CMD curl -f http://localhost:8080/api/v1/health || exit 1

# Graceful termination
STOPSIGNAL SIGTERM

ENTRYPOINT ["python3", "tools/studio/server.py", "--port", "8080", "--host", "0.0.0.0"]
