#!/bin/sh
# T164 WP5, scenario S7: host/pix_layer.v's scaler against model.py (Python), in the modes 576p (gear 5), 720p (gear 2) and 1080p (gear 1)
# for a 320 x 256 and a 704 x 200 source: Integer / Fit / Fill / 3/4 of the source, nearest and smoothed.
#   1. the geometry function against the application's table (model.py selftest);
#   2. SMOOTH = 1 (the host's default), format 1, all six: nearest steps exactly, smoothed steps within one LSB, no late line, no pixel
#      wrong outside the layer; format 2 (576p, both sources, Fit nearest and smoothed);
#   3. SMOOTH = 0: the nearest steps in 576p, format 1 (all four sizes) and format 2 (Fit);
# More modes (EXTRA=1: 480p gear 5, 800x600 gear 3, 1024x768 gear 2, 1280x1024 gear 1, 320 x 256, all steps) cost the same again.
# The runs are independent and go in parallel (JOBS, default 12; each one is one niced vvp of about 2 minutes a frame on a free machine,
# 10 frames for a full plan). Limits for a quick look: MODES, SRCS (WxH), NOF2, NOSM0 in the environment.
set -e
here=$(cd "$(dirname "$0")" && pwd)
out=${OUT:-$here/build}
mkdir -p "$out"; rm -f "$out"/*.rc
dut=$here/../../pix_layer.v
python3 -I "$here/model.py" selftest
list=$out/jobs.txt
: > "$list"
add() { echo "$*" >> "$list"; }   # NAME PIX_LAYER.V MODE SW SH FMT SMOOTH KIND
for mode in ${MODES:-576 720 1080 ${EXTRA:+480 600 768 1024}}; do
  case $mode in 576|720|1080) srcs=${SRCS:-"320x256 704x200"};; *) srcs=${SRCS:-320x256};; esac
  for src in $srcs; do add "sm_${mode}_$src $dut $mode ${src%x*} ${src#*x} 1 1 full"; done
done
[ -n "$NOF2" ] || for src in 320x256 704x200; do add "sm_576_${src}_f2 $dut 576 ${src%x*} ${src#*x} 2 1 ctl"; done
[ -n "$NOSM0" ] || { add "nn_576_320x256 $dut 576 320 256 1 0 nn"; add "nn_576_320x256_f2 $dut 576 320 256 2 0 ctlnn"; }
# one vvp per line of the list
xargs -P "${JOBS:-12}" -L 1 sh -c 'sh "$0" "'"$out"'" "$@"' "$here/job.sh" < "$list"
bad=0
while read -r n rest; do
  rc=$(cat "$out/$n.rc" 2>/dev/null || echo 2)
  if [ "$rc" = 0 ]; then
    echo "pass $n: $(grep -c '^ok' "$out/$n/check.log") steps"
  else
    echo "FAIL pixscale: $n (rc $rc)"; if [ "$rc" = 2 ]; then echo "  (rc 2: the run did not finish: iverilog failed or vvp hit its 4500 s timeout without DONE)"; tail -n 3 "$out/$n/iv.log" "$out/$n/sim.log" 2>&1 || true; else cat "$out/$n/check.log" 2>/dev/null || true; fi; bad=1
  fi
done < "$list"
[ $bad = 0 ] || exit 1
echo "PASS pixscale ($(wc -l < "$list") runs: nearest exact, smoothing within 1 LSB, the build without smoothing)"
