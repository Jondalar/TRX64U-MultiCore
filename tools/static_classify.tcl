# T123 S3: of the boundary-net nodes that differ between two dumps, keep those that are really outside the partition.
#   TRX_DCP  a checkpoint of the host (any: the device and the pblock are the same in both)
#   TRX_IN   lines "<net> <node>"
#   TRX_OUT  the lines of TRX_IN whose node has no wire in the partition's footprint (a node without a tile, as a changed control line, stays)
# Run by tools/static_diff.sh after the dumps have been compared.
set dcp $::env(TRX_DCP)
open_checkpoint $dcp
source [file join [file dirname [info script]] static_footprint.tcl]
set fi [open $::env(TRX_IN) r]
set fo [open $::env(TRX_OUT) w]
set kept 0; set dropped 0
while {[gets $fi line] >= 0} {
  set node [lindex $line 1]
  if {$node ne "" && [inside_node_wires $node]} { incr dropped } else { puts $fo $line; incr kept }
}
close $fi
close $fo
puts "=== T123 S3 classify: $kept outside the partition, $dropped with a wire inside it"
