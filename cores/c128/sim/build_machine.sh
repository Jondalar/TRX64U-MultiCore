#!/bin/sh
# T113: the whole C128 machine (c64_core_c128 with its chips) under Verilator, for the machine benches (run_machine.sh).
# The VHDL (6502n, MMU, T80/Z80 glue) is turned into Verilog by `ghdl synth` once per build, the rest is read as it is.
# Usage (repository root, in trx64-sim-env): sh cores/c128/sim/build_machine.sh <build dir> [extra verilator options]
# Default feature set: Z80 SID_REDIP NO_TURBO NO_REU VDC_WHOLE VDC64 TRX_SIM_ROMS (C128_SIM_DEFS replaces the list).
set -e
root=$(cd "$(dirname "$0")/../../.." && pwd)
out=${1:?build dir}; shift || true
C=$root/vendor/icepi
K=$C/vicii-kawari/hdl
X=$root/cores/c128
mkdir -p "$out/gen" "$out/run/mem"
defs=${C128_SIM_DEFS:-"Z80 SID_REDIP NO_TURBO NO_REU VDC_WHOLE VDC64 TRX_SIM_ROMS"}
vdefs=""
extra_src=""
for d in $defs; do vdefs="$vdefs -D$d"; case $d in ROM_CACHE) extra_src="$extra_src $root/cores/c128/c128_rome.v $root/cores/nes/sim/ddr2c_model.v" ;; RAM_DDR2) extra_src="$extra_src $root/cores/c128/c128_ddrx.v" ;; esac; done

n=$C/6502n
ghdl synth --std=08 -fsynopsys --out=verilog $n/pkg_6502_defs.vhd $n/pkg_6502_opcodes.vhd \
  $n/pkg_6502_decode.vhd $n/alu.vhd $n/bit_cpx_cpy.vhd $n/data_oper.vhd $n/implied.vhd \
  $n/shifter.vhd $n/proc_control.vhd $n/proc_interrupt.vhd $n/proc_registers.vhd $n/proc_core.vhd \
  -e proc_core > "$out/gen/proc_core.v" 2> "$out/gen/proc_core.log"
ghdl synth --std=08 -fsynopsys --out=verilog $C/c64_c1541/iecdrv_via6522.vhd \
  -e iecdrv_via6522 > "$out/gen/iecdrv_via6522.v" 2> "$out/gen/iecdrv_via6522.log"
ghdl synth --std=93c -fsynopsys --out=verilog $X/c128_mmu8722.vhd -e mmu8722 > "$out/gen/mmu8722.v" 2> "$out/gen/mmu8722.log"
ghdl synth --std=93c -fsynopsys -fexplicit -frelaxed-rules --out=verilog $X/z80/T80_Pack.vhd $X/z80/T80_MCode.vhd $X/z80/T80_ALU.vhd \
  $X/z80/T80_Reg.vhd $X/z80/T80.vhd $X/z80/T80pa.vhd $X/z80/cpu_z80.vhd -e cpu_z80 > "$out/gen/cpu_z80.v" 2> "$out/gen/cpu_z80.log"
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
  echo $X/vicii_c128.v
  echo $X/c64_top_c128.v $X/cpu6510_c128.v $X/c64_turbo_c64u.v $X/c64_bus_arbiter_c128.v $X/c64_pla_c128.v $X/c128_map.v $X/c128_x.v $X/c128_act.v $X/c128_iecfs.v
  echo $X/vdc/vdcram.v $X/vdc/vdc_cpu.v
  echo $X/vdc/vdc_top.sv $X/vdc/vdc_signals.sv $X/vdc/vdc_signals_h.sv $X/vdc/vdc_signals_v.sv $X/vdc/vdc_ramiface.sv $X/vdc/vdc_video.sv
  echo $X/c64_core_c128.v $X/c64_mem_c128.v $X/c64_mem_c128d.v $X/c128_ramd.v $X/dma_win_c128.v $X/c128_romc.v $X/c128_chr.v $X/cart_slot_c64u.v $X/slot_port_c64u.v
  echo $out/gen/proc_core.v $out/gen/iecdrv_via6522.v $out/gen/T65.v $out/gen/mmu8722.v $out/gen/cpu_z80.v
} > "$out/gen/files.f"

for f in $C/c64_redip_sid/reDIP-SID/gateware/*.mem $U/trx_sid/mem/*.mem $K/*.bin $K/*.hex $C/usb_hid_host/rom/*.mem; do
  if [ -f "$f" ]; then ln -sf "$f" "$out/run/"; fi
done
for f in $C/c64_c1541/mem/*.mem; do ln -sf "$f" "$out/run/mem/"; done

verilator --binary --timing -j ${JOBS:-2} -O1 --x-assign 0 --x-initial 0 $vdefs \
  -Wno-fatal -Wno-lint -Wno-style -Wno-WIDTH -Wno-PROCASSWIRE -Wno-TIMESCALEMOD \
  --top-module tb_c128 -I$C -I$K -I$X -I$C/c64_redip_sid/reDIP-SID/gateware -I$U -I$X/sim \
  --Mdir "$out/obj" -o tb_c128 \
  $C/vicii_kawari.vh $K/common.vh -f "$out/gen/files.f" $root/cores/c128/sim/tb_c128.v $extra_src "$@" > "$out/verilator.log" 2>&1 \
  || { tail -40 "$out/verilator.log"; echo "FAIL build"; exit 1; }
echo "built $out/obj/tb_c128"
