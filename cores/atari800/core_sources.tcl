# T88: layer-2 synthesis of the Atari 800 XL core, out of context (T94 D1: the core owns its recipe).
# Sourced from host/ (Vivado's working directory) by host/core.tcl (core stage) and, for the monolith, by whoever builds it. Needs $part
# and $bdir; sets l2_dcp. This file belongs to the core: section of host/layers.txt.
# The core has no 1541 and no IEC bus, so neither Gideon's VHDL nor cores/common is read. The VHDL of the machine is VHDL-2008
# (the patches of tools/patches/atari800 made Watson's files strict-2008 clean, GHDL --std=08 is the reference): packages first, the
# rest is resolved by name at elaboration.
set a8   ../cores/atari800
set n6   ../vendor/icepi/6502n
set l2_dcp [file join $bdir layer2_atari800_ooc.dcp]
create_project -in_memory -part $part
set_property target_language Verilog [current_project]
set a8_vhdl [list \
  $n6/pkg_6502_defs.vhd $n6/pkg_6502_opcodes.vhd $n6/pkg_6502_decode.vhd $n6/alu.vhd $n6/bit_cpx_cpy.vhd $n6/shifter.vhd \
  $n6/implied.vhd $n6/data_oper.vhd $n6/proc_control.vhd $n6/proc_registers.vhd \
  $a8/proc_interrupt_a8.vhd $a8/proc_core_a8.vhd $a8/cpu.vhd]
# Watson's files as the core builds them (tools/atari800_gen.sh), and ours; never the pristine rtl/ copy
foreach d {a8core components sioemu} { lappend a8_vhdl {*}[lsort [glob $a8/gen/$d/*.vhd*]] }
lappend a8_vhdl {*}[lsort [glob $a8/trx/*.vhd]]
read_vhdl -vhdl2008 -library work $a8_vhdl
read_verilog $a8/layer2_atari800.v
synth_design -top layer2_atari800 -part $part -mode out_of_context
write_checkpoint -force $l2_dcp
report_utilization -file $bdir/utilization_atari800_synth.rpt
report_utilization -hierarchical -hierarchical_depth 3 -file $bdir/utilization_atari800_hier_synth.rpt
set fh [open $bdir/utilization_atari800_synth.rpt]
foreach ln [split [read $fh] "\n"] {
  if {[regexp {^\| (Slice LUTs|Slice Registers|Block RAM Tile|RAMB36|RAMB18|DSPs) } $ln]} { puts "=== T88 atari800 synth $ln" }
}
close $fh
close_project
puts "=== T88: Atari 800 XL module synthesized out of context ($l2_dcp)"
