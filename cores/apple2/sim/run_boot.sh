#!/bin/sh
# T89 S2/S4: the Enhanced Apple //e boots to the "]" prompt with the real ROM images (third party, outside the repository).
#   A2_ROMS=<dir with apple2e.rom apple2e-video.rom (+ diskii.rom with A2_DISK=1: the Disk II ROM is in slot 6, no disk, the app presses CTRL-RESET; A2_DDR=1: aux RAM in the DDR2)> sh cores/apple2/sim/run_boot.sh      (in a container with ghdl; the Mac: container run vc20-ghdl)
set -e
here=$(cd "$(dirname "$0")" && pwd)
. "$here/lib.sh"
: "${A2_ROMS:?A2_ROMS: directory with apple2e.rom and apple2e-video.rom}"
out=$(a2_workdir boot)
python3 "$here/rom2hex.py" "$A2_ROMS/apple2e.rom" "$out/apple2e.hex"
python3 "$here/rom2hex.py" "$A2_ROMS/apple2e-video.rom" "$out/apple2e-video.hex"
[ -z "$A2_DISK" ] || python3 "$here/rom2hex.py" "$A2_ROMS/diskii.rom" "$out/diskii.hex"
a2_build tb_a2_boot "$out"
cd "$out"
ghdl -r $A2_GHDL_OPTS tb_a2_boot -gROMDIR="$out" -gDISK="${A2_DISK:-0}" -gAUX_DDR="${A2_DDR:-0}" --ieee-asserts=disable 2>&1 | tee run.log | grep --line-buffered -E "report|PASS|FAIL|Error|error|cpu address|t=" | sed 's/^.*(report note): //' 
grep -q "PASS tb_a2_boot banner and prompt" run.log && grep -q "PASS tb_a2_boot main RAM POKE" run.log && grep -q "PASS tb_a2_boot aux RAM POKE" run.log && grep -q "PASS tb_a2_boot PRINT PEEK(768) shows 85" run.log && ! grep -q "FAIL tb_a2_boot" run.log || { echo "FAIL boot"; exit 1; }
echo "PASS boot"
