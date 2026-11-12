#!/bin/sh
# T159 D5: the host's power lamp for every core (host/bling_io.v): WS2812 frame decoded; LED 71 per state with the indicator on: red steady (idle), red flickers off
# (floppy), blue flickers (hard disk, wins over the floppy), yellow steady (SHIFT LOCK, wins over all), a 10-clock pulse still shows; Off = dark / amber;
# LEDs 1-70 dark. host/top.v feeds drive_act from the synchronised case-LED
# activity (cled_s2[2]), hd_act from drive_leds_n[1] and the enable from the selector (source 4).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$here/../../..
out=$here/build; mkdir -p "$out"
f=$root/host/bling_io.v
iverilog -g2012 -Wall -o "$out/tb_bling_lamp" "$here/tb_bling_lamp.v" "$f"
vvp -n "$out/tb_bling_lamp" | tee "$out/bling_lamp.log"
grep -q '^PASS tb_bling_lamp errors=0' "$out/bling_lamp.log"
# the instance in top.v: every wire the lamp needs must be there
t=$root/host/top.v
wiring() { # file
  grep -q '\.drive_act(cled_s2\[2\])' "$1" && grep -q '\.hd_act(!drive_leds_n\[1\])' "$1" &&
  grep -q "\.lamp_en(caseled_set && caseled_sel\[3:0\] == 4'd4)" "$1" && grep -q 'cled_s1 <= {!drive_leds_n\[0\],' "$1" &&
  grep -q 'assign LED_CASE3n = 1.b0;' "$1"
}
wiring "$t" || { echo "FAIL: host/top.v does not wire the lamp"; exit 1; }
echo "PASS host_bling_lamp (lamp colours per state, flicker, hold, priorities, LEDs 1-70 dark)"
