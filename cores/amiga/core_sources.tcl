# T85: layer-2 synthesis of the Amiga core, out of context. Since T164 WP19 the Amiga 500 is a large core (cores/amiga/mono/build_mono.tcl, its own file list): this
# recipe is sourced only by host/build.tcl for a dev monolith with TRX_CORE=amiga (host/ is pinned, the hook stays), and no pipeline lane runs it.
# Needs $part and $bdir; sets l2_dcp.
if {![info exists common]} { set common ../cores/common }
if {![info exists gvhdl]}  { source gideon_vhdl.tcl }

set mm ../cores/amiga/rtl
set l2_dcp [file join $bdir layer2_amiga_ooc.dcp]
# fx68k reads its microcode with $readmemb("microrom.mem"): the files go where the synthesis runs
file copy -force $mm/src/fx68k/microrom.mem [pwd]
file copy -force $mm/src/fx68k/nanorom.mem [pwd]
create_project -in_memory -part $part
set_property target_language Verilog [current_project]
set mm_v [concat [lsearch -all -inline -not -regexp [glob $mm/src/minimig-aga/*.v] {/paula_floppy(_fifo)?\.v$}] [glob $mm/a500/*.v] $mm/src/nanomig.v [glob $mm/trx/*.v]]   ;# A500: own copies of the floppy buffer/fifo and the sd fifo (rtl/a500)
read_verilog -sv $mm_v
read_verilog -sv [list $mm/src/fx68k/fx68k.sv $mm/src/fx68k/fx68kAlu.sv $mm/src/fx68k/uaddrPla.sv]
read_verilog -sv ../cores/amiga/layer2_amiga.v
synth_design -top layer2_amiga -part $part -mode out_of_context -verilog_define INFER_DPRAM
write_checkpoint -force $l2_dcp
report_utilization -file $bdir/utilization_amiga_synth.rpt
report_utilization -hierarchical -hierarchical_depth 4 -file $bdir/utilization_amiga_hier_synth.rpt
set fh [open $bdir/utilization_amiga_synth.rpt]
foreach ln [split [read $fh] "\n"] {
  if {[regexp {^\| (Slice LUTs|LUT as Logic|LUT as Memory|Slice Registers|Block RAM Tile|RAMB36|RAMB18|DSPs)} $ln]} { puts "=== T85 amiga synth $ln" }
}
close $fh
close_project
puts "=== T85: Amiga module synthesized out of context ($l2_dcp)"
