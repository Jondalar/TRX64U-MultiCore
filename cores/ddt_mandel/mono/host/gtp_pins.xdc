# GTP transmitter and reference clock pads (quad 216), from host/c64u.xdc.
# Constraining the pads also fixes which GTPE2_CHANNEL each lane uses:
# lane 0 = TMDS 2 (red), 1 = TMDS 1 (green), 2 = TMDS 0 (blue), 3 = TMDS clock.
set_property PACKAGE_PIN B4 [get_ports {HDMI_DATA_P[2]}]
set_property PACKAGE_PIN A4 [get_ports {HDMI_DATA_N[2]}]
set_property PACKAGE_PIN D5 [get_ports {HDMI_DATA_P[1]}]
set_property PACKAGE_PIN C5 [get_ports {HDMI_DATA_N[1]}]
set_property PACKAGE_PIN B6 [get_ports {HDMI_DATA_P[0]}]
set_property PACKAGE_PIN A6 [get_ports {HDMI_DATA_N[0]}]
set_property PACKAGE_PIN D7 [get_ports HDMI_CLK_P]
set_property PACKAGE_PIN C7 [get_ports HDMI_CLK_N]
set_property PACKAGE_PIN E6 [get_ports HDMI_REFCLK_N]
