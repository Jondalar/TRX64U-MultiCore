#!/bin/sh
# T89 S7: the keyboard (tb_a2_keys.vhd, asm/keys.s).
set -e
here=$(cd "$(dirname "$0")" && pwd)
. "$here/lib.sh"
out=$(a2_workdir keys)
python3 "$here/mkchr.py" "$out/chr.rom"
python3 "$here/rom2hex.py" "$out/chr.rom" "$out/chr.hex"
python3 "$here/mkrom.py" "$here/asm/keys.s" "$out/keys.hex" > /dev/null
a2_build tb_a2_keys "$out"
( cd "$out" && ghdl -r $A2_GHDL_OPTS tb_a2_keys -gDIR="$out" --ieee-asserts=disable 2>&1 | tee run.log | sed -n 's/^.*(report note): /  /p' )
grep -q "PASS tb_a2_keys" "$out/run.log"
echo "PASS keys"
