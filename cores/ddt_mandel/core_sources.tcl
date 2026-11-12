# T168: layer-2 synthesis of the DDT Mandelbrot core (mandelbrUlt), out of context (T94 D1: the core owns its recipe).
# Sourced from host/ (Vivado's working directory) by host/core.tcl (core stage). Needs $part and $bdir; sets l2_dcp. DDT's engines (rtl/, CC BY 4.0) are read as he wrote
# them (the screen text, trx/screen_matrix.v, has one line changed: rtl/screen_matrix.v.diff; the engine and its scheduler, trx/pixel_gen_mandelbrot.sv and trx/fb_scanline_writer.sv, are pipelined with two pixels per engine: T168 S5, rtl/*.diff); the glue is ours (trx/). The ROMs are $readmemh files: the palette (made by tools/ddt_palette.py) is copied to the working directory, the font is the host's mem/ovl_font8.mem.
# The frame buffer and the line buffer are xpm_memory_tdpram (his), so the XPM library is switched on.
set d ../cores/ddt_mandel
set l2_dcp [file join $bdir layer2_ddt_mandel_ooc.dcp]
file copy -force $d/mem/palette_rgb565_256.hex .
create_project -in_memory -part $part
set_property target_language Verilog [current_project]
set_property XPM_LIBRARIES {XPM_MEMORY} [current_project]
read_verilog -sv [list $d/trx/pixel_gen_mandelbrot.sv $d/trx/fb_scanline_writer.sv $d/rtl/fb_bram_dma.sv $d/rtl/linebuf_iter8.sv \
  $d/rtl/framebuffer_bram.sv $d/rtl/joystick_buttons.sv]
read_verilog [lsort [glob $d/trx/*.v]]
read_verilog $d/layer2_ddt_mandel.v
synth_design -top layer2_ddt_mandel -part $part -mode out_of_context
write_checkpoint -force $l2_dcp
report_utilization -file $bdir/utilization_ddt_mandel_synth.rpt
report_utilization -hierarchical -hierarchical_depth 3 -file $bdir/utilization_ddt_mandel_hier_synth.rpt
set fh [open $bdir/utilization_ddt_mandel_synth.rpt]
foreach ln [split [read $fh] "\n"] {
  # "Slice LUTs*" in an out-of-context report: the asterisk is the note about the LUT budget (T95 5.15)
  if {[regexp {^\| (Slice LUTs\*?|Slice Registers|Block RAM Tile|RAMB36|RAMB18|DSPs) } $ln]} { puts "=== T168 ddt_mandel synth $ln" }
}
close $fh
close_project
puts "=== T168: DDT Mandelbrot module synthesized out of context ($l2_dcp)"
