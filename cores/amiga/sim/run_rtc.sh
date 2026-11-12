#!/bin/sh
# T191: the Amiga's real-time clock (amiga_rtc.v). PASS when errors=0.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
t=$root/cores/amiga/rtl/trx
out=$here/build/rtc
mkdir -p "$out"
cd "$out"
iverilog -g2012 -o tb_rtc.vvp "$here/tb_rtc.sv" $t/amiga_rtc.v > build.log 2>&1 || { head -30 build.log; echo FAIL build; exit 1; }
vvp -n tb_rtc.vvp | tee run.log
grep -q 'PASS tb_rtc errors=0' run.log
