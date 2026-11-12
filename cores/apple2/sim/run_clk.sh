#!/bin/sh
# T89 S3: exact Apple clocks (tb_a2_clk.vhd).
set -e
here=$(cd "$(dirname "$0")" && pwd)
. "$here/lib.sh"
out=$(a2_workdir clk)
a2_build tb_a2_clk "$out"
cd "$out"
ghdl -r $A2_GHDL_OPTS tb_a2_clk --ieee-asserts=disable 2>&1 | tee run.log | sed -n 's/^.*(report note): //p'
grep -q "PASS tb_a2_clk" run.log
echo "PASS clk"
