#!/bin/sh
# T141: the raw key queue of amiga_input (cores/amiga/rtl/trx/amiga_input.v, the A500's file; the A1200's own copy is kept equal by cores/amiga1200/tools/check_wrapper_fork.sh).
# PASS when errors=0
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=$here/build/rawkey
mkdir -p "$out"
cd "$out"
src=$root/cores/amiga/rtl/trx/amiga_input.v
iverilog -g2012 -o tb.vvp "$here/tb_rawkey.v" $src > build.log 2>&1 || { head -30 build.log; echo FAIL build; exit 1; }
vvp -n tb.vvp | tee run.log
grep -q 'PASS tb_rawkey errors=0' run.log
