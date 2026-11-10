#!/usr/bin/env bash
# T174 D2.8: the host application's real flow in ue2emu (S44, docs/specs/S44-multicore-cores.md of the emulator): the host app starts, F3 > USB0 > cores >
# vc20.cor > S starts a DFX core (the partial is found by its SHA-256 in the emulator's --cor-dir), the core application identifies VC20 and takes the
# machine, CTRL+RESTORE opens its menu, F8 > "Return to host menu" brings the host application back (IPROG WBSTAR 0, the menu marker).
#
#   tests/hostapp/emu_host.sh <images dir> <out dir>
#
# <images dir>: hostapp_emu.elf (ci/trxapps.sh: the host app built with HOSTAPP_EMU_FLASH) and coreapp.elf. <out dir>: the run's log, flash image and PNGs
# (host-start.png, core-menu.png, host-back.png). The kit (ROMs, flash.bin, profile, vc20.cor) is found as ci/lib.sh ue2emu_kit says (tools/ue2emu_kit.sh);
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
FAIL=0

# nav: past the countdown, F3, USB0, into cores (the list is .., vc20/, vc20.cor), the .cor, S = start; back: the hotkey, RESTORE, F8 System, the last entry
nav() { printf 'wait 15000\npng %s/host-start.png\nkey space\nwait 800\nkey f3\nwait 1500\nkey down\nwait 500\nkey down\nwait 500\nkey return\nwait 2000\n' "$out"
        printf 'key down\nwait 500\nkey return\nwait 2000\nkey down\nwait 500\nkey down\nwait 500\nkey s\nwait 30000\n'; }
back() { printf 'hold ctrl\nwait 300\nrestore\nwait 300\nrelease ctrl\nwait 2000\npng %s/core-menu.png\nkey f8\nwait 1500\n' "$out"
         for ((i = 0; i < 15; i++)); do printf 'key down\nwait 300\n'; done
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

[ $FAIL = 0 ] && echo "tests/hostapp/emu_host: all scenarios passed" || { echo "tests/hostapp/emu_host: FAILED"; exit 1; }
