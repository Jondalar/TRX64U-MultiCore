// T164 WP8 bench models for the Xilinx primitives and the GTP wrapper that hdmi_out.v instantiates (not synthesizable, not the board).
//   MMCME2_BASE  CLKOUT0 = 2 x CLKIN1, rising edges together with CLKIN1's (as the real one is phase-locked to it); LOCKED 16 CLKIN1 clocks
//                after RST falls. Valid for the one multiplier hdmi_out uses (x14 / 7).
//   BUFG         wire
//   hdmi_gtp     usrclk = refclk / 2 (toggles on the rising edge of refclk_p, as pix_clk = refclk), ready 40 usrclk clocks after reset falls
//                and a refclk is running; the lane words tx0..tx3 are read by the bench (hierarchically) at every usrclk edge.
//   rst_sync     top.v's
`timescale 1fs/1fs
module BUFG (input I, output O);
    assign O = I;
endmodule

module MMCME2_BASE #(
    parameter CLKIN1_PERIOD = 10.0, parameter DIVCLK_DIVIDE = 1, parameter CLKFBOUT_MULT_F = 1.0, parameter CLKOUT0_DIVIDE_F = 1.0
) (
    input CLKIN1, CLKFBIN, RST, PWRDWN,
    output CLKFBOUT, output reg CLKOUT0 = 1'b0, output reg LOCKED = 1'b0,
    output CLKOUT0B, CLKOUT1, CLKOUT1B, CLKOUT2, CLKOUT2B, CLKOUT3, CLKOUT3B, CLKOUT4, CLKOUT5, CLKOUT6, CLKFBOUTB
);
    assign CLKFBOUT = CLKIN1;
    assign {CLKOUT0B, CLKOUT1, CLKOUT1B, CLKOUT2, CLKOUT2B, CLKOUT3, CLKOUT3B, CLKOUT4, CLKOUT5, CLKOUT6, CLKFBOUTB} = 11'd0;
    time t_last = 0, per = 0;
    integer n = 0;
    always @(posedge CLKIN1) begin
        CLKOUT0 = 1'b1;
        if (t_last != 0) per = $time - t_last;
        t_last = $time;
        if (RST) begin n = 0; LOCKED <= 1'b0; end
        else begin
            n = n + 1;
            if (n >= 16) LOCKED <= 1'b1;
        end
        if (per != 0) begin
            #(per / 4) CLKOUT0 = 1'b0;
            #(per / 4) CLKOUT0 = 1'b1;
            #(per / 4) CLKOUT0 = 1'b0;
        end
    end
    always @(posedge RST) begin LOCKED <= 1'b0; n = 0; end
endmodule

module hdmi_gtp (
    input  wire        sysclk,
    input  wire        reset,
    input  wire        refclk_p, refclk_n,
    input  wire [19:0] tx0, tx1, tx2, tx3,
    output reg         usrclk = 1'b0,
    output reg         ready = 1'b0,
    output wire [3:0]  txp, txn
);
    assign txp = 4'd0;
    assign txn = 4'd0;
    always @(posedge refclk_p) usrclk <= ~usrclk;
    integer n = 0;
    always @(posedge usrclk or posedge reset)
        if (reset) begin n <= 0; ready <= 1'b0; end
        else begin n <= n + 1; if (n >= 40) ready <= 1'b1; end
endmodule

module rst_sync (
    input  wire clk,
    input  wire arst,
    output wire rst
);
    reg [2:0] rst_sync = 3'b111;
    always @(posedge clk or posedge arst)
        if (arst) rst_sync <= 3'b111;
        else      rst_sync <= {rst_sync[1:0], 1'b0};
    assign rst = rst_sync[2];
endmodule
