# Simulation and lint tools for the CI stages `lint` and `sims` (T31).
#
#   ci/sim/build_image.sh
#
# OSS CAD Suite (iverilog, GHDL, Verilator, Yosys) at the same pinned date as
# icepi-zero-c64's image, plus what that image lacks: a C++ compiler for the
# Verilator benches, make, 64tass and a RISC-V assembler for the bench programs
# (tools/flashgate_test.py calls it riscv64-elf-as, Arch naming), shellcheck, ruff.
# Ubuntu 24.04: the suite's libgrt (GHDL's runtime, linked into every elaborated
# bench) is built against glibc 2.38+ (__isoc23_strtol); 22.04 cannot link it.
# 24.04's tar uses fchmodat2, which the NAS Docker's seccomp profile rejects, so
# the suite is unpacked with Python (chmod(2)).
FROM ubuntu:24.04

ARG OSS_CAD_SUITE_DATE=2026-09-27

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=UTC

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates curl git make g++ ccache xz-utils zip unzip \
        python3 python3-pip python3-yaml python3-numpy python3-pil zlib1g-dev 64tass shellcheck binutils-riscv64-unknown-elf \
    && rm -rf /var/lib/apt/lists/* \
    && for t in as ld objcopy; do ln -s "$(command -v riscv64-unknown-elf-$t)" /usr/local/bin/riscv64-elf-$t; done \
    && pip3 install --no-cache-dir --break-system-packages ruff==0.6.9

RUN curl -fsSL "https://github.com/YosysHQ/oss-cad-suite-build/releases/download/${OSS_CAD_SUITE_DATE}/oss-cad-suite-linux-x64-$(echo ${OSS_CAD_SUITE_DATE} | tr -d -).tgz" \
        | python3 -c "import sys, tarfile; tarfile.open(fileobj=sys.stdin.buffer, mode='r|gz').extractall('/opt', filter='fully_trusted')" \
    && /opt/oss-cad-suite/bin/yosys -V \
    && /opt/oss-cad-suite/bin/ghdl --version | head -1 \
    && /opt/oss-cad-suite/bin/verilator --version \
    && /opt/oss-cad-suite/bin/iverilog -V | head -1

# bin/ holds wrappers that set up the suite's own library paths.
ENV PATH="/opt/oss-cad-suite/bin:${PATH}"
WORKDIR /work
