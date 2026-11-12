# Gideon's VHDL in the c64ref core (the core is self-contained)

Copies of the parts of Gideon Zweijtzer's 1541ultimate (GPL-3.0, `apps/ultimate/LICENSE.txt`) that the c64ref core uses, taken at
`aa2ef5dc62b9ff73a87a1a90e15439c16308b49d` (the commit `apps/ultimate` is at). Column "from": `vendor` = the file as
`tools/gideon_sources.sh` produces it (`vendor/gideon`: the 6502n entities carry the prefix `drv_`, and
`tools/patches/gideon` 0001/0002/0004 are applied where they touch the file), `app` = unchanged from `apps/ultimate`.
No file here carries the clause "not supposed to be used in other projects without written permission" (checked with grep before
copying; the files with that clause - UltiSID, copper, lp_filter, the sampler - are not here and were never opened).
The list is in dependency order (`core_sources.tcl` reads it as is).

Ours, next to them: `c64ref_l2.vhd` (flat-port wrappers: drive, GCR codec, UCI), `c64ref_drive_mem.vhd` (copy of
`cores/common/drive_mem.vhd`, renamed), `uci_unit_c64ref.vhd` (copy of `host/uci_unit.vhd`, renamed),
`c64ref_pseudo_dpram_8x32_x7.vhd` (copy of `host/pseudo_dpram_8x32_x7.vhd`: the architecture of Gideon's entity).

| file | from | path under `apps/ultimate` |
|---|---|---|
| io_bus_pkg.vhd | vendor | fpga/ip/busses/vhdl_source/io_bus_pkg.vhd |
| io_bus_splitter.vhd | vendor | fpga/ip/busses/vhdl_source/io_bus_splitter.vhd |
| c1541_timing.vhd | vendor | fpga/1541/vhdl_source/c1541_timing.vhd |
| pkg_6502_defs.vhd | vendor | fpga/6502n/vhdl_source/pkg_6502_defs.vhd |
| pkg_6502_decode.vhd | vendor | fpga/6502n/vhdl_source/pkg_6502_decode.vhd |
| proc_control.vhd | vendor | fpga/6502n/vhdl_source/proc_control.vhd |
| bit_cpx_cpy.vhd | vendor | fpga/6502n/vhdl_source/bit_cpx_cpy.vhd |
| shifter.vhd | vendor | fpga/6502n/vhdl_source/shifter.vhd |
| alu.vhd | vendor | fpga/6502n/vhdl_source/alu.vhd |
| implied.vhd | vendor | fpga/6502n/vhdl_source/implied.vhd |
| data_oper.vhd | vendor | fpga/6502n/vhdl_source/data_oper.vhd |
| proc_registers.vhd | vendor | fpga/6502n/vhdl_source/proc_registers.vhd |
| proc_interrupt.vhd | vendor | fpga/6502n/vhdl_source/proc_interrupt.vhd |
| proc_core.vhd | vendor | fpga/6502n/vhdl_source/proc_core.vhd |
| cpu6502.vhd | vendor | fpga/6502n/vhdl_source/cpu6502.vhd |
| via6522.vhd | vendor | fpga/1541/vhdl_source/via6522.vhd |
| cia_pkg.vhd | app | fpga/1541/vhdl_source/cia_pkg.vhd |
| cia_timer.vhd | app | fpga/1541/vhdl_source/cia_timer.vhd |
| tl_string_util_pkg.vhd | vendor | target/simulation/packages/vhdl_source/tl_string_util_pkg.vhd |
| cia_registers.vhd | app | fpga/1541/vhdl_source/cia_registers.vhd |
| sync_fifo.vhd | vendor | fpga/ip/sync_fifo/vhdl_source/sync_fifo.vhd |
| stepper.vhd | app | fpga/1541/vhdl_source/stepper.vhd |
| mem_bus_pkg.vhd | vendor | fpga/ip/busses/vhdl_source/mem_bus_pkg.vhd |
| wd177x.vhd | app | fpga/1541/vhdl_source/wd177x.vhd |
| mm_drive_cpu.vhd | app, changed: the 1571 (decode, VIA1 port A, parallel cable on the CIA, fast serial, side, 2 MHz) removed, m(1) is the 1581 (T103 Q3) | fpga/1541/vhdl_source/mm_drive_cpu.vhd |
| noise_generator.vhd | vendor | fpga/io/sigma_delta_dac/vhdl_source/noise_generator.vhd |
| floppy_stream.vhd | vendor | fpga/1541/vhdl_source/floppy_stream.vhd |
| pseudo_dpram_8x32.vhd | vendor | fpga/ip/memory/vhdl_source/pseudo_dpram_8x32.vhd |
| endianness_pkg.vhd | vendor | fpga/ip/busses/vhdl_source/endianness_pkg.vhd |
| floppy_param_mem.vhd | vendor | fpga/1541/vhdl_source/floppy_param_mem.vhd |
| floppy_mem.vhd | vendor | fpga/1541/vhdl_source/floppy_mem.vhd |
| floppy.vhd | vendor | fpga/1541/vhdl_source/floppy.vhd |
| floppy_sound.vhd | vendor | fpga/1541/vhdl_source/floppy_sound.vhd |
| c1541_pkg.vhd | vendor | fpga/1541/vhdl_source/c1541_pkg.vhd |
| drive_registers.vhd | vendor, changed: the drive type register keeps bit 0 at 0, no 1571 (T103 Q3) | fpga/1541/vhdl_source/drive_registers.vhd |
| mem_bus_arbiter_pri.vhd | vendor | fpga/ip/busses/vhdl_source/mem_bus_arbiter_pri.vhd |
| mem_to_mem32.vhd | vendor | fpga/ip/busses/vhdl_source/mem_to_mem32.vhd |
| mm_drive.vhd | app, changed: the GCR floppy gets side 0, double-sided GCR was the 1571 (T103 Q3) | fpga/1541/vhdl_source/mm_drive.vhd |
| bin2gcr.vhd | vendor | fpga/1541/vhdl_source/bin2gcr.vhd |
| gcr2bin.vhd | vendor | fpga/1541/vhdl_source/gcr2bin.vhd |
| gcr_codec.vhd | vendor | fpga/1541/vhdl_source/gcr_codec.vhd |
| dpram.vhd | vendor | fpga/ip/memory/vhdl_source/dpram.vhd |
| slot_bus_pkg.vhd | vendor | fpga/ip/busses/vhdl_source/slot_bus_pkg.vhd |
| command_if_pkg.vhd | vendor | fpga/io/command_interface/vhdl_source/command_if_pkg.vhd |
| command_protocol.vhd | vendor | fpga/io/command_interface/vhdl_source/command_protocol.vhd |
| command_interface.vhd | vendor | fpga/io/command_interface/vhdl_source/command_interface.vhd |
| synchronizer_gzw.vhd | vendor | fpga/ip/synchroniser/vhdl_source/synchronizer_gzw.vhd |
| io_bus_bridge2.vhd | vendor | fpga/ip/busses/vhdl_source/io_bus_bridge2.vhd |
