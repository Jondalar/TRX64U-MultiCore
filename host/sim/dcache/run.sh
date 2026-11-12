#!/bin/sh
# T133 bench: rv_dcache (host/rv_dcache.vhd) in front of Gideon's bus_converter, a DMA master on the
# arbiter, a toggle writer and a memory model (tb_dcache.vhd; spec docs/specs/T133-host-dcache.md §2).
#   directed scenarios S1-S9 and a random run (SEEDS, default "1 2 3"); PASS = no "ERR:" line
# G = Gideon's fpga directory (default vendor/gideon/fpga). Needs ghdl (container trx64-sim-env:4). One process, niced.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
G=${G:-$root/vendor/gideon/fpga}
[ -d "$G" ] || { "$root/tools/gideon_sources.sh"; }
SEEDS=${SEEDS:-1 2 3}
opts="--std=08 -fsynopsys -frelaxed -Wno-hide -Wno-shared -Wno-library"
B=$G/cpu_unit/rvlite/vhdl_source
P=$G/ip/busses/vhdl_source
DC=${DC_SRC:-$root/host/rv_dcache.vhd}

# build <dir> <rv_dcache.vhd>
build() {
  mkdir -p "$1"
  (cd "$1" && rm -f work-obj08.cf && nice -n 15 ghdl -a $opts \
    $P/mem_bus_pkg.vhd $P/io_bus_pkg.vhd $P/mem_bus_arbiter_pri_32.vhd $B/core_pkg.vhd \
    "$2" $B/bus_converter.vhd "$here/tb_dcache.vhd" \
  && nice -n 15 ghdl -e $opts tb_dcache) 2>&1 | tail -20
}

# run1 <dir> <seed> <batches>: prints the ERR lines and the RESULT line
run1() {
  (cd "$1" && timeout 900 nice -n 15 ghdl -r $opts tb_dcache --ieee-asserts=disable -gg_seed=$2 -gg_batches=$3 2>&1 \
    | grep -E "ERR:|RESULT:|S1/S2" | sed 's/^.*(report note): //' || echo "RESULT: no output")
}

out=$here/build
build "$out" "$DC"
fail=0
for s in $SEEDS; do
  res=$(run1 "$out" $s 300)
  echo "$res" | head -20
  echo "$res" | grep -q "RESULT: PASS" || fail=$((fail + 1))
done
if [ $fail = 0 ]; then echo "PASS: dcache, seeds $SEEDS, 0 errors"; else echo "FAIL: dcache, $fail seed(s) failed"; fi
[ $fail = 0 ]
