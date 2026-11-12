#!/bin/sh
# T159 D-all: the Atari core's case LED 1 source (trx_sio_led in layer2_atari800.v: SIO traffic held 95 ms) and its wiring into drive_leds_n[0].
# The module is cut out of the shipped layer2 file.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$here/../../../..
out=$here/build; mkdir -p "$out"
l2=$root/cores/atari800/layer2_atari800.v
sed -n '/^module trx_sio_led/,/^endmodule/p' "$l2" > "$out/trx_sio_led.v"
test -s "$out/trx_sio_led.v"
grep -qF ".tap_sio(a_sio));" "$l2"
grep -qF "trx_sio_led sio_led (.clk(sys_clk), .rst(sys_rst), .sio(a_sio), .led_n(a_sio_led_n));" "$l2"
grep -qF "assign drive_leds_n = {2'b11, a_sio_led_n};" "$l2"
if grep -q "assign drive_leds_n = 3'b111;" "$l2"; then echo "FAIL: layer2 ties drive_leds_n off"; exit 1; fi
iverilog -g2012 -Wall -o "$out/tb" "$here/tb_sio_led.v" "$out/trx_sio_led.v"
vvp -n "$out/tb" | tee "$out/log"
grep -q '^PASS tb_sio_led errors=0' "$out/log"
echo "PASS atari_led (SIO traffic -> drive_leds_n[0] low for the frames and the hold)"
