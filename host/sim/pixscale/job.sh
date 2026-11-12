#!/bin/sh
# One run of the pixscale bench: job.sh OUT NAME PIX_LAYER.V MODE SW SH FMT SMOOTH_BUILD PLAN_KIND
# Writes OUT/NAME/{plan.log,sim.log,check.log} and OUT/NAME.rc: 0 = the check passed, 1 = it failed on a check, 2 = the run did not finish.
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
pl=$root/host/sim/pixlayer
out=$1; n=$2; v=$3; mode=$4; sw=$5; sh=$6; fmt=$7; sm=$8; kind=$9
py="python3 -I $here/model.py"
d=$out/$n; rm -rf "$d" "$out/$n.rc"; mkdir -p "$d"
$py plan "$mode" "$sw" "$sh" "$fmt" "$d" "$kind" > "$d/plan.log"
nice -n 15 iverilog -g2012 -Ptb.SMOOTHP=$sm -Ptb.FMT2=$([ "$fmt" = 2 ] && echo 1 || echo 0) -Ptb.SW=$sw -Ptb.SH=$sh $($py params "$mode") \
  -o "$d/tb.vvp" -s tb "$here/tb_pixscale.v" "$pl/tgl_client.v" "$v" \
  $root/cores/common/trx_io.v $root/host/ddr2_port.v $root/host/sim/mig_ui_model.v > "$d/iv.log" 2>&1 || { echo 2 > "$out/$n.rc"; exit 0; }
timeout 4500 nice -n 15 vvp -n "$d/tb.vvp" +PLAN="$d/plan.hex" +OUT="$d" > "$d/sim.log" 2>&1 || true
if ! grep -q "^DONE tb" "$d/sim.log"; then echo 2 > "$out/$n.rc"; exit 0; fi
if $py check "$mode" "$sw" "$sh" "$fmt" "$d" "$kind" > "$d/check.log" 2>&1 && ! grep -q "^FAIL" "$d/sim.log"; then echo 0 > "$out/$n.rc"; else echo 1 > "$out/$n.rc"; fi
rm -f "$d/tb.vvp"; [ "$(cat "$out/$n.rc")" != 0 ] || rm -f "$d"/f*.hex
