#!/bin/sh
# T119 S1: the VC-20 core's frame writer against the 6561's own picture and a model of the host's e_ port (GHDL, the hand-written test
# program of gen_testrom.py - no Commodore ROM). Needs ghdl on PATH (the sims container has it).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
v=$root/cores/vc20/rtl
out=$here/build/pixw
mkdir -p "$out"
python3 "$here/gen_testrom.py" "$out/vc20_testrom_pkg.vhd"
G="nice -n 15 ghdl"
opts="--std=08 -fsynopsys -frelaxed -Wno-hide -Wno-shared -Wno-library"
cd "$out"
$G -i $opts --work=work \
  $v/src/t65/T65_Pack.vhd $v/src/t65/T65_ALU.vhd $v/src/t65/T65_MCode.vhd $v/src/t65/T65.vhd \
  $v/src/vc20_via6522.vhd $v/src/vic20_clocks.vhd $v/src/vic20_keyboard.vhd $v/src/m6561.vhd \
  $v/trx/vc20_ram.vhd $v/src/vic20.vhd $v/trx/vc20_glue.vhd $v/trx/vc20_regs.vhd $v/trx/vc20_pixw.vhd $v/trx/vc20_core.vhd \
  "$out/vc20_testrom_pkg.vhd" "$here/tb_vc20_pixw.vhd" > ghdl_import.log 2>&1 || { tail -20 ghdl_import.log; exit 1; }
$G -m $opts tb_vc20_pixw > ghdl_make.log 2>&1 || { tail -30 ghdl_make.log; exit 1; }
rc=0
$G -r $opts tb_vc20_pixw --stop-time=${STOP:-160ms} --ieee-asserts=disable > run.log 2>&1 || true
sed 's/^.*(report [a-z]*)://' run.log | grep -E "ok  |FAIL|PASS|frame |error|Error|     buf"
grep -q "PASS tb_vc20_pixw" run.log || rc=1
exit $rc
