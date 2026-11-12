#!/bin/sh
# T89 S14: identity "APL2", windows, absent registers, the app's reset hold (tb_a2_win.vhd).
set -e
here=$(cd "$(dirname "$0")" && pwd)
. "$here/lib.sh"
out=$(a2_workdir win)
a2_build tb_a2_win "$out"
v=$(cat "$root/contract/VERSION")
( cd "$out" && ghdl -r $A2_GHDL_OPTS tb_a2_win -gCONTRACT="$v" --ieee-asserts=disable 2>&1 | tee run.log | sed -n 's/^.*(report note): /  /p' )
grep -q "PASS tb_a2_win" "$out/run.log"
echo "PASS win"
