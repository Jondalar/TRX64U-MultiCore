#!/bin/sh
# T28 §7.1: uci_unit.vhd (Gideon's command_interface, our slot adapter, his
# io_bus_bridge2) under ghdl with tb_uci.vhd: DOS identify, a two-part
# response, abort, the busy error bit, $DE1C. Needs ghdl on PATH (OSS CAD
# Suite). One process, niced.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../../.." && pwd)
"$root/tools/gideon_sources.sh"
G=$root/vendor/gideon/fpga
B=$G/ip/busses/vhdl_source
C=$G/io/command_interface/vhdl_source
out=$here/build
mkdir -p "$out"
cd "$out"
src="$B/io_bus_pkg.vhd $B/slot_bus_pkg.vhd $B/io_bus_splitter.vhd
  $G/ip/synchroniser/vhdl_source/synchronizer_gzw.vhd $B/io_bus_bridge2.vhd $G/ip/memory/vhdl_source/dpram.vhd
  $C/command_if_pkg.vhd $C/command_protocol.vhd $C/command_interface.vhd
  $root/host/uci_unit.vhd $here/tb_uci.vhd"
nice -n 15 ghdl -a --std=08 -frelaxed $src
nice -n 15 ghdl -e --std=08 -frelaxed tb_uci
nice -n 15 ghdl -r --std=08 -frelaxed tb_uci --assert-level=error ${WAVE:+--wave=$out/tb_uci.ghw}
