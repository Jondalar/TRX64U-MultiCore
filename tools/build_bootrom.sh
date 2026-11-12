#!/usr/bin/env bash
# Build the rvlite boot ROM (host/bootrom) into host/bootrom_pkg.vhd,
# with riscv32-unknown-elf-gcc from the 1541ultimate build image (u64-build-env,
# Apple's container CLI; see 1541ultimate/build/build.sh).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
container run --rm --rosetta --arch amd64 -v "$ROOT:/trx" -w /trx/host/bootrom \
    u64-build-env make
