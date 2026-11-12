#!/bin/sh
# T113 5.6: the Z80 and the VIC's bus cycles. The board found that a machine reset (the application's Reset, the rocker, F8 "Restart core") after
# the first run starts the C64 mode, never the C128's: the VIC is not reset by a machine reset (the 6561/VIC-II have no reset pin; D16 of the VC-20),
# so its display is on when the Z80 BIOS starts, and the Z80 - which, unlike the 8502 (RDY = BA), took no notice of BA - read the VIC's screen bytes
# in the badline cycles as code and port data ($DC01 = $00: "C= pressed" -> C64 mode).
# Program z80ba (tools/gen_c128_test.py): at each start the Z80 adds up 512 bytes of its BIOS window; the 8502 switches the display on and idles; the
# bench resets the machine in the idle. Both sums (starts 1 and 2) must be those of the ROM, and the Z80 must have started twice.
# Run from the repository root in trx64-sim-env.  sh cores/c128/sim/run_z80ba.sh [z80ba]
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
build=${C128_BUILD:-$here/build}
modes=${*:-"z80ba"}
base="Z80 SID_REDIP NO_TURBO NO_REU VDC_WHOLE VDC64 TRX_SIM_ROMS"
rc=0
for m in $modes; do
  case $m in
    z80ba)      defs="$base" ;;
    *) echo "unknown mode $m"; exit 2 ;;
  esac
  b="$build.z80ba_$m"
  [ -x "$b/obj/tb_c128" ] || C128_SIM_DEFS="$defs" sh "$here/build_machine.sh" "$b" || { echo "FAIL $m: build"; rc=1; continue; }
  run="$b/run_z80ba"; rm -rf "$run"; mkdir -p "$run"; cp -a "$b/run/." "$run/"
  exp=$(python3 "$root/tools/gen_c128_test.py" z80ba "$run") || { echo "FAIL $m: generator"; rc=1; continue; }
  (cd "$run" && "$b/obj/tb_c128" +MS="${C128_MS:-150}" +EXPECT="$exp" +RESET_AT=60000) > "$run/log" 2>&1
  grep -E "^(cycles|evt\||ok |FAIL)" "$run/log"
  if grep -q "^PASS tb_c128" "$run/log"; then v=PASS; else v=FAIL; fi
  echo "$v $m"; [ "$v" = PASS ] || rc=1
done
[ $rc = 0 ] && echo "PASS run_z80ba" || echo "FAIL run_z80ba"
exit $rc
