# T182 step 1: synthesis-only AREA TRIAL driver. Not the core's build. Run from host/ like cores/c128/mono/build_mono.tcl (Vivado's working directory),
# with the C128 flat build's own environment (TRX_VDC, TRX_Z80, TRX_REU, TRX_DEFS, TRX_STUB, TRX_TOP_OFF, TRX_BUILD, TRX_STOP).
#
# It reads cores/c128/mono/build_mono.tcl as text, patches two places and sources the result:
#   TRX_TRIAL=scpu  : the module scpu_trial_top (cores/c64scpu/trial/scpu_trial.v: 65C816 + CPU-side cache + SCPU registers + a stub of the world
#                     around it) is instantiated inside layer2 (before its first endmodule), its sources are read after layer2's own;
#   TRX_TRIAL=none  : the C128 build unchanged (calibration of the synthesis figures against the routed report of the C128).
# The 65C816 (cores/snes/rtl/65C816, MiSTer, GPL) is read into the library s816: its entity ALU would meet the 6502n's alu in work.
set fh [open ../cores/c128/mono/build_mono.tcl]
set t [read $fh]
close $fh

proc scpu_inject {txt} {
  set k [string first "\nendmodule" $txt]
  if {$k < 0} { error "layer2_c128.v has no endmodule" }
  set ins "\n    scpu_trial_top scpu_t (.clk(sid2x_clk), .rst(sys_rst), .obs());"
  return "[string range $txt 0 [expr {$k - 1}]]$ins[string range $txt $k end]"
}
source ../cores/c64scpu/trial/build_trial_lib.tcl

if {[info exists ::env(TRX_TRIAL)] && $::env(TRX_TRIAL) eq "scpu"} {
  set a {regsub {module layer2_c128 } $txt {module layer2 } txt}
  if {[string first $a $t] < 0} { error "build_mono.tcl: anchor 1 not found" }
  set t [string map [list $a "$a\nset txt \[scpu_inject \$txt\]"] $t]
  set b {read_verilog -sv [lreplace $l2_v 0 0 $bdir/layer2_mono.v]}
  if {[string first $b $t] < 0} { error "build_mono.tcl: anchor 2 not found" }
  set t [string map [list $b "$b\nscpu_read"] $t]
}
file mkdir $::env(TRX_BUILD)
set pf [open $::env(TRX_BUILD)/build_mono_patched.tcl w]
puts $pf $t
close $pf
source $::env(TRX_BUILD)/build_mono_patched.tcl
