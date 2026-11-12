#!/bin/sh
# T157: the joystick swap of nes_pad (bench tb_padswap) iverilog only.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=$here/build/padswap
mkdir -p "$out"
cd "$out"
src="$here/tb_padswap.v $root/cores/nes/trx/nes_pad.v"
iverilog -g2012 -o tb $src
vvp tb | tee run.log
grep -q '^PASS tb_padswap' run.log
echo "PASS run_padswap"
