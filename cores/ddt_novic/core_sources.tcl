# T168: layer-2 synthesis of the DDT "Look Ma, No VIC" core, out of context (T94 D1: the core owns its recipe).
# Sourced from host/ (Vivado's working directory) by host/core.tcl (core stage). Needs $part and $bdir; sets l2_dcp. DDT's scroller and text screen (rtl/, CC BY 4.0) are read as he
# wrote them; his video_pattern.v is trx/ddt_novic_video.v here (rtl/video_pattern.v.diff). The shared glue is the Mandelbrot core's trx/ (register block, raster, frame writer,
# font ROM); the font data is the host's mem/ovl_font8.mem. The Mandelbrot-only files of that directory are not read.
set d ../cores/ddt_novic
set m ../cores/ddt_mandel
set l2_dcp [file join $bdir layer2_ddt_novic_ooc.dcp]
create_project -in_memory -part $part
set_property target_language Verilog [current_project]
read_verilog [list $d/rtl/scroller.v $d/rtl/screen_matrix.v]
read_verilog [concat [lsort [glob $d/trx/*.v]] [list $m/trx/ddt_regs.v $m/trx/ddt_dots.v $m/trx/ddt_pixw.v $m/trx/ddt_clk_load.v $m/trx/c64_font_rom.v]]
read_verilog $d/layer2_ddt_novic.v
synth_design -top layer2_ddt_novic -part $part -mode out_of_context
write_checkpoint -force $l2_dcp
report_utilization -file $bdir/utilization_ddt_novic_synth.rpt
report_utilization -hierarchical -hierarchical_depth 3 -file $bdir/utilization_ddt_novic_hier_synth.rpt
set fh [open $bdir/utilization_ddt_novic_synth.rpt]
foreach ln [split [read $fh] "\n"] {
  if {[regexp {^\| (Slice LUTs\*?|Slice Registers|Block RAM Tile|RAMB36|RAMB18|DSPs) } $ln]} { puts "=== T168 ddt_novic synth $ln" }
}
close $fh
close_project
puts "=== T168: DDT Look Ma, No VIC module synthesized out of context ($l2_dcp)"
