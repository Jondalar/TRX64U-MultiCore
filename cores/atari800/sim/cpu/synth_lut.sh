#!/bin/sh
# T88 §5: LUT/FF count of the 6502 candidates, yosys synth_xilinx (xc7), flattened. Same method as T21's
# "proc_core alone: 675 LUT" figure. ghdl --synth turns VHDL into Verilog (as designs/c64_basic/sim/lorenz/build.sh).
#   synth_lut.sh   (in the vc20-ghdl container: container run ... sh /work/cores/atari800/sim/cpu/synth_lut.sh)
set -e
root=${ROOT:-/work}
N=$root/vendor/icepi/6502n
A=$root/cores/atari800
T=$root/vendor/icepi/T65
out=$root/cores/atari800/sim/cpu/${BUILD:-build}/synth
mkdir -p $out; cd $out
F="--std=08 -frelaxed -fsynopsys"
ghdl --synth $F --out=verilog $N/pkg_6502_defs.vhd $N/pkg_6502_opcodes.vhd $N/pkg_6502_decode.vhd $N/alu.vhd \
    $N/bit_cpx_cpy.vhd $N/shifter.vhd $N/implied.vhd $N/data_oper.vhd $N/proc_control.vhd $N/proc_interrupt.vhd \
    $N/proc_registers.vhd $N/proc_core.vhd $A/proc_interrupt_a8.vhd $A/proc_core_a8.vhd $A/cpu.vhd -e cpu > cpu_adapter.v
# T65 has a signal called "break" (a Verilog keyword): renamed in a scratch copy
for f in T65_Pack T65_MCode T65_ALU T65; do sed 's/\bbreak\b/brk_i/g' $T/$f.vhd > $f.vhd; done
ghdl --synth $F --out=verilog T65_Pack.vhd T65_MCode.vhd T65_ALU.vhd T65.vhd -e t65 > t65.v
ghdl --synth $F --out=verilog $N/pkg_6502_defs.vhd $N/pkg_6502_opcodes.vhd $N/pkg_6502_decode.vhd $N/alu.vhd \
    $N/bit_cpx_cpy.vhd $N/shifter.vhd $N/implied.vhd $N/data_oper.vhd $N/proc_control.vhd $N/proc_interrupt.vhd \
    $N/proc_registers.vhd $N/proc_core.vhd -e proc_core > proc_core.v 2>/dev/null
for m in "cpu cpu_adapter.v" "T65 t65.v" "proc_core proc_core.v"; do
  set -- $m
  echo "== $1"
  yosys -q -p "read_verilog $2; synth_xilinx -top $1 -family xc7 -flatten; tee -o $1.stat stat" > /dev/null 2>&1
  awk '$1 ~ /^(LUT[1-6]|FD[RSCP]E|CARRY4|MUXF[78]|RAM.*)$/ {print "  " $1, $2} $1 ~ /^LUT[1-6]$/ {s+=$2} END {print "  LUT total", s}' $1.stat
done
