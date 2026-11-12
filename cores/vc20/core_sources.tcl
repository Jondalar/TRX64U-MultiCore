# T83: layer-2 synthesis of the VC-20 core, out of context (T94 D1: the core owns its recipe).
# Sourced from host/ (Vivado's working directory) by host/build.tcl (monolith and second
# configuration) and by host/core.tcl (core stage). Needs $part and $bdir; sets l2_dcp.
# This file belongs to the core: section of host/layers.txt.
if {![info exists common]} { set common ../cores/common }
if {![info exists gvhdl]}  { source gideon_vhdl.tcl }

set vic ../cores/vc20/rtl
set l2_dcp [file join $bdir layer2_vc20_ooc.dcp]
create_project -in_memory -part $part
set_property target_language Verilog [current_project]
read_vhdl -library work [list \
  $vic/src/t65/T65_Pack.vhd $vic/src/t65/T65_ALU.vhd $vic/src/t65/T65_MCode.vhd $vic/src/t65/T65.vhd \
  $vic/src/vc20_via6522.vhd $vic/src/vic20_clocks.vhd $vic/src/vic20_keyboard.vhd $vic/src/m6561.vhd \
  $vic/trx/vc20_ram.vhd $vic/src/vic20.vhd $vic/trx/vc20_glue.vhd $vic/trx/vc20_regs.vhd $vic/trx/vc20_pixw.vhd $vic/trx/vc20_core.vhd ]
read_verilog ../cores/vc20/layer2_vc20.v $common/iec_bus.v ../cores/c1541/drive_leds_map.v
read_vhdl -library work $gvhdl
synth_design -top layer2_vc20 -part $part -mode out_of_context
write_checkpoint -force $l2_dcp
report_utilization -file $bdir/utilization_vc20_synth.rpt
report_utilization -hierarchical -hierarchical_depth 3 -file $bdir/utilization_vc20_hier_synth.rpt
set fh [open $bdir/utilization_vc20_synth.rpt]
foreach ln [split [read $fh] "\n"] {
  if {[regexp {^\| (Slice LUTs|Slice Registers|Block RAM Tile|RAMB36|RAMB18|DSPs) } $ln]} { puts "=== T83 vc20 synth $ln" }
}
close $fh
close_project
puts "=== T83: VC-20 module synthesized out of context ($l2_dcp)"
