#!/bin/sh
# rvlite multiply (OPT-rvmul): the vendored core under GHDL (tb_rvmul.vhd) runs
# random RV32IM programs (gen.py: MUL/MULH/MULHSU/MULHU, back-to-back, dependent
# ALU / branch / load-use, wrong-path muls, wait states on the data bus) and
# the data memory is compared with gen.py's reference model. PASS = 0
# mismatches in every program. SEEDS and N override the defaults. Needs ghdl.
# One process, niced.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
# RV_SRC: another rvlite vhdl_source directory (the unpatched core, for a baseline)
if [ -z "$RV_SRC" ]; then
  "$root/tools/gideon_sources.sh"
  RV_SRC=$root/vendor/gideon/fpga/cpu_unit/rvlite/vhdl_source
fi
R=$RV_SRC
out=$here/build${RV_TAG:+_$RV_TAG}
mkdir -p "$out"
cd "$out"
# decode_comb.vhd's process lists only some of the fields of `instruction` it
# reads (not rd, rs1, rs2, func7): in simulation an instruction that differs
# from its predecessor in those fields alone is decoded stale (synthesis is
# unaffected). The simulation copy is sensitive to all.
sed 's/^    process(program_counter, interrupt, inst_valid, inst_func3, inst_imm12, inst_opcode, inst_type, imm_value)/    process(all)/' "$R/decode_comb.vhd" > decode_comb_sim.vhd
cmp -s decode_comb_sim.vhd "$R/decode_comb.vhd" && { echo "decode_comb.vhd: sensitivity list not found"; exit 1; }
opts="--std=08 -fsynopsys -frelaxed -Wno-hide -Wno-shared -Wno-library"
nice -n 15 ghdl -a $opts $root/host/sim/usb/tl_string_util_pkg.vhd \
  $R/core_pkg.vhd $R/alu_pkg.vhd $R/alu_logical.vhd $R/alu_branch.vhd $R/multiply.vhd $R/dsram.vhd \
  $R/gprf.vhd $R/csr.vhd decode_comb_sim.vhd $R/decode.vhd $R/fetch.vhd $R/execute.vhd \
  $R/writeback.vhd $R/core.vhd $here/tb_rvmul.vhd
nice -n 15 ghdl -e $opts tb_rvmul
fail=0
for seed in ${SEEDS:-1 2 3 4 5 6 7 8}; do
  for waits in true false; do
    python3 $here/gen.py gen $seed ${N:-1500} prog.hex model.hex
    nice -n 15 ghdl -r $opts tb_rvmul -gg_prog=prog.hex -gg_out=dmem.hex -gg_trace=trace.txt -gg_waits=$waits 2>&1 | grep -E "TB:" | sed "s/^.*TB:/seed $seed waits $waits:/" || true
    python3 $here/gen.py cmp model.hex dmem.hex trace.txt || { echo "FAIL seed $seed waits $waits"; fail=1; }
  done
done
[ $fail = 0 ] && echo "PASS: rvmul, 0 mismatches"
exit $fail
