#!/bin/bash
# Builds the coreapp image the suite runs (tests/ui/build/coreapp.app) on the build host, because the RISC-V build is heavy (the Mac keeps to two heavy
# processes). It copies the sources of this worktree over a prepared tree on the host (default: ~/t153 on `builder`, a copy of a tree that built once),
# runs the build image with nice, a time limit and --rm, and fetches the result.
#
#   tests/ui/build.sh             BUILDER=builder  BUILD_TREE=t153  BUILD_IMAGE=u64-build-env:ci
#
# The first time, make the tree on the host:  cp -a <a tree that built coreapp> ~/t153  (the libraries of host-app/target/libs are built there once).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
B="${BUILDER:-builder}"; T="${BUILD_TREE:-t153}"; IMG="${BUILD_IMAGE:-u64-build-env:ci}"
cd "$ROOT"
tar c --no-xattrs --exclude='*/result' --exclude='*/output' --exclude='*.o' coreapp app host-app/target/u64ii/riscv/coreapp/Makefile host-app/software vendor/gideon/software vendor/gideon/tools tools tests/coreapp/golden 2>/dev/null \
  | ssh "$B" "cd ~/$T && tar xm"
ssh "$B" "timeout 1500 docker run --rm -v \$HOME/$T:/trx -w /trx/host-app $IMG bash -c '
set -e
H=/trx/host-app/target
mkdir -p /trx/host-app/tools
make -C /trx/vendor/gideon/tools hex2bin checksum swap >/dev/null
for t in hex2bin checksum; do ln -sfn ../../vendor/gideon/tools/\$t /trx/host-app/tools/\$t; done
for lib in lwip lvgl; do nice -n 15 make -j4 -C \$H/libs/riscv/\$lib >/dev/null; done
git config --global --add safe.directory /trx || true
make -C \$H/u64ii/riscv/coreapp gitinfo >/dev/null || true
nice -n 15 make -j6 -C \$H/u64ii/riscv/coreapp 2>&1 | tail -n 15
'" 
mkdir -p "$HERE/build"
scp -q "$B:$T/host-app/target/u64ii/riscv/coreapp/result/coreapp.app" "$HERE/build/coreapp.app"
ls -l "$HERE/build/coreapp.app"
