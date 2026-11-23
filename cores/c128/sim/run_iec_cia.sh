#!/bin/sh
# T170: CIA 1's serial register on the fast serial wiring (tb_iec_cia.sv) under Verilator. Run from the repository root (builder, trx64-sim-env): sh cores/c128/sim/run_iec_cia.sh [lint]
set -e
root=$(cd "$(dirname "$0")/../../.." && pwd)
C=$root/vendor/icepi
out=${TMPDIR:-/tmp}/c128_iec_cia
mkdir -p "$out"
files="$C/c64_redip_cia/reDIP-CIA/gateware/CIA/cia_pkg.sv $(ls $C/c64_redip_cia/reDIP-CIA/gateware/CIA/*.sv | grep -v cia_pkg | tr '\n' ' ') $C/c64_redip_cia/cia_core.sv $C/c64_redip_cia.v $root/cores/c128/c128_iecfs.v $root/cores/c128/sim/tb_iec_cia.sv"
opts="--timing -Wno-fatal -Wno-lint -Wno-style -Wno-WIDTH -Wno-PROCASSWIRE -Wno-TIMESCALEMOD -I$C -I$C/c64_redip_cia --top-module tb_iec_cia"
if [ "$1" = lint ]; then verilator --lint-only $opts $files; echo "lint ok"; exit 0; fi
verilator --binary -j ${JOBS:-2} -O1 --x-assign 0 --x-initial 0 $opts --Mdir "$out/obj" -o tb_iec_cia $files > "$out/verilator.log" 2>&1 || { tail -40 "$out/verilator.log"; echo "FAIL build"; exit 1; }
"$out/obj/tb_iec_cia" | tee "$out/log" | grep -E "^(FAIL|PASS|ok)"
grep -q "^PASS tb_iec_cia" "$out/log" || { echo "bench failed"; exit 1; }
echo "PASS run_iec_cia"
