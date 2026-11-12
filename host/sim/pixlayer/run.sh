#!/bin/sh
# Pixel layer bench (tb_pixlayer.v) under iverilog: pix_layer, trx_io, mig_arb,
# the MIG UI model. One process, niced. About 8 video frames.
set -e
here=$(cd "$(dirname "$0")" && pwd)
D=$here/../..
out=$here/build
mkdir -p "$out"
nice -n 15 iverilog -g2012 -Ptb.FMT2=${FMT2:-0} -o "$out/tb_pixlayer.vvp" -s tb "$here/tb_pixlayer.v" "$here/tgl_client.v" \
  "${PIX:-$D/pix_layer.v}" $D/../cores/common/trx_io.v $D/ddr2_port.v $D/sim/mig_ui_model.v
nice -n 15 vvp -n "$out/tb_pixlayer.vvp" | grep -v finish
