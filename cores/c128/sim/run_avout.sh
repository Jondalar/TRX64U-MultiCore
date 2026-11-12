#!/bin/sh
# T140 S2 (the C128's analogue output, cores/c128/mono/c128_avout.v and c128_yc.v (MiSTer yc_out.sv numbers)): sync tip, blanking, burst, bars and
# PAL alternation for the four System Modes, see tb_c128_avout.sv. Final line PASS or FAIL. Run in the sim container (trx64-sim-env: verilator), from
# anywhere: sh cores/c128/sim/run_avout.sh [build dir]
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=${1:-$here/build/avout}
mkdir -p "$out"
fail=0
files="$root/cores/c128/mono/c128_avout.v $root/cores/c128/mono/c128_yc.v $root/cores/c128/mono/host/pal_ram.v"
build() { # build <name> <extra verilator options>
  [ -x "$out/$1/Vtb_c128_avout" ] && [ -z "$REBUILD" ] && return 0
  nice -n 15 verilator --binary --timing -Wno-fatal -Wno-lint -Wno-style -Wno-TIMESCALEMOD --x-assign 0 --x-initial 0 -j 2 \
    --top-module tb_c128_avout -Mdir "$out/$1" $2 "$here/tb_c128_avout.sv" $files > "$out/$1.build.log" 2>&1 \
    || { tail -30 "$out/$1.build.log"; echo "FAIL build $1"; exit 1; }
}
run() { # run <build> <tag>
  ( cd "$out" && nice -n 15 "$1/Vtb_c128_avout" ) > "$out/$2.log" 2>&1 || { tail -5 "$out/$2.log"; echo "FAIL run $2"; exit 1; }
}
echo "== S2 (four System Modes)"
build main ""
run main main
grep -E "^(M[0-9]|FAIL)" "$out/main.log" | head -40
grep -q "^PASS tb_c128_avout" "$out/main.log" || fail=1
if [ $fail = 0 ]; then echo "PASS run_avout"; else echo "FAIL run_avout"; exit 1; fi
