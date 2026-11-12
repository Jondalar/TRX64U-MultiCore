# The VHDL of the c64_basic build: Gideon's RISC-V and the blocks around it (vendor/gideon), our
# boot ROM package for his bootrom_8k, his 1541 drive (drive_unit.vhd, drive_mem.vhd, drive_l2.vhd),
# his command interface (uci_unit.vhd), and ultimate_sys.vhd that puts them together. Sourced by
# build.tcl; the GHDL benches (sim/dma_bus, eth, input, usb, ultsys) read the list from here.
set g ../vendor/gideon/fpga
set gvhdl [list \
  $g/ip/busses/vhdl_source/io_bus_pkg.vhd $g/ip/busses/vhdl_source/mem_bus_pkg.vhd \
  $g/ip/busses/vhdl_source/io_bus_splitter.vhd $g/ip/busses/vhdl_source/mem_bus_arbiter_pri_32.vhd \
  $g/cpu_unit/rvlite/vhdl_source/core_pkg.vhd $g/cpu_unit/rvlite/vhdl_source/alu_pkg.vhd \
  $g/cpu_unit/rvlite/vhdl_source/alu_branch.vhd $g/cpu_unit/rvlite/vhdl_source/alu_logical.vhd \
  $g/cpu_unit/rvlite/vhdl_source/multiply.vhd $g/cpu_unit/rvlite/vhdl_source/dsram.vhd \
  $g/cpu_unit/rvlite/vhdl_source/gprf.vhd $g/cpu_unit/rvlite/vhdl_source/csr.vhd \
  $g/cpu_unit/rvlite/vhdl_source/decode_comb.vhd $g/cpu_unit/rvlite/vhdl_source/decode.vhd \
  $g/cpu_unit/rvlite/vhdl_source/fetch.vhd $g/cpu_unit/rvlite/vhdl_source/execute.vhd \
  $g/cpu_unit/rvlite/vhdl_source/writeback.vhd $g/cpu_unit/rvlite/vhdl_source/core.vhd \
  $g/cpu_unit/rvlite/vhdl_source/icache.vhd $g/cpu_unit/rvlite/vhdl_source/bus_converter.vhd \
  bootrom_pkg.vhd $g/cpu_unit/rvlite/vhdl_source/bootrom_8k.vhd \
  cache_cfg_pkg.vhd rv_dcache.vhd rv_icache.vhd rvlite_wrapper_dc.vhd \
  $g/ip/clock/vhdl_source/fractional_div.vhd $g/ip/srl_fifo/vhdl_source/srl_fifo-xilinx.vhd \
  $g/ip/sync_fifo/vhdl_source/sync_fifo.vhd \
  $g/io/uart_lite/vhdl_source/tx.vhd $g/io/uart_lite/vhdl_source/rx.vhd \
  $g/io/uart_lite/vhdl_source/uart_peripheral_io.vhd \
  $g/io/uart_lite/vhdl_source/slip_encoder.vhd $g/io/uart_lite/vhdl_source/slip_decoder.vhd \
  $g/io/uart_lite/vhdl_source/tx_dma.vhd $g/io/uart_lite/vhdl_source/rx_dma.vhd \
  $g/io/uart_lite/vhdl_source/uart_dma.vhd \
  $g/io/spi/vhdl_source/spi.vhd \
  $g/io/spi/vhdl_source/spi_peripheral_io.vhd \
  $g/io/itu/vhdl_source/itu_pkg.vhd $g/io/itu/vhdl_source/itu.vhd \
  $g/altera/u2p_io.vhd \
  $g/ip/async_fifo/vhdl_source/gray_code_pkg.vhd $g/ip/async_fifo/vhdl_source/async_fifo_ft.vhd \
  $g/ip/synchroniser/vhdl_source/level_synchronizer.vhd \
  $g/ip/synchroniser/vhdl_source/pulse_synchronizer.vhd \
  $g/ip/memory/vhdl_source/spram.vhd \
  $g/ip/free_queue/vhdl_source/block_bus_pkg.vhd $g/ip/free_queue/vhdl_source/free_queue.vhd \
  $g/io/rmii/vhdl_source/crc32.vhd $g/io/rmii/vhdl_source/rmii_transceiver.vhd \
  $g/io/rmii/vhdl_source/eth_filter.vhd $g/io/rmii/vhdl_source/eth_transmit.vhd \
  $g/io/rmii/vhdl_source/ethernet_interface.vhd \
  $g/ip/busses/vhdl_source/endianness_pkg.vhd \
  $g/ip/memory/vhdl_source/dpram_8x16.vhd $g/ip/memory/vhdl_source/dpram_8x32.vhd usb_dpram.vhd \
  $g/altera/distributed_stack.vhd $g/altera/align_read_to_bram.vhd \
  $g/ip/nano_cpu/vhdl_source/nano_cpu_pkg.vhd $g/ip/nano_cpu/vhdl_source/nano_alu.vhd \
  $g/ip/nano_cpu/vhdl_source/nano_cpu.vhd $g/ip/nano_cpu/vhdl_source/nano.vhd \
  $g/io/usb2/vhdl_source/usb_pkg.vhd $g/io/usb2/vhdl_source/usb_cmd_pkg.vhd \
  $g/io/usb2/vhdl_source/timer.vhd $g/io/usb2/vhdl_source/token_crc.vhd \
  $g/io/usb2/vhdl_source/token_crc_19.vhd $g/io/usb2/vhdl_source/token_crc_check.vhd \
  $g/io/usb2/vhdl_source/data_crc.vhd $g/io/usb2/vhdl_source/ulpi_bus.vhd \
  $g/io/usb2/vhdl_source/ulpi_rx.vhd $g/io/usb2/vhdl_source/ulpi_tx.vhd \
  $g/io/usb2/vhdl_source/usb_host_interface.vhd $g/io/usb2/vhdl_source/host_sequencer.vhd \
  $g/io/usb2/vhdl_source/bridge_to_mem_ctrl.vhd $g/io/usb2/vhdl_source/mem_addr_counter.vhd \
  $g/io/usb2/vhdl_source/mem_remain_counter.vhd $g/io/usb2/vhdl_source/usb_memory_ctrl.vhd \
  $g/io/usb2/vhdl_source/nano_minimal_io.vhd $g/io/usb2/vhdl_source/usb_cmd_nano.vhd \
  $g/io/usb2/vhdl_source/usb_debug.vhd $g/io/usb2/vhdl_source/usb_host_nano.vhd \
  $g/6502n/vhdl_source/pkg_6502_defs.vhd $g/6502n/vhdl_source/pkg_6502_opcodes.vhd \
  $g/6502n/vhdl_source/pkg_6502_decode.vhd $g/6502n/vhdl_source/alu.vhd \
  $g/6502n/vhdl_source/bit_cpx_cpy.vhd $g/6502n/vhdl_source/implied.vhd $g/6502n/vhdl_source/shifter.vhd \
  $g/6502n/vhdl_source/data_oper.vhd $g/6502n/vhdl_source/proc_control.vhd \
  $g/6502n/vhdl_source/proc_interrupt.vhd $g/6502n/vhdl_source/proc_registers.vhd \
  $g/6502n/vhdl_source/proc_core.vhd $g/6502n/vhdl_source/cpu6502.vhd \
  $g/ip/memory/vhdl_source/pseudo_dpram_8x32.vhd pseudo_dpram_8x32_x7.vhd \
  $g/io/sigma_delta_dac/vhdl_source/noise_generator.vhd \
  $g/1541/vhdl_source/c1541_pkg.vhd $g/1541/vhdl_source/via6522.vhd $g/1541/vhdl_source/cpu_part_1541.vhd \
  $g/1541/vhdl_source/c1541_timing.vhd $g/1541/vhdl_source/floppy_param_mem.vhd \
  $g/1541/vhdl_source/floppy_mem.vhd $g/1541/vhdl_source/floppy_stream.vhd $g/1541/vhdl_source/floppy.vhd \
  $g/1541/vhdl_source/floppy_sound.vhd $g/1541/vhdl_source/drive_registers.vhd \
  $g/1541/vhdl_source/c1541_drive.vhd ../cores/common/drive_mem.vhd ../cores/common/drive_unit.vhd \
  ../cores/common/drive_l2.vhd \
  $g/1541/vhdl_source/bin2gcr.vhd $g/1541/vhdl_source/gcr2bin.vhd $g/1541/vhdl_source/gcr_codec.vhd \
  $g/ip/busses/vhdl_source/mem_bus_arbiter_pri.vhd $g/ip/busses/vhdl_source/mem_to_mem32.vhd \
  $g/ip/busses/vhdl_source/slot_bus_pkg.vhd $g/ip/synchroniser/vhdl_source/synchronizer_gzw.vhd \
  $g/ip/busses/vhdl_source/io_bus_bridge2.vhd $g/ip/memory/vhdl_source/dpram.vhd \
  $g/io/command_interface/vhdl_source/command_if_pkg.vhd \
  $g/io/command_interface/vhdl_source/command_protocol.vhd \
  $g/io/command_interface/vhdl_source/command_interface.vhd uci_unit.vhd \
  flash_gate.vhd ultimate_sys.vhd ]
