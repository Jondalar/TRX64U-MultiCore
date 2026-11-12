# T182: the C64SCPU core's sources (forked from cores/c128/core_sources.tcl, which is the T113 measurement build of the C128 core).
# Sourced by mono/build_mono.tcl with TRX_MONO=1, which returns after defining core_read, core_synth and the layer-2 file list.
# Needs $part and $bdir; sets core_read core_synth core_files l2_v (and the Gideon file list g_files). Sourced from host/ (Vivado's working directory), so the paths are
# relative to host/.
# The files this core changes are its own copies (cores/c64scpu); icepi's unchanged chips and the unchanged layer-2 files are read in place from
# vendor/icepi and cores/c64u (nothing there is edited).
set core   ../vendor/icepi
set c64u   ../cores/c64u
set c64scpu ../cores/c64scpu
set kawari $core/vicii-kawari/hdl

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
    $core/c64_c1541/iecdrv_via6522.vhd ]
  # T182 step 2: the 65C816 (own generated copy, tools/scpu_vendor816.py), its package first
  read_vhdl -library work [list $c64scpu/cpu/s816_pkg.vhd $c64scpu/cpu/s816_bcdadder.vhd $c64scpu/cpu/s816_addsubbcd.vhd $c64scpu/cpu/s816_alu.vhd \
    $c64scpu/cpu/s816_addrgen.vhd $c64scpu/cpu/s816_mcode.vhd $c64scpu/cpu/s816_cpu.vhd]
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
    $kawari/comp_sync.v $kawari/bus_access.v $kawari/border.v $kawari/addressgen_spartan.v $c64scpu/vicii_c64scpu.v ]
  read_verilog -sv [list $c64scpu/c64_top_c64scpu.v $c64u/c64_turbo_c64u.v $c64scpu/c64_bus_arbiter_c64scpu.v]
  read_verilog $c64scpu/c64_core_c64scpu.v $c64scpu/c64_mem_c64scpu.v $c64scpu/c64scpu_ramd.v $c64scpu/c64scpu_romc.v $c64scpu/c64scpu_chr.v $c64scpu/scpu_pport.v $c64scpu/cpu65816_c64scpu.v $c64scpu/scpu_mem.v $c64scpu/scpu_cache.v $c64scpu/scpu_cfgtab.v $c64u/cart_slot_c64u.v $c64u/slot_port_c64u.v
  read_xdc -mode out_of_context $c64scpu/c64_core_ooc.xdc
}
set core_files [concat [trx_tree $core] [trx_tree $trxsid] [glob $c64scpu/*.v] [glob $c64scpu/cpu/*.vhd] \
  $c64u/c64_turbo_c64u.v $c64u/cart_slot_c64u.v $c64u/slot_port_c64u.v [lsort [glob mem/*.mem]]]
# one SID block (SID_REDIP; SID2 in the build's defines makes it two SIDs), no turbo (NO_TURBO: the engine stays as the CPU's clock source), no REU unless the build asks for it
set core_synth [list -include_dirs [list $core $kawari $core/c64_redip_sid/reDIP-SID/gateware] \
  -verilog_define SID_REDIP -verilog_define NO_TURBO -verilog_define NO_REU]
puts "=== c64scpu core"
set gdir $c64u/gideon
set g_names { io_bus_pkg.vhd io_bus_splitter.vhd c1541_timing.vhd pkg_6502_defs.vhd pkg_6502_decode.vhd proc_control.vhd bit_cpx_cpy.vhd shifter.vhd alu.vhd implied.vhd data_oper.vhd proc_registers.vhd proc_interrupt.vhd proc_core.vhd cpu6502.vhd via6522.vhd cia_pkg.vhd cia_timer.vhd tl_string_util_pkg.vhd cia_registers.vhd sync_fifo.vhd stepper.vhd mem_bus_pkg.vhd wd177x.vhd mm_drive_cpu.vhd noise_generator.vhd floppy_stream.vhd pseudo_dpram_8x32.vhd endianness_pkg.vhd floppy_param_mem.vhd floppy_mem.vhd floppy.vhd floppy_sound.vhd c1541_pkg.vhd drive_registers.vhd mem_bus_arbiter_pri.vhd mem_to_mem32.vhd mm_drive.vhd bin2gcr.vhd gcr2bin.vhd gcr_codec.vhd tape_speed_control.vhd c2n_playback_io.vhd spike_filter.vhd c2n_record.vhd dpram.vhd slot_bus_pkg.vhd command_if_pkg.vhd command_protocol.vhd command_interface.vhd synchronizer_gzw.vhd io_bus_bridge2.vhd  }
set g_files {}
foreach n $g_names { lappend g_files $gdir/$n }
lappend g_files $gdir/c64u_pseudo_dpram_8x32_x7.vhd $gdir/c64u_drive_mem.vhd $gdir/uci_unit_c64u.vhd $gdir/c64u_l2.vhd
set l2_v [list $c64scpu/layer2_c64scpu.v ../cores/c1541/drive_leds_map.v $c64u/c64u_io.v $c64u/c64u_sys.v $c64u/cart_fill_c64u.v \
  $c64u/dma_win_c64u.v $c64u/iec_bus_c64u.v $c64u/sid_sock_audio_c64u.v $c64scpu/dma_win_c64scpu.v $c64scpu/c64scpu_ddrx.v $c64scpu/c64scpu_regs.v $c64scpu/c64scpu_joyswap.v]
# Flat build (cores/c64scpu/mono/build_mono.tcl): no out-of-context run, the caller reads core_read and the layer-2 sources itself.
return
