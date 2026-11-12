#!/bin/sh
# T97 5.11: structural rule for the sector buffer of paula_floppy.v. Vivado 2024.1 tied the write enables of one of two 256x16 arrays
# written by byte lanes (mem[a][15:8] <= d) to 0 on the A1200 (the floppy read back zeros, board 2026-10-22; synth_check.sh shows it
# in the netlist). Rule: every block-RAM array is 8 bits wide, has exactly ONE write statement, and no write is a part-select.
# Usage: lane_lint.sh <file.v>; exit 0 = rule holds. A proxy for synth_check.sh (which needs Vivado) that runs in the sim image.
f=$1; rc=0
if grep -nE 'fd_dma_mem_[a-z]+\[[^]]*\]\[[0-9]+:[0-9]+\] *<=' "$f"; then echo "LANE-LINT: part-select write into a sector-buffer array"; rc=1; fi
for a in $(grep -E 'ram_style *= *"block"' "$f" | sed -E 's/.*reg *(\[[^]]*\])? *([A-Za-z0-9_]+).*/\2/'); do
  w=$(grep -E "reg *(\[[^]]*\])? *$a *\[" "$f" | sed -E 's/.*reg *(\[[^]]*\])?.*/\1/')
  [ "$w" = "[7:0]" ] || { echo "LANE-LINT: $a is '$w' wide, not [7:0]"; rc=1; }
  n=$(grep -cE "(^|[^A-Za-z0-9_])$a *\[[^]]*\]( *\[[^]]*\])? *<=" "$f")
  [ "$n" = 1 ] || { echo "LANE-LINT: $a has $n write statements, not 1"; rc=1; }
done
exit $rc
