# T142: timing of the SNES core. A Tcl script, not an XDC file, and sourced by mono/build_mono.tcl AFTER synth_design, for the reasons cores/nes/nes_timing.tcl
# gives (Vivado's XDC parser drops every control command; the cells exist only once synthesis has elaborated them).
#
# The machine (snes/m/*: gen/snes.v, the S-CPU, S-PPU, S-SMP, S-DSP and the cartridge mapper of SNES_MiSTer) was written for a 21.477 MHz master clock and runs on
# the 100 MHz cpu_clk with that clock as an enable (trx/snes_core.v): the enable ticks every 4 or 5 clocks (100 / 21.477 = 4.66), never closer, and
# tools/snes_gen.py put it on every clocked process of the machine. Every flip-flop of the machine therefore changes at the edge of a tick and not in between,
# so a path from one of them to another has at least four clocks, not one. The glue in snes_core.v that is clocked by the same enable (the video and audio RAM,
# the mapper's `fresh` flag, the registers the app writes, taken over at a tick) is part of the machine for this purpose.
#   machine -> machine       4 clocks. The long path of the design (65C816 decode, PPU pixel pipeline, S-DSP multiply chains).
#   machine -> the rest      3 clocks. Everything outside that samples the machine's combinational outputs does it three clocks after the tick it looks at
#                            (snes_core.v `cap`, the clock before the next tick), and at no other time where a wrong value would matter.
#   cache output -> machine  3 clocks. The caches' read data (snes_cch.v dq) is registered two clocks after cap and the machine takes it at a tick, which is at
#                            least two clocks later still.
# Not relaxed: everything between two flip-flops outside the machine, the enable nets themselves (ce_r / cen_r to the machine's CE pins have one clock: the
# enable is high for one clock and the machine takes it at the end of that clock), and the machine's two clock-enable generators (cegen: the S-DSP's and the
# mapper's; they count on the falling-edge enable, two clocks behind the rising one, so a path between them and the rest has two clocks, which the default
# one-clock analysis covers).
# The constraints go into the checkpoint with the cells' names; the patterns begin with `*` because the names carry the whole hierarchy. If the names stop
# matching (a different flattening), nothing is relaxed and timing.rpt shows the machine's paths failing - which is how it would be noticed.
set all_m [get_cells -quiet -hierarchical -filter {NAME =~ *snes/m/*}]
set c_cells [get_cells -quiet -hierarchical -filter {NAME =~ *snes/m/* && NAME =~ *cegen*}]
set m_cells [get_cells -quiet -hierarchical -filter {NAME =~ *snes/m/* && NAME !~ *cegen*}]
set e_cells [get_cells -quiet -hierarchical -filter {NAME =~ *snes/vram1* || NAME =~ *snes/vram2* || NAME =~ *snes/aram* || NAME =~ *snes/vrd_d* || NAME =~ *snes/fresh_reg* || NAME =~ *snes/pal_q_reg* || NAME =~ *snes/blend_q_reg* || NAME =~ *snes/dspf_q_reg* || NAME =~ *snes/type_q_reg* || NAME =~ *snes/rmask_q_reg* || NAME =~ *snes/bmask_q_reg* || NAME =~ *snes/rtc_q_reg* || NAME =~ *snes/tick_cnt_reg*}]
set g_cells [get_cells -quiet -hierarchical -filter {NAME =~ *snes/* && NAME !~ *snes/m/* && NAME !~ *snes/vram1* && NAME !~ *snes/vram2* && NAME !~ *snes/aram* && NAME !~ *snes/vrd_d* && NAME !~ *snes/fresh_reg* && NAME !~ *snes/pal_q_reg* && NAME !~ *snes/blend_q_reg* && NAME !~ *snes/dspf_q_reg* && NAME !~ *snes/type_q_reg* && NAME !~ *snes/rmask_q_reg* && NAME !~ *snes/bmask_q_reg* && NAME !~ *snes/rtc_q_reg* && NAME !~ *snes/tick_cnt_reg*}]
set q_cells [get_cells -quiet -hierarchical -filter {NAME =~ *snes/mem/c_* && (NAME =~ *data_reg* || NAME =~ *dq_reg* || NAME =~ *bo_q_reg*)}]
set mm [concat $m_cells $e_cells]
puts "=== T142 snes timing cells: machine [llength $m_cells], enable-clocked glue [llength $e_cells], cegen [llength $c_cells], glue [llength $g_cells], cache outputs [llength $q_cells]"
if {[llength $m_cells] == 0} {
  puts "=== T142: WARNING core_timing.tcl matched no machine cell (snes/m/*): nothing is relaxed, the machine's paths are timed at one clock"
} else {
  set_multicycle_path 4 -setup -from $mm -to $mm
  set_multicycle_path 3 -hold  -from $mm -to $mm
  set_multicycle_path 3 -setup -from $mm -to $g_cells
  set_multicycle_path 2 -hold  -from $mm -to $g_cells
  if {[llength $q_cells] > 0} {
    set_multicycle_path 3 -setup -from $q_cells -to $mm
    set_multicycle_path 2 -hold  -from $q_cells -to $mm
  }
}
