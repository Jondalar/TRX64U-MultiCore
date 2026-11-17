#!/usr/bin/env bash
# T104.2: the generic application on the TRX64 Ultimate bitstream's register map, run in ue2emu (the TRX64-Ultimate emulator, release 0.6.0,
# `--trx-io on --caps 25E40202`, the capability word of release main-b151154b) - the proof "built" of T104.2 before any board step.
# Every scenario is a control script of ue2emu (docs/specs/S08) with an expectation that makes the script fail; every scenario also runs
# once with a WRONG expectation and must fail (the negative control: an expectation that cannot fail proves nothing).
#
#   tests/coreapp/emu/run.sh [coreapp.app]
#
# The emulator is ci/ue2emu.sh (T174 D2.8): the local macOS binary when there is one, else the Linux image UE2EMU_IMAGE (ci/lib.sh) through docker with the
# sources, the work dir and the ROMs mounted; UE2EMU=<path> replaces it. No ROM is in the repository: C64_ROMS names a directory (kernal.901227-03.bin,
# basic.901226-01.bin, characters.901225-01.bin, 1541.bin) - default: roms/ of the emulator kit (ci/lib.sh ue2emu_kit, tools/ue2emu_kit.sh), else
# ~/Development/u64-emulator/firmware/1541ultimate/roms. One emulator at a time, nice 15, timeout 170 s per run. Exit 77 = no emulator or no ROMs here (skipped out loud).
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
source "$ROOT/ci/lib.sh"
UE2EMU="${UE2EMU:-$ROOT/ci/ue2emu.sh}"
if [ -z "${C64_ROMS:-}" ]; then
    if kit="$(ue2emu_kit)"; then C64_ROMS="$kit/roms"; else C64_ROMS="$HOME/Development/u64-emulator/firmware/1541ultimate/roms"; fi
fi
[ -f "$C64_ROMS/1541.bin" ] || { echo "coreapp_emu SKIPPED out loud: no ROMs ($C64_ROMS/1541.bin), see tools/ue2emu_kit.sh" >&2; exit 77; }
"$UE2EMU" run --help >/dev/null 2>&1 || { echo "coreapp_emu SKIPPED out loud: no emulator ($UE2EMU)" >&2; exit 77; }
APP="${1:-$ROOT/host-app/target/u64ii/riscv/coreapp/result/coreapp.app}"
[ -f "$APP" ] || { echo "coreapp_emu SKIPPED out loud: no coreapp.app ($APP)" >&2; exit 77; }
W="$(mktemp -d)"; trap 'rm -rf "$W"' EXIT
STICK="$W/stick"; mkdir -p "$STICK"
python3 -I "$HERE/media.py" "$STICK" "$C64_ROMS/1541.bin" "$W/big.ref" || exit 1
FAIL=0
emu() {   # emu NAME EXPECT_FAIL(0|1) -- script on stdin; flash image kept when KEEP=1
    local name=$1 want=$2
    cat > "$W/$name.txt"
    [ -n "${KEEP:-}" ] || rm -f "$W/flash.bin"
    (cd "$W" && nice -n 15 timeout 170 "$UE2EMU" run --board c64u --caps 25E40202 --trx-io on ${EMU_EXTRA:-} --headless --speed max --firmware "$APP" \
        --roms "$C64_ROMS" --flash "$W/flash.bin" --c64-roms "$C64_ROMS" --usb-dir "$STICK" --usb-dir-work "$W/usbwork" \
        --script "$W/$name.txt" --max-seconds 150 > "$W/$name.log" 2>&1)
    local rc=$?
    if [ "$want" = 1 ]; then
        if [ $rc -ne 0 ]; then echo "  control $name: fails as it must"; else echo "FAIL: control $name passed"; FAIL=1; fi
    else
        if [ $rc -eq 0 ]; then echo "PASS $name"; else echo "FAIL $name (rc $rc)"; tail -5 "$W/$name.log"; FAIL=1; fi
    fi
}
BOOT='expect-console "released" 60000
wait 2000'
OPEN='rocker up
wait 1000'
# into /USB0 (the stick: ../ roms/ big.prg low.prg t1042.d64 test.prg), then N downs and Return
browse() { printf 'key f3\nwait 800\nkey down\nkey down\nkey return\nwait 1200\n'; for ((i=0;i<$1;i++)); do echo key down; done; echo key return; echo wait 1000; }

[ "${ONLY:-}" = crt ] || {   # ONLY=crt runs just the "Run Cartridge" scenarios at the end
# S1, S2, S3, S6, S7 (boot): board profile, descriptor, ROMs from /flash/roms, released, READY on the C64's own picture
for want in 0 1; do
  txt='BASIC: /flash/roms/basic.bin'; [ $want = 1 ] && txt='BASIC: /nonexistent'
  emu "boot$want" $want <<E2
expect-console "board: monolith (ICAP absent, no hand-over)" 60000
expect-console "TRX2 TRX64U, cor (no hand-over)" 5000
expect-console "$txt" 5000
expect-console "TRX64U released" 5000
wait 2500
expect-c64 "READY." 3000
quit
E2
done

# S8: Run PRG of a BASIC file; the program prints and the C64 shows it
for want in 0 1; do
  txt='T1042 OK'; [ $want = 1 ] && txt='T9999 NO'
  emu "runprg$want" $want <<E2
$BOOT
$OPEN
$(browse 5)
key return
wait 800
key runstop
wait 4000
expect-c64 "$txt" 4000
quit
E2
done

# S9: the menu holds the machine ($A0-$A2, the jiffy clock, stands while it is open), Close lets it run
emu freeze0 0 <<E2
$BOOT
$OPEN
$(browse 5)
key return
wait 800
key runstop
wait 4000
monitor m ram 00a0 00a2
wait 500
monitor m ram 00a0 00a2
rocker up
wait 1500
monitor m ram 00a0 00a2
wait 2500
monitor m ram 00a0 00a2
key runstop
wait 1500
monitor m ram 00a0 00a2
wait 1500
monitor m ram 00a0 00a2
quit
E2
awk '/^>R:00a0/{print $2 $3 $4}' "$W/freeze0.log" > "$W/jiffies"
jf() { sed -n "$1p" "$W/jiffies"; }
# running (1 != 2), frozen under the menu (3 == 4), running again (5 != 6); the control swaps the relation of the middle pair: it must fail
check_freeze() { [ $(( $(wc -l < "$W/jiffies") )) = 6 ] && [ "$(jf 1)" != "$(jf 2)" ] && [ "$(jf 3)" "$1" "$(jf 4)" ] && [ "$(jf 5)" != "$(jf 6)" ]; }
if check_freeze = ; then echo "PASS freeze: jiffies $(tr '\n' ' ' < "$W/jiffies")"; else echo "FAIL freeze: $(tr '\n' ' ' < "$W/jiffies")"; FAIL=1; fi
if check_freeze != ; then echo "FAIL control freeze passed"; FAIL=1; else echo "  control freeze: fails as it must"; fi

# S15: a PRG $0801-$FFF7: the bytes under BASIC, the I/O area and the KERNAL land in RAM (bank ram of the monitor)
emu big0 0 <<E2
$BOOT
$OPEN
$(browse 2)
key return
wait 800
key runstop
wait 8000
monitor m ram a000 a00f
monitor m ram d000 d00f
monitor m ram e000 e00f
monitor m ram ffe0 ffff
quit
E2
python3 -I - "$W/big.ref" "$W/big0.log" <<'PY' || FAIL=1
import sys, re
ref = open(sys.argv[1], 'rb').read()
got = {}
for l in open(sys.argv[2], errors='replace'):
    m = re.match(r'>R:([0-9a-f]{4})  ((?:[0-9a-f]{2} )+)', l)
    if m: got[int(m.group(1), 16)] = bytes(int(x, 16) for x in m.group(2).split())
bad = 0
for a, b in got.items():
    for i, v in enumerate(b):
        addr = a + i
        if 0x0801 <= addr <= 0xFFF7 and v != ref[addr - 0x0801]:
            bad += 1; print('  differs at $%04X: %02x, file %02x' % (addr, v, ref[addr - 0x0801]))
need = {0xA000, 0xD000, 0xE000, 0xFFE0}
if need - set(got) or bad: print('FAIL big.prg: windows %s, %d byte(s) differ' % (sorted(map(hex, got)), bad)); sys.exit(1)
print('PASS big.prg: $A000, $D000, $E000 and up to $FFF7 equal the file (%d windows, %d bytes)' % (len(got), sum(map(len, got.values()))))
PY

# S16: Load PRG and start from $1000: the program at $1000 runs (PC = $100B, $2000 = $42, border), a file that covers $0300-$0FFF
for want in 0 1; do
  sa='1000'; [ $want = 1 ] && sa='2000'
  emu "low$want" $want <<E2
$BOOT
$OPEN
$(browse 3)
key down
key return
wait 1000
key del
key del
key del
key del
type 1000
key return
wait 1500
key runstop
wait 3000
monitor r
monitor m ram 2000 2003
expect-console "start \$$sa (CPU address set)" 3000
quit
E2
done
if grep -q '^\.;100b' "$W/low0.log" && grep -q '^>R:2000  42' "$W/low0.log"; then echo "PASS low.prg: PC=\$100B, \$2000=\$42"; else echo "FAIL low.prg: not at \$100B"; grep -E '^\.;|^>R' "$W/low0.log"; FAIL=1; fi

# S10 reset: Home (F1), its first row Reset asks "Reset the machine?" (RETURN confirms) and brings READY back while a program runs
for want in 0 1; do
  txt='READY.'; [ $want = 1 ] && txt='T9999 NO'
  emu "reset$want" $want <<E2
$BOOT
$OPEN
$(browse 5)
key return
wait 800
key runstop
wait 3000
rocker up
wait 1000
key f1
wait 500
key return
wait 800
key return
wait 4000
expect-c64 "$txt" 3000
quit
E2
done

# S11: a C64 setting survives the restart (the file is in the emulated flash drive)
emu set0 0 <<E2
$BOOT
$OPEN
key f5
wait 500
key 2
wait 800
key return
wait 800
quit
E2
if grep -aq '^Freeze mode=CPU only' "$W/flash.bin"; then echo "PASS settings: /flash/trx64u/c64.cfg holds Freeze mode=CPU only"; else echo "FAIL settings: the flash image has no Freeze mode=CPU only"; FAIL=1; fi
if grep -aq '^Freeze mode=Whole machine' "$W/flash.bin"; then echo "FAIL control settings: the unchanged value is in the file"; FAIL=1; else echo "  control settings: the default is not in the file, as it must"; fi
# the second start reads it back: the menu shows the stored value (expect on the Settings tab is a picture; the console line names the file)
KEEP=1 emu set1 0 <<E2
expect-console "settings C64 Settings: /flash/trx64u/c64.cfg" 60000
quit
E2

# T228 DC13: the same start under the MultiCore host (--icap: the ICAP loader answers; there is no hand-over, so the core stays held, and the profile says where its settings
# would live): the parameter flash_cfg is off, so the file path is empty (settings in memory only) and the flash image gets no cfg (D11). The control expects the flash path
# in the console and must fail. A running core under that host always has a .cor and its cfg lies in the core folder; that part is test_board's (start_paths).
for want in 0 1; do
  txt='(no .cfg path, in memory only)'; [ $want = 1 ] && txt='/flash/trx64u/c64.cfg'
  EMU_EXTRA=--icap emu "setmc$want" $want <<E2
expect-console "board: multicore" 60000
expect-console "settings C64 Settings: $txt" 60000
quit
E2
  if [ $want = 0 ]; then
    if grep -aq 'c64.cfg' "$W/flash.bin" 2>/dev/null; then echo "FAIL settings (MultiCore): the flash image holds a cfg"; FAIL=1; else echo "PASS settings (MultiCore): settings in memory only, no cfg in the flash image"; fi
  fi
done

# T104.2 D10 / T228: the drive settings are in F5 tab 1 (Machine), section Drive A; the row "Drive enable" is the fifth selectable row (headings are skipped): one RETURN switches it off and the file says so; the control expects the default
emu drivemenu0 0 <<E2
$BOOT
$OPEN
key f5
wait 500
key 1
wait 800
key down
key down
key down
key down
key return
wait 800
quit
E2
if grep -aq '^Drive=Disabled' "$W/flash.bin"; then echo "PASS drive settings: Drive=Disabled after RETURN on 'Drive enable' (F5 tab 1)"; else echo "FAIL drive settings: no Drive=Disabled in the file"; FAIL=1; fi
if grep -aq '^Drive=Enabled' "$W/flash.bin"; then echo "FAIL control drive settings: the default is in the file"; FAIL=1; else echo "  control drive settings: the default is not in the file, as it must"; fi

# D9.1 cbm1541: the 1541 ROM is chosen from the stick (a stick is not up at the ROM scan, see spec 5), a .d64 is mounted, the drive answers
# (RETURN on a file opens its sheet, the first row sets the ROM and the Library stays in roms/, so 'up, RETURN' on ../ goes back to the stick; Mount Disk leaves the menu open)
for want in 0 1; do
  nm='hello'; [ $want = 1 ] && nm='nothere'
  emu "drive$want" $want <<E2
$BOOT
$OPEN
key f3
wait 800
key down
key down
key return
wait 1200
key down
key return
wait 1200
key down
key down
key return
wait 1000
key up
key up
key up
key return
wait 1000
key up
key return
wait 1000
key down
key down
key down
key down
key return
wait 800
key return
wait 2500
key runstop
wait 1500
type load"$nm",8
key return
wait 12000
expect-c64 "LOADING" 3000
type run
key return
wait 3000
expect-c64 "T1042 OK" 3000
quit
E2
done
}

# "Run Cartridge" (c64.run_crt): a .crt picked in the Library is placed, the machine is reset into it. The four cartridges are crtmedia.py's: a normal
# 8 KB and 16 KB one print a line, the Ocean one switches to bank 1 through $DE00 and prints a second line, the Ultimax one stores $42 in RAM $0800.
# Each runs once with a wrong expectation and must fail (the control). ue2emu serves the cartridge from Gideon's layout (image at 0x03C00000, no
# ROM_SEL): the bitstream's ROM_SEL and descriptor path is the board step.
CRTSTICK="$W/crtstick"; python3 -I "$HERE/crtmedia.py" "$CRTSTICK" || exit 1
STICK_SAVED="$STICK"; STICK="$CRTSTICK"
for want in 0 1; do
  a='CART8 OK'; b='CART16 OK'; c='OCEAN B1'; [ $want = 1 ] && { a='CART9 OK'; b='CART17 OK'; c='OCEAN B2'; }
  emu "crt8_$want" $want <<E2
$BOOT
$OPEN
$(browse 2)
key return
wait 4000
expect-c64 "$a" 4000
quit
E2
  emu "crt16_$want" $want <<E2
$BOOT
$OPEN
$(browse 1)
key return
wait 4000
expect-c64 "$b" 4000
quit
E2
  emu "ocean_$want" $want <<E2
$BOOT
$OPEN
$(browse 3)
key return
wait 4000
expect-c64 "OCEAN B0" 4000
expect-c64 "$c" 4000
quit
E2
done
emu umax0 0 <<E2
$BOOT
$OPEN
$(browse 4)
key return
wait 4000
monitor m ram 0800 0800
wait 500
quit
E2
if grep -q '>R:0800  42' "$W/umax0.log"; then echo "PASS umax: \$0800 = \$42 (the Ultimax ROM at \$E000 ran)"; else echo "FAIL umax: $(grep '>R:0800' "$W/umax0.log")"; FAIL=1; fi
if grep -q '>R:0800  43' "$W/umax0.log"; then echo "FAIL control umax passed"; FAIL=1; else echo "  control umax: fails as it must"; fi
STICK="$STICK_SAVED"
[ $FAIL = 0 ] && echo "tests/coreapp/emu: all scenarios passed, every control failed as it must" || { echo "tests/coreapp/emu: FAILED"; exit 1; }
