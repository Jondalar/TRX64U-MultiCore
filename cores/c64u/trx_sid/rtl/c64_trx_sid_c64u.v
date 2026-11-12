// SPDX-License-Identifier: GPL-3.0-or-later
// ---------------------------------------------------------------------------
// c64_trx_sid_c64u: the T25.1 SID engine in the place of c64_redip_sid in c64_top_c64u:
// trx_sid_c64u (decode, engine) and T25.3's trx_audio_c64u (the app's mixer gains for
// the two UltiSIDs, anti-alias decimation to the I2S frame, DC blocker).
// ---------------------------------------------------------------------------
`timescale 1 ns / 1 ps
module c64_trx_sid_c64u #(
    parameter MEMDIR = "."
) (
    input  wire        clk,
    input  wire        reset,
    input  wire        clk2x,
    input  wire        phi2_n,
    input  wire        clk_sample,
    input  wire        reset_sample,
    input  wire        cen,
    input  wire        we,
    input  wire  [9:0] addr,
    input  wire  [7:0] din,
    output wire  [7:0] dout,
    output wire        hit,
    input  wire        ps_v,                // T29: the turbo CPU's fast writes and reads (trx_sid_c64u.v)
    input  wire  [9:0] ps_a,
    input  wire  [7:0] ps_d,
    input  wire  [9:0] ra,
    output wire        fhit,
    output wire  [7:0] frd,
    output wire        f_full,
    output wire        f_empty,
    output wire        f_quiet,
    input  wire        audio_sample_en,
    output wire signed [23:0] audio_sample_word_0,  // L
    output wire signed [23:0] audio_sample_word_1,  // R
    input  wire        pot_x,
    input  wire        pot_y,
    output wire        pot_dis,
    input  wire [15:0] cfg_base,
    input  wire [15:0] cfg_mask,
    input  wire  [2:0] cfg_split,
    input  wire  [1:0] cfg_8580,
    input  wire  [1:0] cfg_res_high,
    input  wire  [3:0] cfg_digi,
    input  wire [49:0] cfg_rs8,
    input  wire        cv_clk,
    input  wire        cv_we,
    input  wire [11:0] cv_addr,
    input  wire  [7:0] cv_wdata,
    input  wire  [4:0] lv_addr,
    input  wire  [1:0] vmode,           // System Mode (T40), for the decimator ratio
    output wire  [7:0] lv_data,
    input  wire [31:0] mix_gains,           // {U2 L, U2 R, U1 L, U1 R}, 0x80 = 0 dB (any clock)
    input  wire signed [22:0] smp_l,        // T25.6 sampler (trx_sampler_c64u, clk2x)
    input  wire signed [22:0] smp_r,
    input  wire [31:0] mix_gains2           // {Smp R L, Smp R R, Smp L L, Smp L R} (any clock)
);

wire               sum_valid;
wire signed [19:0] sum_u0, sum_u1;

trx_sid_c64u #(.MEMDIR(MEMDIR)) sid (
    .clk(clk), .rst(reset), .clk2x(clk2x), .phi2_n(phi2_n),
    .cen(cen), .we(we), .addr(addr), .din(din), .dout(dout), .hit(hit),
    .ps_v(ps_v), .ps_a(ps_a), .ps_d(ps_d), .ra(ra), .fhit(fhit), .frd(frd),
    .f_full(f_full), .f_empty(f_empty), .f_quiet(f_quiet),
    .pot_x(pot_x), .pot_y(pot_y), .pot_dis(pot_dis),
    .cfg_base(cfg_base), .cfg_mask(cfg_mask), .cfg_split(cfg_split),
    .cfg_8580(cfg_8580), .cfg_res_high(cfg_res_high), .cfg_digi(cfg_digi), .cfg_rs8(cfg_rs8),
    .cv_clk(cv_clk), .cv_we(cv_we), .cv_addr(cv_addr), .cv_wdata(cv_wdata),
    .lv_addr(lv_addr), .lv_data(lv_data),
    .smp_valid(), .smp_sid(), .smp_data(),
    .sum_valid(sum_valid), .sum_u0(sum_u0), .sum_u1(sum_u1)
);

reg  [1:0] rst2 = 2'b11;
always @(posedge clk2x) rst2 <= {rst2[0], reset};

// T25.3: the app's gains, CIC + halfbands + fractional FIR to the I2S frame
trx_audio_c64u #(.MEMDIR(MEMDIR)) aud (
    .clk(clk2x), .rst(rst2[1]), .in_valid(sum_valid), .in0(sum_u0), .in1(sum_u1), .in2(smp_l), .in3(smp_r),
    .gains(mix_gains), .gains2(mix_gains2),
    .mclk(clk_sample), .mrst(reset_sample), .sample_en(audio_sample_en),
    .out_l(audio_sample_word_0), .out_r(audio_sample_word_1), .vmode(vmode));

endmodule
