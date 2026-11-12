// T116 D6 / P6: the Next's mixed stereo sound (o_AUDIO_L/R, 13 bit, unsigned 0..about 6000: three AY, the Soundrive DACs, the beeper and the tape
// ear mixed by audio_mixer.vhd) into 24 bit signed samples of the host's audio clock: one sample every 2112 clocks (47.35 kHz at 100 MHz), the DC
// level taken out by a slow tracker per channel (a silent machine is exactly 0 and the host's mixer sees no step), scaled to the 24 bit
// range and handed to mclk by a toggle (stable for 2112 clocks, mclk reads it two clocks after the toggle). The pause mutes: the next samples are
// exactly 0 and the tracker stands. Written for this project (TREX), no upstream code; the circuit of nes_audio.v (T95) twice.
`timescale 1ns / 1ps
module zxn_audio (
    input  wire               clk,           // cpu_clk
    input  wire [12:0]        l_in,
    input  wire [12:0]        r_in,
    input  wire               mute,
    input  wire               mclk,
    output reg  signed [23:0] audio_l = 24'sd0,
    output reg  signed [23:0] audio_r = 24'sd0
);
    reg [11:0] cnt = 12'd0;
    reg signed [24:0] dcl = 25'sd0, dcr = 25'sd0;          // the averages (x 2^8) that the trackers follow
    reg signed [23:0] sl = 24'sd0, sr = 24'sd0;
    reg        tgl = 1'b0;
    (* ASYNC_REG = "TRUE" *) reg [2:0] t_s = 3'b000;
    wire signed [24:0] vl = {4'd0, l_in, 8'd0};            // sample x 256
    wire signed [24:0] vr = {4'd0, r_in, 8'd0};
    wire signed [24:0] dl = vl - dcl, dr = vr - dcr;
    // toward zero, so that |diff| < 256 is exactly 0 (a signal that stopped moving ends in exactly 0)
    wire signed [24:0] dzl = dl + ((dl < 0) ? 25'sd255 : 25'sd0);
    wire signed [24:0] dzr = dr + ((dr < 0) ? 25'sd255 : 25'sd0);
    wire signed [16:0] ql = dzl >>> 8;                       // the sample minus its average, in sample units
    wire signed [16:0] qr = dzr >>> 8;
    wire signed [23:0] ol = {ql[14:0], 9'd0};                // x 512: the mixer's full range (about 6000) is about 3 M of the 8.4 M
    wire signed [23:0] or_ = {qr[14:0], 9'd0};
    always @(posedge clk) begin
        if (cnt >= 12'd2111) begin
            cnt <= 12'd0;
            if (mute) begin sl <= 24'sd0; sr <= 24'sd0; end
            else begin
                sl <= ol; sr <= or_;
                dcl <= dcl + {{8{ql[16]}}, ql}; dcr <= dcr + {{8{qr[16]}}, qr};
            end
            tgl <= ~tgl;
        end else cnt <= cnt + 12'd1;
    end
    always @(posedge mclk) begin
        t_s <= {t_s[1:0], tgl};
        if (t_s[2] != t_s[1]) begin
            audio_l <= sl;
            audio_r <= sr;
        end
    end
endmodule
