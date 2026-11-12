#!/bin/sh
# T85: amiga_core on mig_arb + behavioural DDR2, driven like the application. Verilator.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
m=$root/cores/amiga/rtl/src
t=$root/cores/amiga/rtl/trx
sim=$root/host/sim
# the host files come from the core's own copy (T164): the A1200 build (TG68K) from cores/amiga1200/mono/host, the OCS/ECS build from cores/amiga/mono/host
if [ -n "${TG68K:-}" ]; then hd=$root/cores/amiga1200/mono/host; else hd=$root/cores/amiga/mono/host; fi
out=$here/build/l2${BOOT:+boot}${ECS:+e}${OUT_SUFFIX}
mkdir -p "$out"
if [ -n "$BOOT" ]; then
  python3 -c "
import sys
r=open('$KICK','rb').read(); a=open('$ADF','rb').read()
open('$out/rom.hex','w').write(''.join('%02x%02x\\n'%(r[i],r[i+1]) for i in range(0,len(r),2)))
open('$out/adf.hex','w').write(''.join('%02x%02x\\n'%(a[i],a[i+1]) for i in range(0,len(a),2)))
"
else
  python3 "$here/gen_testrom.py" "$out/rom.hex" ${MODE:-basic} > /dev/null
fi
# TG68K=1: the Amiga 1200 core's CPU (T97 step 2), the 68020 kernel behind cores/amiga1200/rtl/tg68k_020.v, no fx68k
if [ -n "$TG68K" ]; then
  a12=$root/cores/amiga1200
  perl -pe 's/^module TG68KdotC_Kernel\b/module TG68KdotC_Kernel_core/' "$a12/sim/tg68k/TG68KdotC_Kernel.v" > "$out/tg68k_kernel.v"
  cpusrc="-DENABLE_TG68K $a12/rtl/tg68k_020.v tg68k_kernel.v"
  sh "$a12/tools/check_wrapper_fork.sh"
  # T97 5.9: the A1200's own amiga_core.v (and its own copy of amiga_input.v, T141)
  trxv="$a12/rtl/amiga_core.v $a12/rtl/amiga_fb.v $a12/rtl/amiga_regs.v $a12/rtl/amiga_mem.v $a12/rtl/amiga_input.v $(ls $t/*.v | grep -v -e /amiga_core.v -e /amiga_fb.v -e /amiga_regs.v -e /amiga_mem.v -e /amiga_input.v)"
  wrap="$a12/rtl/cpu_wrapper.v $a12/rtl/paula_floppy_fifo.v $(ls $m/minimig-aga/*.v | grep -v -e /cpu_wrapper.v -e /paula_floppy_fifo.v -e /minimig\.v)"
  nanov=$a12/rtl/nanomig.v; wrap="$wrap $a12/rtl/minimig.v"
  # T136: the A1200's amiga_fb writes 32-bit pixels (the bench reads them so)
  fb32=1
  # T97 5.11: the A1200's copies of paula_floppy.v (sector buffer in block RAM) and amiga_sdc.v (queues in block RAM, trx_afifo_rr.v)
  wrap="$a12/rtl/paula_floppy.v $(printf '%s\n' $wrap | grep -v /minimig-aga/paula_floppy.v)"
  trxv="$a12/rtl/amiga_sdc.v $a12/rtl/trx_afifo_rr.v $(printf '%s\n' ${trxv:-$t/*.v} | grep -v /trx/amiga_sdc.v)"
else
  cp $m/fx68k/microrom.mem $m/fx68k/nanorom.mem "$out/"
  for f in fx68k fx68kAlu uaddrPla; do sed -e 's/unique case/case/g' $m/fx68k/$f.sv > "$out/$f.sv"; done
  cpusrc="fx68k.sv fx68kAlu.sv uaddrPla.sv"
  # A500 (2026-11-08): its own copies of the floppy buffer/fifo and the sd fifo (cores/amiga/rtl/a500, taken from the A1200)
  wrap="$(ls $m/minimig-aga/*.v | grep -v -e /paula_floppy.v -e /paula_floppy_fifo.v) $root/cores/amiga/rtl/a500/paula_floppy.v $root/cores/amiga/rtl/a500/paula_floppy_fifo.v"
  trxv="$t/*.v $root/cores/amiga/rtl/a500/trx_afifo_rr.v"
fi
cd "$out"
nice -n 15 verilator --binary --timing -Wno-fatal -Wno-lint -Wno-style -Wno-WIDTH -Wno-CASEINCOMPLETE -Wno-BLKANDNBLK \
  -Wno-PROCASSINIT ${VOPT:--O3 -CFLAGS -O2} -DINFER_DPRAM $(if [ "${MOTORCHK:-0}" != 0 ]; then echo -DMOTORCHK_ON; fi) -GFAST=${FAST:-0} -GFASTPAT=${FASTPAT:-0} -GFASTCFG=${FASTCFG:-2} -GZ3=${Z3:-0} -GDDRLOG2=${DDRLOG2:-22} -GNOPIC=${NOPIC:-0} -GMEMNB=${MEMNB:-0} -GDISK=${DISK:-0} -GMOTORCHK=${MOTORCHK:-0} -GDSKCHG=${DSKCHG:-0} -GFRAMES=${FRAMES:-9} -GIDE=${IDE:-0} -GKEY=${KEY:-0} -GAUDIO=${AUDIO:-0} -GSLOW=${SLOW:-0} -GBOOT=${BOOT:-0} -GECS=${ECS:-0} -GCFGCHK=${CFGCHK:-0} -GC020=${C020:-0} -GWINCHK=${WINCHK:-0} -GFB32=${fb32:-0} -GCHS_INIT=${CHS_INIT:-0} -GMEM_INIT=${MEM_INIT:-0} -GHDF_SECT=${HDF_SECT:-0} -GMEMCFG=${MEMCFG:-4} -GPAUSE=${PAUSE:-0} -GMIGLAT=${MIGLAT:-8} -GMIGMISS=${MIGMISS:-0} -GMIGSTALL=${MIGSTALL:-0} -GTICKS=${TICKS:-6500000} -GSTOPUS=${STOPUS:-20000000} -GDUMPUS=${DUMPUS:-5000000} -j 2 --top-module tb_l2 -o tb_l2 \
  -I"$here" "$here/tb_l2.sv" $sim/mig_ui_model.v $hd/ddr2_port.v ${trxv:-$t/*.v} \
  ${nanov:-$m/nanomig.v} $wrap $cpusrc > build.log 2>&1 || { grep -E "%Error" -A4 build.log | head -60; exit 1; }
./obj_dir/tb_l2 $RUNARGS
