#!/bin/sh
# T182: the whole C64SCPU machine (c64_core_c64scpu with its chips) under Verilator, for the machine benches (run_base.sh). Cut from cores/c128/sim/build_machine.sh.
# The VHDL (6502n, T65 stub, the drive's VIA) is turned into Verilog by `ghdl synth` once per build, the rest is read as it is.
# Usage (repository root, in trx64-sim-env): sh cores/c64scpu/sim/build_machine.sh <build dir> [extra verilator options]
# Default feature set: SID_REDIP NO_TURBO NO_REU TRX_SIM_ROMS SCPU_FLATMAP (the plain C64 map of steps 1-2; the SCPU map benches build with C64SCPU_SIM_DEFS without it) (C64SCPU_SIM_DEFS replaces the list). Step 2 adds: C64SCPU_CPU6510 (the 6510 of step 1 instead of the 65816:
# the reference of run_emu.sh), SCPU_MUT_SLOW (the 65816 steps in every second PHI2 cycle: the negative control of its cycle check).
set -e
root=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:?build dir}; shift || true
C=$root/vendor/icepi
K=$C/vicii-kawari/hdl
U=$root/cores/c64u
X=$root/cores/c64scpu
mkdir -p "$out/gen" "$out/run/mem"
defs=${C64SCPU_SIM_DEFS:-"SID_REDIP NO_TURBO NO_REU TRX_SIM_ROMS SCPU_FLATMAP"}
vdefs=""
for d in $defs; do vdefs="$vdefs -D$d"; done

n=$C/6502n
ghdl synth --std=08 -fsynopsys --out=verilog $n/pkg_6502_defs.vhd $n/pkg_6502_opcodes.vhd \
  $n/pkg_6502_decode.vhd $n/alu.vhd $n/bit_cpx_cpy.vhd $n/data_oper.vhd $n/implied.vhd \
  $n/shifter.vhd $n/proc_control.vhd $n/proc_interrupt.vhd $n/proc_registers.vhd $n/proc_core.vhd \
  -e proc_core > "$out/gen/proc_core.v" 2> "$out/gen/proc_core.log"
# T182 step 2: the 65C816 (cores/c64scpu/cpu, tools/scpu_vendor816.py) becomes one Verilog file the same way
for f in s816_pkg s816_bcdadder s816_addsubbcd s816_alu s816_addrgen s816_mcode s816_cpu; do s816="$s816 $X/cpu/$f.vhd"; done
ghdl synth --std=08 -fsynopsys --out=verilog $s816 -e s816_cpu > "$out/gen/s816_cpu.v" 2> "$out/gen/s816_cpu.log"
ghdl synth --std=08 -fsynopsys --out=verilog $C/c64_c1541/iecdrv_via6522.vhd \
  -e iecdrv_via6522 > "$out/gen/iecdrv_via6522.v" 2> "$out/gen/iecdrv_via6522.log"
cat > "$out/gen/T65.v" <<'EOT'
module T65(input [1:0] Mode, input Res_n, Enable, Clk, Rdy, Abort_n, IRQ_n, NMI_n, SO_n,
           output R_W_n, output [15:0] A, input [7:0] DI, output [7:0] DO, output NMI_ack);
endmodule
EOT

{
  echo $C/c64_redip_cia/reDIP-CIA/gateware/CIA/cia_pkg.sv
  ls $C/c64_redip_cia/reDIP-CIA/gateware/CIA/*.sv | grep -v cia_pkg
  echo $C/c64_redip_cia/cia_core.sv
  echo $C/c64_redip_cia/reDIP-CIA/gateware/VIA/via_pkg.sv
  ls $C/c64_redip_cia/reDIP-CIA/gateware/VIA/*.sv | grep -v via_pkg
  echo $C/c64_redip_cia/via_core.sv
  echo $C/c64_redip_sid/reDIP-SID/gateware/sid_pkg.sv
  ls $C/c64_redip_sid/reDIP-SID/gateware/*.sv | grep -v sid_pkg
  echo $C/c64_redip_sid/sid_api.sv $C/c64_redip_sid/sid_multi.sv $C/c64_redip_sid/muladd.sv
  ls $C/hdmi/*.sv
  ls $C/*.v | grep -vE '/c64_(top|turbo|bus_arbiter)\.v$'
  ls $C/cdc/*.v $C/c64_c1541/*.v $C/usb_hid_host/rtl/*.v
  echo $C/hdmi/hdmi.v $C/c64_action_replay/c64_action_replay_pla.v $C/c64_redip_sid/dc_blocker.v
  ls $U/trx_sid/rtl/*.v
  for f in videoram serration lumareg equalization divide colorreg sprites registers_scaled raster \
           pixel_sequencer matrix lightpen hires_dvi_sync_scaled hires_pixel_sequencer hires_matrix \
           hires_addressgen cycles comp_sync bus_access border addressgen_spartan; do
    echo $K/$f.v
  done
  echo $X/vicii_c64scpu.v
  echo $X/c64_top_c64scpu.v $U/c64_turbo_c64u.v $X/c64_bus_arbiter_c64scpu.v
  echo $X/c64_core_c64scpu.v $X/c64_mem_c64scpu.v $X/c64scpu_ramd.v $X/c64scpu_romc.v $X/c64scpu_chr.v $X/c64scpu_ddrx.v $U/cart_slot_c64u.v $U/slot_port_c64u.v
  echo $X/cpu65816_c64scpu.v $X/scpu_pport.v $X/scpu_mem.v $X/scpu_cache.v $X/scpu_cfgtab.v $out/gen/s816_cpu.v
  echo $out/gen/proc_core.v $out/gen/iecdrv_via6522.v $out/gen/T65.v
} > "$out/gen/files.f"

for f in $C/c64_redip_sid/reDIP-SID/gateware/*.mem $U/trx_sid/mem/*.mem $K/*.bin $K/*.hex $C/usb_hid_host/rom/*.mem; do
  if [ -f "$f" ]; then ln -sf "$f" "$out/run/"; fi
done
for f in $C/c64_c1541/mem/*.mem; do ln -sf "$f" "$out/run/mem/"; done

verilator --binary --timing -j ${JOBS:-2} -O1 --x-assign 0 --x-initial 0 $vdefs \
  -Wno-fatal -Wno-lint -Wno-style -Wno-WIDTH -Wno-PROCASSWIRE -Wno-TIMESCALEMOD \
  --top-module tb_c64scpu -I$C -I$K -I$X -I$C/c64_redip_sid/reDIP-SID/gateware -I$U -I$X/sim \
  --Mdir "$out/obj" -o tb_c64scpu \
  $C/vicii_kawari.vh $K/common.vh -f "$out/gen/files.f" $X/sim/tb_c64scpu.v $root/cores/nes/sim/ddr2c_model.v "$@" > "$out/verilator.log" 2>&1 \
  || { tail -40 "$out/verilator.log"; echo "FAIL build"; exit 1; }
echo "built $out/obj/tb_c64scpu"
