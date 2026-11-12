#!/bin/sh
# T134 S9: the IEEE drive bench (tb_pet_disk.sv) under Verilator; the T65 (VHDL) goes through `ghdl synth` first (c264 does the same). Run from anywhere in
# trx64-sim-env (Verilator, GHDL). The Commodore ROMs are not in this repository; point ROMDIR at a folder with
#   dos4040.bin (12 KB, VICE's dos4040-901468-14-16.bin) ctl4040.bin (2 KB) dos8250.bin (16 KB, VICE's dos1001) ctl8250.bin (2 KB)
# SYNTH=1 sh run_disk.sh : the ROM-less smoke (tb_pet_disk.sv +synth)
# usage: ROMDIR=<dir> sh run_disk.sh [4040|8250]     (8250 needs a D80 / D82 image: DISK= and PRG=; default is the generated D64 for the 4040)
set -e
here=$(cd "$(dirname "$0")" && pwd)
pet=$here/..
B=${DISK_BUILD:-/tmp/pet_disk_build}
rm -rf "$B"; mkdir -p "$B/gen" "$B/media"
t=$pet/rtl/src/t65
( cd "$B/gen" && ghdl -a --std=08 -fsynopsys --work=work $t/T65_Pack.vhd $t/T65_ALU.vhd $t/T65_MCode.vhd $t/T65.vhd && ghdl --synth --std=08 -fsynopsys --out=verilog T65 | sed -e 's/\bbreak\b/break_s/g' -e '/^   input  \(BCD_en\|Rdy\|Abort_n\|NMI_n\),$/d' -e "s/^   output NMI_ack);/   output NMI_ack);\n  wire BCD_en = 1'b1, Rdy = 1'b1, Abort_n = 1'b1, NMI_n = 1'b1;/" > t65_synth.v )   # the VHDL defaults of the inputs the drive leaves open (Vivado honours them, ghdl's Verilog does not)   # 'break' is a SystemVerilog keyword
python3 "$here/gen_d64.py" "$B/media"
verilator --binary --timing -j ${JOBS:-2} -O1 --x-assign 0 --x-initial 0 -Wno-fatal -Wno-lint -Wno-style -Wno-WIDTH -Wno-TIMESCALEMOD -Wno-PINMISSING -Wno-PROCASSWIRE \
  --top-module tb_pet_disk --Mdir "$B/obj" -o tb_pet_disk \
  $pet/rtl/ieee_drive/types.sv $pet/rtl/ieee_drive/ieee_drive.sv $pet/rtl/ieee_drive/ieee_misc.sv $pet/rtl/ieee_drive/ieeedrv_drv.sv \
  $pet/rtl/ieee_drive/ieeedrv_logic.sv $pet/rtl/ieee_drive/ieeedrv_step.sv $pet/rtl/ieee_drive/ieeedrv_track.sv $pet/rtl/ieee_drive/ieeedrv_trkgen.sv \
  $pet/rtl/ieee_drive/M6532.sv $pet/rtl/ieee_drive/ieeedrv_via6522.v $pet/rtl/trx/pet_dblk.v $pet/rtl/trx/pet_ieee.sv "$B/gen/t65_synth.v" \
  $here/tb_pet_disk.sv > "$B/verilator.log" 2>&1 || { tail -30 "$B/verilator.log"; echo "FAIL build"; exit 1; }
ty=${1:-4040}
if [ -n "${SYNTH:-}" ]; then   # no Commodore ROM: the synthetic DOS image of tb_pet_disk.sv (drive alive on the bus)
  timeout ${SIM_TIMEOUT:-3000} "$B/obj/tb_pet_disk" +synth +type=4040 | tail -${TAIL:-20}
  exit 0
fi
R=${ROMDIR:?ROMDIR}
if [ "$ty" = 4040 ]; then dosoff=1000; else dosoff=0; fi
timeout ${SIM_TIMEOUT:-3000} "$B/obj/tb_pet_disk" +type=$ty +dos=$R/dos$ty.bin +ctl=$R/ctl$ty.bin +dosoff=$dosoff \
  +disk=${DISK:-$B/media/disk.d64} +prg=${PRG:-$B/media/file.prg} | tail -${TAIL:-20}
