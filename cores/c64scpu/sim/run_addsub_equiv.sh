#!/bin/bash
# T248 S2, S3: the carry-select decimal adder against the upstream one. Run by hand, in a container with ghdl (never on the Mac):
#   Run on the builder, in the trx-ghdl6 image: cd <checkout> && docker run --rm -v "$PWD:/w" -w /w trx-ghdl6 nice -n 15 timeout 1800 cores/c64scpu/sim/run_addsub_equiv.sh
# Reference = cores/snes/rtl/65C816/{AddSubBCD,BCDAdder}.vhd with the script's renames, entities suffixed _ref (tools/scpu_vendor816.py --ref).
set -e
cd "$(dirname "$0")/../../.."
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
python3 -B tools/scpu_vendor816.py --ref "$W/ref"
C=cores/c64scpu
ghdl -a --std=08 --workdir="$W" $C/cpu/s816_pkg.vhd "$W/ref/BCDAdder_ref.vhd" "$W/ref/AddSubBCD_ref.vhd" $C/cpu/s816_addsubbcd.vhd $C/sim/tb_addsub_equiv.vhd
ghdl -e --std=08 --workdir="$W" -o "$W/tb" tb_addsub_equiv
"$W/tb" 2>&1 | tee "$W/out.txt"
grep -q 'S2 PASS 524288/524288' "$W/out.txt" && grep -q 'S3 PASS' "$W/out.txt" && ! grep -q 'MISMATCH\|FAIL' "$W/out.txt"
