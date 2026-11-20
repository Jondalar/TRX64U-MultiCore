#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# U16 D6: c64_text.v's geometry on hdmi_timing.v (tb_text_geom.v), every row of U16 D2 at every scale, one whole frame each; then the
# negative control (+mut=1) must fail. JOBS runs in parallel (default 4).
set -e
here=$(cd "$(dirname "$0")" && pwd)
hd=$(cd "$here/../../mono/host" && pwd)
out=$here/build/text   # its own folder: the nightly runs this bench and run_scaler.sh at the same time (runs 2492, 2534)
mkdir -p "$out"
iverilog -g2012 -y "$hd" -y "$here" -I "$hd" -o "$out/tb.vvp" "$here/tb_text_geom.v"
: > "$out/cases.txt"
for m in 0 1 2 6 7 8 9; do for s in 0 1 2; do echo "m${m}_s${s} +mode=$m +sc=$s" >> "$out/cases.txt"; done; done
echo "ctl_m7_s1 +mode=7 +sc=1 +mut=1" >> "$out/cases.txt"
xargs -P "${JOBS:-4}" -L 1 sh -c 'log=$0; cd "'"$here"'" && timeout 1800 nice -n 15 vvp -n "'"$out/tb.vvp"'" "$@" > "'"$out"'/$log.log" 2>&1 || true' < "$out/cases.txt"
fail=0
for m in 0 1 2 6 7 8 9; do for s in 0 1 2; do
  l="$out/m${m}_s${s}.log"
  if grep -q "^PASS text" "$l"; then grep -h "^PASS text" "$l" | sed 's/^PASS //'; else grep -h "^FAIL" "$l" | head -5; echo "FAIL text m$m s$s"; fail=1; fi
done; done
# (the control's own FAIL line is not echoed: ci/sims.sh takes any FAIL in the output as the bench's)
if grep -q "^FAIL text mode" "$out/ctl_m7_s1.log"; then echo "control +mut=1 (720p60 Fit, hd + 1) caught"; else echo "FAIL control +mut=1 not caught"; fail=1; fi
[ $fail = 0 ] || { echo "FAIL text"; exit 1; }
echo "PASS text geometry (7 rows x 3 scales, control caught)"
