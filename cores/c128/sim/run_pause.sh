#!/bin/sh
# T113 S12: Freeze. The machine is held for several frames in the middle of a counting program (halt_req, the gate closes at a frame
# boundary); the run with the hold and the run without it must show the same RAM (a hash of $0200-$03FF, which holds the program's counters
# and copies of CIA 1's timer and the raster register) at the same CPU cycle, and no CPU cycle happens while the machine is halted.
# Run from the repository root in trx64-sim-env; builds with build_machine.sh when needed.
set -u
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
build=${C128_BUILD:-$here/build}
[ -x "$build/obj/tb_c128" ] || sh "$here/build_machine.sh" "$build" || exit 1
run="$build/run_pause"
rm -rf "$run"; mkdir -p "$run"; cp -a "$build/run/." "$run/"
python3 "$root/tools/gen_c128_test.py" pause "$run" > /dev/null || exit 1
N=${PAUSE_N:-20000}
tb() { (cd "$run" && "$build/obj/tb_c128" +MS=80 "$@") 2>&1; }
a=$(tb +SNAPCYC=$N | grep "^snap")
b=$(tb +PAUSE_CYC=5000 +PAUSE_NS=3000000 +SNAPCYC=$N)
ha=$(echo "$a" | sed -n 's/.*hash \([0-9a-f]*\).*/\1/p' | head -1)
hb=$(echo "$b" | grep "^snap" | sed -n 's/.*hash \([0-9a-f]*\).*/\1/p' | head -1)
echo "without hold: $a"
echo "with hold:    $(echo "$b" | grep '^snap')"
echo "$b" | grep "^held"
rc=0
if [ -n "$ha" ] && [ "$ha" = "$hb" ]; then echo "ok   same RAM at CPU cycle $N with and without the hold"; else echo "FAIL: RAM differs at cycle $N ($ha / $hb)"; rc=1; fi
hb_held=$(echo "$b" | sed -n 's/^held: \([0-9]*\) .*/\1/p')
if [ "$hb_held" = 0 ]; then echo "ok   no CPU cycle while the machine was halted"; else echo "FAIL: $hb_held CPU cycles while halted"; rc=1; fi
[ $rc = 0 ] && echo "PASS run_pause" || echo "FAIL run_pause"
exit $rc
