# Core stage (T87 D9): implement ONE core (the content of layer 2) against the cached host (the host
# entry's static_routed.dcp = the routed full design with the reference core), without synthesizing
# or routing the host: layer 2 is carved out and locked first, as the t83-vc20 second configuration.
# Run by ci/bitstream.sh core <name> through vivado -source core.tcl in host.
#   TRX_HOST_DCP  static_routed.dcp of the host entry (the routed design with the reference core)
#   TRX_CORE      the core name (the core: section of layers.txt): picks cores/<name>/core_sources.tcl
#   TRX_L2_KEY    ooc key of the core (ci/lib.sh ooc_key): the cached layer 2 synthesis
#   TRX_PART / TRX_BUILD as for build.tcl
# Output in $bdir: <core>_partial.bit, <core>_full.bit, timing.rpt, utilization*.rpt, core_routed.dcp.
# This file is the host's recipe and belongs to the host: section of layers.txt; what a core
# synthesizes is its own cores/<name>/core_sources.tcl (T94 D1), which must set l2_dcp.
set part xc7a50tfgg484-1
if {[info exists ::env(TRX_PART)] && $::env(TRX_PART) ne ""} { set part $::env(TRX_PART) }
set bdir build
if {[info exists ::env(TRX_BUILD)] && $::env(TRX_BUILD) ne ""} { set bdir $::env(TRX_BUILD) }
foreach v {TRX_HOST_DCP TRX_CORE} {
  if {![info exists ::env($v)] || $::env($v) eq ""} { error "core.tcl: $v is not set" }
}
set host_dcp $::env(TRX_HOST_DCP)
set corename $::env(TRX_CORE)
if {![file exists $host_dcp]} { error "core.tcl: no host checkpoint $host_dcp" }
source ../tools/vivado_lib.tcl
source check/trx_constraints.tcl
file mkdir $bdir
set ::trx_build $bdir

# the core's own synthesis: the core's recipe sets l2_dcp (and core_dcp where the core has an
# inner cell of its own, as the reference core's c64 has). Checkpoints come from the cache
# when their keys match.
source ../cores/$corename/core_sources.tcl

# the routed static, layer 2 carved out above: read_checkpoint -cell fills it (UG909 flow). l2/c64 is a black
# box inside l2's checkpoint and is filled in the same way as in the monolithic build.
open_checkpoint $host_dcp
puts "=== core $corename: host checkpoint $host_dcp"
update_design -cell l2 -black_box
lock_design -level routing
read_checkpoint -cell l2 $l2_dcp
if {[info exists core_dcp]} { read_checkpoint -cell l2/c64 $core_dcp }

# the T29 multicycle path of build.tcl: the nets it names are inside layer 2, which the static
# checkpoint does not have, so it is applied here
set rdy_nets [get_nets -quiet -hier -filter {NAME =~ l2/c64/c64/cpu_rdy}]
if {[llength $rdy_nets] == 1} {
  set sid2x_c [get_clocks -of_objects [get_pins mmcm_sys/CLKOUT1]]
  set_multicycle_path 2 -setup -end -through $rdy_nets -to $sid2x_c
  set_multicycle_path 1 -hold  -end -through $rdy_nets -to $sid2x_c
  puts "=== T29: cpu_rdy paths to clk2x timed with two clk2x periods"
} else {
  puts "=== T29: WARNING cpu_rdy net not found ([llength $rdy_nets]), no multicycle path"
}
report_utilization -pblocks [get_pblocks pb_l2] -file $bdir/utilization_pb_l2_opt.rpt

trx_keep_boundary
trx_check_boundary "core, before opt_design"
trx_check_constraints "host core, before opt_design"
opt_design
trx_check_boundary "core, after opt_design"
set pdir ExtraTimingOpt
if {[info exists ::env(T79_PLACE)] && $::env(T79_PLACE) ne ""} { set pdir $::env(T79_PLACE) }
set rdir Explore
if {[info exists ::env(T79_ROUTE)] && $::env(T79_ROUTE) ne ""} { set rdir $::env(T79_ROUTE) }
place_design -directive $pdir
phys_opt_design -directive AggressiveExplore
route_design -directive $rdir
phys_opt_design -directive AggressiveExplore
set p [get_timing_paths -setup -max_paths 1]
set wns [expr {[llength $p] == 0 ? 1000.0 : [get_property SLACK $p]}]
puts "=== core $corename: WNS $wns ns"

report_utilization -file $bdir/utilization.rpt
report_utilization -pblocks [get_pblocks pb_l2] -file $bdir/utilization_pb_l2.rpt
report_utilization -hierarchical -hierarchical_depth 8 -file $bdir/utilization_hier.rpt
report_timing_summary -max_paths 5 -file $bdir/timing.rpt
report_route_status -file $bdir/route_status.rpt
report_timing -delay_type max -max_paths 60 -nworst 1 -slack_lesser_than 0 -sort_by slack -file $bdir/timing_neg.rpt
write_checkpoint -force $bdir/core_routed.dcp

# the partial must fit the static it was routed against: a failure here fails the stage
pr_verify -initial $host_dcp -additional $bdir/core_routed.dcp -file $bdir/pr_verify.rpt
puts "=== core $corename: pr_verify passed"

if {$wns < 0} {
  puts "=== core $corename: timing failed, no bitstream"
} else {
  write_bitstream -force -cell l2 $bdir/${corename}_partial.bit
  trx_axss
  write_bitstream -force $bdir/${corename}_full.bit
  puts "=== core $corename: [file size $bdir/${corename}_partial.bit] bytes partial, [file size $bdir/${corename}_full.bit] bytes full"
}
puts "=== done"
