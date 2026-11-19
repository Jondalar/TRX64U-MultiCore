# T239.1 D12: the build image of GitHub's runners (ghcr.io/<owner>/trx64u-build). The RISC-V compiler of the former u64-build-env, nothing else of it:
# no Quartus, no ESP-IDF. Built by .github/workflows/ci-images.yml; the proof is the log line of `riscv32-unknown-elf-gcc --version` (11.3.0).
#
# ubuntu:22.04 by digest (the index digest read from the registry; it moves only when this file moves).
FROM ubuntu:22.04@sha256:9abcaffc92a5279fff3a6a41635c0800ea33e528425fb389658806476e7defde

ARG XPACK_VERSION=11.3.0-1
ARG XPACK_SHA256=1c99fb1573ff2ed5deffce64cb96bb9272f351f054684c49dfd5ad41c6dd804d

ENV DEBIAN_FRONTEND=noninteractive

# make, python3, git, curl, ccache and a host C/C++ compiler: ci/apptests.sh exits without c++, tools/make_update.sh builds hex2bin, checksum and swap with make.
# zip and unzip: the app suites and the package steps read and write archives.
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates curl git make build-essential ccache python3 zip unzip xz-utils \
    && rm -rf /var/lib/apt/lists/*

# xPack riscv-none-elf-gcc, checked against the SHA-256 the publisher lists in the .sha file of the same release. The makefiles call the
# riscv32-unknown-elf-* names, so every tool of the archive's bin/ gets a link under that name.
RUN set -eux; \
    f="xpack-riscv-none-elf-gcc-${XPACK_VERSION}-linux-x64.tar.gz"; \
    curl -fsSL -o "/tmp/$f" "https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases/download/v${XPACK_VERSION}/$f"; \
    echo "${XPACK_SHA256}  /tmp/$f" | sha256sum -c -; \
    mkdir -p /opt/xpack-riscv; \
    tar -xzf "/tmp/$f" -C /opt/xpack-riscv --strip-components=1; \
    rm -f "/tmp/$f"; \
    for t in /opt/xpack-riscv/bin/riscv-none-elf-*; do \
        ln -s "$t" "/usr/local/bin/riscv32-unknown-elf-${t##*/riscv-none-elf-}"; \
    done; \
    riscv32-unknown-elf-gcc --version | head -1 | grep -F '11.3.0'

ENV CCACHE_DIR=/ccache
WORKDIR /work
