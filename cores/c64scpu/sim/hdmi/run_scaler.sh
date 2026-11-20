#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# U16: vic_scaler.v on hdmi_timing.v (tb_scaler.v), every row of U16 D2 at every scale (Integer, Fit, Fill), two frames each after the
# lock; four of the runs also change the scale (two that move the align line, two that keep it) and two check the palette and the scan
# lines; then the three negative controls (+mut) must fail. JOBS runs in parallel (default 4).
set -e
here=$(cd "$(dirname "$0")" && pwd)
hd=$(cd "$here/../../mono/host" && pwd)
out=$here/build/scaler   # its own folder: the nightly runs this bench and run_text.sh at the same time (runs 2492, 2534)
mkdir -p "$out"
iverilog -g2012 -y "$hd" -y "$here" -I "$hd" -o "$out/tb.vvp" "$here/tb_scaler.v"
cases="0:0 0:1:2 0:2 1:0 1:1 1:2 2:0 2:1:2:x 2:2 6:0 6:1 6:2 7:0 7:1 7:2 8:0 8:1:0:x 8:2 9:0:1 9:1 9:2"
ctls="2:1:1 0:1:2 8:2:3"
jobs=${JOBS:-4}
for c in $cases; do
  m=$(echo $c | cut -d: -f1); s=$(echo $c | cut -d: -f2); s2=$(echo $c | cut -d: -f3); x=$(echo $c | cut -d: -f4)
  a="+mode=$m +sc=$s"; [ -n "$s2" ] && a="$a +sc2=$s2"; [ -n "$x" ] && a="$a +extra"
  echo "m${m}_s${s} $a"
done > "$out/cases.txt"
for c in $ctls; do
  m=$(echo $c | cut -d: -f1); s=$(echo $c | cut -d: -f2); u=$(echo $c | cut -d: -f3)
  echo "ctl${u}_m${m}_s${s} +mode=$m +sc=$s +mut=$u +nf=1"
done >> "$out/cases.txt"
# each line: <log> <args>; xargs runs JOBS of them at once
xargs -P "$jobs" -L 1 sh -c 'log=$0; cd "'"$here"'" && timeout 1800 nice -n 15 vvp -n "'"$out/tb.vvp"'" "$@" > "'"$out"'/$log.log" 2>&1 || true' < "$out/cases.txt"
fail=0
for c in $cases; do
  m=$(echo $c | cut -d: -f1); s=$(echo $c | cut -d: -f2)
  l="$out/m${m}_s${s}.log"
  if grep -q "^PASS scaler" "$l"; then grep -h "^mode \|^scale change\|^palette" "$l"; else grep -h "^FAIL" "$l" | head -5; echo "FAIL scaler m$m s$s"; fail=1; fi
done
for c in $ctls; do
  m=$(echo $c | cut -d: -f1); s=$(echo $c | cut -d: -f2); u=$(echo $c | cut -d: -f3)
  l="$out/ctl${u}_m${m}_s${s}.log"
  # (the control's own FAIL line is not echoed: ci/sims.sh takes any FAIL in the output as the bench's)
  if grep -q "^FAIL scaler mode" "$l"; then echo "control +mut=$u (mode $m scale $s) caught: $(grep -m1 '^FAIL mode' "$l" | sed 's/^FAIL mode [0-9]* scale [0-9]*: //')"
  else echo "FAIL control +mut=$u (mode $m scale $s) not caught"; fail=1; fi
done
[ $fail = 0 ] || { echo "FAIL scaler"; exit 1; }
echo "PASS scaler (7 rows x 3 scales, 4 scale changes, palette and scan lines, 3 controls caught)"
