# Clocks of c64_core for its out-of-context synthesis (see c64_core.v). gclk is
# clk through top.v's BUFGCE: same source, so the two are timed together.
# T40: at the fastest System Mode clock (PAL-60, 32.765 MHz; sid2x twice that).
create_clock -name sys_clk -period 30.520 [get_ports clk]
create_clock -name sys_gclk -period 30.520 [get_ports gclk]
# the SID engine's clock (T25.1): twice clk, edge-aligned (top.v mmcm_sys)
create_clock -name sid2x -period 15.260 [get_ports clk2x]
create_clock -name sid_cv_clk -period 10.000 [get_ports sid_cv_clk]
set_clock_groups -asynchronous -group {sys_clk sys_gclk sid2x} -group sid_cv_clk
create_clock -name snd_clk -period 81.375 [get_ports snd_clk]
set_clock_groups -asynchronous -group {sys_clk sys_gclk sid2x} -group snd_clk
create_clock -name cart_wclk -period 13.333 [get_ports cart_wclk]
set_clock_groups -asynchronous -group {sys_clk sys_gclk sid2x} -group cart_wclk
create_clock -name cart_ee_clk -period 10.000 [get_ports cart_ee_clk]
set_clock_groups -asynchronous -group {sys_clk sys_gclk sid2x} -group cart_ee_clk
