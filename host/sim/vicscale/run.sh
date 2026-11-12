#!/bin/sh
# T164 WP8, scenario S8: the VIC raster stays locked in 1080p (tb_vicscale.v). vic_scaler (EXT = 1) on hdmi_timing's counters, a PAL (63 x 312, row 2)
# and an NTSC (65 x 263, row 8) raster model in the exact clock ratio of the locked rows, FRAMES (default 100) output frames each. S8 names 1,000 frames: that is about 12 hours per row in iverilog (45 s a frame),
# FRAMES=1000 runs it; the position check is exact, so a clock one part in 10,000 off is caught in the first frames (control slip).
# Controls, each must FAIL with a check of its own:
#   slip     the TMDS clock one part in 10,000 slow (row 2)            -> "slipped against the VIC frame"
#   noalign  hdmi_timing ignores the align pulse (row 2)               -> a pixel check (the window does not hold one VIC frame)
#   slip60   as slip, row 8
# FRAMES, CTL_FRAMES (controls, default 40), JOBS, OUT in the environment. The last line is "PASS vicscale" only if both rows pass and every control fails.
set -u
here=$(cd "$(dirname "$0")" && pwd)
D=$here/../..
out=${OUT:-$(mktemp -d "${TMPDIR:-/tmp}/t164-wp8-vicscale.XXXXXX")}
mkdir -p "$out"
FRAMES=${FRAMES:-100}
JOBS=${JOBS:-5}
CF=${CTL_FRAMES:-40}      # the controls fail within a few frames; they need not run all
SRC="$D/vic_scaler.v $D/pal_ram.v"
build() {   # <name> <hdmi_timing.v> <row> <slip>
  iverilog -g2012 -I "$D" -Ptb.ROW="$3" -Ptb.SLIP="$4" -Ptb.FRAMES="${5:-$FRAMES}" -s tb -o "$out/$1.vvp" "$here/tb_vicscale.v" "$2" $SRC || exit 2
}
sed 's/wire do_align = align \&\& hm_locked(m);/wire do_align = 1'"'"'b0;/' "$D/hdmi_timing.v" > "$out/ht_noalign.v"
cmp -s "$out/ht_noalign.v" "$D/hdmi_timing.v" && { echo "control noalign: the mutation changed nothing"; exit 2; }
build r2 "$D/hdmi_timing.v" 2 0
build r8 "$D/hdmi_timing.v" 8 0
build slip "$D/hdmi_timing.v" 2 1 "$CF"
build slip60 "$D/hdmi_timing.v" 8 1 "$CF"
build noalign "$out/ht_noalign.v" 2 0 "$CF"
printf '%s\n' r2 r8 slip slip60 noalign | nice -n 15 xargs -P "$JOBS" -I{} sh -c "timeout ${RUN_TIMEOUT:-14000} vvp -n $out/{}.vvp > $out/{}.log 2>&1; echo rc=\$? >> $out/{}.log"
rc=0
for r in r2 r8; do
  grep -v 'finish\|Time:\|^rc=' "$out/$r.log" | sed "s/^/[$r] /"
  grep -q "PASS$" "$out/$r.log" || { echo "$r: expected PASS"; rc=1; }
done
expect() {   # <control> <message>
  if grep -q "FAIL$" "$out/$1.log" && grep -q "$2" "$out/$1.log"; then echo "control $1: failed as it must ($2)"
  else echo "control $1: did not fail with '$2'"; tail -3 "$out/$1.log"; rc=1; fi
}
expect slip "slipped against the VIC frame"
expect slip60 "slipped against the VIC frame"
expect noalign "colour index"
if [ $rc = 0 ]; then echo "PASS vicscale ($FRAMES frames of 1080p50 PAL and 1080p60 NTSC locked, no slip; 3 controls failed as they must)"; else echo "FAIL vicscale"; fi
if [ -z "${OUT:-}" ]; then rm -rf "$out"; fi
exit $rc
