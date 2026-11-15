#!/bin/sh
# T160 S1 (T164 WP11): the host's master volume (host/trx_gain.v): unity passes unchanged, 0 silent, half, saturation, channels apart.
# The wiring: top.v feeds HDMI and I2S from the gain and the gain's byte from the register, ultimate_sys resets it to unity and writes it
# at 0x50 of the mixer page.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$here/../../..
out=$here/build; mkdir -p "$out"
g=$here/../../trx_gain.v
iverilog -g2012 -o "$out/tb_gain" "$here/tb_gain.v" "$g"
vvp -n "$out/tb_gain" | tee "$out/gain.log"
grep -q '^PASS tb_gain errors=0' "$out/gain.log"
t=$root/host/top.v
u=$root/host/ultimate_sys.vhd
wiring() { # top.v ultimate_sys.vhd
  grep -q 'trx_gain mgain (.clk(mclk), .vol(master_vol), .in_l(tone_l), .in_r(tone_r), .out_l(gain_l), .out_r(gain_r));' "$1" &&
  grep -q "snd_l <= pause_mclk\[1\] ? 24'sd0 : gain_l;" "$1" && grep -q "snd_r <= pause_mclk\[1\] ? 24'sd0 : gain_r;" "$1" &&
  grep -q '\.master_vol(master_vol)' "$1" &&
  grep -q 'signal master_vol_i : std_logic_vector(7 downto 0) := X"80";' "$2" &&
  grep -q 'when X"50" => master_vol_i <= io_req.data;' "$2" && grep -q 'master_vol <= master_vol_i;' "$2"
}
wiring "$t" "$u" || { echo "FAIL: host/top.v or ultimate_sys.vhd does not wire the master volume"; exit 1; }
echo "PASS host_gain (unity exact, 0 silent, half, saturation; the wiring is there)"
