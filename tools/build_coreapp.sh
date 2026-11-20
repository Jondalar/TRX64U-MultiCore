#!/usr/bin/env bash
# Build the T93 core application (coreapp/) into
# host-app/target/u64ii/riscv/coreapp/result/coreapp.{bin,elf,app}
# with riscv32-unknown-elf-gcc from the 1541ultimate build image (u64-build-env, Apple's container CLI), on the same
# base as tools/build_hostapp.sh (it builds the lwip and lvgl libraries the same way). nice-d, JOBS jobs.
#
#   tools/build_coreapp.sh [make target...]    # default: all;  "clean" to start over
#   tools/build_coreapp.sh --dist              # build, then dist/coreapps/<core>.app for atari800, nes, apple2, zx, cpc, zxnext, snes, vc20, c264 and amiga (BLUEPRINT_CORES)
#
# One image serves every core (T93 D14): the descriptor is chosen at run time from the core's ID block, so the
# files in dist/coreapps/ are the same bytes under the names ci/package.sh and tools/pack_cores_mc.sh look for.
# Those files hold coreapp.bin (the raw image), not coreapp.app - see the copy below.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# One job by default: `all` has `gitinfo` beside the compile targets (host-app/target/common/rules.mk:23), and with
# -j the rule rewrites gitinfo.h with `>` while a source that includes it is being compiled, which fails as
# "'APP_BUILD_DATE' was not declared" (T93 5.15). JOBS=2 is fine once that rule writes the file atomically.
JOBS="${JOBS:-1}"
DIST=0
if [ "${1:-}" = "--dist" ]; then DIST=1; shift; fi
container run --rm --rosetta --arch amd64 -v "$ROOT:/trx" -w /trx/host-app u64-build-env bash -c '
set -e
H=/trx/host-app/target
T=/trx/vendor/gideon/tools
[ -x $T/hex2bin ] && head -c4 $T/hex2bin | grep -q ELF || make -C $T hex2bin checksum swap >/dev/null
mkdir -p /trx/host-app/tools
for t in hex2bin checksum; do ln -sfn ../../vendor/gideon/tools/$t /trx/host-app/tools/$t; done
for lib in lwip lvgl; do nice -n 15 make -j'"$JOBS"' -C $H/libs/riscv/$lib "$@"; done
git config --global --add safe.directory /trx || true
make -C $H/u64ii/riscv/coreapp gitinfo
nice -n 15 make -j'"$JOBS"' -C $H/u64ii/riscv/coreapp "$@"
' bash "$@"
R="$ROOT/host-app/target/u64ii/riscv/coreapp/result"
if [ -f "$R/coreapp.app" ]; then
  ls -l "$R"/coreapp.app "$R"/coreapp.bin
  if [ "$DIST" = 1 ]; then
    mkdir -p "$ROOT/dist/coreapps"
    # Only the cores that have a descriptor in coreapp/src/table.cc. Not "every core whose core.toml says blueprint": the Amiga and
    # the A1200 say so too (spec 5.2) and their applications are Gideon-base builds that must never be overwritten here.
    for c in ${BLUEPRINT_CORES:-atari800 nes apple2 zx cpc zxnext snes neogeo ti99 vc20 c264 pet c128 c64scpu amiga diag}; do
      # The .bin, never the .app: a .app is the image behind a 12-byte header (load, length, entry), which the host
      # update file wants and a bundle does not - cor_pack puts record 3 at app_load as it is and jumps to app_entry,
      # so the header would sit on the entry point. That is what shipped on 2026-10-15 and left the Atari and the NES
      # executing their own load address (T93 5.12). The name stays <core>.app because that is what ci/package.sh and
      # tools/pack_cores_mc.sh look for; cor_pack.py refuses a header here now, so the mistake cannot come back quietly.
      cp "$R/coreapp.bin" "$ROOT/dist/coreapps/$c.app"
      echo "dist/coreapps/$c.app"
    done
  fi
fi
