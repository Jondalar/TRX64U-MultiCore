#!/bin/sh
# T189 S1-S5: the tone filter (host/trx_tone.v) with the gain behind it. Flat bit-exact, shelf gains against a double-precision reference, no wrap,
# silent when idle, HDMI and I2S see the same pair; then the controls: a filter stuck on, and swapped presets MUST fail. The wiring (host/top.v,
# ultimate_sys.vhd) is grepped as in host/sim/gain.  The coefficients in trx_tone.v must be the ones gen_coeffs.py writes.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$here/../../..
out=$here/build; mkdir -p "$out"
python3 "$here/gen_coeffs.py" check
srcs="$here/tb_tone.v $root/host/trx_tone.v $root/host/trx_gain.v"
iverilog -g2012 -o "$out/tb_tone" $srcs
vvp -n "$out/tb_tone" | tee "$out/tone.log"
grep -q '^PASS tb_tone errors=0' "$out/tone.log"
for m in TRX_TONE_MUT_STUCK TRX_TONE_MUT_SWAP; do
  iverilog -g2012 -D$m -o "$out/tb_tone_mut" $srcs
  if vvp -n "$out/tb_tone_mut" +stop | grep -q '^PASS tb_tone'; then echo "FAIL: control $m did not fail the bench"; exit 1; fi
  echo "control $m fails the bench, as it must"
done
t=$root/host/top.v
u=$root/host/ultimate_sys.vhd
wiring() { # top.v ultimate_sys.vhd
  grep -q 'trx_tone mtone (.clk(mclk), .frame_en(audio_sample_en), .sel(tone_sel), .in_l(audio_l), .in_r(audio_r), .out_l(tone_l), .out_r(tone_r));' "$1" &&
  grep -q 'trx_gain mgain (.clk(mclk), .vol(master_vol), .in_l(tone_l), .in_r(tone_r), .out_l(gain_l), .out_r(gain_r));' "$1" &&
  grep -q '\.tone_sel(tone_sel)' "$1" &&
  grep -q 'signal tone_sel_i *: std_logic_vector(7 downto 0) := X"00";' "$2" &&
  grep -q 'when X"51" => tone_sel_i <= io_req.data;' "$2" && grep -q 'tone_sel <= tone_sel_i;' "$2"
}
wiring "$t" "$u" || { echo "FAIL: host/top.v or ultimate_sys.vhd does not wire the tone preset"; exit 1; }
echo "PASS host_tone (Flat exact, shelf gains, no wrap, quiet, one pair for HDMI and I2S, controls fail; the wiring is there)"
