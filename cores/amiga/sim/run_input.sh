#!/bin/sh
# T85: keys, mouse, joysticks and audio glue of the Amiga core. PASS when errors=0.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
t=$root/cores/amiga/rtl/trx
out=$here/build/input
mkdir -p "$out"
cd "$out"
iverilog -g2012 -o tb_input.vvp "$here/tb_input.sv" $t/amiga_input.v $t/amiga_audio.v > build.log 2>&1 || { head -30 build.log; echo FAIL build; exit 1; }
vvp -n tb_input.vvp | tee run.log
grep -q 'PASS tb_input errors=0' run.log
