// T164 WP2 bench (S2): host/hdmi_timing.v, each of the 9 modes of the table (family * 6 + hdmi; family 0 rows 3..5 do not exist). For one
// full frame it counts, from the outputs only:
// clocks per frame (htot x vtot x gear), pixel enables (htot x vtot), active pixels (hact x vact), the position and width of the
// hsync pulse and of the vsync pulse and their polarity, the first active pixel and line, that every pixel value is held for gear
// clocks, and the frame rate rate/clocks against the table of tools/hdmi_modes.py (0.1 %). The expected geometry is written out
// here from Gideon's table (hdmi_scan.cc), not read from hdmi_modes.vh. Then: the modes that are not in the table (3, 4, 5, 12..15; T188 edition: 3, 4, 5, 15) are
// refused (valid 0, running and ce stay 0); the align pulse re-phases the frame to the active origin on the two locked rows (1080p) only,
// on every other row the sequence goes on undisturbed.
`timescale 1ns/1ps
module tb;
    reg clk = 0;
    always #3 clk = ~clk;
    reg rst = 1; reg [3:0] mode = 0;
    wire ce, de, hs, vs, running, valid, aligned; reg align = 0;
    wire [23:0] rgb; wire [11:0] cx, hact, xact0; wire [10:0] cy, vact, yact0; wire [3:0] gear; wire [6:0] vic;
    wire [31:0] rate; wire [12:0] sx_e, fw_m1; wire [10:0] sy_e, ey_e; wire [2:0] ph_e;
    hdmi_timing dut (.clk(clk), .rst(rst), .mode(mode), .align(align), .valid(valid), .aligned(aligned), .ce(ce), .rgb(rgb), .de(de), .hs(hs), .vs(vs), .cx(cx), .cy(cy),
        .running(running), .hact(hact), .vact(vact), .xact0(xact0), .yact0(yact0), .gear(gear), .vic(vic), .rate(rate),
        .sx_e(sx_e), .fw_m1(fw_m1), .sy_e(sy_e), .ey_e(ey_e), .ph_e(ph_e));

    // expected: vic, gear, hact, hfp, hsync, hbp, hpol, vact, vfp, vsync, vbp, vpol, fps*1000
    integer e [0:14][0:12];
    initial begin
        // fam 0: 50 Hz (rows 3..5 do not exist)
        e[0][0]=17; e[0][1]=5; e[0][2]=720;  e[0][3]=12;  e[0][4]=64;  e[0][5]=68;  e[0][6]=0; e[0][7]=576;  e[0][8]=5;  e[0][9]=5; e[0][10]=39; e[0][11]=0; e[0][12]=50124;
        e[1][0]=19; e[1][1]=2; e[1][2]=1280; e[1][3]=440; e[1][4]=40;  e[1][5]=220; e[1][6]=1; e[1][7]=720;  e[1][8]=5;  e[1][9]=5; e[1][10]=20; e[1][11]=1; e[1][12]=50124;
        e[2][0]=31; e[2][1]=1; e[2][2]=1920; e[2][3]=528; e[2][4]=44;  e[2][5]=148; e[2][6]=1; e[2][7]=1080; e[2][8]=4;  e[2][9]=5; e[2][10]=36; e[2][11]=1; e[2][12]=50124;
        // fam 1: 60 Hz
        e[6][0]=1;  e[6][1]=5; e[6][2]=640;  e[6][3]=16;  e[6][4]=96;  e[6][5]=48;  e[6][6]=0; e[6][7]=480;  e[6][8]=10; e[6][9]=2; e[6][10]=33; e[6][11]=0; e[6][12]=59822;
        e[7][0]=4;  e[7][1]=2; e[7][2]=1280; e[7][3]=110; e[7][4]=40;  e[7][5]=220; e[7][6]=1; e[7][7]=720;  e[7][8]=5;  e[7][9]=5; e[7][10]=20; e[7][11]=1; e[7][12]=59827;
        e[8][0]=16; e[8][1]=1; e[8][2]=1920; e[8][3]=88;  e[8][4]=44;  e[8][5]=148; e[8][6]=1; e[8][7]=1080; e[8][8]=4;  e[8][9]=5; e[8][10]=36; e[8][11]=1; e[8][12]=59827;
        e[9][0]=0;  e[9][1]=3; e[9][2]=800;  e[9][3]=40;  e[9][4]=124; e[9][5]=88;  e[9][6]=1; e[9][7]=600;  e[9][8]=20; e[9][9]=5; e[9][10]=25; e[9][11]=1; e[9][12]=60035;
        e[10][0]=0; e[10][1]=2; e[10][2]=1024; e[10][3]=24;  e[10][4]=136; e[10][5]=160; e[10][6]=0; e[10][7]=768;  e[10][8]=3;  e[10][9]=6; e[10][10]=29; e[10][11]=0; e[10][12]=60000;
        e[11][0]=0; e[11][1]=1; e[11][2]=1280; e[11][3]=48;  e[11][4]=112; e[11][5]=248; e[11][6]=1; e[11][7]=1024; e[11][8]=1;  e[11][9]=3; e[11][10]=24; e[11][11]=1; e[11][12]=60001;
`ifdef T188
        // T234: rows 12..14 are the host's own 50 Hz television rows (Frame follow Off), written out here as for the host (rows 0..2 above, before the T188 edit)
        e[12][0]=17; e[12][1]=5; e[12][2]=720;  e[12][3]=12;  e[12][4]=64; e[12][5]=68;  e[12][6]=0; e[12][7]=576;  e[12][8]=5; e[12][9]=5; e[12][10]=39; e[12][11]=0; e[12][12]=50124;
        e[13][0]=19; e[13][1]=2; e[13][2]=1280; e[13][3]=440; e[13][4]=40; e[13][5]=220; e[13][6]=1; e[13][7]=720;  e[13][8]=5; e[13][9]=5; e[13][10]=20; e[13][11]=1; e[13][12]=50124;
        e[14][0]=31; e[14][1]=1; e[14][2]=1920; e[14][3]=528; e[14][4]=44; e[14][5]=148; e[14][6]=1; e[14][7]=1080; e[14][8]=4; e[14][9]=5; e[14][10]=36; e[14][11]=1; e[14][12]=50124;
        // T188 D4: the Amiga copies (cores/amiga*/mono/host) hold rows 0..2 bent to the 313-line Amiga frame, 49.920 Hz (tools/hdmi_modes.py amiga-gen)
        e[0][3]=15;  e[0][12]=49920;
        e[1][3]=438; e[1][8]=4; e[1][12]=49920;
        e[2][3]=526; e[2][12]=49920;
`endif
    end

    integer rstn;
    integer nref = 0;
`ifdef T188
    localparam NROWS = 15;           // T234: 12 rows + the host's three 50 Hz rows 12..14
`else
    localparam NROWS = 12;
`endif
    integer nmodes = 0, fails = 0, m, htot, vtot, g, only;
    // the mode under test, as scalars (the monitor runs every clock)
    integer x_ha, x_hf, x_hs, x_hb, x_hp, x_va, x_vf, x_vs, x_vb, x_vp, x_vic, x_fps;
    integer clk_cnt, t0, last_t, pixels, des, bad_hold, bad_ph, bad_pos, bad_de, bad_hs, bad_vs, hs_max, e_cx, e_cy, frame_clocks;
    reg ce_q, mon, seen, frame_done;
    wire e_de = (cx >= x_hf + x_hs + x_hb) && (cy >= x_vf + x_vs + x_vb);
    wire e_hs = (cx >= x_hf && cx < x_hf + x_hs) ? x_hp[0] : ~x_hp[0];
    wire e_vs = (cy >= x_vf && cy < x_vf + x_vs) ? x_vp[0] : ~x_vp[0];
    always @(posedge clk) begin
        ce_q <= ce;
        if (mon) begin
            clk_cnt = clk_cnt + 1;
            if (ce_q) begin                         // the pixel registers changed at the last edge
                if (cx == 0 && cy == 0) begin
                    if (seen) begin frame_done = 1; frame_clocks = clk_cnt - t0; mon = 0; end
                    else begin seen = 1; t0 = clk_cnt; last_t = clk_cnt - g; e_cx = 0; e_cy = 0; end
                end
                if (seen && mon) begin
                    if (clk_cnt - last_t != g) bad_hold = bad_hold + 1;
                    last_t = clk_cnt;
                    pixels = pixels + 1;
                    if (cx != e_cx || cy != e_cy) bad_pos = bad_pos + 1;
                    e_cx = (e_cx == htot - 1) ? 0 : e_cx + 1;
                    if (e_cx == 0) e_cy = (e_cy == vtot - 1) ? 0 : e_cy + 1;
                    if (de) des = des + 1;
                    if (de !== e_de) bad_de = bad_de + 1;
                    if (hs !== e_hs) bad_hs = bad_hs + 1;
                    if (vs !== e_vs) bad_vs = bad_vs + 1;
                end
            end
            if (seen && !frame_done && ph_e !== clk_cnt - last_t) bad_ph = bad_ph + 1;       // the encoder's phase: clocks since the pixel registers changed
        end
    end

    // vtot_m1 must be valid when the generator first runs on it (pre-NBA values at the first edge with running = 1). Only checked when the
    // DUT has a vtot_m1 (all copies have); a control starts running before the register has settled.
    reg run_q = 0; integer vt_bad = 0;
    always @(posedge clk) begin
        run_q <= running;
        if (running && !run_q && dut.vtot_m1 !== vtot - 1) vt_bad = vt_bad + 1;
    end
    task fail(input [255:0] what, input integer got, input integer want);
        begin fails = fails + 1; $display("FAIL mode %0d: %0s got %0d want %0d", m, what, got, want); end
    endtask

    // ---- align: a pulse in the middle of the frame (cx 500, cy 300). A locked row (hm_locked: the 1080p rows) restarts at the active origin:
    // the pixel after (501, 300) is (xact0, yact0), then the frame goes on; every other row ignores it. Sampled at negedge clk.
    integer a_n, a_ecx, a_ecy, a_seen0, a_lock, a_bad, a_hold;
    reg ce_last = 0, ce_p_get = 0;
    task step; begin @(negedge clk); ce_p_get = ce_last; ce_last = ce; end endtask   // ce_p_get: the pixel registers changed at the last posedge
    task align_test;
        begin
            a_lock = (m == 2 || m == 8 || m == 14);
            step; step;
            while (!(ce_p_get && cx == 500 && cy == 300)) step;
            if (aligned !== 1'b0) fail("aligned before the pulse", aligned, 0);
            align = 1; a_hold = 0;               // the pixel registered at the next posedge is still (501, 300)
            a_ecx = 501; a_ecy = 300; a_n = 0; a_seen0 = 0; a_bad = 0;
            while (a_n < htot * vtot + 2) begin
                step; a_hold = a_hold + 1; if (a_hold >= g) align = 0;     // high for gear clocks: covers a ce edge on every row
                if (ce_p_get) begin                              // a new pixel at the last posedge
                    if (cx != a_ecx || cy != a_ecy) a_bad = a_bad + 1;
                    if (de !== ((cx >= x_hf + x_hs + x_hb) && (cy >= x_vf + x_vs + x_vb))) a_bad = a_bad + 1;
                    if (hs !== ((cx >= x_hf && cx < x_hf + x_hs) ? x_hp[0] : ~x_hp[0])) a_bad = a_bad + 1;
                    if (vs !== ((cy >= x_vf && cy < x_vf + x_vs) ? x_vp[0] : ~x_vp[0])) a_bad = a_bad + 1;
                    if (cx == 0 && cy == 0) a_seen0 = a_seen0 + 1;
                    a_n = a_n + 1;
                    if (a_n == 1 && a_lock) begin a_ecx = x_hf + x_hs + x_hb; a_ecy = x_vf + x_vs + x_vb; end
                    else begin
                        a_ecx = (a_ecx == htot - 1) ? 0 : a_ecx + 1;
                        if (a_ecx == 0) a_ecy = (a_ecy == vtot - 1) ? 0 : a_ecy + 1;
                    end
                end
            end
            if (a_bad != 0) fail("align: pixels off the expected sequence / de / sync", a_bad, 0);
            if (aligned !== a_lock[0]) fail("aligned flag", aligned, a_lock);
            if (a_seen0 == 0) fail("align: the frame never wrapped to (0, 0) again", 0, 1);
        end
    endtask

    // ---- a mode that is not in the table: refused, nothing runs
    integer r_n, r_ce, r_run;
    task refuse_test;
        begin
            mode = m; rst = 1; repeat (6) @(posedge clk); rst = 0;
            r_ce = 0; r_run = 0;
            for (r_n = 0; r_n < 600; r_n = r_n + 1) begin @(posedge clk); if (ce) r_ce = r_ce + 1; if (running) r_run = r_run + 1; end
            if (valid !== 1'b0) fail("valid of a mode that is not in the table", valid, 0);
            if (r_run != 0 || r_ce != 0) fail("a refused mode ran (running / ce clocks)", r_run + r_ce, 0);
        end
    endtask

    initial begin
        mon = 0; ce_q = 0; seen = 0; frame_done = 0;
        if (!$value$plusargs("only=%d", only)) only = -1;
        if (!$value$plusargs("rst=%d", rstn)) rstn = 6;
        for (m = 0; m < NROWS; m = m + 1) if ((only < 0 || only == m) && (m < 3 || (m > 5 && m < 12) || m >= 12 && NROWS > 12)) begin
            nmodes = nmodes + 1;
            x_vic = e[m][0]; g = e[m][1]; x_ha = e[m][2]; x_hf = e[m][3]; x_hs = e[m][4]; x_hb = e[m][5]; x_hp = e[m][6];
            x_va = e[m][7]; x_vf = e[m][8]; x_vs = e[m][9]; x_vb = e[m][10]; x_vp = e[m][11]; x_fps = e[m][12];
            htot = x_ha + x_hf + x_hs + x_hb; vtot = x_va + x_vf + x_vs + x_vb;
            mode = m; rst = 1; repeat (rstn) @(posedge clk); rst = 0;
            wait (running);
            clk_cnt = 0; t0 = 0; last_t = 0; pixels = 0; des = 0; bad_hold = 0; bad_ph = 0; bad_pos = 0; bad_de = 0; bad_hs = 0; bad_vs = 0;
            seen = 0; frame_done = 0; mon = 1;
            wait (frame_done);
            if (pixels != htot * vtot) fail("pixels per frame", pixels, htot * vtot);
            if (frame_clocks != htot * vtot * g) fail("clocks per frame", frame_clocks, htot * vtot * g);
            if (des != x_ha * x_va) fail("active pixels", des, x_ha * x_va);
            if (bad_hold != 0) fail("pixels not held for gear clocks", bad_hold, 0);
            if (bad_ph != 0) fail("clocks with a wrong encoder phase ph_e", bad_ph, 0);
            if (bad_pos != 0) fail("pixels with a wrong cx / cy", bad_pos, 0);
            if (bad_de != 0) fail("pixels with a wrong de", bad_de, 0);
            if (bad_hs != 0) fail("pixels with a wrong hsync (position, width or polarity)", bad_hs, 0);
            if (bad_vs != 0) fail("pixels with a wrong vsync (position, width or polarity)", bad_vs, 0);
            if (vt_bad != 0) fail("vtot_m1 not valid when the generator started", vt_bad, 0);
            if (gear != g) fail("gear", gear, g);
            if (vic != x_vic) fail("vic", vic, x_vic);
            if (sx_e != g * (x_hf + x_hs + x_hb)) fail("sx_e", sx_e, g * (x_hf + x_hs + x_hb));
            if (fw_m1 != g * htot - 1) fail("fw_m1", fw_m1, g * htot - 1);
            if (sy_e != x_vf + x_vs + x_vb) fail("sy_e", sy_e, x_vf + x_vs + x_vb);
            if (ey_e != vtot) fail("ey_e", ey_e, vtot);
            begin : fr
                integer fps1000;
                fps1000 = (rate * 1000.0) / (frame_clocks / g);     // rate is the pixel clock
                if ((fps1000 - x_fps) > x_fps / 1000 || (x_fps - fps1000) > x_fps / 1000) fail("frame rate (mHz)", fps1000, x_fps);
                $display("mode %0d: %0d x %0d x%0d = %0d clocks, %0d.%03d Hz (table %0d.%03d), pixel clock %0d Hz", m, htot, vtot, g, frame_clocks,
                         fps1000 / 1000, fps1000 % 1000, x_fps / 1000, x_fps % 1000, rate);
            end
            align_test;
        end
        if (only < 0 || only == 99) for (m = 3; m < 16; m = m + 1) if (m < 6 || (m > 11 && m >= NROWS)) begin nref = nref + 1; refuse_test; end
        if (fails == 0) $display("PASS hdmi_timing: %0d modes, %0d refused", nmodes, nref);
        else $display("FAIL hdmi_timing: %0d checks", fails);
        $finish;
    end
endmodule
