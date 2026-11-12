# T89: layer-2 synthesis of the Apple //e core, out of context (T94 D1: the core owns its recipe).
# Sourced from host/ (Vivado's working directory) by host/core.tcl (core stage). Needs $part and $bdir; sets l2_dcp.
# The core has no 1541 and no IEC bus, so neither Gideon's VHDL nor cores/common is read. What is read: the Apple //e machine of
# NanoApple2 as changed here (rtl/src: a clock enable on every process, no Gowin block, no ROM), our glue (rtl/trx) and the
# partition's edge (layer2_apple2.v).
set apl ../cores/apple2
set l2_dcp [file join $bdir layer2_apple2_ooc.dcp]
create_project -in_memory -part $part
set_property target_language Verilog [current_project]
# the mouse card's 6805 (jt6805, Verilog) reads its microcode with $readmemb from the working directory
file copy -force $apl/rtl/mouse/jt6805/6805.uc [pwd]
read_vhdl -library work [list \
  $apl/rtl/src/R65Cx2.vhd $apl/rtl/src/timing_generator.vhd $apl/rtl/src/video_generator.vhd \
  $apl/rtl/src/drive_ii.vhd $apl/rtl/src/disk_ii.vhd $apl/rtl/src/apple2.vhd \
  $apl/rtl/mouse/pia6821.vhd $apl/rtl/mouse/applemouse.vhd \
  $apl/rtl/trx/a2_ram.vhd $apl/rtl/trx/a2_glue.vhd $apl/rtl/trx/a2_trk.vhd $apl/rtl/trx/a2_hdd.vhd $apl/rtl/trx/a2_palette_pkg.vhd \
  $apl/rtl/trx/a2_pix.vhd $apl/rtl/trx/a2_win.vhd $apl/rtl/trx/a2_regs.vhd $apl/rtl/trx/a2_aux.vhd $apl/rtl/trx/a2_core.vhd ]
read_verilog [list $apl/layer2_apple2.v $apl/rtl/mouse/jt6805/jtframe_6805mcu.v $apl/rtl/mouse/jt6805/jt6805.v $apl/rtl/mouse/jt6805/jt6805_alu.v \
  $apl/rtl/mouse/jt6805/jt6805_ctrl.v $apl/rtl/mouse/jt6805/jt6805_regs.v]
synth_design -top layer2_apple2 -part $part -mode out_of_context -include_dirs [list $apl/rtl/mouse/jt6805]
write_checkpoint -force $l2_dcp
report_utilization -file $bdir/utilization_apple2_synth.rpt
report_utilization -hierarchical -hierarchical_depth 3 -file $bdir/utilization_apple2_hier_synth.rpt
set fh [open $bdir/utilization_apple2_synth.rpt]
foreach ln [split [read $fh] "\n"] {
  # "Slice LUTs*" in an out-of-context report: the asterisk is the note about the LUT budget
  if {[regexp {^\| (Slice LUTs\*?|Slice Registers|Block RAM Tile|RAMB36|RAMB18|DSPs) } $ln]} { puts "=== T89 apple2 synth $ln" }
}
close $fh
close_project
puts "=== T89: Apple //e module synthesized out of context ($l2_dcp)"
