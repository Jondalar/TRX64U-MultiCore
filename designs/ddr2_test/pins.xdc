# ddr2_test pins: the DDR2 balls, standards and pad false paths are host/ddr2_pins.xdc (shared with c64_basic).

# the rest
set_property PACKAGE_PIN B17 [get_ports RMII_REFCLK]
set_property IOSTANDARD LVCMOS18 [get_ports RMII_REFCLK]
create_clock -name rmii_clock -period 20 [get_ports RMII_REFCLK]

set_property PACKAGE_PIN P16 [get_ports LED_BOARDn]
set_property PACKAGE_PIN H19 [get_ports LED_CASE1n]
set_property PACKAGE_PIN H17 [get_ports LED_CASE2n]
set_property IOSTANDARD LVCMOS33 [get_ports {LED_BOARDn LED_CASE1n LED_CASE2n}]

set_property PACKAGE_PIN D16 [get_ports SLOT_ADDROEn]
set_property PACKAGE_PIN T6  [get_ports SLOT_DATAOEn]
set_property PACKAGE_PIN W9  [get_ports SLOT_ROMLn]
set_property PACKAGE_PIN Y8  [get_ports SLOT_ROMHn]
set_property IOSTANDARD LVCMOS18 [get_ports {SLOT_ADDROEn SLOT_DATAOEn SLOT_ROMLn SLOT_ROMHn}]

set_property CFGBVS VCCO [current_design]
set_property CONFIG_VOLTAGE 3.3 [current_design]
set_property BITSTREAM.GENERAL.COMPRESS TRUE [current_design]

# Clocks. rmii_clock (50 MHz) -> MMCM -> clk200 -> ddr2_lean's PLL: clk300 (the SERDES CLK) and ui_clk
# (CLKDIV, all the fabric) come out of that one PLL, 300 MHz = 4 x 75 MHz from one VCO, and are timed
# against each other (CLOCK_DELAY_GROUP in ddr2_lean.v keeps their buffer delays equal). The JTAG TCK
# is a clock of its own: jtag_dbg's toggle handshake is its only crossing.
create_clock -name jtag_tck -period 50 [get_pins -hier -filter {NAME =~ dbg/bscan/TCK}]
set_clock_groups -asynchronous \
  -group [get_clocks jtag_tck] \
  -group [get_clocks -include_generated_clocks rmii_clock]

# The tester's status crosses from ui_clk to 50 MHz through synchronizers (ddr2_lean's lk_s is in
# ddr2_pins.xdc).
set_false_path -to [get_cells -hier -filter {NAME =~ *_s_reg[0]}]
