#!/bin/sh
# T88 S12 (D10): Freeze freezes - the whole Atari core under GHDL with random pauses against a free-running reference.
# Four runs of tb_a8_pause, each writes a hash trace (one line per 1024 granted bus cycles of the machine, chained over
# their addresses and data - not over ticks, and not over the pixel and audio fields: see tb_a8_pause):
#   free        no pause                          pauses1   random pauses (seed 1)
#   pauses2     random pauses (seed 2), another DDR2 latency and busy rate: the memory timing must not matter either
#   other       free, one byte of the BASIC image changed: the negative control, its trace must differ from `free`
# PASS needs: pauses1 == free, pauses2 == free, other != free, and no check of the bench failed (nothing moves while `halted`).
#   container run --rm --cpus 2 -v "$PWD":/work -w /work -e IN_CONTAINER=1 vc20-ghdl sh cores/atari800/sim/machine/run_pause.sh
# Arguments, in any order: case names (only those run, nothing is compared - for debugging one
# case), and -g generics, which go to every case that runs, e.g.
#   run_pause.sh other -gCTRL_SEL=0 -gCTRL_ADDR=16380     (the OS reset vector instead of BASIC)
# To stop a run, stop the CONTAINER (`container list`, `container stop <id>`). Killing this script
# or its `container run` only takes the client away: the simulation keeps running inside and keeps
# writing its trace file, and it will overwrite the trace of the next run (2026-10-14: a zombie from
# a killed batch rewrote free.trace with 424 lines of a different cycle budget, and the comparison
# then said Freeze changes the machine - it did not).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../../.." && pwd)
out=$here/build
mkdir -p "$out"
if ! command -v ghdl >/dev/null 2>&1; then
  64tass -q -b --nostart -o "$out/testos2.rom" "$here/testos2.asm"
  python3 "$here/gen_images.py" "$out" "$out/testos2.rom"
  exec nice -n 15 container run --rm --cpus 2 -v "$root":/work -w /work -e IN_CONTAINER=1 ${TICKS:+-e TICKS=$TICKS} ${STOP:+-e STOP=$STOP} vc20-ghdl sh /work/cores/atari800/sim/machine/run_pause.sh "$@"
fi
if [ -z "$IN_CONTAINER" ]; then
  64tass -q -b --nostart -o "$out/testos2.rom" "$here/testos2.asm"
  python3 "$here/gen_images.py" "$out" "$out/testos2.rom"
fi
. "$root/cores/atari800/sim/files.sh"
W=$out/work_pause
mkdir -p "$W"
cd "$W"
rm -f ./*.cf
nice -n 15 ghdl -i $A8OPTS --work=work $A8FILES "$here/tb_a8_pkg.vhd" "$here/tb_a8_pause.vhd" > import.log 2>&1 || { tail -20 import.log; exit 1; }
nice -n 15 ghdl -m $A8OPTS tb_a8_pause > make.log 2>&1 || { tail -30 make.log; exit 1; }
TICKS=${TICKS:-85000}            # granted bus cycles (tb_a8_pause TICKS), not sys_clk ticks
G="-gOS_HEX=$out/testos2.hex -gBASIC_HEX=$out/basic.hex -gTICKS=$TICKS"
run() { # name, extra generics
  name=$1; shift
  nice -n 15 ghdl -r $A8OPTS tb_a8_pause $G -gTRACE=$W/$name.trace "$@" --ieee-asserts=disable --stop-time=${STOP:-2000ms} > "$name.log" 2>&1 || true
  echo "== $name: $(grep -E 'PASS|FAIL' "$name.log" | sed 's/^.*(report [a-z]*)://' | head -3) ($(wc -l < "$W/$name.trace") trace lines)"
}
cases=""; extra=""
for a in "$@"; do
  case $a in -g*) extra="$extra $a" ;; *) cases="$cases $a" ;; esac
done
only() { [ -z "$cases" ] && return 0; for c in $cases; do [ "$c" = "$1" ] && return 0; done; return 1; }
for name in free pauses1 pauses2 other; do
  only "$name" || continue
  case $name in
    free)    run free -gFREEZE=0 -gRAMSEL=1 $extra ;;
    pauses1) run pauses1 -gFREEZE=1 -gSEED=1 -gRAMSEL=1 $extra ;;
    pauses2) run pauses2 -gFREEZE=1 -gSEED=2 -gRAMSEL=1 -gDDR_LAT=70 -gDDR_BUSY=30 $extra ;;
    other)   run other -gFREEZE=0 -gRAMSEL=1 -gCTRL_BYTE=170 $extra ;;
  esac
done
[ -z "$cases" ] || { echo "(only$cases, no comparison)"; exit 0; }
ok=1
cmp -s free.trace pauses1.trace && echo "ok   pauses1 == free ($(wc -l < free.trace) lines)" || { echo "FAIL pauses1 differs from free"; ok=0; }
cmp -s free.trace pauses2.trace && echo "ok   pauses2 == free" || { echo "FAIL pauses2 differs from free"; ok=0; }
cmp -s free.trace other.trace && { echo "FAIL the negative control (one BASIC byte changed) has the same trace: the hash does not see a difference"; ok=0; } || echo "ok   the negative control differs"
for n in free pauses1 pauses2 other; do grep -q "FAIL" $n.log && { echo "FAIL a bench check in $n"; ok=0; }; done
[ "$ok" = 1 ] && echo "PASS tb_a8_pause"
[ "$ok" = 1 ]
