#!/bin/sh
# T159 S4: the A1200's case LED 1 source (trx_motor_led, rtl/minimig.v): motor of a present drive + hard disk access, latched like paula_floppy.v.
# The module is cut out of the fork itself (minimig.v needs the whole chipset). Also checks that layer2_amiga.v (drive_leds_n[0] = the LED, shared with the A500) still has its three wiring lines exactly once.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$here/../../../..
out=$here/build; mkdir -p "$out"
m=$root/cores/amiga1200/rtl/minimig.v
sed -n '/^module trx_motor_led/,/^endmodule/p' "$m" > "$out/trx_motor_led.v"
test -s "$out/trx_motor_led.v"
l2=$root/cores/amiga/layer2_amiga.v
for pat in '.drv_led(a_drv_led), .drv_hd(a_drv_hd));' "assign drive_leds_n = {1'b1, ~a_drv_hd, ~a_drv_led};" 'a_drv_led, a_drv_hd;'; do
  [ "$(grep -cF "$pat" "$l2")" = 1 ] || { echo "FAIL: layer2_amiga.v no longer has '$pat' exactly once (the A1200 and the A500 share it)"; exit 1; }
done
iverilog -g2012 -Wall -o "$out/tb_motorled" "$here/tb_motorled.v" "$out/trx_motor_led.v"
vvp -n "$out/tb_motorled" | tee "$out/motorled.log"
grep -q '^PASS tb_motorled errors=0' "$out/motorled.log"
echo "PASS amiga1200_motorled (motor latched per /SELx, absent drive ignored, hdd held, reset)"
