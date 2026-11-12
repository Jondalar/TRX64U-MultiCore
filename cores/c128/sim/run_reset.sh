#!/bin/sh
# T113 5.6: a machine reset must not touch the VIC (D16 of the VC-20 core: the VIC has no reset pin; a reset that moved its raster moved the picture against
# the host scaler's one lock). The reset is requested in the middle of a frame and held for 2 ms (63 raster lines; the application's is 50 ms). Checks:
#   1  every VIC frame is 628,992 clocks, the one with the reset in it too (no ROM cache in this build: nothing else lengthens a frame),
#   2  the arbiter keeps serving the VIC's fetches while the reset is held: clocks in which a VIC half-cycle selects RAM, colour RAM or the character ROM
#      ("resetwin|clocks|selects" of tb_c128.v) - the VIC reads its screen through that path, a stopped path is a picture of stale bytes.
# Run from the repository root in trx64-sim-env.  sh cores/c128/sim/run_reset.sh [reset]
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
build=${C128_BUILD:-$here/build}
modes=${*:-"reset"}
base="Z80 SID_REDIP NO_TURBO NO_REU VDC_WHOLE VDC64 TRX_SIM_ROMS"
rc=0
for m in $modes; do
  case $m in
    reset)      defs="$base" ;;
    *) echo "unknown mode $m"; exit 2 ;;
  esac
  b="$build.reset_$m"
  [ -x "$b/obj/tb_c128" ] || C128_SIM_DEFS="$defs" sh "$here/build_machine.sh" "$b" || { echo "FAIL $m: build"; rc=1; continue; }
  run="$b/run_boot"; rm -rf "$run"; mkdir -p "$run"; cp -a "$b/run/." "$run/"
  exp=$(python3 "$root/tools/gen_c128_test.py" boot "$run") || { echo "FAIL $m: generator"; rc=1; continue; }
  (cd "$run" && "$b/obj/tb_c128" +MS=180 +EXPECT="$exp" +RESET_AT=70000 +RESET_US=2000 +FRAMES) > "$run/log" 2>&1
  grep -E "^(evt\||resetwin\|)" "$run/log" | cat
  # frames from the third on (the reset comes in the fourth): every one exactly one frame long
  fr=$(grep '^frame|' "$run/log" | awk -F'|' 'NR > 2 { n++; if ($3 + 0 != 628992) { bad++; if (bad < 4) printf "  frame %s has %s clocks\n", $2, $3 > "/dev/stderr" } } END { printf "%d %d", n, bad }')
  set -- $fr
  nfr=${1:-0}; bad=${2:-0}
  sel=$(grep '^resetwin|' "$run/log" | cut -d'|' -f3)
  clk=$(grep '^resetwin|' "$run/log" | cut -d'|' -f2)
  rf=$(grep '^frame|' "$run/log" | awk -F'|' '$4 == 1 { printf "%s", $3; exit }')
  c1=PASS; [ "$nfr" -ge 2 ] && [ "$bad" -eq 0 ] || c1=FAIL
  c2=PASS; [ "${sel:-0}" -ge 1000 ] || c2=FAIL
  echo "$m: $nfr frames after the second, $bad of them not 628,992 clocks (check 1 $c1; the frame with the reset in it: ${rf:-none} clocks); during the reset ${clk:-0} clocks, ${sel:-0} VIC fetch selects (check 2 $c2)"
  if [ "$c1" = PASS ] && [ "$c2" = PASS ]; then echo "PASS $m"; else echo "FAIL $m"; rc=1; fi
done
[ $rc = 0 ] && echo "PASS run_reset" || echo "FAIL run_reset"
exit $rc
