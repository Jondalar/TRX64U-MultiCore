#!/bin/sh
# T159 D-all: the A500's case LED 1 source through the chipset, not just the module: the A500 core (fx68k, its shared minimig.v / nanomig.v / amiga_core.v) runs the
# disk test ROM (CIAB PRB: motor on, then select DF0, then a track by disk DMA) and amiga_core's drv_led (-> layer2 a_drv_led -> drive_leds_n[0]) must be
# low after reset and go high (tb_l2.sv MOTORCHK). The wiring behind it is the same layer2_amiga.v line the A1200's sim/motorwire checks.
set -e
here=$(cd "$(dirname "$0")" && pwd)
out=$here/build; mkdir -p "$out"
l2=$here/run_l2.sh
grep -qF "assign drive_leds_n = {1'b1, ~a_drv_hd, ~a_drv_led};" "$here/../layer2_amiga.v"
FRAMES=18 DISK=1 MODE=disk MOTORCHK=1 OUT_SUFFIX=a5motor sh "$l2" > "$out/a5motor.log" 2>&1 || { tail -20 "$out/a5motor.log"; echo "FAIL build/run"; exit 1; }
grep -E 'drv_led|FAIL|PASS' "$out/a5motor.log"
grep -q 'PASS tb_l2 errors=0' "$out/a5motor.log"
grep -q 'drv_led high after the ROM' "$out/a5motor.log"
echo "PASS amiga_motorpath (A500 drv_led rises with the ROM's motor-on + select)"
