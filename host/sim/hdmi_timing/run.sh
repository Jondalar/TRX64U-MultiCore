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
echo "PASS hdmi_timing (9 modes exact, align on 9 rows, 7 refused)"
