// T143: the YM2610 mix (signed 16 bit, changes at about 55.5 kHz) into 24 bit signed samples of the host's audio clock: one sample every 1000 clocks of the machine
// clock (48 kHz), scaled by 128 (neogeo.sv handed the host the mix halved: AUDIO_L = snd_mix_l[16:1], the room the CD's audio had), handed to mclk by a toggle
// (stable for 1000 clocks, mclk reads it two clocks after the toggle). The pause mutes: the next samples are exactly 0. Written for this project (TREX), no
// upstream code; the circuit of zxn_audio.v (T116) without the DC tracker (the YM2610's output is signed and has no offset).
`timescale 1ns / 1ps
module neo_audio (
    input  wire               clk,           // the machine clock (48 MHz)
    input  wire signed [15:0] l_in,
    input  wire signed [15:0] r_in,
    input  wire               mute,
    input  wire               mclk,
    output reg  signed [23:0] audio_l = 24'sd0,
    output reg  signed [23:0] audio_r = 24'sd0
);
    reg [9:0] cnt = 10'd0;
    reg signed [23:0] sl = 24'sd0, sr = 24'sd0;
    reg        tgl = 1'b0;
    (* ASYNC_REG = "TRUE" *) reg [2:0] t_s = 3'b000;
    // a word that changes with the YM2610's sample strobe is read twice and taken when both reads agree
    reg signed [15:0] l_q = 16'sd0, r_q = 16'sd0;
    always @(posedge clk) begin
        l_q <= l_in; r_q <= r_in;
        if (cnt == 10'd999) begin
            cnt <= 10'd0;
            if (mute) begin sl <= 24'sd0; sr <= 24'sd0; end
            else if (l_q == l_in && r_q == r_in) begin
                sl <= {{1{l_in[15]}}, l_in, 7'd0};
                sr <= {{1{r_in[15]}}, r_in, 7'd0};
            end
            tgl <= ~tgl;
        end else cnt <= cnt + 10'd1;
    end
    always @(posedge mclk) begin
        t_s <= {t_s[1:0], tgl};
        if (t_s[2] != t_s[1]) begin
            audio_l <= sl;
            audio_r <= sr;
        end
    end
endmodule
