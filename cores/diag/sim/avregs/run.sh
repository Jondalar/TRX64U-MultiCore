#!/bin/sh
# T201 S2 / S10: the A/V knob registers of the diag core change the encoder's words as specified, live (tb_avregs), and the sync knobs give the specified csync (tb_avsync).
# Both are tops of cores/diag/sim/tb_avregs.v; they run side by side (one thread each).
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
root=$here/../../..
out=$here/build/avregs; mkdir -p "$out"
src="$here/tb_avregs.v $root/cores/diag/rtl/diag_regs.v $root/cores/diag/rtl/diag_avout.v $root/cores/diag/rtl/diag_yc.v"
iverilog -g2012 -s tb_avregs -o "$out/tb.vvp" $src
iverilog -g2012 -s tb_avsync -o "$out/sy.vvp" $src
vvp -n "$out/sy.vvp" > "$out/sylog" 2>&1 &
sy=$!
vvp -n "$out/tb.vvp" > "$out/log" || true
wait $sy || true
grep "^FAIL\|^PASS\|^metrics" "$out/log"
grep "^FAIL\|^PASS\|^sync" "$out/sylog"
grep -q '^PASS tb_avregs errors=0' "$out/log" || { echo "FAIL avregs"; exit 1; }
grep -q '^PASS tb_avsync errors=0' "$out/sylog" || { echo "FAIL avsync"; exit 1; }
echo "PASS avregs (every knob register changes the words as specified, live; marker freeze; the csync of each sync knob)"
