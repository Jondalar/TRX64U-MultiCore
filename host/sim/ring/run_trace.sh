#!/bin/sh
# trx_engine.v's trace former (tb_eng_trace.v) under iverilog. One process, niced.
set -e
here=$(cd "$(dirname "$0")" && pwd)
out=$here/build
mkdir -p "$out"
nice -n 15 iverilog -g2012 -o "$out/tb_eng_trace.vvp" -s tb_eng_trace "$here/tb_eng_trace.v" "$here/../../../cores/common/trx_engine.v"
nice -n 15 vvp -n "$out/tb_eng_trace.vvp"
