#!/usr/bin/env bash
# GitHub #93 (T209 section "File Browser, n entries"): the real host application in ue2emu on a USB stick with a folder of 784 empty files named like snes zips.
# F3 > USB0 > snes, then "+" to the end (pictures: first page, last page, one page back, the cursor walk down to the last row).
#
#   tests/hostapp/emu_browser_n.sh <images dir with hostapp_emu.elf> <out dir> [files, default 784]
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
source "$ROOT/ci/lib.sh"
img="${1:?usage: emu_browser_n.sh <images dir> <out dir> [files]}"; out="${2:?usage: emu_browser_n.sh <images dir> <out dir> [files]}"; N="${3:-784}"
UE2EMU="${UE2EMU:-$ROOT/ci/ue2emu.sh}"
kit="$(ue2emu_kit)" || { echo "no emulator kit" >&2; exit 77; }
mkdir -p "$out"; out="$(cd "$out" && pwd -P)"; img="$(cd "$img" && pwd -P)"
w="$out/work"; rm -rf "$w"; mkdir -p "$w/cors" "$w/profiles" "$w/stick/snes"
python3 -I - "$w/stick/snes" "$N" <<'PY'
import sys
d, n = sys.argv[1], int(sys.argv[2])
for i in range(n):
    open("%s/Game %04d (USA).sfc.zip" % (d, i), "wb").close()
PY
P="$out/browse-$N"
{ printf 'wait 15000\nkey space\nwait 800\nkey f3\nwait 1500\nkey down\nwait 500\nkey down\nwait 500\nkey return\nwait 2500\n'
  printf 'key down\nwait 500\nkey return\nwait 4000\npng %s-1-top.png\n' "$P"
  for ((i = 0; i < $((N / 12 + 4)); i++)); do printf 'key +\nwait 150\n'; done
  printf 'wait 800\npng %s-2-last.png\nkey -\nwait 800\npng %s-3-pageback.png\n' "$P" "$P"
  for ((i = 0; i < $((N / 12 + 4)); i++)); do printf 'key +\nwait 100\n'; done
  for ((i = 0; i < 3; i++)); do printf 'key down\nwait 200\n'; done
  printf 'wait 500\npng %s-4-cursor-end.png\nquit\n' "$P"; } > "$w/flow.ctl"
cp "$kit/flash.bin" "$out/flash.bin"
( cd "$w" && nice -n 15 timeout 600 "$UE2EMU" run --board c64u --trx-io=on --icap --cor-dir "$w/cors" --core-dir "$w/profiles" --firmware "$img/hostapp_emu.elf" \
    --roms "$kit/roms" --flash "$out/flash.bin" --usb-dir "$w/stick" --usb-dir-work "$w/usbwork" --headless --speed max ${EMU_SCREEN:+--screen $EMU_SCREEN} --script "$w/flow.ctl" \
    --max-seconds 500 > "$out/browse-$N.log" 2>&1 )
rc=$?
rm -rf "$w" "$out/flash.bin"
ls -l "$out"/browse-$N-*.png
exit $rc
