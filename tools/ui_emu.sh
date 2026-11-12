#!/usr/bin/env bash
# The host app's UI in the ue2 emulator (T101; UI path only: the emulator runs the host app unmodified, RETURN on a core does
# not load it, there is no .cfw/ICAP and no core behind it but the C64).
#
#   tools/ui_emu.sh [--app <hostapp.app>] [--cores <dir>] [--headless --script <s.ctl>] [-- <more ue2emu flags>]
#
#   --app      the host app [default: the newest dist/pkg-main-*/hostapp.app]
#   --cores    .cor/.cfw files for the stick, copied into a fresh USB0 [default: the .cor of the same package]
#   --headless with --script: no window, e.g. "wait 25000 / key r / wait 5000 / png OUT/menu.png / quit" (PNG 960x540, the
#              pixel layer; the stick (USB0) is mounted about 20 s emulated after the start, so earlier scans see no bundles);
#              without it a window opens for keyboard use.
# The emulator lives in ../u64-emulator (UE2EMU_DIR), branch s37-trx-pixel-layer, macOS build only; it is used in place.
# Every run gets its own scratch dir with a copy of the emulator's flash image (the run writes it) and is removed at the end.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
U="${UE2EMU_DIR:-$ROOT/../u64-emulator}"
BIN="$U/target/release/ue2emu"
app="" cores="" headless="" script="" extra=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --app) app="$2"; shift 2 ;;
    --cores) cores="$2"; shift 2 ;;
    --headless) headless=1; shift ;;
    --script) script="$2"; shift 2 ;;
    --) shift; extra=("$@"); break ;;
    *) echo "ui_emu: unknown argument $1" >&2; exit 2 ;;
  esac
done
[[ -x "$BIN" ]] || { echo "ui_emu: no emulator at $BIN (cargo build --release in $U)" >&2; exit 1; }
if [[ -z "$app" ]]; then
  app="$(ls -t "$ROOT"/dist/pkg-main-*/hostapp.app 2>/dev/null | head -1 || true)"
  [[ -n "$app" ]] || { echo "ui_emu: no dist/pkg-main-*/hostapp.app; pass --app" >&2; exit 1; }
fi
[[ -n "$cores" ]] || cores="$(dirname "$app")"
[[ -z "$headless" || -n "$script" ]] || { echo "ui_emu: --headless needs --script" >&2; exit 2; }

S="$(mktemp -d "${TMPDIR:-/tmp}/ui_emu.XXXXXX")"
trap 'rm -rf "$S"' EXIT
mkdir -p "$S/stick/cores" "$S/usbwork"
cp "$U/run/flash.bin" "$S/flash.bin"
n=0
for f in "$cores"/*.cor "$cores"/*.cfw; do
  [[ -e "$f" ]] || continue
  cp "$f" "$S/stick/cores/"; n=$((n + 1))
done
echo "ui_emu: app $app, $n bundle(s) from $cores"
args=(run --board c64u --trx-io=on --firmware "$app" --roms "$U/firmware/1541ultimate/roms" --flash "$S/flash.bin" --c64-roms
      --usb-dir "$S/stick" --usb-dir-work "$S/usbwork")
if [[ -n "$headless" ]]; then
  args+=(--headless --speed max --script "$script")
elif [[ -n "$script" ]]; then
  args+=(--script "$script")
fi
nice -n 15 "$BIN" "${args[@]}" "${extra[@]+"${extra[@]}"}"
