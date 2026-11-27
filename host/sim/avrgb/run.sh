#!/bin/sh
# T265 P4: host/av_rgb.v with the real pix_layer (second requester), mig_arb and the MIG UI model (tb_avrgb.v). One process, niced.
set -e
here=$(cd "$(dirname "$0")" && pwd)
D=$here/../..
out=$here/build
mkdir -p "$out"
nice -n 15 iverilog -g2012 -o "$out/tb_avrgb.vvp" -s tb "$here/tb_avrgb.v" "$D/av_rgb.v" "$D/pix_layer.v" "$D/ddr2_port.v" "$D/sim/mig_ui_model.v"
nice -n 15 vvp -n "$out/tb_avrgb.vvp" | grep -v finish
