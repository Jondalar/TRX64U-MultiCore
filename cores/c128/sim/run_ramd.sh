#!/bin/sh
# T140 D25: the RAM-in-DDR2 module (c128_ramd.v + c128_ddrx.v) against the NES bench's DDR2 model and a reference memory (tb_ramd.v), at the model's latency
# and at a worst case (reads 52..92 ui clocks, 0.7-1.2 us: more than the measured 15-27 core clocks). Run from the repository root.
set -e
out=${TMPDIR:-/tmp}/c128_ramd
mkdir -p "$out"
src="cores/c128/sim/tb_ramd.v cores/c128/c128_ramd.v cores/c128/c128_ddrx.v cores/nes/sim/ddr2c_model.v"
iverilog -g2012 -o "$out/tb" $src
vvp "$out/tb" | tee "$out/log" | grep -E "^(==|   |PASS|FAIL)"
grep -q "^PASS tb_ramd" "$out/log" || { echo "bench failed"; exit 1; }
iverilog -g2012 -DLATMIN=52 -DLATRND=40 -o "$out/tb_slow" $src
vvp "$out/tb_slow" > "$out/log_slow"
grep -E "^(PASS|FAIL)" "$out/log_slow" | sed 's/^/slow DDR2 (52..92 ui clocks): /'
grep -q "^PASS tb_ramd" "$out/log_slow" || { echo "slow-DDR2 run failed"; exit 1; }
# the pinned build (NSLOT = 4, PIN = 1: the whole RAM block 0 mirrored, what the C128 core uses)
iverilog -g2012 -DPIN -o "$out/tb_pin" $src
vvp "$out/tb_pin" > "$out/log_pin"
grep -E "^(==|   |PASS|FAIL)" "$out/log_pin" | sed 's/^/pin| /'
grep -q "^PASS tb_ramd" "$out/log_pin" || { echo "pinned run failed"; exit 1; }
iverilog -g2012 -DPIN -DLATMIN=52 -DLATRND=40 -o "$out/tb_pin_slow" $src
vvp "$out/tb_pin_slow" > "$out/log_pin_slow"
grep -q "^PASS tb_ramd" "$out/log_pin_slow" || { echo "pinned slow-DDR2 run failed"; exit 1; }
echo "pin| slow DDR2 (52..92 ui clocks): PASS"
# T140 D28: NSLOT = 5, PIN = 1, NDYN = 1 (the C128 core): four pinned banks of block 0 plus slot 4 taken on demand for one bank of block 1
iverilog -g2012 -DDYN -o "$out/tb_dyn" $src
vvp "$out/tb_dyn" > "$out/log_dyn"
grep -E "^(==|   |PASS|FAIL)" "$out/log_dyn" | sed 's/^/dyn| /'
grep -q "^PASS tb_ramd" "$out/log_dyn" || { echo "dyn run failed"; exit 1; }
iverilog -g2012 -DDYN -DLATMIN=52 -DLATRND=40 -o "$out/tb_dyn_slow" $src
vvp "$out/tb_dyn_slow" > "$out/log_dyn_slow"
grep -q "^PASS tb_ramd" "$out/log_dyn_slow" || { echo "dyn slow-DDR2 run failed"; exit 1; }
echo "dyn| slow DDR2 (52..92 ui clocks): PASS"
echo "PASS run_ramd"
