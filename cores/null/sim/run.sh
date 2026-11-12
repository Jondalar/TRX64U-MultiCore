#!/bin/sh
# T123 S1: bench null_regs (tb_null.v), under iverilog. One process at a time, niced.
#   sh cores/null/sim/run.sh
set -e
here=$(cd "$(dirname "$0")" && pwd)
core=$here/..
out=${TMPDIR:-/tmp}/null_sim.$$
mkdir -p "$out"
trap 'rm -rf "$out"' EXIT

build() {   # build <dir> <null_regs.v>
    nice -n 15 iverilog -g2012 -o "$1/tb.vvp" -s tb_null "$here/tb_null.v" "$2" "$core/layer2_null.v" 2>&1 | grep -v "timescale\|^$" || true
}

echo "=== null_regs: the bench"
build "$out" "$core/null_regs.v"
nice -n 15 vvp -n "$out/tb.vvp" | tee "$out/main.log"
grep -q '^PASS null_regs' "$out/main.log" || { echo "FAIL null_regs: the bench did not pass"; exit 1; }
