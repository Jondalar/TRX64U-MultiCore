#!/bin/sh
# T194 / T140 section 18: c128_act.v (which chip is being used). Run from the repository root.
set -e
out=${TMPDIR:-/tmp}/c128_act
mkdir -p "$out"
iverilog -g2012 -o "$out/tb" cores/c128/sim/tb_act.v cores/c128/c128_act.v
vvp "$out/tb" | tee "$out/log" | grep -E "^(FAIL|PASS)"
grep -q "^PASS tb_act" "$out/log" || { echo "bench failed"; exit 1; }
echo "PASS run_act"
