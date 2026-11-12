#!/bin/sh
# T85: the sector service (floppy and hard disk) on the real mig_arb and a DDR2 model. PASS when errors=0.
# T102 D1: the same bench with the HDF size register in bytes (the Amiga core) and in sectors (the A1200 core, MegaAGS.hdf of
# 9,008,283,648 bytes = 17,594,304 sectors).
# run_sdc.sh [a500|a1200|all] (T100 D10: one bench per core): a500 = the Amiga's amiga_sdc (size register in bytes), a1200 = the A1200's copy
# (queues in block RAM, size in sectors).
set -e
which=${1:-all}
case "$which" in a500|a1200|all) ;; *) echo "usage: run_sdc.sh [a500|a1200|all]"; exit 2 ;; esac
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
t=$root/cores/amiga/rtl/trx
sim=$root/host/sim
hd=$root/cores/amiga/mono/host   # each core's own copy of the host files (T164); the A1200 part switches it
out=$here/build/sdc
mkdir -p "$out"
cd "$out"
run() {   # name, then iverilog -P options
    n=$1; shift
    iverilog -g2012 "$@" -o tb_sdc_$n.vvp "$here/tb_sdc.sv" $sim/mig_ui_model.v $hd/ddr2_port.v ${sdcv:-$root/cores/amiga/rtl/a500/trx_afifo_rr.v $t/amiga_sdc.v} $t/amiga_eport.v > build_$n.log 2>&1 || { head -30 build_$n.log; echo FAIL build; exit 1; }
    vvp -n tb_sdc_$n.vvp > run_$n.log
    cat run_$n.log
}
if [ "$which" != a1200 ]; then
echo "== bytes (HDF_SECT=0), 4 MiB image"
run bytes
grep -q 'PASS tb_sdc errors=0' run_bytes.log
echo "== sectors (HDF_SECT=1), MegaAGS.hdf 9,008,283,648 bytes, last sector 17,594,303"
run sect -Ptb_sdc.HDFSECT=1 -Ptb_sdc.HSIZE_REG=17594304 -Ptb_sdc.HSIZE_BYTES=9008283648 -Ptb_sdc.HLBA_LAST=17594303
grep -q 'PASS tb_sdc errors=0' run_sect.log
fi
if [ "$which" != a500 ]; then
# T97 5.11: the A1200's copy of amiga_sdc (the two queues in block RAM, trx_afifo_rr.v) in the same bench, both size units
echo "== A1200 copy (registered-read queues): bytes, sectors"
hd=$root/cores/amiga1200/mono/host
sdcv="$root/cores/amiga1200/rtl/trx_afifo_rr.v $root/cores/amiga1200/rtl/amiga_sdc.v"
run a12bytes
grep -q 'PASS tb_sdc errors=0' run_a12bytes.log
run a12sect -Ptb_sdc.HDFSECT=1 -Ptb_sdc.HSIZE_REG=17594304 -Ptb_sdc.HSIZE_BYTES=9008283648 -Ptb_sdc.HLBA_LAST=17594303
grep -q 'PASS tb_sdc errors=0' run_a12sect.log
fi
echo "PASS run_sdc ($which)"
