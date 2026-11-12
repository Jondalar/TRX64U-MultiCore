#!/usr/bin/env bash
# T152: the host application's MultiCore update and its F8 rows in ue2emu (the emulator's SPI flash is a model with erase and program, so what the run writes can be
# read back from the flash image afterwards). Not part of CI: the emulator lives in ../u64-emulator (UE2EMU_DIR), macOS only; the host app is a build with
# HOSTAPP_EXTRA=-DHOSTAPP_EMU_FLASH (its flash is an Infineon S25FL128L, which the board build refuses and the lock leaves alone: JEDEC 01 60 18).
#
#   tests/hostapp/emu_txu.sh --app <hostapp.app> --stick <dir> --script <s.ctl> --out <dir> [--old-firmware]
#
# --stick is copied to the emulated USB stick (the .txu files, .cfw/.ue2 for the inspection, cores/ for the survey). --old-firmware fills the bitstream and application
# slots first (the sectors of a real update are never blank). The script is ue2emu's control language (docs/specs/S08 there); "@OUT@" in it is the --out directory,
# "png <path>" writes a picture. The flash image the run ended with is <out>/flash_after.bin, the one it started with <out>/flash_before.bin.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
U="${UE2EMU_DIR:-$ROOT/../u64-emulator}"
BIN="$U/target/release/ue2emu"
app="" stick="" script="" out="" prefill=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --app) app="$2"; shift 2 ;;
    --stick) stick="$2"; shift 2 ;;
    --script) script="$2"; shift 2 ;;
    --out) out="$2"; shift 2 ;;
    --old-firmware) prefill=1; shift ;;
    *) echo "emu_txu: unknown argument $1" >&2; exit 2 ;;
  esac
done
[[ -x "$BIN" && -f "$app" && -d "$stick" && -f "$script" && -n "$out" ]] || { echo "emu_txu: need the emulator at $BIN, --app, --stick (a directory), --script and --out" >&2; exit 1; }
mkdir -p "$out"
S="$(mktemp -d "${TMPDIR:-/tmp}/emu_txu.XXXXXX")"
trap 'rm -rf "$S"' EXIT
mkdir -p "$S/usbwork"
cp -R "$stick" "$S/stick"
cp "$U/run/flash.bin" "$S/flash.bin"
if [[ "$prefill" = 1 ]]; then
  python3 -I - "$S/flash.bin" <<'PY'
import sys
f = open(sys.argv[1], "r+b")
f.write(bytes(((i * 7 + 0x21) & 0xFF) | 0x01 for i in range(0x240000)))      # programmed over erased flash: bits only cleared, nothing to read as blank
f.close()
PY
fi
cp "$S/flash.bin" "$out/flash_before.bin"
sed -e "s|@OUT@|$out|g" "$script" > "$S/run.ctl"
nice -n 15 "$BIN" run --board c64u --trx-io=on --firmware "$app" --roms "$U/firmware/1541ultimate/roms" --flash "$S/flash.bin" --c64-roms \
     --usb-dir "$S/stick" --usb-dir-work "$S/usbwork" --headless --speed max --script "$S/run.ctl" > "$out/emu.log" 2>&1 || { echo "emu_txu: the emulator exited non-zero, see $out/emu.log" >&2; tail -5 "$out/emu.log" >&2; }
cp "$S/flash.bin" "$out/flash_after.bin"
ls -l "$out"
