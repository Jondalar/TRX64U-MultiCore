// T201 S2: every knob register of the A/V encoder (diag_regs.v 050-05F) changes the words as specified, live (no reset is ever applied, the registers are written through the
// register bus while the picture runs). Units 0-7 are one diag_regs + diag_avout each, written once at clock 50 and measured over the first picture lines: unit 0 is the reset
// state (PAL, the C128's levels), 1 V sign, 2 burst amplitude, 3 chroma gain, 4 sync level, 5 blank level, 6 white level, 7 burst window, 9 the freeze bit (053 bit 1). Unit 8 walks standard, group order, bit order and
// trim while it runs: line and frame length of all four standards, the subcarrier constants, the six group orders against the encoder words, the eight bit orders, the trim in 31 Hz steps.
`timescale 1ns / 1ps
module unit (input clk, input we, input re, input [11:0] a, input [7:0] d, output [7:0] q, output [7:0] ga, gb, gc);
    wire [92:0] cfg;
    wire ack, irq;
    diag_regs r (.clk(clk), .we(we), .re(re), .addr({5'd0, a}), .wdata(d), .rdata(q), .ack(ack), .irq(irq), .build(32'd0), .px_we(), .px_re(), .px_rdata(8'd0),
        .ev_vsync(1'b0), .ev_flip(1'b0), .ev_eng(1'b0), .run(), .abort(), .go(), .fifo_clr(), .st_we(), .st_addr(), .st_wdata(), .status(8'd0), .pc(8'd0), .ref_pc(8'd0), .ref_cnt(8'd0),
        .fifo_q(32'd0), .fifo_level(10'd0), .fifo_rd(), .live(128'd0), .drv_en(128'd0), .guard_now(1'b0), .cap_rst(), .e_we(1'b0), .e_off(12'd0), .e_wd(8'd0), .e_wack(), .cap_sel(), .cap_st(8'd0), .cap_count(32'd0), .cap_lost(32'd0), .cap_lines(32'd0), .rd_start(), .rd_line_ix(), .rd_valid_i(1'b0), .rd_data(128'd0), .av_cfg(cfg));
    diag_avout o (.clk(clk), .av_cfg(cfg), .ga(ga), .gb(gb), .gc(gc));
endmodule

module tb_avregs;
    reg clk = 0;
    always #5 clk = ~clk;
    reg we = 0, re = 0;
    reg [11:0] a = 0;
    reg [7:0] d = 0;
    reg [9:0] sel = 10'd0;
    wire [7:0] q [0:9];
    wire [7:0] ga [0:9], gb [0:9], gc [0:9];
    genvar g;
    generate for (g = 0; g < 10; g = g + 1) begin : u
        unit un (.clk(clk), .we(we & sel[g]), .re(re & sel[g]), .a(a), .d(d), .q(q[g]), .ga(ga[g]), .gb(gb[g]), .gc(gc[g]));
    end endgenerate
    integer errors = 0, checks = 0;
    task check(input c, input [8*70-1:0] msg); begin checks = checks + 1; if (!c) begin errors = errors + 1; $display("FAIL %0s", msg); end end endtask
    task wr(input [9:0] s, input [11:0] ad, input [7:0] dt);
        begin @(posedge clk); sel <= s; a <= ad; d <= dt; we <= 1; @(posedge clk); we <= 0; sel <= 0; end
    endtask
    reg [7:0] rv;
    task rdb(input integer un, input [11:0] ad);
        begin @(posedge clk); sel <= 10'd1 << un; a <= ad; re <= 1; @(posedge clk); re <= 0; @(posedge clk); @(posedge clk); rv = q[un]; sel <= 0; end
    endtask

    // ---- metrics of units 0..7 over the first lines ----
    integer sumc [0:7], devpic [0:7], devburst [0:7], nburst [0:7], maxy [0:7], miny [0:7], blky [0:7];
    integer n = 0;
    function integer cdev(input [7:0] c); cdev = (c > 8'd128) ? (c - 128) : (128 - c); endfunction
    generate for (g = 0; g < 8; g = g + 1) begin : m
        reg b1 = 0, b2 = 0;
        initial begin sumc[g] = 0; devpic[g] = 0; devburst[g] = 0; nburst[g] = 0; maxy[g] = 0; miny[g] = 255; blky[g] = -1; end
        always @(posedge clk) begin
            b1 <= u[g].un.o.enc.ph3_burst; b2 <= b1;
            if (n > 100 && n < 150000) begin
                if (u[g].un.o.vpos == 9'd40) begin
                    sumc[g] = sumc[g] + u[g].un.o.c_w;
                    if (u[g].un.o.enc.ph2_burst) nburst[g] = nburst[g] + 1;
                    if (u[g].un.o.hx == 10'd72 && u[g].un.o.dv == 2'd0) blky[g] = u[g].un.o.y_w;
                end
                if (u[g].un.o.vpos >= 9'd20 && u[g].un.o.vpos <= 9'd100 && u[g].un.o.hx >= 10'd110 && u[g].un.o.hx <= 10'd480) begin
                    if (cdev(u[g].un.o.c_w) > devpic[g]) devpic[g] = cdev(u[g].un.o.c_w);
                    if (u[g].un.o.y_w > maxy[g]) maxy[g] = u[g].un.o.y_w;
                end
                if (u[g].un.o.enc.ph2_burst || u[g].un.o.enc.ph3_burst || b1 || b2) begin
                    if (cdev(u[g].un.o.c_w) > devburst[g]) devburst[g] = cdev(u[g].un.o.c_w);
                end
                if (u[g].un.o.y_w < miny[g]) miny[g] = u[g].un.o.y_w;
            end
        end
    end endgenerate
    always @(posedge clk) n <= n + 1;

    // the marker position of unit 9
    reg [9:0] mx_a;


    // ---- unit 8: the walk ----
    integer t0, t1, k, ok;
    reg [7:0] y8, c8, cv8, ey, ec, ecv;
    wire [7:0] y_w8 = u[8].un.o.y_w, c_w8 = u[8].un.o.c_w, cv_w8 = u[8].un.o.cv_w;
    reg [7:0] py, pc, pcv;
    always @(posedge clk) begin py <= y_w8; pc <= c_w8; pcv <= cv_w8; end
    function [7:0] brv; input [7:0] x; brv = {x[0], x[1], x[2], x[3], x[4], x[5], x[6], x[7]}; endfunction
    task line_period(output integer p);
        integer s;
        begin
            @(posedge u[8].un.o.hs_r); s = n; @(posedge u[8].un.o.hs_r); @(posedge u[8].un.o.hs_r); p = n - s; p = p / 2;
        end
    endtask
    task frame_period(output integer p);
        integer s;
        begin
            @(posedge u[8].un.o.frame_tick); @(posedge u[8].un.o.frame_tick); s = n; @(posedge u[8].un.o.frame_tick); p = n - s;
        end
    endtask
    task orders(input integer perm);
        integer i, bad;
        reg [7:0] xa, xb, xc;
        begin
            bad = 0;
            for (i = 0; i < 3000; i = i + 1) begin
                @(posedge clk); #1;
                case (perm) 0: begin xa = py; xb = pc; xc = pcv; end 1: begin xa = py; xb = pcv; xc = pc; end 2: begin xa = pc; xb = py; xc = pcv; end
                            3: begin xa = pc; xb = pcv; xc = py; end 4: begin xa = pcv; xb = py; xc = pc; end default: begin xa = pcv; xb = pc; xc = py; end endcase
                if (ga[8] !== xa || gb[8] !== xb || gc[8] !== xc) bad = bad + 1;
            end
            check(bad == 0, "group order matches the encoder words");
        end
    endtask
    task revs(input integer r);
        integer i, bad;
        reg [7:0] xa, xb, xc;
        begin
            bad = 0;
            for (i = 0; i < 3000; i = i + 1) begin
                @(posedge clk); #1;
                xa = r[0] ? brv(pcv) : pcv; xb = r[1] ? brv(pc) : pc; xc = r[2] ? brv(py) : py;     // perm 5: A = CV, B = C, C = Y
                if (ga[8] !== xa || gb[8] !== xb || gc[8] !== xc) bad = bad + 1;
            end
            check(bad == 0, "bit order of each group");
        end
    endtask
    initial begin
        repeat (30) @(posedge clk);
        // the reset values read back
        rdb(0, 12'h050); check(rv == 8'd0, "STD resets to PAL");
        rdb(0, 12'h051); check(rv == 8'd5, "PERM resets to the C128's order");
        rdb(0, 12'h052); check(rv == 8'd0, "REV resets to 0");
        rdb(0, 12'h053); check(rv == 8'd1, "VINV resets to 1");
        rdb(0, 12'h055); check(rv == 8'd28, "BSTART resets to 28");
        rdb(0, 12'h056); check(rv == 8'd99, "BEND resets to 99");
        rdb(0, 12'h058); check(rv == 8'd32, "BAMP resets to 32");
        rdb(0, 12'h059); check(rv == 8'd16, "GAIN resets to 16");
        rdb(0, 12'h05A); check(rv == 8'd0, "SYNC resets to 0");
        rdb(0, 12'h05B); check(rv == 8'd77, "BLANK resets to 77");
        rdb(0, 12'h05C); check(rv == 8'd11, "WHITE resets to 11");
        rdb(0, 12'h006); check(rv == 8'h1F && rv[2], "DCAPS has the A/V bit (bit 2; T260: bits 3 and 4 are the pin bank and the capture)");
        // live writes, no reset
        wr(10'b0000000010, 12'h053, 8'd0);
        wr(10'b0000000100, 12'h058, 8'd16);
        wr(10'b0000001000, 12'h059, 8'd8);
        wr(10'b0000010000, 12'h05A, 8'd20);
        wr(10'b0000100000, 12'h05B, 8'd90);
        wr(10'b0001000000, 12'h05C, 8'd16);
        wr(10'b0010000000, 12'h055, 8'd40);
        wr(10'b0010000000, 12'h056, 8'd60);
        wr(10'b0010000000, 12'h057, 8'd0);
        rdb(1, 12'h053); check(rv == 8'd0, "VINV reads back");
        rdb(2, 12'h058); check(rv == 8'd16, "BAMP reads back");
        rdb(3, 12'h059); check(rv == 8'd8, "GAIN reads back");
        rdb(4, 12'h05A); check(rv == 8'd20, "SYNC reads back");
        rdb(5, 12'h05B); check(rv == 8'd90, "BLANK reads back");
        rdb(6, 12'h05C); check(rv == 8'd16, "WHITE reads back");
        rdb(7, 12'h055); check(rv == 8'd40, "BSTART reads back");
        rdb(7, 12'h056); check(rv == 8'd60, "BEND reads back");
        wr(10'b0100000000, 12'h051, 8'd7);
        rdb(8, 12'h051); check(rv == 8'd5, "PERM above 5 is held at 5");
        wr(11'b1000000000, 12'h053, 8'd3);
        rdb(9, 12'h053); check(rv == 8'd3, "FRZ reads back in bit 1");
        // wait until the metrics are complete (150000 clocks)
        wait (n > 150100);
        check(miny[0] == 0 && maxy[0] == 252 && blky[0] == 77, "unit 0: sync 0, white 252, blank 77");
        check(nburst[0] == 72, "unit 0: burst 28 .. 99 is 72 samples");
        check(devpic[0] > 20 && devburst[0] > 20, "unit 0: there is chroma and burst");
        check(sumc[1] != sumc[0] && maxy[1] == maxy[0] && miny[1] == miny[0] && blky[1] == blky[0], "V sign changes chroma, not luma");
        check(devburst[2] * 2 >= devburst[0] - 3 && devburst[2] * 2 <= devburst[0] + 3 && devpic[2] == devpic[0], "burst amplitude 16 halves the burst only");
        check(devpic[3] * 2 >= devpic[0] - 4 && devpic[3] * 2 <= devpic[0] + 4 && devburst[3] == devburst[0], "chroma gain 8 halves the picture chroma only");
        check(miny[4] == 20 && blky[4] == blky[0], "sync level 20");
        check(blky[5] == 90, "blank level 90");
        check(maxy[6] == 255 && miny[6] == 0, "white level 16 saturates at 255");
        check(nburst[7] == 21, "burst window 40 .. 60 is 21 samples");
        $display("metrics u0: miny %0d maxy %0d blk %0d burst %0d devpic %0d devburst %0d | u2 %0d | u3 %0d", miny[0], maxy[0], blky[0], nburst[0], devpic[0], devburst[0], devburst[2], devpic[3]);
        // standards: line and frame length, subcarrier
        wr(10'b0100000000, 12'h050, 8'd0); repeat (50) @(posedge clk);
        line_period(t0); check(t0 == 2016, "PAL line is 504 dots");
        check(u[8].un.o.inc == 26'h900035, "PAL subcarrier constant");
        frame_period(t0); check(t0 == 312 * 2016, "PAL frame is 312 lines");
        wr(10'b0100000000, 12'h050, 8'd1); repeat (50) @(posedge clk);
        line_period(t0); check(t0 == 2080, "NTSC line is 520 dots");
        check(u[8].un.o.inc == 26'h700162, "NTSC subcarrier constant");
        frame_period(t0); check(t0 == 263 * 2080, "NTSC frame is 263 lines");
        wr(10'b0100000000, 12'h050, 8'd2); repeat (50) @(posedge clk);
        line_period(t0); check(t0 == 2080, "PAL-60 line is 520 dots");
        check(u[8].un.o.inc == 26'h8a9009, "PAL-60 subcarrier constant");
        wr(10'b0100000000, 12'h050, 8'd3); repeat (50) @(posedge clk);
        line_period(t0); check(t0 == 2016, "NTSC-50 line is 504 dots");
        check(u[8].un.o.inc == 26'h740d18, "NTSC-50 subcarrier constant");
        // trim
        wr(10'b0100000000, 12'h050, 8'd0);
        wr(10'b0100000000, 12'h054, 8'd5); repeat (5) @(posedge clk);
        check(u[8].un.o.inc == 26'h900035 + 26'd320, "trim +5 adds 5 x 64");
        wr(10'b0100000000, 12'h054, 8'hFD); repeat (5) @(posedge clk);
        check(u[8].un.o.inc == 26'h900035 - 26'd192, "trim -3 subtracts 3 x 64");
        wr(10'b0100000000, 12'h054, 8'd0);
        // group orders, bit orders
        for (k = 0; k < 6; k = k + 1) begin
            wr(10'b0100000000, 12'h051, k[7:0]); repeat (5) @(posedge clk);
            orders(k);
        end
        wr(10'b0100000000, 12'h051, 8'd5);
        for (k = 0; k < 8; k = k + 1) begin
            wr(10'b0100000000, 12'h052, k[7:0]); repeat (5) @(posedge clk);
            revs(k);
        end
        // the freeze bit (unit 9): the marker moves 4 dots a frame (mx changes one clock after frame_tick); with FRZ set it holds for two frames, with FRZ cleared it moves again
        wr(10'b1000000000, 12'h053, 8'd3);
        @(posedge u[9].un.o.frame_tick); repeat (3) @(posedge clk); mx_a = u[9].un.o.mx;
        @(posedge u[9].un.o.frame_tick); @(posedge u[9].un.o.frame_tick); repeat (3) @(posedge clk);
        check(u[9].un.o.mx == mx_a, "marker holds for two frames with FRZ set");
        wr(10'b1000000000, 12'h053, 8'd1);
        @(posedge u[9].un.o.frame_tick); repeat (3) @(posedge clk);
        check(u[9].un.o.mx == mx_a + 10'd4, "marker moves again after FRZ is cleared");
        // the sync knobs: tb_avsync below is a top of its own (run.sh runs both side by side)
        rdb(0, 12'h05D); check(rv == 8'd0, "EQ / L262 reset to 0");
        rdb(0, 12'h05E); check(rv == 8'd6, "VLEN resets to 6 half lines");
        rdb(0, 12'h05F); check(rv == 8'd0, "VPOS resets to 0");
        if (errors == 0 && checks > 50) $display("PASS tb_avregs errors=0 checks=%0d", checks);
        else $display("FAIL tb_avregs errors=%0d checks=%0d", errors, checks);
        $finish;
    end
    initial begin #3000000000; $display("FAIL tb_avregs timeout"); $finish; end
endmodule

// T201 D4 / S10: the sync knobs (diag_regs.v 05D-05F, diag_avout.v). One unit; per setting the csync of one whole frame is measured at the encoder's csync input (cs_w, one sample a clock):
// every pulse is recorded with its line (the raster's vpos), position in the line (sx), width and start time. The expectations are computed here from the specification (pulse per line,
// narrow = half the sync width, broad = half line less the serration, which is the sync width, blocks of vlen half lines), not read from the design.
module tb_avsync;
    reg clk = 0;
    always #5 clk = ~clk;
    reg we = 0, re = 0;
    reg [11:0] a = 0;
    reg [7:0] d = 0;
    wire [7:0] q, ga, gb, gc;
    unit un (.clk(clk), .we(we), .re(re), .a(a), .d(d), .q(q), .ga(ga), .gb(gb), .gc(gc));
    integer errors = 0, checks = 0;
    reg done = 0;
    task check(input c, input [8*70-1:0] msg); begin checks = checks + 1; if (!c) begin errors = errors + 1; $display("FAIL sync: %0s", msg); end end endtask
    task wr(input [11:0] ad, input [7:0] dt);
        begin @(posedge clk); a <= ad; d <= dt; we <= 1; @(posedge clk); we <= 0; end
    endtask

    // the recorded frame
    integer np, pl [0:1023], pp [0:1023], pw [0:1023], pt [0:1023];
    integer clkn = 0, run = 0, w = 0, ft = 0, nde = 0;
    reg prev = 0, prevde = 0, rec = 0;
    wire cs = un.o.cs_w;
    wire de = un.o.de_w;
    always @(posedge clk) begin
        clkn <= clkn + 1;
        if (rec) begin
            if (cs && !prev) begin pl[np] = un.o.vpos; pp[np] = un.o.sx; pt[np] = clkn - ft; w = 1; end
            else if (cs) w = w + 1;
            if (!cs && prev) begin pw[np] = w; np = np + 1; end
            if (de && !prevde && np < 1000) nde = nde + 1;
        end
        prev <= cs; prevde <= de;
    end

    integer HT, HTH, W, VS, LT, ACT;
    integer n_nar, n_brd, n_nor, n_part, bad_sp, bad_w, first_brd_l, first_brd_h, first_nar_l, first_nar_h, i, blk, last_t, lines_in, exp_cnt;
    // set a configuration, wait for the next frame start, measure that frame
    task measure(input integer std, input integer eq, input integer l262, input integer vlen, input integer vpos);
        integer per, s2, slot;
        begin
            wr(12'h050, std); wr(12'h05D, eq | (l262 << 1)); wr(12'h05E, vlen); wr(12'h05F, vpos);
            @(posedge un.o.frame_tick);          // the first frame start with the new values
            ft = clkn; np = 0; nde = 0; rec = 1;
            @(posedge un.o.frame_tick); rec = 0; per = clkn - ft;
            if (std == 1 || std == 2) begin HT = 520; HTH = 260; W = 38; VS = 244; LT = l262 ? 262 : 263; ACT = 240; end
            else begin HT = 504; HTH = 252; W = 37; VS = 280; LT = 312; ACT = 272; end
            check(per == LT * HT * 4, "frame length is LT lines");
            check(nde == ACT, "active lines per frame");
            n_nar = 0; n_brd = 0; n_nor = 0; n_part = 0; bad_sp = 0; bad_w = 0; first_brd_l = -1; first_brd_h = 0; first_nar_l = -1; first_nar_h = 0; last_t = -1;
            for (i = 0; i < np; i = i + 1) begin
                if (pw[i] == (W / 2) * 4) begin
                    n_nar = n_nar + 1;
                    if (first_nar_l < 0) begin first_nar_l = pl[i]; first_nar_h = (pp[i] >= HTH); end
                end else if (pw[i] == W * 4) n_nor = n_nor + 1;
                else if (pw[i] == (eq ? (HTH - W) : (HT - W)) * 4) begin
                    n_brd = n_brd + 1;
                    if (first_brd_l < 0) begin first_brd_l = pl[i]; first_brd_h = (pp[i] >= HTH); end
                end else if (!eq && pw[i] == HTH * 4) begin
                    n_part = n_part + 1;
                end else bad_w = bad_w + 1;
                if (eq && pw[i] != W * 4) begin                     // block pulses: half a line apart
                    if (last_t >= 0 && pt[i] - last_t != HTH * 4) bad_sp = bad_sp + 1;
                    last_t = pt[i];
                end else if (eq && last_t >= 0 && pt[i] - last_t != HTH * 4) last_t = -1;
            end
            check(bad_w == 0, "every pulse has a specified width");
            if (eq) begin
                // blocks start at S = VS - 3 + vpos (lines), broad at slot 2 S + vlen; slots are half lines
                s2 = 2 * (VS - 3 + vpos); slot = s2 + vlen;
                lines_in = (3 * vlen + 1) / 2;
                check(n_nar == 2 * vlen, "narrow pulses: 2 blocks of vlen half lines");
                check(n_brd == vlen, "broad pulses: vlen half lines");
                check(n_nor == LT - lines_in, "normal line pulses outside the blocks");
                check(first_nar_l == VS - 3 + vpos && first_nar_h == 0, "pre-equalizing starts at line S");   // a block past the last line: the pulse counts prove its tail is there
                check(first_brd_l == slot / 2 && first_brd_h == (slot % 2), "broad block starts vlen half lines later");
                check(bad_sp == 0 && np == LT - lines_in + 3 * vlen, "block pulses half a line apart; pulse count");
            end else begin
                lines_in = (vlen + 1) / 2;
                check(n_nar == 0, "no narrow pulses without equalizing");
                check(n_brd + n_part == lines_in && n_part == (vlen % 2), "broad lines: vlen / 2 rounded up, the odd half line short");
                check(n_nor == LT - lines_in, "normal line pulses outside the vsync");
                check(first_brd_l == VS + vpos && first_brd_h == 0, "vsync starts at VS + vpos");
                check(np == LT, "one pulse a line");
            end
        end
    endtask
    initial begin
        repeat (60) @(posedge clk);
        // NTSC 60 Hz, 263 lines: lengths and positions with and without equalizing
        measure(1, 0, 0, 6, 0);
        $display("sync default NTSC: %0d pulses in %0d lines, broad at line %0d, %0d broad", np, LT, first_brd_l, n_brd);
        measure(1, 0, 0, 5, 0);
        measure(1, 0, 0, 12, 0);
        measure(1, 0, 0, 6, -8);
        measure(1, 0, 0, 6, 8);
        measure(1, 1, 0, 6, 0);
        $display("sync eq NTSC 3 lines: %0d pulses, %0d narrow, %0d broad, %0d normal, first narrow line %0d, first broad line %0d half %0d", np, n_nar, n_brd, n_nor, first_nar_l, first_brd_l, first_brd_h);
        measure(1, 1, 0, 5, 0);
        measure(1, 1, 0, 12, -8);
        measure(1, 1, 0, 6, 8);
        measure(1, 1, 0, 6, -4);
        // 262 lines per frame
        measure(1, 0, 1, 6, 0);
        $display("sync 262 lines: %0d pulses", np);
        measure(1, 1, 1, 6, 0);
        measure(1, 1, 1, 12, 0);
        measure(2, 1, 1, 5, 4);
        measure(1, 1, 1, 12, 8);                            // 18 lines from line 249 in a 262-line frame: wraps into lines 0-4
        $display("sync wrap 262 lines eq 6 lines +8: %0d pulses, %0d narrow, %0d broad, first narrow line %0d", np, n_nar, n_brd, first_nar_l);
        // 50 Hz keeps 312 lines whatever the bit says
        measure(0, 1, 1, 5, 0);
        measure(3, 0, 1, 6, 0);
        if (errors == 0 && checks > 100) $display("PASS tb_avsync errors=0 checks=%0d", checks);
        else $display("FAIL tb_avsync errors=%0d checks=%0d", errors, checks);
        done = 1;
        $finish;
    end
    initial begin #6000000000; $display("FAIL tb_avsync timeout"); $finish; end
endmodule
