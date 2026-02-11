# Multi-stage build for erpl-deploy
# Build stage: Ubuntu 24.04 with full toolchain
# Runtime stage: Distroless for minimal image size

# ---------- Build stage ----------
FROM ubuntu:24.04 AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        ninja-build \
        git \
        curl \
        zip \
        unzip \
        tar \
        pkg-config \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Clone and bootstrap vcpkg
ENV VCPKG_ROOT=/opt/vcpkg
RUN git clone https://github.com/microsoft/vcpkg.git "$VCPKG_ROOT" \
    && "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics

WORKDIR /src
COPY . .

RUN make release

# ---------- Runtime stage ----------
FROM gcr.io/distroless/cc-debian12

COPY --from=build /src/build/erpl-deploy /erpl-deploy

ENTRYPOINT ["/erpl-deploy"]
