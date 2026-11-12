#!/bin/sh
# Drive latency to DDR2 before T11 (mig_arb etc. as of 49645bc) and now, idle
# and loaded (tb_lat.v). Needs iverilog on PATH. run.sh [us]
set -e
here=$(cd "$(dirname "$0")" && pwd)
D=$here/../..
out=$here/build
mkdir -p "$out"
git -C "$here" show 49645bc:designs/c64_basic/ddr2_port.v | \
  sed 's/module ddr2_port/module ddr2_port_old/; s/module mig_arb/module mig_arb_old/; s/module cpu_mem_bridge/module cpu_mem_bridge_old/' \
  > "$out/ddr2_port_old.v"
iverilog -g2012 -DOLD -o "$out/old.vvp" "$here/tb_lat.v" "$out/ddr2_port_old.v" "$D/sim/mig_ui_model.v"
iverilog -g2012 -o "$out/new.vvp" "$here/tb_lat.v" "$D/ddr2_port.v" "$D/sim/mig_ui_model.v"
us=${1:-2000}
for load in "" "+cpu" "+cpu +reu"; do
  nice -n 15 vvp -n "$out/old.vvp" +us=$us $load | grep -v finish
done
for load in "" "+cpu" "+cpu +reu" "+cpu +reu +cart" "+cpu +reu +cart +vid"; do
  nice -n 15 vvp -n "$out/new.vvp" +us=$us $load | grep -v finish
done
