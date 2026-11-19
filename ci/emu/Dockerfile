# T239.1 D12, D23: the emulator image of GitHub's runners (ghcr.io/<owner>/trx64u-emu): the build image plus ue2emu, built here from its public
# source (Jondalar/UE2-C64U-Emulator) at the pinned tag, so the emulator suites run in the job container (no docker socket there).
# Built by .github/workflows/ci-images.yml, which passes the build image by digest as BASE.
# The build stage is Ubuntu 22.04 too, so the binary links against the glibc and libasound it runs with.
ARG BASE=trx64u-build:ci

FROM ubuntu:22.04@sha256:9abcaffc92a5279fff3a6a41635c0800ea33e528425fb389658806476e7defde AS build
ARG UE2EMU_TAG=v0.7.0
ARG UE2EMU_COMMIT=4612884e7a633d52375277060f1a7e9bbf6552ba
ARG RUSTUP_VERSION=1.29.3
ARG RUSTUP_SHA256=b1c8cb14e30889e7e2999e6b69e62d9e9b9ffc0f4df8180564d306d9680e6c55
ARG RUST_VERSION=1.98.1
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates curl build-essential git libasound2-dev pkg-config \
 && rm -rf /var/lib/apt/lists/*
# rustup-init by version and SHA-256 (the .sha256 file of the same URL), the compiler at the version the emulator's install notes name.
RUN set -eux; \
    curl -fsSL -o /tmp/rustup-init "https://static.rust-lang.org/rustup/archive/${RUSTUP_VERSION}/x86_64-unknown-linux-gnu/rustup-init"; \
    echo "${RUSTUP_SHA256}  /tmp/rustup-init" | sha256sum -c -; \
    chmod +x /tmp/rustup-init; \
    /tmp/rustup-init -y --profile minimal --default-toolchain "${RUST_VERSION}"; \
    rm -f /tmp/rustup-init
ENV PATH=/root/.cargo/bin:$PATH
WORKDIR /src
# The tag must still point at the commit pinned here; cargo fetches its git dependency (trx64-core, pinned by tag in the emulator's Cargo.toml) itself.
RUN git clone -q --depth 1 --branch "${UE2EMU_TAG}" https://github.com/Jondalar/UE2-C64U-Emulator.git . \
 && [ "$(git rev-parse HEAD)" = "${UE2EMU_COMMIT}" ]
# Without the `net` feature: Ubuntu 22.04's libslirp (4.6) lacks slirp_handle_timer, and the suites run without a network anyway.
RUN cargo build --release --locked -p ue2emu --no-default-features --features trx64

FROM ${BASE}
RUN apt-get update && apt-get install -y --no-install-recommends libasound2 && rm -rf /var/lib/apt/lists/*
COPY --from=build /src/target/release/ue2emu /usr/local/bin/ue2emu
