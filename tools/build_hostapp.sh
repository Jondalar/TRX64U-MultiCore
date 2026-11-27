#!/usr/bin/env bash
# Build the T84 host boot app (host-app) into
# host-app/target/u64ii/riscv/hostapp/result/hostapp.{bin,elf}
# with riscv32-unknown-elf-gcc from the 1541ultimate build image (u64-build-env,
# Apple's container CLI), like tools/build_bootrom.sh. nice-d, one job.
#
#   tools/build_hostapp.sh [make target...]    # default: all;  "clean" to start over
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# One job by default, same gitinfo race as tools/build_coreapp.sh (T93 5.15)
JOBS="${JOBS:-1}"
container run --rm --rosetta --arch amd64 -v "$ROOT:/trx" -w /trx/host-app u64-build-env bash -c '
set -e
H=/trx/host-app/target
# hex2bin writes the {dest, length, run} record of the .app; it is built for this image once, in
# vendor/gideon, and reached through host-app/tools (TOOLS in target/common/environment.mk)
T=/trx/vendor/gideon/tools
[ -x $T/hex2bin ] && head -c4 $T/hex2bin | grep -q ELF || make -C $T hex2bin checksum swap >/dev/null
mkdir -p /trx/host-app/tools
for t in hex2bin checksum; do ln -sfn ../../vendor/gideon/tools/$t /trx/host-app/tools/$t; done
for lib in lwip lvgl; do nice -n 15 make -j'"$JOBS"' -C $H/libs/riscv/$lib "$@"; done
# gitinfo.h is written by its own target; with -j it would race the first compile of hostapp.cc
git config --global --add safe.directory /trx || true
# T265 P2: the recovery program first: the host app links its slot image
make -C $H/u64ii/riscv/hostrecovery gitinfo
nice -n 15 make -j'"$JOBS"' -C $H/u64ii/riscv/hostrecovery "$@"
make -C $H/u64ii/riscv/hostapp gitinfo
nice -n 15 make -j'"$JOBS"' -C $H/u64ii/riscv/hostapp "$@"
' bash "$@"
