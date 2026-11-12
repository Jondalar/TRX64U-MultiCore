#!/bin/sh
# T83: the VC-20 core boots a hand-written test program, in GHDL (no Commodore ROM, nothing
# outside the repository). Needs ghdl on PATH; the Mac runs it in a container:
#   container run --rm -v "$PWD":/w -w /w vc20-ghdl sh cores/vc20/sim/run.sh
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
v=$root/cores/vc20/rtl
out=$here/build
mkdir -p "$out"
python3 "$here/gen_testrom.py" "$out/vc20_testrom_pkg.vhd"
G="nice -n 15 ghdl"
opts="--std=08 -fsynopsys -frelaxed -Wno-hide -Wno-shared -Wno-library"
cd "$out"
$G -i $opts --work=work \
  $v/src/t65/T65_Pack.vhd $v/src/t65/T65_ALU.vhd $v/src/t65/T65_MCode.vhd $v/src/t65/T65.vhd \
  $v/src/vc20_via6522.vhd $v/src/vic20_clocks.vhd $v/src/vic20_keyboard.vhd $v/src/m6561.vhd \
  $v/trx/vc20_ram.vhd $v/src/vic20.vhd $v/trx/vc20_glue.vhd $v/trx/vc20_regs.vhd $v/trx/vc20_pixw.vhd $v/trx/vc20_core.vhd \
  "$out/vc20_testrom_pkg.vhd" "$here/tb_vc20.vhd" "$here/tb_vc20_ext.vhd" > ghdl_import.log 2>&1 || { tail -20 ghdl_import.log; exit 1; }
$G -m $opts tb_vc20 > ghdl_make.log 2>&1 || { tail -30 ghdl_make.log; exit 1; }
$G -m $opts tb_vc20_ext >> ghdl_make.log 2>&1 || { tail -30 ghdl_make.log; exit 1; }
$G -r $opts tb_vc20 --stop-time=${STOP:-80ms} --ieee-asserts=disable > run.log 2>&1 || true
grep -E "ok  |FAIL|PASS|error|Error" run.log | sed 's/^.*(report [a-z]*)://'
$G -r $opts tb_vc20_ext --stop-time=60ms --ieee-asserts=disable > run_ext.log 2>&1 || true
grep -E "ok  |FAIL|PASS|error|Error" run_ext.log | sed 's/^.*(report [a-z]*)://'
grep -q "PASS tb_vc20 *$" run.log && grep -q "PASS tb_vc20_ext" run_ext.log
