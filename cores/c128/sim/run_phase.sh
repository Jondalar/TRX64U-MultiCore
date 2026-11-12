#!/bin/sh
# T113 5.7: the VIC never waits for the ROM. With the ROM in the DDR2 behind the line cache (ROM_CACHE, the board's build) program romwalk reads 1,024 ROM
# lines after about six frames - every read is a miss of the cache - and the bench reads the frame starts (tb_c128.v +FRAMES: "frame|n|clocks|reset|phase").
# Checks:
#   1  every frame from the fourth on is exactly 628,992 clocks (PAL) and so is the shortest and the longest frame that the core's own meter reports
#      ("frmeter|min|max", the words c128_regs.v FR_MIN / FR_MAX give the application),
#   2  the program really took the misses: "romc|hits|misses|waits|longest fetch" - at least 1,000 fetches and as many CPU cycles held, and it finished (+EXPECT).
# Run from the repository root in trx64-sim-env (Verilator).  sh cores/c128/sim/run_phase.sh [nostall]
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
build=${C128_BUILD:-$here/build}
modes=${*:-"nostall"}
base="Z80 SID_REDIP NO_TURBO NO_REU VDC_WHOLE VDC64 TRX_SIM_ROMS ROM_CACHE"
rc=0
for m in $modes; do
  case $m in
    nostall)      defs="$base" ;;
    *) echo "unknown mode $m"; exit 2 ;;
  esac
  b="$build.phase_$m"
  [ -x "$b/obj/tb_c128" ] || C128_SIM_DEFS="$defs" sh "$here/build_machine.sh" "$b" || { echo "FAIL $m: build"; rc=1; continue; }
  run="$b/run_romwalk"; rm -rf "$run"; mkdir -p "$run"; cp -a "$b/run/." "$run/"
  exp=$(python3 "$root/tools/gen_c128_test.py" romwalk "$run") || { echo "FAIL $m: generator"; rc=1; continue; }
  (cd "$run" && "$b/obj/tb_c128" +MS="${C128_MS:-320}" +EXPECT="$exp" +FRAMES) > "$run/log" 2>&1
  prog=PASS; grep -q "^PASS tb_c128" "$run/log" || prog=FAIL
  # frames from the fourth on: how many, how many are not exactly one frame long
  fr=$(grep '^frame|' "$run/log" | awk -F'|' 'NR > 3 { n++; if ($3 + 0 != 628992) { bad++; if (!first) first = $3 } } END { printf "%d %d %d", n, bad, first }')
  set -- $fr
  nfr=${1:-0}; bad=${2:-0}; first=${3:-0}
  mm=$(grep '^frmeter|' "$run/log" | head -1)
  fmin=$(echo "$mm" | cut -d'|' -f2); fmax=$(echo "$mm" | cut -d'|' -f3)
  rm_=$(grep '^romc|' "$run/log" | head -1)
  nmiss=$(echo "$rm_" | cut -d'|' -f3); nwait=$(echo "$rm_" | cut -d'|' -f4); fmx=$(echo "$rm_" | cut -d'|' -f5)
  c1=PASS; { [ "$nfr" -ge 8 ] && [ "$bad" -eq 0 ] && [ "${fmin:-0}" -eq 628992 ] && [ "${fmax:-0}" -eq 628992 ]; } || c1=FAIL
  c2=PASS; { [ "${nmiss:-0}" -ge 1000 ] && [ "${nwait:-0}" -ge 1000 ] && [ "$prog" = PASS ]; } || c2=FAIL
  echo "$m: $nfr frames, $bad not 628,992 clocks (first: ${first:-0}), meter min ${fmin:-?} max ${fmax:-?} (check 1 $c1); ROM cache ${nmiss:-0} fetches, ${nwait:-0} CPU cycles held, longest fetch ${fmx:-?} clocks, program $prog (check 2 $c2)"
  if [ "$c1" = PASS ] && [ "$c2" = PASS ]; then echo "PASS $m"; else echo "FAIL $m"; rc=1; fi
done
[ $rc = 0 ] && echo "PASS run_phase" || echo "FAIL run_phase"
exit $rc
