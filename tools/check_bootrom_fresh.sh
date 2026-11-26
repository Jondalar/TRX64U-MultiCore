#!/usr/bin/env bash
# T265 P5: host/bootrom_pkg.vhd is made by hand (tools/build_bootrom.sh) and is a key input of the host. Red when the last commit that touched host/bootrom/*
# came after the last commit that touched host/bootrom_pkg.vhd, i.e. the boot ROM source moved and the package was not rebuilt. A shallow checkout cannot
# tell: skipped, out loud.
set -euo pipefail
cd "$(dirname "$0")/.."
if [ "$(git rev-parse --is-shallow-repository)" = true ]; then echo "check_bootrom_fresh: shallow checkout, skipped"; exit 0; fi
src="$(git log -1 --format=%H -- host/bootrom)"; pkg="$(git log -1 --format=%H -- host/bootrom_pkg.vhd)"
[ -n "$src" ] && [ -n "$pkg" ] || { echo "check_bootrom_fresh: no history for host/bootrom or host/bootrom_pkg.vhd" >&2; exit 1; }
if [ "$src" != "$pkg" ] && git merge-base --is-ancestor "$pkg" "$src"; then
  echo "host/bootrom changed in ${src:0:9} after host/bootrom_pkg.vhd (${pkg:0:9}): rebuild it with tools/build_bootrom.sh and commit it" >&2
  exit 1
fi
