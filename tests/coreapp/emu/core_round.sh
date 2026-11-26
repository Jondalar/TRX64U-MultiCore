#!/usr/bin/env bash
# Step 12 of a core lane (T239.5 D18): the core's own app alone in ue2emu - no host app, no host menu, no IPROG. The .cor's app boots as the firmware
# (tools/ue2emu_core.py firmware: the bytes of the .cor's app record in the app's ELF, so the emulator has the link's symbols) on the machine of the core's
# profile cores/<core>/app/ue2emu.toml (--core: the ID word, the registers, the hand-over page). The .cor lies on the USB stick at the hand-over's path, the
# zero-filled stand-ins of the ROM files the core needs in its core folder (tools/ue2emu_core.py roms). The round proves from the console, in this order:
#   1. the app identifies the core: its line "phase core identified: [core] <ID word> <name>," (the profile's ID word, desc.toml's name)
#   2. the core starts: "phase boot_start returned (running)", or "(waiting for a program)" for a core whose desc.toml [core] has needs_program
#   3. the menu opens ("[core] menu open"): by the menu hotkey (desc.toml [hotkeys] mod held with RESTORE), by the menu button (`rocker up`) for a core
#      without one; a core that waits for a program and a menu_first core open it by themselves
#   4. the same key closes it again ("[core] menu closed, machine running"). The round stops after 3 for a menu_first core (nothing is behind its menu) and
#      for a core that waits for a program: its menu cannot close before a program runs (MenuCtl::closable) and its hotkeys are off until the core has
#      started (HkLoop::poll); a program to run is a file of the core's own kind, which a round that names no core does not make.
# 2b. a core whose app waits on its ROM page (D20: the files it checks are not the stand-ins) ends there: identified, then the app's ROM page line
#     "[coreapp] ROM files:" (every core's, ui.cc), picture core-rompage.png; no core is named here
# Pictures in <out dir>: core-start.png (a running core before the menu), core-menu.png (the open menu), core-back.png (after the close); the menu picture
# must differ from the one after the close. The emulator's console is <out dir>/round.log.
#
#   tests/coreapp/emu/core_round.sh <core> <core.cor> <core.elf> <out dir>
#     <core.cor>, <core.elf>: the core lane's .cor and the ELF of the link of its app (step 13 delivers both)
# env: UE2EMU_ROMS (the emulator's ROM folder, Gideon's roms/: ci/emu_kit.sh KIT makes KIT/roms), else the kit of ci/lib.sh ue2emu_kit;
#      UE2EMU (default ci/ue2emu.sh)
# Prints one PASS or FAIL line; a FAIL names the first step that is missing and shows the end of the console. Exit 0 = PASS, 1 = FAIL, 77 = nothing to run
# with (no emulator or no ROM folder: said out loud).
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
source "$ROOT/ci/lib.sh"
usage="usage: core_round.sh <core> <core.cor> <core.elf> <out dir>"
core="${1:?$usage}"; cor="${2:?$usage}"; elf="${3:?$usage}"; out="${4:?$usage}"
UE2EMU="${UE2EMU:-$ROOT/ci/ue2emu.sh}"
skip() { echo "core_round SKIPPED out loud: $*" >&2; exit 77; }
fail() { echo "FAIL $core alone: $*"; exit 1; }
[ -f "$cor" ] && [ -f "$elf" ] || fail "no $cor or no $elf (the core lane delivers both)"
emu_roms="${UE2EMU_ROMS:-}"
if [ -z "$emu_roms" ]; then
    kit="$(ue2emu_kit)" || skip "no ROM folder for the emulator (UE2EMU_ROMS, or a kit: ci/emu_kit.sh)"
    emu_roms="$kit/roms"
fi
[ -d "$emu_roms" ] || skip "no ROM folder $emu_roms"
"$UE2EMU" run --help >/dev/null 2>&1 || skip "no emulator ($UE2EMU)"
# prof, ident, hold, start (running | program | menu_first), stick_cor, rom_dir
facts="$(python3 -I "$ROOT/tools/ue2emu_core.py" direct "$core" "$cor")" || fail "no facts of the round (the reason above)"
eval "$facts"
case "$start" in running|program|menu_first) ;; *) fail "unknown start \"$start\" (tools/ue2emu_core.py direct)" ;; esac
mkdir -p "$out"; out="$(cd "$out" && pwd -P)"
cor="$(cd "$(dirname "$cor")" && pwd -P)/$(basename "$cor")"; elf="$(cd "$(dirname "$elf")" && pwd -P)/$(basename "$elf")"
w="$out/work"; rm -rf "$w" "$out"/core-*.png "$out/rompage.log"; mkdir -p "$w/stick/$rom_dir"
cp "$cor" "$w/stick/$stick_cor"
python3 -I "$ROOT/tools/ue2emu_core.py" roms "$core" "$w/stick/$rom_dir" > "$out/stand-ins.txt" || fail "no ROM stand-ins (the reason above)"
python3 -I "$ROOT/tools/ue2emu_core.py" firmware "$cor" "$elf" "$w/app.elf" > "$out/app.txt" || fail "the ELF is not the link of the .cor's app (the reason above)"

q() { local s="${1//\\/\\\\}"; printf '"%s"' "${s//\"/\\\"}"; }   # a text of the control language: double quotes, \" and \\ escaped
menu_key() { if [ -n "$hold" ]; then printf 'hold %s\nwait 300\nrestore\nwait 300\nrelease %s\n' "$hold" "$hold"; else printf 'rocker up\n'; fi; }
if [ -n "$hold" ]; then how="the hotkey $hold+RESTORE"; else how="the menu button"; fi
boot="running"; [ "$start" != program ] || boot="waiting for a program"
# the steps the console must show, in order, and what a missing one means
says=("phase core identified: [core] $ident," "phase boot_start returned ($boot)" "[core] menu open")
whys=("the core app did not identify ${ident%% *}" "the core did not start ($boot)" "the menu did not open")
case "$start" in
    running) says+=("[core] menu closed"); whys+=("$how did not close the menu") ;;
esac
{
    printf 'expect-console %s 60000\n' "$(q "${says[0]}")"
    printf 'expect-console %s 120000\n' "$(q "${says[1]}")"
    case "$start" in
        running)
            printf 'wait 3000\npng %s/core-start.png\n' "$out"; menu_key
            printf 'expect-console %s 30000\nwait 2000\npng %s/core-menu.png\n' "$(q "[core] menu open")" "$out"; menu_key
            printf 'expect-console %s 30000\nwait 2000\npng %s/core-back.png\n' "$(q "[core] menu closed")" "$out" ;;
        program|menu_first)
            printf 'expect-console %s 60000\nwait 2000\npng %s/core-menu.png\n' "$(q "[core] menu open")" "$out" ;;
    esac
    printf 'quit\n'
} > "$w/round.ctl" || exit 1
# the flash image beside the pictures: ci/ue2emu.sh's container fallback mounts the directories of the files it is given, and the pictures go to $out
emu_run() {   # <control file> <console log>
    ( cd "$w" && nice -n 15 timeout 480 "$UE2EMU" run --board c64u --trx-io=on --c64 none --core "$prof" --firmware "$w/app.elf" --roms "$emu_roms" \
        --flash "$out/round-flash.bin" --usb-dir "$w/stick" --usb-dir-work "$w/usbwork" --headless --speed max --script "$1" \
        --max-seconds 420 > "$2" 2>&1 )
}
emu_run "$w/round.ctl" "$out/round.log"
rc=$?

# D20: a core whose app waits on its ROM page (the files are not the ones it accepts) ends its round there: the app prints every ROM page it shows as
# "[coreapp] ROM files: <text>" (ui.cc message), and a core that started never shows one after its identification. Proven = identified, then that line; the
# picture of the page is made by a short second run of the same boot (the first run's script ends at the step that did not come).
rompage=0
if tr -d '\000' < "$out/round.log" | grep -a -v expect-console | awk -v id="phase core identified: [core] $ident," \
    'index($0, id) { seen = 1; next } seen && index($0, "phase boot_start returned") { started = 1 } seen && index($0, "[coreapp] ROM files:") { found = 1 } END { exit !(found && !started) }'; then
    {
        printf 'expect-console %s 60000\n' "$(q "${says[0]}")"
        printf 'expect-console %s 120000\nwait 2000\npng %s/core-rompage.png\nquit\n' "$(q "[coreapp] ROM files:")" "$out"
    } > "$w/rompage.ctl"
    emu_run "$w/rompage.ctl" "$out/rompage.log"; rc2=$?
    if [ "$rc2" -eq 0 ] && [ -s "$out/core-rompage.png" ]; then rompage=1; fi
fi
rm -rf "$w" "$out/round-flash.bin"

# how many of the steps the console shows, each after the one before it (the log may hold NUL bytes of the firmware's UART: out before awk). The emulator's
# report of a control line that failed quotes the line ("1: expect-console \"[core] menu closed\": not in the console output ..."): it is no step.
found="$(tr -d '\000' < "$out/round.log" | awk 'BEGIN { n = ARGC - 1; for (i = 1; i < ARGC; i++) want[i] = ARGV[i]; ARGC = 1; k = 1 }
    index($0, "expect-console") { next } k <= n && index($0, want[k]) { k++ } END { print k - 1 }' "${says[@]}")"
red() {
    echo "FAIL $core alone: $*"
    echo "--- last 60 lines of $out/round.log"; tail -n 60 "$out/round.log" | tr -d '\000' | cut -c1-200; echo "---"
    exit 1
}
if [ "$rompage" -eq 1 ] && [ "$found" -eq 1 ]; then
    page="$(tr -d '\000' < "$out/rompage.log" | grep -a -v expect-console | grep -a -m1 -A1 '\[coreapp\] ROM files:' | cut -c1-160 | tr '\n' ' ')"
    echo "PASS $core alone: ${ident%% *} identified (${ident#* }), waits on its ROM page: ${page} (PNG $out/core-rompage.png)"
    exit 0
fi
if [ "$found" -lt "${#says[@]}" ]; then
    why="${whys[$found]}"
    case "$found" in
        0) got="$(grep -a -m1 'phase core identified' "$out/round.log" | tr -d '\000' | cut -c1-200)"; why="$why (${got:-no identification line})" ;;
        1) got="$(tr -d '\000' < "$out/round.log" | grep -a -v expect-console | grep -a -m1 -A4 -E 'phase boot_start returned|ROM files' | cut -c1-160 | tr '\n' ' ')"
           why="$why: ${got:-no line of the start}" ;;
    esac
    red "$why; the emulator exited $rc (see $out/round.log)"
fi
[ "$rc" -eq 0 ] || red "every step is on the console, but the emulator exited $rc (see $out/round.log)"
[ -s "$out/core-menu.png" ] || red "no picture of the open menu"
if [ "$start" = running ]; then
    [ -s "$out/core-back.png" ] || red "no picture after the close"
    ! cmp -s "$out/core-menu.png" "$out/core-back.png" || red "the picture of the open menu is the picture after the close"
fi
case "$start" in
    running)    echo "PASS $core alone: ${ident%% *} identified (${ident#* }), started, $how opened the menu and closed it, the machine running (PNGs in $out)" ;;
    program)    echo "PASS $core alone: ${ident%% *} identified (${ident#* }), waits for a program with its menu open (it closes once a program runs; PNGs in $out)" ;;
    menu_first) echo "PASS $core alone: ${ident%% *} identified (${ident#* }), its menu is its screen and open (PNGs in $out)" ;;
esac
exit 0
