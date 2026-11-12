#!/bin/sh
# T85: behind amiga_mem (line cache, elastic stall) and a DDR2 model the machine is the same machine: the frames and the
# CPU counter equal the ideal-memory run's, for the light and the DMA-heavy test ROM; reports the stall rate (S4).
set -e
here=$(cd "$(dirname "$0")" && pwd)
# Own output directory and own build directory (OUT_SUFFIX), see run_equiv.sh: the two benches run at the
# same time and used to write the same names in /tmp.
w=$here/build/membench
mkdir -p "$w"
export OUT_SUFFIX=_mb
sh "$here/run_ideal.sh" 0 > "$w/ideal0.log" 2>&1 || { tail -20 "$w/ideal0.log"; echo FAIL ideal; exit 1; }
grep '^frame' "$w/ideal0.log" > "$w/f0.txt"
for lat in 8 20; do
  sh "$here/run_mem.sh" $lat > "$w/mem$lat.log" 2>&1 || { tail -20 "$w/mem$lat.log"; echo FAIL mem $lat; exit 1; }
  grep '^frame' "$w/mem$lat.log" > "$w/fm$lat.txt"
  diff "$w/f0.txt" "$w/fm$lat.txt" || { echo "FAIL frames differ at RD_LAT $lat"; exit 1; }
  grep -E 'speed|hold|mem:' "$w/mem$lat.log"
done
MODE=stress sh "$here/run_mem.sh" 8 > "$w/mems.log" 2>&1 || { tail -20 "$w/mems.log"; echo FAIL stress; exit 1; }
grep -E 'speed|hold|mem:' "$w/mems.log"
grep -q 'PASS tb_mem' "$w/mems.log"
echo "PASS amiga_mem"
