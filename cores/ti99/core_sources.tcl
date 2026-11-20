# Layer-2 synthesis of the TI-99/4A core, out of context (the core owns its recipe).
# Sourced from host/ (Vivado's working directory) by host/core.tcl (core stage). Needs $part and $bdir; sets l2_dcp.
# Read: Mega99's machine as changed here (rtl/src, see rtl/NOTICE; its clkgen is replaced by rtl/trx/clkgen_nco.v), our glue (rtl/trx) and the
# partition's edge (layer2_ti99.v). No 1541, no IEC bus, so neither Gideon's VHDL nor cores/common is read.
set ti ../cores/ti99
set l2_dcp [file join $bdir layer2_ti99_ooc.dcp]
create_project -in_memory -part $part
set_property target_language Verilog [current_project]
set files [list]
foreach f [concat [glob -nocomplain $ti/rtl/src/*.v] [glob -nocomplain $ti/rtl/src/*/*.v]] { lappend files $f }
foreach f [glob $ti/rtl/trx/*.v] { lappend files $f }
lappend files $ti/layer2_ti99.v
read_verilog $files
synth_design -top layer2_ti99 -part $part -mode out_of_context
write_checkpoint -force $l2_dcp
report_utilization -file $bdir/utilization_ti99_synth.rpt
report_utilization -hierarchical -hierarchical_depth 3 -file $bdir/utilization_ti99_hier_synth.rpt
set fh [open $bdir/utilization_ti99_synth.rpt]
foreach ln [split [read $fh] "\n"] {
  if {[regexp {^\| (Slice LUTs\*?|Slice Registers|Block RAM Tile|RAMB36|RAMB18|DSPs) } $ln]} { puts "=== ti99 synth $ln" }
}
close $fh
close_project
puts "=== TI-99/4A module synthesized out of context ($l2_dcp)"
