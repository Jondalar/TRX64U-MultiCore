# T123: layer-2 synthesis of the null core (the flash filler), out of context (T94 D1: the core owns its recipe).
# Sourced from host/ (Vivado's working directory) by host/core.tcl (core stage). Needs $part and $bdir; sets l2_dcp.
# Two small Verilog files, no IP, no vendor tree.
set nul ../cores/null
set l2_dcp [file join $bdir layer2_null_ooc.dcp]
create_project -in_memory -part $part
set_property target_language Verilog [current_project]
read_verilog [list $nul/null_regs.v $nul/layer2_null.v]
synth_design -top layer2_null -part $part -mode out_of_context
write_checkpoint -force $l2_dcp
report_utilization -file $bdir/utilization_null_synth.rpt
set fh [open $bdir/utilization_null_synth.rpt]
foreach ln [split [read $fh] "\n"] {
  if {[regexp {^\| (Slice LUTs\*?|Slice Registers|Block RAM Tile|RAMB36|RAMB18|DSPs) } $ln]} { puts "=== T123 null synth $ln" }
}
close $fh
close_project
puts "=== T123: null module synthesized out of context ($l2_dcp)"
