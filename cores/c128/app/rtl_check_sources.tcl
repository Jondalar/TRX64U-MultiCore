# The source list of the C128's design for tools/rtl_check.py (tests/rtl_check, the case of cores/c128/app/gateware.toml): cores/c128/core_sources.tcl with the list of Gideon's VHDL files (its
# `foreach n $g_names { lappend g_files $gdir/$n }`) written out as names, because the checker reads the file names of a list as text and cannot run the loop; without them mm_drive, drive_registers and the
# record packages are no part of the design, and the drive window cannot be followed. A file that cores/c128/core_sources.tcl adds to or takes from the register path has to be changed here too.
# T113 measurement build: the C128 core skeleton (must-have set of Q2) and its layer 2, synthesized out of context.
# Sourced by core.tcl (core stage) or by the measurement driver. Needs $part and $bdir; sets core_dcp core_stub l2_dcp l2_stub.
# Sourced from host/ (Vivado's working directory), so the paths are relative to host/.
# Q1 (isolated): the files this core changes are its own copies (cores/c128); icepi's unchanged chips and the unchanged
# layer-2 files are read in place from vendor/icepi and cores/c64u (nothing there is edited). The real core takes copies of
# those as c64u does; for the measurement only the modified ones are forked.
# Variant: TRX_VDC = none | whole | split (env), default whole.
set core   ../vendor/icepi
set c64u   ../cores/c64u
set c128   ../cores/c128
set kawari $core/vicii-kawari/hdl
set vdcmode whole
if {[info exists ::env(TRX_VDC)] && $::env(TRX_VDC) ne ""} { set vdcmode $::env(TRX_VDC) }
set vdef {}
if {$vdcmode eq "whole"} { set vdef VDC_WHOLE } elseif {$vdcmode eq "split"} { set vdef VDC_SPLIT }

foreach f [concat [glob $core/c64_redip_sid/reDIP-SID/gateware/*.mem] [glob $kawari/*.bin] \
                  [glob $core/usb_hid_host/rom/*.mem]] {
  file copy -force $f .
}
file copy -force {*}[glob $core/c64_c1541/mem/*.mem] mem/
set trxsid $c64u/trx_sid
file copy -force {*}[glob $trxsid/mem/*.mem] .

set core_read {
  read_verilog $core/vicii_kawari.vh $kawari/common.vh
  set_property is_global_include true [get_files [list $core/vicii_kawari.vh $kawari/common.vh]]
  read_vhdl -library work [list \
    $core/6502n/pkg_6502_defs.vhd $core/6502n/pkg_6502_opcodes.vhd $core/6502n/pkg_6502_decode.vhd \
    $core/6502n/alu.vhd $core/6502n/bit_cpx_cpy.vhd $core/6502n/data_oper.vhd $core/6502n/implied.vhd \
    $core/6502n/shifter.vhd $core/6502n/proc_control.vhd $core/6502n/proc_interrupt.vhd \
    $core/6502n/proc_registers.vhd $core/6502n/proc_core.vhd \
    $core/T65/T65_Pack.vhd $core/T65/T65_ALU.vhd $core/T65/T65_MCode.vhd $core/T65/T65.vhd \
    $core/c64_c1541/iecdrv_via6522.vhd $c128/c128_mmu8722.vhd ]
  if {[info exists ::env(TRX_Z80)] && $::env(TRX_Z80) eq "1"} {
    read_vhdl -library work [list $c128/z80/T80_Pack.vhd $c128/z80/T80_MCode.vhd $c128/z80/T80_ALU.vhd $c128/z80/T80_Reg.vhd \
      $c128/z80/T80.vhd $c128/z80/T80pa.vhd $c128/z80/cpu_z80.vhd]
  }
  read_verilog -sv [list \
    $core/c64_redip_cia/reDIP-CIA/gateware/CIA/cia_pkg.sv \
    {*}[lsort [glob $core/c64_redip_cia/reDIP-CIA/gateware/CIA/*.sv]] \
    $core/c64_redip_cia/cia_core.sv \
    $core/c64_redip_cia/reDIP-CIA/gateware/VIA/via_pkg.sv \
    {*}[lsort [glob $core/c64_redip_cia/reDIP-CIA/gateware/VIA/*.sv]] \
    $core/c64_redip_cia/via_core.sv \
    $core/c64_redip_sid/reDIP-SID/gateware/sid_pkg.sv \
    {*}[lsort [glob $core/c64_redip_sid/reDIP-SID/gateware/*.sv]] \
    $core/c64_redip_sid/sid_api.sv $core/c64_redip_sid/sid_multi.sv $core/c64_redip_sid/muladd.sv \
    {*}[glob $core/hdmi/*.sv] ]
  read_verilog -sv [list \
    {*}[lsearch -all -inline -not -regexp [glob $core/*.v] {/c64_(top|turbo|bus_arbiter)\.v$}] {*}[glob $core/cdc/*.v] $core/hdmi/hdmi.v \
    {*}[glob $core/c64_c1541/*.v] $core/c64_action_replay/c64_action_replay_pla.v \
    $core/c64_redip_sid/dc_blocker.v \
    {*}[glob $trxsid/rtl/*.v] \
    {*}[glob $core/usb_hid_host/rtl/*.v] \
    $kawari/videoram.v $kawari/serration.v $kawari/lumareg.v $kawari/equalization.v $kawari/divide.v \
    $kawari/colorreg.v $kawari/sprites.v $kawari/registers_scaled.v $kawari/raster.v \
    $kawari/pixel_sequencer.v $kawari/matrix.v $kawari/lightpen.v $kawari/hires_dvi_sync_scaled.v \
    $kawari/hires_pixel_sequencer.v $kawari/hires_matrix.v $kawari/hires_addressgen.v $kawari/cycles.v \
    $kawari/comp_sync.v $kawari/bus_access.v $kawari/border.v $kawari/addressgen_spartan.v $c128/vicii_c128.v ]
  read_verilog -sv [list $c128/c64_top_c128.v $c128/cpu6510_c128.v $c64u/c64_turbo_c64u.v $c128/c64_bus_arbiter_c128.v $c128/c64_pla_c128.v \
    $c128/c128_map.v $c128/c128_x.v]
  read_verilog -sv [list $c128/vdc/vdcram.v $c128/vdc/vdc_cpu.v]
  if {$vdef eq "VDC_WHOLE"} {
    read_verilog -sv [list $c128/vdc/vdc_top.sv $c128/vdc/vdc_signals.sv $c128/vdc/vdc_signals_h.sv $c128/vdc/vdc_signals_v.sv \
      $c128/vdc/vdc_ramiface.sv $c128/vdc/vdc_video.sv]
  }
  read_verilog $c128/c64_core_c128.v $c128/c64_mem_c128.v $c128/c64_mem_c128d.v $c128/c128_ramd.v $c128/c128_romc.v $c128/c128_chr.v $c64u/cart_slot_c64u.v $c64u/slot_port_c64u.v
  read_xdc -mode out_of_context $c128/c64_core_ooc.xdc
}
set core_files [concat [trx_tree $core] [trx_tree $trxsid] [glob $c128/*.v] [glob $c128/*.vhd] [glob $c128/vdc/*] \
  $c64u/c64_turbo_c64u.v $c64u/cart_slot_c64u.v $c64u/slot_port_c64u.v [lsort [glob mem/*.mem]]]
set vdcdefs {}
if {[info exists ::env(TRX_ROM)] && $::env(TRX_ROM) eq "ddr2"} { lappend vdcdefs -verilog_define ROM_DDR2 }
if {$vdef ne ""} { lappend vdcdefs -verilog_define $vdef }
# one SID (the reDIP block, sid_dual off), no turbo (NO_TURBO: the engine stays as the CPU's clock source), no REU
set core_synth [list -include_dirs [list $core $kawari $core/c64_redip_sid/reDIP-SID/gateware] \
  -verilog_define SID_REDIP -verilog_define NO_TURBO -verilog_define NO_REU {*}$vdcdefs]
puts "=== c128 variant: VDC $vdcmode"
set gdir $c64u/gideon
# generated into explicit file names for tools/rtl_check.py (the checker reads the names of a list as text and does not run `foreach`)
set g_files {
  $gdir/io_bus_pkg.vhd
  $gdir/io_bus_splitter.vhd
  $gdir/c1541_timing.vhd
  $gdir/pkg_6502_defs.vhd
  $gdir/pkg_6502_decode.vhd
  $gdir/proc_control.vhd
  $gdir/bit_cpx_cpy.vhd
  $gdir/shifter.vhd
  $gdir/alu.vhd
  $gdir/implied.vhd
  $gdir/data_oper.vhd
  $gdir/proc_registers.vhd
  $gdir/proc_interrupt.vhd
  $gdir/proc_core.vhd
  $gdir/cpu6502.vhd
  $gdir/via6522.vhd
  $gdir/cia_pkg.vhd
  $gdir/cia_timer.vhd
  $gdir/tl_string_util_pkg.vhd
  $gdir/cia_registers.vhd
  $gdir/sync_fifo.vhd
  $gdir/stepper.vhd
  $gdir/mem_bus_pkg.vhd
  $gdir/wd177x.vhd
  $gdir/mm_drive_cpu.vhd
  $gdir/noise_generator.vhd
  $gdir/floppy_stream.vhd
  $gdir/pseudo_dpram_8x32.vhd
  $gdir/endianness_pkg.vhd
  $gdir/floppy_param_mem.vhd
  $gdir/floppy_mem.vhd
  $gdir/floppy.vhd
  $gdir/floppy_sound.vhd
  $gdir/c1541_pkg.vhd
  $gdir/drive_registers.vhd
  $gdir/mem_bus_arbiter_pri.vhd
  $gdir/mem_to_mem32.vhd
  $gdir/mm_drive.vhd
  $gdir/bin2gcr.vhd
  $gdir/gcr2bin.vhd
  $gdir/gcr_codec.vhd
  $gdir/tape_speed_control.vhd
  $gdir/c2n_playback_io.vhd
  $gdir/spike_filter.vhd
  $gdir/c2n_record.vhd
  $gdir/dpram.vhd
  $gdir/slot_bus_pkg.vhd
  $gdir/command_if_pkg.vhd
  $gdir/command_protocol.vhd
  $gdir/command_interface.vhd
  $gdir/synchronizer_gzw.vhd
  $gdir/io_bus_bridge2.vhd
}
lappend g_files $gdir/c64u_pseudo_dpram_8x32_x7.vhd $gdir/c64u_drive_mem.vhd $gdir/uci_unit_c64u.vhd $gdir/c64u_l2.vhd
set l2_v [list $c128/layer2_c128.v ../cores/c1541/drive_leds_map.v $c64u/c64u_io.v $c64u/c64u_sys.v $c64u/cart_fill_c64u.v \
  $c64u/dma_win_c64u.v $c64u/iec_bus_c64u.v $c64u/sid_sock_audio_c64u.v $c128/c128_rome.v $c128/c128_ddrx.v $c128/dma_win_c128.v $c128/c128_regs.v $c128/c128_act.v $c128/c128_keys.v $c128/c128_joyswap.v $c128/c128_pixw.v $c128/c128_rgbi.v]
# Flat build (cores/c128/mono/build_mono.tcl): no out-of-context run, the caller reads core_read and the layer-2 sources itself.
if {[info exists ::env(TRX_MONO)] && $::env(TRX_MONO) eq "1"} { return }
lassign [trx_ooc c64_core_c128 $part $core_files $core_read $core_synth] core_dcp core_stub

# --- layer 2: layer2_c128.v with this core's checkpoint inside; the full design sees it as a black box (its stub) ---
set_msg_config -id {Project 1-486} -new_severity INFO
set l2_dcp  [file join $bdir layer2_c128_ooc.dcp]
set l2_stub [file join $bdir layer2_c128_stub.v]
create_project -in_memory -part $part
set_property target_language Verilog [current_project]
read_vhdl -library work $g_files
read_verilog $core_stub
read_verilog -sv $l2_v
synth_design -top layer2_c128 -part $part -mode out_of_context -include_dirs $c64u {*}$vdcdefs
write_verilog -force -mode synth_stub $l2_stub
write_checkpoint -force $l2_dcp
report_utilization -hierarchical -hierarchical_depth 3 -file $bdir/utilization_c128_hier_synth.rpt
report_utilization -file $bdir/utilization_c128_l2_synth.rpt
close_project
puts "=== c128: layer 2 synthesized out of context ($l2_dcp)"
