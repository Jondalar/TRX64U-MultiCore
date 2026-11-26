#!/bin/sh
# T15 §8: the boot ROM (bootrom_pkg.vhd) on Gideon's rvlite with his SPI block
# and a read-only flash model, under ghdl; run.py makes the images and checks
# each case (arguments: case names, default all). Needs ghdl on PATH (OSS CAD
# Suite). One process at a time, niced.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
"$root/tools/gideon_sources.sh"

out=$here/build
mkdir -p "$out"
cd "$out"
GH="nice -n 15 ghdl"
F="--std=08 -frelaxed -fsynopsys"
R=$root/vendor/gideon/fpga/cpu_unit/rvlite/vhdl_source
# decode_comb's main process lists neither inst_rd nor inst_rs1/rs2: in a
# simulation an instruction that differs from the one before only in those
# fields keeps the old register numbers (synthesis ignores the list). The
# bench runs a copy with process(all).
sed 's/^    process(program_counter, interrupt, inst_valid, inst_func3, inst_imm12, inst_opcode, inst_type, imm_value)$/    process(all)/' \
    $R/decode_comb.vhd > decode_comb_sim.vhd
grep -q 'process(all)' decode_comb_sim.vhd || { echo "decode_comb.vhd changed: sensitivity fix not applied"; exit 1; }
# Two builds of the bench: the boot ROM as committed and the same ROM without
# the guard (-DTRX_GUARD=0, T265 S2's negative control) in build/noguard.
analyse() {
    pkg=$1
    $GH -a $F $root/host/sim/usb/tl_string_util_pkg.vhd \
        $root/vendor/gideon/fpga/ip/busses/vhdl_source/io_bus_pkg.vhd \
        $root/vendor/gideon/fpga/ip/busses/vhdl_source/mem_bus_pkg.vhd \
        $root/vendor/gideon/fpga/ip/busses/vhdl_source/mem_bus_arbiter_pri_32.vhd \
        $root/vendor/gideon/fpga/io/spi/vhdl_source/spi.vhd \
        $root/vendor/gideon/fpga/io/spi/vhdl_source/spi_peripheral_io.vhd \
        $R/core_pkg.vhd $R/alu_pkg.vhd $R/alu_logical.vhd $R/alu_branch.vhd $R/multiply.vhd \
        $R/dsram.vhd $R/gprf.vhd $R/csr.vhd decode_comb_sim.vhd $R/decode.vhd $R/fetch.vhd \
        $R/execute.vhd $R/writeback.vhd $R/core.vhd $R/icache.vhd $R/bus_converter.vhd \
        $pkg $R/bootrom_8k.vhd $R/rvlite_wrapper.vhd \
        $here/flash_rom_model.vhd $here/tb_flash_boot.vhd
    $GH -e $F tb_flash_boot
}
analyse $root/host/bootrom_pkg.vhd
mkdir -p noguard
cp decode_comb_sim.vhd noguard/
(cd noguard && analyse $here/bootrom_noguard_pkg.vhd)
python3 "$here/run.py" "$@"
