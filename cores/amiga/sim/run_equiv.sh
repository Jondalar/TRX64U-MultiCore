#!/bin/sh
# T85: the clock enable ce28 threaded through Minimig/fx68k changes nothing: the ideal-memory machine with ce28 always 1
# and with a sparse random ce28 shows the same frames and the same CPU counter. Prints PASS or FAIL.
set -e
here=$(cd "$(dirname "$0")" && pwd)
# Own output directory and own build directory (OUT_SUFFIX): amiga_mem builds the same SPARSE=0 machine and
# ci/sims.sh runs the two at the same time in one tree; both used build/ideal0 and the same names in /tmp
# (run 866: this bench failed, and the log it showed may have been amiga_mem's).
w=$here/build/equiv
mkdir -p "$w"
export OUT_SUFFIX=_eq
sh "$here/run_ideal.sh" 0 > "$w/ideal0.log" 2>&1 || { tail -20 "$w/ideal0.log"; echo FAIL ideal 0; exit 1; }
sh "$here/run_ideal.sh" 1 > "$w/ideal1.log" 2>&1 || { tail -20 "$w/ideal1.log"; echo FAIL ideal 1; exit 1; }
grep '^frame' "$w/ideal0.log" > "$w/f0.txt"
grep '^frame' "$w/ideal1.log" > "$w/f1.txt"
cat "$w/f0.txt"
test -s "$w/f0.txt"
if diff "$w/f0.txt" "$w/f1.txt"; then
  # the CPU must have run: the counter of the last frame is not zero
  if tail -1 "$w/f0.txt" | grep -q 'cpu_counter 0000'; then echo "FAIL cpu did not run"; exit 1; fi
  echo "PASS amiga_equiv"
else
  echo "FAIL sparse ce28 differs from dense"; exit 1
fi
