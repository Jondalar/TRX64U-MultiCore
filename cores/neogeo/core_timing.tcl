# T143: timing of the Neo Geo core. A Tcl script, not an XDC file, and sourced by mono/build_mono.tcl AFTER synth_design, for the reasons cores/zxnext/core_timing.tcl gives
# (Vivado's XDC parser drops every control command; the cells exist only once synthesis has elaborated them).
#
# The machine runs on its own 48 MHz clock (neo_clk48: an MMCM on cpu_clk) with 24 MHz enables, as on the MiSTer; it meets the host's clocks only in synchronisers and in toggles with
# stable data (cpu_clk: the registers; ui_clk: the DDR2 service and the images' second ports), so the clock is a group of its own, asynchronous to all others (the host's
# groups are in host/pins.xdc and name the other clocks).
set neo_clk [get_clocks -quiet -of_objects [get_pins -quiet -hierarchical -filter {NAME =~ *mmcm_neo/CLKOUT0}]]
if {[llength $neo_clk] == 0} {
  puts "=== T143: WARNING core_timing.tcl found no clock on mmcm_neo/CLKOUT0: the machine's clock is not a group of its own"
} else {
  puts "=== T143 timing: the machine's clock $neo_clk is asynchronous to the host's clocks"
  set_clock_groups -asynchronous -group $neo_clk
}
# The upstream's own constraints (rtl/cpu/FX68K/fx68k.sdc and NeoGeo.sdc in NeoGeo_MiSTer), with the cell names of this design: the 68000 core's instruction register to its
# micro / nano address, the nano latch and the ALU operands to the status flags (two clocks), and the Z80 (its enables are twelve clocks apart; the upstream gave it two).
set fx [get_cells -quiet -hierarchical -filter {NAME =~ *M68KCPU/FX68K*}]
if {[llength $fx] > 0} {
  set ir   [get_cells -quiet -hierarchical -filter {NAME =~ *M68KCPU/FX68K/Ir_reg*}]
  set uadr [get_cells -quiet -hierarchical -filter {NAME =~ *M68KCPU/FX68K/microAddr_reg*}]
  set nadr [get_cells -quiet -hierarchical -filter {NAME =~ *M68KCPU/FX68K/nanoAddr_reg*}]
  set nlat [get_cells -quiet -hierarchical -filter {NAME =~ *M68KCPU/FX68K/nanoLatch_reg*}]
  set oper [get_cells -quiet -hierarchical -filter {NAME =~ *M68KCPU/FX68K/excUnit/alu/oper_reg*}]
  set pswc [get_cells -quiet -hierarchical -filter {NAME =~ *M68KCPU/FX68K/excUnit/alu/pswCcr_reg*}]
  puts "=== T143 timing: fx68k cells [llength $fx], Ir [llength $ir], microAddr [llength $uadr], nanoAddr [llength $nadr], nanoLatch [llength $nlat], oper [llength $oper], pswCcr [llength $pswc]"
  if {[llength $ir] && [llength $uadr]} { set_multicycle_path -setup 2 -start -from $ir -to $uadr; set_multicycle_path -hold 1 -start -from $ir -to $uadr }
  if {[llength $ir] && [llength $nadr]} { set_multicycle_path -setup 2 -start -from $ir -to $nadr; set_multicycle_path -hold 1 -start -from $ir -to $nadr }
  if {[llength $nlat] && [llength $pswc]} { set_multicycle_path -setup 2 -start -from $nlat -to $pswc; set_multicycle_path -hold 1 -start -from $nlat -to $pswc }
  if {[llength $oper] && [llength $pswc]} { set_multicycle_path -setup 2 -start -from $oper -to $pswc; set_multicycle_path -hold 1 -start -from $oper -to $pswc }
} else {
  puts "=== T143: WARNING core_timing.tcl matched no fx68k cell"
}
set z80 [get_cells -quiet -hierarchical -filter {NAME =~ *Z80CPU/*}]
if {[llength $z80] > 0} {
  puts "=== T143 timing: Z80 cells [llength $z80]"
  set_multicycle_path -setup 2 -from $z80
  set_multicycle_path -hold 1 -from $z80
}
