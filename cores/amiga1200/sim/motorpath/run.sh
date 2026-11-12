#!/bin/sh
# T159 S4b: the motor path through the chipset, not just the module: the A1200's full core (TG68K, its own minimig.v / nanomig.v / amiga_core.v) runs the
# disk test ROM (CIAB PRB: motor on, then select DF0, then a track by disk DMA) and amiga_core's drv_led (-> layer2 a_drv_led -> drive_leds_n[0]) must be
# low after reset and go high (tb_l2.sv MOTORCHK). The wiring behind it (layer2's edits, top.v) is sim/motorwire's job.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$here/../../../..
l2=$root/cores/amiga/sim/run_l2.sh
out=$here/build; mkdir -p "$out"
TG68K=1 CHS_INIT=16 MEM_INIT=3 HDF_SECT=1 DISK=1 MODE=disk FRAMES=18 MOTORCHK=1 OUT_SUFFIX=motor sh "$l2" > "$out/motor.log" 2>&1 || { tail -20 "$out/motor.log"; echo "FAIL build/run"; exit 1; }
grep -E 'drv_led|FAIL|PASS' "$out/motor.log"
grep -q 'PASS tb_l2 errors=0' "$out/motor.log"
grep -q 'drv_led high after the ROM' "$out/motor.log"
echo "PASS amiga1200_motorpath (drv_led rises with the ROM's motor-on + select)"
