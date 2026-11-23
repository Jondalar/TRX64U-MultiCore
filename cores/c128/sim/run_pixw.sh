#!/bin/sh
# T113 S9 "both pictures reach the host": the VDC (c128_x -DVDC_WHOLE -DVDC64) -> c128_rgbi -> c128_pixw -> a model of the host's e_ port,
# see tb_c128_pixw.sv.
# Final line PASS or FAIL. Run in the sim container (trx64-sim-env: verilator), from anywhere: sh cores/c128/sim/run_pixw.sh [build dir]
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
x=$root/cores/c128
out=${1:-$here/build/pixw}
mkdir -p "$out"
fail=0
files="$x/c128_x.v $x/c128_rgbi.v $x/c128_pixw.v $x/vdc/vdc_top.sv $x/vdc/vdc_signals.sv $x/vdc/vdc_signals_h.sv $x/vdc/vdc_signals_v.sv $x/vdc/vdc_ramiface.sv $x/vdc/vdc_video.sv $x/vdc/vdcram.v"
build() { # build <name> <extra verilator options>
  [ -x "$out/$1/Vtb_c128_pixw" ] && [ -z "$REBUILD" ] && return 0
  # -UVERILATOR: kept; vdc_video.sv's scalar-port variant is under VDC_SCALAR_BUFS now (PROVENANCE), not under VERILATOR
  nice -n 15 verilator --binary --timing -Wno-fatal -Wno-lint -Wno-style -Wno-TIMESCALEMOD -UVERILATOR --x-assign 0 --x-initial 0 -j 2 \
    -DVDC_WHOLE -DVDC64 --top-module tb_c128_pixw -I"$here" -Mdir "$out/$1" $2 "$here/tb_c128_pixw.sv" $files > "$out/$1.build.log" 2>&1 \
    || { tail -30 "$out/$1.build.log"; echo "FAIL build $1"; exit 1; }
}
build main ""
run() { # run <build> <tag> [plusargs]
  b=$1; t=$2; shift 2
  ( cd "$out" && nice -n 15 "$b/Vtb_c128_pixw" "$@" ) > "$out/$t.log" 2>&1 || { tail -5 "$out/$t.log"; echo "FAIL run $t"; exit 1; }
}
echo "== S9 (VDC -> frame writer -> DDR2 model)"
run main main $PIXW_ARGS
grep -vE "^ok " "$out/main.log"
grep -E "^ok " "$out/main.log" | sort | uniq -c | sed 's/^ *\([0-9]*\) ok  /     \1 x ok /'
grep -q "^PASS tb_c128_pixw" "$out/main.log" || fail=1
if [ $fail = 0 ]; then echo "PASS run_pixw"; else echo "FAIL run_pixw"; exit 1; fi
