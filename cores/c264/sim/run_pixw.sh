#!/bin/sh
# T90: the frame writer against a TED-shaped raster and a model of the e_ port (Icarus).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
c=$root/cores/c264/rtl/trx
out=$here/build
mkdir -p "$out"
iverilog -g2012 -o "$out/pixw.vvp" "$here/tb_c264_pixw.v" "$c/c264_nco.v" "$c/c264_pixw.v"
cd "$out" && nice -n 15 vvp -n pixw.vvp | tee pixw.log
grep -q "^PASS tb_c264_pixw" pixw.log
