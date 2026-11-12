#!/bin/sh
# rv_dbg.v (tb_rv_dbg.v) under iverilog. One process, niced.
set -e
here=$(cd "$(dirname "$0")" && pwd)
out=$here/build
mkdir -p "$out"
nice -n 15 iverilog -g2012 -o "$out/tb_rv_dbg.vvp" -s tb_rv_dbg "$here/tb_rv_dbg.v" "$here/../../rv_dbg.v"
nice -n 15 vvp -n "$out/tb_rv_dbg.vvp"
