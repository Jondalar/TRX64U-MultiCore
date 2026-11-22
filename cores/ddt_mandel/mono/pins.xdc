# T255: the pins of the Mandelbrot large core. Balls, standards and clocks are the lines of the host's pins.xdc / gtp_pins.xdc (taken from cores/diag/mono/host, the host copy of T164); only the ports of
# mono/top.v are here. A ball that is no port of this design is left floating (UNUSEDPIN PULLNONE below): the host's top has every ball as a port, so it never sits at Vivado's default pull-down.
set_property PACKAGE_PIN B17 [get_ports RMII_REFCLK]
set_property IOSTANDARD LVCMOS18 [get_ports RMII_REFCLK]
create_clock -name rmii_clock -period 20 [get_ports RMII_REFCLK]

set_property PACKAGE_PIN F6 [get_ports HDMI_REFCLK_P]
# The modes run 148.06-149.14 MHz; the 6.585 ns period (151.85 MHz) is tighter than any of them and stays (as the host); this core runs 148.87 MHz only
create_clock -name hdmi_refclk -period 6.585 [get_ports HDMI_REFCLK_P]

set_property PACKAGE_PIN D17 [get_ports HDMI_CLK_SYNTH]
set_property IOSTANDARD LVCMOS18 [get_ports HDMI_CLK_SYNTH]
set_property DRIVE 4 [get_ports HDMI_CLK_SYNTH]

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

# port 2 (the joystick the demo is steered with) and RESTORE; low = pressed, released = the pull-up
set_property PACKAGE_PIN W21  [get_ports {KBJOY_2_JOY[0]}]
set_property PACKAGE_PIN U21  [get_ports {KBJOY_2_JOY[1]}]
set_property PACKAGE_PIN U22  [get_ports {KBJOY_2_JOY[2]}]
set_property PACKAGE_PIN T21  [get_ports {KBJOY_2_JOY[3]}]
set_property PACKAGE_PIN V22  [get_ports {KBJOY_2_JOY[4]}]
set_property PACKAGE_PIN M16  [get_ports KBJOY_RESTORE]
set_property IOSTANDARD LVCMOS33 [get_ports {KBJOY_2_JOY[*] KBJOY_RESTORE}]
set_property PULLTYPE PULLUP [get_ports {KBJOY_2_JOY[*] KBJOY_RESTORE}]

# the enables of the C64-side port buffers and their supply: without them the contacts never reach the FPGA
set_property PACKAGE_PIN H13 [get_ports CIA_BUFFER_EN]
set_property PACKAGE_PIN G18 [get_ports CIA_PWR_EN]
set_property IOSTANDARD LVCMOS33 [get_ports {CIA_BUFFER_EN CIA_PWR_EN}]

set_property CFGBVS VCCO [current_design]
set_property CONFIG_VOLTAGE 3.3 [current_design]
set_property BITSTREAM.GENERAL.COMPRESS TRUE [current_design]
# Configuration from the SPI flash as the host (x1, CCLK 16): the IPROG that starts this core reads the bitstream from the flash drive's scratch file with the same settings
set_property CONFIG_MODE SPIx1 [current_design]
set_property BITSTREAM.CONFIG.CONFIGRATE 16 [current_design]
set_property BITSTREAM.CONFIG.USR_ACCESS TIMESTAMP [current_design]
set_property BITSTREAM.CONFIG.UNUSEDPIN PULLNONE [current_design]

# The clock domains meet only in synchronisers, the frame buffer's two ports and the sticky reset flops: time none of the paths between them.
set_clock_groups -asynchronous \
  -group [get_clocks rmii_clock] \
  -group [get_clocks -of_objects [get_pins mmcm_sys/CLKOUT0]] \
  -group [get_clocks -of_objects [get_pins mmcm_ref/CLKOUT0]] \
  -group [get_clocks -of_objects [get_pins mmcm_cpu/CLKOUT0]] \
  -group [get_clocks -include_generated_clocks hdmi_refclk] \
  -group [get_clocks -include_generated_clocks -of_objects [get_pins -hier -filter {NAME =~ *gtp/*gtpe2_i/TXOUTCLK}]]
