# The DDR2 pins of c64_basic and ddr2_test (read by both builds). The balls and IO standards are Gideon's (host/c64u.xdc: HSTL, bank 35, internal
# Vref 0.9 V; the pins are those of host/mig_ddr2.prj), driven by ddr2_lean (no MIG).
#   data DQ, DM: HSTL_II_18, strobes DQS: DIFF_HSTL_II_18, CK, CK#, address, control: HSTL_I_18.
# Fast slew everywhere on the DDR2 pins (the SERDES slots are 1.67 ns). No input termination in the FPGA
# (the HR bank has no DCI; Gideon's XDC has none either): the DRAM's ODT is off as in his calibrator.
set_property INTERNAL_VREF 0.9 [get_iobanks 35]

set_property PACKAGE_PIN H2 [get_ports {ddr2_addr[0]}]
set_property PACKAGE_PIN J1 [get_ports {ddr2_addr[1]}]
set_property PACKAGE_PIN K1 [get_ports {ddr2_addr[2]}]
set_property PACKAGE_PIN E3 [get_ports {ddr2_addr[3]}]
set_property PACKAGE_PIN F3 [get_ports {ddr2_addr[4]}]
set_property PACKAGE_PIN F1 [get_ports {ddr2_addr[5]}]
set_property PACKAGE_PIN G1 [get_ports {ddr2_addr[6]}]
set_property PACKAGE_PIN D2 [get_ports {ddr2_addr[7]}]
set_property PACKAGE_PIN E2 [get_ports {ddr2_addr[8]}]
set_property PACKAGE_PIN B2 [get_ports {ddr2_addr[9]}]
set_property PACKAGE_PIN C2 [get_ports {ddr2_addr[10]}]
set_property PACKAGE_PIN A1 [get_ports {ddr2_addr[11]}]
set_property PACKAGE_PIN B1 [get_ports {ddr2_addr[12]}]
set_property PACKAGE_PIN J2 [get_ports {ddr2_ba[0]}]
set_property PACKAGE_PIN K2 [get_ports {ddr2_ba[1]}]
set_property PACKAGE_PIN G2 [get_ports {ddr2_ba[2]}]
set_property PACKAGE_PIN J5 [get_ports ddr2_ras_n]
set_property PACKAGE_PIN H5 [get_ports ddr2_cas_n]
set_property PACKAGE_PIN H3 [get_ports ddr2_we_n]
set_property PACKAGE_PIN G3 [get_ports {ddr2_cs_n[0]}]
set_property PACKAGE_PIN H4 [get_ports {ddr2_cke[0]}]
set_property PACKAGE_PIN G4 [get_ports {ddr2_odt[0]}]
set_property PACKAGE_PIN E1 [get_ports {ddr2_ck_p[0]}]
set_property PACKAGE_PIN D1 [get_ports {ddr2_ck_n[0]}]
set_property PACKAGE_PIN N4 [get_ports {ddr2_dm[0]}]
set_property PACKAGE_PIN M3 [get_ports {ddr2_dm[1]}]
set_property PACKAGE_PIN P5 [get_ports {ddr2_dqs_p[0]}]
set_property PACKAGE_PIN P4 [get_ports {ddr2_dqs_n[0]}]
set_property PACKAGE_PIN M1 [get_ports {ddr2_dqs_p[1]}]
set_property PACKAGE_PIN L1 [get_ports {ddr2_dqs_n[1]}]
set_property PACKAGE_PIN N3 [get_ports {ddr2_dq[0]}]
set_property PACKAGE_PIN R1 [get_ports {ddr2_dq[1]}]
set_property PACKAGE_PIN P1 [get_ports {ddr2_dq[2]}]
set_property PACKAGE_PIN P2 [get_ports {ddr2_dq[3]}]
set_property PACKAGE_PIN N2 [get_ports {ddr2_dq[4]}]
set_property PACKAGE_PIN M6 [get_ports {ddr2_dq[5]}]
set_property PACKAGE_PIN M5 [get_ports {ddr2_dq[6]}]
set_property PACKAGE_PIN P6 [get_ports {ddr2_dq[7]}]
set_property PACKAGE_PIN K4 [get_ports {ddr2_dq[8]}]
set_property PACKAGE_PIN J4 [get_ports {ddr2_dq[9]}]
set_property PACKAGE_PIN L3 [get_ports {ddr2_dq[10]}]
set_property PACKAGE_PIN K3 [get_ports {ddr2_dq[11]}]
set_property PACKAGE_PIN M2 [get_ports {ddr2_dq[12]}]
set_property PACKAGE_PIN K6 [get_ports {ddr2_dq[13]}]
set_property PACKAGE_PIN J6 [get_ports {ddr2_dq[14]}]
set_property PACKAGE_PIN L5 [get_ports {ddr2_dq[15]}]

set_property IOSTANDARD DIFF_HSTL_II_18 [get_ports {ddr2_dqs_p[*] ddr2_dqs_n[*]}]
set_property IOSTANDARD HSTL_II_18 [get_ports {ddr2_dq[*] ddr2_dm[*]}]
set_property IOSTANDARD HSTL_I_18 [get_ports {ddr2_addr[*] ddr2_ba[*] ddr2_ras_n ddr2_cas_n ddr2_we_n ddr2_cs_n[0] ddr2_cke[0] ddr2_odt[0] ddr2_ck_p[0] ddr2_ck_n[0]}]
set_property SLEW FAST [get_ports {ddr2_dq[*] ddr2_dm[*] ddr2_dqs_p[*] ddr2_dqs_n[*] ddr2_addr[*] ddr2_ba[*] ddr2_ras_n ddr2_cas_n ddr2_we_n ddr2_cs_n[0] ddr2_cke[0] ddr2_odt[0] ddr2_ck_p[0] ddr2_ck_n[0]}]

# A13 of the DDR2 (bank 35, 1.8 V), not used by a 1 Gbit part: held low.
set_property PACKAGE_PIN F4 [get_ports SDRAM_A13]
set_property IOSTANDARD HSTL_I_18 [get_ports SDRAM_A13]

# ddr2_lean's lock / IDELAYCTRL-ready synchronizer into ui_clk (the PLL's LOCKED has no launching clock).
set_false_path -to [get_cells -hier -filter {NAME =~ *lk_s_reg[0]}]

# The pads of the DDR2 are not timed: the read side is calibrated (IDELAY taps, bitslip, read delay),
# the write side is a fixed pattern on the 600 Mbit/s slot grid, the command is 3.3 ns from CK on both sides.
set_false_path -from [get_ports {ddr2_dq[*] ddr2_dqs_p[*] ddr2_dqs_n[*]}]
set_false_path -to [get_ports {ddr2_dq[*] ddr2_dqs_p[*] ddr2_dqs_n[*] ddr2_dm[*] ddr2_addr[*] ddr2_ba[*] ddr2_ras_n ddr2_cas_n ddr2_we_n ddr2_cs_n[0] ddr2_cke[0] ddr2_odt[0] ddr2_ck_p[0] ddr2_ck_n[0]}]
