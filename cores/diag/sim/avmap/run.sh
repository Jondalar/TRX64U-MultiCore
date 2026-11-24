#!/bin/sh
# T259 S1, S2, S3: the channel map (names / scan groups), the channel probe, the RGB mode and the SYNCn / FB knobs of the diag core, through the register bus, on the ball vectors (tb_avmap).
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
root=$here/../../..
out=$here/build/avmap; mkdir -p "$out"
iverilog -g2012 -s tb_avmap -o "$out/tb.vvp" "$here/tb_avmap.v" "$root/cores/diag/rtl/diag_regs.v" "$root/cores/diag/rtl/diag_avout.v" "$root/cores/diag/rtl/diag_yc.v"
vvp -n "$out/tb.vvp" > "$out/log" || true
grep "^FAIL\|^PASS\|^  run" "$out/log" || true
grep -q '^PASS tb_avmap errors=0' "$out/log" || { echo "FAIL avmap"; exit 1; }
echo "PASS avmap (names and scan-group maps, channel probe words per bar, RGB words per permutation and bit order, SYNCn / FB per knob)"
