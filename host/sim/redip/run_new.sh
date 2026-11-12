#!/bin/sh
# T25.4/T25.5: the benches that need no reference stream (the hardware configuration of sid_multi,
# 8 SIDs: LEGACY_FC = 2, variant A, reDIP's pulse behaviour): tb_new (cutoff 5 * FC / function of
# FC, base, offset, RES tables, DIGI, sums, ENV3 / level readback, reset) and tb_sym (all SIDs are the same SID).
# Needs verilator on PATH. Niced, one job. Prints one PASS/FAIL line per run.
set -e
here=$(cd "$(dirname "$0")" && pwd)
sh "$here/build_new.sh"
b=$here/build
fail=0
# random filter source / Auto / 6581 base and offset per UltiSID (5 * FC, the own 6581 curve is a
# function of FC, base and offset, resonance and digis Auto follow the chip model)
for s in 1 2 3 4 5 6 11 12 13 14 15 16 17 18; do
  (cd "$b/new8fc2/run" && nice -n 15 ../tb_new $s 30000) || fail=$((fail+1))
done
# PULSE_FIX = 1 (the parameter, not the hardware default): all eight SIDs are the same SID
for s in 1 2 3; do
  (cd "$b/sym1/run" && nice -n 15 ../tb_sym $s 100000 0) || fail=$((fail+1))
done
# PULSE_FIX = 0 (hardware, Alex 2026-10-07: reDIP's sound): the SIDs at the same place of a pair
# are equal, and reDIP's pulse behaviour of the second place shows with the 8580 (seeds 1, 5)
for s in 1 2 5; do
  (cd "$b/sym0/run" && nice -n 15 ../tb_sym $s 100000 2) || fail=$((fail+1))
done
[ $fail = 0 ] && echo "redip new: PASS" || { echo "redip new: $fail FAIL"; exit 1; }
