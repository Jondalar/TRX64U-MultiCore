// T143 D2: the machine's 48 MHz clock, from the host's 100 MHz cpu_clk: VCO 960 MHz (100 / 5 x 48), divided by 20. The vendored chip models were written for one 48 MHz
// clock with 24 MHz enables (neogeo.sv) and run unchanged on it. A fifth MMCM of the XC7A50T (the host uses four).
`timescale 1ns / 1ps
module neo_clk48 (
    input  wire cpu_clk,
    output wire clk48,
    output wire locked
);
    wire fb, o0;
    MMCME2_BASE #(
        .CLKIN1_PERIOD(10.0), .DIVCLK_DIVIDE(5), .CLKFBOUT_MULT_F(48.0),
        .CLKOUT0_DIVIDE_F(20.0)
    ) mmcm_neo (
        .CLKIN1(cpu_clk), .CLKFBIN(fb), .CLKFBOUT(fb), .CLKOUT0(o0),
        .LOCKED(locked), .RST(1'b0), .PWRDWN(1'b0),
        .CLKOUT0B(), .CLKOUT1(), .CLKOUT1B(), .CLKOUT2(), .CLKOUT2B(), .CLKOUT3(),
        .CLKOUT3B(), .CLKOUT4(), .CLKOUT5(), .CLKOUT6(), .CLKFBOUTB()
    );
    BUFG bg48 (.I(o0), .O(clk48));
endmodule
