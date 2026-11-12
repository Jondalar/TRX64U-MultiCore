#!/bin/sh
# One run of the T194 S2 bench: job_vicshow.sh OUT NAME MODE V60
# Writes OUT/NAME/{plan.log,sim.log,check.log} and OUT/NAME.rc: 0 = the check passed, 1 = it failed on a check, 2 = the run did not finish.
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
ps=$here/pixscale
pl=$root/host/sim/pixlayer
c=$root/cores/c128/mono/host
out=$1; n=$2; mode=$3; v60=$4
sh=$([ "$v60" = 1 ] && echo 240 || echo 270)
py="python3 -I $ps/model.py"
d=$out/$n; rm -rf "$d" "$out/$n.rc"; mkdir -p "$d"
$py plan "$mode" 400 "$sh" 2 "$d" full > "$d/plan.log"
nice -n 15 iverilog -g2012 -Ptb.V60=$v60 $($py params "$mode") \
  -o "$d/tb.vvp" -s tb "$here/tb_vicshow.v" "$pl/tgl_client.v" "$c/pix_layer.v" "$c/pal_ram.v" \
  $root/cores/common/trx_io.v $c/ddr2_port.v $root/host/sim/mig_ui_model.v > "$d/iv.log" 2>&1 || { echo 2 > "$out/$n.rc"; exit 0; }
timeout 4500 nice -n 15 vvp -n "$d/tb.vvp" +PLAN="$d/plan.hex" +OUT="$d" > "$d/sim.log" 2>&1 || true
if ! grep -q "^DONE tb" "$d/sim.log"; then echo 2 > "$out/$n.rc"; exit 0; fi
if $py check "$mode" 400 "$sh" 2 "$d" full > "$d/check.log" 2>&1 && ! grep -q "^FAIL" "$d/sim.log"; then echo 0 > "$out/$n.rc"; else echo 1 > "$out/$n.rc"; fi
rm -f "$d/tb.vvp"; [ "$(cat "$out/$n.rc")" != 0 ] || rm -f "$d"/f*.hex
