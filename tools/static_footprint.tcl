# T123 S3: the footprint of the partition pb_l2 in the tile grid, for tools/static_dump.tcl and tools/static_classify.tcl.
# Needs an open design. Defines inside_tile, inside_node (by the tile a node is named after) and inside_node_wires (any wire of it).
#
# The footprint: for every tile ROW the lowest and highest tile COLUMN of the sites of pb_l2, two columns wider on each side (the
# interconnect and RAM interface tiles beside the sites); a row without sites (the clock region breaks) takes the union of its
# neighbours.
set pb [get_pblocks pb_l2]
set seen [dict create]
set lo [dict create]
set hi [dict create]
foreach s [get_sites -of_objects $pb] {
  set t [get_tiles -of_objects $s]
  if {[dict exists $seen $t]} { continue }
  dict set seen $t 1
  set c [get_property COLUMN $t]
  set r [get_property ROW $t]
  if {![dict exists $lo $r] || $c < [dict get $lo $r]} { dict set lo $r $c }
  if {![dict exists $hi $r] || $c > [dict get $hi $r]} { dict set hi $r $c }
}
set span_lo [dict create]
set span_hi [dict create]
foreach r [dict keys $lo] {
  dict set span_lo $r [expr {[dict get $lo $r] - 2}]
  dict set span_hi $r [expr {[dict get $hi $r] + 2}]
}
foreach r [dict keys $lo] {
  foreach g [list [expr {$r - 1}] [expr {$r + 1}]] {
    if {[dict exists $lo $g]} { continue }
    foreach n [list [expr {$g - 1}] [expr {$g + 1}]] {
      if {![dict exists $lo $n]} { continue }
      set a [dict get $span_lo $n]; set b [dict get $span_hi $n]
      if {![dict exists $span_lo $g] || $a < [dict get $span_lo $g]} { dict set span_lo $g $a }
      if {![dict exists $span_hi $g] || $b > [dict get $span_hi $g]} { dict set span_hi $g $b }
    }
  }
}
set tclass [dict create]
proc inside_tile {tile} {
  global tclass span_lo span_hi
  if {[dict exists $tclass $tile]} { return [dict get $tclass $tile] }
  set v 0
  set t [get_tiles -quiet $tile]
  if {$t ne ""} {
    set c [get_property COLUMN $t]
    set r [get_property ROW $t]
    if {[dict exists $span_lo $r] && $c >= [dict get $span_lo $r] && $c <= [dict get $span_hi $r]} { set v 1 }
  }
  dict set tclass $tile $v
  return $v
}
proc inside_node {node} {
  return [inside_tile [lindex [split $node /] 0]]
}
# A long line is named after one tile and reaches 6 or 18 tiles away, so a route of the core may use a node whose name is outside the
# footprint while its wire runs inside it. Slow (a wire list per node): only for the few nodes that differ (tools/static_classify.tcl).
proc inside_node_wires {node} {
  if {[inside_node $node]} { return 1 }
  foreach w [get_wires -quiet -of_objects [get_nodes -quiet $node]] {
    if {[inside_tile [lindex [split $w /] 0]]} { return 1 }
  }
  return 0
}
