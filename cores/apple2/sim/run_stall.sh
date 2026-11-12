#!/bin/sh
# T89 Q4: what the aux RAM in the DDR2 costs (tb_a2_stall.vhd): the 80 column screen, zero page + stack in aux, both - with the DDR2 read latency at the
# ends of the range the spec names (0.4 and 0.7 us = 30 and 52 ui_clk). A measurement with a limit, not a proof; the numbers go to the report.
set -e
here=$(cd "$(dirname "$0")" && pwd)
. "$here/lib.sh"
out=$(a2_workdir stall)
python3 "$here/mkchr.py" "$out/chr.rom"
python3 "$here/rom2hex.py" "$out/chr.rom" "$out/chr.hex"
python3 "$here/mkrom.py" "$here/asm/text80.s" "$out/text80.hex" > /dev/null
python3 "$here/mkrom.py" "$here/asm/auxbusy.s" "$out/auxbusy0.hex" > /dev/null
python3 "$here/mkrom.py" "$here/asm/auxbusy.s" "$out/auxbusy1.hex" COL80=1 > /dev/null
a2_build tb_a2_stall "$out"
for lat in 30 52; do
  for p in text80 auxbusy0 auxbusy1; do
    ( cd "$out" && ghdl -r $A2_GHDL_OPTS tb_a2_stall -gDIR="$out" -gPROG="$p.hex" -gC_LAT=$lat --ieee-asserts=disable 2>&1 | tee "stall_${p}_$lat.log" | sed -n 's/^.*(report note): /  /p' )
    grep -q "PASS tb_a2_stall" "$out/stall_${p}_$lat.log"
  done
done
echo "PASS stall (the limit holds)"
