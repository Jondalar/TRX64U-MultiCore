// T201: the diag core's analogue test picture - colour bars, a grey ramp and a moving marker as Y, C and CVBS words on the three 8 ball groups of the A/V port, with every
// value of the encoder a live register (diag_regs.v 050-05F, the bus av_cfg below). Own copy of the C128's cores/c128/mono/c128_avout.v (raster, sync and burst windows, levels,
// subcarrier constants; mono/host/FROM): the raster does not come from a VIC but runs by itself; the palette is a fixed table (the C64's, Colodore) instead of pal_ram.
//
// av_cfg (cpu_clk domain, two flip-flops per bit here, not a word handshake: a field the application changes live can be read half old, half new for one or two
// clocks, so a live change may disturb one frame; nothing latches the value):
//   [1:0] std    0 PAL 63 x 312, 1 NTSC 65 x 263, 2 PAL-60 65 x 263, 3 NTSC-50 63 x 312 (the C128's vmode; the clock sys_clk must be that System Mode's - the application sets both)
//   [4:2] perm   which word goes to which group (A, B, C): 0 Y C CV, 1 Y CV C, 2 C Y CV, 3 C CV Y, 4 CV Y C, 5 CV C Y (5 = the C128's)
//   [7:5] rev    bit n reverses the bit order of group n (A, B, C)
//   [8]   vinv   V sign inverted
//   [16:9] trim  signed, subcarrier fine trim: inc += trim x 8 (a unit is 8 x f_clk / 2^26 = 3.90 Hz at NTSC's 32.725694 MHz, 3.76 Hz at PAL's 31.527778 MHz; range about +-500 Hz)
//   [23:17] bs, [33:24] be   the burst window {start, end} in samples after the sync's trailing edge
//   [39:34] bamp burst amplitude (32 = the C128's), [44:40] gain chroma gain x 16, [52:45] synl sync tip level, [60:53] blank blanking level, [65:61] wamp white level x 16, [66] frz the marker stands still,
//   [67] eq equalizing pulses (0: the vsync is 'vlen' lines of broad pulses at the line rate; 1: pre-equalizing, serrated broad and post-equalizing blocks of 'vlen' lines each, pulses at the half-line rate),
//   [68] l262 262 lines per frame at the 60 Hz standards (0: 263), [73:69] vlen the block length in half lines (6 = 3 lines; 5 = 2.5; 12 = 6),
//   [81:74] vpos signed, the vsync's position in lines against the default (the broad pulses start at line 244 / 280 of the picture; with eq the pre-equalizing block starts 3 lines earlier, whatever vlen is)
//   T259 (zero in every field is the T201 behaviour):
//   [82] map    0 scan groups (the boundary scan's A, B, C of mono/top.v), 1 names (H1: a word per AVID_R, AVID_G, AVID_B on bits [9:2], MSB first; slot 0 = R, 1 = G, 2 = B, perm and rev act on these three)
//   [84:83] mode  0 the test picture (Y, C, CVBS words), 1 channel probe (one channel with sync, blanking and bars, the others at blanking), 2 RGB (the palette as R, G, B words, perm picks the channels)
//   [86:85] pch   the channel under test in the probe: slot 0, 1, 2 (R G B under names, A B C under scan groups)
//   [88:87] rch   RGB: 0 the palette picture, 1 / 2 / 3 only the R / G / B word lit as a ramp across the picture, the others 0
//   [90:89] synm  AVID_SYNCn: 0 high, 1 low, 2 csync (low in the sync pulses), 3 inverted csync (high in the sync pulses)
//   [92:91] fbm   AVID_FB: 0 low, 1 high, 2 the picture window (high in the picture), 3 inverted window
// Outputs: the three group words, SYNCn and FB, registered in the I/O flops. The group-to-ball map of the scan groups is cores/diag/mono/top.v; the names map is made here (the words
// of ga, gb, gc are arranged so that the same top.v wiring puts word bit n of a channel on its AVID_x[n + 2]).
module diag_avout (
    input  wire        clk,             // sys_clk
    input  wire [92:0] av_cfg,
    output wire [7:0]  ga, gb, gc,
    output wire        syn_n, fb
);
    // the start value is the register block's reset value (diag_regs.v): PAL, the C128's groups, V sign inverted, burst 28 .. 99, amplitude 32, gain 1.0, sync 0, blank 77, white 11, marker moving, vsync as the first version (broad pulses, 3 lines, 263 lines)
    localparam [92:0] CFG0 = {11'd0, 8'd0, 5'd6, 1'b0, 1'b0, 1'b0, 5'd11, 8'd77, 8'd0, 5'd16, 6'd32, 10'd99, 7'd28, 8'd0, 1'b1, 3'd0, 3'd5, 2'd0};
    (* ASYNC_REG = "TRUE" *) reg [92:0] cf1 = CFG0, cf2 = CFG0;
    always @(posedge clk) begin cf1 <= av_cfg; cf2 <= cf1; end
    wire [1:0]  std   = cf2[1:0];
    wire [2:0]  perm  = cf2[4:2];
    wire [2:0]  rev   = cf2[7:5];
    wire        vinv  = cf2[8];
    wire [7:0]  trim  = cf2[16:9];
    wire [16:0] burst = {cf2[23:17], cf2[33:24]};
    wire [5:0]  bamp  = cf2[39:34];
    wire [4:0]  gain  = cf2[44:40];
    wire [7:0]  synl  = cf2[52:45];
    wire [7:0]  blk   = cf2[60:53];
    wire [4:0]  wamp  = cf2[65:61];
    wire        frz   = cf2[66];
    wire        eq    = cf2[67];
    wire        l262  = cf2[68];
    wire [4:0]  vlen  = cf2[73:69];
    wire signed [7:0] vpos_k = cf2[81:74];
    wire        nmap  = cf2[82];
    wire [1:0]  mode  = cf2[84:83];
    wire [1:0]  pch   = (cf2[86:85] > 2'd2) ? 2'd2 : cf2[86:85];
    wire [1:0]  rch   = cf2[88:87];
    wire [1:0]  synm  = cf2[90:89];
    wire [1:0]  fbm   = cf2[92:91];

    wire v60 = (std == 2'd1) || (std == 2'd2);
    wire pal = (std == 2'd0) || (std == 2'd2);

    wire [9:0] HT = v60 ? 10'd520 : 10'd504;
    wire [8:0] LT = v60 ? (l262 ? 9'd262 : 9'd263) : 9'd312;
    wire [9:0] XS = v60 ? 10'd4   : 10'd5;
    wire [9:0] W  = v60 ? 10'd38  : 10'd37;
    wire [8:0] Y0 = v60 ? 9'd31   : 9'd16;
    wire [8:0] ACT = v60 ? 9'd240 : 9'd272;
    wire [8:0] VS = v60 ? 9'd244  : 9'd280;

    // ---- the raster: a dot is four clocks ------------------------------------------------------------------------------------
    reg  [9:0] hx = 10'd0;
    reg  [8:0] ly = 9'd0;
    reg  [1:0] dv = 2'd0;
    reg        frame_tick = 1'b0;
    always @(posedge clk) begin
        dv <= dv + 2'd1;
        frame_tick <= 1'b0;
        if (dv == 2'd3) begin
            if (hx >= HT - 10'd1) begin
                hx <= 10'd0;
                if (ly >= LT - 9'd1) begin ly <= 9'd0; frame_tick <= 1'b1; end
                else ly <= ly + 9'd1;
            end else hx <= hx + 10'd1;
        end
    end

    wire [9:0] sx   = (hx >= XS) ? hx - XS : hx + HT - XS;
    wire [8:0] vpos = (ly >= Y0) ? ly - Y0 : ly + LT - Y0;
    // ---- the sync: csync = the line's sync pulse, except in the vertical sync block. Its position is counted in half lines: slot = 2 x line + (second half of the line).
    //   eq = 0: lines [S, S + vlen/2) have a broad pulse (low for the line less the sync width, one serration at the line's end); an odd vlen ends with the first half of a line low.
    //   eq = 1: slots [S, S + vlen) narrow pulses (half the sync width) at the start of each half line, then vlen slots of broad pulses (low for the half line less the sync width,
    //           the serration is the sync width at its end), then vlen slots of narrow pulses. S = line 244 / 280 + vpos_k, with eq 3 lines earlier (so the broad block starts where it does without eq for vlen = 3 lines).
    localparam [9:0] HTH_60 = 10'd260, HTH_50 = 10'd252;
    wire [9:0] HTH = v60 ? HTH_60 : HTH_50;
    wire signed [10:0] s_line = $signed({2'b00, VS}) + {{3{vpos_k[7]}}, vpos_k} - (eq ? 11'sd3 : 11'sd0);
    wire signed [11:0] two_v  = $signed({2'b00, vpos, 1'b0});
    reg  signed [11:0] r0 = 12'sd0;                                     // the slot of the next line's first half, counted from S (negative: before the block)
    wire signed [11:0] d0 = two_v - $signed({s_line, 1'b0});
    // a block that runs past the frame's last line goes on in the next frame's first lines (slots before S count from the previous frame's S)
    always @(posedge clk) if (hx == XS - 10'd1) r0 <= (d0 < 12'sd0) ? d0 + $signed({2'b00, LT, 1'b0}) : d0;
    wire hs_w = (sx < W);
    wire        half = (sx >= HTH);
    wire [9:0]  hp   = half ? sx - HTH : sx;                            // position inside the half line
    wire signed [11:0] rel = r0 + (half ? 12'sd1 : 12'sd0);
    wire signed [11:0] nS  = $signed({7'd0, vlen});
    wire        inb    = (rel >= 12'sd0) && (rel < nS + nS + nS);
    wire        broad  = (rel >= nS) && (rel < nS + nS);
    wire        eq_cs  = inb ? (broad ? (hp < HTH - W) : (hp < (W >> 1))) : hs_w;
    wire        lin    = (r0 >= 12'sd0) && (r0 < nS);
    wire        part   = (r0 + 12'sd1 == nS);
    wire        ln_cs  = lin ? (part ? (sx < HTH) : (sx < HT - W)) : hs_w;
    wire cs_w = eq ? eq_cs : ln_cs;
    wire de_w = (hx >= 10'd96) && (hx < 10'd496) && (vpos < ACT);

    // ---- the picture: 8 bars (white yellow cyan green magenta red blue black), a grey ramp (black .. white), black with a marker that moves 4 dots a frame ------------
    reg [9:0] mx = 10'd0;
    always @(posedge clk) if (frame_tick && !frz) mx <= (mx >= 10'd396) ? 10'd0 : mx + 10'd4;
    wire [9:0] xx = hx - 10'd96;                                        // 0..399 inside the picture
    reg [3:0] pix;
    always @(*) begin
        if (vpos < 9'd150) begin
            case (xx[9:0] / 10'd50)
            10'd0: pix = 4'd1;  10'd1: pix = 4'd7;  10'd2: pix = 4'd3;  10'd3: pix = 4'd5;
            10'd4: pix = 4'd4;  10'd5: pix = 4'd2;  10'd6: pix = 4'd6;  default: pix = 4'd0;
            endcase
        end else if (vpos < 9'd210) begin
            case (xx[9:0] / 10'd80)
            10'd0: pix = 4'd0;  10'd1: pix = 4'd11; 10'd2: pix = 4'd12; 10'd3: pix = 4'd15; default: pix = 4'd1;
            endcase
        end else if (vpos >= ACT - 9'd24 && xx >= mx && xx < mx + 10'd40) pix = 4'd1;
        else pix = 4'd0;
    end

    function [23:0] pal_rgb;                                            // Colodore, as pal_ram.v starts
        input [3:0] i;
        case (i)
        4'd0:  pal_rgb = 24'h000000;  4'd1:  pal_rgb = 24'hFFFFFF;  4'd2:  pal_rgb = 24'h813338;  4'd3:  pal_rgb = 24'h75CEC8;
        4'd4:  pal_rgb = 24'h8E3C97;  4'd5:  pal_rgb = 24'h56AC4D;  4'd6:  pal_rgb = 24'h2E2C9B;  4'd7:  pal_rgb = 24'hEDF171;
        4'd8:  pal_rgb = 24'h8E5029;  4'd9:  pal_rgb = 24'h553800;  4'd10: pal_rgb = 24'hC46C71;  4'd11: pal_rgb = 24'h4A4A4A;
        4'd12: pal_rgb = 24'h7B7B7B;  4'd13: pal_rgb = 24'hA9FF9F;  4'd14: pal_rgb = 24'h706DEB;  default: pal_rgb = 24'hB2B2B2;
        endcase
    endfunction

    reg [3:0] idx_r = 4'd0;
    reg       hs_r = 1'b0, cs_r = 1'b0, de_r = 1'b0;
    always @(posedge clk) begin
        idx_r <= pix;
        hs_r <= hs_w; cs_r <= cs_w; de_r <= de_w;
    end
    wire [3:0] sidx;

    // PHASE_INC = fsc / f_clk x 2^26 for the clock each System Mode runs (as c128_avout.v), plus the trim
    reg [25:0] inc = 26'h900035;
    wire [25:0] trim_inc = {{15{trim[7]}}, trim, 3'd0};
    always @(posedge clk)
        case (std)
        2'd0:    inc <= 26'h900035 + trim_inc;      // PAL     4.43361875 MHz / 31.527778 MHz
        2'd1:    inc <= 26'h700162 + trim_inc;      // NTSC    3.579545   MHz / 32.725694 MHz
        2'd2:    inc <= 26'h8a9009 + trim_inc;      // PAL-60  4.43361875 MHz / 32.765152 MHz
        default: inc <= 26'h740d18 + trim_inc;      // NTSC-50 3.579545   MHz / 31.584821 MHz
        endcase

    wire [7:0] y_w, c_w, cv_w;
    diag_yc #(.WIDTH(26)) enc (
        .clk(clk), .inc(inc), .pal_en(pal), .burst(burst), .blk(blk), .vinv(vinv), .bamp(bamp), .gain(gain), .wamp(wamp), .synl(synl),
        .hsync(hs_r), .csync(cs_r), .de(de_r), .idx(idx_r), .sidx(sidx), .rgb_s(pal_rgb(sidx)),
        .y_o(y_w), .c_o(c_w), .cv_o(cv_w)
    );

    // ---- T259: the words of the probe and the RGB mode (direct words, no encoder), registered at t3 (idx_r is t0); sync and window are delayed with them -------------------------------------------
    // sh[k] / dh[k] are cs_r / de_r delayed by k + 1 clocks: the direct words are at t3 (the group flops then at t4), the encoder's at t8 (group flops at t9)
    reg [7:0] sh = 8'd0, dh = 8'd0;
    reg [9:0] xx1 = 10'd0;
    reg [4:0] bk2 = 5'd0, bk3 = 5'd0;
    always @(posedge clk) begin sh <= {sh[6:0], cs_r}; dh <= {dh[6:0], de_r}; xx1 <= xx; bk2 <= xx1[9:5]; bk3 <= bk2; end
    wire [14:0] rm = xx1[8:0] * 6'd41;
    wire [7:0] ramp = rm[13:6];                                            // 0 .. 255 across the 400 dots (xx1 is aligned with idx_r)
    reg  [23:0] cc = 24'd0;                                                // t1: the palette colour of idx_r, or the ramp of one word
    always @(posedge clk) begin
        if (!de_r) cc <= 24'd0;                                            // black outside the picture window
        else if (rch == 2'd0) cc <= pal_rgb(idx_r);
        else cc <= {rch == 2'd1 ? ramp : 8'd0, rch == 2'd2 ? ramp : 8'd0, rch == 2'd3 ? ramp : 8'd0};
    end
    wire [4:0] wsc = (wamp > 5'd16) ? 5'd16 : wamp;                         // the RGB full scale: wamp / 16
    (* use_dsp = "yes" *) reg [12:0] pr13 = 13'd0, pg13 = 13'd0, pb13 = 13'd0;
    always @(posedge clk) begin
        pr13 <= cc[23:16] * {8'd0, wsc}; pg13 <= cc[15:8] * {8'd0, wsc}; pb13 <= cc[7:0] * {8'd0, wsc};
    end
    // the probe: bar k (dot / 32) of the first eight has only bit k set, the rest of the picture has all bits
    wire [7:0] bar = (bk3 < 5'd8) ? (8'd1 << bk3[2:0]) : 8'hFF;
    reg [7:0] pw = 8'd0, pb = 8'd0;                                         // the word under test (sync, blanking, bars) and the others (blanking)
    reg [7:0] rw = 8'd0, gw = 8'd0, bw = 8'd0;
    always @(posedge clk) begin
        pw <= sh[1] ? synl : dh[1] ? bar : blk;           // from the signals at t2
        pb <= blk;
        rw <= pr13[11:4]; gw <= pg13[11:4]; bw <= pb13[11:4];               // the products are at most 255 x 16: bits 11:4 are the scaled value
    end

    // ---- the groups: which word, which bit order --------------------------------------------------------------------------------------
    function [5:0] who_f;                                               // {slot 0, 1, 2}: which of the three words (0 Y / R, 1 C / G, 2 CV / B)
        input [2:0] p;
        case (p)
        3'd0:    who_f = {2'd0, 2'd1, 2'd2};
        3'd1:    who_f = {2'd0, 2'd2, 2'd1};
        3'd2:    who_f = {2'd1, 2'd0, 2'd2};
        3'd3:    who_f = {2'd1, 2'd2, 2'd0};
        3'd4:    who_f = {2'd2, 2'd0, 2'd1};
        default: who_f = {2'd2, 2'd1, 2'd0};
        endcase
    endfunction
    wire [5:0] who = who_f(perm);
    function [7:0] pick;
        input [1:0] w;
        input [7:0] y, c, cv;
        pick = (w == 2'd0) ? y : (w == 2'd1) ? c : cv;
    endfunction
    function [7:0] brev;
        input [7:0] x;
        brev = {x[0], x[1], x[2], x[3], x[4], x[5], x[6], x[7]};
    endfunction
    wire [7:0] wa = (mode == 2'd1) ? ((pch == 2'd0) ? pw : pb) : (mode == 2'd2) ? pick(who[5:4], rw, gw, bw) : pick(who[5:4], y_w, c_w, cv_w);
    wire [7:0] wb = (mode == 2'd1) ? ((pch == 2'd1) ? pw : pb) : (mode == 2'd2) ? pick(who[3:2], rw, gw, bw) : pick(who[3:2], y_w, c_w, cv_w);
    wire [7:0] wc = (mode == 2'd1) ? ((pch == 2'd2) ? pw : pb) : (mode == 2'd2) ? pick(who[1:0], rw, gw, bw) : pick(who[1:0], y_w, c_w, cv_w);
    wire [7:0] ca = rev[0] ? brev(wa) : wa;                             // the channel words: slot 0, 1, 2 = A, B, C (scan groups) or R, G, B (names)
    wire [7:0] cb = rev[1] ? brev(wb) : wb;
    wire [7:0] cc2 = rev[2] ? brev(wc) : wc;
    // names: AVID_x[n + 2] = word x bit n. The balls of ga, gb, gc as in mono/top.v: ga = {G9 R9 G7 G6 G4 G3 B9 B6}, gb = {R4 R3 B8 B7 B5 B4 B3 B2}, gc = {R8 R7 R6 R5 R2 G8 G5 G2}
    wire [7:0] nr = ca, ng = cb, nb = cc2;
    wire [7:0] na = {ng[7], nr[7], ng[5], ng[4], ng[2], ng[1], nb[7], nb[4]};
    wire [7:0] nbb = {nr[2], nr[1], nb[6], nb[5], nb[3], nb[2], nb[1], nb[0]};
    wire [7:0] nc = {nr[6], nr[5], nr[4], nr[3], nr[0], ng[6], ng[3], ng[0]};
    (* IOB = "TRUE" *) reg [7:0] ga_r = 8'd0;
    (* IOB = "TRUE" *) reg [7:0] gb_r = 8'd0;
    (* IOB = "TRUE" *) reg [7:0] gc_r = 8'd0;
    (* IOB = "TRUE" *) reg syn_r = 1'b1;
    (* IOB = "TRUE" *) reg fb_r = 1'b0;
    wire dmode = (mode == 2'd1) || (mode == 2'd2);
    wire cs_x = dmode ? sh[2] : sh[7];                         // the sync and the window as late as the words of the mode
    wire de_x = dmode ? dh[2] : dh[7];
    always @(posedge clk) begin
        ga_r <= nmap ? na : ca;
        gb_r <= nmap ? nbb : cb;
        gc_r <= nmap ? nc : cc2;
        syn_r <= (synm == 2'd0) ? 1'b1 : (synm == 2'd1) ? 1'b0 : (synm == 2'd2) ? ~cs_x : cs_x;
        fb_r  <= (fbm == 2'd0) ? 1'b0 : (fbm == 2'd1) ? 1'b1 : (fbm == 2'd2) ? de_x : ~de_x;
    end
    assign ga = ga_r;
    assign gb = gb_r;
    assign gc = gc_r;
    assign syn_n = syn_r;
    assign fb = fb_r;
endmodule
