#!/bin/sh
# T194 S2: the C128's pix_layer copy shows the VIC capture (format 3, palette) at Integer / Fit / Fill / 3/4, nearest (exact) and smoothed (within 1 LSB), in 1080p and
# 720p, at 50 Hz (400 x 270) and 60 Hz (400 x 240); see tb_vicshow.v. Four independent runs in parallel (each one niced vvp, about 2 minutes a frame, 10 frames).
# Run in the sim container: sh cores/c128/sim/run_vicshow.sh      Quick look: MODES="720" in the environment.
set -e
here=$(cd "$(dirname "$0")" && pwd)
out=${OUT:-$here/build/vicshow}
mkdir -p "$out"; rm -f "$out"/*.rc
python3 -I "$here/pixscale/model.py" selftest
list=$out/jobs.txt
: > "$list"
for mode in ${MODES:-1080 720}; do for v in 0 1; do echo "vs_${mode}_$([ $v = 1 ] && echo 60 || echo 50) $mode $v" >> "$list"; done; done
xargs -P "${JOBS:-4}" -L 1 sh -c 'sh "$0" "'"$out"'" "$@"' "$here/job_vicshow.sh" < "$list"
bad=0
while read -r n rest; do
  rc=$(cat "$out/$n.rc" 2>/dev/null || echo 2)
  if [ "$rc" = 0 ]; then
    echo "pass $n: $(grep -c '^ok' "$out/$n/check.log") steps"
  else
    echo "FAIL vicshow: $n (rc $rc)"; if [ "$rc" = 2 ]; then tail -n 3 "$out/$n/iv.log" "$out/$n/sim.log" 2>&1 || true; else cat "$out/$n/check.log" 2>/dev/null || true; fi; bad=1
  fi
done < "$list"
[ $bad = 0 ] || exit 1
echo "PASS vicshow ($(wc -l < "$list") runs: nearest exact, smoothing within 1 LSB)"
