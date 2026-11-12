#!/bin/sh
# T194 S2 (tear): tb_vicpic_tear.v - the layer never shows a frame assembled from two VIC captures. A small display mode (520 x 330 clocks) so that a run takes seconds:
# the display's frame time equal to the VIC's (the locked rows) at four starting phases, and +-10 % (a row that is not locked: the phase runs through a whole frame every ten frames), 50 Hz and 60 Hz VIC frames.
# Run in the sim container: sh cores/c128/sim/run_vicpic_tear.sh     Final line PASS or FAIL.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${OUT:-$here/build/tear}
mkdir -p "$out"; rm -f "$out"/*.rc
list=$out/jobs.txt; : > "$list"
for v in 0 1; do
  t=$([ $v = 1 ] && echo 17350000000 || echo 19950000000)         # the VIC frame in ps (63 x 312 and 65 x 263 cycles of 8 pixels, 4 vclk of 31.718 ns each)
  for p in 0 2 4 6; do echo "t${v}_ph$p $v $t 0 $((p * 520 / 8)) $((p * 330 / 8)) 4" >> "$list"; done
  echo "t${v}_fast $v $t -10 0 0 10" >> "$list"
  echo "t${v}_slow $v $t 10 0 0 10" >> "$list"
done
xargs -P "${JOBS:-6}" -L 1 sh -c 'sh "$0" "'"$out"'" "$@"' "$here/job_tear.sh" < "$list"
bad=0
while read -r n rest; do
  rc=$(cat "$out/$n.rc" 2>/dev/null || echo 2)
  if [ "$rc" = 0 ]; then echo "pass $n: $(tail -n 3 "$out/$n/sim.log" | head -1)"; else echo "FAIL tear: $n (rc $rc)"; grep -E "^FAIL" "$out/$n/sim.log" | head -3; tail -n 3 "$out/$n/iv.log" 2>/dev/null; bad=1; fi
done < "$list"
[ $bad = 0 ] || { echo "FAIL run_vicpic_tear"; exit 1; }
echo "PASS run_vicpic_tear ($(wc -l < "$list") runs)"
