// T142 D7 / P6: the S-DSP's stereo sound (AUDIO_L/R, 16 bit signed, one new pair every 1/32 kHz of machine time) into the host's audio frames: one 24 bit pair
// for every audio_sample_en of the host (a frame of its I2S output, 48 kHz, in mclk).
// The first version held the newest S-DSP pair for an output clock of its own (2112 clocks, 47.35 kHz): the 32 kHz pairs were repeated one or two times in an
// uneven pattern (the bench: -23 dB error against the ideal resampling for a 1 kHz tone), and the host, whose frame is 48.003 kHz, repeated one output sample in
// about 70 (a click every 1.5 ms). Now the host asks (audio_sample_en -> a toggle into the machine clock) and the answer is the cubic (Catmull-Rom) interpolation
// between the two S-DSP pairs that surround a point two pairs back in time, at the fraction that has passed since the newest pair arrived, counted in machine
// ticks (`ce`: a stalled or paused machine does not age it). The pairs are detected by their change (an unchanged pair is the same value either way).
// The answer is taken only when the pipeline has settled after a new pair (no mixed state). The S-DSP's output is centred, so there is no DC tracker.
// The pause and the reset mute: the next samples are exactly 0.
// Written for this project (TREX), no upstream code; the request/answer toggles are those of the other cores' stream ports.
`timescale 1ns / 1ps
module snes_cubic (
    input  wire               clk,
    input  wire signed [15:0] p0, p1, p2, p3,   // four neighbours; the result lies between p1 and p2
    input  wire        [15:0] x,                // 0 .. 65535 = p1 .. p2
    output reg  signed [17:0] y = 18'sd0        // 9 clocks behind the inputs
);
    // The inputs are registered here first and b's four terms are summed over two clocks (run 1093: WNS -0.019 ns, r2_reg -> m2, 14 levels).
    reg signed [15:0] p0r = 16'sd0, p1r = 16'sd0, p2r = 16'sd0, p3r = 16'sd0;
    reg        [15:0] xr = 16'd0;
    // y = p1 + x/2 * ((p2 - p0) + x * ((2 p0 - 5 p1 + 4 p2 - p3) + x * (3 (p1 - p2) + p3 - p0)))
    wire signed [19:0] a_w = 3 * ($signed({{4{p1r[15]}}, p1r}) - $signed({{4{p2r[15]}}, p2r})) + $signed({{4{p3r[15]}}, p3r}) - $signed({{4{p0r[15]}}, p0r});
    wire signed [19:0] bp_w = 2 * $signed({{4{p0r[15]}}, p0r}) - 5 * $signed({{4{p1r[15]}}, p1r});
    wire signed [19:0] bq_w = 4 * $signed({{4{p2r[15]}}, p2r}) - $signed({{4{p3r[15]}}, p3r});
    wire signed [19:0] c_w = $signed({{4{p2r[15]}}, p2r}) - $signed({{4{p0r[15]}}, p0r});
    reg signed [19:0] bp1, bq1;
    reg signed [19:0] a1, c1, b2, c2, u3, c3, c4, v5, t1, t2;
    reg signed [15:0] q1, q2, q3, q4, q5, q6;
    reg        [15:0] x1, x2, x3, x4, x5, x6;
    reg signed [19:0] t3;
    wire signed [36:0] m1 = a1 * $signed({1'b0, x1});
    wire signed [36:0] m2 = u3 * $signed({1'b0, x3});
    wire signed [36:0] m3 = v5 * $signed({1'b0, x5});
    always @(posedge clk) begin
        p0r <= p0; p1r <= p1; p2r <= p2; p3r <= p3; xr <= x;
        a1 <= a_w; bp1 <= bp_w; bq1 <= bq_w; c1 <= c_w; q1 <= p1r; x1 <= xr;
        t1 <= m1 >>> 16; b2 <= bp1 + bq1; c2 <= c1; q2 <= q1; x2 <= x1;
        u3 <= b2 + t1; c3 <= c2; q3 <= q2; x3 <= x2;
        t2 <= m2 >>> 16; c4 <= c3; q4 <= q3; x4 <= x3;
        v5 <= c4 + t2; q5 <= q4; x5 <= x4;
        t3 <= m3 >>> 16; q6 <= q5; x6 <= x5;
        y <= $signed({{2{q6[15]}}, q6}) + (t3 >>> 1);
    end
endmodule

module snes_audio #(
    parameter integer TICKS = 670                 // machine ticks (21.477 MHz) between two S-DSP pairs: 669.7 measured in the bench (21,477,272 / 32,070)
) (
    input  wire               clk,           // the machine clock (cpu_clk)
    input  wire signed [15:0] l_in,
    input  wire signed [15:0] r_in,
    input  wire               cap,           // the clock before a machine tick: the machine's outputs are settled then
    input  wire               ce,            // a machine tick
    input  wire               mute,
    input  wire               mclk,
    input  wire               asen,          // mclk: the host wants the next pair (one mclk, once per frame)
    output reg  signed [23:0] audio_l = 24'sd0,
    output reg  signed [23:0] audio_r = 24'sd0,
    input  wire               pk_clr,        // one clk: the two peaks start again (a write to the register block's PEAK)
    output reg         [15:0] pk_dsp = 16'd0, // largest |sample| of the S-DSP's own pairs (l_in / r_in) since pk_clr, 0 .. 32768
    output reg         [15:0] pk_out = 16'd0, // largest |sample| handed to the host's audio path (the cubic's result, before x 128 and the clock crossing)
    output reg         [15:0] hands = 16'd0   // answers handed over (once per host audio frame that was taken), wraps
);
    localparam integer KM = (65536 + TICKS - 1) / TICKS;       // fraction (16 bit) per tick, rounded up: the last tick before the next pair reaches 1
    reg signed [15:0] l0 = 16'sd0, l1 = 16'sd0, l2 = 16'sd0, l3 = 16'sd0, r0 = 16'sd0, r1 = 16'sd0, r2 = 16'sd0, r3 = 16'sd0;   // l3 the newest
    reg signed [15:0] lp = 16'sd0, rp = 16'sd0;                                                                                  // the pair at the last cap
    reg        [10:0] age = 11'd0;
    reg         [4:0] settle = 5'd31;                  // clocks since the last new pair
    reg               pk_v = 1'b0, pk_v2 = 1'b0;     // a pair was handed over one / two clocks ago (the peak's pipeline)
    reg        [15:0] pk_al = 16'd0, pk_ar = 16'd0;
    // The peaks tell the S-DSP (the core) from everything behind snes_audio (the host's I2S path): the application reads them (SNES_PEAK_DSP / _OUT / _HANDS).
    function [15:0] absv(input signed [15:0] v);
        absv = v[15] ? (~v + 16'd1) : v;                           // -32768 gives 32768
    endfunction
    function [15:0] maxv(input [15:0] a, input [15:0] b);
        maxv = (a > b) ? a : b;
    endfunction
    always @(posedge clk) begin
        if (ce && age != 11'd2047) age <= age + 11'd1;
        if (settle != 5'd31) settle <= settle + 5'd1;
        if (cap) begin
            lp <= l_in; rp <= r_in;
            pk_dsp <= maxv(pk_dsp, maxv(absv(l_in), absv(r_in)));
            if (l_in != lp || r_in != rp) begin
                l0 <= l1; l1 <= l2; l2 <= l3; l3 <= l_in;
                r0 <= r1; r1 <= r2; r2 <= r3; r3 <= r_in;
                age <= 11'd0;
                settle <= 5'd0;
            end
        end
        if (pk_clr) pk_dsp <= 16'd0;
    end
    wire [26:0] fr_w = age * KM;
    wire [15:0] fr = (fr_w > 27'd65535) ? 16'hFFFF : fr_w[15:0];
    wire signed [17:0] yl, yr;
    snes_cubic cl (.clk(clk), .p0(l0), .p1(l1), .p2(l2), .p3(l3), .x(fr), .y(yl));
    snes_cubic cr (.clk(clk), .p0(r0), .p1(r1), .p2(r2), .p3(r3), .x(fr), .y(yr));
    function signed [15:0] clamp16(input signed [17:0] v);
        clamp16 = (v > 18'sd32767) ? 16'sd32767 : (v < -18'sd32768) ? -16'sd32768 : v[15:0];
    endfunction
    // the request: mclk -> clk, the answer: clk -> mclk (both toggles), the value is held between
    reg rq = 1'b0, ak = 1'b0;
    always @(posedge mclk) if (asen) rq <= ~rq;
    (* ASYNC_REG = "TRUE" *) reg [2:0] rq_s = 3'b000;
    reg pend = 1'b0;
    reg signed [23:0] hl = 24'sd0, hr = 24'sd0;
    wire signed [15:0] ol16 = clamp16(yl), or16 = clamp16(yr);
    always @(posedge clk) begin
        rq_s <= {rq_s[1:0], rq};
        if (rq_s[2] != rq_s[1]) pend <= 1'b1;
        if (pend && settle >= 5'd12 && !(rq_s[2] != rq_s[1])) begin       // the pipeline (9 clocks) has seen no new pair for 12 clocks
            pend <= 1'b0;
            hands <= hands + 16'd1;
            pk_v <= 1'b1;                                            // the peak follows from the registered pair, two clocks later (run 1091: -0.141 ns from cl/y into pk_out)
            hl <= mute ? 24'sd0 : {ol16[15], ol16, 7'd0};            // x 128: a full 16 bit swing is +-2^22 of the 24 bit range
            hr <= mute ? 24'sd0 : {or16[15], or16, 7'd0};
            ak <= ~ak;
        end
        // the hand-over peak in two stages behind hl/hr (already 0 when muted): |sample| into pk_al/pk_ar, then the maximum
        pk_v2 <= pk_v;
        if (pk_v) begin
            pk_v  <= 1'b0;
            pk_al <= absv(hl[22:7]);
            pk_ar <= absv(hr[22:7]);
        end
        if (pk_v2) pk_out <= maxv(pk_out, maxv(pk_al, pk_ar));
        if (pk_clr) pk_out <= 16'd0;
    end
    (* ASYNC_REG = "TRUE" *) reg [2:0] ak_s = 3'b000;
    always @(posedge mclk) begin
        ak_s <= {ak_s[1:0], ak};
        if (ak_s[2] != ak_s[1]) begin audio_l <= hl; audio_r <= hr; end
    end
endmodule
