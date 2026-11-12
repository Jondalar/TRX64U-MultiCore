#!/bin/sh
# T90 S5: the TED colour table (needs cores/c264/rtl/src, tools/vendor_c16nano.sh).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
mkdir -p "$here/build"
iverilog -g2012 -o "$here/build/col.vvp" "$here/tb_c264_colors.v" "$root/cores/c264/rtl/src/colors_to_rgb.v"
cd "$here/build" && vvp -n col.vvp | tee col.log
grep -q "^PASS tb_c264_colors" col.log
