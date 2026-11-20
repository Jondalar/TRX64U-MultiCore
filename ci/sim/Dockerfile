# Simulation and lint tools for the CI stages `lint` and `sims` (T31).
#
#   ci/sim/build_image.sh
#
# OSS CAD Suite (iverilog, GHDL, Verilator, Yosys) at the same pinned date as
# icepi-zero-c64's image, plus what that image lacks: a C++ compiler for the
# Verilator benches, make, 64tass and a RISC-V assembler for the bench programs
# (tools/flashgate_test.py calls it riscv64-elf-as, Arch naming), shellcheck, ruff.
# Ubuntu 24.04 (by digest, the index digest read from the registry): the suite's libgrt (GHDL's runtime, linked into every elaborated
# bench) is built against glibc 2.38+ (__isoc23_strtol); 22.04 cannot link it.
# 24.04's tar uses fchmodat2, which the NAS Docker's seccomp profile rejects, so
# the suite is unpacked with Python (chmod(2)).
FROM ubuntu:24.04@sha256:73b999393d07f2bf65a40373778a44dc9a6d54ad7718d29247d8ff9413c4d6a8

ARG OSS_CAD_SUITE_DATE=2026-09-27

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=UTC

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates curl git make g++ ccache xz-utils zip unzip \
        python3 python3-pip python3-yaml python3-numpy python3-pil zlib1g-dev 64tass shellcheck binutils-riscv64-unknown-elf \
    && rm -rf /var/lib/apt/lists/* \
    && for t in as ld objcopy; do ln -s "$(command -v riscv64-unknown-elf-$t)" /usr/local/bin/riscv64-elf-$t; done \
    && pip3 install --no-cache-dir --break-system-packages ruff==0.6.9

# T215 D2: ci/code_hash.py (plan + cache keys); the pins are those of ci/codehash-requirements.txt (keep equal), grammars are fetched now, not at run time
ENV TREE_SITTER_LANGUAGE_PACK_CACHE_DIR=/opt/tslp-grammars
RUN pip3 install --no-cache-dir --break-system-packages tree-sitter==0.26.1 tree-sitter-language-pack==1.23.1 \
    && python3 -c "import tree_sitter_language_pack as p; p.download(['verilog','vhdl','c','cpp','python','tcl','bash','toml'])"

RUN curl -fsSL "https://github.com/YosysHQ/oss-cad-suite-build/releases/download/${OSS_CAD_SUITE_DATE}/oss-cad-suite-linux-x64-$(echo ${OSS_CAD_SUITE_DATE} | tr -d -).tgz" \
        | python3 -c "import sys, tarfile; tarfile.open(fileobj=sys.stdin.buffer, mode='r|gz').extractall('/opt', filter='fully_trusted')" \
    && /opt/oss-cad-suite/bin/yosys -V \
    && /opt/oss-cad-suite/bin/ghdl --version | head -1 \
    && /opt/oss-cad-suite/bin/verilator --version \
    && /opt/oss-cad-suite/bin/iverilog -V | head -1

# bin/ holds wrappers that set up the suite's own library paths.
ENV PATH="/opt/oss-cad-suite/bin:${PATH}"
WORKDIR /work
