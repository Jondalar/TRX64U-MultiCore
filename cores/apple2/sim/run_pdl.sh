#!/bin/sh
# T89 S8: the game port (tb_a2_pdl.vhd, asm/pdl.s).
set -e
here=$(cd "$(dirname "$0")" && pwd)
. "$here/lib.sh"
out=$(a2_workdir pdl)
python3 "$here/mkchr.py" "$out/chr.rom"
python3 "$here/rom2hex.py" "$out/chr.rom" "$out/chr.hex"
python3 "$here/mkrom.py" "$here/asm/pdl.s" "$out/pdl.hex" > /dev/null
a2_build tb_a2_pdl "$out"
( cd "$out" && ghdl -r $A2_GHDL_OPTS tb_a2_pdl -gDIR="$out" --ieee-asserts=disable 2>&1 | tee run.log | sed -n 's/^.*(report note): /  /p' )
grep -q "PASS tb_a2_pdl" "$out/run.log"
echo "PASS pdl"
