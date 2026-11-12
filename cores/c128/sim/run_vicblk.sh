#!/bin/sh
# c128-vicglitch: the VIC reads RAM block 1 (MMU RCR $D506 bit 6, what PETSCII Robots C128 sets: $45/$41).
# Programs vicblk0 / vicblk1 (tools/gen_c128_test.py) build the same multicolour bitmap picture in RAM block 0 / block 1 and point the VIC at it, so the
# VIC's pixel stream (hash of frame 10) must be the same. Builds: bram (RAM in block RAM, the reference) and ddr (RAM_DDR2, PIN: what the board has).
# T140 D28: the ddr build has the fifth slot for RAM block 1 (c128_ramd NSLOT = 5), so block 1 must show the same picture as block 0, with a residual of $FF reads
# only while the slot is swept (+VICCHK: wrong block-1 reads and their window, "vicwin|"). Modes: bram, ddr (PASS: same hash in both blocks and in both builds,
# residual within MAXBAD reads / MAXWIN_US). The controls noearly and nodyn are gone with their RTL hooks (T174 D5.4: controls only for flash and persistence).
# Final line PASS when every mode did what it must, FAIL otherwise.
# Run from the repository root in trx64-sim-env (Verilator, ghdl):  sh cores/c128/sim/run_vicblk.sh [modes: bram ddr]
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
build=${C128_BUILD:-$here/build}
modes=${*:-"bram ddr"}
MAXBAD=${MAXBAD:-150}; MAXWIN_US=${MAXWIN_US:-150}
base="Z80 SID_REDIP NO_TURBO NO_REU VDC_WHOLE VDC64 TRX_SIM_ROMS ROM_CACHE"
rc=0
for m in $modes; do
  b="$build.vb_$m"
  case $m in bram) defs="$base";; ddr) defs="$base RAM_DDR2";; *) echo "FAIL unknown mode $m"; exit 1;; esac
  [ -x "$b/obj/tb_c128" ] || C128_SIM_DEFS="$defs" sh "$here/build_machine.sh" "$b" || { echo "FAIL build $m"; exit 1; }
  for blk in 0 1; do
    run="$b/run_vicblk$blk"; rm -rf "$run"; mkdir -p "$run"; cp -a "$b/run/." "$run/"
    exp=$(python3 "$root/tools/gen_c128_test.py" vicblk$blk "$run") || exit 1
    (cd "$run" && "$b/obj/tb_c128" +MS=${C128_MS:-260} +EXPECT="$exp" +PIXHASH=10 +PIXDUMP=10 +VICCHK ${C128_ARGS:-}) > "$run/log" 2>&1
    echo "$m block $blk: $(grep '^pixhash|' "$run/log" | head -1) $(grep '^ramd|' "$run/log" | head -1) $(grep '^vicchk|' "$run/log" | head -1) $(grep -E '^(PASS|FAIL) tb_c128' "$run/log" | head -1)"
    grep '^vicbad|' "$run/log" | head -4
  done
  h0=$(grep '^pixhash|' "$b/run_vicblk0/log" | head -1 | cut -d'|' -f3)
  h1=$(grep '^pixhash|' "$b/run_vicblk1/log" | head -1 | cut -d'|' -f3)
  bad=$(grep '^vicchk|' "$b/run_vicblk1/log" | head -1 | cut -d'|' -f5)
  win=$(grep '^vicwin|' "$b/run_vicblk1/log" | head -1 | sed 's/.*|window \([0-9.]*\) us/\1/')
  [ "$m" = bram ] && ref=$h1
  echo "$m: block 1 wrong reads ${bad:-n/a}, window ${win:-n/a} us; $(grep '^vicwin|' "$b/run_vicblk1/log" | head -1)"
  case $m in
  bram) if [ -n "$h0" ] && [ "$h0" = "$h1" ]; then echo "PASS $m: block 1 shows the same picture as block 0 ($h0)"; else echo "FAIL $m: block 0 $h0, block 1 $h1"; rc=1; fi ;;
  ddr) if [ -n "$h0" ] && [ "$h0" = "$h1" ] && [ "$h1" = "${ref:-x}" ] && [ "${bad:-99999}" -le "$MAXBAD" ] && [ "${win%.*}" -le "$MAXWIN_US" ]; then
         echo "PASS $m: block 1 = block 0 = bram ($h1), $bad wrong block-1 reads in $win us"; else echo "FAIL $m: block 0 $h0, block 1 $h1 (bram ${ref:-?}), wrong block-1 reads ${bad:-?}, window ${win:-?} us"; rc=1; fi ;;
  esac
done
exit $rc
