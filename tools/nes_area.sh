#!/bin/sh
# T95 M4: Yosys (synth_xilinx -family xc7, flattened, I/O buffers not counted) area of the NES core's parts, one module at a time, from
# cores/nes/gen and cores/nes/trx. It is not Vivado - it counts the same primitives, usually a little high - and it cannot read ppu.sv
# (an enum with an initialiser, line 532 of the upstream file) or the machine that instantiates it, so the machine is not in the list:
# what is, is the mappers that are built (and the ones that could be), the 6502, the APU's parts (the whole APU takes Yosys minutes and
# 3 GB), and our glue. Usage: tools/nes_area.sh [module ...]
here=$(cd "$(dirname "$0")/.." && pwd)
g=$here/cores/nes/gen
t=$here/cores/nes/trx
one() { # one <module> <files...>   (CHP: extra yosys commands, e.g. chparam)
  m=$1; shift
  nice -n 15 yosys -q -p "read_verilog -sv $*; $CHP hierarchy -top $m; synth_xilinx -family xc7 -top $m -flatten; tee -o /dev/stdout stat" 2>&1 | awk -v m="$m" '
    /^ +[0-9]+ +(LUT[1-6]|FDRE|FDCE|FDPE|FDSE|RAMB36E1|RAMB18E1|RAM32M|RAM64M|RAM32X1D|RAM64X1D|RAM128X1D|RAM256X1S|RAM32X1S|RAM64X1S|DSP48E1|CARRY4|MUXF7|MUXF8|LDCE|RAMB36|RAMB18)/ { c[$2] += $1 }
    /Number of cells/ { }
    END { lut = c["LUT1"]+c["LUT2"]+c["LUT3"]+c["LUT4"]+c["LUT5"]+c["LUT6"]; ff = c["FDRE"]+c["FDCE"]+c["FDPE"]+c["FDSE"];
          ram = c["RAM32M"]+c["RAM64M"]+c["RAM32X1D"]+c["RAM64X1D"]+c["RAM128X1D"]+c["RAM256X1S"]+c["RAM32X1S"]+c["RAM64X1S"];
          printf "%-14s %5d LUT %5d FF %4d distRAM %3d RAMB36 %3d RAMB18 %2d DSP\n", m, lut, ff, ram, c["RAMB36E1"], c["RAMB18E1"], c["DSP48E1"] }'
}
list=${*:-"Mapper28 MMC1 MMC2 MMC4 MMC3 VRC24 Mapper66 Mapper34 Mapper71 Mapper79 cart_top T65_nes SquareChan TriangleChan NoiseChan DmcChan FrameCtr APUMixer nes_mem nes_pixw nes_regs nes_flags nes_audio nes_win nes_pal"}
for m in $list; do
  case $m in
    Mapper28) one $m $g/mappers/generic.sv;;
    Mapper66|Mapper34|Mapper71|Mapper79) one $m $g/mappers/generic.sv;;
    cart_top) one $m $g/regs_savestates.sv $g/cart.sv $g/mappers/generic.sv $g/mappers/MMC1.sv $g/mappers/MMC2.sv $g/mappers/MMC3.sv $g/mappers/VRC.sv;;
    MMC1) one $m $g/mappers/MMC1.sv;;
    MMC2|MMC4) one $m $g/mappers/MMC2.sv;;
    MMC3) one $m $g/mappers/MMC3.sv;;
    VRC24) one $m $g/mappers/VRC.sv;;
    T65_nes) one $m $g/t65/t65_nes.v;;
    SquareChan|TriangleChan|NoiseChan|APUMixer) one $m $g/regs_savestates.sv $g/apu.sv;;
    DmcChan) CHP="chparam -set SSREG_INDEX_DMC1 17 -set SSREG_INDEX_DMC2 18 DmcChan;" one $m $g/regs_savestates.sv $g/apu.sv;;
    FrameCtr) CHP="chparam -set SSREG_INDEX_FCT 19 FrameCtr;" one $m $g/regs_savestates.sv $g/apu.sv;;
    nes_*) one $m $t/$m.v;;
    *) echo "unknown module $m";;
  esac
done
