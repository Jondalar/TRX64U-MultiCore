#!/bin/sh
# One run of the tear bench: job_tear.sh OUT NAME V60 FRAME_PS DRIFT_PERCENT H0 V0 NF
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
H=$root/host
C=$root/cores/c128/mono/host
out=$1; n=$2; v=$3; t=$4; dr=$5; h0=$6; v0=$7; nf=$8
per=$(( t * (100 + dr) / 100 / (520 * 330) ))
d=$out/$n; rm -rf "$d" "$out/$n.rc"; mkdir -p "$d"
nice -n 15 iverilog -g2012 -Ptb.V60=$v -Ptb.NF=$nf -Ptb.H0=$h0 -Ptb.V0=$v0 -Ptb.HTOT=520 -Ptb.VTOT=330 -Ptb.XA=100 -Ptb.YA=40 -Ptb.HACT=400 \
  -Ptb.VACT=$([ "$v" = 1 ] && echo 240 || echo 270) -Ptb.GEAR=1 -Ptb.PER=$per \
  -o "$d/tb.vvp" -s tb "$here/tb_vicpic_tear.v" "$H/sim/pixlayer/tgl_client.v" "$C/pix_layer.v" "$C/pal_ram.v" \
  $root/cores/common/trx_io.v $C/ddr2_port.v $H/sim/mig_ui_model.v > "$d/iv.log" 2>&1 || { echo 2 > "$out/$n.rc"; exit 0; }
timeout 1800 nice -n 15 vvp -n "$d/tb.vvp" > "$d/sim.log" 2>&1 || true
if grep -q "^PASS" "$d/sim.log" && ! grep -q "^FAIL" "$d/sim.log"; then echo 0 > "$out/$n.rc"; else echo 1 > "$out/$n.rc"; fi
rm -f "$d/tb.vvp"
