# T90: layer-2 synthesis of the C264 core, out of context. Since T164 WP19b the C264 is a large core (cores/c264/mono/build_mono.tcl, its own file list): this
# recipe is sourced only by host/build.tcl for a dev monolith with TRX_CORE=c264 (host/ is pinned, the hook stays), and no pipeline lane runs it.
# Needs $part and $bdir; sets l2_dcp.
if {![info exists common]} { set common ../cores/common }
if {![info exists gvhdl]}  { source gideon_vhdl.tcl }

set c16 ../cores/c264/rtl
set l2_dcp [file join $bdir layer2_c264_ooc.dcp]
create_project -in_memory -part $part
set_property target_language Verilog [current_project]
read_vhdl -library work [list \
  $c16/src/t65/T65_Pack.vhd $c16/src/t65/T65_ALU.vhd $c16/src/t65/T65_MCode.vhd $c16/src/t65/T65.vhd ]
read_verilog [list $c16/src/ted.v $c16/src/colors_to_rgb.v \
  $c16/trx/c264_nco.v $c16/trx/c264_keys.v $c16/trx/c264_audio.v $c16/trx/c264_mem.v $c16/trx/c264_win.v \
  $c16/trx/c264_regs.v $c16/trx/c264_pixw.v $c16/trx/c264_cpu.v $c16/trx/c264_core.v ]
read_verilog ../cores/c264/layer2_c264.v $common/iec_bus.v ../cores/c1541/drive_leds_map.v
read_vhdl -library work $gvhdl
synth_design -top layer2_c264 -part $part -mode out_of_context
write_checkpoint -force $l2_dcp
report_utilization -file $bdir/utilization_c264_synth.rpt
report_utilization -hierarchical -hierarchical_depth 3 -file $bdir/utilization_c264_hier_synth.rpt
set fh [open $bdir/utilization_c264_synth.rpt]
foreach ln [split [read $fh] "\n"] {
  if {[regexp {^\| (Slice LUTs|Slice Registers|Block RAM Tile|RAMB36|RAMB18|DSPs) } $ln]} { puts "=== T90 c264 synth $ln" }
}
close $fh
close_project
puts "=== T90: C264 module synthesized out of context ($l2_dcp)"
