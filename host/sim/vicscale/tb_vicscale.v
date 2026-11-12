// T164 WP8, scenario S8: the VIC raster stays locked in 1080p. vic_scaler (EXT = 1) runs on hdmi_timing's counters, fed by a VIC raster model (PAL
// 63 x 312 for row 2 = 1080p50, NTSC 65 x 263 for row 8 = 1080p60) whose dot clock and the TMDS clock are in the exact ratio the tool proves for
// the locked rows (hdmi_modes.vh: 6875 / 1456, 61875 / 13676). The model writes pix = {marker, frame[2:0]}: the marker is set on the window's
// edge pixels (raster x 96 and 495, the first and the last raster line of the window); frame increments when the raster wraps.
// FRAMES output frames are checked after the lock (the first two are skipped):
//   - the frame holds ONE VIC frame: every window pixel has the tag of the frame the lock started in (60 Hz: +1 for the lines that wrap into the next
//     raster frame), none from another one (no ring overrun, no tear), the tag rises by exactly one from output frame to output frame (no frame
//     lost or shown twice) - the output frame period equals the VIC frame period
//   - the window sits where it does today: 1600 pixels of the 1920 centred, black bars, markers at the edge pixels and the first / last window line
//   - the lock holds: align pulses once, scaler.locked never falls, and at every VIC frame event (the scaler's frame_seen) the timing is at the
//     same position (slips counted when it differs)
// SLIP = 1 makes the TMDS clock one part in 10,000 slow (the control): the position at the VIC event moves by about 300 clocks a frame.
// Last line: "vicscale <row> PASS" / "FAIL".
`timescale 1fs/1fs
module tb;
    parameter ROW = 2;                    // 2: 1080p50 (PAL), 8: 1080p60 (NTSC)
    parameter FRAMES = 1000;
    parameter SLIP = 0;
    localparam V60 = (ROW == 8);
    localparam integer K = V60 ? 494 : 4614;
    localparam integer P_VIC = V60 ? 61875 * K : 6875 * K;
    localparam integer P_PIX0 = V60 ? 13676 * K : 1456 * K;
    localparam integer P_PIX = SLIP ? P_PIX0 + (P_PIX0 / 10000) / 2 * 2 : P_PIX0;
    localparam integer PXMAX = V60 ? 519 : 503;
    localparam integer PYMAX = V60 ? 262 : 311;
    localparam integer LINES = PYMAX + 1;
    localparam integer Y0 = V60 ? 31 : 16;
    // 60 Hz: the ring holds 64 raster lines and 263 is not a multiple of it, so raster lines 256..262 (row 0..6) are overwritten by lines 0..6 of the next
    // frame before the window reads them (they and lines 0..7 are the C64's bottom / top border): the window lines from 256 on show the next frame's tag.
    // Today's behaviour of the module, expected here as it is.
    localparam integer NEXTF = V60 ? 256 : 1000;
    localparam integer FTOT = V60 ? 2475000 : 2970000;

    reg vclk = 1'b0, clk = 1'b0;
    always #(P_VIC / 2) vclk = ~vclk;
    always #(P_PIX / 2) clk = ~clk;

    reg rst = 1'b1;
    initial begin repeat (20) @(posedge clk); rst <= 1'b0; end

    // ---- the VIC model
    reg [1:0] vph = 2'd0; reg [9:0] vpx = 10'd0; reg [8:0] vpy = 9'd0; reg [7:0] vframe = 8'd0;
    always @(posedge vclk) begin
        vph <= vph + 2'd1;
        if (vph == 2'd3) begin
            vpx <= (vpx == PXMAX) ? 10'd0 : vpx + 10'd1;
            if (vpx == PXMAX) begin
                vpy <= (vpy == PYMAX) ? 9'd0 : vpy + 9'd1;
                if (vpy == PYMAX) vframe <= vframe + 8'd1;
            end
        end
    end
    // the line markers need the window's last raster line: from the same line model the scaler uses
    integer lnt [0:1079];
    integer ln_i, acc_i, y_i;
    initial begin
        ln_i = 0; acc_i = 0;
        for (y_i = 0; y_i < 1080; y_i = y_i + 1) begin
            lnt[y_i] = ln_i;
            acc_i = acc_i + (V60 ? 2 : 1);
            if (acc_i >= (V60 ? 9 : 4)) begin acc_i = acc_i - (V60 ? 9 : 4); ln_i = ln_i + 1; end
        end
    end
    localparam integer YT = Y0;
    integer YB;
    initial YB = (Y0 + lnt[1079]) % LINES;
    wire marker = (vpx == 96) || (vpx == 495) || (vpy == YT) || (vpy == YB);
    wire [3:0] vpix = {marker, vframe[2:0]};

    // ---- the DUT: the timing and the scaler on its counters
    wire align, t_de, t_hs, t_vs, running;
    wire [23:0] t_rgb;
    wire [11:0] t_cx, hact, xact0; wire [10:0] t_cy, vact, yact0;
    wire ce, aligned;
    hdmi_timing mtim (.clk(clk), .rst(rst), .mode(ROW[3:0]), .align(align), .valid(), .aligned(aligned), .ce(ce), .rgb(t_rgb),
        .de(t_de), .hs(t_hs), .vs(t_vs), .cx(t_cx), .cy(t_cy), .running(running), .hact(hact), .vact(vact), .xact0(xact0), .yact0(yact0),
        .gear(), .vic(), .rate(), .sx_e(), .fw_m1(), .sy_e(), .ey_e(), .ph_e(), .mode_q());
    wire [23:0] rgb; wire de, hs, vs, locked; wire [11:0] cx; wire [10:0] cy; wire [9:0] x_text;
    vic_scaler #(.EXT(1'b1)) sc (
        .vclk(vclk), .pix(vpix), .px(vpx), .py(vpy), .clk(clk), .rst(rst),
        .pal_clk(clk), .pal_we(1'b0), .pal_addr(6'd0), .pal_wdata(8'd0),
        .rgb(rgb), .de(de), .hs(hs), .vs(vs), .cx(cx), .cy(cy), .locked(locked),
        .blank(1'b0), .x_text(x_text), .v60(V60[0]), .scan(1'b0), .relock(1'b0),
        .en(1'b1), .t_de(t_de), .t_hs(t_hs), .t_vs(t_vs), .t_cx(t_cx), .t_cy(t_cy), .xact0(xact0), .yact0(yact0), .align(align));

    // ---- the checks
    // colour -> palette index through a table keyed by (r + 2 g + b) mod 256 (checked unique for the 16 colours)
    integer lut [0:255];
    integer li, lk;
    initial begin
        for (li = 0; li < 256; li = li + 1) lut[li] = -1;
        #1;
        for (li = 0; li < 16; li = li + 1) begin
            lk = (sc.pal.pal_r[li] + 2 * sc.pal.pal_g[li] + sc.pal.pal_b[li]) & 255;
            if (lut[lk] >= 0) $display("bench error: palette colours %0d and %0d share the key", lut[lk], li);
            lut[lk] = li;
        end
    end
    function integer idx_of(input [23:0] c);
        idx_of = lut[(c[23:16] + 2 * c[15:8] + c[7:0]) & 255];
    endfunction
    integer ax, ay, ix, fo, frames_ok = 0, errs = 0, align_n = 0, lock_fall = 0, tag_f, tag_prev = -1, exp_idx, sxx, lnn, slips = 0, tag_err = 0, geo_err = 0;
    integer ev_gap = 0;
    reg primed = 1'b0;
    reg dbg; initial dbg = $test$plusargs("DBG");
    integer fcount = 0, pc = 0, per_bad = 0, vic_ev = 0, ref_h = -1, ref_v = -1;
    reg locked_q = 1'b0, seen_lock = 1'b0, de_q;
    reg [3:0] ix4;
    always @(posedge clk) begin
        if (align) align_n <= align_n + 1;
        locked_q <= locked;
        if (locked_q && !locked) lock_fall <= lock_fall + 1;
        // the VIC frame event: where is the timing
        if (ev_gap != 0) ev_gap <= ev_gap - 1;
        // the VIC holds raster x 0 of its line ALIGN for four dot4x clocks: frame_seen pulses up to four times a frame, the first one counts
        if (sc.frame_seen && locked && ev_gap == 0) begin
            ev_gap <= 200;
            if (!primed) primed <= 1'b1;            // the pulses of the frame that locked are not compared
            else vic_ev <= vic_ev + 1;
            if (!primed) begin end
            else if (ref_h < 0) begin ref_h <= mtim.hc; ref_v <= mtim.vc; end
            else if (mtim.hc != ref_h || mtim.vc != ref_v) slips <= slips + 1;
        end
        if (locked) seen_lock <= 1'b1;
        // the output
        if (de && seen_lock) begin
            ax = cx - xact0; ay = cy - yact0;
            if (ax == 0 && ay == 0) begin
                fcount = fcount + 1; pc = 0;
            end
            if (fcount > 2 && fcount <= FRAMES + 2) begin
                ix = idx_of(rgb);
                if (dbg && (ay == 107 || ay == 1013) && (ax == 159 || ax == 160 || ax == 164 || ax == 500 || ax == 1000 || ax == 1756 || ax == 1759)) $display("dbg frame %0d (%0d,%0d) idx %0d  vframe %0d vpy %0d", fcount, ax, ay, ix, vframe, vpy);
                if (ax == 500 && ay == 100) tag_f = ix & 7;
                if (ay > 100 || (ay == 100 && ax >= 500)) begin
                    // the expectation against the tag of this frame
                    lnn = lnt[ay];
                    if (ax >= 160 && ax < 1760) begin
                        sxx = 96 + (ax - 160) / 4;
                        exp_idx = ((sxx == 96 || sxx == 495 || lnn == 0 || ((Y0 + lnn) % LINES) == YB) ? 8 : 0)
                                  + ((tag_f + ((Y0 + lnn) >= NEXTF ? 1 : 0)) & 7);
                    end else exp_idx = 0;
                    if (ix != exp_idx) begin
                        if (ax >= 160 && ax < 1760 && (ix & 7) != (exp_idx & 7)) tag_err = tag_err + 1; else geo_err = geo_err + 1;
                        if (tag_err + geo_err < 4) $display("  FAIL row %0d frame %0d pixel (%0d, %0d): colour index %0d, want %0d", ROW, fcount, ax, ay, ix, exp_idx);
                    end
                end
                if (ax == 1919 && ay == 1079) begin
                    if (tag_prev >= 0 && tag_f != ((tag_prev + 1) & 7)) begin
                        tag_err = tag_err + 1;
                        if (tag_err < 4) $display("  FAIL row %0d frame %0d: tag %0d after %0d (a frame lost or shown twice)", ROW, fcount, tag_f, tag_prev);
                    end
                    tag_prev = tag_f;
                    frames_ok = frames_ok + 1;
                end
            end
        end
    end
    // the output frame period: clocks between two frame starts of the timing
    integer last_vs_t = -1, nclk = 0;
    reg vs_q = 1'b0, al_prev = 1'b0;
    always @(posedge clk) begin
        nclk <= nclk + 1;
        vs_q <= t_vs;
        if (t_vs && !vs_q) begin      // both 1080p rows have a positive vsync
            if (last_vs_t >= 0 && (nclk - last_vs_t) != FTOT && aligned && al_prev) per_bad <= per_bad + 1;
            last_vs_t <= nclk;
            al_prev <= aligned;
        end
    end

    initial begin
        wait (frames_ok >= FRAMES);
        @(posedge clk);
        if (align_n != 1) begin errs = errs + 1; $display("  FAIL row %0d: align pulsed %0d times", ROW, align_n); end
        if (lock_fall != 0) begin errs = errs + 1; $display("  FAIL row %0d: the lock fell %0d times", ROW, lock_fall); end
        if (slips != 0) begin errs = errs + 1; $display("  FAIL row %0d: the timing slipped against the VIC frame %0d times in %0d frames", ROW, slips, vic_ev); end
        if (tag_err != 0 || geo_err != 0) errs = errs + 1;
        $display("  row %0d: %0d frames, %0d VIC frame events, align pulses %0d, slips %0d, tag errors %0d, window errors %0d, frame period != %0d clocks: %0d",
                 ROW, frames_ok, vic_ev, align_n, slips, tag_err, geo_err, FTOT, per_bad);
        if (per_bad != 0) errs = errs + 1;
        if (errs == 0) $display("vicscale row %0d PASS", ROW); else $display("vicscale row %0d FAIL", ROW);
        $finish;
    end
    // a run that does not get there: FRAMES frames of FTOT clocks plus the lock
    initial begin
        #(64'd1) ;
        #(64'd25000000000000 * (FRAMES + 10));   // 25 ms (> one 50 Hz frame) in fs per frame
        $display("vicscale row %0d FAIL timeout (%0d of %0d frames)", ROW, frames_ok, FRAMES);
        $finish;
    end
endmodule
