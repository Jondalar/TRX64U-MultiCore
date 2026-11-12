# Pins used by hdmi_test, taken from host/c64u.xdc (Gideon, issue #615).
set_property PACKAGE_PIN B17 [get_ports RMII_REFCLK]
set_property IOSTANDARD LVCMOS18 [get_ports RMII_REFCLK]
create_clock -name rmii_clock -period 20 [get_ports RMII_REFCLK]

set_property PACKAGE_PIN F6 [get_ports HDMI_REFCLK_P]
create_clock -name hdmi_refclk -period 6.734 [get_ports HDMI_REFCLK_P]

set_property PACKAGE_PIN D17 [get_ports HDMI_CLK_SYNTH]
set_property IOSTANDARD LVCMOS18 [get_ports HDMI_CLK_SYNTH]
set_property DRIVE 4 [get_ports HDMI_CLK_SYNTH]
set_property PACKAGE_PIN AB5 [get_ports CLOCK_C64_SYNTH]
set_property IOSTANDARD LVCMOS18 [get_ports CLOCK_C64_SYNTH]
set_property DRIVE 4 [get_ports CLOCK_C64_SYNTH]

set_property PACKAGE_PIN U7 [get_ports I2C18_SCL]
set_property PACKAGE_PIN W6 [get_ports I2C18_SDA]
set_property IOSTANDARD LVCMOS18 [get_ports {I2C18_SCL I2C18_SDA}]
set_property PACKAGE_PIN L20 [get_ports HDMI_SCL]
set_property PACKAGE_PIN K18 [get_ports HDMI_SDA]
set_property IOSTANDARD LVCMOS33 [get_ports {HDMI_SCL HDMI_SDA}]
set_property PULLUP true [get_ports {I2C18_SCL I2C18_SDA HDMI_SCL HDMI_SDA}]

set_property PACKAGE_PIN N19 [get_ports HDMI_DDC_EN]
set_property IOSTANDARD LVCMOS33 [get_ports HDMI_DDC_EN]
set_property PACKAGE_PIN H18 [get_ports HDMI_HPD]
set_property IOSTANDARD LVCMOS33 [get_ports HDMI_HPD]

set_property PACKAGE_PIN P16 [get_ports LED_BOARDn]
set_property PACKAGE_PIN H19 [get_ports LED_CASE1n]
set_property PACKAGE_PIN H17 [get_ports LED_CASE2n]
set_property IOSTANDARD LVCMOS33 [get_ports {LED_BOARDn LED_CASE1n LED_CASE2n}]

# Keep the slot transceivers off and the slot strobes inactive, so nothing
# is driven onto the expansion port or the SID sockets.
set_property PACKAGE_PIN D16 [get_ports SLOT_ADDROEn]
set_property PACKAGE_PIN T6  [get_ports SLOT_DATAOEn]
set_property PACKAGE_PIN W9  [get_ports SLOT_ROMLn]
set_property PACKAGE_PIN Y8  [get_ports SLOT_ROMHn]
set_property IOSTANDARD LVCMOS18 [get_ports {SLOT_ADDROEn SLOT_DATAOEn SLOT_ROMLn SLOT_ROMHn}]

set_property CFGBVS VCCO [current_design]
set_property CONFIG_VOLTAGE 3.3 [current_design]
set_property BITSTREAM.GENERAL.COMPRESS TRUE [current_design]

# ready crosses from the 50 MHz domain into the transmitter clock through
# the vrst_sync synchronizer.
set_false_path -to [get_cells {vrst_sync_reg[0]}]
