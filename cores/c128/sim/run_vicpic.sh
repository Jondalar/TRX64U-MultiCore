#!/bin/sh
# T194 S1: the VIC capture of the C128's pix_layer copy (tb_vicpic.v): pix_layer, mig_arb, the MIG UI model, the other DDR2 clients at full load; 50 Hz, 60 Hz, 50 Hz.
# One process, niced. Final line PASS or FAILED. Run in the sim container (trx64-sim-env: iverilog): sh cores/c128/sim/run_vicpic.sh
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
H=$root/host
C=$root/cores/c128/mono/host
out=$here/build/vicpic
mkdir -p "$out"
nice -n 15 iverilog -g2012 -o "$out/tb_vicpic.vvp" -s tb "$here/tb_vicpic.v" "$H/sim/pixlayer/tgl_client.v" \
  "$C/pix_layer.v" "$C/pal_ram.v" "$C/ddr2_port.v" "$H/sim/mig_ui_model.v"
nice -n 15 vvp -n "$out/tb_vicpic.vvp" | grep -v finish | tee "$out/run.log"
grep -q "^PASS" "$out/run.log"
