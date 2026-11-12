#!/bin/sh
# T164 S3: host/tmds_rep.v (the TMDS bit repeater, ADR-0060) under iverilog. tb_tmds_rep.v unpacks the GTP words into the serial line bits and
# compares them with the pixel symbol stream at 1/gear rate: gear 1, 2, 3, 5, 3, each after a reset that changes the gear, 4 lanes.
set -e
here=$(cd "$(dirname "$0")" && pwd)
dut=$here/../../tmds_rep.v
out=$here/build
mkdir -p "$out"
run() {   # <name> <dut.v>
  nice -n 15 iverilog -g2012 -o "$out/$1.vvp" -s tb "$here/tb_tmds_rep.v" "$2"
  timeout 600 nice -n 15 vvp -n "$out/$1.vvp" > "$out/$1.log" 2>&1 || true
}
run all "$dut"
grep -q "^PASS tmds_rep" "$out/all.log" || { grep "^FAIL" "$out/all.log" | head; echo "FAIL tmds_rep"; exit 1; }
grep "^gear " "$out/all.log"
echo "PASS tmds_rep (gears 1 2 3 5 3 exact)"
