#!/bin/sh
# T89 S16: Freeze (tb_a2_pause.vhd, asm/busy.s).
set -e
here=$(cd "$(dirname "$0")" && pwd)
. "$here/lib.sh"
out=$(a2_workdir pause)
python3 "$here/mkchr.py" "$out/chr.rom"
python3 "$here/rom2hex.py" "$out/chr.rom" "$out/chr.hex"
python3 "$here/mkrom.py" "$here/asm/busy.s" "$out/busy.hex" > /dev/null
a2_build tb_a2_pause "$out"
( cd "$out" && ghdl -r $A2_GHDL_OPTS tb_a2_pause -gDIR="$out" -gSIGN=${SIGN:-1500000} --ieee-asserts=disable 2>&1 | tee run.log | sed -n 's/^.*(report note): /  /p' )
grep -q "PASS tb_a2_pause" "$out/run.log"
echo "PASS pause"
