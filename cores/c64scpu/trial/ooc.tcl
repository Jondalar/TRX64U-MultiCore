# T182 trial: the CPU block alone (scpu_cpu = 65C816 + cache + SCPU registers + port + mirror + speed control), out of context, to see its own size without the
# host and the C128 base around it. Run from host/ (paths as build_trial.tcl). TRX_MCODE=block selects the block-RAM microcode copy.
set part xc7a50tfgg484-1
set bdir $::env(TRX_BUILD)
file mkdir $bdir
source ../cores/c64scpu/trial/build_trial_lib.tcl
create_project -in_memory -part $part
scpu_read
set top scpu_cpu
if {[info exists ::env(TRX_TOP)]} { set top $::env(TRX_TOP) }   ;# scpu_cpu816 = the 65C816 alone
synth_design -top $top -part $part -mode out_of_context
report_utilization -file $bdir/utilization_ooc.rpt
report_utilization -hierarchical -hierarchical_depth 3 -file $bdir/utilization_hier_ooc.rpt
puts "=== T182 trial: ooc done"
