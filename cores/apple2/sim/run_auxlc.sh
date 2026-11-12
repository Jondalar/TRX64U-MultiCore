#!/bin/sh
# T89 S11 + Q4: aux RAM, ALTZP, 80STORE/PAGE2, the language card; the DDR2 variant is the same machine as the block RAM variant (tb_a2_auxlc.vhd).
set -e
here=$(cd "$(dirname "$0")" && pwd)
. "$here/lib.sh"
out=$(a2_workdir auxlc)
python3 "$here/mkchr.py" "$out/chr.rom"
python3 "$here/rom2hex.py" "$out/chr.rom" "$out/chr.hex"
python3 "$here/mkrom.py" "$here/asm/auxlc.s" "$out/auxlc.hex" > /dev/null
a2_build tb_a2_auxlc "$out"
( cd "$out" && ghdl -r $A2_GHDL_OPTS tb_a2_auxlc -gDIR="$out" --ieee-asserts=disable 2>&1 | tee run.log | sed -n 's/^.*(report note): /  /p' )
grep -q "PASS tb_a2_auxlc" "$out/run.log"
echo "PASS auxlc"
