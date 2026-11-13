#!/bin/sh
# T201 S1: the diag core's test picture encoder (cores/diag/rtl/diag_avout.v + diag_yc.v) against the unedited copy of the C128's encoder (sim/av/c128_yc_ref.v), four standards, 1.2 frames each.
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
root=$here/../../..
out=$here/build/avenc; mkdir -p "$out"
iverilog -g2012 -o "$out/tb.vvp" "$here/tb_avenc.v" "$root/cores/diag/rtl/diag_avout.v" "$root/cores/diag/rtl/diag_yc.v" "$here/av/c128_yc_ref.v"
vvp -n "$out/tb.vvp" > "$out/log" || true
cat "$out/log" | grep "^std\|^PASS\|^FAIL"
grep -q '^PASS tb_avenc errors=0' "$out/log" || { echo "FAIL avenc"; exit 1; }
echo "PASS avenc (the diag encoder = the C128's, PAL / NTSC / PAL-60 / NTSC-50)"
