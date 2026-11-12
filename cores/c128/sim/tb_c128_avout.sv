// T140 S2 "the encoder in simulation", for the C128's analogue output (cores/c128/mono/c128_avout.v around MiSTer's yc_out.sv).
// One checker per System Mode (0 PAL, 1 NTSC, 2 PAL-60, 3 NTSC-50), each with its own clock and a model of the VIC's pixel stream
// (palette index, raster x in dots, raster line; a dot every 4 clocks): 16 colour bars of 25 dots across the 400-dot window, in the
// palette's own colours (pal_ram's reset contents). The checker watches the three DAC words and measures, per standard:
//   sync    the Y word is 0 for the sync: width, line period (HT dots x 4 clocks), 3 serrated vsync lines at the expected raster lines,
//           LT lines per frame; the sync is a pulse (the Y word is back at the blanking level after it)
//   blank   Y = BLK between the sync and the picture, in the front porch, and on a line outside the picture window
//   burst   after the sync's trailing edge: amplitude (peak to peak on the C word around 128), frequency (zero crossings, fsc +-4 %),
//           C = 128 while the sync is low and in the blanking after the burst; the burst of a vsync line is not there
//   bars    on a line in the middle of the window: Y of each bar = BLK + 11/16 of the BT.601 luma of its palette colour (+-2);
//           the grey bars (0 1 11 12 15) carry no chroma, the others do, at the colour subcarrier's frequency (+-5 %)
//   PAL     the burst's phase against a reference at the exact subcarrier moves by +-90 degrees from line to line, the sign
//           alternating; NTSC: it does not move (+-25 degrees)
// The final line is PASS tb_c128_avout or FAIL; every check prints "ok" or "FAIL <scenario> ...".
`timescale 1ns / 1ps

module avchk #(parameter integer VMODE = 0, parameter real HALF = 15.859, parameter real FCLK = 31527777.78, parameter real RUN = 75000000.0)
    (output integer o_fail, output integer o_ok);
    localparam bit V60 = (VMODE == 1) || (VMODE == 2);
    localparam bit PAL = (VMODE == 0) || (VMODE == 2);
    localparam integer HT = V60 ? 520 : 504;
    localparam integer LT = V60 ? 263 : 312;
    localparam integer Y0 = V60 ? 31 : 16;
    localparam integer ACT = V60 ? 240 : 272;
    localparam integer XS = V60 ? 4 : 5;
    localparam integer WD = V60 ? 38 : 37;
    localparam integer VSL0 = V60 ? 12 : 296;         // raster line of the first serrated sync period
    localparam integer BLK = PAL ? 77 : 73;
    localparam real FSC = PAL ? 4433618.75 : 3579545.4545;
    localparam integer LINE = HT * 4;                 // clocks per line

    reg clk = 1'b0;
    always #(HALF) clk = ~clk;

    // ---- the VIC model
    reg [9:0] gx = 10'd0;
    reg [8:0] gy = 9'd0;
    reg [1:0] gd = 2'd0;
    reg [3:0] gp;
    always @(posedge clk) begin
        gd <= gd + 2'd1;
        if (gd == 2'd3) begin
            if (gx == HT - 1) begin gx <= 0; gy <= (gy == LT - 1) ? 9'd0 : gy + 9'd1; end
            else gx <= gx + 1;
        end
    end
    always @* begin
        if (gx >= 96 && gx < 496) gp = (gx - 96) / 25;
        else gp = 4'd14;
    end

    wire [7:0] y_o, c_o, cv_o;
    c128_avout #(.PAL_VINV(1)) dut (.clk(clk), .vmode(VMODE[1:0]), .pix(gp), .px(gx), .py(gy),
        .pal_clk(clk), .pal_we(1'b0), .pal_addr(6'd0), .pal_wdata(8'd0), .y_o(y_o), .c_o(c_o), .cv_o(cv_o));

    // ---- the clocks of the sync's leading edge: the model's own time base, to find the pipeline delay
    longint unsigned t = 0;
    longint unsigned g_lead = 0;                      // clock at which the model's dot XS began (latest)
    reg [7:0] y_d = 8'd0;
    always @(posedge clk) begin
        t <= t + 1;
        if (gx == XS && gd == 2'd0) g_lead <= t;
    end

    // ---- capture: one buffer per sync period, started at the leading edge
    reg [7:0] yb [0:LINE + 64];
    reg [7:0] cb [0:LINE + 64];
    reg [7:0] vb [0:LINE + 64];
    integer   n = 0;                                  // samples captured in the period
    bit       cap = 0;
    longint unsigned lead_t = 0, lead_g = 0, prev_lead_t = 0;
    integer   lead_y = 0;
    integer   nlines = 0, nbroad = 0, nfail = 0, nok = 0;
    integer   broad_first_y = -1, broad_seq = 0, frames = 0, lines_in_frame = 0, last_broad_frame_line = -1;
    integer   dmin = 1000, dmax = -1000;
    real      prev_theta = 0.0, prev_d = 0.0;
    bit       prev_theta_ok = 0, prev_d_ok = 0;
    integer   pal_alt_bad = 0, pal_alt_n = 0, ntsc_bad = 0, ntsc_n = 0;
    integer   bars_checked = 0, blank_checked = 0;

    task automatic ok(input string what, input bit cond);
        if (cond) begin nok = nok + 1; end
        else begin nfail = nfail + 1; $display("FAIL M%0d %s", VMODE, what); end
    endtask

    function automatic real lum(input integer r, input integer g, input integer b);   // yc_out's own coefficients: 306 R + 601 G + 117 B
        lum = ((306 * r + 601 * g + 117 * b) / 1024);
    endfunction

    reg [23:0] pal16 [0:15];
    initial begin
        pal16[0]=24'h000000; pal16[1]=24'hFFFFFF; pal16[2]=24'h813338; pal16[3]=24'h75CEC8; pal16[4]=24'h8E3C97; pal16[5]=24'h56AC4D;
        pal16[6]=24'h2E2C9B; pal16[7]=24'hEDF171; pal16[8]=24'h8E5029; pal16[9]=24'h553800; pal16[10]=24'hC46C71; pal16[11]=24'h4A4A4A;
        pal16[12]=24'h7B7B7B; pal16[13]=24'hA9FF9F; pal16[14]=24'h706DEB; pal16[15]=24'hB2B2B2;
    end

    // peak to peak / frequency / phase of c in [a, b) of the capture
    function automatic integer pp(input integer a, input integer b);
        integer i, lo, hi;
        begin lo = 255; hi = 0;
            for (i = a; i < b; i = i + 1) begin if (cb[i] < lo) lo = cb[i]; if (cb[i] > hi) hi = cb[i]; end
            pp = hi - lo;
        end
    endfunction
    function automatic real freq(input integer a, input integer b);   // crossings of c = 128 (linear interpolation between samples), Hz
        integer i, cr; real tc, t1, tl; bit h0, h1;
        begin cr = 0; t1 = 0; tl = 0;
            for (i = a; i < b - 1; i = i + 1) begin
                h0 = cb[i] >= 129; h1 = cb[i + 1] >= 129;
                if (h0 != h1) begin
                    tc = i + (128.5 - $itor(cb[i])) / ($itor(cb[i + 1]) - $itor(cb[i]));
                    cr = cr + 1; if (cr == 1) t1 = tc; tl = tc;
                end
            end
            freq = (cr > 3 && tl > t1) ? ((cr - 1) / 2.0) * FCLK / (tl - t1) : 0.0;
        end
    endfunction
    function automatic real theta(input integer a, input integer b, input longint unsigned t0);   // burst phase against a reference at FSC
        integer i; real s, c, w, ph;
        begin s = 0; c = 0; w = 6.283185307179586 * FSC / FCLK;
            for (i = a; i < b; i = i + 1) begin
                ph = w * (t0 + i);
                s = s + (cb[i] - 128.0) * $sin(ph);
                c = c + (cb[i] - 128.0) * $cos(ph);
            end
            theta = $atan2(s, c);
        end
    endfunction
    // the carrier's (U, V) vector in [a, b) of the capture against the exact subcarrier: x = A sin(wt + phi) -> (s, c) = N A / 2 (cos phi, sin phi)
    task automatic zvec(input integer a, input integer b, input longint unsigned t0, output real zs, output real zc);
        integer i; real w, ph;
        begin zs = 0; zc = 0; w = 6.283185307179586 * FSC / FCLK;
            for (i = a; i < b; i = i + 1) begin
                ph = w * (t0 + i);
                zs = zs + (cb[i] - 128.0) * $sin(ph);
                zc = zc + (cb[i] - 128.0) * $cos(ph);
            end
        end
    endtask
    real prev_bs = 0.0, prev_bc = 0.0;
    bit  prev_b_ok = 0;
    integer hue_n = 0, hue_bad = 0, hue_blue = 0, hue_lgreen = 0;
    function automatic real wrap(input real a);
        real r;
        begin r = a; while (r > 3.141592653589793) r = r - 6.283185307179586; while (r < -3.141592653589793) r = r + 6.283185307179586; wrap = r; end
    endfunction

    // analysis of a finished normal (not serrated) sync period of n samples, raster line lead_y at its leading edge
    task automatic analyse_normal();
        integer i, w, bs, be, k, a, b, d, vp, yy;
        real f, th, dd, exp_y, mean;
        integer wmin;
        begin
            // sync width: samples with Y = 0 from the start
            w = 0; while (w < n && yb[w] == 0) w = w + 1;
            ok("sync width 4 clocks per dot", w >= WD * 4 - 8 && w <= WD * 4 + 8);
            // after the sync Y is at the blanking level until the picture: sample 90 clocks after the trailing edge
            ok("Y = blanking after the sync", yb[w + 40] == BLK && yb[w + 90] == BLK);
            // burst window: after the trailing edge
            bs = w + 40; be = w + 80;           // inside both standards' burst (starts 28/20, ends 99/102 samples after the edge)
            a = w + (PAL ? 40 : 34); b = w + (PAL ? 90 : 90);
            ok("burst amplitude", pp(a, b) >= 60 && pp(a, b) <= 110);
            f = freq(a, b);
            ok($sformatf("burst frequency %0f Hz, fsc +-2 %%", f), f > FSC * 0.98 && f < FSC * 1.02);
            // C = 128 inside the sync and well after the burst
            ok("C flat 128 during the sync", pp(2, w - 2) == 0 && cb[4] == 128);
            ok("C flat after the burst", pp(w + 130, w + 160) == 0);
            // the front porch and the rest of the blanking: Y = BLK, C = 128 (before the picture starts)
            // sync-relative positions: the picture starts (96 - XS) dots after the leading edge, plus the pipeline offset d
            d = lead_t - lead_g;
            if (d < dmin) dmin = d; if (d > dmax) dmax = d;
            vp = (lead_y >= Y0) ? lead_y - Y0 : lead_y + LT - Y0;
            // lead_y is the model's raster line at the leading edge: the picture on this period's line starts at (96 - XS) dots
            if (vp >= 20 && vp < 30 && frames >= 1) begin
                // a picture line: bars
                for (k = 0; k < 16; k = k + 1) begin
                    a = ((96 - XS) + 25 * k) * 4 + 8;
                    b = ((96 - XS) + 25 * (k + 1)) * 4 - 8;
                    mean = 0; for (i = a; i < b; i = i + 1) mean = mean + yb[i]; mean = mean / (b - a);
                    exp_y = BLK + (11.0 * lum(pal16[k][23:16], pal16[k][15:8], pal16[k][7:0])) / 16.0;
                    if (k == 0 || k == 1 || k == 7 || k == 4) ok($sformatf("bar %0d luma %0f expected %0f", k, mean, exp_y), mean > exp_y - 3.0 && mean < exp_y + 3.0);
                    else ok($sformatf("bar %0d luma %0f expected %0f", k, mean, exp_y), mean > exp_y - 3.0 && mean < exp_y + 3.0);
                    if (k == 0 || k == 1 || k == 11 || k == 12 || k == 15)
                        ok($sformatf("grey bar %0d has no chroma (pp %0d)", k, pp(a, b)), pp(a, b) <= 3);
                    else begin
                        ok($sformatf("colour bar %0d has chroma (pp %0d)", k, pp(a, b)), pp(a, b) >= 16);
                        if (k == 2 || k == 6 || k == 13) begin
                            f = freq(((96 - XS) + 25 * k) * 4 + 4, ((96 - XS) + 25 * (k + 1)) * 4 - 4);
                            ok($sformatf("bar %0d chroma frequency %0f", k, f), f > FSC * 0.98 && f < FSC * 1.02);
                        end
                    end
                    bars_checked = bars_checked + 1;
                    // the hue the way a TV decodes it: the burst (PAL: the two bursts' bisector = the -U axis, the burst's side of it = the sign of V) is the
                    // reference; (U, V) of the bar, rotated into that frame, must point where the colour's (0.492 (B-Y), 0.877 (R-Y)) point
                    if (!(k == 0 || k == 1 || k == 11 || k == 12 || k == 15) && prev_b_ok && lead_t - prev_lead_t == LINE) begin : hue
                        real bs1, bc1, ms, mc, mm, us, uc, ws, wc, bws, bwc, sg, zs, zc, ue, ve, ang_e, ang_m, err, yy2;
                        zvec(w + (PAL ? 44 : 36), w + (PAL ? 84 : 78), lead_t, bs1, bc1);
                        if (PAL) begin ms = bs1 + prev_bs; mc = bc1 + prev_bc; end else begin ms = bs1; mc = bc1; end
                        mm = $sqrt(ms * ms + mc * mc);
                        us = -ms / mm; uc = -mc / mm;                       // the U axis
                        // w = z * conj(Uhat): real = U, imag = V (the imaginary axis is the carrier's cos, +90 degrees from U)
                        bws = bs1 * us + bc1 * uc; bwc = bc1 * us - bs1 * uc;
                        zvec(a, b, lead_t, zs, zc);
                        ws = zs * us + zc * uc; wc = zc * us - zs * uc;
                        // burst expressed in the U frame is at 180 +- 45 degrees: Im > 0 means 135 degrees = the +V line
                        sg = PAL ? ((bwc > 0.0) ? 1.0 : -1.0) : 1.0;
                        if (PAL) sg = -sg;       // the board's order: V is opposite to yc_out.sv's against the same burst swing (c128_yc VINV)
                        yy2 = lum(pal16[k][23:16], pal16[k][15:8], pal16[k][7:0]);
                        ue = 0.492 * (pal16[k][7:0] - yy2); ve = 0.877 * (pal16[k][23:16] - yy2);
                        ang_e = $atan2(ve, ue); ang_m = $atan2(sg * wc, ws);
                        err = wrap(ang_m - ang_e) * 57.29578;
                        if (err < 0.0) err = -err;
                        hue_n = hue_n + 1; if (k == 6) hue_blue = hue_blue + 1; if (k == 13) hue_lgreen = hue_lgreen + 1;
                        ok($sformatf("hue of bar %0d: U/V angle %0f deg, expected %0f (line type V%s, raster line %0d)", k, ang_m * 57.29578, ang_e * 57.29578, sg > 0.0 ? "+" : "-", lead_y), err < 22.0);
                        if (err >= 22.0) hue_bad = hue_bad + 1;
                    end
                end
                // where the white bar (bar 1) begins: its dot (96 + 25), 4 clocks a dot, from the sync's leading edge
                i = ((96 - XS) + 25) * 4 - 40; while (i < LINE && yb[i] < BLK + 88) i = i + 1;
                ok($sformatf("picture starts where the sync says (white bar edge %0d clocks off)", i - (((96 - XS) + 25) * 4)),
                   i - (((96 - XS) + 25) * 4) >= -6 && i - (((96 - XS) + 25) * 4) <= 6);
                // front porch / border outside the window: blank
                ok("blank before the picture", yb[((96 - XS) - 6) * 4] == BLK);
                ok("blank after the picture", yb[((496 - XS) + 4) * 4] == BLK);
                ok("C flat in the front porch", cb[((496 - XS) + 4) * 4] == 128);
            end
            // a line outside the picture window is blank all along after the burst
            if (vp >= ACT + 2 && vp < ACT + 6 || (vp >= 0 && vp < 0)) begin
                wmin = 255; for (i = w + 4; i < LINE - 8; i = i + 1) if (yb[i] != BLK) wmin = 0;
                ok("line outside the window is blank", wmin != 0);
                blank_checked = blank_checked + 1;
            end
            // burst phase against the exact subcarrier, line to line (consecutive periods only)
            th = theta(w + (PAL ? 44 : 36), w + (PAL ? 84 : 78), lead_t);
            if (prev_theta_ok && lead_t - prev_lead_t == LINE) begin
                dd = wrap(th - prev_theta);
                if (PAL) begin
                    pal_alt_n = pal_alt_n + 1;
                    ok($sformatf("PAL burst moves by +-90 degrees (%0f deg, raster line %0d)", dd * 57.29578, lead_y), ((dd > 1.0 && dd < 2.14) || (dd < -1.0 && dd > -2.14)));
                    if (prev_d_ok && !(dd * prev_d < 0.0)) pal_alt_bad = pal_alt_bad + 1;
                    prev_d = dd; prev_d_ok = 1;
                end else begin
                    ntsc_n = ntsc_n + 1;
                    ok($sformatf("NTSC burst does not move (%0f deg)", dd * 57.29578), dd > -0.44 && dd < 0.44);
                end
            end else prev_d_ok = 0;
            zvec(w + (PAL ? 44 : 36), w + (PAL ? 84 : 78), lead_t, prev_bs, prev_bc);
            prev_b_ok = 1;
            prev_theta = th; prev_theta_ok = 1;
        end
    endtask

    task automatic analyse_broad();
        integer i, w;
        begin
            w = 0; while (w < n && yb[w] == 0) w = w + 1;
            ok("serrated sync: low for the line but the last pulse width", w >= (HT - WD) * 4 - 12 && w <= (HT - WD) * 4 + 12);
            ok("serrated sync: no burst on the sync tip", pp(40, w - 10) == 0);
            prev_theta_ok = 0; prev_d_ok = 0; prev_b_ok = 0;
        end
    endtask

    // the capture state machine: a period starts at the Y word's fall to 0 and ends at the next one
    reg [7:0] yq = 8'd255;
    integer   rec = 0;
    always @(posedge clk) begin
        yq <= y_o;
        if (t > 20) begin
            if (yq != 0 && y_o == 0) begin               // a leading edge
                if (cap && n > 8) begin
                    // finish the period before: normal or serrated by its low width
                    begin : fin
                        integer w; w = 0; while (w < n && yb[w] == 0) w = w + 1;
                        if (w > 200) begin
                            nbroad = nbroad + 1; analyse_broad();
                            if (broad_seq == 0) begin
                                ok($sformatf("first serrated line is raster line %0d (got %0d)", VSL0, lead_y), lead_y == VSL0);
                                if (frames >= 1) ok($sformatf("%0d lines per frame (got %0d)", LT, lines_in_frame), lines_in_frame == LT);
                                lines_in_frame = 0;
                            end
                            broad_seq = broad_seq + 1;
                        end else begin
                            if (broad_seq != 0) begin
                                ok($sformatf("three serrated lines per frame (got %0d)", broad_seq), broad_seq == 3);
                                frames = frames + 1;
                            end
                            broad_seq = 0;
                            if (n >= LINE - 8 && t > 9000) analyse_normal();
                        end
                    end
                end
                if (cap && t > 9000) begin
                    ok($sformatf("line period %0d clocks (expected %0d)", t - lead_t, LINE), t - lead_t == LINE);
                    lines_in_frame = lines_in_frame + 1;
                end
                prev_lead_t = lead_t;
                lead_t = t; lead_g = g_lead; lead_y = gy; n = 0; cap = 1; nlines = nlines + 1;
            end
            if (cap && n < LINE + 60) begin
                yb[n] = y_o; cb[n] = c_o; vb[n] = cv_o; n = n + 1;
            end
        end
    end

    initial begin
        #(RUN);
        ok("frames seen", frames >= 2);
        ok($sformatf("bars checked %0d", bars_checked), bars_checked >= 160);
        ok($sformatf("blank lines checked %0d", blank_checked), blank_checked >= 4);
        ok($sformatf("pipeline delay of the picture path constant (%0d..%0d clocks)", dmin, dmax), dmin == dmax && dmin > 0 && dmin < 40);
        if (PAL) begin
            ok($sformatf("PAL lines measured %0d", pal_alt_n), pal_alt_n >= 300);
            ok($sformatf("PAL alternation holds line to line (%0d breaks)", pal_alt_bad), pal_alt_bad == 0);
        end else ok($sformatf("NTSC lines measured %0d", ntsc_n), ntsc_n >= 300);
        ok($sformatf("hue decoded on %0d bars (%0d blue = U-heavy, %0d light green = V-heavy)", hue_n, hue_blue, hue_lgreen), hue_n >= 100 && hue_blue >= 8 && hue_lgreen >= 8);
        $display("M%0d %s: %0d lines, %0d serrated, %0d frames, D=%0d clocks, %0d checks ok, %0d failed", VMODE, PAL ? "PAL " : "NTSC", nlines, nbroad, frames, dmin, nok, nfail);
        o_fail = nfail; o_ok = nok;
    end
endmodule

module tb_c128_avout;
    wire [31:0] f0, f1, f2, f3, k0, k1, k2, k3;
    avchk #(.VMODE(0), .HALF(15.859), .FCLK(31527777.78)) m0 (.o_fail(f0), .o_ok(k0));
    avchk #(.VMODE(1), .HALF(15.278), .FCLK(32725694.44)) m1 (.o_fail(f1), .o_ok(k1));
    avchk #(.VMODE(2), .HALF(15.260), .FCLK(32765151.52)) m2 (.o_fail(f2), .o_ok(k2));
    avchk #(.VMODE(3), .HALF(15.829), .FCLK(31584821.43)) m3 (.o_fail(f3), .o_ok(k3));
    initial begin
        #75000010;
        if (f0 + f1 + f2 + f3 == 0 && k0 > 0 && k1 > 0 && k2 > 0 && k3 > 0) $display("PASS tb_c128_avout");
        else $display("FAIL tb_c128_avout: %0d checks failed", f0 + f1 + f2 + f3);
        $finish;
    end
endmodule
