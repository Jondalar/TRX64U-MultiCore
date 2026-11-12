#!/bin/sh
# T85: the machine's rate under memory stalls. The real Minimig + fx68k behind amiga_ce/amiga_mem and the DDR2 model, a test ROM
# ("load": 4 bitplanes of DMA, a CPU that reads random words of the 512 KB chip RAM between copy words; 4.3 % of the reads miss the
# 4 KB cache, the board counted 4.6 % under Turrican 2). The rate is measured over the loaded part of the run (after frame 5).
# RD_LAT 20 is the board's Turrican 2 case: the old design is 4.3 % slow here (board: 4.6 %, 14.6 M stall clocks in 10 s, 14.3
# clocks per miss); RD_LAT 40 stands for a worse window (the old design is 6.7 % slow; LATS="60" gives 8.8 %, which the new design
# does not hold: 0.8 % behind, the stalls then take about 8 % of the clocks and the catch-up has 10 % to give). PASS = the machine is at most 3000 ppm behind real time (ticks / (clocks x 0.9)) over the window.
# Real time is possible only while the stalls take less than
# 10 % of the clocks (the machine can tick at most once per clock = 10/9 of its rate).
set -e
here=$(cd "$(dirname "$0")" && pwd)
w=$here/build/rate
mkdir -p "$w"
rc=0
for lat in ${LATS:-20 40}; do
  MODE=load LOADN=1 LOADR=4 sh "$here/run_mem.sh" $lat > "$w/new$lat.log" 2>&1 || { tail -20 "$w/new$lat.log"; echo "FAIL build/run (RD_LAT $lat)"; exit 1; }
  grep -E '^(rate|ce hold|hold by|mem):' "$w/new$lat.log" | sed "s/^/RD_LAT $lat: /"
  grep -q '^PASS rate' "$w/new$lat.log" || { echo "FAIL rate at RD_LAT $lat"; rc=1; }
done
[ $rc -eq 0 ] && echo "PASS amiga_rate"
exit $rc
