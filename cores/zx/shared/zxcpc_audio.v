// T91 D7 (shared with the CPC core, T92): the machine's mixed 16 bit sample (signed, any rate up to sys_clk) into 24 bit samples of
// the host's audio clock: averaged over 656 sys_clk (48.06 kHz), the DC level taken out by a slow tracker (a constant input ends in
// exactly 0, so the host's mixer sees no step), scaled to the 24 bit range and handed to mclk by a toggle (stable for 656 clocks,
// mclk reads it two clocks after the toggle). `mute` (Freeze) makes the next samples exactly 0 and the tracker stands. Mono.
// Written for this project (TREX), no upstream code; the circuit is that of the NES and C16 cores' audio blocks.
`timescale 1ns / 1ps
module zxcpc_audio (
    input  wire               clk,
    input  wire signed [15:0] a_in,
    input  wire               mute,
    input  wire               mclk,
    output reg  signed [23:0] audio_l = 24'sd0,
    output reg  signed [23:0] audio_r = 24'sd0
);
    reg signed [27:0] acc = 28'sd0;                 // 656 x +-32768 = +-21.5 M
    reg [9:0]  cnt = 10'd0;
    reg signed [27:0] dc = 28'sd0;                  // the average that the tracker follows (same scale as acc)
    reg signed [23:0] smp = 24'sd0;
    reg        tgl = 1'b0;
    (* ASYNC_REG = "TRUE" *) reg [2:0] t_s = 3'b000;
    wire signed [27:0] a_ext = {{12{a_in[15]}}, a_in};
    wire signed [27:0] acc_n = acc + a_ext;
    wire signed [27:0] diff  = acc_n - dc;
    // toward zero, so that a signal that stopped moving ends in exactly 0 (|diff| < 256 is 0)
    wire signed [27:0] dz    = diff + ((diff < 0) ? 28'sd255 : 28'sd0);
    wire signed [27:0] dstep = dz >>> 8;
    // sample = (acc - dc) * 100 / 256, i.e. (avg - dc/656) * 256: full swing +-32768 -> +-8.4 M (24 bit)
    wire signed [35:0] scaled = ({{8{diff[27]}}, diff} <<< 6) + ({{8{diff[27]}}, diff} <<< 5) + ({{8{diff[27]}}, diff} <<< 2);
    wire signed [35:0] s8 = scaled >>> 8;
    wire signed [23:0] sat = (s8 > 36'sd8388607) ? 24'sh7FFFFF : (s8 < -36'sd8388608) ? 24'sh800000 : s8[23:0];
    wire signed [23:0] sat0 = (diff > -28'sd256 && diff < 28'sd256) ? 24'sd0 : sat;
    always @(posedge clk) begin
        if (cnt == 10'd655) begin
            cnt <= 10'd0;
            acc <= 28'sd0;
            if (mute) begin
                smp <= 24'sd0;
            end else begin
                smp <= sat0;
                dc  <= dc + dstep;
            end
            tgl <= ~tgl;
        end else begin
            cnt <= cnt + 10'd1;
            acc <= acc_n;
        end
    end
    always @(posedge mclk) begin
        t_s <= {t_s[1:0], tgl};
        if (t_s[2] != t_s[1]) begin
            audio_l <= smp;
            audio_r <= smp;
        end
    end
endmodule
