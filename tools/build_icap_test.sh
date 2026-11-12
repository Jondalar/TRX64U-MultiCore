#!/usr/bin/env bash
# Build the T79 ICAP loader test program (host/icap_test) into build/icap_test.bin with
# riscv32-unknown-elf-gcc from the 1541ultimate build image (u64-build-env, Apple's container CLI).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
container run --rm --rosetta --arch amd64 -v "$ROOT:/trx" -w /trx/host/icap_test \
    u64-build-env make
