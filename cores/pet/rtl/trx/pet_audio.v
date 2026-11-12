// T134: the PET CB2 sound line as 24 bit samples of the host audio clock (from cores/c264/rtl/trx/c264_audio.v, T90, same author; the input is a
// 16 bit level here): averaged over 656 sys_clk (48.06 kHz), the DC level taken out by a slow tracker (a silent line is exactly 0), handed to mclk
// by a toggle. The pause mutes. Mono. Written for this project (TREX), no upstream code.
`timescale 1ns / 1ps
module pet_audio (
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
    reg        primed = 1'b0;                       // the first window is thrown away: the line is undefined for the first clocks of a simulation and the tracker would keep that
    (* ASYNC_REG = "TRUE" *) reg [2:0] t_s = 3'b000;
    wire signed [27:0] a_ext = {{12{a_in[15]}}, a_in};
    wire signed [27:0] acc_n = acc + a_ext;
    wire signed [27:0] diff  = acc_n - dc;
    // sample = (acc - dc) * 400 / 256, i.e. (avg - dc/656) * 1024. The TED's loudest level is both voices at volume 8 = 2 x 8191
    // (ted.v GAIN = 2, a unipolar 0..16382), so +-8191 around its mean -> +-8.39 M (24 bit full scale); one voice at volume 8 is
    // half of that (the VC-20 core's full swing is +-5 M)
    wire signed [35:0] scaled = (diff <<< 8) + (diff <<< 7) + (diff <<< 4);
    wire signed [35:0] s8 = scaled >>> 8;
    wire signed [23:0] sat = (s8 > 36'sd8388607) ? 24'sh7FFFFF : (s8 < -36'sd8388608) ? 24'sh800000 : s8[23:0];
    always @(posedge clk) begin
        if (cnt == 10'd655) begin
            cnt <= 10'd0;
            acc <= 28'sd0;
            primed <= 1'b1;
            if (mute || !primed) begin
                smp <= 24'sd0;
            end else begin
                smp <= sat;
                dc  <= dc + (diff >>> 8);
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
