#!/bin/sh
# T85 S4: amiga_mem on the real ddr2_lean (designs/ddr2_test models) behind the real mig_arb with video load.
#   run_lat.sh [CYCLES] [VIDEO 0|1]
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
D=$root/cores/amiga/mono/host   # the core's own copy of the host files (T164)
T=$root/designs/ddr2_test/sim
t=$root/cores/amiga/rtl/trx
out=$here/build/lat
mkdir -p "$out"
cd "$out"
nice -n 15 iverilog -g2012 -Ptb_lat.CYCLES="${1:-20000}" -Ptb_lat.VIDEO="${2:-1}" -o tb_lat.vvp "$here/tb_lat.sv" $D/cal_seq.v $D/ddr2_lean.v $D/ddr2_port.v $T/xilinx_models.v $T/ddr2_model.v $t/trx_afifo.v $t/amiga_ce.v $t/amiga_mem.v > build.log 2>&1 || { head -30 build.log; echo FAIL build; exit 1; }
nice -n 15 vvp -n tb_lat.vvp | tee run.log
grep -q 'PASS tb_lat errors=0' run.log
