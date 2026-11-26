#!/bin/sh
# T164 WP8: host/hdmi_out.v end to end (tb_hdmi_out.v): 1080p50 (VIC path, MODE 2) and 576p50 (gear 5, pixel layer, MODE 0), the GTP words decoded
# back to pixels and InfoFrames.
# OUT = work directory (default: a new one), JOBS = parallel runs. The last line is "PASS hdmi_out" only if every run did what it must.
set -u
here=$(cd "$(dirname "$0")" && pwd)
D=$here/../..
out=${OUT:-$(mktemp -d "${TMPDIR:-/tmp}/t164-wp8-hdmi_out.XXXXXX")}
mkdir -p "$out"
JOBS=${JOBS:-8}
V=$D/../vendor/icepi/hdmi
SRC="$D/hdmi_timing.v $D/av_rgb.v $D/mode_req.v $D/cdce_mode.v $D/tmds_rep.v $D/vic_scaler.v $D/pal_ram.v $D/overlay.v $D/pix_layer.v $D/c64_text.v $D/dfx_decouple.v \
 $D/hdmi_enc/hdmi.v $D/hdmi_enc/packet_picker.sv $D/hdmi_enc/auxiliary_video_information_info_frame.sv $D/hdmi_enc/audio_clock_regeneration_packet.sv \
 $D/hdmi_enc/packet_assembler.sv $D/hdmi_enc/tmds_channel.sv $V/audio_sample_packet.sv $V/audio_info_frame.sv $V/source_product_description_info_frame.sv \
 $D/sim/mig_ui_model.v $here/models.v"
build() {   # <name> <hdmi_out.v> <mode>
  # shellcheck disable=SC2086
  iverilog -g2012 -I "$D" -Ptb.MODE="$3" -s tb -o "$out/$1.vvp" "$here/tb_hdmi_out.v" "$2" $SRC 2>&1 | grep -v 'sorry:'
  return 0
}
MODES=${MODES:-"2 0"}      # MODES=0: only the 576p run (a half-length run when the builder is loaded)
jobs=""
for m in $MODES; do build run$m "$D/hdmi_out.v" $m; jobs="$jobs run$m"; done
# shellcheck disable=SC2086
printf '%s\n' $jobs | nice -n 15 xargs -P "$JOBS" -I{} sh -c "cd $D && timeout ${RUN_TIMEOUT:-7000} vvp -n $out/{}.vvp > $out/{}.log 2>&1; echo rc=\$? >> $out/{}.log"
rc=0
for j in $jobs; do
  grep -v 'finish\|Time:' "$out/$j.log" | grep -v '^rc=' | sed "s/^/[$j] /"
  grep -q "PASS$" "$out/$j.log" || { echo "$j: expected PASS"; rc=1; }
done
if [ $rc = 0 ]; then echo "PASS hdmi_out (1080p50 VIC path and 576p50 gear 5 decoded from the GTP words)"; else echo "FAIL hdmi_out"; fi
if [ -z "${OUT:-}" ]; then rm -rf "$out"; fi
exit $rc
