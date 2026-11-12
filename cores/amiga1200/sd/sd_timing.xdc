# T149: SD SPI at 25 MHz SCK (rate 1, 100 MHz cpu_clk), for the flat builds of amiga1200 and zxnext only (the host's pins.xdc has
# no SD timing and stays as it is). Read AFTER pins.xdc by cores/amiga1200/sd/sd_mono.tcl. sd_spi.vhd puts SCK, MOSI and SSn in IOB
# output flops and takes MISO in an IOB input flop (attribute IOB on sck_o, mosi_o, ssn_o, miso_i), sampling the pad one clock after
# the rising SCK edge.
# Reference clock cpu_clk (mmcm_ddr CLKOUT2). A launch edge F is the clock that makes the falling SCK edge; the rising edge
# follows at F+20 ns and the sample is taken at F+30 ns.
#  MISO max 23.5 = SCK pad 5.0 (IOB flop + slow LVCMOS33 OBUF) + card output 14 + board 3 + 1.5 for the clock-tree latency
#  of the launching flop that STA does not count for an input delay; min 3.5 = 2.0 + 0 + 1.5. Setup is captured 3 clocks
#  after F, hold is checked against F (the next card output change comes at F+40 ns or later, 10 ns after the sample).
#  MOSI/SSn: the card needs 5 ns setup and 5 ns hold around the rising SCK edge at F+20 ns; both leave the same kind of
#  IOB flop as SCK, so the SCK pad latency cancels. Setup is captured 2 clocks after F, hold against the rising edge before F.
set sd_clk [get_clocks -of_objects [get_pins mmcm_ddr/CLKOUT2]]
set_input_delay -clock $sd_clk -max 23.5 [get_ports SD_MISO]
set_input_delay -clock $sd_clk -min 3.5 [get_ports SD_MISO]
set_multicycle_path 3 -setup -end -from [get_ports SD_MISO] -to $sd_clk
set_multicycle_path 2 -hold -end -from [get_ports SD_MISO] -to $sd_clk
set_output_delay -clock $sd_clk -max 6.0 [get_ports {SD_MOSI SD_SSn}]
set_output_delay -clock $sd_clk -min -5.0 [get_ports {SD_MOSI SD_SSn}]
set_multicycle_path 2 -setup -end -from $sd_clk -to [get_ports {SD_MOSI SD_SSn}]
set_multicycle_path 3 -hold -end -from $sd_clk -to [get_ports {SD_MOSI SD_SSn}]
