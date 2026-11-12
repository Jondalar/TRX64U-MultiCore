# T123 S3: dump what the static part of a routed design is - placement of every primitive cell outside the partition, the route of every
# net that is not (even partly) inside it - so two checkpoints of the same host with different cores in layer 2 can be compared.
#   TRX_DCP     the checkpoint (the host's static_routed.dcp with the reference core, or a core stage's core_routed.dcp)
#   TRX_DUMP    the file to write
# Run by tools/static_diff.sh:  vivado -mode batch -source tools/static_dump.tcl
# Lines (sorted by the caller):
#   C <cell> <LOC> <BEL>      a primitive cell whose name is not under l2/ (an I/O buffer: LOC only, its BEL is reported differently
#                             by the two checkpoints without the pad having moved)
#   N <net> <ROUTE>          a net that has no pin on the l2 cell, is no constant and is not under l2/: its whole route
#   B <net> <node>           a net that has a pin on the l2 cell, or a constant net (VCC/GND reach the core's cells too): its routing nodes
#                             OUTSIDE the partition's footprint (tools/static_footprint.tcl). What is inside is the core's own routing and
#                             may differ. The clock delivery to the loads (HCLK_LEAF_CLK nodes) follows the loads of the core and is left
#                             out as well. A node named outside whose long wire runs inside is sorted out by tools/static_classify.tcl,
#                             for the few that differ.
#   S <what> <count>          counts, so a dump of an empty design cannot pass as equal to another one
#

set dcp $::env(TRX_DCP)
set out $::env(TRX_DUMP)
open_checkpoint $dcp
set fh [open $out w]

source [file join [file dirname [info script]] static_footprint.tcl]

# cells outside l2
set ncell 0
foreach c [get_cells -hier -filter {IS_PRIMITIVE && NAME !~ l2/*}] {
  set bel [get_property BEL $c]
  if {[string match IO.* [get_property PRIMITIVE_TYPE $c]]} { set bel "" }
  puts $fh "C $c [get_property LOC $c] $bel"
  incr ncell
}
puts $fh "S cells $ncell"

# nets
set bnets [dict create]
foreach n [get_nets -quiet -of_objects [get_pins -of_objects [get_cells l2]]] { dict set bnets $n 1 }
set nn 0; set nb 0; set nbn 0; set ni 0
foreach n [get_nets -hier -filter {NAME !~ l2/*}] {
  incr ni
  if {$ni % 1000 == 0} { puts "=== T123 S3 dump: $ni nets done"; flush $fh }
  set tn [clock milliseconds]
  if {[dict exists $bnets $n] || [get_property TYPE $n] in {GROUND POWER}} {
    incr nb
    foreach nd [get_nodes -quiet -of_objects $n] {
      if {[string match *HCLK_LEAF_CLK_* $nd]} { continue }
      if {![inside_node $nd]} { puts $fh "B $n $nd"; incr nbn }
    }
  } else {
    puts $fh "N $n [get_property ROUTE $n]"
    incr nn
  }
  if {[clock milliseconds] - $tn > 3000} { puts "=== T123 S3 dump: slow net $n: [expr {[clock milliseconds] - $tn}] ms"; flush stdout }
}
puts $fh "S nets $nn"
puts $fh "S boundary_and_constant_nets $nb"

puts "=== T123 S3 dump: $ncell cells, $nn static nets, $nb boundary and constant nets ($nbn static nodes)"
close $fh
puts "=== T123 S3 dump written: $out"
