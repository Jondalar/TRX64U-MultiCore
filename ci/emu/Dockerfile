# T174 D2.8: the CI build image with ue2emu inside, so the emulator suites run in the job container (no docker socket there).
# Build context: an export of u64-emulator at the pinned release (ci/emu/build.sh). The build stage is the
# build env's Ubuntu 22.04 too, so the binary links against the same glibc and libasound it runs with.
FROM ubuntu:22.04 AS build
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates curl build-essential git libasound2-dev pkg-config \
 && rm -rf /var/lib/apt/lists/* && curl -sSf https://sh.rustup.rs | sh -s -- -y --profile minimal
ENV PATH=/root/.cargo/bin:$PATH
WORKDIR /src
COPY . .
# Without the `net` feature: Ubuntu 22.04's libslirp (4.6) lacks slirp_handle_timer, and the suites run without a network anyway.
RUN cargo build --release -j 5 -p ue2emu --no-default-features --features trx64

FROM u64-build-env:ci
RUN apt-get update && apt-get install -y --no-install-recommends libasound2 && rm -rf /var/lib/apt/lists/*
COPY --from=build /src/target/release/ue2emu /usr/local/bin/ue2emu
