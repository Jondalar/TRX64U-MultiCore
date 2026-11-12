#!/bin/sh
# T113: the machine benches of the C128 core (tb_c128.v on a synthetic ROM, tools/gen_c128_test.py). Run from the repository root in
# trx64-sim-env (Verilator, GHDL). Each program ends with one PASS or FAIL line;
#   sh cores/c128/sim/run_machine.sh [program ...]        default: every program of the list below
# C128_MS=<n> overrides the simulated time of every program, C128_BUILD the build directory, C128_SIM_DEFS the feature defines.
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
build=${C128_BUILD:-$here/build}
progs=${*:-"boot mmu xkeys fast c64mode iec z80back cache_boot cache_mmu cache_fast cache_c64mode cache_z80back"}
defs0=${C128_SIM_DEFS:-"Z80 SID_REDIP NO_TURBO NO_REU VDC_WHOLE VDC64 TRX_SIM_ROMS"}
# a build of its own with an extra define
mutbuild() { # mutbuild <dir> <define>
  [ -x "$1/obj/tb_c128" ] || C128_SIM_DEFS="$defs0 $2" sh "$here/build_machine.sh" "$1" || exit 1
}
if [ ! -x "$build/obj/tb_c128" ]; then
  sh "$here/build_machine.sh" "$build" || exit 1
fi
rc=0
for p in $progs; do
  gen=$p
  bin="$build/obj/tb_c128"; rd="$build/run"
  pargs=""
  case $p in
    xkeys) pargs="+XK=000100" ;;
    cache_*) gen=${p#cache_}; mutbuild "$build.cache" ROM_CACHE; bin="$build.cache/obj/tb_c128"; rd="$build.cache/run" ;;
  esac
  ms=${C128_MS:-6}
  run="$build/run_$p"
  rm -rf "$run"; mkdir -p "$run"
  cp -a "$rd/." "$run/"
  exp=$(python3 "$root/tools/gen_c128_test.py" "$gen" "$run") || { echo "FAIL $p: generator"; rc=1; continue; }
  (cd "$run" && "$bin" +MS="$ms" +EXPECT="$exp" $pargs ${C128_ARGS:-}) > "$run/log" 2>&1
  if grep -q "^PASS tb_c128" "$run/log"; then v=PASS; else v=FAIL; fi
  grep -E "^(cycles|ok |FAIL|PASS|first)" "$run/log"
  echo "$v $p"
  [ "$v" = PASS ] || rc=1
done
[ $rc = 0 ] && echo "PASS run_machine" || echo "FAIL run_machine"
exit $rc
