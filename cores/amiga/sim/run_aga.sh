#!/bin/sh
# A1200 stripes: full-chipset AGA frame bench. run_aga.sh FMODE  -> build/aga_<fmode>/frame.txt, then aga_view.py
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
m=$root/cores/amiga/rtl/src; t=$root/cores/amiga/rtl/trx; sim=$root/host/sim
out=$here/build/aga_${1:-0}$([ "${HIRES:-0}" = 1 ] && echo h || true)${NPL:+p$NPL}${SCR:+s$SCR}${SPR:+x}
mkdir -p "$out"
python3 "$here/gen_aga.py" "$out" ${1:-0} ${HIRES:-0} ${DDFS:-0x38} ${DDFE:-0xD0} ${NPL:-8} ${SCR:-0} ${SPR:-0}
cp $m/fx68k/microrom.mem $m/fx68k/nanorom.mem "$out/"
for f in fx68k fx68kAlu uaddrPla; do sed -e 's/unique case/case/g' $m/fx68k/$f.sv > "$out/$f.sv"; done
cd "$out"
nice -n 15 verilator --binary --timing -Wno-fatal -Wno-lint -Wno-style -Wno-WIDTH -Wno-CASEINCOMPLETE -Wno-BLKANDNBLK -Wno-PROCASSINIT \
  -DINFER_DPRAM -GDUMPFR=${DUMPFR:-6} -j 2 --top-module tb_aga -o tb_aga \
  "$here/tb_aga.sv" $sim/mig_ui_model.v $t/trx_afifo.v $t/trx_tdp16.v $t/amiga_ce.v $t/amiga_mem.v \
  $m/nanomig.v $(ls $m/minimig-aga/*.v | grep -v -e /paula_floppy.v -e /paula_floppy_fifo.v) $root/cores/amiga/rtl/a500/paula_floppy.v $root/cores/amiga/rtl/a500/paula_floppy_fifo.v $t/amiga_rtc.v fx68k.sv fx68kAlu.sv uaddrPla.sv > build.log 2>&1 || { grep -E "%Error" -A4 build.log | head -40; exit 1; }
./obj_dir/tb_aga | tail -3
