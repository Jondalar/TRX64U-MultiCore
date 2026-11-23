# T170: the serial port's SRQ input (the fast serial line) of the C128 large core (read by build_mono.tcl). Ball and standard are the line of host/c64u.xdc
# (Gideon's issue-615 constraints; IOSTANDARD LVCMOS33 for IEC_*); the host's pins.xdc has IEC_SRQ_O only. _I = 1 while the line is low, like the other IEC inputs.
set_property PACKAGE_PIN K14 [get_ports IEC_SRQ_I]
set_property IOSTANDARD LVCMOS33 [get_ports IEC_SRQ_I]
