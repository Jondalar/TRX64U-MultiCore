#!/bin/sh
# T89 S6: the speaker's pitch from the audio stream (tb_a2_spk.vhd, asm/spk.s).
set -e
here=$(cd "$(dirname "$0")" && pwd)
. "$here/lib.sh"
out=$(a2_workdir spk)
python3 "$here/mkchr.py" "$out/chr.rom"
python3 "$here/rom2hex.py" "$out/chr.rom" "$out/chr.hex"
python3 "$here/mkrom.py" "$here/asm/spk.s" "$out/spk.hex" > /dev/null
a2_build tb_a2_spk "$out"
( cd "$out" && ghdl -r $A2_GHDL_OPTS tb_a2_spk -gDIR="$out" --ieee-asserts=disable 2>&1 | tee run.log | sed -n 's/^.*(report note): /  /p' )
grep -q "PASS tb_a2_spk" "$out/run.log"
echo "PASS spk"
