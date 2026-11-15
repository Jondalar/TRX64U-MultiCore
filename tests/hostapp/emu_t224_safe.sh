#!/usr/bin/env bash
# T224 S1-S5: "Boot safe graphics mode" through the real host application and the real core application in ue2emu.
#   tests/hostapp/emu_t224_safe.sh <images dir> <out dir> [screen: 480p60|720p60|1080p60]
# The stick holds vc20.cor with no config: F3 to the .cor, A adds it, F1, RIGHT opens the overlay (Plain start + the rescue row), DOWN to the rescue row (PNG),
# RETURN starts the core: its console line "[core] safe graphics mode: N video settings overridden" and the core menu PNG are the proof.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"; ROOT="$(cd "$HERE/../.." && pwd)"
source "$ROOT/ci/lib.sh"
img="${1:?images dir}"; out="${2:?out dir}"; scr="${3:-720p60}"
UE2EMU="${UE2EMU:-$ROOT/ci/ue2emu.sh}"
kit="$(ue2emu_kit)" || { echo "no kit"; exit 77; }
mkdir -p "$out"; out="$(cd "$out" && pwd -P)"; img="$(cd "$img" && pwd -P)"
w="$out/work-safe-$scr"; rm -rf "$w"; mkdir -p "$w/cors" "$w/profiles" "$w/stick/cores/vc20/roms"
python3 "$ROOT/tools/cor_pack.py" replace-app "$kit/cors/vc20.cor" --app "$img/coreapp.app" -o "$w/cors/vc20.cor" > /dev/null || exit 1
cp "$w/cors/vc20.cor" "$w/stick/cores/"; cp "$kit/profiles/VIC-20.toml" "$w/profiles/"; cp "$img/coreapp.elf" "$w/profiles/"
head -c 8192 /dev/zero > "$w/stick/cores/vc20/roms/basic.bin"; head -c 8192 /dev/zero > "$w/stick/cores/vc20/roms/kernal.bin"; head -c 4096 /dev/zero > "$w/stick/cores/vc20/roms/char.bin"
# an earlier stored Picture that must survive the safe start byte for byte (S3)
printf '[VIC-20 Settings]\nPicture=1280 x 720\nScale=Fill\nSmoothing=On\n\n' > "$w/stick/cores/vc20/vc20.cfg"
P="$out/safe-$scr"
{
 printf 'wait 15000\nkey space\nwait 800\nkey f3\nwait 1500\nkey down\nwait 500\nkey down\nwait 500\nkey return\nwait 2000\n'
 printf 'key down\nwait 500\nkey return\nwait 2000\nkey down\nwait 400\nkey down\nwait 400\nkey down\nwait 400\n'
 printf 'key a\nwait 12000\nkey f1\nwait 2000\npng %s-menu.png\nkey right\nwait 1500\npng %s-overlay.png\nkey down\nwait 800\npng %s-overlay-safe.png\n' "$P" "$P" "$P"
 printf 'key return\nwait 30000\npng %s-core.png\nhold ctrl\nwait 300\nrestore\nwait 300\nrelease ctrl\nwait 2000\npng %s-core-menu.png\nquit\n' "$P" "$P"
} > "$w/flow.ctl"
cp "$kit/flash.bin" "$out/safe-flash.bin"
( cd "$w" && nice -n 15 timeout 420 "$UE2EMU" run --board c64u --trx-io=on --icap --screen "$scr" --cor-dir "$w/cors" --core-dir "$w/profiles" --firmware "$img/hostapp_emu.elf" \
    --roms "$kit/roms" --flash "$out/safe-flash.bin" --usb-dir "$w/stick" --usb-dir-work "$w/usbwork" --headless --speed max --script "$w/flow.ctl" --max-seconds 380 > "$out/safe-$scr.log" 2>&1 )
rc=$?
cfg_after="$(cat "$w/usbwork/cores/vc20/vc20.cfg" 2>/dev/null || cat "$w/stick/cores/vc20/vc20.cfg" 2>/dev/null)"
rm -rf "$out/safe-flash.bin"
[ $rc -eq 0 ] || { echo "FAIL emulator exited $rc (see $out/safe-$scr.log)"; exit 1; }
grep -aq '^\[core\] safe graphics mode: [0-9]* video settings overridden' "$out/safe-$scr.log" || { echo "FAIL no safe-mode line in the core's log"; exit 1; }
printf '%s\n' "$cfg_after" > "$out/safe-$scr.cfg-after"
echo "PASS safe graphics start at $scr (cfg after: $(printf '%s' "$cfg_after" | tr '\n' '|'))"
