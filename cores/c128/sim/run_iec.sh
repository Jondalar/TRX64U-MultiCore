#!/bin/sh
# T170: the fast serial wiring (c128_iecfs.v), the bus with SRQ (iec_bus_c128.v) and the bus trace (c128_iectrace.v). Run from the repository root.
set -e
out=${TMPDIR:-/tmp}/c128_iec
mkdir -p "$out"
iverilog -g2012 -o "$out/tb" cores/c128/sim/tb_iec.v cores/c128/c128_iecfs.v cores/c128/iec_bus_c128.v cores/c128/c128_iectrace.v
vvp "$out/tb" | tee "$out/log" | grep -E "^(FAIL|PASS)"
grep -q "^PASS tb_iec" "$out/log" || { echo "bench failed"; exit 1; }
echo "PASS run_iec"
