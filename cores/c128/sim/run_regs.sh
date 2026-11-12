#!/bin/sh
# T113 5.6: the register page's COLD register (c128_regs.v offset 080). Run from the repository root.
set -e
out=${TMPDIR:-/tmp}/c128_regs
mkdir -p "$out"
src="cores/c128/sim/tb_regs.v cores/c128/c128_regs.v cores/c128/c128_joyswap.v"
iverilog -g2012 -o "$out/tb" $src
vvp "$out/tb" | tee "$out/log" | grep -E "^(FAIL|PASS)"
grep -q "^PASS tb_regs" "$out/log" || { echo "bench failed"; exit 1; }
echo "PASS run_regs"
