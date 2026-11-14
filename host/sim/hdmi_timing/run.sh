#!/bin/sh
# T164 WP2 (S2): host/hdmi_timing.v, iverilog, with the mode table host/hdmi_modes.vh:
#   1. the 9 valid rows: clocks per frame, pixels per frame, active pixels, per-pixel cx / cy / de / hsync / vsync (position, width, polarity),
#      every pixel held for gear clocks, the encoder's frame constants, the frame rate against Gideon's table;
#   2. align on every row: a pulse mid-frame re-phases to the active origin on the two locked (1080p) rows, is ignored on the other seven;
#   3. the 7 modes that are not in the table (3, 4, 5, 12..15) are refused: valid 0, running and ce stay 0;
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
# T164 WP18: HOSTDIR (relative to the repository root) points the bench at a large core's copy of the host files (cores/<id>/mono/host); default host/
hd=$root/${HOSTDIR:-host}
out=$here/build$(echo "${HOSTDIR:-}" | tr / _)
mkdir -p "$out"
dut=$hd/hdmi_timing.v
# T188: the Amiga copies carry the Amiga edition of hdmi_modes.vh (rows 0..2 bent to the Amiga frame)
def=""; case "${HOSTDIR:-}" in cores/amiga/*|cores/amiga1200/*) def="-DT188";; esac
run() {   # <name> <dut.v> [+only=N]
  nice -n 15 iverilog -g2012 $def -I"$hd" -o "$out/$1.vvp" -s tb "$here/tb_hdmi_timing.v" "$2"
  timeout 1500 nice -n 15 vvp -n "$out/$1.vvp" $3 > "$out/$1.log" 2>&1 || true
}
run all "$dut"
grep -q "^PASS hdmi_timing: 9 modes, 7 refused" "$out/all.log" || { grep "^FAIL" "$out/all.log" | head; grep -q "^FAIL" "$out/all.log" || echo "no verdict in $out/all.log: vvp ended on its 1500 s timeout (a loaded machine) or crashed"; echo "FAIL hdmi_timing"; exit 1; }
grep "^mode " "$out/all.log"
# T164 run 1142: a copy whose vtot_m1 is a second stage (sum of the registered yact0 / vact, cores/c128/mono/host) gets a short-rst run
if grep -q "vtot_m1 <= yact0 + vact" "$dut"; then
  run short "$dut" "+only=9 +rst=2"
  grep -q "^PASS hdmi_timing: 1 modes" "$out/short.log" || { grep "^FAIL" "$out/short.log" | head; echo "FAIL hdmi_timing: rst of 2 clocks"; exit 1; }
  echo "short| rst held 2 clocks (mode 9, switched from the reset mode): vtot_m1 valid at the first running edge"
  echo "PASS hdmi_timing vtot_m1 stage (2-clock rst)"
fi
# T188 D3: the Amiga copies' hdmi_timing has FOLLOW (the frame follows the core): bench tb_follow.v on a reduced table (24 pixels per line, the real vertical rasters)
if grep -q "parameter FOLLOW" "$dut"; then
  mini=$out/mini; mkdir -p "$mini"
  python3 - "$hd/hdmi_modes.vh" "$mini/hdmi_modes.vh" <<'PYEOF'
import re, sys
s = open(sys.argv[1]).read()
s2 = re.sub(r"(hm_row = \{7'd\d+, )4'd\d+, 12'd\d+, 12'd\d+, 12'd\d+, 12'd\d+,", lambda m: m.group(1) + "4'd1, 12'd8, 12'd4, 12'd4, 12'd8,", s)
assert s2 != s
open(sys.argv[2], "w").write(s2)
PYEOF
  cp "$dut" "$mini/hdmi_timing.v"
  nice -n 15 iverilog -g2012 $def -I"$mini" -o "$out/follow1.vvp" -s tb "$here/tb_follow.v" "$mini/hdmi_timing.v"
  nice -n 15 iverilog -g2012 $def -I"$mini" -Ptb.FOLLOW=0 -o "$out/follow0.vvp" -s tb "$here/tb_follow.v" "$mini/hdmi_timing.v"
  fl() {   # <vvp> <plusargs...>: one run, the result line
    v=$1; shift; timeout 1500 nice -n 15 vvp -n "$out/$v.vvp" "$@" 2>&1 | grep "^fc=" || echo "FAIL follow: no result line ($v $*)"
  }
  # follow on: no repeat, no skip, follow_e inside -8 .. 16, every frame as long as the table says plus follow_e
  for fc in 49.90 49.920128 50.080128 50.20 49.60; do   # T188 D5: lower limit -8 lines covers 50.20 Hz (about -6 lines at 1080p)
    r=$(fl follow1 +fc=$fc +mode=2 +frames=700); echo "follow| $r"
    echo "$r" | grep -q " repeats=0 skips=0 .*len_bad=0/" || { echo "FAIL follow: $r"; exit 1; }
    echo "$r" | sed 's/.*fe=\[\(-*[0-9]*\)\.\.\([0-9-]*\)\].*/\1 \2/' | awk '{ exit !($1 >= -8 && $2 <= 16) }' || { echo "FAIL follow: follow_e outside -8 .. 16: $r"; exit 1; }
  done
  # T188 board 2026-11-13 / D4: the table's rate is the 313-line frame; the Amiga frame is 313 lines (49.92 Hz) with 312-line frames mixed in (measured 49.943 Hz = about 14 % short frames)
  r=$(fl follow1 +fc=49.920 +mix=0.14 +mode=2 +frames=700); echo "follow| mix 14 % 312-line frames: $r"
  echo "$r" | grep -q " repeats=0 skips=0 .*len_bad=0/" || { echo "FAIL follow: $r"; exit 1; }
  r=$(fl follow1 +fc=49.920 +mix=0.5 +mode=0 +frames=700); echo "follow| mix 50 % 312-line frames: $r"
  echo "$r" | grep -q " repeats=0 skips=0 .*len_bad=0/" || { echo "FAIL follow: $r"; exit 1; }
  for m in 0 1; do
    r=$(fl follow1 +fc=49.90 +mode=$m +frames=700); echo "follow| $r"
    echo "$r" | grep -q " repeats=0 skips=0 .*len_bad=0/" || { echo "FAIL follow: $r"; exit 1; }
  done
  # the 60 Hz row is not followed: follow_e stays 0
  r=$(fl follow1 +fc=49.90 +mode=8 +frames=40); echo "follow| $r"
  echo "$r" | grep -q "fe=\[0\.\.0\] fe_end=0 " || { echo "FAIL follow: 60 Hz row moved: $r"; exit 1; }
  # control: follow off (FOLLOW = 0) at 49.60 and 50.20 Hz shows the beat (repeats, skips)
  r=$(fl follow0 +fc=49.60 +mode=2 +frames=700); echo "control| $r"
  echo "$r" | grep -q " repeats=0 " && { echo "FAIL follow control: no repeat without the follow: $r"; exit 1; }
  r=$(fl follow0 +fc=50.20 +mode=2 +frames=700); echo "control| $r"
  echo "$r" | grep -q " skips=0 " && { echo "FAIL follow control: no skip without the follow: $r"; exit 1; }
  echo "PASS hdmi_timing follow (no repeat, no skip at 49.60 .. 50.20 Hz, limits kept, control shows the beat)"
fi
echo "PASS hdmi_timing (9 modes exact, align on 9 rows, 7 refused)"
