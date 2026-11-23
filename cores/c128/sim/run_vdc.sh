#!/bin/sh
# T113 S8 "the VDC is a VDC": the MiSTer VDC (c128_x -DVDC_WHOLE -DVDC64) against the independent model vdc_model.py, three programs
# (registers / data port / fill / copy / busy / 64 KiB wrap; 80 x 25 text with attributes; 640 x 200 bitmap). Final line PASS or FAIL.
# Run in the sim container (trx64-sim-env: verilator, python3), from anywhere: sh cores/c128/sim/run_vdc.sh [build dir]
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
x=$root/cores/c128
out=${1:-$here/build/vdc}
mkdir -p "$out"
fail=0
files="$x/c128_x.v $x/vdc/vdc_top.sv $x/vdc/vdc_signals.sv $x/vdc/vdc_signals_h.sv $x/vdc/vdc_signals_v.sv $x/vdc/vdc_ramiface.sv $x/vdc/vdc_video.sv $x/vdc/vdcram.v"
build() { # build <name> <defines>
  [ -x "$out/$1/Vtb_c128_vdc" ] && [ -z "$REBUILD" ] && return 0
  # -UVERILATOR: vdc_video.sv has a scalar-port variant for Verilator that does not match vdc_top.sv's array connection
  nice -n 15 verilator --binary --timing -Wno-fatal -Wno-lint -Wno-style -Wno-TIMESCALEMOD -UVERILATOR --x-assign 0 --x-initial 0 -j 2 \
    --top-module tb_c128_vdc -I"$here" -Mdir "$out/$1" $2 "$here/tb_c128_vdc.sv" $files > "$out/$1.build.log" 2>&1 \
    || { tail -30 "$out/$1.build.log"; echo "FAIL build $1"; exit 1; }
}
build v64 "-DVDC_WHOLE -DVDC64"
build mut "-DVDC_WHOLE -DVDC64 -DVDC_NO_HSS_OFFSET"
python3 "$here/vdc_model.py" gen "$out" || exit 1
run() { # run <build> <test> <tag> [plusargs]
  b=$1; n=$2; t=$3; shift 3
  ( cd "$out" && nice -n 15 "$b/Vtb_c128_vdc" +stim="$out/stim_$n.hex" +out="$out/r${n}_$t" "$@" ) > "$out/r${n}_$t.log" 2>&1 \
    || { tail -5 "$out/r${n}_$t.log"; echo "FAIL tb run $t test $n"; exit 1; }
  grep -E "^(FAIL|tb:)" "$out/r${n}_$t.log"
}
for n in 1 2 3 4 5 6 7 8 9 10; do
  echo "== S8 test $n (64 KiB VDC)"
  run v64 $n main
  opt=""
  python3 "$here/vdc_model.py" check "$out" $n "$out/r${n}_main" $opt > "$out/chk${n}_main.log"
  grep -v "^ok " "$out/chk${n}_main.log"
  echo "     $(grep -c '^ok ' "$out/chk${n}_main.log") checks ok"
  if grep -q "^FAIL" "$out/chk${n}_main.log"; then fail=1; fi
done
# control (T113 D1): the same program 4 (R25 low nibble 0) on the VDC without the window offset must NOT equal the model
echo "== S8 control: test 4 without the D1 window offset (must differ)"
run mut 4 mut
if python3 "$here/vdc_model.py" check "$out" 4 "$out/r4_mut" > "$out/chk4_mut.log"; then echo "FAIL control: the mutant without the offset equals the model"; fail=1;
else echo "     control ok: the mutant differs from the model ($(grep -o '[0-9]* pixels differ' "$out/chk4_mut.log"))"; fi
if [ $fail = 0 ]; then echo "PASS run_vdc"; else echo "FAIL run_vdc"; exit 1; fi
