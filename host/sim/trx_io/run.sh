#!/bin/sh
# The app's side of our IO block (tb_trx_io.v) under iverilog: trx_io.v in the
# app's clock with trx_sys.v in the C64's. One process, niced.
set -e
here=$(cd "$(dirname "$0")" && pwd)
D=$here/../..
out=$here/build
mkdir -p "$out"
nice -n 15 iverilog -g2012 -o "$out/tb_trx_io.vvp" -s tb_trx_io "$here/tb_trx_io.v" $D/../cores/common/trx_io.v $D/../cores/common/trx_sys.v
nice -n 15 vvp -n "$out/tb_trx_io.vvp"
