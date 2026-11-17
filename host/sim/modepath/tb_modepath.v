// T164 WP7 (S6) bench: the path of an HDMI mode request in the host, from the application's byte at 0x4F00 to what the board sees:
// mode_req (clk_sys write, crossing, accept / refuse, hold) -> cdce_mode (the I2C bytes, decoded here from SCL / SDA) and -> mode_pix -> hdmi_timing
// (taken while pix_rst is high), and the status register 0x4F04.. read back over the same bus. The pixel clock stops for the retune as on the board.
// +c64=0: a frame-buffer core (all 9 rows, hm_cdce); +c64=1: a C64-type core (locked rows only, family from fam_v60, hm_cdce_lock).
// Phase 1 (default): every request value 0..15 after every other, then 0x10 / 0xFF / 0x12 / 0x8C; the expected behaviour (accepted, refused, the row in
// use, the clocks per frame) is written out below, not read from hdmi_modes.vh.
// Phase 2 (+i2c): for every effective row, the CDCE transactions: two writes of C8 14 08 57 00 01 01 + the 4 bytes of the row (written out below).
`timescale 1ns/1ps
module tb;
    reg clk_sys = 0, clk50 = 0, raw = 0, pix_run = 1;
    always #5 clk_sys = ~clk_sys;
    always #10 clk50 = ~clk50;
    always #3.4 raw = ~raw;
    wire pix_clk = raw & pix_run;
    reg pix_locked = 1;
    integer c64 = 0;
    reg fam = 0;
    reg [2:0] prs = 3'b111;                              // pix_rst as top.v's rst_sync: asserted with the lock loss, released after three clocks
    always @(posedge pix_clk or negedge pix_locked) if (!pix_locked) prs <= 3'b111; else prs <= {prs[1:0], 1'b0};
    wire pix_rst = prs[2];

    reg we = 0; reg [14:0] addr = 0; reg [7:0] wdata = 0;
    wire [7:0] rdata; wire rhit;
    wire [3:0] vm_mode, mode_pix, mode_q; wire vm_rst;
    wire ce, de, hs, vs, running; wire [23:0] rgb; wire [11:0] cx, hact, xact0; wire [10:0] cy, vact, yact0;
    wire [3:0] gear; wire [6:0] vic; wire [31:0] rate; wire [12:0] sx_e, fw_m1; wire [10:0] sy_e, ey_e; wire [2:0] ph_e;
    wire scl_low, sda_low, done, valid;
    reg c64_r = 0;
    mode_req dut (.clk_sys(clk_sys), .we(we), .addr(addr), .wdata(wdata), .rdata(rdata), .rhit(rhit),
        .clk50(clk50), .c64_type(c64_r), .fam_v60(fam), .vm_mode(vm_mode), .vm_rst(vm_rst),
        .clk_pix(pix_clk), .mode_pix(mode_pix), .gt_ready(1'b1), .pix_locked(pix_locked), .running(running), .mode_q(mode_q));
    cdce_mode i2c (.clk(clk50), .rst(vm_rst), .scl_low(scl_low), .sda_low(sda_low), .done(done), .mode(vm_mode), .lock(c64_r));
    hdmi_timing mtim (.clk(pix_clk), .rst(pix_rst), .mode(mode_pix), .align(1'b0), .valid(valid), .aligned(), .ce(ce), .rgb(rgb), .de(de), .hs(hs), .vs(vs),
        .cx(cx), .cy(cy), .running(running), .hact(hact), .vact(vact), .xact0(xact0), .yact0(yact0), .gear(gear), .vic(vic), .rate(rate),
        .sx_e(sx_e), .fw_m1(fw_m1), .sy_e(sy_e), .ey_e(ey_e), .ph_e(ph_e), .mode_q(mode_q));

    // clocks per frame of each row, written out: htot * vtot * gear
    integer want [0:14];
    initial begin
        want[0] = 864*625*5;   want[1] = 1980*750*2;  want[2] = 2640*1125;   want[3] = 0;  want[4] = 0;   want[5] = 0;
`ifdef T188
        want[0] = 867*625*5;   want[1] = 1978*749*2;  want[2] = 2638*1125;   // T188: the Amiga copies (rows 0..2 bent to the Amiga frame)
        want[12] = 864*625*5;  want[13] = 1980*750*2; want[14] = 2640*1125;  // T234: the host's rows 0..2 (Frame follow Off)
`endif
        want[6] = 800*525*5;   want[7] = 1650*750*2;  want[8] = 2200*1125;   want[9] = 1052*650*3;  want[10] = 1344*806*2;  want[11] = 1688*1052;
    end
    // the 4 CDCE bytes of each row (hm_cdce); the locked rows of a C64-type core (hm_cdce_lock)
    reg [31:0] pll_fb [0:14]; reg [31:0] pll_lock [0:11];
    initial begin
        pll_fb[0] = 32'h0190238D; pll_fb[1] = 32'h037033ED; pll_fb[2] = 32'h037033ED; pll_fb[6] = 32'h1311834D; pll_fb[7] = 32'h2A416BED;
        pll_fb[8] = 32'h2A416BED; pll_fb[9] = 32'h00D0034C; pll_fb[10] = 32'h10F11B6D; pll_fb[11] = 32'h0EF152CC;
        pll_lock[2] = 32'h037033ED; pll_lock[8] = 32'h3DE0F3CD;
`ifdef T188
        pll_fb[0] = 32'h0E81238D; pll_fb[1] = 32'h2AF143ED; pll_fb[2] = 32'h2B418BED; pll_lock[2] = 32'h0;   // no C64-type use of the Amiga copy
        pll_fb[12] = 32'h0190238D; pll_fb[13] = 32'h037033ED; pll_fb[14] = 32'h037033ED;   // T234: the host's bytes of rows 0..2
`endif
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
    integer fol = 0;                                               // T234 (the Amiga copies): the Frame follow written (+fol=1); the default is Off
    integer amiga = 0;
    integer cur = 2;                                               // the expected row in use (the table row: 12..14 for 0..2 with Frame follow Off)
    integer ref_now = 0;                                           // the refused bit the last request left in the status register
    integer cur_req = 2;                                           // the requested row, what the status register reports
    function ok_req(input integer v); begin
        if (v >= 12) ok_req = 0;
        else if (c64) ok_req = ((v % 6) == 2);                     // locked rows: the 1080p ones
        else ok_req = (v != 3 && v != 4 && v != 5);                // the 50 Hz PC rows do not exist
    end endfunction
    function integer eff_req(input integer v); begin
        eff_req = c64 ? ((fam ? 6 : 0) + (v % 6)) : (amiga && !fol && v < 3) ? v + 12 : v;
    end endfunction

    task check_status(input integer exp_row, input integer exp_ref); begin
        bus_read(4); if (rd !== 8'h48) begin fails = fails + 1; $display("FAIL status magic %h", rd); end
        bus_read(5); if (rd !== (amiga ? 8'h04 : 8'h01)) begin fails = fails + 1; $display("FAIL status version %h", rd); end
        if (amiga) begin bus_read(12); if (rd !== fol[7:0]) begin fails = fails + 1; $display("FAIL status Frame follow readback %h want %0d", rd, fol); end end
        bus_read(6);
        if (rd[0] !== 1'b1 || rd[1] !== 1'b1 || rd[2] !== 1'b1 || rd[3] !== (exp_row == 2 || exp_row == 8 || exp_row == 14) || rd[4] !== c64_r || rd[7] !== exp_ref[0] || rd[6:5] !== 2'b0) begin
            fails = fails + 1; $display("FAIL status flags %b: want gt 1 pix_locked 1 running 1 locked %0d c64 %0d refused %0d", rd, (exp_row == 2 || exp_row == 8 || exp_row == 14), c64_r, exp_ref);
        end
        bus_read(7); if (rd !== (exp_row >= 12 ? exp_row - 12 : exp_row)) begin fails = fails + 1; $display("FAIL status mode %0d want %0d (the requested row)", rd, exp_row >= 12 ? exp_row - 12 : exp_row); end
        bus_read(8); if (rd !== reqs[7:0]) begin fails = fails + 1; $display("FAIL status requests written %0d want %0d", rd, reqs[7:0]); end
        bus_read(9); if (rd !== reqs[7:0]) begin fails = fails + 1; $display("FAIL status requests processed %0d want %0d", rd, reqs[7:0]); end
    end endtask

    integer settle_to;
    task request(input integer v, input integer from); integer got; begin
        write_req(v[7:0]);
        if (ok_req(v)) begin
            cur = eff_req(v); cur_req = v; ref_now = 0;
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
            ref_now = 1;
            check_status(cur, 1);
        end
    end endtask

    // T234: Frame follow written at 0x4F02: the effective row changes like after a request (CDCE written again, timing generator restarted), no request written
    task set_fol(input integer v); integer to, changed; begin
        seen_rst = 0; changed = (v != fol); fol = v;
        @(posedge clk_sys); #1; we = 1; addr = 15'h4F02; wdata = v[7:0];
        @(posedge clk_sys); #1; we = 0; addr = 0;
        cur = eff_req(cur_req);
        if (changed) retune; else repeat (300) @(posedge clk50);       // the same value again: nothing re-syncs
        to = 0;
        while (!running && to < 100000) begin @(posedge clk50); to = to + 1; end
        repeat (4) @(posedge pix_clk);
        if (mode_q !== cur[3:0] || vm_mode !== cur[3:0] || mode_pix !== cur[3:0]) begin
            fails = fails + 1; $display("FAIL Frame follow %0d: vm_mode %0d mode_pix %0d mode_q %0d want %0d", v, vm_mode, mode_pix, mode_q, cur);
        end
        if (changed && !seen_rst) begin fails = fails + 1; $display("FAIL Frame follow %0d: the CDCE was not restarted", v); end
        if (!changed && seen_rst) begin fails = fails + 1; $display("FAIL Frame follow %0d written again: the CDCE was restarted", v); end
        if ((mtim.htot_m1 + 1) * (mtim.vtot_m1 + 1) * gear != want[cur]) begin fails = fails + 1; $display("FAIL Frame follow %0d: frame %0d clocks, want %0d", v, (mtim.htot_m1 + 1) * (mtim.vtot_m1 + 1) * gear, want[cur]); end
        else $display("Frame follow %0d: request row %0d runs table row %0d, %0d clocks per frame", v, cur_req, cur, (mtim.htot_m1 + 1) * (mtim.vtot_m1 + 1) * gear);
        check_status(cur, ref_now);
    end endtask

    // ---- phase 2: the CDCE bytes of one effective row
    task i2c_row(input integer v); reg [31:0] p; integer t, w; begin
        p = c64 ? pll_lock[eff_req(v)] : pll_fb[eff_req(v)];
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

    integer a, b;
    initial begin
        if ($value$plusargs("c64=%d", c64)) ;
        c64_r = (c64 != 0);
`ifdef T188
        amiga = 1;
`endif
        if ($value$plusargs("fol=%d", fol)) ;
        if (!amiga) fol = 1;                                        // no Frame follow in the host's own mode_req: rows are the plain ones
        fam = 0;
        pix_locked = 0; repeat (50) @(posedge raw); pix_locked = 1; repeat (3000) @(posedge raw);
        if (amiga) begin
            cur = 14; cur_req = 2;                                  // power-up: Off, row 2 requested = the host's 1080p50 (table row 14)
            if (fol != 0) begin begin : wr integer f; f = fol; fol = 0; set_fol(f); end end
        end
        if ($test$plusargs("i2c")) begin
            for (a = 0; a < 12; a = a + 1) if (ok_req(a)) i2c_row(a);
            if (c64) begin fam = 1; for (a = 0; a < 12; a = a + 1) if (ok_req(a)) begin repeat (100) @(posedge clk50); i2c_row(a); end end
        end else begin
            if (!amiga) cur = 2;
            request(2, 99);                                         // from the shipped row, counted as a request
            for (a = 0; a < 16; a = a + 1)
                for (b = 0; b < 16; b = b + 1) if (b != a) begin
                    if (!$test$plusargs("quick") || b == (a + 1) % 12 || b == 2) begin
                        request(b, a);                              // the request before
                        request(a, b);
                    end
                end
            request(16, 2); request(255, 2); request(18, 2); request(8'h8C, 2);      // bits 7:4 set: refused
            if (amiga) begin                                        // T234: the setting changes the row of a running request, both ways, and a refused request leaves it
                request(1, 99);
                set_fol(!fol); set_fol(fol); set_fol(!fol); request(0, 1); request(2, 0); request(13, 2); set_fol(!fol);
            end
            if (c64) begin
                // the family follows the System Mode: a change restarts the synthesizer on the other family's row, with no request
                request(2, 99);
                fam = 1; repeat (2) @(posedge clk50);
                retune; cur = 8;
                a = 0; while (!running && a < 100000) begin @(posedge clk50); a = a + 1; end
                repeat (4) @(posedge pix_clk);
                if (vm_mode !== 4'd8 || mode_q !== 4'd8) begin fails = fails + 1; $display("FAIL family bit: vm_mode %0d mode_q %0d want 8", vm_mode, mode_q); end
                else $display("family bit 1: the C64-type core follows to row 8");
                request(2, 8);                                      // a request for family 0 lands on the System Mode's family
            end
        end
        if (fails == 0) $display("PASS modepath: c64=%0d %0s", c64, $test$plusargs("i2c") ? "i2c" : "path"); else $display("FAIL modepath: %0d", fails);
        $finish;
    end
endmodule
