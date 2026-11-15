// SPDX-License-Identifier: GPL-3.0-or-later
// U16 bench (D4): the path of an HDMI mode request, from the application's byte at 0x4F00 to what the board sees, and the scale at
// 0x4F01. Ported from TRX64U-MultiCore's host/sim/modepath/tb_modepath.v (T164 WP7 there) for this design, a C64-type core (every row
// locked, the family from fam_v60, hm_cdce_lock): mode_req (clk_sys write, crossing, accept / refuse, hold) -> cdce_mode (the I2C
// bytes, decoded here from SCL / SDA) and -> mode_pix -> hdmi_timing (taken while pix_rst is high), and the status register 0x4F04..
// read back over the same bus. The pixel clock stops for the retune as on the board.
// Phase 1 (default): every request value 0..15 after every other, then 0x10 / 0xFF / 0x12 / 0x8C, in family 0 and then in family 1;
// a family change (System Mode) restarts the synthesizer on the other family's row with no request, 800 x 600 going to 576p at 50 Hz. The expected behaviour (accepted,
// refused, the row in use, the clocks per frame) is written out below, not read from hdmi_modes.vh.
// Phase 2 (+i2c): for every effective row of both families, the CDCE transactions: two writes of C8 14 08 57 00 01 01 + the 4 bytes of
// the row (written out below, from tools/hdmi_modes.py table).
// Phase 3 (+scale): 0x4F01 takes 0, 1, 2 and refuses everything else (3, 4, 0x80, 0xFF: nothing changes); the scale reaches
// scale_pix only at a frame start (fstart), 0x4F0A reads it, a write touches neither the request counters nor the refused bit, the
// scale survives a mode request (pix_rst), power-up is 1 (Fit).
//   sh cores/c64scpu/sim/hdmi/run_one.sh tb_mode_req.v [-DPHASE_I2C | -DPHASE_SCALE]   (or +i2c / +scale to vvp)
`timescale 1ns/1ps
module tb;
    reg clk_sys = 0, clk50 = 0, raw = 0, pix_run = 1;
    always #5 clk_sys = ~clk_sys;
    always #10 clk50 = ~clk50;
    always #3.4 raw = ~raw;
    wire pix_clk = raw & pix_run;
    reg pix_locked = 1;
    integer fam = 0;
    reg [2:0] prs = 3'b111;                              // pix_rst as top.v's rst_sync: asserted with the lock loss, released after three clocks
    always @(posedge pix_clk or negedge pix_locked) if (!pix_locked) prs <= 3'b111; else prs <= {prs[1:0], 1'b0};
    wire pix_rst = prs[2];

    reg we = 0; reg [14:0] addr = 0; reg [7:0] wdata = 0;
    wire [7:0] rdata; wire rhit;
    wire [3:0] vm_mode, mode_pix, mode_q; wire vm_rst;
    wire ce, de, hs, vs, running; wire [23:0] rgb; wire [11:0] cx, hact, xact0; wire [10:0] cy, vact, yact0;
    wire [3:0] gear; wire [6:0] vic; wire [31:0] rate; wire [12:0] sx_e, fw_m1; wire [10:0] sy_e, ey_e; wire [2:0] ph_e;
    wire scl_low, sda_low, done, valid;
    wire [1:0] scale_pix;
    wire fstart = ce && cx == 12'd0 && cy == 11'd0;          // as top.v
    mode_req dut (.clk_sys(clk_sys), .we(we), .addr(addr), .wdata(wdata), .rdata(rdata), .rhit(rhit),
        .clk50(clk50), .fam_v60(fam != 0), .vm_mode(vm_mode), .vm_rst(vm_rst),
        .clk_pix(pix_clk), .mode_pix(mode_pix), .fstart(fstart), .scale_pix(scale_pix),
        .gt_ready(1'b1), .pix_locked(pix_locked), .running(running), .mode_q(mode_q));
    cdce_mode i2c (.clk(clk50), .rst(vm_rst), .scl_low(scl_low), .sda_low(sda_low), .done(done), .mode(vm_mode));
    hdmi_timing mtim (.clk(pix_clk), .rst(pix_rst), .mode(mode_pix), .align(1'b0), .valid(valid), .aligned(), .ce(ce), .rgb(rgb), .de(de), .hs(hs), .vs(vs),
        .cx(cx), .cy(cy), .running(running), .hact(hact), .vact(vact), .xact0(xact0), .yact0(yact0), .gear(gear), .vic(vic), .rate(rate),
        .sx_e(sx_e), .fw_m1(fw_m1), .sy_e(sy_e), .ey_e(ey_e), .ph_e(ph_e), .mode_q(mode_q));

    // clocks per frame of each row, written out: htot * vtot * gear (row 6: 526 lines, U16 D2)
    integer want [0:11];
    initial begin
        want[0] = 864*625*5;   want[1] = 1980*750*2;  want[2] = 2640*1125;
        want[6] = 800*526*5;   want[7] = 1650*750*2;  want[8] = 2200*1125;   want[9] = 1052*650*3;
    end
    // the 4 CDCE bytes of each row (tools/hdmi_modes.py table: N 25/7, 55/14, 55/14, 16/5, 990/263, 990/263, 78/25)
    reg [31:0] pll_lock [0:11];
    initial begin
        pll_lock[0] = 32'h0190238D; pll_lock[1] = 32'h037033ED; pll_lock[2] = 32'h037033ED;
        pll_lock[6] = 32'h01001B2D; pll_lock[7] = 32'h3DE0F3CD; pll_lock[8] = 32'h3DE0F3CD; pll_lock[9] = 32'h04E0C30C;
    end

    // ---- the I2C slave: START / STOP / bytes from SCL and SDA (open drain: the line is high unless pulled low)
    wire scl = ~scl_low, sda = ~sda_low;
    reg scl_d = 1, sda_d = 1;
    integer nbit = 0, nbyte = 0, ntx = 0;
    reg [7:0] sh = 0;
    reg [7:0] txb [0:1][0:15];
    integer txn_len [0:1];
    reg in_tx = 0;
    reg clr = 0;
    always @(posedge clk50) begin
        scl_d <= scl; sda_d <= sda;
        if (clr) begin ntx = 0; nbyte = 0; nbit = 0; in_tx = 0; txn_len[0] = 0; txn_len[1] = 0; end
        else if (scl && sda_d && !sda) begin in_tx = 1; nbit = 0; nbyte = 0; end                  // START
        else if (scl && !sda_d && sda) begin                                                      // STOP
            if (in_tx && ntx < 2) begin txn_len[ntx] = nbyte; ntx = ntx + 1; end
            in_tx = 0;
        end else if (in_tx && scl && !scl_d) begin                                                // SCL rises: a bit
            if (nbit == 8) begin nbit = 0; if (ntx < 2 && nbyte < 16) txb[ntx][nbyte] = sh; nbyte = nbyte + 1; end
            else begin sh = {sh[6:0], sda}; nbit = nbit + 1; end
        end
    end

    // ---- the application
    integer fails = 0, reqs = 0;
    reg seen_rst = 0;
    always @(posedge clk50) if (vm_rst) seen_rst <= 1;
    reg [7:0] rd;
    task bus_read(input [3:0] a); begin
        @(posedge clk_sys); #1; addr = {11'h4F0, a};
        @(posedge clk_sys); #1;
        @(posedge clk_sys); #1;
        if (!rhit) begin fails = fails + 1; $display("FAIL read 0x4F0%0h: rhit 0", a); end
        rd = rdata; addr = 0;
    end endtask
    task write_req(input [7:0] v); begin
        reqs = reqs + 1; seen_rst = 0;
        @(posedge clk_sys); #1; we = 1; addr = 15'h4F00; wdata = v;
        @(posedge clk_sys); #1; we = 0; addr = 0;
    end endtask
    task retune; begin                                             // what the board does: clock away, lock lost first, back last
        repeat (400) @(posedge clk50);
        pix_locked = 0; repeat (20) @(posedge raw);
        pix_run = 0; repeat (3000) @(posedge clk50);
        pix_run = 1; repeat (200) @(posedge raw);
        pix_locked = 1; repeat (200) @(posedge raw);
    end endtask

    // the expected behaviour, written out
    integer cur = 2;                                               // the expected row in use
    function ok_req(input integer v); begin
        if (v >= 12) ok_req = 0;
        else ok_req = fam ? ((v % 6) <= 3) : ((v % 6) <= 2);      // the rows of U16 D2 in the System Mode's family
    end endfunction
    function integer eff_req(input integer v); begin
        eff_req = (fam ? 6 : 0) + (v % 6);
    end endfunction

    task check_status(input integer exp_row, input integer exp_ref); begin
        bus_read(4); if (rd !== 8'h48) begin fails = fails + 1; $display("FAIL status magic %h", rd); end
        bus_read(5); if (rd !== 8'h02) begin fails = fails + 1; $display("FAIL status version %h", rd); end
        bus_read(6);
        if (rd[0] !== 1'b1 || rd[1] !== 1'b1 || rd[2] !== 1'b1 || rd[3] !== 1'b1 || rd[4] !== 1'b1 || rd[7] !== exp_ref[0] || rd[6:5] !== 2'b0) begin
            fails = fails + 1; $display("FAIL status flags %b: want gt 1 pix_locked 1 running 1 locked 1 c64 1 refused %0d", rd, exp_ref);
        end
        bus_read(7); if (rd !== exp_row[7:0]) begin fails = fails + 1; $display("FAIL status mode %0d want %0d", rd, exp_row); end
        bus_read(8); if (rd !== reqs[7:0]) begin fails = fails + 1; $display("FAIL status requests written %0d want %0d", rd, reqs[7:0]); end
        bus_read(9); if (rd !== reqs[7:0]) begin fails = fails + 1; $display("FAIL status requests processed %0d want %0d", rd, reqs[7:0]); end
    end endtask

    integer settle_to;
    task request(input integer v, input integer from); integer got; begin
        write_req(v[7:0]);
        if (ok_req(v)) begin
            cur = eff_req(v);
            retune;
            settle_to = 0;
            while (!running && settle_to < 100000) begin @(posedge clk50); settle_to = settle_to + 1; end
            repeat (4) @(posedge pix_clk);
            if (mode_q !== cur[3:0] || vm_mode !== cur[3:0] || mode_pix !== cur[3:0]) begin
                fails = fails + 1; $display("FAIL request %0d after %0d: vm_mode %0d mode_pix %0d mode_q %0d want %0d", v, from, vm_mode, mode_pix, mode_q, cur);
            end
            got = (mtim.htot_m1 + 1) * (mtim.vtot_m1 + 1) * gear;
            if (got != want[cur]) begin fails = fails + 1; $display("FAIL request %0d after %0d: frame %0d clocks, want %0d", v, from, got, want[cur]); end
            else if (from == (v + 1) % 12) $display("request %0d after %0d: row %0d, %0d clocks per frame", v, from, cur, got);
            check_status(cur, 0);
        end else begin
            repeat (300) @(posedge clk50);
            if (seen_rst || vm_mode !== cur[3:0] || mode_pix !== cur[3:0] || mode_q !== cur[3:0] || !running) begin
                fails = fails + 1; $display("FAIL refused request %0d after %0d: rst %0d vm_mode %0d mode_pix %0d mode_q %0d running %0d (row in use %0d)", v, from, seen_rst, vm_mode, mode_pix, mode_q, running, cur);
            end
            check_status(cur, 1);
        end
    end endtask

    // ---- phase 2: the CDCE bytes of one effective row
    task i2c_row(input integer v); reg [31:0] p; integer t, w; begin
        p = pll_lock[eff_req(v)];
        pix_run = 0;                                               // the clock is away for the whole retune: no pixel events
        write_req(v[7:0]);
        repeat (40) @(posedge clk50);
        clr = 1; @(posedge clk50); #1; clr = 0; w = 0;
        while (!done && w < 3000000) begin @(posedge clk50); w = w + 1; end
        if (!done) begin fails = fails + 1; $display("FAIL i2c row %0d: cdce_mode did not finish", eff_req(v)); end
        repeat (4) @(posedge clk50);
        if (ntx != 2) begin fails = fails + 1; $display("FAIL i2c row %0d: %0d transactions, want 2", eff_req(v), ntx); end
        for (t = 0; t < 2; t = t + 1) begin
            if (txn_len[t] != 11) begin fails = fails + 1; $display("FAIL i2c row %0d tx %0d: %0d bytes", eff_req(v), t, txn_len[t]); end
            if (txb[t][0] !== 8'hC8 || txb[t][1] !== 8'h14 || txb[t][2] !== 8'h08 || txb[t][3] !== 8'h57 || txb[t][4] !== 8'h00 || txb[t][5] !== 8'h01 || txb[t][6] !== 8'h01
                || txb[t][7] !== p[31:24] || txb[t][8] !== p[23:16] || txb[t][9] !== p[15:8] || txb[t][10] !== p[7:0]) begin
                fails = fails + 1;
                $display("FAIL i2c row %0d tx %0d: %h %h %h %h %h %h %h | %h %h %h %h want pll %h", eff_req(v), t, txb[t][0], txb[t][1], txb[t][2], txb[t][3], txb[t][4], txb[t][5], txb[t][6],
                         txb[t][7], txb[t][8], txb[t][9], txb[t][10], p);
            end
        end
        $display("i2c row %0d: 2 x C8 14 08 57 00 01 01 %h %h %h %h", eff_req(v), txb[1][7], txb[1][8], txb[1][9], txb[1][10]);
        pix_run = 1;
    end endtask

    // ---- phase 3: the scale
    reg [1:0] sc_cur = 2'd1;
    integer fs_seen;
    task wait_fstart; begin
        fs_seen = 0;
        while (!fs_seen) begin @(posedge pix_clk); if (fstart) fs_seen = 1; end
    end endtask
    task write_scale(input [7:0] v); integer t, early; reg [7:0] w0, a0; reg r0; begin
        bus_read(8); w0 = rd; bus_read(9); a0 = rd; bus_read(6); r0 = rd[7];
        @(posedge clk_sys); #1; we = 1; addr = 15'h4F01; wdata = v;
        @(posedge clk_sys); #1; we = 0; addr = 0;
        // not before a frame start: the scale in use stays until the next one
        early = 0;
        for (t = 0; t < 200; t = t + 1) begin @(posedge pix_clk); if (fstart) t = 200; else if (scale_pix !== sc_cur) early = 1; end
        if (early) begin fails = fails + 1; $display("FAIL scale %0d: scale_pix changed before a frame start", v); end
        if (v < 3) sc_cur = v[1:0];
        wait_fstart; repeat (2) @(posedge pix_clk);
        if (scale_pix !== sc_cur) begin fails = fails + 1; $display("FAIL scale %0d: scale_pix %0d want %0d", v, scale_pix, sc_cur); end
        repeat (10) @(posedge clk_sys);
        bus_read(10); if (rd !== {6'd0, sc_cur}) begin fails = fails + 1; $display("FAIL scale %0d: 0x4F0A reads %0d want %0d", v, rd, sc_cur); end
        bus_read(8); if (rd !== w0) begin fails = fails + 1; $display("FAIL scale %0d: requests written moved %0d -> %0d", v, w0, rd); end
        bus_read(9); if (rd !== a0) begin fails = fails + 1; $display("FAIL scale %0d: requests processed moved %0d -> %0d", v, a0, rd); end
        bus_read(6); if (rd[7] !== r0) begin fails = fails + 1; $display("FAIL scale %0d: the refused bit moved", v); end
        $display("scale write %0d: %0s, in use %0d", v, v < 3 ? "taken at the next frame start" : "refused", scale_pix);
    end endtask

    integer a, b;
    reg ph_i2c = 0, ph_scale = 0;
    initial begin
        ph_i2c = $test$plusargs("i2c"); ph_scale = $test$plusargs("scale");
`ifdef PHASE_I2C
        ph_i2c = 1;
`endif
`ifdef PHASE_SCALE
        ph_scale = 1;
`endif
        fam = 0;
        pix_locked = 0; repeat (50) @(posedge raw); pix_locked = 1; repeat (3000) @(posedge raw);
        if (ph_i2c) begin
            for (a = 0; a < 12; a = a + 1) if (ok_req(a)) i2c_row(a);
            fam = 1; for (a = 0; a < 12; a = a + 1) if (ok_req(a)) begin repeat (100) @(posedge clk50); i2c_row(a); end
        end else if (ph_scale) begin
            a = 0; while (!running && a < 100000) begin @(posedge clk50); a = a + 1; end
            bus_read(10); if (rd !== 8'd1 || scale_pix !== 2'd1) begin fails = fails + 1; $display("FAIL power-up scale %0d / %0d want 1 (Fit)", rd, scale_pix); end
            write_scale(0); write_scale(2); write_scale(3); write_scale(1); write_scale(8'hFF); write_scale(2); write_scale(4); write_scale(8'h80);
            write_scale(0);
            request(1, 2);                                          // a mode request restarts the pixel side: the scale stays
            if (scale_pix !== 2'd0) begin fails = fails + 1; $display("FAIL the scale did not survive a mode request: %0d", scale_pix); end
            bus_read(10); if (rd !== 8'd0) begin fails = fails + 1; $display("FAIL 0x4F0A after a mode request: %0d", rd); end
            write_scale(1);
        end else begin
            for (fam = 0; fam < 2; fam = fam + 1) begin
                if (fam) begin
                    // the family follows the System Mode: a change restarts the synthesizer on the other family's row, with no request
                    request(2, 99);
                    repeat (2) @(posedge clk50);
                    retune; cur = 8;
                    a = 0; while (!running && a < 100000) begin @(posedge clk50); a = a + 1; end
                    repeat (4) @(posedge pix_clk);
                    if (vm_mode !== 4'd8 || mode_q !== 4'd8) begin fails = fails + 1; $display("FAIL family bit: vm_mode %0d mode_q %0d want 8", vm_mode, mode_q); end
                    else $display("family bit 1: the row follows to 8");
                end
                request(2, 99);
                for (a = 0; a < 16; a = a + 1)
                    for (b = 0; b < 16; b = b + 1) if (b != a) begin
                        if (!$test$plusargs("quick") || b == (a + 1) % 12 || b == 2) begin
                            request(b, a);                          // the request before
                            request(a, b);
                        end
                    end
                request(16, 2); request(255, 2); request(18, 2); request(8'h8C, 2);      // bits 7:4 set: refused
            end
            fam = 1;
            request(2, 8);                                          // a request for family 0 lands on the System Mode's family
            if (cur != 8) begin fails = fails + 1; $display("FAIL family 1: request 2 gave row %0d", cur); end
            // 800 x 600 (row 9), then the System Mode goes to 50 Hz: no row 3 there, the family's 576p (row 0) is taken; back to 60 Hz: 9
            request(3, 8);
            fam = 0; repeat (2) @(posedge clk50);
            retune; cur = 0;
            a = 0; while (!running && a < 100000) begin @(posedge clk50); a = a + 1; end
            repeat (4) @(posedge pix_clk);
            if (vm_mode !== 4'd0 || mode_q !== 4'd0) begin fails = fails + 1; $display("FAIL 800 x 600 to 50 Hz: vm_mode %0d mode_q %0d want 0", vm_mode, mode_q); end
            else $display("800 x 600 and a 50 Hz System Mode: row 0 (576p)");
            fam = 1; repeat (2) @(posedge clk50);
            retune; cur = 9;
            a = 0; while (!running && a < 100000) begin @(posedge clk50); a = a + 1; end
            repeat (4) @(posedge pix_clk);
            if (vm_mode !== 4'd9 || mode_q !== 4'd9) begin fails = fails + 1; $display("FAIL back to 60 Hz: vm_mode %0d mode_q %0d want 9", vm_mode, mode_q); end
            else $display("back to 60 Hz: row 9 again");
        end
        if (fails == 0) $display("PASS mode_req: %0s", ph_i2c ? "i2c" : ph_scale ? "scale" : "path");
        else $display("FAIL mode_req: %0d", fails);
        $finish;
    end
endmodule
