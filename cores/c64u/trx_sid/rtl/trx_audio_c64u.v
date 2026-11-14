// SPDX-License-Identifier: GPL-3.0-or-later
// ---------------------------------------------------------------------------
// trx_audio_c64u: T25.3 mixer and decimator (TRX64-Ultimate/docs/specs/T25.3-lp-filter.md).
// Written from textbook DSP (Hogenauer CIC, halfband FIR, polyphase
// fractional resampling with linear interpolation between phases); the
// coefficients come from trx64/trx_sid_c64u/model/design_decim.py. No lp_filter,
// sys_to_aud or other HDL of others was used.
//
//   per C64 cycle (in_valid, clk = the SID engine's clk2x):
//     mixer      L/R = UltiSID 1 x gain + UltiSID 2 x gain + sampler L x gain + sampler R x gain (the app's
//                bytes, 0x80 = 0 dB), 24 bit, saturated; one multiplier, 8 clocks
//     CIC        R = 4, N = 6, 36-bit state in LUTRAM, one adder  -> 246 kHz
//     HB1        15-tap halfband /2                                -> 123 kHz
//     HB2        35-tap halfband /2                                -> 61.6 kHz
//     FRAC       56 taps x 32 phases, phases linearly interpolated; the
//                output position advances by 49259/38400 input samples, an
//                exact integer accumulator (C64 clock and MCLK share the
//                crystal: 48,003.07 Hz out of 985,243.06 Hz, no drift);
//                per System Mode (T40 §9.5, vmode) N/DEN = 49259/38400 PAL,
//                409045/307200 NTSC, 563115/422400 PAL-60, 394785/307200
//                NTSC-50, the phase rem x 32 x 4096 / DEN = rem x M >> 15
//   -> cdc_handshake to mclk -> 8-entry FIFO, read once per I2S frame; when it
//      runs dry (the machine is paused: no C64 cycles) the output decays to 0
//      (time constant 64 frames, 1.3 ms; < 2 % after 5 ms) instead of stepping -> DC blocker (first order, ~7 Hz).
// ---------------------------------------------------------------------------
`timescale 1 ns / 1 ps
module trx_audio_c64u #(
    parameter MEMDIR = ".",
    parameter FIR_SHIFT = 17                // decim_fir.mem scale 2^FIR_SHIFT
) (
    input  wire        clk,
    input  wire        rst,
    input  wire        in_valid,            // one clock per C64 cycle
    input  wire signed [19:0] in0,          // UltiSID 1
    input  wire signed [19:0] in1,          // UltiSID 2
    input  wire signed [22:0] in2,          // sampler L (trx_sampler_c64u, T25.6)
    input  wire signed [22:0] in3,          // sampler R
    input  wire [31:0] gains,               // {U2 L, U2 R, U1 L, U1 R} (any clock, quasi-static)
    input  wire [31:0] gains2,              // {Smp R L, Smp R R, Smp L L, Smp L R}
    input  wire        mclk,
    input  wire        mrst,
    input  wire        sample_en,           // mclk, once per I2S frame
    output reg  signed [23:0] out_l = 24'sd0,
    output reg  signed [23:0] out_r = 24'sd0,
    input  wire [1:0]  vmode                // System Mode (any clock; changes while no samples come)
);

function signed [23:0] sat24(input signed [47:0] x);
    sat24 = x > 48'sh7fffff ? 24'sh7fffff : x < -48'sh800000 ? -24'sh800000 : x[23:0];
endfunction

// ---- mixer: four sources x two gains through ONE multiplier, one product per clock
// (UltiSID 1, UltiSID 2, sampler L, sampler R; T25.6). Inputs are latched at in_valid; L = sources x gain L
// (g[15:8], g[31:24], gk[15:8], gk[31:24]), R = sources x gain R (g[7:0], g[23:16], gk[7:0], gk[23:16]).
(* ASYNC_REG = "TRUE" *) reg [31:0] g1, g, gk1, gk;
always @(posedge clk) begin g1 <= gains; g <= g1; gk1 <= gains2; gk <= gk1; end
reg  signed [19:0] x0, x1;
reg  signed [22:0] x2, x3;
reg  [3:0]  ms = 4'd15;                     // product being started, 0..7; 15 idle
reg  signed [22:0] xa;
reg  [7:0]  gb;
reg  signed [31:0] prod;
reg  signed [34:0] acc;
reg  [3:0]  md = 4'd15;                     // the product `prod` holds
reg  signed [23:0] mx_l, mx_r;
reg                mx_v = 1'b0;
wire [1:0]  mi = ms[1:0];
always @* begin
    case (mi)
        2'd0:    xa = {{3{x0[19]}}, x0};
        2'd1:    xa = {{3{x1[19]}}, x1};
        2'd2:    xa = x2;
        default: xa = x3;
    endcase
    case ({ms[2], mi})
        3'd0: gb = g[15:8];   3'd1: gb = g[31:24];   3'd2: gb = gk[15:8];   3'd3: gb = gk[31:24];
        3'd4: gb = g[7:0];    3'd5: gb = g[23:16];   3'd6: gb = gk[7:0];    default: gb = gk[23:16];
    endcase
end
wire signed [34:0] acc_n = (md[1:0] == 2'd0 ? 35'sd0 : acc) + prod;
always @(posedge clk) begin
    mx_v <= 1'b0;
    if (in_valid && !rst) begin
        x0 <= in0; x1 <= in1; x2 <= in2; x3 <= in3;
        ms <= 4'd0;
    end else if (ms != 4'd15) ms <= ms == 4'd7 ? 4'd15 : ms + 4'd1;
    md <= ms;
    if (ms != 4'd15) prod <= xa * $signed({1'b0, gb});
    if (md != 4'd15) begin
        acc <= acc_n;
        // x * g / 128 << 4 -> 24 bit, saturated; the fourth product of a side completes it
        if (md == 4'd3) mx_l <= sat24(acc_n >>> 3);
        if (md == 4'd7) begin mx_r <= sat24(acc_n >>> 3); mx_v <= 1'b1; end
    end
    if (rst) begin ms <= 4'd15; md <= 4'd15; end
end

// ---- CIC: integrators at {ch, 0..5}, comb delays at {ch, 8..13}; each of
// the 12 (24 with the combs) steps takes two clocks: read into registers,
// then add and write back
reg  [35:0] cic [0:31];
integer k;
initial for (k = 0; k < 32; k = k + 1) cic[k] = 36'd0;
reg  [4:0]  cs = 5'd31;                     // step: 0..11 integrators, 12..23 combs, 31 idle
reg         cph = 1'b0;                     // 0: read, 1: add and write
reg  [1:0]  c4 = 2'd0;
reg  signed [35:0] cprev, crd, c5;
reg  signed [23:0] cin_l, cin_r;
reg         cdec;
wire        c_ch   = cs >= 5'd12 ? (cs >= 5'd18) : (cs >= 5'd6);
wire [2:0]  c_k    = cs >= 5'd12 ? (cs >= 5'd18 ? cs - 5'd18 : cs - 5'd12) : (cs >= 5'd6 ? cs - 5'd6 : cs);
wire        c_comb = cs >= 5'd12;
wire [4:0]  c_a    = {c_ch, c_comb, c_k};
// integrator: new = old + prev; comb: out = prev - delay, delay <= prev
wire signed [35:0] c_in = (!c_comb && c_k == 3'd0) ? (c_ch ? {{12{cin_r[23]}}, cin_r} : {{12{cin_l[23]}}, cin_l}) :
                          ( c_comb && c_k == 3'd0) ? c5 : cprev;
wire signed [35:0] c_res = c_comb ? c_in - crd : crd + c_in;
reg  signed [23:0] s1_l, s1_r;
reg                s1_v = 1'b0;
always @(posedge clk) begin
    s1_v <= 1'b0;
    if (mx_v) begin
        cs <= 5'd0;
        cph <= 1'b0;
        cin_l <= mx_l;
        cin_r <= mx_r;
        cdec <= c4 == 2'd3;
        c4 <= c4 + 2'd1;
    end else if (cs != 5'd31) begin
        if (!cph) begin
            crd <= cic[c_a];
            c5 <= cic[{c_ch, 1'b0, 3'd5}];
            cph <= 1'b1;
        end else begin
            cic[c_a] <= c_comb ? c_in : c_res;
            cprev <= c_res;
            if (c_comb && c_k == 3'd5) begin
                if (c_ch) begin s1_r <= c_res >>> 12; s1_v <= 1'b1; end
                else s1_l <= c_res >>> 12;
            end
            cph <= 1'b0;
            cs <= (cs == 5'd11 && !cdec) || cs == 5'd23 ? 5'd31 : cs + 5'd1;
        end
    end
    if (rst) begin cs <= 5'd31; c4 <= 2'd0; end
end

// ---- halfbands
wire signed [23:0] s2_l, s2_r, s3_l, s3_r;
wire               s2_v, s3_v;
trx_hb_c64u #(.NT(15), .MEM({MEMDIR, "/decim_hb1.mem"})) hb1 (
    .clk(clk), .rst(rst), .in_valid(s1_v), .in_l(s1_l), .in_r(s1_r),
    .out_valid(s2_v), .out_l(s2_l), .out_r(s2_r));
trx_hb_c64u #(.NT(35), .MEM({MEMDIR, "/decim_hb2.mem"})) hb2 (
    .clk(clk), .rst(rst), .in_valid(s2_v), .in_l(s2_l), .in_r(s2_r),
    .out_valid(s3_v), .out_l(s3_l), .out_r(s3_r));

// ---- fractional FIR: 56 taps, 32 phases, prototype 1792 (first half stored)
localparam T = 56, P = 32, PT = T * P, PH = PT / 2;
reg  signed [17:0] fir [0:PH-1];
initial $readmemh({MEMDIR, "/decim_fir.mem"}, fir);
reg  [23:0] xl [0:63], xr [0:63];
reg  [5:0]  fw = 6'd0;                      // next write position
reg  [19:0] rem = 20'd0;                    // (next output - this input) x DEN
(* ASYNC_REG = "TRUE" *) reg [1:0] vm1 = 2'd0, vm = 2'd0;
always @(posedge clk) begin vm1 <= vmode; vm <= vm1; end
reg  [19:0] f_n, f_den;
reg  [16:0] f_m;
always @(posedge clk)
    case (vm)
        2'd1:    begin f_n <= 20'd409045; f_den <= 20'd307200; f_m <= 17'd13981;  end
        2'd2:    begin f_n <= 20'd563115; f_den <= 20'd422400; f_m <= 17'd10168;  end
        2'd3:    begin f_n <= 20'd394785; f_den <= 20'd307200; f_m <= 17'd13981;  end
        default: begin f_n <= 20'd49259;  f_den <= 20'd38400;  f_m <= 17'd111848; end
    endcase
reg  [16:0] d;                              // mu * 32 * 4096
reg         fbusy = 1'b0;
reg  [5:0]  fj;                             // tap
reg  [5:0]  fn;                             // position of x[n]
// tap j: prototype index j*32 + d/4096, fraction d%4096
wire [10:0] fi0 = {fj, 5'd0} + d[16:12];
wire [10:0] fi1 = fi0 + 11'd1;
function [9:0] mirror(input [10:0] i);
    mirror = i < PH ? i[9:0] : (PT - 1 - i);
endfunction
reg  signed [17:0] h0, h1;
reg  [5:0]  fx1;                            // x index through the pipeline
reg  [11:0] fr1;
reg         v1 = 1'b0, v2 = 1'b0, v3 = 1'b0, v4 = 1'b0, v4d = 1'b0;
reg         z0, z1;                         // index PT is 0
always @(posedge clk) begin
    h0 <= fir[mirror(fi0)];
    h1 <= fir[mirror(fi1)];
    z0 <= fi0 >= PT;
    z1 <= fi1 >= PT;
end
(* use_dsp = "yes" *) reg  signed [18:0] cc;
reg  signed [18:0] hd, h0q;
reg  [11:0] fr2;
reg  [5:0]  fx2;
reg  signed [23:0] cxl, cxr;
reg  signed [42:0] pml, pmr;
wire signed [31:0] hdm = hd * $signed({1'b0, fr2});
// the accumulators, the interpolated coefficient and the DC blocker state are adders that sit in DSP48s (LUT budget, T103)
(* use_dsp = "yes" *) reg  signed [47:0] acc_l = 48'sd0;
(* use_dsp = "yes" *) reg  signed [47:0] acc_r = 48'sd0;
// rem x 32 / DEN in 1/4096: rem x M >> 15 (PAL: x 111848 >> 15 = x 55924 >> 14)
wire [36:0] mu_m = rem * f_m;
// t: tap address -> t+1: h1 - h0 -> t+2: interpolated coefficient, x -> t+3:
// products -> t+4: sums
always @(posedge clk) begin
    if (s3_v) begin
        xl[fw] <= s3_l;
        xr[fw] <= s3_r;
        fw <= fw + 6'd1;
        if (rem < f_den) begin
            // an output lies between this input and the next
            d <= mu_m[31:15];
            fn <= fw;
            fj <= 6'd0;
            fbusy <= 1'b1;
            rem <= rem + f_n - f_den;
        end else
            rem <= rem - f_den;
    end
    v1 <= fbusy;
    fx1 <= fn - fj;
    fr1 <= d[11:0];
    if (fbusy) begin
        fj <= fj + 6'd1;
        if (fj == T - 1) fbusy <= 1'b0;
    end
    v2 <= v1;
    hd <= (z1 ? 18'sd0 : h1) - (z0 ? 18'sd0 : h0);
    h0q <= z0 ? 18'sd0 : h0;
    fr2 <= fr1;
    fx2 <= fx1;
    v3 <= v2;
    cc <= h0q + (hdm >>> 12);
    cxl <= xl[fx2];
    cxr <= xr[fx2];
    v4 <= v3;
    pml <= cc * cxl;
    pmr <= cc * cxr;
    v4d <= v4;
    if (v4) begin
        acc_l <= acc_l + pml;
        acc_r <= acc_r + pmr;
    end else if (v4d) begin
        acc_l <= 48'sd0;
        acc_r <= 48'sd0;
    end
    if (rst) begin fbusy <= 1'b0; rem <= 20'd0; end
end
wire fo_done = v4d && !v4;              // the sums are complete

// ---- to mclk
reg  [47:0] send_d;
reg         send = 1'b0;
wire        busy;
always @(posedge clk) begin
    if (fo_done) begin
        send_d <= {sat24(acc_l >>> FIR_SHIFT), sat24(acc_r >>> FIR_SHIFT)};
        send <= 1'b1;
    end else if (send && !busy)
        send <= 1'b0;
end
wire [47:0] m_d;
wire        m_v;
reg         m_ack;
cdc_handshake #(.EXTERNAL_ACK(1), .WIDTH(48)) cdc (
    .clk_src(clk), .rst_src(rst), .data_in(send_d), .send(send), .busy(busy),
    .clk_dst(mclk), .rst_dst(mrst), .data_out(m_d), .valid(m_v), .ack_in(m_ack));

reg  [47:0] q [0:7];
reg  [2:0]  qw = 3'd0, qr = 3'd0;
reg  [3:0]  ql = 4'd0;
reg         primed = 1'b0;
reg  signed [23:0] o_l = 24'sd0, o_r = 24'sd0;
// DC blocker: y = x - x1 + y1 - y1/4096 (corner ~1.9 Hz: -0.04 dB at 20 Hz)
(* use_dsp = "yes" *) reg  signed [25:0] y_l = 26'sd0;
(* use_dsp = "yes" *) reg  signed [25:0] y_r = 26'sd0;
reg  signed [23:0] x1_l = 24'sd0, x1_r = 24'sd0;
wire signed [25:0] yn_l = o_l - x1_l + y_l - (y_l >>> 12);
wire signed [25:0] yn_r = o_r - x1_r + y_r - (y_r >>> 12);
always @(posedge mclk) begin
    m_ack <= 1'b0;
    if (m_v && !m_ack) begin
        m_ack <= 1'b1;
        if (ql != 4'd8) begin
            q[qw] <= m_d;
            qw <= qw + 3'd1;
        end
    end
    if (sample_en) begin
        if (primed && ql != 4'd0) begin
            o_l <= q[qr][47:24];
            o_r <= q[qr][23:0];
            qr <= qr + 3'd1;
        end else begin
            o_l <= o_l - (o_l >>> 6);
            o_r <= o_r - (o_r >>> 6);
        end
        if (ql == 4'd0) primed <= 1'b0;
        else if (ql >= 4'd3) primed <= 1'b1;
        x1_l <= o_l;
        x1_r <= o_r;
        y_l <= yn_l;
        y_r <= yn_r;
        out_l <= sat24(yn_l);
        out_r <= sat24(yn_r);
    end
    ql <= ql + ((m_v && !m_ack && ql != 4'd8) ? 4'd1 : 4'd0) - ((sample_en && primed && ql != 4'd0) ? 4'd1 : 4'd0);
    if (mrst) begin
        qw <= 3'd0; qr <= 3'd0; ql <= 4'd0; primed <= 1'b0;
    end
end

endmodule

// ---------------------------------------------------------------------------
// trx_hb_c64u: halfband decimator by 2, two channels, one multiplier, one read of
// the delay line per clock. NT = 4m+3 taps, centre c = (NT-1)/2 = 1/2,
// nonzero taps h[2m] (m < (c+1)/2), symmetric:
//   y = x[n-c]/2 + sum_m h[2m] (x[n-2m] + x[n-NT+1+2m])
// MEM holds h[2m] x 2^17. One output per two inputs, 2 x (2 NU + 1) clocks.
// ---------------------------------------------------------------------------
module trx_hb_c64u #(
    parameter NT = 11,
    parameter MEM = ""
) (
    input  wire        clk,
    input  wire        rst,
    input  wire        in_valid,
    input  wire signed [23:0] in_l,
    input  wire signed [23:0] in_r,
    output reg         out_valid = 1'b0,
    output reg  signed [23:0] out_l,
    output reg  signed [23:0] out_r
);
localparam C = (NT - 1) / 2;
localparam NU = (C + 1) / 2;
reg signed [17:0] h [0:NU-1];
initial $readmemh(MEM, h);
reg  [23:0] xl [0:63], xr [0:63];
reg  [5:0]  w = 6'd0;
reg         ph = 1'b0;
reg         run = 1'b0;
reg  [5:0]  m;                              // pair 0..NU-1, NU = centre
reg  [1:0]  ph4;                            // per pair: read a, read b, multiply, add
reg         ch;
reg  [5:0]  n;                              // position of x[n]
wire [5:0]  ra = m == NU ? n - C : ph4 == 2'd1 ? n - (NT - 1) + {m[4:0], 1'b0} : n - {m[4:0], 1'b0};
wire signed [23:0] xv = ch ? xr[ra] : xl[ra];
reg  signed [23:0] xa, xb;
reg  signed [42:0] prod;
(* use_dsp = "yes" *) reg  signed [47:0] acc;
function signed [23:0] sat(input signed [47:0] v);
    sat = v > 48'sh7fffff ? 24'sh7fffff : v < -48'sh800000 ? -24'sh800000 : v[23:0];
endfunction
wire signed [47:0] fin = acc + (xa <<< 16);  // centre: x[n-c]/2 at the 2^17 scale
always @(posedge clk) begin
    out_valid <= 1'b0;
    if (in_valid) begin
        xl[w] <= in_l;
        xr[w] <= in_r;
        w <= w + 6'd1;
        ph <= !ph;
        if (ph) begin
            run <= 1'b1;
            m <= 6'd0;
            ph4 <= 2'd0;
            ch <= 1'b0;
            n <= w;
            acc <= 48'sd0;
        end
    end else if (run) begin
        ph4 <= ph4 + 2'd1;
        case (ph4)
        2'd0: xa <= xv;
        2'd1: begin
            if (m == NU) begin
                // xa holds x[n-c]
                if (ch) begin out_r <= sat(fin >>> 17); out_valid <= 1'b1; run <= 1'b0; end
                else out_l <= sat(fin >>> 17);
                ch <= !ch;
                m <= 6'd0;
                ph4 <= 2'd0;
                acc <= 48'sd0;
            end else
                xb <= xv;
        end
        2'd2: prod <= (xa + xb) * h[m];
        default: begin
            acc <= acc + prod;
            m <= m + 6'd1;
        end
        endcase
    end
    if (rst) begin run <= 1'b0; ph <= 1'b0; end
end
endmodule
