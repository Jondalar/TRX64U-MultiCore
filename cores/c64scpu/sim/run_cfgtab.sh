#!/bin/sh
# T248 S1: the class table bench (iverilog). By hand, on the builder, in a throwaway container:
#   docker run --rm -v <repo>:/r:ro trx64-sim-env:5 sh /r/cores/c64scpu/sim/run_cfgtab.sh
set -eu
here=$(cd "$(dirname "$0")" && pwd)
w=$(mktemp -d)
python3 "$here/cfgtab_exp.py" "$w/cfgtab_exp.hex"
cd "$w"
iverilog -g2005 -o tb "$here/tb_cfgtab.v" "$here/../scpu_cfgtab.v"
vvp tb | tee "$w/out.txt"
grep -q 'S1 PASS 4096/4096' "$w/out.txt"
