#!/bin/sh
# T97 5.13: rvlite trap/mret bench (tb_rvtrap.vhd, prog.S). The vendored core as the host builds
# it runs two tasks under a FreeRTOS-style handler with random wait states and a level interrupt
# at random phases; a monitor on the core's own signals checks the instruction word against its
# PC, the PC sequence, the trap cause against the instruction, and mstatus/mepc against a model of
# what retired. PASS = 0 monitor errors, the software's MIE check never fires, and both tasks'
# results equal the run without interrupts.
#   MODE=main (default)  the core as given (RV_SRC, default vendor/gideon)
#   RV_DC=1              the wrapper with the data cache (host/rvlite_wrapper_dc.vhd, T133) instead of Gideon's
# SEEDS, PERIODS, WAITS override the matrix (CI: 2 x 2 x 2; the full one of T97 5.13 is SEEDS="1 2 3 4 5 6" PERIODS="300 900 2500"). RV_SRC: another rvlite vhdl_source directory.
# Needs ghdl (container trx64-sim-env:4) and python3; nothing else. One process, niced.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
if [ -z "$RV_SRC" ]; then
  [ -d "$root/vendor/gideon/fpga" ] || "$root/tools/gideon_sources.sh"
  RV_SRC=$root/vendor/gideon/fpga/cpu_unit/rvlite/vhdl_source
fi
G=$(cd "$RV_SRC/../../.." && pwd)
# RV_DC=1 builds in its own directory: CI runs rvtrap and rvtrap_dc at the same time (run 1007: one bench compiled into the other)
[ -n "$RV_DC" ] && RV_TAG=${RV_TAG:-dc}
SEEDS=${SEEDS:-1 2}
PERIODS=${PERIODS:-300 2500}
WAITS=${WAITS:-0 3}
opts="--std=08 -fsynopsys -frelaxed -Wno-hide -Wno-shared -Wno-library"

# compile <dir> <rvlite source dir>
compile() {
  mkdir -p "$1"
  # decode_comb.vhd: the sensitivity list misses instruction fields, in simulation only (see rvmul/run.sh)
  sed 's/^    process(program_counter, interrupt, inst_valid, inst_func3, inst_imm12, inst_opcode, inst_type, imm_value)/    process(all)/' "$2/decode_comb.vhd" > "$1/decode_comb_sim.vhd"
  cmp -s "$1/decode_comb_sim.vhd" "$2/decode_comb.vhd" && { echo "decode_comb.vhd: sensitivity list not found"; exit 1; }
  python3 "$here/splice.py" "$2" "$1"
  wrap=$2/rvlite_wrapper.vhd
  # RV_DC=1: the wrapper the host builds, with the data cache (T133)
  if [ -n "$RV_DC" ]; then wrap="$root/host/cache_cfg_pkg.vhd $root/host/rv_dcache.vhd $root/host/rv_icache.vhd $root/host/rvlite_wrapper_dc.vhd"; fi
  (cd "$1" && rm -f work-obj08.cf && nice -n 15 ghdl -a $opts \
    $root/host/sim/usb/tl_string_util_pkg.vhd \
    $G/ip/busses/vhdl_source/mem_bus_pkg.vhd $G/ip/busses/vhdl_source/io_bus_pkg.vhd \
    $G/ip/busses/vhdl_source/mem_bus_arbiter_pri_32.vhd \
    $here/rvtrap_pkg.vhd $2/core_pkg.vhd $2/alu_pkg.vhd $2/alu_logical.vhd $2/alu_branch.vhd $2/multiply.vhd $2/dsram.vhd \
    $2/gprf.vhd csr_mon.vhd decode_comb_sim.vhd $2/decode.vhd $2/fetch.vhd $2/execute.vhd \
    $2/writeback.vhd core_mon.vhd $2/icache.vhd $2/bus_converter.vhd \
    $root/host/bootrom_pkg.vhd $2/bootrom_8k.vhd $wrap $here/tb_rvtrap.vhd \
  && nice -n 15 ghdl -e $opts tb_rvtrap)
}

# runone <dir> <seed> <period> <waits>  -> the "TB: result" line, the first error lines
runone() {
  (cd "$1" && cp "$here/prog.hex" . && timeout 900 nice -n 15 ghdl -r $opts tb_rvtrap --ieee-asserts=disable \
    -gg_seed=$2 -gg_irq_period=$3 -gg_waits=$4 -gg_quiet=true 2>&1 | grep -E "TB:" || echo "TB: no output")
}

if [ "${MODE:-main}" = build ]; then
  compile "$here/build${RV_TAG:+_$RV_TAG}" "$RV_SRC"
  exit 0
fi
if [ "${MODE:-main}" = main ]; then
  out=$here/build${RV_TAG:+_$RV_TAG}
  compile "$out" "$RV_SRC"
  ref=$(runone "$out" 1 0 0 | grep "TB: result")
  echo "reference (no interrupts, no waits): $ref"
  refres=$(echo "$ref" | sed 's/ sw_MIE0.*//')
  fail=0; runs=0
  for seed in $SEEDS; do for per in $PERIODS; do for w in $WAITS; do
    runs=$((runs + 1))
    res=$(runone "$out" $seed $per $w)
    line=$(echo "$res" | grep "TB: result" || true)
    ok=1
    [ "$(echo "$line" | sed 's/ sw_MIE0.*//')" = "$refres" ] || ok=0
    echo "$line" | grep -q "sw_MIE0=0 .*errors=0" || ok=0
    if [ $ok = 1 ]; then echo "seed $seed period $per waits $w: ok  $line"
    else echo "seed $seed period $per waits $w: FAIL  $line"; echo "$res" | grep "ERROR" | head -3; fail=$((fail + 1)); fi
  done; done; done
  if [ $fail = 0 ]; then echo "PASS: rvtrap, $runs runs, 0 errors"; else echo "FAIL: rvtrap, $fail of $runs runs"; fi
  [ $fail = 0 ]
fi
