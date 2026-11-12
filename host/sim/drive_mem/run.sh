#!/bin/sh
# drive_mem's cache and snooping on mig_arb and the MIG model (tb_drive_mem.v).
# Needs ghdl and iverilog on PATH (OSS CAD Suite).
set -e
here=$(cd "$(dirname "$0")" && pwd)
D=$here/../..
"$D/../tools/gideon_sources.sh"
B=$D/../vendor/gideon/fpga/ip/busses/vhdl_source
out=$here/build
mkdir -p "$out"
ghdl synth --std=08 -fsynopsys --out=verilog $B/mem_bus_pkg.vhd $D/../cores/common/drive_mem.vhd "$here/drive_mem_flat.vhd" \
  -e drive_mem_flat > "$out/drive_mem_flat.v" 2> "$out/ghdl.log"
iverilog -g2012 -o "$out/tb.vvp" "$here/tb_drive_mem.v" "$out/drive_mem_flat.v" "$D/ddr2_port.v" "$D/sim/mig_ui_model.v"
nice -n 15 vvp -n "$out/tb.vvp" | grep -v finish
