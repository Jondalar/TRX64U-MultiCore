# T143: a quick measurement of the Neo Geo machine alone: synthesis out of context (no host, no pins), then the utilisation by module. Not a build step of the
# pipeline; the numbers it prints are the machine's cost before the host is added (cores/neogeo/mono/build_mono.tcl is the real build).
#   cd host && vivado -mode batch -source ../cores/neogeo/mono/synth_ooc.tcl     (env TRX_TOP: module to synthesise, default neo_core; TRX_BUILD: output directory;
#   TRX_RTL=1: elaborate only; TRX_CLKSTUB=1: neo_clk48 is a wire (no MMCM) )
set part xc7a50tfgg484-1
set bdir synth_ooc_neo
if {[info exists ::env(TRX_BUILD)] && $::env(TRX_BUILD) ne ""} { set bdir $::env(TRX_BUILD) }
set top neo_core
if {[info exists ::env(TRX_TOP)] && $::env(TRX_TOP) ne ""} { set top $::env(TRX_TOP) }
file mkdir $bdir
source ../cores/neogeo/sources.tcl
foreach f $neo_mem { file copy -force $f [pwd] }
create_project -in_memory -part $part
set_property target_language Verilog [current_project]
set trx $neo_trx
if {[info exists ::env(TRX_CLKSTUB)] && $::env(TRX_CLKSTUB) eq "1"} {
  set trx [lsearch -all -inline -not -exact $trx ../cores/neogeo/trx/neo_clk48.v]
  set fh [open $bdir/neo_clk48_stub.v w]; puts $fh "module neo_clk48(input cpu_clk, output clk48, output locked); assign clk48 = cpu_clk; assign locked = 1'b1; endmodule"; close $fh
  lappend trx $bdir/neo_clk48_stub.v
}
read_verilog -sv $neo_v
read_verilog -sv $neo_sv
read_vhdl -library work $neo_vhdl
read_verilog -sv $trx
if {[info exists ::env(TRX_RTL)] && $::env(TRX_RTL) eq "1"} {
  synth_design -top $top -part $part -mode out_of_context -rtl
  puts "=== T143 ooc: $top elaborated"
  return
}
synth_design -top $top -part $part -mode out_of_context
puts "=== T143 ooc: $top synthesised"
report_utilization -file $bdir/utilization_synth.rpt
report_utilization -hierarchical -hierarchical_depth 4 -file $bdir/utilization_hier_synth.rpt
puts "=== T143 ooc: reports written"
