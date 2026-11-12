# T95 (docs/specs/T95-nes-core.md 5.5, 5.9, 5.15): timing of the NES core. A Tcl script, not an XDC file, and sourced by
# mono/build_mono.tcl AFTER synth_design: Vivado's XDC parser accepts no `if`, `puts`, `concat` or any other control command
# ("CRITICAL WARNING: [Designutils 20-1307] Command 'if' is not supported in the xdc constraint file"), so as an .xdc every
# line below that decides something was dropped and nothing was relaxed - run 42 routed the machine at one clock and reached
# WNS -8.563 ns after placement (5.15). Here the commands run, and the cells exist because synthesis has elaborated them.
#
# The machine (nes/m: NES_MiSTer's NES, CPU, PPU, APU, cart and mappers) was written for a 21.477 MHz master clock and runs on the
# partition's 100 MHz cpu_clk with a clock enable (nes_core.v): the enable ticks every 4 or 5 clocks (100 / 21.477 = 4.66), never
# closer, and tools/nes_gen.py put it on every clocked process of the machine. Every flip-flop in nes/m therefore changes at the
# edge of a tick and not in between, so a path from one of them to another has at least four clocks, not one.
#   machine -> machine       4 clocks. The machine is the long path of the design (6502 decode, mapper address arithmetic).
#   machine -> the rest      3 clocks. Everything outside that samples the machine's combinational outputs does it three clocks
#                            after the tick it looks at (nes_core.v `cap` and the three-stage dot strobe), and at no other time
#                            where a wrong value would matter.
#   tick-aligned -> machine  4 clocks. What the machine gets from outside changes only on a tick: the pause request, reset and the
#                            mapper flags are registered on the enable (nes_core.v), and nes_mem.v's outputs follow the captured
#                            address one clock after a tick (block RAM outputs).
# The constraints go into the out-of-context checkpoint and come back scoped to the cell when the core stage reads it
# (`read_checkpoint -cell l2`), and Vivado writes them out as the command text with these get_cells expressions inside, so
# they are evaluated a SECOND time in the implementation. There NAME carries the full hierarchical name, which now starts
# with the cell: `l2/nes/m/...`. Every pattern therefore begins with `*` - without it run 43 answered the second evaluation
# with `CRITICAL WARNING: [Vivado 12-4739] set_multicycle_path:No valid object(s) found` six times, nothing was relaxed
# again, and the placement landed at WNS -8.969 ns (docs/specs/T95-nes-core.md 5.16).
# Since T164 WP19 the NES is built flat (mono/build_mono.tcl): this script runs once, after the synthesis of the whole design, and nothing is read back
# into a second stage; the patterns stay the same.
# Not relaxed: everything between two flip-flops outside the machine, and the enable net itself: ce_r to the machine's CE pins has
# one clock (it is high for one clock and the machine takes it at the end of that clock). If the names below stop matching
# (a different flattening), nothing is relaxed and timing.rpt shows the machine's paths failing - which is how it would be noticed.
set m_cells [get_cells -quiet -hierarchical -filter {NAME =~ *nes/m/*}]
set g_cells [get_cells -quiet -hierarchical -filter {NAME !~ *nes/m/* && NAME =~ *nes/*}]
set t_cells [concat [get_cells -quiet -hierarchical -filter {NAME =~ *nes/pausecore_reg* || NAME =~ *nes/rst_m_q_reg* || NAME =~ *nes/sys_q_reg* || NAME =~ *nes/flags_q_reg* || NAME =~ *nes/pmask_q_reg* || NAME =~ *nes/cmask_q_reg*}] \
                  [get_cells -quiet -hierarchical -filter {NAME =~ *nes/mem/*}]]
puts "=== T95 nes timing cells: machine [llength $m_cells], glue [llength $g_cells], tick-aligned [llength $t_cells]"
if {[llength $m_cells] == 0} {
  puts "=== T95: WARNING nes_timing.tcl matched no machine cell (nes/m/*): nothing is relaxed, the machine's paths are timed at one clock"
}
if {[llength $m_cells] > 0} {
  set_multicycle_path 4 -setup -from $m_cells -to $m_cells
  set_multicycle_path 3 -hold  -from $m_cells -to $m_cells
  set_multicycle_path 3 -setup -from $m_cells -to $g_cells
  set_multicycle_path 2 -hold  -from $m_cells -to $g_cells
  if {[llength $t_cells] > 0} {
    set_multicycle_path 4 -setup -from $t_cells -to $m_cells
    set_multicycle_path 3 -hold  -from $t_cells -to $m_cells
  }
}
