#!/usr/bin/env bash
# GitHub #93 (T209 section "File Browser, n entries", the core application's side): the real generic application in ue2emu on a USB stick with a folder of N empty
# files named like snes zips. F3 > USB0 > snes, then "+" to the end (pictures: first page, last page, one page back). With a mock-up image built with
# -DLISTGROW_START=40 -DLISTGROW_HARD=60 the list is cut and the last page shows the line "... more not shown".
#
#   tests/coreapp/emu/library_n.sh <coreapp.app> <out dir> [files, default 784] [screen, default 1080p60]
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
source "$ROOT/ci/lib.sh"
APP="${1:?usage: library_n.sh <coreapp.app> <out dir> [files] [screen]}"; out="${2:?usage: library_n.sh <coreapp.app> <out dir> [files] [screen]}"; N="${3:-784}"; SCR="${4:-1080p60}"
UE2EMU="${UE2EMU:-$ROOT/ci/ue2emu.sh}"
kit="$(ue2emu_kit)" || { echo "no emulator kit" >&2; exit 77; }
mkdir -p "$out"; out="$(cd "$out" && pwd -P)"; APP="$(cd "$(dirname "$APP")" && pwd -P)/$(basename "$APP")"
w="$out/work-$N-$SCR"; rm -rf "$w"; mkdir -p "$w/stick/snes"
python3 -I - "$w/stick/snes" "$N" <<'PY'
import sys
d, n = sys.argv[1], int(sys.argv[2])
for i in range(n):
    open("%s/Game %04d (USA).sfc.zip" % (d, i), "wb").close()
PY
P="$out/lib-$N-$SCR"
{ printf 'expect-console "released" 60000\nwait 2500\nrocker up\nwait 1500\nkey f3\nwait 1000\nkey down\nkey down\nkey return\nwait 1500\nkey down\nkey return\nwait 4000\npng %s-1-top.png\n' "$P"
  for ((i = 0; i < $((N / 10 + 4)); i++)); do printf 'key +\nwait 120\n'; done
  printf 'wait 800\npng %s-2-last.png\nkey -\nwait 800\npng %s-3-pageback.png\nquit\n' "$P" "$P"; } > "$w/flow.ctl"
( cd "$w" && nice -n 15 timeout 600 "$UE2EMU" run --board c64u --caps 25E40202 --trx-io on --headless --speed max --screen "$SCR" --firmware "$APP" \
    --roms "$kit/roms" --flash "$w/flash.bin" --c64-roms "$kit/roms" --usb-dir "$w/stick" --usb-dir-work "$w/usbwork" --script "$w/flow.ctl" --max-seconds 500 > "$out/lib-$N-$SCR.log" 2>&1 )
rc=$?
rm -rf "$w"
ls -l "$out"/lib-$N-$SCR-*.png
exit $rc
