#!/bin/sh
# A1200 stripes: AGA wide fetch (dout48 / p2_dout48) of amiga_mem against a known 64-bit pattern, chip and fast RAM, miss and hit.
#   run_wide.sh          must print PASS
#   NEG=1 run_wide.sh    the old tie-off (consumers see 48'd0): must FAIL (negative control)
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
t=$root/cores/amiga/rtl/trx
out=$here/build/wide${NEG:+neg}
mkdir -p "$out"; cd "$out"
nice -n 15 verilator --binary --timing -Wno-fatal -Wno-lint -Wno-style -Wno-WIDTH -DINFER_DPRAM ${NEG:+-DNEG} -j 2 \
  --top-module tb_wide -o tb_wide "$here/tb_wide.sv" $root/host/sim/mig_ui_model.v $t/trx_afifo.v $t/amiga_mem.v > build.log 2>&1 || { tail -40 build.log; exit 1; }
./obj_dir/tb_wide | tee run.log
grep -q '^PASS' run.log
