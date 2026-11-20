# T239.1 D12: the build image of GitHub's runners (ghcr.io/<owner>/trx64u-build). The RISC-V compiler of the former u64-build-env, nothing else of it:
# no Quartus, no ESP-IDF. Built by .github/workflows/ci-images.yml; the proof is the log line of `riscv32-unknown-elf-gcc --version` (11.3.0).
#
# ubuntu:22.04 by digest (the index digest read from the registry; it moves only when this file moves).
FROM ubuntu:22.04@sha256:9abcaffc92a5279fff3a6a41635c0800ea33e528425fb389658806476e7defde

ARG XPACK_VERSION=11.3.0-1
ARG XPACK_SHA256=1c99fb1573ff2ed5deffce64cb96bb9272f351f054684c49dfd5ad41c6dd804d

ENV DEBIAN_FRONTEND=noninteractive

# make, python3, git, curl, ccache and a host C/C++ compiler: ci/apptests.sh exits without c++, tools/make_update.sh builds hex2bin, checksum and swap with make.
# zip and unzip: the app suites and the package steps read and write archives. python3-pip: for the code-hash libraries below.
# zlib1g-dev: the render benches (tests/view, tests/coreapp/render, tests/hostapp/render) include zlib.h and link -lz.
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates curl git make build-essential ccache python3 python3-pip zip unzip xz-utils zlib1g-dev \
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

# The pinned tree-sitter libraries of ci/code_hash.py (D13): the cache keys of the gateware targets hash the sources through them, and ci/plan.py
# needs them. ubuntu:22.04 has Python 3.10, so this is the cp310 install of the builder's pylibs-cp310. ci/install_codehash.sh and its requirements
# file come from the build context "codehash" (the ci/ folder: build-contexts of ci-images.yml, or --build-context codehash=ci with docker build),
# the grammars are downloaded here (checked by the language pack's manifest), so a run needs no network for them.
COPY --from=codehash install_codehash.sh codehash-requirements.txt /tmp/codehash/
RUN bash /tmp/codehash/install_codehash.sh /opt/codehash \
    && rm -rf /tmp/codehash \
    && CODE_HASH_LIBS=/opt/codehash TREE_SITTER_LANGUAGE_PACK_CACHE_DIR=/opt/codehash/grammars python3 -c \
       "import sys; sys.path.insert(0, '/opt/codehash'); import tree_sitter, tree_sitter_language_pack as p; p.get_parser('verilog'); print('code-hash libs ok, python', sys.version.split()[0])"
ENV CODE_HASH_LIBS=/opt/codehash \
    TREE_SITTER_LANGUAGE_PACK_CACHE_DIR=/opt/codehash/grammars

ENV CCACHE_DIR=/ccache
WORKDIR /work
