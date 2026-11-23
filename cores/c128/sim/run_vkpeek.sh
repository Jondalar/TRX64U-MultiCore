#!/bin/sh
# T257 S4/S5/S6 "the VDC read-back": c128_regs.v + c128_x.v (-DVDC_WHOLE -DVDC64), see tb_c128_vkpeek.sv. Final line PASS or FAIL.
# Run in the sim container (trx64-sim-env: verilator), from anywhere: sh cores/c128/sim/run_vkpeek.sh [build dir]
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
x=$root/cores/c128
out=${1:-$here/build/vkpeek}
mkdir -p "$out"
files="$x/c128_x.v $x/c128_regs.v $x/vdc/vdc_top.sv $x/vdc/vdc_signals.sv $x/vdc/vdc_signals_h.sv $x/vdc/vdc_signals_v.sv $x/vdc/vdc_ramiface.sv $x/vdc/vdc_video.sv $x/vdc/vdcram.v"
[ -x "$out/main/Vtb_c128_vkpeek" ] && [ -z "$REBUILD" ] || {
  # -UVERILATOR: vdc_video.sv has a scalar-port variant for Verilator that does not match vdc_top.sv's array connection
  nice -n 15 verilator --binary --timing -Wno-fatal -Wno-lint -Wno-style -Wno-TIMESCALEMOD -UVERILATOR --x-assign 0 --x-initial 0 -j 2 \
    -DVDC_WHOLE -DVDC64 --top-module tb_c128_vkpeek -I"$here" -Mdir "$out/main" "$here/tb_c128_vkpeek.sv" $files > "$out/build.log" 2>&1 \
    || { tail -30 "$out/build.log"; echo "FAIL build"; exit 1; }
}
( cd "$out" && nice -n 15 "main/Vtb_c128_vkpeek" ) > "$out/main.log" 2>&1 || { tail -5 "$out/main.log"; echo "FAIL run"; exit 1; }
grep -vE "^ok " "$out/main.log"
echo "     $(grep -c '^ok ' "$out/main.log") checks ok"
grep -q "^PASS tb_c128_vkpeek" "$out/main.log" && echo "PASS run_vkpeek" || { echo "FAIL run_vkpeek"; exit 1; }
