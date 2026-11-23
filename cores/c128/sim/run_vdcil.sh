#!/bin/sh
# T257 fault 2: the VDC's interlaced bitmap (R8 = 3) with Busted Boulders' title registers and VRAM layout, the ruler program's four modes
# (vdc_il_ruler.asm), through the VDC (c128_x -DVDC_WHOLE -DVDC64) -> c128_rgbi -> c128_pixw -> a model of the host's e_ port; see tb_c128_vdcil.sv.
# Final line PASS or FAIL. Run in the sim container (trx64-sim-env: verilator, python3), from anywhere: sh cores/c128/sim/run_vdcil.sh [build dir]
# VDCIL_ARGS=+dump=<prefix> writes the first checked picture of every mode as <prefix>_<m>.ppm (vdcil_model.py png converts one).
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
x=$root/cores/c128
out=${1:-$here/build/vdcil}
mkdir -p "$out"
fail=0
files="$x/c128_x.v $x/c128_rgbi.v $x/c128_pixw.v $x/vdc/vdc_top.sv $x/vdc/vdc_signals.sv $x/vdc/vdc_signals_h.sv $x/vdc/vdc_signals_v.sv $x/vdc/vdc_ramiface.sv $x/vdc/vdc_video.sv $x/vdc/vdcram.v"
python3 "$here/vdcil_model.py" gen "$out" || { echo "FAIL model"; exit 1; }
build() { # build <name> <extra verilator options>
  [ -x "$out/$1/Vtb_c128_vdcil" ] && [ -z "$REBUILD" ] && return 0
  # -UVERILATOR: vdc_video.sv has a scalar-port variant for Verilator that does not match vdc_top.sv's array connection
  nice -n 15 verilator --binary --timing -Wno-fatal -Wno-lint -Wno-style -Wno-TIMESCALEMOD -UVERILATOR --x-assign 0 --x-initial 0 -j 2 \
    -DVDC_WHOLE -DVDC64 --top-module tb_c128_vdcil -I"$here" -Mdir "$out/$1" $2 "$here/tb_c128_vdcil.sv" $files > "$out/$1.build.log" 2>&1 \
    || { tail -30 "$out/$1.build.log"; echo "FAIL build $1"; exit 1; }
}
build main ""
echo "== T257 fault 2 (R8 = 3 bitmap, Busted Boulders' layout: modes A..D of vdc_il_ruler.asm)"
( cd "$out" && nice -n 15 main/Vtb_c128_vdcil +lines="$out" $VDCIL_ARGS ) > "$out/main.log" 2>&1 || { tail -5 "$out/main.log"; echo "FAIL run"; exit 1; }
cat "$out/main.log" | grep -v "^- "
grep -q "^PASS tb_c128_vdcil" "$out/main.log" || fail=1
if [ $fail = 0 ]; then echo "PASS run_vdcil"; else echo "FAIL run_vdcil"; exit 1; fi
