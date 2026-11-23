# T170: the serial port's SRQ input (the fast serial line) of the C128 large core (read by build_mono.tcl). Ball and standard are the line of host/c64u.xdc
# (Gideon's issue-615 constraints; IOSTANDARD LVCMOS33 for IEC_*); the host's pins.xdc has IEC_SRQ_O only. _I = 1 while the line is low, like the other IEC inputs.
set_property PACKAGE_PIN K14 [get_ports IEC_SRQ_I]
set_property IOSTANDARD LVCMOS33 [get_ports IEC_SRQ_I]
# No drive on the line: released it must read as idle. _I = 1 means the line is low, so the pin idles low: a pulldown (T170 review). A line stuck low would also hold CIA 1 FLAG low (c128_iecfs.v:26) and block the tape input.
set_property PULLTYPE PULLDOWN [get_ports IEC_SRQ_I]
