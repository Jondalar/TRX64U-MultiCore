# The C64U vanilla core (T103) and layer 2, synthesized out of context and cached.
# Sourced by core.tcl (core stage). Needs $part and $bdir; sets core_dcp core_stub l2_dcp l2_stub.
# Sourced from host/ (Vivado's working directory), so the paths are relative to host/.
# Self-contained (T103 Q5): nothing under the deleted trx64 core, cores/common or host/ is read. icepi's tree (vendor/icepi) is read
# in place EXCEPT c64_top.v and c64_turbo.v, which this core has its own copies of (cores/c64u/c64_top_c64u.v: tape, POT;
# cores/c64u/c64_turbo_c64u.v: go's last LUT, T103 §5).
# All blocks are on (T103 Q7): TRX_FEATURES is not read. The SID is the 8-SID reDIP (SID_MULTI), as the spec asks for the
# stock set (UltiSID/reSID8); TRX_SID is not read either (the pipeline's TRX_SID=redip2 is a trx64 measurement).
set core   ../vendor/icepi
set c64u   ../cores/c64u
set kawari $core/vicii-kawari/hdl

# The icepi sources read their init files by bare name ($readmemh "x.mem"), relative to where synthesis runs: put copies here.
foreach f [concat [glob $core/c64_redip_sid/reDIP-SID/gateware/*.mem] [glob $kawari/*.bin] \
                  [glob $core/usb_hid_host/rom/*.mem]] {
  file copy -force $f .
}
file copy -force {*}[glob $core/c64_c1541/mem/*.mem] mem/
# the SID engine's tables (cores/c64u/trx_sid, read as ./sid_*.mem)
set trxsid $c64u/trx_sid
file copy -force {*}[glob $trxsid/mem/*.mem] .

# --- C64 core, out of context and cached (c64_core_c64u.v wraps this core's copy of icepi's c64_top) ---
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
  # icepi's .v files use SystemVerilog here and there (i++ in for loops); yosys takes them as SV, so read them the same way.
  # c64_top.v and c64_turbo.v are this core's own copies (c64_top_c64u.v, c64_turbo_c64u.v), the originals are not read.
  read_verilog -sv [list \
    {*}[lsearch -all -inline -not -regexp [glob $core/*.v] {/c64_(top|turbo)\.v$}] {*}[glob $core/cdc/*.v] $core/hdmi/hdmi.v \
    {*}[glob $core/c64_c1541/*.v] $core/c64_action_replay/c64_action_replay_pla.v \
    $core/c64_redip_sid/dc_blocker.v \
    {*}[glob $trxsid/rtl/*.v] \
    {*}[glob $core/usb_hid_host/rtl/*.v] \
    $kawari/videoram.v $kawari/serration.v $kawari/lumareg.v $kawari/equalization.v $kawari/divide.v \
    $kawari/colorreg.v $kawari/sprites.v $kawari/registers_scaled.v $kawari/raster.v \
    $kawari/pixel_sequencer.v $kawari/matrix.v $kawari/lightpen.v $kawari/hires_dvi_sync_scaled.v \
    $kawari/hires_pixel_sequencer.v $kawari/hires_matrix.v $kawari/hires_addressgen.v $kawari/cycles.v \
    $kawari/comp_sync.v $kawari/bus_access.v $kawari/border.v $kawari/addressgen_spartan.v $kawari/vicii.v ]

  # c64_top_c64u.v and c64_turbo_c64u.v: SystemVerilog like the rest of icepi (a reg on an output port is legal there, run 879)
  read_verilog -sv [list $c64u/c64_top_c64u.v $c64u/c64_turbo_c64u.v]
  read_verilog $c64u/c64_core_c64u.v $c64u/c64_mem_c64u.v $c64u/cart_slot_c64u.v $c64u/slot_port_c64u.v
  read_xdc -mode out_of_context $c64u/c64_core_ooc.xdc
}
set core_files [concat [trx_tree $core] [trx_tree $trxsid] \
  $c64u/c64_core_c64u.v $c64u/c64_top_c64u.v $c64u/c64_turbo_c64u.v $c64u/c64_mem_c64u.v $c64u/cart_slot_c64u.v $c64u/slot_port_c64u.v \
  $c64u/c64_core_ooc.xdc [lsort [glob mem/*.mem]]]
# the 8-SID reDIP behind the T25.1 decode and mixer, all blocks on: no NO_REU, NO_SAMPLER, NO_TURBO, no SID_REDIP
set core_synth [list -include_dirs [list $core $kawari $core/c64_redip_sid/reDIP-SID/gateware] -verilog_define SID_MULTI]
puts "=== SID: reDIP, 8 SIDs (c64u: fixed)"
puts "=== c64u blocks: reu sampler turbo uci streams tape paddles drive-a (mm_drive 1541/1581) all on"
lassign [trx_ooc c64_core_c64u $part $core_files $core_read $core_synth] core_dcp core_stub

# --- layer 2: layer2_c64u.v with this core's checkpoint inside; the full design sees it as a black box (its stub) ---
set_msg_config -id {Project 1-486} -new_severity INFO
set l2_dcp  [file join $bdir layer2_c64u_ooc.dcp]
set l2_stub [file join $bdir layer2_c64u_stub.v]
# T87: the layer 2 synthesis is cached by the core key (TRX_L2_KEY, ci/lib.sh ooc_key)
set l2_key ""
if {[info exists ::env(TRX_L2_KEY)]} { set l2_key $::env(TRX_L2_KEY) }
set l2_cdcp  [file join $::trx_cache ooc_l2_${l2_key}.dcp]
set l2_cstub [file join $::trx_cache ooc_l2_${l2_key}.stub.v]
if {$l2_key ne "" && [file exists $l2_cdcp] && [file exists $l2_cstub]} {
  file copy -force $l2_cdcp $l2_dcp
  file copy -force $l2_cstub $l2_stub
  puts "=== layer 2 OOC: from cache $l2_cdcp"
} else {
  create_project -in_memory -part $part
  set_property target_language Verilog [current_project]
  # Gideon's VHDL this core uses (cores/c64u/gideon, dependency order) and ours around it
  set gdir $c64u/gideon
  set g_names { io_bus_pkg.vhd io_bus_splitter.vhd c1541_timing.vhd pkg_6502_defs.vhd pkg_6502_decode.vhd proc_control.vhd bit_cpx_cpy.vhd shifter.vhd alu.vhd implied.vhd data_oper.vhd proc_registers.vhd proc_interrupt.vhd proc_core.vhd cpu6502.vhd via6522.vhd cia_pkg.vhd cia_timer.vhd tl_string_util_pkg.vhd cia_registers.vhd sync_fifo.vhd stepper.vhd mem_bus_pkg.vhd wd177x.vhd mm_drive_cpu.vhd noise_generator.vhd floppy_stream.vhd pseudo_dpram_8x32.vhd endianness_pkg.vhd floppy_param_mem.vhd floppy_mem.vhd floppy.vhd floppy_sound.vhd c1541_pkg.vhd drive_registers.vhd mem_bus_arbiter_pri.vhd mem_to_mem32.vhd mm_drive.vhd bin2gcr.vhd gcr2bin.vhd gcr_codec.vhd tape_speed_control.vhd c2n_playback_io.vhd spike_filter.vhd c2n_record.vhd dpram.vhd slot_bus_pkg.vhd command_if_pkg.vhd command_protocol.vhd command_interface.vhd synchronizer_gzw.vhd io_bus_bridge2.vhd  }
  set g_files {}
  foreach n $g_names { lappend g_files $gdir/$n }
  lappend g_files $gdir/c64u_pseudo_dpram_8x32_x7.vhd $gdir/c64u_drive_mem.vhd $gdir/uci_unit_c64u.vhd $gdir/c64u_l2.vhd
  read_vhdl -library work $g_files
  read_verilog $core_stub $c64u/layer2_c64u.v $c64u/c64u_io.v $c64u/c64u_sys.v $c64u/cart_fill_c64u.v \
    $c64u/dma_win_c64u.v $c64u/iec_bus_c64u.v $c64u/pktport_core_c64u.v $c64u/udp_stream_c64u.v $c64u/udp_audio_c64u.v \
    $c64u/stream_mux_c64u.v $c64u/sid_sock_audio_c64u.v
  synth_design -top layer2_c64u -part $part -mode out_of_context -include_dirs $c64u
  write_verilog -force -mode synth_stub $l2_stub
  write_checkpoint -force $l2_dcp
  report_utilization -hierarchical -hierarchical_depth 3 -file $bdir/utilization_c64u_hier_synth.rpt
  close_project
  if {$l2_key ne ""} {
    file copy -force $l2_dcp $l2_cdcp.tmp.[pid]
    file copy -force $l2_stub $l2_cstub.tmp.[pid]
    file rename -force $l2_cdcp.tmp.[pid] $l2_cdcp
    file rename -force $l2_cstub.tmp.[pid] $l2_cstub
  }
}
puts "=== c64u: layer 2 synthesized out of context ($l2_dcp)"
