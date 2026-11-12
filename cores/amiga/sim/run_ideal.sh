#!/bin/sh
# T85: Minimig + fx68k, ideal memory. Verilator on the Mac / in CI. Usage: run_ideal.sh [SPARSE=0|1]
# OUT_SUFFIX names the build directory apart, so two callers (amiga_equiv and amiga_mem) can build the
# same SPARSE=0 machine at the same time: ci/sims.sh runs benches in parallel in one tree (a sandbox only
# with --check-deps), and both wrote build/ideal0 (run 866: amiga_equiv exit 1 beside amiga_mem).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
m=$root/cores/amiga/rtl/src
out=$here/build/ideal${1:-0}${OUT_SUFFIX:-}
mkdir -p "$out"
python3 "$here/gen_testrom.py" "$out/rom.hex" > /dev/null
cp $m/fx68k/microrom.mem $m/fx68k/nanorom.mem "$out/"
for f in fx68k fx68kAlu uaddrPla; do sed -e 's/unique case/case/g' $m/fx68k/$f.sv > "$out/$f.sv"; done
cd "$out"
nice -n 15 verilator --binary --timing -Wno-fatal -Wno-lint -Wno-style -Wno-WIDTH -Wno-CASEINCOMPLETE -Wno-BLKANDNBLK \
  -DINFER_DPRAM -GSPARSE=${1:-0} -j 2 --top-module tb_ideal -o tb_ideal \
  "$here/tb_ideal.sv" $root/cores/amiga/rtl/trx/trx_tdp16.v $m/nanomig.v $(ls $m/minimig-aga/*.v | grep -v -e /paula_floppy.v -e /paula_floppy_fifo.v) $root/cores/amiga/rtl/a500/paula_floppy.v $root/cores/amiga/rtl/a500/paula_floppy_fifo.v $root/cores/amiga/rtl/trx/amiga_rtc.v fx68k.sv fx68kAlu.sv uaddrPla.sv > build.log 2>&1 || { tail -40 build.log; exit 1; }
./obj_dir/tb_ideal
