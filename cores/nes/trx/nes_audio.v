// T95: the APU's 16 bit sample (NES_MiSTer's `sample`, unsigned, mixed by the machine) into 24 bit samples of the host's audio
// clock: one sample every 2112 clocks (47.35 kHz at 100 MHz), the DC level taken out by a slow tracker (a silent APU is
// exactly 0 and the host's mixer sees no step), scaled to the 24 bit range and handed to mclk by a toggle (stable for 2112
// clocks, mclk reads it two clocks after the toggle). The pause mutes: the next samples are exactly 0 and the tracker stands.
// Mono. Written for this project (TREX), no upstream code. The machine's sample changes with its clock enable and is read here on
// the free clock (one word of one clock domain, no crossing); polarity is the machine's (inverted), which nobody hears.
`timescale 1ns / 1ps
module nes_audio (
    input  wire               clk,           // the machine clock (cpu_clk)
    input  wire [15:0]        a_in,
    input  wire               cap,           // the clock before a machine tick: the machine's outputs are settled then
    input  wire               mute,
    input  wire               mclk,
    output reg  signed [23:0] audio_l = 24'sd0,
    output reg  signed [23:0] audio_r = 24'sd0
);
    reg [11:0] cnt = 12'd0;
    reg signed [24:0] dc = 25'sd0;                   // the average (x 2^8) that the tracker follows
    reg signed [23:0] smp = 24'sd0;
    reg        tgl = 1'b0;
    (* ASYNC_REG = "TRUE" *) reg [2:0] t_s = 3'b000;
    wire signed [24:0] v = {1'b0, a_in, 8'd0};       // sample x 256
    wire signed [24:0] diff = v - dc;
    // toward zero, so that |diff| < 256 is exactly 0 (a signal that stopped moving ends in exactly 0)
    wire signed [24:0] dz = diff + ((diff < 0) ? 25'sd255 : 25'sd0);
    wire signed [16:0] q16 = dz >>> 8;               // the sample minus its average, in sample units
    wire signed [23:0] out = {q16, 7'd0};            // x 128: a full 16 bit swing is +-2^22 of the 24 bit range
    always @(posedge clk) begin
        if (cnt >= 12'd2111 && cap) begin
            cnt <= 12'd0;
            if (mute) smp <= 24'sd0;
            else begin
                smp <= out;
                dc <= dc + {{8{q16[16]}}, q16};
            end
            tgl <= ~tgl;
        end else if (cnt < 12'd4095) cnt <= cnt + 12'd1;
    end
    always @(posedge mclk) begin
        t_s <= {t_s[1:0], tgl};
        if (t_s[2] != t_s[1]) begin
            audio_l <= smp;
            audio_r <= smp;
        end
    end
endmodule
