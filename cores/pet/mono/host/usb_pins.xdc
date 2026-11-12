# USB PHY (ULPI, bank 34, 1.8 V) and hub (bank 15, 3.3 V): pins, drive and
# timing from Gideon's C64U XDC (host/c64u.xdc lines 7-19).
set_property PACKAGE_PIN V4  [get_ports ULPI_CLOCK]
set_property PACKAGE_PIN U2  [get_ports ULPI_NXT]
set_property PACKAGE_PIN T1  [get_ports ULPI_DIR]
set_property PACKAGE_PIN R2  [get_ports ULPI_STP]
set_property PACKAGE_PIN R3  [get_ports ULPI_RESET]
set_property PACKAGE_PIN R4  [get_ports ULPI_REFCLK]
set_property PACKAGE_PIN U1  [get_ports {ULPI_DATA[0]}]
set_property PACKAGE_PIN V2  [get_ports {ULPI_DATA[1]}]
set_property PACKAGE_PIN V3  [get_ports {ULPI_DATA[2]}]
set_property PACKAGE_PIN W1  [get_ports {ULPI_DATA[3]}]
set_property PACKAGE_PIN Y1  [get_ports {ULPI_DATA[4]}]
set_property PACKAGE_PIN Y2  [get_ports {ULPI_DATA[5]}]
set_property PACKAGE_PIN AA1 [get_ports {ULPI_DATA[6]}]
set_property PACKAGE_PIN AB1 [get_ports {ULPI_DATA[7]}]
set_property IOSTANDARD LVCMOS18 [get_ports {ULPI_CLOCK ULPI_NXT ULPI_DIR ULPI_STP ULPI_RESET ULPI_REFCLK ULPI_DATA[*]}]
set_property DRIVE 4 [get_ports {ULPI_STP ULPI_RESET ULPI_REFCLK ULPI_DATA[*]}]
set_property PACKAGE_PIN J20 [get_ports HUB_RESETn]
set_property PACKAGE_PIN G20 [get_ports HUB_CLOCK]
set_property IOSTANDARD LVCMOS33 [get_ports {HUB_RESETn HUB_CLOCK}]

# 60 MHz from the PHY
create_clock -period 16.500 -name ulpi_clock [get_ports ULPI_CLOCK]
set_clock_latency -source -late 0.500 [get_ports ULPI_CLOCK]
set_input_delay -clock [get_clocks ulpi_clock] -max -add_delay 3.500 [get_ports {ULPI_DATA[*] ULPI_DIR ULPI_NXT}]
set_input_delay -clock [get_clocks ulpi_clock] -min -add_delay 1.500 [get_ports {ULPI_DATA[*] ULPI_DIR ULPI_NXT}]
set_output_delay -clock [get_clocks ulpi_clock] -max -add_delay 3.000 [get_ports {ULPI_DATA[*] ULPI_STP}]
set_output_delay -clock [get_clocks ulpi_clock] -min -add_delay 0.000 [get_ports {ULPI_DATA[*] ULPI_STP}]
# Output flops only: ulpi_bus keeps its input flops out of the IOBs (hold
# against the BUFG'd clock is fixed in the fabric)
set_property IOB TRUE [get_cells -hier -filter {NAME =~ *ulpi_data_out_reg* || NAME =~ *ulpi_stop_reg*}]

# The link meets the CPU clock only in synchronizers, the async FIFO and the
# dual-clock RAMs; the 24 MHz references only drive their ODDRs.
set_clock_groups -asynchronous -group [get_clocks ulpi_clock]
set_clock_groups -asynchronous -group [get_clocks -of_objects [get_pins usb/pll_usb/CLKOUT0]]
# ULPI_RESET and HUB_RESETn are static levels
set_false_path -to [get_ports {ULPI_RESET HUB_RESETn}]
