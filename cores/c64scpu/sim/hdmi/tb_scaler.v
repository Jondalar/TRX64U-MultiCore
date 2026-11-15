// SPDX-License-Identifier: GPL-3.0-or-later
// U16 bench: vic_scaler.v on hdmi_timing.v, one row of U16 D2 and one scale per run (+mode=M +sc=S; sim/scaler/run.sh runs all 21).
// The VIC is synthetic (colour = x[3:0] ^ y[3:0], 504 x 312 dots at PAL dot4x / 520 x 263 at NTSC dot4x, one dot per 4 vclk); the
// pixel clock is the TMDS clock of the row, dot4x x 125/104 x N/M with the CDCE's N/M written out here (tools/hdmi_modes.py table), so
// one output frame lasts exactly one VIC frame (periods to 1 fs). After the lock, for NF frames:
//   - every output frame has g x htot x vtot pixel clocks and lasts the VIC frame (within one pixel clock), `locked` stays, no align;
//   - the window (from hm_geom: xw0, yw0, steps hn / hd, vn / vd; its size ceil(400 hd / hn) x ceil(wln vd / vn)) lies inside the
//     active area, every pixel in it shows VIC pixel (X0 + floor((x - xw0) hn / hd), Y0 + floor((y - yw0) vn / vd)) (Y0_60 and the
//     wrap at 263 at 60 Hz) in the reset palette, every other pixel is black, the window has exactly that many pixels per frame;
//   - the ring: at every window pixel's block RAM read the slot holds that very raster line (the bench mirrors the VIC's writes into
//     the slots, the window row mod 64: another line there = read before the VIC wrote it, or the ring was overtaken).
// +sc2=S: after the checked frames the scale changes to S at a frame start (as mode_req.v does): where the align line changes the
// lock is dropped and taken again within two VIC frames, else it holds; then NF frames are checked with S.
// +extra (T59, T62 G09): then the palette is rewritten through the bus port during a vertical blanking and one frame must show the
// new colours; then scan = 1 and one frame must show the last output line of every VIC line at 25 % (each channel >> 2).
// +mut=N, the negative controls (each run must FAIL): 1 the align line 10 lines early (a pixel read before the VIC wrote it), 2 the
// align line 70 lines late (the ring overtaken), 3 the horizontal step hd one larger (wrong pixels, window size).
`timescale 1ns/1fs
module tb;
`include "hdmi_modes.vh"
    localparam X0 = 96, Y0 = 16, Y0_60 = 31;      // vic_scaler.v, tools/hdmi_modes.py
    integer m, sc, sc2, extra, NF, mut;
    integer fam, XMAX, YMAX, N, M, g, ha, va, htot, vtot, xa0, ya0, wln;
    real    dot4x, ft, hv, hp;
    reg vclk = 0, clk = 0;
    initial begin
        if (!$value$plusargs("mode=%d", m)) m = 2;
        if (!$value$plusargs("sc=%d", sc)) sc = 1;
        if (!$value$plusargs("sc2=%d", sc2)) sc2 = -1;
        extra = $test$plusargs("extra");
        if (!$value$plusargs("nf=%d", NF)) NF = 2;
        if (!$value$plusargs("mut=%d", mut)) mut = 0;
        fam = m >= 6;
        dot4x = fam ? 50.0e6 * 47.125 / 3.0 / 24.0 : 50.0e6 * 56.75 / 3.0 / 30.0;   // NTSC / PAL (mode_ctl.v)
        case (m)        // the CDCE's N / M per row (hdmi_modes.vh hm_cdce_lock, tools/hdmi_modes.py)
        0: begin N = 25;  M = 7;   end
        1, 2: begin N = 55; M = 14; end
        6: begin N = 16;  M = 5;   end
        7, 8: begin N = 990; M = 263; end
        default: begin N = 78; M = 25; end
        endcase
        ft = dot4x * 125.0 / 104.0 * N / M;
        hv = 1.0e9 / dot4x / 2.0;
        hp = 1.0e9 / ft / 2.0;
        XMAX = fam ? 519 : 503; YMAX = fam ? 262 : 311; wln = fam ? 240 : 270;
    end
    always #(hv) vclk = ~vclk;
    always #(hp) clk = ~clk;

    // VIC: one dot per 4 vclk
    reg [1:0] ph = 0; reg [9:0] x = 0; reg [8:0] y = 0;
    always @(posedge vclk) begin
        ph <= ph + 1;
        if (ph == 3) begin
            if (x == XMAX) begin x <= 0; y <= (y == YMAX) ? 0 : y + 1; end
            else x <= x + 1;
        end
    end
    wire [3:0] pix = x[3:0] ^ y[3:0];
    // the ring's content as the bench sees it: the raster line last written into each slot (slot = the window row, the raster line
    // - Y0 modulo the frame, mod 64, as vic_scaler.v writes it)
    integer wl [0:32767];
    integer wi, wrow;
    initial for (wi = 0; wi < 32768; wi = wi + 1) wl[wi] = -1;
    always @(posedge vclk) begin
        wrow = fam ? (y + 263 - Y0_60) % 263 : (y + 312 - Y0) % 312;
        wl[{wrow[5:0], x[8:0]}] = y;
    end

    // palette port (T59)
    reg pclk = 0;
    always #6.1 pclk = ~pclk;
    reg        pal_we = 0;
    reg  [5:0] pal_addr = 0;
    reg  [7:0] pal_wdata = 0;
    reg [23:0] epal [0:15];
    integer pi;
    initial begin
        epal[0]=24'h000000; epal[1]=24'hFFFFFF; epal[2]=24'h813338; epal[3]=24'h75CEC8;
        epal[4]=24'h8E3C97; epal[5]=24'h56AC4D; epal[6]=24'h2E2C9B; epal[7]=24'hEDF171;
        epal[8]=24'h8E5029; epal[9]=24'h553800; epal[10]=24'hC46C71; epal[11]=24'h4A4A4A;
        epal[12]=24'h7B7B7B; epal[13]=24'hA9FF9F; epal[14]=24'h706DEB; epal[15]=24'hB2B2B2;
    end
    task padd(input [5:0] a, input [7:0] d);
        begin @(posedge pclk); pal_we <= 1; pal_addr <= a; pal_wdata <= d; @(posedge pclk); pal_we <= 0; end
    endtask
    task write_palette;
        reg [23:0] c;
        begin
            for (pi = 0; pi < 16; pi = pi + 1) begin
                c = {8'(pi * 16 + 5), 8'(255 - pi * 13), 8'(pi * 7 + 100)};
                epal[pi] = c;
                padd(pi * 4 + 0, c[23:16]); padd(pi * 4 + 1, c[15:8]); padd(pi * 4 + 2, c[7:0]); padd(pi * 4 + 3, 8'hFF);
            end
        end
    endtask

    // ---- the DUTs
    reg rst = 1, scan = 0;
    reg [3:0] mode = 0;
    reg [1:0] scale = 1;
    wire ce, t_de, t_hs, t_vs, running, aligned, align, locked;
    wire [23:0] t_rgb, rgb;
    wire [11:0] t_cx, hact, xact0, htot_m1, cx;
    wire [10:0] t_cy, vact, yact0, vtot_m1, cy;
    wire [3:0]  gear, mode_q;
    wire de, hs, vs;
    wire [81:0] geom;
    hdmi_timing tim (.clk(clk), .rst(rst), .mode(mode), .align(align), .valid(), .aligned(aligned), .ce(ce), .rgb(t_rgb),
        .de(t_de), .hs(t_hs), .vs(t_vs), .cx(t_cx), .cy(t_cy), .running(running), .hact(hact), .vact(vact), .xact0(xact0),
        .yact0(yact0), .gear(gear), .htot_m1(htot_m1), .vtot_m1(vtot_m1), .vic(), .rate(), .sx_e(), .fw_m1(), .sy_e(), .ey_e(),
        .ph_e(), .mode_q(mode_q));
    vic_scaler dut (.vclk(vclk), .pix(pix), .px(x), .py(y), .clk(clk), .rst(rst), .ce(ce),
        .pal_clk(pclk), .pal_we(pal_we), .pal_addr(pal_addr), .pal_wdata(pal_wdata),
        .mode(mode_q), .scale(scale), .t_de(t_de), .t_hs(t_hs), .t_vs(t_vs), .t_cx(t_cx), .t_cy(t_cy),
        .xact0(xact0), .yact0(yact0), .hact(hact), .vact(vact), .htot_m1(htot_m1), .geom(geom), .align(align),
        .rgb(rgb), .de(de), .hs(hs), .vs(vs), .cx(cx), .cy(cy), .locked(locked), .blank(1'b0), .x_text(), .scan(scan));

    // ---- the expected geometry of the scale in use (from hm_geom, as the tool wrote it)
    integer hn, hd, vn, vd, xw0, yw0, ww, wh, fails = 0;
    task set_geom(input integer s);
        reg [81:0] gg;
        begin
            gg = hm_geom(m, s);
            hn = gg[81:76]; hd = gg[75:70]; vn = gg[69:64]; vd = gg[63:58]; xw0 = gg[45:34]; yw0 = gg[33:23];
            ww = (400 * hd + hn - 1) / hn; wh = (wln * vd + vn - 1) / vn;
        end
    endtask
    task fail(input [8*64-1:0] what, input integer got, input integer want);
        begin
            fails = fails + 1;
            if (fails <= 20) $display("FAIL mode %0d scale %0d: %0s got %0d want %0d", m, scale, what, got, want);
        end
    endtask
    // expected for the output pixel at (ox, oy) of the active area: in the window?, the VIC raster x / line
    function in_win(input integer ox, input integer oy);
        in_win = ox >= xw0 && ox < xw0 + ww && oy >= yw0 && oy < yw0 + wh;
    endfunction
    function integer ex_of(input integer ox); ex_of = X0 + ((ox - xw0) * hn) / hd; endfunction
    function integer ey_of(input integer oy);
        integer r;
        begin
            r = (fam ? Y0_60 : Y0) + ((oy - yw0) * vn) / vd;
            ey_of = (fam && r >= 263) ? r - 263 : r;
        end
    endfunction
    function last_of(input integer oy);     // the last output line of its VIC line
        last_of = (((oy + 1 - yw0) * vn) / vd) != (((oy - yw0) * vn) / vd);
    endfunction

    // ---- checks, while `chk`: stage 1 (address and ring) at every ce, the output at every ce
    reg chk = 0, scan_chk = 0, started = 0;
    integer clocks, fclocks, wpix, npix, ox, oy, ex, ey, rrow, ral, frames = 0, aligns = 0;
    reg [23:0] want_rgb, c;
    real tf0, tfr, vf;
    always @(posedge clk) if (chk) begin
        clocks = clocks + 1;
        if (align) aligns = aligns + 1;
        if (!locked) fail("lock lost", 0, 1);
        if (ce) begin
            // stage 1: the read of the block RAM at this edge
            // (from the first output frame start on: before it the pipeline may still hold pixels of the unaligned timing)
            ox = dut.s1_h - xact0; oy = dut.s1_v - yact0;
            if (!started) ;
            else if (dut.s1_ctl[2] && in_win(ox, oy)) begin
                if (!dut.s1_win) fail("stage 1 not in the window", ox * 10000 + oy, 1);
                ex = ex_of(ox); ey = ey_of(oy);
                rrow = ((oy - yw0) * vn) / vd;
                if (dut.s1_addr !== {rrow[5:0], ex[8:0]}) fail("ring address", dut.s1_addr, {rrow[5:0], ex[8:0]});
                ral = wl[dut.s1_addr];
                if (ral != ey) fail("the ring slot holds another raster line (read before the VIC wrote it, or overtaken)", ral, ey);
            end else if (dut.s1_win) fail("stage 1 window outside the window", ox * 10000 + oy, 0);
            // output
            ox = cx - xact0; oy = cy - yact0;
            if (de && in_win(ox, oy)) begin
                c = epal[ex_of(ox) % 16 ^ ey_of(oy) % 16];
                want_rgb = (scan_chk && last_of(oy)) ? {2'b00, c[23:18], 2'b00, c[15:10], 2'b00, c[7:2]} : c;
                wpix = wpix + 1;
            end else want_rgb = 24'h000000;
            if (started && rgb !== want_rgb) fail("rgb", rgb, want_rgb);
            if (de) npix = npix + 1;
            if (cx == 0 && cy == 0) begin          // the first pixel of the next frame: the last one is complete
                if (started) begin
                    fclocks = clocks - 1;
                    tfr = $realtime - tf0;
                    frames = frames + 1;
                    if (fclocks != g * htot * vtot) fail("pixel clocks per frame", fclocks, g * htot * vtot);
                    if (tfr - vf > 2.0 * hp || vf - tfr > 2.0 * hp) fail("frame time - VIC frame time (ps)", (tfr - vf) * 1000, 0);
                    if (wpix != ww * wh) fail("window pixels per frame", wpix, ww * wh);
                    if (npix != ha * va) fail("active pixels per frame", npix, ha * va);
                end
                started = 1; clocks = 1; tf0 = $realtime; wpix = 0; npix = 0;
            end
        end
    end
    // start checking at an output frame start (the counters at the output: cx = 0, cy = 0)
    task check_frames(input integer n);   // from the next output frame start (the checker's first (0, 0) starts the count)
        begin
            @(negedge clk);
            clocks = 0; wpix = 0; npix = 0; frames = 0; aligns = 0; started = 0; chk = 1;
            while (frames < n) @(posedge clk);
            chk = 0;
            if (aligns != 0) fail("align asserted while locked", aligns, 0);
        end
    endtask
    // the scale changes at a frame start (the timing's counters: cy = 0, cx = 0), as mode_req.v's fstart
    task change_scale(input integer s);
        begin
            @(posedge clk); while (!(ce && t_cx == 0 && t_cy == 0)) @(posedge clk);
            scale <= s;
        end
    endtask

    integer t, relocked, a1, a2;
    initial begin
        #1;
        mode = m; scale = sc;
        set_geom(sc);
        vf = 63.0 * 312.0 * 32.0 * 2.0 * hv; if (fam) vf = 65.0 * 263.0 * 32.0 * 2.0 * hv;
        if (mut == 1 || mut == 2) begin : ctl
            integer al;
            al = hm_align(m, sc) + (mut == 1 ? -10 : 70);
            al = (al + (fam ? 263 : 312)) % (fam ? 263 : 312);
            force dut.al_v = al; force dut.al_want = al;
        end
        if (mut == 3) begin : ctl3
            reg [81:0] gm;
            gm = hm_geom(m, sc); gm[75:70] = gm[75:70] + 6'd1;
            repeat (3) @(posedge clk);
            force dut.geom = gm;
        end
        repeat (20) @(posedge clk); rst = 0;
        wait (running);
        g = gear; ha = hact; va = vact; htot = htot_m1 + 1; vtot = vtot_m1 + 1; xa0 = xact0; ya0 = yact0;
        if (xw0 + ww > ha || yw0 + wh > va) fail("window not inside the active area", (xw0 + ww) * 10000 + yw0 + wh, ha * 10000 + va);
        t = 0;
        while (!locked && t < 3 * g * htot * vtot) begin @(posedge clk); t = t + 1; end
        if (!locked) begin fail("no lock within 3 frames", 0, 1); $display("FAIL scaler"); $finish; end
        check_frames(NF);
        $display("mode %0d (%0dx%0d, %0dx%0d x%0d) scale %0d: window %0dx%0d at (%0d, %0d), steps %0d/%0d %0d/%0d, align line %0d, %0d frames checked",
                 m, ha, va, htot, vtot, g, sc, ww, wh, xw0, yw0, hn, hd, vn, vd, hm_align(m, sc), NF);
        if (sc2 >= 0) begin
            a1 = hm_align(m, sc); a2 = hm_align(m, sc2);
            change_scale(sc2);
            set_geom(sc2);
            relocked = 0; t = 0;
            if (a1 != a2) begin
                while (locked && t < 1000) begin @(posedge clk); t = t + 1; end
                if (locked) fail("the lock held across a change of the align line", a1, a2);
                t = 0;
                while (!locked && t < 2 * g * htot * vtot) begin @(posedge clk); t = t + 1; end
                if (!locked) fail("no lock again within 2 frames after the scale change", 0, 1);
            end else begin
                while (t < 1000) begin @(posedge clk); t = t + 1; if (!locked) relocked = 1; end
                if (relocked) fail("the lock dropped though the align line stayed", a1, a2);
            end
            check_frames(NF);
            $display("scale change %0d -> %0d (align line %0d -> %0d): %0s, window %0dx%0d at (%0d, %0d), %0d frames checked",
                     sc, sc2, a1, a2, a1 != a2 ? "aligned again" : "lock held", ww, wh, xw0, yw0, NF);
        end
        if (extra) begin
            // T59: a new palette, written while the output is in its vertical blanking
            @(posedge clk); while (!(cy == 0 && cx == 0)) @(posedge clk);
            write_palette;
            check_frames(1);
            // T62 G09
            scan = 1; repeat (10) @(posedge clk); scan_chk = 1;
            check_frames(1);
            scan_chk = 0;
            $display("palette (T59) and scan lines (T62 G09): 1 frame each checked");
        end
        if (fails == 0) $display("PASS scaler mode %0d scale %0d%0s%0s", m, sc, sc2 >= 0 ? " +change" : "", extra ? " +palette +scan" : "");
        else $display("FAIL scaler mode %0d scale %0d: %0d checks", m, sc, fails);
        $finish;
    end
endmodule
