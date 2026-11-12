#!/bin/sh
# Copy the parts of Gideon's open 1541ultimate VHDL that the TRX64 designs use
# into vendor/gideon, unchanged. vendor/gideon is generated (not in git); the
# builds call tools/gideon_sources.sh, which runs this on apps/ultimate.
# Usage: tools/vendor_gideon.sh <1541ultimate checkout>
#
# Only files from the list below are taken. None of them carries the clause
# "not supposed to be used in other projects without written permission"
# (fpga/sid6581, fpga/io/copper, io/audio/lp_filter*, sys_to_aud do); the
# script stops if one ever does. VENDOR.md records the commit.
set -e
src=${1:?1541ultimate checkout}
here=$(cd "$(dirname "$0")/.." && pwd)
dst=$here/vendor/gideon

files="
fpga/cpu_unit/rvlite/vhdl_source/alu_branch.vhd
fpga/cpu_unit/rvlite/vhdl_source/alu_logical.vhd
fpga/cpu_unit/rvlite/vhdl_source/alu_pkg.vhd
fpga/cpu_unit/rvlite/vhdl_source/bootrom_8k.vhd
fpga/cpu_unit/rvlite/vhdl_source/bus_converter.vhd
fpga/cpu_unit/rvlite/vhdl_source/core.vhd
fpga/cpu_unit/rvlite/vhdl_source/core_pkg.vhd
fpga/cpu_unit/rvlite/vhdl_source/csr.vhd
fpga/cpu_unit/rvlite/vhdl_source/decode.vhd
fpga/cpu_unit/rvlite/vhdl_source/decode_comb.vhd
fpga/cpu_unit/rvlite/vhdl_source/dsram.vhd
fpga/cpu_unit/rvlite/vhdl_source/execute.vhd
fpga/cpu_unit/rvlite/vhdl_source/fetch.vhd
fpga/cpu_unit/rvlite/vhdl_source/gprf.vhd
fpga/cpu_unit/rvlite/vhdl_source/icache.vhd
fpga/cpu_unit/rvlite/vhdl_source/multiply.vhd
fpga/cpu_unit/rvlite/vhdl_source/rvlite_wrapper.vhd
fpga/cpu_unit/rvlite/vhdl_source/writeback.vhd
fpga/ip/busses/vhdl_source/io_bus_pkg.vhd
fpga/ip/busses/vhdl_source/mem_bus_pkg.vhd
fpga/ip/busses/vhdl_source/io_bus_splitter.vhd
fpga/ip/busses/vhdl_source/mem_bus_arbiter_pri_32.vhd
fpga/ip/busses/vhdl_source/slot_bus_pkg.vhd
fpga/ip/busses/vhdl_source/io_bus_bridge2.vhd
fpga/ip/synchroniser/vhdl_source/synchronizer_gzw.vhd
fpga/ip/memory/vhdl_source/dpram.vhd
fpga/io/command_interface/vhdl_source/command_if_pkg.vhd
fpga/io/command_interface/vhdl_source/command_protocol.vhd
fpga/io/command_interface/vhdl_source/command_interface.vhd
fpga/ip/clock/vhdl_source/fractional_div.vhd
fpga/ip/srl_fifo/vhdl_source/srl_fifo-xilinx.vhd
fpga/ip/sync_fifo/vhdl_source/sync_fifo.vhd
fpga/io/itu/vhdl_source/itu.vhd
fpga/io/itu/vhdl_source/itu_pkg.vhd
fpga/io/uart_lite/vhdl_source/uart_peripheral_io.vhd
fpga/io/uart_lite/vhdl_source/tx.vhd
fpga/io/uart_lite/vhdl_source/rx.vhd
fpga/io/uart_lite/vhdl_source/uart_dma.vhd
fpga/io/uart_lite/vhdl_source/tx_dma.vhd
fpga/io/uart_lite/vhdl_source/rx_dma.vhd
fpga/io/uart_lite/vhdl_source/slip_encoder.vhd
fpga/io/uart_lite/vhdl_source/slip_decoder.vhd
fpga/altera/u2p_io.vhd
fpga/io/rmii/vhdl_source/ethernet_rmii.vhd
fpga/io/rmii/vhdl_source/ethernet_interface.vhd
fpga/io/rmii/vhdl_source/eth_filter.vhd
fpga/io/rmii/vhdl_source/eth_transmit.vhd
fpga/io/rmii/vhdl_source/rmii_transceiver.vhd
fpga/io/rmii/vhdl_source/crc32.vhd
fpga/ip/free_queue/vhdl_source/free_queue.vhd
fpga/ip/free_queue/vhdl_source/block_bus_pkg.vhd
fpga/ip/async_fifo/vhdl_source/async_fifo_ft.vhd
fpga/ip/async_fifo/vhdl_source/gray_code_pkg.vhd
fpga/ip/synchroniser/vhdl_source/level_synchronizer.vhd
fpga/ip/synchroniser/vhdl_source/pulse_synchronizer.vhd
fpga/ip/memory/vhdl_source/spram.vhd
fpga/io/spi/vhdl_source/spi.vhd
fpga/io/spi/vhdl_source/spi_peripheral_io.vhd
fpga/ip/video/vhdl_source/char_generator_pkg.vhd
fpga/ip/video/vhdl_source/char_generator_regs.vhd
fpga/ip/video/vhdl_source/char_generator_slave12.vhd
fpga/ip/video/vhdl_source/char_generator_rom_pkg.vhd
fpga/ip/video/vhdl_gen/font_pkg.vhd
target/simulation/packages/vhdl_source/tl_string_util_pkg.vhd
fpga/io/usb2/vhdl_source/usb_pkg.vhd
fpga/io/usb2/vhdl_source/usb_cmd_pkg.vhd
fpga/io/usb2/vhdl_source/ulpi_bus.vhd
fpga/io/usb2/vhdl_source/ulpi_rx.vhd
fpga/io/usb2/vhdl_source/ulpi_tx.vhd
fpga/io/usb2/vhdl_source/data_crc.vhd
fpga/io/usb2/vhdl_source/token_crc.vhd
fpga/io/usb2/vhdl_source/token_crc_19.vhd
fpga/io/usb2/vhdl_source/token_crc_check.vhd
fpga/io/usb2/vhdl_source/timer.vhd
fpga/io/usb2/vhdl_source/usb_host_interface.vhd
fpga/io/usb2/vhdl_source/host_sequencer.vhd
fpga/io/usb2/vhdl_source/bridge_to_mem_ctrl.vhd
fpga/io/usb2/vhdl_source/mem_addr_counter.vhd
fpga/io/usb2/vhdl_source/mem_remain_counter.vhd
fpga/io/usb2/vhdl_source/usb_memory_ctrl.vhd
fpga/io/usb2/vhdl_source/nano_minimal_io.vhd
fpga/io/usb2/vhdl_source/usb_cmd_nano.vhd
fpga/io/usb2/vhdl_source/usb_debug.vhd
fpga/io/usb2/vhdl_source/usb_host_nano.vhd
fpga/ip/nano_cpu/vhdl_source/nano_cpu_pkg.vhd
fpga/ip/nano_cpu/vhdl_source/nano_alu.vhd
fpga/ip/nano_cpu/vhdl_source/nano_cpu.vhd
fpga/ip/nano_cpu/vhdl_source/nano.vhd
fpga/ip/memory/vhdl_source/dpram_8x16.vhd
fpga/ip/memory/vhdl_source/dpram_8x32.vhd
fpga/ip/busses/vhdl_source/endianness_pkg.vhd
fpga/altera/align_read_to_bram.vhd
fpga/altera/distributed_stack.vhd
software/io/usb/nano_minimal.nan
tools/parse_nano.py
fpga/ip/busses/vhdl_source/mem_bus_arbiter_pri.vhd
fpga/ip/busses/vhdl_source/mem_to_mem32.vhd
fpga/ip/memory/vhdl_source/pseudo_dpram_8x32.vhd
fpga/io/sigma_delta_dac/vhdl_source/noise_generator.vhd
fpga/6502n/vhdl_source/pkg_6502_defs.vhd
fpga/6502n/vhdl_source/pkg_6502_opcodes.vhd
fpga/6502n/vhdl_source/pkg_6502_decode.vhd
fpga/6502n/vhdl_source/alu.vhd
fpga/6502n/vhdl_source/bit_cpx_cpy.vhd
fpga/6502n/vhdl_source/implied.vhd
fpga/6502n/vhdl_source/shifter.vhd
fpga/6502n/vhdl_source/data_oper.vhd
fpga/6502n/vhdl_source/proc_control.vhd
fpga/6502n/vhdl_source/proc_interrupt.vhd
fpga/6502n/vhdl_source/proc_registers.vhd
fpga/6502n/vhdl_source/proc_core.vhd
fpga/6502n/vhdl_source/cpu6502.vhd
fpga/1541/vhdl_source/c1541_pkg.vhd
fpga/1541/vhdl_source/via6522.vhd
fpga/1541/vhdl_source/cpu_part_1541.vhd
fpga/1541/vhdl_source/c1541_timing.vhd
fpga/1541/vhdl_source/floppy_param_mem.vhd
fpga/1541/vhdl_source/floppy_mem.vhd
fpga/1541/vhdl_source/floppy_stream.vhd
fpga/1541/vhdl_source/floppy.vhd
fpga/1541/vhdl_source/floppy_sound.vhd
fpga/1541/vhdl_source/drive_registers.vhd
fpga/1541/vhdl_source/bin2gcr.vhd
fpga/1541/vhdl_source/gcr2bin.vhd
fpga/1541/vhdl_source/gcr_codec.vhd
fpga/1541/vhdl_source/c1541_drive.vhd
software/portable/riscv/bootloader_u64ii.c
LICENSE.txt
"

rm -rf "$dst"
mkdir -p "$dst"
for f in $files; do
  if grep -qi "without written permission" "$src/$f"; then
    echo "$f: restricted licence, not vendored" >&2
    exit 1
  fi
  mkdir -p "$dst/$(dirname "$f")"
  cp "$src/$f" "$dst/$f"
done

# The C64 core (vendor/icepi, out of context) has the same 6502n entities; the
# drive's copies get a prefix so the two netlists never share a module name.
perl -pi -e 's/\b(alu|bit_cpx_cpy|data_oper|implied|shifter|proc_control|proc_interrupt|proc_registers|proc_core)\b/drv_$1/g' \
  "$dst"/fpga/6502n/vhdl_source/*.vhd

# local changes, kept as patches in tools/patches/gideon
for p in "$here"/tools/patches/gideon/*.patch; do
  [ -f "$p" ] && git -C "$here" apply -p1 --directory=vendor/gideon "$p"
done

{
  echo "# 1541ultimate (Gideon Zweijtzer), vendored parts"
  echo
  echo "Generated by \`tools/gideon_sources.sh\` from the submodule apps/ultimate. Do not edit here: change upstream or add a patch in tools/patches/gideon."
  echo
  echo "| repository | commit |"
  echo "|---|---|"
  echo "| 1541ultimate | $(git -C "$src" rev-parse HEAD) |"
  echo
  echo "Licence: GPL-3.0 (LICENSE.txt); no file here carries the restricted clause."
  echo "\`bootloader_u64ii.c\` is kept as the reference for \`host/bootrom/\`."
  echo "The char_generator files and font_pkg are the reference for host/overlay.v (fonts via tools/font2mem.py)."
  echo "\`nano_minimal.nan\` (the USB nano CPU program the app loads) and \`parse_nano.py\` are kept for the USB simulation (host/sim/usb)."
  echo "dpram_8x16/dpram_8x32 come as entities only; host/usb_dpram.vhd has the architectures."
  echo "fpga/6502n: the entities carry the prefix drv_ (the C64 core has the same ones; see tools/vendor_gideon.sh)."
  echo "fpga/ip/memory pseudo_dpram_8x32: only the entity; host/pseudo_dpram_8x32_x7.vhd is its architecture."
  echo "fpga/io/command_interface: the UCI (T28, host/uci_unit.vhd), with dpram, slot_bus_pkg, io_bus_bridge2 and synchronizer_gzw."
  echo "Patches applied from tools/patches/gideon:"
  for p in "$here"/tools/patches/gideon/*.patch; do
    [ -f "$p" ] && echo "- $(basename "$p")"
  done
  true
} > "$dst/VENDOR.md"
echo "vendored into $dst"
