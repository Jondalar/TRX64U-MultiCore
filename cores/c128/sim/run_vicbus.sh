#!/bin/sh
# Issue #19 (T140 section 16): the bytes the VIC latches from the bus while the CPU reads the MMU's configuration register $FF00 and rewrites the bitmap, the video
# matrix and the colour RAM the VIC shows (program vicmmu, tools/gen_c128_test.py: the vicblk0 multicolour bitmap picture, then that loop).
# tb_c128 +VICBUS compares every c/g/p/s byte the VIC latches against the bench's reference memory (every RAM write command) and the colour RAM; +FRAMEHASH hashes
# every frame. PASS per build: 0 wrong bytes, 0 wrong colour nibbles, more than MINCHK bytes checked, and frames FH0..FH1 have one hash (the loop rewrites the same
# values, so the picture must not move). Builds: bram (RAM in block RAM) and ddr (RAM_DDR2, what the board has).
# Run from the repository root in trx64-sim-env (Verilator, ghdl):  sh cores/c128/sim/run_vicbus.sh [modes: bram ddr]   (REBUILD=1: build again)
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
build=${C128_BUILD:-$here/build}
modes=${*:-"bram ddr"}
MINCHK=${MINCHK:-50000}; FH0=${FH0:-11}; FH1=${FH1:-14}
base="Z80 SID_REDIP NO_TURBO NO_REU VDC_WHOLE VDC64 TRX_SIM_ROMS ROM_CACHE"
rc=0
for m in $modes; do
  b="$build.vbus_$m"
  case $m in bram) defs="$base";; ddr) defs="$base RAM_DDR2";; *) echo "FAIL unknown mode $m"; exit 1;; esac
  [ "${REBUILD:-0}" = 1 ] && rm -rf "$b"
  [ -x "$b/obj/tb_c128" ] || C128_SIM_DEFS="$defs" sh "$here/build_machine.sh" "$b" || { echo "FAIL build $m"; exit 1; }
  run="$b/run_vicmmu"; rm -rf "$run"; mkdir -p "$run"; cp -a "$b/run/." "$run/"
  exp=$(python3 "$root/tools/gen_c128_test.py" vicmmu "$run") || exit 1
  (cd "$run" && "$b/obj/tb_c128" +MS=${C128_MS:-300} +EXPECT="$exp" +VICBUS +FRAMEHASH ${C128_ARGS:-}) > "$run/log" 2>&1
  grep '^vicbus|bad|' "$run/log" | head -12
  v=$(grep '^vicbus|[0-9]' "$run/log" | head -1)
  n=$(echo "$v" | cut -d'|' -f2); bad=$(echo "$v" | cut -d'|' -f3); cbad=$(echo "$v" | cut -d'|' -f4)
  nh=$(awk -F'|' -v a="$FH0" -v z="$FH1" '$1 == "fh" && $2 >= a && $2 <= z { h[$3] = 1; k++ } END { n = 0; for (x in h) n++; print (k == z - a + 1) ? n : -1 }' "$run/log")
  echo "$m: $v, distinct frame hashes in frames $FH0..$FH1: $nh; $(grep -E '^(PASS|FAIL) tb_c128' "$run/log" | head -1)"
  if [ -n "$v" ] && [ "${n:-0}" -gt "$MINCHK" ] && [ "$bad" = 0 ] && [ "$cbad" = 0 ] && [ "$nh" = 1 ] && grep -q '^PASS tb_c128' "$run/log"; then
    echo "PASS $m: $n VIC bytes checked, none wrong, one picture"
  else
    echo "FAIL $m: wrong ${bad:-?} bytes, ${cbad:-?} colour nibbles of ${n:-?}, $nh frame hashes in $FH0..$FH1"; rc=1
  fi
done
[ $rc = 0 ] && echo "PASS run_vicbus" || echo "FAIL run_vicbus"
exit $rc
