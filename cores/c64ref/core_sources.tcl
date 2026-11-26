# The c64ref core (T115) and layer 2, synthesized out of context and cached: the reference core the host's static is routed with
# (host/layers.txt reference:). Sourced by build.tcl (host stage, the core named by layers.txt) - it is never a core stage of its own.
# Needs $part and $bdir; sets core_dcp core_stub l2_dcp l2_stub gvhdl top_synth.
# Sourced from host/ (Vivado's working directory), so the paths are relative to host/.
# Self-contained (T103 Q5): nothing under the deleted trx64 core, cores/common or host/ is read. icepi's tree (vendor/icepi) is read
# in place EXCEPT c64_top.v and c64_turbo.v, which this core has its own copies of (cores/c64ref/c64_top_c64ref.v: tape, POT;
# cores/c64ref/c64_turbo_c64ref.v: go's last LUT, T103 §5).
# Built OFF in this core: REU, turbo (NO_REU, NO_TURBO), and not in it at all: the sampler, the datasette, the UDP streams
# (T115 section 5). The SID is out (T107 D9): c64_top_c64ref.v drives silence on the audio words. TRX_FEATURES only picks the HOST's top_synth defines below (jtag/uci/streams), exactly as the deleted trx64 core did.
set core   ../vendor/icepi
set c64ref   ../cores/c64ref
set kawari $core/vicii-kawari/hdl

# The icepi sources read their init files by bare name ($readmemh "x.mem"), relative to where synthesis runs: put copies here.
foreach f [concat [glob $core/c64_redip_sid/reDIP-SID/gateware/*.mem] [glob $kawari/*.bin] \
                  [glob $core/usb_hid_host/rom/*.mem]] {
  file copy -force $f .
}
file copy -force {*}[glob $core/c64_c1541/mem/*.mem] mem/

# --- C64 core, out of context and cached (c64_core_c64ref.v wraps this core's copy of icepi's c64_top) ---
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
  # c64_top.v and c64_turbo.v are this core's own copies (c64_top_c64ref.v, c64_turbo_c64ref.v), the originals are not read.
  read_verilog -sv [list \
    {*}[lsearch -all -inline -not -regexp [glob $core/*.v] {/c64_(top|turbo)\.v$}] {*}[glob $core/cdc/*.v] $core/hdmi/hdmi.v \
    {*}[glob $core/c64_c1541/*.v] $core/c64_action_replay/c64_action_replay_pla.v \
    $core/c64_redip_sid/dc_blocker.v \
    {*}[glob $core/usb_hid_host/rtl/*.v] \
    $kawari/videoram.v $kawari/serration.v $kawari/lumareg.v $kawari/equalization.v $kawari/divide.v \
    $kawari/colorreg.v $kawari/sprites.v $kawari/registers_scaled.v $kawari/raster.v \
    $kawari/pixel_sequencer.v $kawari/matrix.v $kawari/lightpen.v $kawari/hires_dvi_sync_scaled.v \
    $kawari/hires_pixel_sequencer.v $kawari/hires_matrix.v $kawari/hires_addressgen.v $kawari/cycles.v \
    $kawari/comp_sync.v $kawari/bus_access.v $kawari/border.v $kawari/addressgen_spartan.v $kawari/vicii.v ]

  # c64_top_c64ref.v and c64_turbo_c64ref.v: SystemVerilog like the rest of icepi (a reg on an output port is legal there, run 879)
  read_verilog -sv [list $c64ref/c64_top_c64ref.v $c64ref/c64_turbo_c64ref.v]
  read_verilog $c64ref/c64_core_c64ref.v $c64ref/c64_mem_c64ref.v
  read_xdc -mode out_of_context $c64ref/c64_core_ooc.xdc
}
set core_files [concat [trx_tree $core] \
  $c64ref/c64_core_c64ref.v $c64ref/c64_top_c64ref.v $c64ref/c64_turbo_c64ref.v $c64ref/c64_mem_c64ref.v \
  $c64ref/c64_core_ooc.xdc [lsort [glob mem/*.mem]]]
# no SID engine in the core (T107 D9); NO_REU and NO_TURBO as before
set core_synth [list -include_dirs [list $core $kawari $core/c64_redip_sid/reDIP-SID/gateware] -verilog_define NO_REU -verilog_define NO_TURBO]
puts "=== SID: none (c64ref, T107 D9: silence stand-in in c64_top_c64ref.v)"
puts "=== c64ref blocks: the machine only (T265 D12): CPU, VIC-II, CIAs, RAM and ROMs, SID stand-in; reu turbo off (NO_REU NO_TURBO); no cartridge, slot, UCI, drive, register windows"
lassign [trx_ooc c64_core_c64ref $part $core_files $core_read $core_synth] core_dcp core_stub

# Gideon's VHDL the HOST reads ($gvhdl: build.tcl's synth of top)
source gideon_vhdl.tcl

# The host's top_synth defines: the blocks the host itself carries, from TRX_FEATURES as trx64/core_sources.tcl reads it
# (unset or empty = all on; the CI sets jtag: UCI and the UDP streams off, the JTAG debug master on).
set trx_optional {reu sampler uci jtag turbo streams}
set trx_on $trx_optional
if {[info exists ::env(TRX_FEATURES)] && $::env(TRX_FEATURES) ne ""} {
  set trx_on [split $::env(TRX_FEATURES) ", "]
}
set top_synth {}
if {"uci" ni $trx_on}     { lappend top_synth -verilog_define NO_UCI }
if {"jtag" ni $trx_on}    { lappend top_synth -verilog_define NO_JTAG_DBG }
if {"streams" ni $trx_on} { lappend top_synth -verilog_define NO_STREAMS }

# --- layer 2: layer2_c64ref.v with this core's checkpoint inside; the full design sees it as a black box (its stub) ---
set_msg_config -id {Project 1-486} -new_severity INFO
set l2_dcp  [file join $bdir layer2_c64ref_ooc.dcp]
set l2_stub [file join $bdir layer2_c64ref_stub.v]
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
  read_verilog $core_stub $c64ref/layer2_c64ref.v
  synth_design -top layer2 -part $part -mode out_of_context
  write_verilog -force -mode synth_stub $l2_stub
  write_checkpoint -force $l2_dcp
  report_utilization -hierarchical -hierarchical_depth 3 -file $bdir/utilization_c64ref_hier_synth.rpt
  close_project
  if {$l2_key ne ""} {
    file copy -force $l2_dcp $l2_cdcp.tmp.[pid]
    file copy -force $l2_stub $l2_cstub.tmp.[pid]
    file rename -force $l2_cdcp.tmp.[pid] $l2_cdcp
    file rename -force $l2_cstub.tmp.[pid] $l2_cstub
  }
}
puts "=== c64ref: layer 2 synthesized out of context ($l2_dcp)"
