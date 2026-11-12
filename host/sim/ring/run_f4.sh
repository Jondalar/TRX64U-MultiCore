#!/bin/sh
# trx_engine.v's drain F4 (tb_eng_f4.v) under iverilog. One process, niced.
set -e
here=$(cd "$(dirname "$0")" && pwd)
out=$here/build
mkdir -p "$out"
nice -n 15 iverilog -g2012 -o "$out/tb_eng_f4.vvp" -s tb_eng_f4 "$here/tb_eng_f4.v" "$here/../../../cores/common/trx_engine.v"
nice -n 15 vvp -n "$out/tb_eng_f4.vvp"
