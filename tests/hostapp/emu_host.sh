#!/usr/bin/env bash
# T174 D2.8: the host application's real flow in ue2emu (S44, docs/specs/S44-multicore-cores.md of the emulator): the host app starts, F3 > USB0 > cores >
# vc20.cor > S starts a DFX core (the partial is found by its SHA-256 in the emulator's --cor-dir), the core application identifies VC20 and takes the
# machine, CTRL+RESTORE opens its menu, F8 > "Return to host menu" brings the host application back (IPROG WBSTAR 0, the menu marker).
#
#   tests/hostapp/emu_host.sh <images dir> <out dir>
#
# <images dir>: hostapp_emu.elf (ci/trxapps.sh: the host app built with HOSTAPP_EMU_FLASH) and coreapp.elf. <out dir>: the run's log, flash image and PNGs
# (host-start.png, core-menu.png, host-back.png; the T187 picture flow: host-home, host-f8-list, host-picture-page, host-picture-applied). The kit (ROMs, flash.bin, profile, vc20.cor) is found as ci/lib.sh ue2emu_kit says (tools/ue2emu_kit.sh);
# the emulator is ci/ue2emu.sh (local binary or the UE2EMU_IMAGE container). Prints PASS/FAIL lines; 
# Exit 0 = PASS, 1 = FAIL, 77 = nothing to run with (no images, kit or emulator: said out loud).
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
source "$ROOT/ci/lib.sh"
img="${1:?usage: emu_host.sh <images dir> <out dir>}"; out="${2:?usage: emu_host.sh <images dir> <out dir>}"
UE2EMU="${UE2EMU:-$ROOT/ci/ue2emu.sh}"
skip() { echo "hostapp_emu SKIPPED out loud: $*" >&2; exit 77; }
[ -f "$img/hostapp_emu.elf" ] && [ -f "$img/coreapp.elf" ] && [ -f "$img/coreapp.app" ] || skip "no hostapp_emu.elf / coreapp.elf / coreapp.app in $img (ci/trxapps.sh builds them)"
kit="$(ue2emu_kit)" || skip "no emulator kit (UE2EMU_KIT, tools/ue2emu_kit.sh)"
[ -f "$kit/cors/vc20.cor" ] && [ -f "$kit/profiles/VIC-20.toml" ] || skip "kit $kit has no cors/vc20.cor or profiles/VIC-20.toml"
"$UE2EMU" run --help >/dev/null 2>&1 || skip "no emulator ($UE2EMU)"
mkdir -p "$out"; out="$(cd "$out" && pwd -P)"; img="$(cd "$img" && pwd -P)"
# T221: the images of a dev build carry no version (no git in the build container); the package stamps them (ci/package.sh), so the flows run on stamped copies, the pictures show real strings
mkdir -p "$out/img"; cp "$img/hostapp_emu.elf" "$img/coreapp.elf" "$img/coreapp.app" "$out/img/"
for fld in "TRXVSTAMP hostapp" "TRXHSTAMP host" "TRXBSTAMP bundle"; do
    python3 -I "$ROOT/tools/stamp_version.py" stamp "$out/img/hostapp_emu.elf" --marker "${fld% *}" --version "$("$ROOT/ci/version.sh" "${fld#* }")" >/dev/null || skip "cannot stamp ${fld% *} into hostapp_emu.elf"
done
python3 -I "$ROOT/tools/stamp_version.py" stamp "$out/img/coreapp.elf" "$out/img/coreapp.app" --version "$("$ROOT/ci/version.sh" coreapp)" >/dev/null || skip "cannot stamp coreapp"
img="$out/img"
FAIL=0

# nav: past the countdown, F3, USB0, into cores (the list is .., vc20/, vc20.cor), the .cor, S = start; back: the hotkey, RESTORE, F8 System (first focus Restart core), one down to Return to host menu (T187 D8)
nav() { printf 'wait 15000\npng %s/host-start.png\nkey space\nwait 800\nkey f3\nwait 1500\nkey down\nwait 500\nkey down\nwait 500\nkey return\nwait 2000\n' "$out"
        printf 'key down\nwait 500\nkey return\nwait 2000\nkey down\nwait 500\nkey down\nwait 500\nkey s\nwait 30000\n'; }
back() { printf 'hold ctrl\nwait 300\nrestore\nwait 300\nrelease ctrl\nwait 2000\npng %s/core-menu.png\nkey f8\nwait 1500\n' "$out"
         for ((i = 0; i < 1; i++)); do printf 'key down\nwait 300\n'; done
         printf 'key return\nwait 30000\npng %s/host-back.png\nquit\n' "$out"; }

run_flow() {   # run_flow NAME WITH_COR(1|0): the whole flow; the log is $out/NAME.log
    local name=$1 with=$2 w="$out/work-$1"
    rm -rf "$w"; mkdir -p "$w/cors" "$w/profiles" "$w/stick/cores/vc20/roms"
    # the kit's vc20.cor carries the core application it was packed with; the flow must run the image under test, whose ELF gives the symbols
    # (C64U_emu, 2026-11-08: the kit's app ran with this build's ELF, so the fault hook fired on a wrong address - a false vAssertCalled halt)
    if [ "$with" = 1 ]; then
        python3 "$ROOT/tools/cor_pack.py" replace-app "$kit/cors/vc20.cor" --app "$img/coreapp.app" -o "$w/cors/vc20.cor" > /dev/null || return 1
        cp "$w/cors/vc20.cor" "$w/stick/cores/"
    fi
    cp "$kit/profiles/VIC-20.toml" "$w/profiles/"; cp "$img/coreapp.elf" "$w/profiles/"
    head -c 8192 /dev/zero > "$w/stick/cores/vc20/roms/basic.bin"; head -c 8192 /dev/zero > "$w/stick/cores/vc20/roms/kernal.bin"
    head -c 4096 /dev/zero > "$w/stick/cores/vc20/roms/char.bin"
    { nav; back; } > "$w/flow.ctl"
    cp "$kit/flash.bin" "$out/$name-flash.bin"
    ( cd "$w" && nice -n 15 timeout 420 "$UE2EMU" run --board c64u --trx-io=on --icap --cor-dir "$w/cors" --core-dir "$w/profiles" --firmware "$img/hostapp_emu.elf" \
        --roms "$kit/roms" --flash "$out/$name-flash.bin" --usb-dir "$w/stick" --usb-dir-work "$w/usbwork" --headless --speed max --script "$w/flow.ctl" \
        --max-seconds 380 > "$out/$name.log" 2>&1 )
    local rc=$?
    rm -rf "$w" "$out/$name-flash.bin"
    return $rc
}
check_flow() {   # check_flow NAME: 0 when the flow ran through; prints the first missing step
    local l="$out/$1.log"
    grep -aq '^icap: the partial of "VIC-20"' "$l" || { echo "the partial of VIC-20 was not found"; return 1; }
    grep -aq 'phase core identified: \[core\] VC20 VIC-20, cor /USB0/cores/vc20.cor' "$l" || { echo "the core application did not identify VC20"; return 1; }
    grep -aq '^icap: IPROG WBSTAR 0: the host bitstream, .* the menu marker set' "$l" || { echo "no IPROG back to the host bitstream"; return 1; }
    grep -aq 'direct boot: decision .*marker 1' "$l" || { echo "the host application did not take the menu marker"; return 1; }
    return 0
}

run_flow flow 1; rc=$?
if [ $rc -ne 0 ]; then echo "FAIL host->vc20->menu->host: the emulator exited $rc (see $out/flow.log)"; FAIL=1
elif why="$(check_flow flow)"; then echo "PASS host->vc20->menu->host: partial found, VC20 identified, back on the host with the menu marker (PNGs in $out)"
else echo "FAIL host->vc20->menu->host: $why (see $out/flow.log)"; FAIL=1; fi

# T187: the Picture flow of the host (own run, no core): boot menu (two-line rows of 36 px, one entry added from the stick with A), F8 settings list with the Picture row, the V key opens the Picture page,
# RETURN on a mode applies it. The emulator has no mode request register (hv_gateware() is false: "the old firmware path"), so the 8 s question and the
# revert do not exist here: the render bench (tests/hostapp/render, picnote_*.png) is their proof.
pic_flow() { printf 'wait 15000\nkey space\nwait 800\nkey f3\nwait 1500\nkey down\nwait 500\nkey down\nwait 500\nkey return\nwait 2000\n'
             printf 'key down\nwait 500\nkey return\nwait 2000\nkey down\nwait 500\nkey down\nwait 6000\nkey a\nwait 8000\nkey f1\nwait 2000\npng %s/host-home.png\nkey f8\nwait 1500\npng %s/host-f8-list.png\nkey v\nwait 1500\npng %s/host-picture-page.png\n' "$out" "$out" "$out"
             printf 'key down\nwait 300\nkey return\nwait 4000\npng %s/host-picture-applied.png\nquit\n' "$out"; }
run_pic() {
    local w="$out/work-pic"
    rm -rf "$w"; mkdir -p "$w/cors" "$w/profiles" "$w/stick/cores"
    cp "$kit/cors/vc20.cor" "$w/stick/cores/"   # the entry the home screen shows: A on the .cor in the browser adds it to the boot menu
    pic_flow > "$w/flow.ctl"
    cp "$kit/flash.bin" "$out/pic-flash.bin"
    ( cd "$w" && nice -n 15 timeout 300 "$UE2EMU" run --board c64u --trx-io=on --icap --cor-dir "$w/cors" --core-dir "$w/profiles" --firmware "$img/hostapp_emu.elf" \
        --roms "$kit/roms" --flash "$out/pic-flash.bin" --usb-dir "$w/stick" --usb-dir-work "$w/usbwork" --headless --speed max ${EMU_SCREEN:+--screen $EMU_SCREEN} --script "$w/flow.ctl" \
        --max-seconds 280 > "$out/pic.log" 2>&1 )
    local rc=$?
    rm -rf "$w" "$out/pic-flash.bin"
    return $rc
}
run_pic; rc=$?
shots="host-home host-f8-list host-picture-page host-picture-applied"
if [ $rc -ne 0 ]; then echo "FAIL host picture flow: the emulator exited $rc (see $out/pic.log)"; FAIL=1
else
    bad=""
    for n in $shots; do [ -s "$out/$n.png" ] && [ "$(wc -c < "$out/$n.png")" -gt 5000 ] || bad="$bad $n(missing)"; done
    # each step must change the screen: the list is not the home, the page is not the list, the applied picture is not the page
    prev=""; for n in $shots; do [ -z "$prev" ] || ! cmp -s "$out/$prev.png" "$out/$n.png" || bad="$bad $n(same as $prev)"; prev=$n; done
    if [ -z "$bad" ]; then echo "PASS host picture flow: home, F8 list, Picture page, applied mode - four different screens (PNGs in $out)"
    else echo "FAIL host picture flow:$bad (see $out/pic.log)"; FAIL=1; fi
fi

# ---- T198 S2/S3/S4/S6/D15: the config overlay through the real host application ------------------------------------------------------------------
# The stick has two copies of the VIC-20 bundle: vc20.cor with 3 configs (Alpha, Bravo, Charlie) in cores/vc20/configs/ and vc20x.cor with 10 (Config 01..10).
# The walk: F3 on vc20.cor RIGHT (overlay, PNG), A adds both cores to the boot menu, F1 (the "N configs" badges, PNG), RIGHT (3 configs, PNG), D on Bravo
# (default), F5 (the badges and the Default config row, PNG), the 10-config overlay scrolled to its last row (PNG), then RETURN on Charlie in the vc20
# overlay starts the core with that config: the core application's console line "[core] config Charlie: <path>" is the proof the hand-over page carried the name.
cfg_nav() {
    local P="$out/cfg"
    printf 'wait 15000\nkey space\nwait 800\nkey f3\nwait 1500\nkey down\nwait 500\nkey down\nwait 500\nkey return\nwait 2000\n'
    printf 'key down\nwait 500\nkey return\nwait 2000\nkey down\nwait 400\nkey down\nwait 400\nkey down\nwait 400\n'
    printf 'key right\nwait 1500\npng %s-browse-overlay.png\nkey left\nwait 800\n' "$P"
    printf 'key a\nwait 12000\nkey down\nwait 800\nkey a\nwait 12000\n'                 # the bundle check (SHA-256) takes its time, as on the board
    printf 'key f1\nwait 2000\npng %s-menu-badges.png\nkey right\nwait 1500\npng %s-overlay-3.png\n' "$P" "$P"
    printf 'key down\nwait 400\nkey down\nwait 400\nkey d\nwait 1000\npng %s-overlay-3-default.png\nkey left\nwait 800\n' "$P"
    printf 'key f5\nwait 1500\npng %s-edit-badges.png\nkey f1\nwait 1500\n' "$P"
    printf 'key down\nwait 800\nkey right\nwait 1500\npng %s-overlay-10-first.png\n' "$P"
    for ((i = 0; i < 11; i++)); do printf 'key down\nwait 300\n'; done
    printf 'png %s-overlay-10-last.png\nkey left\nwait 800\nkey up\nwait 800\n' "$P"
    printf 'key right\nwait 1500\nkey down\nwait 400\nkey return\nwait 30000\nquit\n'    # Bravo is the default and the cursor's start; DOWN = Charlie
}
run_cfg_flow() {
    local w="$out/work-cfg"
    rm -rf "$w"; mkdir -p "$w/cors" "$w/profiles" "$w/stick/cores"
    python3 "$ROOT/tools/cor_pack.py" replace-app "$kit/cors/vc20.cor" --app "$img/coreapp.app" -o "$w/cors/vc20.cor" > /dev/null || return 1
    cp "$w/cors/vc20.cor" "$w/stick/cores/"; cp "$w/cors/vc20.cor" "$w/stick/cores/vc20x.cor"
    cp "$kit/profiles/VIC-20.toml" "$w/profiles/"; cp "$img/coreapp.elf" "$w/profiles/"
    local d n
    for d in vc20 vc20x; do
        mkdir -p "$w/stick/cores/$d/roms" "$w/stick/cores/$d/configs"
        head -c 8192 /dev/zero > "$w/stick/cores/$d/roms/basic.bin"; head -c 8192 /dev/zero > "$w/stick/cores/$d/roms/kernal.bin"; head -c 4096 /dev/zero > "$w/stick/cores/$d/roms/char.bin"
    done
    for n in Alpha Bravo Charlie; do printf '# T198 emulator flow\n' > "$w/stick/cores/vc20/configs/$n.cfg"; done
    for n in 01 02 03 04 05 06 07 08 09 10; do printf '# T198 emulator flow\n' > "$w/stick/cores/vc20x/configs/Config $n.cfg"; done
    cfg_nav > "$w/flow.ctl"
    cp "$kit/flash.bin" "$out/cfg-flash.bin"
    ( cd "$w" && nice -n 15 timeout 420 "$UE2EMU" run --board c64u --trx-io=on --icap --cor-dir "$w/cors" --core-dir "$w/profiles" --firmware "$img/hostapp_emu.elf" \
        --roms "$kit/roms" --flash "$out/cfg-flash.bin" --usb-dir "$w/stick" --usb-dir-work "$w/usbwork" --headless --speed max --script "$w/flow.ctl" \
        --max-seconds 380 > "$out/cfg.log" 2>&1 )
    local rc=$?
    rm -rf "$w" "$out/cfg-flash.bin"
    return $rc
}
# T221: where a version is shown on the host: the boot menu list (a small and a large core, both with their SemVer), the chips (host, app), and the update ask of a
# .txu (its bundle version, and the warning when it is older than the running bundle). The stick carries vc20.cor, a large amiga1200.cor and an app-only .txu of
# an older bundle (0.1.0); A on each .cor adds it, F1 shows the list, the .txu opens the ask (nothing is written: the flow quits at the question).
semver_flow() { printf 'wait 2000\npng %s/semver-splash1.png\nwait 3000\npng %s/semver-splash2.png\nwait 10000\n' "$out" "$out"; printf 'key space\nwait 800\nkey f3\nwait 1500\nkey down\nwait 500\nkey down\nwait 500\nkey return\nwait 2000\n'
                printf 'key down\nwait 500\nkey return\nwait 2000\nkey down\nwait 500\nkey a\nwait 12000\nkey down\nwait 500\nkey down\nwait 500\nkey a\nwait 3000\npng %s/semver-dbg.png\nwait 12000\n' "$out"   # the bundle check (SHA-256) takes its time
                printf 'key f1\nwait 2000\npng %s/semver-list.png\n' "$out"
                printf 'key f3\nwait 2000\nkey up\nwait 800\npng %s/semver-update-sel.png\nkey u\nwait 8000\npng %s/semver-update.png\nquit\n' "$out" "$out"; }
run_semver() {
    local w="$out/work-semver"
    rm -rf "$w"; mkdir -p "$w/cors" "$w/profiles" "$w/stick/cores"
    cp "$kit/cors/vc20.cor" "$w/stick/cores/"
    python3 -I - "$ROOT/tools" "$w/stick/cores" <<'PY' || return 1
import struct, sys
sys.path.insert(0, sys.argv[1])
import cor_pack as C, test_cor_pack as TC, txu_pack as X
d = sys.argv[2]
open(d + "/amiga1200.cor", "wb").write(C.pack_large("amiga1200", "Amiga 1200 (AGA)", "0.3.0-dev+85acf2b0", TC.full_bit(frames=2000), b"\x13" * 4096))
app = b"\x5a" * 4096
open(d + "/update_old.txu", "wb").write(X.pack(app=struct.pack("<III", 0x30000, len(app), 0x30000) + app, version="0.1.0-dev+0123abcd", commit="0123abcd0123abc"))
PY
    semver_flow > "$w/flow.ctl"
    cp "$kit/flash.bin" "$out/semver-flash.bin"
    ( cd "$w" && nice -n 15 timeout 300 "$UE2EMU" run --board c64u --trx-io=on --icap --cor-dir "$w/cors" --core-dir "$w/profiles" --firmware "$img/hostapp_emu.elf" \
        --roms "$kit/roms" --flash "$out/semver-flash.bin" --usb-dir "$w/stick" --usb-dir-work "$w/usbwork" --headless --speed max ${EMU_SCREEN:+--screen $EMU_SCREEN} --script "$w/flow.ctl" \
        --max-seconds 280 > "$out/semver.log" 2>&1 )
    local rc=$?
    rm -rf "$w" "$out/semver-flash.bin"
    return $rc
}
run_semver; rc=$?
if [ $rc -ne 0 ]; then echo "FAIL T221 version flow: the emulator exited $rc (see $out/semver.log)"; FAIL=1
elif [ ! -s "$out/semver-list.png" ] || [ ! -s "$out/semver-update.png" ] || cmp -s "$out/semver-list.png" "$out/semver-update.png"; then
    echo "FAIL T221 version flow: the list and the update ask are not two different pictures"; FAIL=1
else echo "PASS T221 version flow: list with small and large core versions, update ask (PNGs $out/semver-*.png)"; fi

run_cfg_flow; rc=$?
if [ $rc -ne 0 ]; then echo "FAIL T198 config overlay flow: the emulator exited $rc (see $out/cfg.log)"; FAIL=1
elif ! grep -aq '^\[core\] config Charlie: /USB0/cores/vc20/configs/Charlie.cfg' "$out/cfg.log"; then
    echo "FAIL T198 config overlay flow: the core did not start with the config Charlie (no '[core] config Charlie:' line, see $out/cfg.log)"; FAIL=1
else echo "PASS T198 config overlay flow: overlay, badges, default, 10-config scroll, start with Charlie (PNGs $out/cfg-*.png)"; fi

# ---- T223: 32 boot entries, the visible rows read, the full menu's message -----------------------------------------------------------------------
# The stick has 33 copies of the VIC-20 bundle (cores/c01.cor .. c33.cor). The walk adds them one by one with A (the browser's own add, no seeded boot.cfg): the 33rd is
# refused with the message "Menu full (32/32): remove one in F5 first." (PNG), then the boot menu at its top, scrolled, on its last page, and the editor on the last page.
m32_nav() {
    local P="$out/t223" i
    printf 'wait 15000\nkey space\nwait 800\nkey f3\nwait 1500\nkey down\nwait 500\nkey down\nwait 500\nkey return\nwait 2000\n'
    printf 'key down\nwait 500\nkey return\nwait 2000\nkey down\nwait 500\n'
    for ((i = 1; i <= 33; i++)); do printf 'key a\nwait 14000\n'; [ $i = 32 ] && printf 'png %s-added-32.png\n' "$P"; printf 'key down\nwait 300\n'; done
    printf 'wait 4000\npng %s-full-message.png\nkey f1\nwait 4000\npng %s-menu-top.png\n' "$P" "$P"
    for ((i = 0; i < 14; i++)); do printf 'key down\nwait 300\n'; done
    printf 'wait 3000\npng %s-menu-scrolled.png\n' "$P"
    for ((i = 0; i < 17; i++)); do printf 'key down\nwait 300\n'; done
    printf 'wait 6000\npng %s-menu-last.png\nkey f5\nwait 2500\npng %s-editor-last.png\nquit\n' "$P" "$P"
}
run_m32_flow() {
    local w="$out/work-m32" i
    rm -rf "$w"; mkdir -p "$w/cors" "$w/profiles" "$w/stick/cores"
    for ((i = 1; i <= 33; i++)); do cp "$kit/cors/vc20.cor" "$w/stick/cores/$(printf 'c%02d' $i).cor"; done
    m32_nav > "$w/flow.ctl"
    cp "$kit/flash.bin" "$out/m32-flash.bin"
    ( cd "$w" && nice -n 15 timeout 1700 "$UE2EMU" run --board c64u --trx-io=on --icap --cor-dir "$w/cors" --core-dir "$w/profiles" --firmware "$img/hostapp_emu.elf" \
        --roms "$kit/roms" --flash "$out/m32-flash.bin" --usb-dir "$w/stick" --usb-dir-work "$w/usbwork" --headless --speed max --script "$w/flow.ctl" \
        --max-seconds 1600 > "$out/m32.log" 2>&1 )
    local rc=$?
    python3 -I - "$out/m32-flash.bin" "$ROOT/tests/updater" > "$out/m32-boot.cfg" <<'PY'
import sys
sys.path.insert(0, sys.argv[2])
from fatread import Fat
f = Fat(open(sys.argv[1], "rb").read()[0x580000:])
e = f.find("boot.cfg")
sys.stdout.write(f.read(e).decode() if e else "NO BOOT.CFG\n")
PY
    rm -rf "$w" "$out/m32-flash.bin"
    return $rc
}
run_m32_flow; rc=$?
n32=$(grep -c '^core=' "$out/m32-boot.cfg" 2>/dev/null || echo 0)
if [ $rc -ne 0 ]; then echo "FAIL T223 32 entries: the emulator exited $rc (see $out/m32.log)"; FAIL=1
elif [ "$n32" != 32 ]; then echo "FAIL T223 32 entries: boot.cfg holds $n32 core= lines, not 32 (the 33rd must be refused, see $out/m32.log)"; FAIL=1
else echo "PASS T223 32 entries: 32 added, the 33rd refused (boot.cfg 32 lines; PNGs $out/t223-*.png)"; fi

# ---- T211 D2 (Alex 11-15, S3 = a): the real Page Up / Page Down page a host list like + / -. The stick has a folder of 80 files: the browser shows its top, Page Down (T223 D7 paging keeps the selection on its window row, so Page Up lands on the top window with the selection two rows down; two UP bring it to the first row, then the panel equals the top)
# moves a page (a different panel), Page Up brings the top back (same panel; only the host panel is compared, the emulated machine behind it blinks).
page_nav() { printf 'wait 15000\nkey space\nwait 800\nkey f3\nwait 1500\nkey down\nwait 500\nkey down\nwait 500\nkey return\nwait 2000\nkey down\nwait 500\nkey return\nwait 3000\n'
             printf 'png %s/browse-top.png\nusbkey pagedown\nwait 1500\npng %s/browse-pagedown.png\nusbkey pageup\nwait 1500\nusbkey up\nwait 400\nusbkey up\nwait 800\npng %s/browse-pageup.png\n' "$out" "$out" "$out"
             printf 'usbkey pagedown\nwait 800\nusbkey pagedown\nwait 1500\npng %s/browse-pagedown2.png\nusbkey f1\nwait 1500\npng %s/browse-f1.png\nquit\n' "$out" "$out"; }
run_page() {
    local w="$out/work-page" n
    rm -rf "$w"; mkdir -p "$w/cors" "$w/profiles" "$w/stick/many"
    for n in $(seq -w 1 80); do printf 'x\n' > "$w/stick/many/file$n.txt"; done
    page_nav > "$w/flow.ctl"
    cp "$kit/flash.bin" "$out/page-flash.bin"
    ( cd "$w" && nice -n 15 timeout 300 "$UE2EMU" run --board c64u --trx-io=on --icap --cor-dir "$w/cors" --core-dir "$w/profiles" --firmware "$img/hostapp_emu.elf" \
        --roms "$kit/roms" --flash "$out/page-flash.bin" --usb-dir "$w/stick" --usb-dir-work "$w/usbwork" --usb-keyboard --headless --speed max --script "$w/flow.ctl" \
        --max-seconds 280 > "$out/page.log" 2>&1 )
    local rc=$?
    rm -rf "$w" "$out/page-flash.bin"
    return $rc
}
same() { python3 "$HERE/png_panel_same.py" "$out/$1.png" "$out/$2.png"; }
run_page; rc=$?
if [ $rc -ne 0 ]; then echo "FAIL T211 page keys: the emulator exited $rc (see $out/page.log)"; FAIL=1
else
    bad=""
    for n in browse-top browse-pagedown browse-pageup browse-pagedown2 browse-f1; do [ -s "$out/$n.png" ] || bad="$bad $n(missing)"; done
    [ -z "$bad" ] && same browse-top browse-pagedown && bad="$bad Page Down did not page"
    [ -z "$bad" ] && ! same browse-top browse-pageup && bad="$bad Page Up did not bring the top back"
    [ -z "$bad" ] && same browse-pagedown browse-pagedown2 && bad="$bad the second Page Down did not move"
    [ -z "$bad" ] && same browse-pagedown2 browse-f1 && bad="$bad F1 (control) did not change the screen"
    if [ -z "$bad" ]; then echo "PASS T211 page keys: Page Down pages the browser list, Page Up goes back, F1 still navigates (PNGs in $out)"
    else echo "FAIL T211 page keys:$bad (see $out/page.log)"; FAIL=1; fi
fi

# ---- T211 D3 (core app follows D1/D2): inside the started core, F keys navigate the core menu and the real Page Up / Page Down page its F3 browser list like +/-.
# The core menu opens with CTRL+RESTORE; the stick has a folder of 80 files. Panels are compared in the core menu's own area (the left 960 x 540 stays the menu).
core_page_nav() { nav
    printf 'hold ctrl\nwait 300\nrestore\nwait 300\nrelease ctrl\nwait 2500\nkey f1\nwait 1500\npng %s/core-f1.png\nkey f3\nwait 1500\npng %s/core-f3.png\n' "$out" "$out"
    printf 'key down\nwait 600\nkey down\nwait 600\nkey return\nwait 2500\nkey down\nwait 600\nkey down\nwait 600\nkey return\nwait 3000\npng %s/core-browse-top.png\n' "$out"
    printf 'usbkey pagedown\nwait 1500\npng %s/core-browse-pagedown.png\nusbkey pageup\nwait 1500\npng %s/core-browse-pageup.png\n' "$out" "$out"
    printf 'usbkey pagedown\nwait 800\nusbkey pagedown\nwait 1500\npng %s/core-browse-pagedown2.png\nusbkey f1\nwait 1500\npng %s/core-browse-f1.png\nquit\n' "$out" "$out"; }
run_core_page() {
    local w="$out/work-cpage" n
    rm -rf "$w"; mkdir -p "$w/cors" "$w/profiles" "$w/stick/cores/vc20/roms" "$w/stick/many"
    python3 "$ROOT/tools/cor_pack.py" replace-app "$kit/cors/vc20.cor" --app "$img/coreapp.app" -o "$w/cors/vc20.cor" > /dev/null || return 1
    cp "$w/cors/vc20.cor" "$w/stick/cores/"; cp "$kit/profiles/VIC-20.toml" "$w/profiles/"; cp "$img/coreapp.elf" "$w/profiles/"
    head -c 8192 /dev/zero > "$w/stick/cores/vc20/roms/basic.bin"; head -c 8192 /dev/zero > "$w/stick/cores/vc20/roms/kernal.bin"; head -c 4096 /dev/zero > "$w/stick/cores/vc20/roms/char.bin"
    for n in $(seq -w 1 80); do printf 'x\n' > "$w/stick/many/file$n.txt"; done
    core_page_nav > "$w/flow.ctl"
    cp "$kit/flash.bin" "$out/cpage-flash.bin"
    ( cd "$w" && nice -n 15 timeout 420 "$UE2EMU" run --board c64u --trx-io=on --icap --cor-dir "$w/cors" --core-dir "$w/profiles" --firmware "$img/hostapp_emu.elf" \
        --roms "$kit/roms" --flash "$out/cpage-flash.bin" --usb-dir "$w/stick" --usb-dir-work "$w/usbwork" --usb-keyboard --headless --speed max --script "$w/flow.ctl" \
        --max-seconds 380 > "$out/cpage.log" 2>&1 )
    local rc=$?
    rm -rf "$w" "$out/cpage-flash.bin"
    return $rc
}
run_core_page; rc=$?
if [ $rc -ne 0 ]; then echo "FAIL T211 core page keys: the emulator exited $rc (see $out/cpage.log)"; FAIL=1
else
    bad=""
    for n in core-f1 core-f3 core-browse-top core-browse-pagedown core-browse-pageup core-browse-pagedown2 core-browse-f1; do [ -s "$out/$n.png" ] || bad="$bad $n(missing)"; done
    [ -z "$bad" ] && same core-f1 core-f3 && bad="$bad F3 did not change the core menu"
    [ -z "$bad" ] && same core-browse-top core-browse-pagedown && bad="$bad Page Down did not page"
    [ -z "$bad" ] && ! same core-browse-top core-browse-pageup && bad="$bad Page Up did not bring the top back"
    [ -z "$bad" ] && same core-browse-pagedown core-browse-pagedown2 && bad="$bad the second Page Down did not move"
    [ -z "$bad" ] && same core-browse-pagedown2 core-browse-f1 && bad="$bad F1 (control) did not change the screen"
    if [ -z "$bad" ]; then echo "PASS T211 core page keys: F3 navigates, Page Down pages the core browser list, Page Up goes back, F1 navigates (PNGs in $out)"
    else echo "FAIL T211 core page keys:$bad (see $out/cpage.log)"; FAIL=1; fi
fi

[ $FAIL = 0 ] && echo "tests/hostapp/emu_host: all scenarios passed" || { echo "tests/hostapp/emu_host: FAILED"; exit 1; }
