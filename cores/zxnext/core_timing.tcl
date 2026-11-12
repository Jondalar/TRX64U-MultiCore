# T116: timing of the ZX Spectrum Next core. A Tcl script, not an XDC file, and sourced by core_sources.tcl AFTER synth_design, for the
# reasons nes_timing.tcl gives (Vivado's XDC parser drops every control command; the cells exist only once synthesis has elaborated them; the
# constraints come back scoped to the cell when the core stage reads the checkpoint, so every pattern begins with `*`).
#
# The machine (zxn/zxn/*: gen/zxnext.v) was written for five related clocks, the fastest 28 MHz, and runs on the partition's 100 MHz cpu_clk
# with those clocks as enables (zxn_clk.v, tools/zxnext_gen.py): the master cycle's rising edge (r28) comes every 3 or 4 clocks, never closer;
# its falling edge (f28, and every flop of the 28 MHz inverse) one clock behind it. Every flop of the machine therefore changes at an enable
# pulse and not in between. The machine took 35.7 ns for a full cycle and 17.9 ns for a half cycle (the dual-port RAMs' second port, the CPU's
# falling-edge flops, the sprites' 180 degree RAM clock); here the longest gap between two enables of the machine is 3 clocks (a full cycle:
# 30 ns at the shortest), the half cycle is one clock one way and two the other.
#   machine -> machine   2 clocks (20 ns). Not 3: a path from a falling-edge flop to a rising-edge flop has two clocks and from a rising-edge
#                        flop to a falling-edge flop one; the paths of the second kind are the short ones (CPU outputs, RAM addresses). If the
#                        longest rising-edge paths do not close at 20 ns the report names them and this is the number to revisit.
# Not relaxed: everything between the machine and the glue (zxn_mem takes the machine's request signals one clock after the enable pulse and
# they must have settled by then), and everything between two flip-flops of the glue.
set m_cells [get_cells -quiet -hierarchical -filter {NAME =~ *zxn/zxn/*}]
puts "=== T116 zxnext timing cells: machine [llength $m_cells]"
if {[llength $m_cells] == 0} {
  puts "=== T116: WARNING core_timing.tcl matched no machine cell (zxn/zxn/*): nothing is relaxed, the machine's paths are timed at one clock"
} else {
  set_multicycle_path 2 -setup -from $m_cells -to $m_cells
  set_multicycle_path 1 -hold  -from $m_cells -to $m_cells
}
