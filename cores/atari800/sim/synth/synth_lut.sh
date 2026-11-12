#!/bin/sh
# T88 D11 / S14 (estimate, not Vivado): LUT / FF / block RAM of the whole Atari core, yosys synth_xilinx (xc7).
# ghdl --synth turns the VHDL (gen/, trx/, the 6502) into Verilog (as cores/atari800/sim/cpu/synth_lut.sh does for the CPU alone),
# yosys maps it. Top: a8_core (layer2_atari800.v around it is wires). Numbers are yosys's, not Vivado's: the block RAMs are
# KNOWN (T88 5.8): ghdl 5.0.1 dies on a8_core here (internal error in its Verilog writer, netlists-disp_verilog.adb:1336), so there is
# no whole-core figure yet. Numbers are yosys's, not Vivado's: the block RAMs are
# counted as yosys maps them (RAMB36/RAMB18), distributed RAM and the shift registers as LUTs.
#   container run --rm --cpus 2 -v "$PWD":/work -w /work vc20-ghdl sh cores/atari800/sim/synth/synth_lut.sh   (ghdl part)
#   then on the Mac: ROOT=$PWD sh cores/atari800/sim/synth/synth_lut.sh stat                                (yosys part)
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=${ROOT:-$(cd "$here/../../../.." && pwd)}
out=$here/build
mkdir -p "$out"
cd "$out"
if [ "${1:-}" != stat ]; then
  . "$root/cores/atari800/sim/files.sh"
  nice -n 15 ghdl --synth $A8OPTS --out=verilog $A8FILES -e a8_core > a8_core.v 2> ghdl_synth.log || { tail -30 ghdl_synth.log; exit 1; }
  echo "ghdl --synth: $(wc -l < a8_core.v) lines of Verilog"
fi
if [ "${1:-}" = stat ] || command -v yosys >/dev/null 2>&1; then
  nice -n 15 yosys -q -l yosys.log -p "read_verilog -sv a8_core.v; synth_xilinx -top a8_core -family xc7 -flatten; tee -o a8_core.stat stat" > /dev/null 2>&1 || { tail -30 yosys.log; exit 1; }
  awk '$1 ~ /^(LUT[1-6]|FD[RSCP]E?|CARRY4|MUXF[789]|RAMB18E1|RAMB36E1|RAM32M|RAM64M|RAM32X1D|RAM64X1D|RAM128X1D|RAM256X1D|RAM32X1S|RAM64X1S|DSP48E1|SRL.*)$/ {print "  " $1, $2} $1 ~ /^LUT[1-6]$/ {s+=$2} $1 ~ /^FD/ {f+=$2} END {print "  LUT total", s, "  FF total", f}' a8_core.stat
fi
