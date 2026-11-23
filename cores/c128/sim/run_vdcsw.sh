#!/bin/sh
# T257 fault 5: the CREST part of Risen from Oblivion VDC V2 (R25 and R27 written once per line, bitmap stride R1 + R27 = 128) through the VDC
# (c128_x -DVDC_WHOLE -DVDC64), every row against the memory line of the R27 running sum; see tb_c128_vdcsw.sv.
# THE DEMO'S DATA IS NOT IN THE REPOSITORY: VDCSW_DATA=<dir> (vram.hex, regs.txt, frames.txt; format in the bench's header). Without it: SKIP.
# Final line PASS or FAIL. Run in the sim container (trx64-sim-env: verilator, python3), from anywhere: VDCSW_DATA=<dir> sh cores/c128/sim/run_vdcsw.sh [build dir]
# VDCSW_ARGS="+dump=<file> +diag +per=<us>" go to the bench (per = the loop's own period instead of our line; a diagnostic, not part of the verdict).
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
x=$root/cores/c128
out=${1:-$here/build/vdcsw}
mkdir -p "$out"
[ -n "$VDCSW_DATA" ] || { echo "SKIP run_vdcsw: VDCSW_DATA not set (the demo's data is not in the repository)"; exit 0; }
files="$x/c128_x.v $x/vdc/vdc_top.sv $x/vdc/vdc_signals.sv $x/vdc/vdc_signals_h.sv $x/vdc/vdc_signals_v.sv $x/vdc/vdc_ramiface.sv $x/vdc/vdc_video.sv $x/vdc/vdcram.v"
[ -x "$out/main/Vtb_c128_vdcsw" ] && [ -z "$REBUILD" ] || {
  nice -n 15 verilator --binary --timing -Wno-fatal -Wno-lint -Wno-style -Wno-TIMESCALEMOD -UVERILATOR --x-assign 0 --x-initial 0 -j 2 \
    -DVDC_WHOLE -DVDC64 --top-module tb_c128_vdcsw -I"$here" -Mdir "$out/main" "$here/tb_c128_vdcsw.sv" $files > "$out/main.build.log" 2>&1 \
    || { tail -30 "$out/main.build.log"; echo "FAIL build"; exit 1; }
}
echo "== T257 fault 5 (CREST part: R25/R27 per line, 64 KiB, stride 128)"
# the loop's start after the fall of status bit 5: 54 us (the demo's own), and 16 and 32 us, where its R25 write falls inside the row (R25 in the blank part of
# the line: 54; the part's loop is no multiple of our line, so every phase of the line occurs in one picture on the board)
fail=0
for d in 54 16 32; do
  ( cd "$out" && nice -n 15 main/Vtb_c128_vdcsw +data="$VDCSW_DATA" +d0=$d $VDCSW_ARGS ) > "$out/main_$d.log" 2>&1 || { tail -5 "$out/main_$d.log"; echo "FAIL run d0=$d"; exit 1; }
  echo "-- loop starts $d us after the fall"
  grep -v "^- " "$out/main_$d.log" | grep -E "frame|^ok|^FAIL|^PASS"
  grep -q "^PASS tb_c128_vdcsw" "$out/main_$d.log" || fail=1
done
if [ $fail = 0 ]; then echo "PASS run_vdcsw"; else echo "FAIL run_vdcsw"; exit 1; fi
