#!/bin/sh
# T85: random-access fuzz of amiga_mem against a shadow array, DDR2 model with random timing. Verilator.
#   run_fuzz.sh [MIGLAT MIGMISS MIGSTALL SEED NCYC WRP]   (build once, run with the given parameters; each distinct set rebuilds)
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
t=$root/cores/amiga/rtl/trx
sim=$root/host/sim
lat=${1:-8}; miss=${2:-0}; stall=${3:-0}; seed=${4:-1}; ncyc=${5:-2000000}; wrp=${6:-25}
out=$here/build/fuzz${OUT_SUFFIX}_${lat}_${miss}_${stall}_${seed}
mkdir -p "$out"; cd "$out"
nice -n 15 verilator --binary --timing -Wno-fatal -Wno-lint -Wno-style -Wno-WIDTH -Wno-BLKANDNBLK -O3 -CFLAGS -O2 \
  -GMIGLAT=$lat -GMIGMISS=$miss -GMIGSTALL=$stall -GSEED=$seed -GNCYC=$ncyc -GWRP=$wrp -j 2 --top-module tb_fuzz -o tb_fuzz \
  "$here/tb_fuzz.sv" $sim/mig_ui_model.v $t/trx_afifo.v $t/amiga_ce.v $t/amiga_mem.v > build.log 2>&1 || { grep -E "%Error" -A4 build.log | head -40; exit 1; }
./obj_dir/tb_fuzz
