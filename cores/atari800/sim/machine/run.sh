#!/bin/sh
# T88: the whole Atari core under GHDL, loaded as the app loads it, with a test OS and test images of our own (no Atari ROM):
#   run.sh boot      128 KB RAM, no cartridge: BASIC at $A000, the extended banks through the DDR2 cache, keys, joystick, sound, picture
#   run.sh cart8     an 8 KB cartridge at $A000            run.sh cart16   a 16 KB cartridge at $8000-$BFFF
#   run.sh cartmax   a 128 KB AtariMax cartridge (type 2): the bank register at $D5xx
# Needs ghdl; the Mac runs it in a container:
#   container run --rm --cpus 2 -v "$PWD":/work -w /work -e IN_CONTAINER=1 vc20-ghdl sh cores/atari800/sim/machine/run.sh boot
# The images are made on the host (64tass, python) when ghdl is not there and the script starts the container itself.
# To stop a run, stop the CONTAINER (`container list`, `container stop <id>`). Killing this script
# or its `container run` only takes the client away: the simulation keeps running inside and keeps
# writing its trace file, and it will overwrite the trace of the next run (2026-10-14: a zombie from
# a killed batch rewrote free.trace with 424 lines of a different cycle budget, and the comparison
# then said Freeze changes the machine - it did not).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
what=${1:-boot}
[ $# -gt 0 ] && shift            # what is left are extra generics for ghdl -r
out=$here/build
mkdir -p "$out"
if ! command -v ghdl >/dev/null 2>&1; then
  64tass -q -b --nostart -o "$out/testos2.rom" "$here/testos2.asm"
  python3 "$here/gen_images.py" "$out" "$out/testos2.rom"
  python3 "$here/rom2hex.py" "$root/cores/atari800/roms/atarixl.rom" "$out/osxl.hex" > /dev/null
  python3 "$here/rom2hex.py" "$root/cores/atari800/roms/ataribas.rom" "$out/basxl.hex" > /dev/null
  exec nice -n 15 container run --rm --cpus 2 -v "$root":/work -w /work -e IN_CONTAINER=1 ${FRAMES:+-e FRAMES=$FRAMES} ${STOP:+-e STOP=$STOP} vc20-ghdl sh /work/cores/atari800/sim/machine/run.sh "$what" "$@"
fi
if [ -z "$IN_CONTAINER" ]; then
  64tass -q -b --nostart -o "$out/testos2.rom" "$here/testos2.asm"
  python3 "$here/gen_images.py" "$out" "$out/testos2.rom"
  python3 "$here/rom2hex.py" "$root/cores/atari800/roms/atarixl.rom" "$out/osxl.hex" > /dev/null
  python3 "$here/rom2hex.py" "$root/cores/atari800/roms/ataribas.rom" "$out/basxl.hex" > /dev/null
fi
. "$root/cores/atari800/sim/files.sh"
cd "$out"
W=$out/work
mkdir -p "$W"
cd "$W"
rm -f ./*.cf
nice -n 15 ghdl -i $A8OPTS --work=work $A8FILES "$here/tb_a8_pkg.vhd" "$here/tb_a8_machine.vhd" > import.log 2>&1 || { tail -20 import.log; exit 1; }
nice -n 15 ghdl -m $A8OPTS tb_a8_machine > make.log 2>&1 || { tail -30 make.log; exit 1; }
G="-gOS_HEX=$out/testos2.hex -gBASIC_HEX=$out/basic.hex"
case "$what" in
  boot)    G="$G -gRAMSEL=1 -gFRAMES=${FRAMES:-8} -gPPM=$out/frame.ppm" ;;
  cart8)   G="$G -gRAMSEL=0 -gFRAMES=${FRAMES:-6} -gCART_HEX=$out/cart8.hex -gCART_MODE=1" ;;
  cart16)  G="$G -gRAMSEL=0 -gFRAMES=${FRAMES:-6} -gCART_HEX=$out/cart16.hex -gCART_MODE=33" ;;
  cartmax) G="$G -gRAMSEL=0 -gFRAMES=${FRAMES:-6} -gCART_HEX=$out/cartmax.hex -gCART_MODE=2" ;;
  # the real machine: the XL OS and BASIC out of cores/atari800/roms (Alex allowed them into the
  # repository on 2026-10-14) instead of testos2, so TESTOS=0 drops every check about testos2's own
  # RAM. What is left is the picture (build/frame.ppm), the frame timing and the DDR2 window.
  os)      G="-gOS_HEX=$out/osxl.hex -gBASIC_HEX=$out/basxl.hex -gTESTOS=0"
           G="$G -gRAMSEL=1 -gFRAMES=${FRAMES:-30} -gPPM=$out/frame.ppm" ;;
  *) echo "run.sh boot|cart8|cart16|cartmax|os"; exit 2 ;;
esac
nice -n 15 ghdl -r $A8OPTS tb_a8_machine $G "$@" --ieee-asserts=disable --stop-time=${STOP:-3000ms} > "run_$what.log" 2>&1 || true
grep -E "ok  |FAIL|PASS|error|Error|tone|cache|frames run|loaded|cartridge" "run_$what.log" | sed 's/^.*(report [a-z]*)://'
grep -q "PASS tb_a8_machine" "run_$what.log"
