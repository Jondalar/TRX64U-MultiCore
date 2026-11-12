#!/bin/sh
# T85: Minimig + fx68k behind amiga_mem (cache, elastic stall) and a behavioural DDR2. Verilator.
#   run_mem.sh [RD_LAT]     RD_LAT in ui_clk cycles of the DDR2 model (default 8 = ddr2_lean-like)
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
m=$root/cores/amiga/rtl/src
t=$root/cores/amiga/rtl/trx
sim=$root/host/sim
out=$here/build/mem${1:-8}${MODE:-basic}${LOADN}_${LOADR}${DBG:+d}
mkdir -p "$out"
python3 "$here/gen_testrom.py" "$out/rom.hex" ${MODE:-basic} ${LOADN:-4} ${LOADR:-1} > /dev/null
cp $m/fx68k/microrom.mem $m/fx68k/nanorom.mem "$out/"
for f in fx68k fx68kAlu uaddrPla; do sed -e 's/unique case/case/g' $m/fx68k/$f.sv > "$out/$f.sv"; done
cd "$out"
nice -n 15 verilator --binary --timing -Wno-fatal -Wno-lint -Wno-style -Wno-WIDTH -Wno-CASEINCOMPLETE -Wno-BLKANDNBLK \
  -DINFER_DPRAM -GRD_LAT=${1:-8} -GDBG=${DBG:-0} -j 2 --top-module tb_mem -o tb_mem \
  "$here/tb_mem.sv" $sim/mig_ui_model.v $t/trx_afifo.v $t/trx_tdp16.v $t/amiga_ce.v $t/amiga_mem.v \
  $m/nanomig.v $(ls $m/minimig-aga/*.v | grep -v -e /paula_floppy.v -e /paula_floppy_fifo.v) $root/cores/amiga/rtl/a500/paula_floppy.v $root/cores/amiga/rtl/a500/paula_floppy_fifo.v $t/amiga_rtc.v fx68k.sv fx68kAlu.sv uaddrPla.sv > build.log 2>&1 || { tail -40 build.log; exit 1; }
./obj_dir/tb_mem
