#!/bin/sh
# T113 5.6: the cold start of the machine (c128_regs.v COLD -> c64_core_c128.v cold_rst), on the synthetic ROM, no ROM cache (no stalls: the frame is
# exactly 628,992 clocks). Program coldprog counts its starts in RAM ($0410, which the cold start does not clear) and stores what CIA 1 DDRA, the VIC's
# border colour and the MMU's $D506 read at each start, then sets them. The bench requests the cold start in the middle of the run and checks
#   1  the program started twice (the machine really restarted: CPU, Z80 hand-over, MMU),
#   2  the chips read the same at the second start as at the first (power-on values: they were reset),
#   4  the VIC's registers $D000-$D02E read the same at both starts (the chip has no reset for them: the core writes 0 to them after the release),
#   3  the frame phase of the VIC (frame start modulo one frame, from the reference frame 3) is the same after the cold start as before: the machine
#      restarts in the phase of the power-on, so the host scaler's one lock still fits.
# Run from the repository root in trx64-sim-env.  sh cores/c128/sim/run_cold.sh [cold]
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
build=${C128_BUILD:-$here/build}
modes=${*:-"cold"}
base="Z80 SID_REDIP NO_TURBO NO_REU VDC_WHOLE VDC64 TRX_SIM_ROMS"
TOL=${PHASE_TOL:-40}
rc=0
for m in $modes; do
  case $m in
    cold)        defs="$base" ;;
    *) echo "unknown mode $m"; exit 2 ;;
  esac
  b="$build.cold_$m"
  [ -x "$b/obj/tb_c128" ] || C128_SIM_DEFS="$defs" sh "$here/build_machine.sh" "$b" || { echo "FAIL $m: build"; rc=1; continue; }
  run="$b/run_coldprog"; rm -rf "$run"; mkdir -p "$run"; cp -a "$b/run/." "$run/"
  exp=$(python3 "$root/tools/gen_c128_test.py" coldprog "$run") || { echo "FAIL $m: generator"; rc=1; continue; }
  (cd "$run" && "$b/obj/tb_c128" +MS="${C128_MS:-300}" +EXPECT="$exp" +COLD_AT=123456 +FRAMES +DUMP=0410 +DUMPN=28) > "$run/log" 2>&1
  grep -E "^(evt\||ok |FAIL)" "$run/log"
  cnt=PASS; grep -q "^PASS tb_c128" "$run/log" || cnt=FAIL
  # the three slots 1 and 2 of $0420, $0430, $0440 are bytes 1 and 2 of the 96 byte dump's lines 2, 3, 4 (0410.., 0420.., 0430.., 0440..)
  same=PASS
  for off in 0420 0430 0440; do
    line=$(grep "^ram|$off|" "$run/log" | head -1 | cut -d'|' -f3)
    set -- $line
    [ "${2:-x}" = "${3:-y}" ] || same=FAIL
    echo "slot $off: first start ${2:-?}, second start ${3:-?}"
  done
  # the VIC's registers $D000-$D02E at the two starts (tables at $0540 and $0580), without the ones that move by themselves: the raster ($D011/$D012),
  # the light pen ($D013/$D014), the interrupt flags ($D019), the collision registers ($D01E/$D01F)
  vd=$(awk -F'|' '/^ram\|05[4-9a][0]\|/ { t[$2] = $3 }
        END { bad = 0; n = 0; split("0540 0550 0560", p1, " "); split("0580 0590 05a0", p2, " ")
              for (l = 1; l <= 3; l++) { split(t[p1[l]], x, " "); split(t[p2[l]], y, " ")
                for (i = 1; i <= 16; i++) { r = (l - 1) * 16 + i - 1; if (r > 46) continue
                  if (r == 17 || r == 18 || r == 19 || r == 20 || r == 25 || r == 30 || r == 31) continue
                  n++; if (x[i] != y[i]) bad++ } }
              printf "%d %d", n, bad }' "$run/log")
  set -- $vd
  vv=PASS; [ "${1:-0}" -gt 30 ] && [ "${2:-1}" -eq 0 ] || vv=FAIL
  echo "VIC registers read the same at both starts: ${2:-?} of ${1:-0} differ $vv"
  ph=$(grep '^frame|' "$run/log" | awk -F'|' -v tol="$TOL" 'NR > 10 { p = $5 + 0; if (p > 314496) p -= 628992; a = p < 0 ? -p : p; if (a > mx) mx = a; n++ } END { printf "%d %d", n, mx }')
  set -- $ph
  pv=PASS; [ "${1:-0}" -gt 0 ] && [ "${2:-99999}" -le "$TOL" ] || pv=FAIL
  echo "$m: starts counted $cnt, chips read the same at both starts $same, frame phase after the cold start: ${2:-?} clocks off over ${1:-0} frames (tolerance $TOL) $pv"
  v=PASS; [ "$cnt" = PASS ] && [ "$same" = PASS ] && [ "$pv" = PASS ] && [ "$vv" = PASS ] || v=FAIL
  [ "$v" = PASS ] || rc=1; echo "$v $m"
done
[ $rc = 0 ] && echo "PASS run_cold" || echo "FAIL run_cold"
exit $rc
