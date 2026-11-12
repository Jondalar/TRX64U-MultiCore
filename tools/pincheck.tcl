# Compare two parts' package pins for every ball the design constrains (T53).
#
#   PART_A=xc7a50tfgg484-1 PART_B=xc7a100tfgg484-1 vivado -mode batch -source tools/pincheck.tcl
#
# Run from the repository root. Reads the PACKAGE_PIN of every .xdc under host/
# (Gideon's c64u.xdc among them) and the PADName of host/mig_ddr2.prj; prints, per
# ball, nothing when bank and pin function agree and a line when they do not.
# Exit status 1 when a constrained ball differs.
proc part_pins {part} {
  link_design -part $part -name pc_$part
  set d [dict create]
  foreach p [get_package_pins] {
    dict set d [get_property NAME $p] [list [get_property BANK $p] [get_property PIN_FUNC $p]]
  }
  close_design
  return $d
}
set a [expr {[info exists ::env(PART_A)] ? $::env(PART_A) : "xc7a50tfgg484-1"}]
set b [expr {[info exists ::env(PART_B)] ? $::env(PART_B) : "xc7a100tfgg484-1"}]
set pa [part_pins $a]
set pb [part_pins $b]
set used {}
foreach f [glob host/*.xdc] {
  set fh [open $f]; set txt [read $fh]; close $fh
  foreach {m pin} [regexp -all -inline {PACKAGE_PIN\s+(\w+)} $txt] { lappend used $pin }
}
set fh [open host/mig_ddr2.prj]; set txt [read $fh]; close $fh
foreach {m pin} [regexp -all -inline {PADName="(\w+)"} $txt] { lappend used $pin }
set used [lsort -unique $used]
set bad 0
foreach p $used {
  if {![dict exists $pa $p] || ![dict exists $pb $p] || [dict get $pa $p] ne [dict get $pb $p]} {
    puts "DIFFERS $p: $a [expr {[dict exists $pa $p] ? [dict get $pa $p] : {-}}]  $b [expr {[dict exists $pb $p] ? [dict get $pb $p] : {-}}]"
    incr bad
  }
}
set differ 0
dict for {p v} $pa { if {![dict exists $pb $p] || [dict get $pb $p] ne $v} { incr differ } }
puts "$a and $b: [dict size $pa] and [dict size $pb] balls, $differ differ; [llength $used] constrained balls checked, $bad differ"
exit [expr {$bad ? 1 : 0}]
