// T160: the master volume of every core (host/top.v): a gain on the two 24-bit audio samples before HDMI and I2S. Moved here from the A1200 trial.
// out = sat24(in * vol / 128): vol 128 passes the sample unchanged, 0 is silence, 255 is +6 dB and saturates instead of wrapping.
// T189: the input is 25 bit (the tone filter before it may reach +-2x full scale); a 24-bit core sign-extends. One 25x9 multiplier per channel (one DSP48 each), registered once in clk (mclk). The volume byte comes from the register block
// (cpu_clk, quasi static): two flip-flops into clk, accepted only when two consecutive reads agree, so a byte that changes
// in the middle of a write is never used half old, half new.
// Written for this project.
`timescale 1ns / 1ps
module trx_gain (
    input  wire               clk,
    input  wire [7:0]         vol,                      // another clock, quasi static
    input  wire signed [24:0] in_l,
    input  wire signed [24:0] in_r,
    output reg  signed [23:0] out_l = 24'sd0,
    output reg  signed [23:0] out_r = 24'sd0
);
    (* ASYNC_REG = "TRUE" *) reg [7:0] v1 = 8'h80, v2 = 8'h80;
    reg [7:0] v = 8'h80;
    always @(posedge clk) begin
        v1 <= vol; v2 <= v1;
        if (v1 == v2) v <= v2;
    end
    wire signed [33:0] pl = in_l * $signed({1'b0, v});
    wire signed [33:0] pr = in_r * $signed({1'b0, v});
    wire signed [26:0] sl = pl[33:7];
    wire signed [26:0] sr = pr[33:7];
    function signed [23:0] sat(input signed [26:0] x);
        sat = (x > 27'sd8388607) ? 24'sh7FFFFF : (x < -27'sd8388608) ? 24'sh800000 : x[23:0];
    endfunction
    always @(posedge clk) begin
`ifdef TRX_GAIN_MUT_BYPASS
        out_l <= in_l[23:0]; out_r <= in_r[23:0];                   // negative control of the bench: no gain at all
`else
        out_l <= sat(sl); out_r <= sat(sr);
`endif
    end
endmodule
