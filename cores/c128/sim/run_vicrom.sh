#!/bin/sh
# c128-vicglitch (issue #21): a static text screen under ROM line cache misses (programs vicrom0 / vicrom1, tools/gen_c128_test.py): every VIC frame after the setup must give
# the same pixel hash (+FRAMEHASH). Prints the distinct hashes and how often each came. PASS when one hash only.
# Run from the repository root in trx64-sim-env:  sh cores/c128/sim/run_vicrom.sh [modes: bram ddr]   (ROM cache is on in both; C128_MS default 700)
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
build=${C128_BUILD:-$here/build}
modes=${*:-"bram ddr"}
base="Z80 SID_REDIP NO_TURBO NO_REU VDC_WHOLE VDC64 TRX_SIM_ROMS ROM_CACHE"
rc=0
for m in $modes; do
  b="$build.vr_$m"
  case $m in bram) defs="$base";; ddr) defs="$base RAM_DDR2";; esac
  [ -x "$b/obj/tb_c128" ] || C128_SIM_DEFS="$defs" sh "$here/build_machine.sh" "$b" || { echo "FAIL build $m"; exit 1; }
  for p in vicrom0 vicrom1; do
    run="$b/run_$p"; rm -rf "$run"; mkdir -p "$run"; cp -a "$b/run/." "$run/"
    exp=$(python3 "$root/tools/gen_c128_test.py" $p "$run") || exit 1
    (cd "$run" && "$b/obj/tb_c128" +MS=${C128_MS:-700} +EXPECT="$exp" +FRAMEHASH +VICCHK +FRAMES ${C128_ARGS:-}) > "$run/log" 2>&1
    grep "^fh|" "$run/log" | awk -F"|" '$2>=12{c[$3]++} END{for(h in c) print "      hash " h " x " c[h]}' | sort
    n=$(grep "^fh|" "$run/log" | awk -F"|" '$2>=12{c[$3]++} END{print length(c)}')
    echo "$m $p: distinct frame hashes from frame 12 on: $n; $(grep "^romc|" "$run/log" | head -1) $(grep "^vicchk|" "$run/log" | head -1) $(grep -E "^(PASS|FAIL) tb_c128" "$run/log" | head -1)"
    [ "$n" = 1 ] || rc=1
  done
done
[ $rc = 0 ] && echo "PASS run_vicrom" || echo "FAIL run_vicrom"
exit $rc
