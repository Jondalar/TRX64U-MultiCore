#!/bin/sh
# trx_engine.v APPLY (tb_apply.v) under iverilog. One process, niced.
set -e
here=$(cd "$(dirname "$0")" && pwd)
out=$here/build
mkdir -p "$out"
nice -n 15 iverilog -g2012 -o "$out/tb_apply.vvp" -s tb_apply "$here/tb_apply.v" "$here/../../../cores/common/trx_engine.v"
nice -n 15 vvp -n "$out/tb_apply.vvp" "$@"
