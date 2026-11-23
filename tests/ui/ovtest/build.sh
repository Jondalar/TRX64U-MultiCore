#!/bin/bash
# The test library of the overlay features: tests/ui/build/ovtest.u2a, which tests/ui/fixtures.py (make_ovtest_cor) packs into the .cor of the core a feature with
# `# core: <core>+ovtest` runs. The RISC-V compiler is the build image's, so run it where tools/dev.sh runs (the builder), or with RV_PREFIX=<your riscv32 toolchain>-.
#   tests/ui/ovtest/build.sh        RV_PREFIX=riscv32-unknown-elf-
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
RV="${RV_PREFIX:-riscv32-unknown-elf-}"
nice -n 15 make -C "$HERE" RV_PREFIX="$RV"
mkdir -p "$HERE/../build"
cp "$HERE/ovtest.u2a" "$HERE/../build/ovtest.u2a"
ls -l "$HERE/../build/ovtest.u2a"
