// SPDX-License-Identifier: GPL-3.0-or-later
// ---------------------------------------------------------------------------
// trx_sampler_c64u: the Ultimate Audio sampler (TRX64-Ultimate/docs/specs/T25.6-sampler.md), 8 voices on one time-multiplexed datapath.
// Written from the app's side of the interface and the players that drive it (spec 1); no HDL of others.
//
// clk2x: 64 clocks per C64 cycle form one frame (free running; a C64 cycle sets `adv`, so the voices only advance
// while the C64 runs). Voice state is a 64 x 32 bit distributed RAM with four byte lanes: 8 words per voice,
//   word 0 CONTROL/VOLUME/PAN   1 START   2 LENGTH   3 RATE   4 REPEAT_A   5 REPEAT_B   (the app's bytes, big endian
//   inside a word: byte +0 of the word is bits 7:0; the bus never writes words 6 and 7)
//   word 6: ticks to the next sample (signed, x 256, less 1) and the low byte of the current sample
//   word 7: rel (byte offset of the next sample from START) and the high byte of the current sample
// Sub-cycle blend: the counter holds the step instant to 1/256 of a 6.25 MHz tick, but the output changes only once per
// frame (one C64 cycle, tinc/256 = 6.34 ticks). The weight of the step cycle that the new sample owns, w = 0..128
// (= the share of the cycle after the step instant), travels with the fetch (sv_w, f_w); when the sample lands the
// old one and w go to sold_m / sw_m and the next visit puts out old + (new - old) * w / 128, the time average of the
// cycle. w = 128 (no blend) is the idle value of sw_m.
// run / chg / irq_pend are 8 FFs each. Schedule (t = 0..63):
//   0..39   per voice 5 clocks: read counter, read rate, write counter, read the sample's high byte, read word 0;
//           the multiplier follows each visit: blend, volume, left, right (the DSP is busy 4 clocks of 5)
//   40 41   the pending register write of the C64, of the app;  42 43 the sample back from DDR2
//   44..51  the handler (a started or stopped voice, or one due step);  48 the sums go out;  60 61 writes again
// ---------------------------------------------------------------------------
`timescale 1 ns / 1 ps
module trx_sampler_c64u (
    input  wire        clk,                 // C64 clock (sys_clk): the slot side
    input  wire        clk2x,               // 2 x clk, same MMCM
    input  wire        rst,                 // clk
    input  wire  [1:0] vmode,               // System Mode (clk, quasi static)
    input  wire        c64_en,              // C64_SAMPLER_ENABLE (any clock, quasi static)

    // the C64 bus (clk), as the UCI sees it (c64_core_c64u.v)
    input  wire [15:0] bus_addr,
    input  wire        io_ev,               // one pulse per executed C64 cycle
    input  wire [15:0] io_ev_addr,
    input  wire  [7:0] io_ev_data,
    input  wire        io_ev_we,
    input  wire        io_ev_io,            // a real access to $DE00-$DFFF with I/O mapped
    output reg         oe = 1'b0,
    output reg   [7:0] dout = 8'd0,
    output reg         irq = 1'b0,

    // the app (trx_io.v page 8): toggle handshake, address and data held until the answer toggles back
    input  wire        a_tgl,
    input  wire        a_we,
    input  wire  [7:0] a_addr,
    input  wire  [7:0] a_wdata,
    output reg         a_rs_tgl = 1'b0,
    output reg   [7:0] a_rdata = 8'd0,

    // DDR2 read (top.v, REU's port): addr = byte address, data = the 32 bit word at addr & ~3
    output reg         f_req_tgl = 1'b0,
    output reg  [26:0] f_addr = 27'd0,
    input  wire        f_done_tgl,
    input  wire [31:0] f_data,

    // mixer inputs (clk2x), signed 23 bit, held for a frame
    output reg  signed [22:0] smp_l = 23'sd0,       // 8 voices x 2^19 at most; the mixer saturates
    output reg  signed [22:0] smp_r = 23'sd0
);

localparam [7:0] VERSION = 8'h01;

// ---- clk side: the C64's window $DF20-$DFFF, voice = (A7:5) - 1
(* ASYNC_REG = "TRUE" *) reg [1:0] en_s = 2'b00;
always @(posedge clk) en_s <= {en_s[0], c64_en};
wire en = en_s[1];
wire        rd_win = bus_addr[15:8] == 8'hDF && bus_addr[7:5] != 3'd0;
wire  [2:0] rd_v   = bus_addr[7:5] - 3'd1;
reg   [7:0] irq_pend = 8'd0;                // clk2x register, read here (a few ns of mux, a half-rate path)
always @(posedge clk) begin
    oe   <= en && rd_win;
    dout <= bus_addr[4:0] == 5'd0 ? {7'd0, irq_pend[rd_v]} : bus_addr[4:0] == 5'd1 ? VERSION : 8'd0;
    irq  <= en && (irq_pend != 8'd0);
end

reg         tk_tgl = 1'b0, c_tgl = 1'b0;
reg   [7:0] c_addr = 8'd0, c_data = 8'd0;
always @(posedge clk) begin
    if (io_ev) tk_tgl <= ~tk_tgl;
    if (io_ev && io_ev_io && io_ev_we && en && io_ev_addr[15:8] == 8'hDF && io_ev_addr[7:5] != 3'd0) begin
        c_addr <= {io_ev_addr[7:5] - 3'd1, io_ev_addr[4:0]};
        c_data <= io_ev_data;
        c_tgl  <= ~c_tgl;
    end
end

// ---- clk2x side
(* ASYNC_REG = "TRUE" *) reg [2:0] r_s = 3'b111, tk_s = 3'b000, c_s = 3'b000, a_s = 3'b000, d_s = 3'b000;
(* ASYNC_REG = "TRUE" *) reg [1:0] vm1 = 2'd0, vm = 2'd0;
always @(posedge clk2x) begin
    r_s  <= {r_s[1:0], rst};
    tk_s <= {tk_s[1:0], tk_tgl};
    c_s  <= {c_s[1:0], c_tgl};
    a_s  <= {a_s[1:0], a_tgl};
    d_s  <= {d_s[1:0], f_done_tgl};
    vm1  <= vmode; vm <= vm1;
end
wire r = r_s[2];
wire tk_ev = tk_s[2] != tk_s[1];
wire c_ev  = c_s[2] != c_s[1];
wire a_ev  = a_s[2] != a_s[1];
wire d_ev  = d_s[2] != d_s[1];

// 6.25 MHz ticks per C64 cycle x 256, by System Mode (the ratios of trx_audio_c64u)
reg [10:0] tinc;
always @(posedge clk2x)
    case (vm)
        2'd1:    tinc <= 11'd1565;
        2'd2:    tinc <= 11'd1563;
        2'd3:    tinc <= 11'd1621;
        default: tinc <= 11'd1624;
    endcase

// ---- the frame
reg  [5:0] t = 6'd0;
reg  [2:0] vv = 3'd0;
reg  [2:0] vj = 3'd0;
reg  [2:0] rot = 3'd0;                      // the voice the frame starts with: the one step per frame goes round
reg        adv = 1'b0, tk_p = 1'b0;
wire       vis = t < 6'd40;
always @(posedge clk2x) begin
    t <= t + 6'd1;
    if (t == 6'd63) begin
        adv  <= tk_p || tk_ev;
        tk_p <= 1'b0;
        vv <= rot; vj <= 3'd0;
        rot <= rot + 3'd1;
    end else begin
        if (tk_ev) tk_p <= 1'b1;
        if (vj == 3'd4) begin vj <= 3'd0; vv <= vv + 3'd1; end
        else vj <= vj + 3'd1;
    end
end

// ---- state RAM
reg  [7:0] m0 [0:63], m1 [0:63], m2 [0:63], m3 [0:63];
integer    ii;
initial for (ii = 0; ii < 64; ii = ii + 1) begin m0[ii] = 8'd0; m1[ii] = 8'd0; m2[ii] = 8'd0; m3[ii] = 8'd0; end
reg  [5:0] ra;
reg  [3:0] we;
reg [31:0] wd;
wire [31:0] rd = {m3[ra], m2[ra], m1[ra], m0[ra]};
always @(posedge clk2x) begin
    if (we[0]) m0[ra] <= wd[7:0];
    if (we[1]) m1[ra] <= wd[15:8];
    if (we[2]) m2[ra] <= wd[23:16];
    if (we[3]) m3[ra] <= wd[31:24];
end

// ---- control state
reg  [7:0] run = 8'd0, chg = 8'd0;
reg        c_pend = 1'b0, a_pend = 1'b0;
reg  [7:0] c_a = 8'd0, c_d = 8'd0, a_a = 8'd0, a_d = 8'd0;
reg        a_w = 1'b0;
reg        sv_valid = 1'b0, fbusy = 1'b0, fret = 1'b0;
reg        fq = 1'b0;                       // fret as seen at slot 42: slot 43 writes the high byte of the same sample
reg        fkill = 1'b0;                    // the voice was restarted with a fetch in flight: drop that sample
reg  [2:0] sv = 3'd0, fv = 3'd0;
reg        f_b16 = 1'b0;
reg  [1:0] f_a10 = 2'd0;
reg [31:0] f_d = 32'd0;

// visit: j0 read counter, j1 read rate, j2 decide and write the counter, j3 read word 0, j4 read the sample's high byte
// E = ticks x 256 to the next sample minus 1 (signed 24 bit): every frame E - T, when that is negative a step is
// due and E + rate (so that the test is the sign bit)
reg  [23:0] c0;
reg   [7:0] slo;
reg  [14:0] rate_r;
(* use_dsp = "yes" *) reg  signed [24:0] y0;
reg   [5:0] vol;
reg   [3:0] pan;
(* use_dsp = "yes" *) reg  [15:0] s16;
(* use_dsp = "yes" *) reg signed [16:0] dlt;                      // new - old sample of the voice being visited
reg  [15:0] sold_r;
reg   [7:0] w_r;
reg   [7:0] sv_w = 8'd128, f_w = 8'd128;
reg   [7:0] lo_old;
reg  [15:0] sold_m [0:7];
reg   [7:0] sw_m   [0:7];
integer     jj;
initial for (jj = 0; jj < 8; jj = jj + 1) begin sold_m[jj] = 16'd0; sw_m[jj] = 8'd128; end
wire [15:0] rate  = {rd[23:16], rd[31:24]};
wire [14:0] ratee = rate[15] ? 15'h7fff : rate[14:3] == 12'd0 && rate[2:0] < 3'd7 ? 15'd7 : rate[14:0];
wire signed [24:0] y1 = y0 + {2'b00, rate_r, 8'd0};
wire        due   = adv && run[vv] && y0[24];
wire        take  = due && !sv_valid && !chg[vv];
// w = ny / tinc * 128 with ny = -y0 - 1 = the 1/256 ticks of the cycle left after the step instant; ny >= tinc (a step
// that is late already, or the first sample at once) owns the whole cycle. 1/tinc*128 as three shifts (+0.3 %).
wire [24:0] ny    = ~y0;
wire        w_all = ny >= {14'd0, tinc};
wire [13:0] ny8   = {ny[10:0], 3'd0};
wire [13:0] w8    = (ny8 >> 4) + (ny8 >> 6) + ((vm == 2'd1 || vm == 2'd2) ? (ny8 >> 8) : (ny8 >> 10)) + 14'd4;
wire  [7:0] w_c   = w_all ? 8'd128 : w8[13:3] > 11'd128 ? 8'd128 : w8[10:3];

// ---- the register writes (slots 40, 41, 60, 61)
wire        slot_c = (t == 6'd40 || t == 6'd60) && c_pend;
wire        slot_a = (t == 6'd41 || t == 6'd61) && a_pend;
wire  [7:0] op_a   = slot_c ? c_a : a_a;
wire  [7:0] op_d   = slot_c ? c_d : a_d;
wire        op_w   = slot_c || a_w;
wire        op     = slot_c || slot_a;
wire  [2:0] op_v   = op_a[7:5];
wire  [2:0] op_wd  = op_a[4:2];
wire  [1:0] op_ln  = op_a[1:0];
wire        op_ram = op && op_w && op_wd <= 3'd5;
wire        op_ctl = op_ram && op_wd == 3'd0 && op_ln == 2'd0;

// ---- the handler
reg   [1:0] hm = 2'd0;                      // 0 idle, 1 start / stop, 2 step
reg   [2:0] hv = 3'd0;
wire  [5:0] hs = t - 6'd44;
reg         en_h = 1'b0, rep_h = 1'b0, irq_h = 1'b0, b16_h = 1'b0, e_len = 1'b0, e_b = 1'b0;
(* use_dsp = "yes" *) reg  [23:0] rel_h;
// the big endian 24 bit value of bytes +1..+3 of a word, compared with rel (bit 0 dropped for 16 bit samples)
wire [23:0] rd24   = {rd[15:8], rd[23:16], rd[31:24]};
wire        e_rd   = {rel_h[23:1], rel_h[0] & ~b16_h} == {rd24[23:1], rd24[0] & ~b16_h};
wire        wrap_c = rep_h && (e_b || e_len);
wire        stop_c = !rep_h && e_len;
wire [23:0] rel_n  = rel_h + (b16_h ? 24'd2 : 24'd1);
wire [26:0] fa     = {rd[2:0], rd24} + {3'd0, rel_h};
wire [15:0] smp_in = !run[fv] ? 16'd0 :
                     f_b16 ? (f_a10[1] ? f_d[31:16] : f_d[15:0]) : {f_d[8 * f_a10 +: 8], 8'h00};
wire [7:0]  chg_lo_m = chg & (~chg + 8'd1);   // lowest set bit
wire [2:0]  chg_lo   = chg_lo_m[1] ? 3'd1 : chg_lo_m[2] ? 3'd2 : chg_lo_m[3] ? 3'd3 : chg_lo_m[4] ? 3'd4 :
                       chg_lo_m[5] ? 3'd5 : chg_lo_m[6] ? 3'd6 : chg_lo_m[7] ? 3'd7 : 3'd0;

always @* begin
    ra = 6'd0; we = 4'b0000; wd = 32'd0;
    if (vis) begin
        case (vj)
            3'd0: ra = {vv, 3'd6};
            3'd1: ra = {vv, 3'd3};
            3'd2: begin ra = {vv, 3'd6}; we = 4'b0111; wd = {8'd0, take ? y1[23:0] : y0[23:0]}; end
            3'd3: ra = {vv, 3'd7};
            default: ra = {vv, 3'd0};
        endcase
    end else if (op) begin
        if (op_ram) begin
            ra = {op_v, op_wd};
            we = 4'b0001 << op_ln;
            wd = {4{op_d}};
        end
    end else if (t == 6'd42) begin
        ra = {fv, 3'd6};
        if (fret && !fkill) begin we = 4'b1000; wd = {smp_in[7:0], 24'd0}; end
    end else if (t == 6'd43) begin
        ra = {fv, 3'd7};
        if (fq && !fkill) begin we = 4'b1000; wd = {smp_in[15:8], 24'd0}; end
    end else if (hm == 2'd1) begin
        case (hs)
            6'd0: ra = {hv, 3'd0};
            6'd1: begin ra = {hv, 3'd6}; we = 4'b1111; wd = 32'h00FF_FFFF; end   // E = -1: the first sample at once
            6'd2: begin ra = {hv, 3'd7}; we = 4'b1111; end
            default: ;
        endcase
    end else if (hm == 2'd2) begin
        case (hs)
            6'd0: ra = {hv, 3'd0};
            6'd1: ra = {hv, 3'd7};
            6'd2: ra = {hv, 3'd2};
            6'd3: ra = {hv, 3'd5};
            6'd4: ra = {hv, 3'd4};
            6'd5: ra = {hv, 3'd1};
            6'd6: if (stop_c) begin ra = {hv, 3'd7}; we = 4'b1000; end
                  else begin ra = {hv, 3'd7}; we = 4'b0111; wd = {8'd0, rel_n}; end
            6'd7: if (stop_c) begin ra = {hv, 3'd6}; we = 4'b1000; end
            default: ;
        endcase
    end
end

// ---- multiplier: sample x volume, x left factor, x right factor; one DSP
// Pan law as measured on Gideon's core (T25.6, 2026-10-08): nibble 0..7 the L lane grows 0..full in 7 steps while R stays
// full, 8..15 mirrored (L full, R falls); full = 166/2048 of sample x volume, which puts the centre at Gideon's level.
reg   [6:0] mp = 7'd0;
reg  signed [25:0] bm;
reg  signed [21:0] m1r;
reg  signed [20:0] mlr, mrr;
function [7:0] pan_t(input [2:0] n);
    case (n)
        3'd0: pan_t = 8'd0;   3'd1: pan_t = 8'd24;  3'd2: pan_t = 8'd47;  3'd3: pan_t = 8'd71;
        3'd4: pan_t = 8'd95;  3'd5: pan_t = 8'd119; 3'd6: pan_t = 8'd142; default: pan_t = 8'd166;
    endcase
endfunction
wire        [7:0]  pan_l = pan[3] ? 8'd166 : pan_t(pan[2:0]);
wire        [7:0]  pan_r = pan[3] ? pan_t(~pan[2:0]) : 8'd166;
wire signed [21:0] ma   = mp[0] ? {{5{dlt[16]}}, dlt} : mp[2] ? {{6{s16[15]}}, s16} : m1r;
wire signed  [8:0] mb   = mp[0] ? {1'b0, w_r} : mp[2] ? {3'd0, vol} : mp[3] ? {1'b0, pan_l} : {1'b0, pan_r};
wire signed [30:0] pc   = ma * mb;
// y0, s16, dlt, rel_h and the accumulators are adders that sit in DSP48s (LUT budget, T103)
(* use_dsp = "yes" *) reg  signed [22:0] acc_l = 23'sd0;
(* use_dsp = "yes" *) reg  signed [22:0] acc_r = 23'sd0;
wire signed [22:0] acc_a = mp[5] ? acc_l : acc_r;
wire signed [22:0] acc_s = acc_a + (mp[5] ? {{2{mlr[20]}}, mlr} : {{2{mrr[20]}}, mrr});

always @(posedge clk2x) begin
    // visit
    if (vis) begin
        case (vj)
            3'd0: begin c0 <= rd[23:0]; slo <= rd[31:24]; end
            3'd1: begin
                rate_r <= ratee;
                y0 <= $signed({c0[23], c0}) - $signed({14'd0, adv ? tinc : 11'd0});
            end
            3'd2: if (take) begin sv_valid <= 1'b1; sv <= vv; sv_w <= w_c; end
            3'd3: begin
                dlt    <= $signed({rd[31], rd[31:24], slo}) - $signed({sold_m[vv][15], sold_m[vv]});
                sold_r <= sold_m[vv];
                w_r    <= sw_m[vv];
            end
            default: begin vol <= rd[13:8]; pan <= rd[19:16]; end
        endcase
    end
    mp <= {mp[5:0], vis && vj == 3'd3};
    if (mp[0]) bm <= pc[25:0];
    if (mp[1]) s16 <= sold_r + bm[22:7];
    if (mp[2]) m1r <= pc[21:0];
    if (mp[3]) mlr <= {pc[30], pc[30:11]};
    if (mp[4]) mrr <= {pc[30], pc[30:11]};
    if (mp[5]) acc_l <= acc_s;
    if (mp[6]) acc_r <= acc_s;
    if (t == 6'd48) begin
        smp_l <= acc_l;
        smp_r <= acc_r;
        acc_l <= 23'sd0;
        acc_r <= 23'sd0;
    end

    // events from the other clocks
    if (c_ev) begin c_pend <= 1'b1; c_a <= c_addr; c_d <= c_data; end
    if (a_ev) begin a_pend <= 1'b1; a_a <= a_addr; a_d <= a_wdata; a_w <= a_we; end
    if (d_ev) begin fret <= 1'b1; f_d <= f_data; end

    // register write slots
    if (op) begin
        if (slot_c) c_pend <= 1'b0; else begin a_pend <= 1'b0; a_rs_tgl <= ~a_rs_tgl; end
        if (!slot_c && !a_w)
            a_rdata <= (op_wd == 3'd0 && op_ln == 2'd0) ? {7'd0, irq_pend[op_v]} :
                       (op_wd == 3'd0 && op_ln == 2'd1) ? VERSION : 8'd0;
        if (op_w && op_wd == 3'd7 && op_ln == 2'd3) irq_pend[op_v] <= 1'b0;
        // rd = the old word: a write that keeps the enable bit changes repeat / IRQ only
        if (op_ctl && (!op_d[0] || !rd[0])) begin
            chg[op_v] <= 1'b1;
            run[op_v] <= 1'b0;
            if (fbusy && fv == op_v) fkill <= 1'b1;
        end
    end

    // the sample back from DDR2
    if (t == 6'd42) begin fq <= fret; lo_old <= rd[31:24]; end
    if (vis && vj == 3'd3 && sw_m[vv] != 8'd128) sw_m[vv] <= 8'd128;
    if (t == 6'd43 && fq && !fkill) begin sold_m[fv] <= {rd[31:24], lo_old}; sw_m[fv] <= f_w; end
    if (hm == 2'd1 && hs == 6'd2) sw_m[hv] <= 8'd128;
    if (t == 6'd43 && fq) begin
        fret  <= 1'b0;
        fq    <= 1'b0;
        fbusy <= 1'b0;
        fkill <= 1'b0;
    end

    // the handler
    if (t == 6'd43) begin
        if (chg != 8'd0) begin hm <= 2'd1; hv <= chg_lo; end
        else if (sv_valid && (!fbusy || fq)) begin hm <= 2'd2; hv <= sv; end
        else hm <= 2'd0;
    end
    if (hm == 2'd1) begin
        if (hs == 6'd0) en_h <= rd[0];
        if (hs == 6'd2) begin chg[hv] <= 1'b0; run[hv] <= en_h; hm <= 2'd0; end
    end
    if (hm == 2'd2) begin
        case (hs)
            6'd0: begin rep_h <= rd[1]; irq_h <= rd[2]; b16_h <= rd[4]; end
            6'd1: rel_h <= rd[23:0];
            6'd2: e_len <= e_rd;
            6'd3: e_b   <= rd24 != 24'd0 && e_rd;
            6'd4: if (wrap_c) rel_h <= rd24;
            6'd5: begin
                sv_valid <= 1'b0;
                if (!stop_c) begin
                    f_addr    <= fa;
                    f_req_tgl <= ~f_req_tgl;
                    fbusy     <= 1'b1;
                    fv        <= hv;
                    f_b16     <= b16_h;
                    f_w       <= sv_w;
                    f_a10     <= fa[1:0];
                    if (wrap_c && irq_h) irq_pend[hv] <= 1'b1;
                end else begin
                    run[hv] <= 1'b0;
                    if (irq_h) irq_pend[hv] <= 1'b1;
                end
            end
            6'd7: hm <= 2'd0;
            default: ;
        endcase
    end

    if (r) begin
        run <= 8'd0; chg <= 8'd0; irq_pend <= 8'd0;     // a_pend is never reset: the app waits for its answer
        sv_valid <= 1'b0; fbusy <= 1'b0; fret <= 1'b0; fq <= 1'b0; fkill <= 1'b0; hm <= 2'd0;
    end
end

endmodule
