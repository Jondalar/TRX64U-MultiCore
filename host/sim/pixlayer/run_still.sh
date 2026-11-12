#!/bin/sh
# Still capture bench (tb_still.v) under iverilog: pix_layer, mig_arb, the MIG
# UI model; 50 Hz, 60 Hz, 50 Hz frames. One process, niced. About 5 minutes.
set -e
here=$(cd "$(dirname "$0")" && pwd)
D=$here/../..
out=$here/build
mkdir -p "$out"
nice -n 15 iverilog -g2012 -o "$out/tb_still.vvp" -s tb "$here/tb_still.v" "$here/tgl_client.v" \
  "${PIX:-$D/pix_layer.v}" $D/ddr2_port.v $D/sim/mig_ui_model.v
nice -n 15 vvp -n "$out/tb_still.vvp" | grep -v finish
