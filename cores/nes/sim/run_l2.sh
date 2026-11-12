#!/bin/sh
# T95: layer2_nes at the partition's real edge (all 228 ports, declared from the module header by mk_l2tb.py), Verilator.
# No cartridge is loaded: the PRG is empty, the CPU runs through zeros and the PPU shows its backdrop - what is checked is the
# edge (ID, reset, Freeze, idle ports), not the machine (run.sh does that).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
out=$here/build/l2
mkdir -p "$out"
python3 "$here/mk_l2tb.py" "$root/cores/nes/layer2_nes.v" "$out/tb_nes_l2.v"
g=$root/cores/nes/gen
src="$g/regs_savestates.sv $g/nes.v $g/ppu.sv $g/apu.sv $g/cart.sv $g/mappers/*.sv $g/t65/t65_nes.v $root/cores/nes/trx/*.v $root/cores/nes/layer2_nes.v"
# shellcheck disable=SC2086
nice -n 15 verilator --binary --timing -Wno-fatal -Wno-lint -Wno-style -Wno-TIMESCALEMOD -j 2 --top-module tb_nes_l2 \
  -Mdir "$out/obj" --x-assign 0 --x-initial 0 "$out/tb_nes_l2.v" $src > "$out/build.log" 2>&1 || { tail -30 "$out/build.log"; echo "FAIL build"; exit 1; }
(cd "$out" && nice -n 15 obj/Vtb_nes_l2) | tee "$out/run.log"
grep -q "^PASS tb_nes_l2" "$out/run.log"
echo "PASS run_l2"
