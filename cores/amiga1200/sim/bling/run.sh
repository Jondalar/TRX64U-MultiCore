#!/bin/sh
# T159 D3/D4: the A1200's power lamp (mono/host/bling_io.v): WS2812 frame decoded; LED 71 per state with the indicator on: red steady (idle), red flickers off
# (floppy), blue flickers (hard disk, wins over the floppy), yellow steady (SHIFT LOCK, wins over all), a 10-clock pulse still shows; Off = dark / amber;
# LEDs 1-70 dark. mono/host/top.v feeds drive_act from the synchronised case-LED
# activity (cled_s2[2]), hd_act from drive_leds_n[1] and the enable from the selector (source 4).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$here/../../../..
out=$here/build; mkdir -p "$out"
f=$root/cores/amiga1200/mono/host/bling_io.v
iverilog -g2012 -Wall -o "$out/tb_bling" "$here/tb_bling.v" "$f"
vvp -n "$out/tb_bling" | tee "$out/bling.log"
grep -q '^PASS tb_bling errors=0' "$out/bling.log"
# the instance in top.v
t=$root/cores/amiga1200/mono/host/top.v
grep -q '\.drive_act(cled_s2\[2\])' "$t"
grep -q '\.hd_act(!drive_leds_n\[1\])' "$t"
grep -q "\.lamp_en(caseled_set && caseled_sel\[3:0\] == 4'd4)" "$t"
grep -q 'cled_s1 <= {!drive_leds_n\[0\],' "$t"
grep -q 'assign LED_CASE3n = 1.b0;' "$t"
echo "PASS amiga1200_bling (lamp colours per state, flicker, hold, priorities, LEDs 1-70 dark)"
