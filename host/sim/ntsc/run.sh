#!/bin/sh
# T40 benches (Icarus, e.g. from the OSS CAD Suite, on PATH): mode_ctl with
# cdce_i2c against an MMCM DRP model; vic_scaler (sim/tb_scaler.v) and
# udp_stream in both frame families. The KERNAL in each mode and the live
# switch: sim/lorenz (VMODE=n tb_lorenz boot, tb_lorenz modes). Output under build/.
set -e
here=$(cd "$(dirname "$0")" && pwd)
D=$here/../..
out=$here/build
mkdir -p "$out"
iverilog -g2012 -o "$out/mode_ctl.vvp" "$here/tb_mode_ctl.v" "$D/mode_ctl.v" "$D/cdce_i2c.v"
nice -n 15 vvp -n "$out/mode_ctl.vvp" | grep -v finish
for v in "" -DV60; do
  iverilog -g2012 $v -o "$out/scaler.vvp" "$D/sim/tb_scaler.v" "$D/vic_scaler.v"
  nice -n 15 vvp -n "$out/scaler.vvp" | grep -v "finish\|mismatch"
  iverilog -g2012 $v -o "$out/udp.vvp" "$here/tb_udp_stream.v" "$D/udp_stream.v"
  nice -n 15 vvp -n "$out/udp.vvp" | grep -v finish
done
