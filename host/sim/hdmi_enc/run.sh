#!/bin/sh
# T164 WP4 / S4: the host's HDMI encoder fork (host/hdmi_enc) decoded in every row of hdmi_modes.vh (tb_hdmi_enc.v), and four controls on the
# 576p row (gear 5, the tightest blanking, 144 pixels). A control passes only when its run ends in FAIL with the message of its own fault:
#   pr        the AVI InfoFrame says PR = g - 1 (4) instead of 0                                   -> "AVI PR"
#   ctstmds   the audio clock regeneration counts TMDS clocks, not pixels (ce off in the ACR file)  -> "CTS is not the pixel clock"
#   noce      tmds_channel.sv without ce (symbols and running disparity change every TMDS clock)    -> "symbol changed between two pixels"
#   fourpk    MAX_PACKETS = 4: the island ends at cx 144 and runs into the video preamble           -> "island does not leave"
# The last line is "hdmi_enc PASS" only if every row passed and every control failed the way it must.
set -u
here=$(cd "$(dirname "$0")" && pwd)
D=$here/../..
out=${HDMI_ENC_OUT:-$(mktemp -d)}
mkdir -p "$out"
SRC="hdmi.v packet_picker.sv auxiliary_video_information_info_frame.sv audio_clock_regeneration_packet.sv packet_assembler.sv tmds_channel.sv"
VENDOR="audio_sample_packet.sv audio_info_frame.sv source_product_description_info_frame.sv"
JOBS=${HDMI_ENC_JOBS:-4}

# build <name> <srcdir> <mode> <maxp>  -> $out/<name>.vvp
build() {
  files=""
  for f in $SRC; do files="$files $2/$f"; done
  for f in $VENDOR; do files="$files $D/../vendor/icepi/hdmi/$f"; done
  # shellcheck disable=SC2086
  iverilog -g2012 -I "$D" -Ptb.MODE="$3" -Ptb.MAXP="$4" -s tb -o "$out/$1.vvp" "$here/tb_hdmi_enc.v" $files || exit 2
}

# mut <name> <file> <sed expression>: a copy of the fork with one mutation, which must change the file
mut() {
  mkdir -p "$out/$1"
  cp "$D"/hdmi_enc/*.sv "$D"/hdmi_enc/hdmi.v "$out/$1/"
  sed -i.bak "$3" "$out/$1/$2"
  if cmp -s "$out/$1/$2" "$D/hdmi_enc/$2"; then echo "control $1: the mutation changed nothing"; exit 2; fi
  rm -f "$out/$1/$2.bak"
}
mut pr auxiliary_video_information_info_frame.sv 's/{YCC_QUANTIZATION_RANGE, CONTENT_TYPE, PIXEL_REPETITION}/{YCC_QUANTIZATION_RANGE, CONTENT_TYPE, 4'"'"'d4}/'
mut ctstmds audio_clock_regeneration_packet.sv 's/always_ff @(posedge clk_pixel) if (ce)/always_ff @(posedge clk_pixel)/'
mut noce tmds_channel.sv 's/@(posedge clk_pixel) if (ce)/@(posedge clk_pixel)/'

jobs=""
for m in 0 1 2 6 7 8 9 10 11; do
  build row$m "$D/hdmi_enc" $m 3; jobs="$jobs row$m"
done
build pr "$out/pr" 0 3;           jobs="$jobs pr"
build ctstmds "$out/ctstmds" 0 3; jobs="$jobs ctstmds"
build noce "$out/noce" 0 3;       jobs="$jobs noce"
build fourpk "$D/hdmi_enc" 0 4;   jobs="$jobs fourpk"

# shellcheck disable=SC2086
printf '%s\n' $jobs | nice -n 15 xargs -P "$JOBS" -I{} sh -c "timeout 3000 vvp -n $out/{}.vvp > $out/{}.log 2>&1; echo rc=\$? >> $out/{}.log"

rc=0
expect() {   # expect <control> <message>
  if grep -q "FAIL" "$out/$1.log" && grep -q "$2" "$out/$1.log"; then echo "control $1: failed as it must ($2)"
  else echo "control $1: did not fail with '$2'"; rc=1; fi
}
for j in $jobs; do
  case $j in
    row*) grep -v 'finish\|Time:' "$out/$j.log" | grep -v '^rc=' | sed "s/^/[$j] /"
          grep -q "PASS$" "$out/$j.log" || { echo "row $j: expected PASS"; rc=1; } ;;
  esac
done
expect pr "AVI PR"
expect ctstmds "CTS is not the pixel clock"
expect noce "symbol changed between two pixels"
expect fourpk "island does not leave"
if [ $rc = 0 ]; then echo "hdmi_enc PASS: 9 rows decoded, 4 controls failed as they must"; else echo "hdmi_enc FAIL"; fi
if [ -z "${HDMI_ENC_OUT:-}" ]; then rm -rf "$out"; fi
exit $rc
