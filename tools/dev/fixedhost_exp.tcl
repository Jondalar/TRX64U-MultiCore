# Fixed-host experiment (not CI): the C64 against its own locked static (run 400 repro).
# EXP_MODE=E1: as core.tcl. EXP_MODE=E2: static nets that route through pb_l2 are unrouted before
# lock_design and routed again together with the core.
set part xc7a50tfgg484-1
set mode $::env(EXP_MODE)
set bdir exp_$mode
set host_dcp $::env(TRX_HOST_DCP)
source ../../tools/vivado_lib.tcl
file mkdir $bdir
set ::trx_build $bdir
source core_sources.tcl
set_param general.maxThreads 4

open_checkpoint $host_dcp
set out [open $bdir/analysis.txt w]
puts $out "mode $mode host $host_dcp"
puts $out "reference (C64 co-routed) WNS: [get_property SLACK [get_timing_paths -setup -max_paths 1]]"

# partition pins of l2 as the reference run placed them
set pp [get_pplocs -pins [get_pins l2/*]]
puts $out "pplocs: [llength $pp]"
set ppf [open $bdir/pplocs.txt w]; foreach p $pp { puts $ppf $p }; close $ppf

update_design -cell l2 -black_box

# INT tiles inside pb_l2: grid rectangles of the two site ranges, +-1 column for the INT next to a CLB
proc grid {site} { set t [get_tiles -of_objects [get_sites $site]]; return [list [get_property GRID_POINT_X $t] [get_property GRID_POINT_Y $t]] }
set rects {}
foreach {a b} {SLICE_X0Y0 SLICE_X55Y99 SLICE_X0Y100 SLICE_X9Y149} {
  lassign [grid $a] ax ay; lassign [grid $b] bx by
  lappend rects [list [expr {min($ax,$bx)-1}] [expr {max($ax,$bx)+1}] [expr {min($ay,$by)}] [expr {max($ay,$by)}]]
}
puts $out "pb_l2 grid rects: $rects"
set inl2 [dict create]
foreach t [get_tiles -filter {TYPE =~ INT_*}] {
  set x [get_property GRID_POINT_X $t]; set y [get_property GRID_POINT_Y $t]
  foreach r $rects { lassign $r x0 x1 y0 y1
    if {$x >= $x0 && $x <= $x1 && $y >= $y0 && $y <= $y1} { dict set inl2 $t 1; break } }
}
puts $out "INT tiles in pb_l2: [dict size $inl2]"

set bnets [dict create]
foreach n [get_nets -quiet -of_objects [get_pins -quiet l2/*]] { dict set bnets $n 1 }
puts $out "boundary nets (to/from l2 ports): [dict size $bnets]"
set crossing {}
set bcross 0
set total 0
set nets [get_nets -hier -filter {ROUTE_STATUS == ROUTED && TYPE == SIGNAL}]
foreach n $nets {
  set k 0
  foreach t [get_tiles -quiet -of_objects [get_nodes -quiet -of_objects $n]] { if {[dict exists $inl2 $t]} { incr k } }
  if {$k > 0} {
    if {[dict exists $bnets $n]} { incr bcross; puts $out "BOUND $k $n" } else { lappend crossing $n; incr total $k; puts $out "CROSS $k $n" }
  }
}
puts $out "static signal nets routed: [llength $nets], pure static crossing pb_l2: [llength $crossing] (INT tiles used inside: $total), boundary nets inside pb_l2: $bcross"
flush $out

if {$mode eq "E2"} {
  route_design -unroute -nets [get_nets $crossing]
  puts $out "E2: unrouted [llength $crossing] crossing nets before lock"
}
lock_design -level routing
# slices of pb_l2 the locked static occupies (run 400 full core: 1,014 unavailable)
set l2sites [dict create]
foreach st [get_sites -of_objects [get_pblocks pb_l2] -filter {SITE_TYPE =~ SLICE*}] { dict set l2sites $st 1 }
set occ [dict create]; set occs [dict create]
foreach c [get_cells -hier -filter {IS_PRIMITIVE && LOC =~ SLICE_*}] {
  set st [get_property LOC $c]
  if {[dict exists $l2sites $st]} { dict set occs $st 1; dict incr occ [join [lrange [split $c /] 0 1] /] }
}
puts $out "pb_l2 slices: [dict size $l2sites], occupied by static cells: [dict size $occs]"
foreach {k v} [lsort -stride 2 -index 1 -integer -decreasing $occ] { puts $out "OCC $v $k" }
flush $out
if {$mode eq "A"} { close $out; puts "=== exp A done"; exit }
read_checkpoint -cell l2 $l2_dcp
read_checkpoint -cell l2/c64 $core_dcp
set rdy_nets [get_nets -quiet -hier -filter {NAME =~ l2/c64/c64/cpu_rdy}]
if {[llength $rdy_nets] == 1} {
  set sid2x_c [get_clocks -of_objects [get_pins mmcm_sys/CLKOUT1]]
  set_multicycle_path 2 -setup -end -through $rdy_nets -to $sid2x_c
  set_multicycle_path 1 -hold  -end -through $rdy_nets -to $sid2x_c
}
trx_keep_boundary
opt_design
place_design -directive ExtraTimingOpt
phys_opt_design -directive AggressiveExplore
route_design -directive Explore
phys_opt_design -directive AggressiveExplore
set wns [get_property SLACK [get_timing_paths -setup -max_paths 1]]
puts $out "RESULT $mode WNS $wns"

# worst paths: inside l2 only, or crossing the l2 boundary (a partition pin on the path)
foreach p [get_timing_paths -setup -max_paths 40 -nworst 1 -slack_lesser_than 0.2] {
  set cells [get_cells -quiet -of_objects [get_pins -quiet -of_objects $p]]
  set inl 0; set outl 0
  foreach c $cells { if {[string match l2/* $c]} { incr inl } else { incr outl } }
  set kind [expr {$outl == 0 ? "l2-internal" : ($inl == 0 ? "static" : "boundary")}]
  puts $out "PATH [get_property SLACK $p] $kind [get_property STARTPOINT_PIN $p] -> [get_property ENDPOINT_PIN $p] levels [get_property LOGIC_LEVELS $p]"
}
report_design_analysis -congestion -file $bdir/congestion.rpt
report_route_status -file $bdir/route_status.rpt
report_timing -delay_type max -max_paths 20 -nworst 1 -file $bdir/timing_worst.rpt
close $out
puts "=== exp $mode done WNS $wns"
