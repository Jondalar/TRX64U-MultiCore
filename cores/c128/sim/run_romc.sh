#!/bin/sh
# T113 §5.4: the ROM line cache bench. Run from the repository root.
set -e
out=${TMPDIR:-/tmp}/c128_romc
mkdir -p "$out"
src="cores/c128/sim/tb_romc.v cores/c128/c128_romc.v cores/c128/c128_rome.v cores/nes/sim/ddr2c_model.v"
iverilog -g2012 -o "$out/tb" $src
vvp "$out/tb" | tee "$out/log" | grep -E "^(==|   |PASS|FAIL)"
grep -q "^PASS tb_romc" "$out/log" || { echo "bench failed"; exit 1; }
echo "PASS run_romc"
