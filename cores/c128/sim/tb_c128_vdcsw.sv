// T257 fault 5: the VDC drives a bitmap the way the CREST part of Risen from Oblivion VDC V2 does, and every captured row is compared with the
// memory line that the R27 running sum gives under VICE's rule. Written for this project (TREX).
// THE DEMO'S DATA NEVER ENTERS THE REPOSITORY. The bench reads it from a directory given as +data=<dir> (the maintainer's session scratchpad, made by
// a script that unpacks the demo and logs its VDC writes): vram.hex (65536 bytes, one hex byte per line: the VDC RAM as the part leaves it),
// regs.txt (one "<register> <value>" per line: the part's static registers, written after the RAM), frames.txt (per frame a line "F", then one
// line "<n> <off> <reg> <val>" per CPU write of the part's per-line loop: n = index of the loop's pair, off = microseconds after that pair's
// R25 write; R25 at pair n goes out n lines after the first). Without +data the bench says SKIP and passes nothing.
// The bench drives our VDC through the C128's bus as the demo does: wait for the fall of status bit 5, then pair n goes out at
// d0 + n * L microseconds (L = one line, measured from two falls; d0 = +d0=<us>, default 54), R25 first, R27 `off` microseconds later,
// at the end R25 = $87, R13, R26, R27 = 0. The picture is taken from the VDC's own output (pixel enable, h and v blank), not through c128_pixw.
// Oracle: the memory line of row y is  ds + sum over k = 1..y of (R1 + R27_k)  (VICE vdc.c: bitmap_counter += mem_counter_inc + skip_after_line once per
// raster line), R27_k = the value written in pair k - 1 + a (a = -1, 0, 1: which line the loop's write meets, fixed for the whole picture),
// ds = R12/R13 before or after this frame's end writes (one of the two for the whole picture). A row is right when its 8..687th pixel equal
// the line's pixels shifted by -7..7 (R25[3:0] moves the window, not the data), at most +tol=<n> (16) of them differing (an R25 write in the middle of a row disturbs the pixels around it). +dump=<file> writes the rows of every frame as hex.
`timescale 1ns / 1ps
module tb_c128_vdcsw;
    reg clk = 1'b0;
    always #15.859 clk = ~clk;                        // sys_clk 31.5278 MHz
    reg rst = 1'b1;
    wire [7:0] dout;
    wire [3:0] rgbi;
    wire ce, hb, vb;
    wire [1:0] vdc_il;
    c128_x dut (.clk(clk), .rst(rst), .bus_en(bus_en), .vdc_cs(cs), .vdc_we(we), .vdc_rs(rs), .vdc_din(din), .vdc_dout(dout),
        .vicie_cs(1'b0), .vicie_we(1'b0), .vicie_a0(1'b0), .vicie_din(8'd0), .vicie_dout(), .k_sel(), .fast(),
        .vdc_rgbi(rgbi), .vdc_ce(ce), .vdc_hb(hb), .vdc_vb(vb), .vdc_il(vdc_il),
        .vd_clk(1'b0), .vd_addr(16'd0), .vd_we(1'b0), .vd_wdata(8'd0), .vd_rdata(), .vk_req(18'd0), .vk_ans());
`include "vdc_bus.svh"

    localparam integer NF = 4, MAXEV = 3000, MAXROW = 320;
    reg [7:0] vram [0:65535];
    reg [7:0] rg_r [0:63];
    reg [7:0] rg_v [0:63];
    integer   nreg = 0;
    integer   fstart [0:NF];
    integer   ev_n [0:MAXEV-1];
    integer   ev_off [0:MAXEV-1];
    integer   ev_r [0:MAXEV-1];
    integer   ev_v [0:MAXEV-1];
    integer   nev = 0;
    string    dir, dumpf;
    integer   d0 = 54, s25 = 0, s27 = 0, tol = 16, per = 0;

    // ---- the picture, as the VDC sends it: one 704-bit row per displayed line (white = 1), pixel 0 = bit 703
    reg [703:0] rows [0:NF*MAXROW-1];
    integer     nrow [0:NF-1];
    integer     rowlen [0:NF*MAXROW-1];
    integer     cur_f = -1, r_cur = 0, px = 0;
    reg         vb_q = 1'b1, hb_q = 1'b1;
    reg [703:0] rowacc = 704'd0;
    always @(posedge clk) if (ce) begin
        vb_q <= vb; hb_q <= hb;
        if (vb) r_cur <= 0;
        if (!vb && !hb) begin
            if (px < 704) rowacc[703 - px] <= (rgbi == 4'hF);
            px <= px + 1;
        end
        if (hb && !hb_q && !vb_q && cur_f >= 0 && r_cur < MAXROW) begin     // the row ends where hb rises
            rows[cur_f * MAXROW + r_cur] <= rowacc;
            rowlen[cur_f * MAXROW + r_cur] <= px;
            nrow[cur_f] <= r_cur + 1;
            r_cur <= r_cur + 1;
        end
        if (hb) begin px <= 0; rowacc <= 704'd0; end
    end

    // ---- the CPU side
    reg [7:0] st;
    task automatic fall;            // waits for the fall of status bit 5 (vertical blank ends)
        integer n;
        begin
            n = 0; st = 8'h00;
            while (st[5] !== 1'b1 && n < 400000) begin bus(1'b0, 1'b0, 8'd0, st); n = n + 1; end
            n = 0;
            while (st[5] !== 1'b0 && n < 400000) begin bus(1'b0, 1'b0, 8'd0, st); n = n + 1; end
        end
    endtask
    task automatic wr_at(input longint t, input [7:0] r, input [7:0] v);   // the data write lands about t
        begin
            while (cyc < t - 40) @(posedge clk);
            wr_sel(r); wr_dat(v);
        end
    endtask

    integer fails = 0;
    task check(input ok, input string what);
        begin
            if (ok) $display("ok   %0s", what);
            else begin $display("FAIL %0s", what); fails = fails + 1; end
            $fflush;
        end
    endtask

    integer i, f, e, fd, rc, k;
    longint t_fall, t_fall2, lclk, t0, frame_clk;
    string  ln;
    integer a1, a2, a3, a4;
    integer ds_pre [0:NF-1];
    integer ds_post [0:NF-1];
    integer lines;
    integer w27 [0:NF*300-1];      // R27 written in pair n of frame f
    integer cur13;
    reg [7:0] tmp;

    // ---- the oracle
    reg [735:0] mv;
    reg [703:0] msk;
    function automatic integer row_ok(input integer fr, input integer r, input integer addr);
        integer j, p;
        reg [703:0] c;
        reg [735:0] sh;
        begin
            for (j = 0; j < 92; j = j + 1) mv[735 - 8 * j -: 8] = vram[(addr - 1 + j) & 16'hFFFF];
            row_ok = 0;
            for (p = -7; p <= 7; p = p + 1) begin
                sh = mv >> (24 - p);
                c = sh[703:0];
                if ($countones((c ^ rows[fr * MAXROW + r]) & msk) <= tol) row_ok = 1;
            end
        end
    endfunction
    integer best_good, best_a, best_dsel, good, a, dsel, y, ad, fr2, firstbad, nb;
    integer tot_good, tot_rows;
    integer sumk, dd, j2, diag = 0;
    longint t27 [0:NF*300-1];      // when pair n of frame f wrote R27, clocks after the fall of status bit 5
    // R27 as the line that starts tl clocks after the fall sees it: the last write before that
    function automatic integer v27(input integer fr, input longint tl);
        integer n, v;
        begin
            v = 0;
            for (n = 0; n < 255; n = n + 1) if (t27[fr * 300 + n] != 0 && t27[fr * 300 + n] <= tl) v = w27[fr * 300 + n];
            v27 = v;
        end
    endfunction
    // VICE's rule: line y of the picture is at ds + sum over k = 1..y of (R1 + R27 as line k sees it); the line starts phi after the fall
    function automatic integer addr_of(input integer fr, input integer y, input integer phi, input integer dsel);
        integer k, ad2;
        begin
            ad2 = (dsel ? ds_post[fr] : ds_pre[fr]);
            for (k = 1; k <= y; k = k + 1) ad2 = (ad2 + 88 + v27(fr, (phi * 31528) / 1000 + (k * frame_clk) / lines)) & 16'hFFFF;
            addr_of = ad2;
        end
    endfunction
    integer best_phi, phi;
    task automatic judge(input integer fr);
        begin
            best_good = -1;
            for (phi = 0; phi < 65; phi = phi + 1)
                for (dsel = 0; dsel < 2; dsel = dsel + 1) begin
                    good = 0; ad = (dsel ? ds_post[fr] : ds_pre[fr]);
                    for (y = 0; y < 256 && y < nrow[fr]; y = y + 1) begin
                        if (y > 0) ad = (ad + 88 + v27(fr, (phi * 31528) / 1000 + (y * frame_clk) / lines)) & 16'hFFFF;
                        if (row_ok(fr, y, ad)) good = good + 1;
                    end
                    if (good > best_good) begin best_good = good; best_phi = phi; best_dsel = dsel; end
                end
            // the rows of the best reading that are wrong
            ad = (best_dsel ? ds_post[fr] : ds_pre[fr]);
            firstbad = -1; nb = 0;
            for (y = 0; y < 256 && y < nrow[fr]; y = y + 1) begin
                if (y > 0) ad = (ad + 88 + v27(fr, (best_phi * 31528) / 1000 + (y * frame_clk) / lines)) & 16'hFFFF;
                if (!row_ok(fr, y, ad)) begin
                    if (firstbad < 0) firstbad = y;
                    nb = nb + 1;
                    if (nb <= 12 && diag) begin       // which memory line the row shows instead: the address difference to the running sum
                        dd = -999;
                        for (j2 = -300; j2 <= 300; j2 = j2 + 1) if (dd == -999 && row_ok(fr, y, (ad + j2) & 16'hFFFF)) dd = j2;
                        $display("       row %0d (%0d px): expected line at %0d, shows the line at %0s%0d", y, rowlen[fr * MAXROW + y], ad, (dd == -999) ? "none within 300 of " : "expected + ", (dd == -999) ? 0 : dd);
                    end
                end
            end
            $display("     frame %0d: %0d rows captured (row 0 is %0d px, row 100 %0d px), best reading: line 0 starts %0d us after the fall, ds = %0s: %0d of 256 rows are the memory line of the running sum, %0d wrong, first wrong row %0d",
                     fr, nrow[fr], rowlen[fr * MAXROW], rowlen[fr * MAXROW + 100], best_phi, best_dsel ? "post" : "pre", best_good, nb, firstbad);
            tot_good = tot_good + best_good; tot_rows = tot_rows + 256;
        end
    endtask

    initial begin
        if (!$value$plusargs("data=%s", dir)) begin $display("SKIP tb_c128_vdcsw: no +data=<dir> (the demo's data is not in the repository)"); $finish; end
        if (!$value$plusargs("dump=%s", dumpf)) dumpf = "";
        if (!$value$plusargs("d0=%d", d0)) d0 = 54;
        if ($test$plusargs("diag")) diag = 1;
        if (!$value$plusargs("per=%d", per)) per = 0;
        if (!$value$plusargs("tol=%d", tol)) tol = 16;      // pixels of a row that may differ: a write of R25 inside the row moves the pixels around it
        if (!$value$plusargs("s25=%d", s25)) s25 = 0;     // extra delay of every R25 write, microseconds (a diagnostic: which register is phase sensitive)
        if (!$value$plusargs("s27=%d", s27)) s27 = 0;
        msk = {704{1'b1}}; msk[703:696] = 8'h00; msk[15:0] = 16'h0000;
        $readmemh({dir, "/vram.hex"}, vram);
        fd = $fopen({dir, "/regs.txt"}, "r");
        while ($fscanf(fd, "%d %d\n", a1, a2) == 2) begin rg_r[nreg] = a1; rg_v[nreg] = a2; nreg = nreg + 1; end
        $fclose(fd);
        fd = $fopen({dir, "/frames.txt"}, "r");
        f = -1;
        while (!$feof(fd)) begin
            rc = $fgets(ln, fd);
            if (rc > 0) begin
                if (ln[0] == "F") begin f = f + 1; fstart[f] = nev; end
                else if ($sscanf(ln, "%d %d %d %d", a1, a2, a3, a4) == 4 && f >= 0) begin
                    ev_n[nev] = a1; ev_off[nev] = a2; ev_r[nev] = a3; ev_v[nev] = a4; nev = nev + 1;
                end
            end
        end
        $fclose(fd);
        fstart[f + 1] = nev;
        $display("     data: %0d registers, %0d frames, %0d writes", nreg, f + 1, nev);
        for (f = 0; f < NF; f = f + 1) nrow[f] = 0;
        cur13 = 0;
        for (i = 0; i < nreg; i = i + 1) if (rg_r[i] == 13) cur13 = rg_v[i];

        repeat (16) @(posedge clk);
        rst <= 1'b0;
        repeat (70000) @(posedge clk);
        wr_reg(8'd28, 8'h30);                            // 64 KiB addressing before the first byte
        wr_reg(8'd24, 8'h00);
        wr_reg(8'd18, 8'h00); wr_reg(8'd19, 8'h00);
        wr_sel(8'd31);
        for (i = 0; i < 65536; i = i + 1) begin wr_dat(vram[i]); wait_ready; end
        $display("     the part's VRAM is in the VDC RAM (cycle %0d)", cyc);
        for (i = 0; i < nreg; i = i + 1) wr_reg(rg_r[i], rg_v[i]);
        // let the VDC take the registers, measure one frame
        fall; fall;
        fall; t_fall = cyc; fall; t_fall2 = cyc;
        lines = 312;
        frame_clk = t_fall2 - t_fall;
        lclk = frame_clk / lines;
        if (per > 0) lclk = (per * 31528) / 1000;        // +per=<us>: the loop's own period, as the demo's calibration chose it, not our line
        $display("     frame = %0d clocks, line = %0d clocks = %0d.%0d us", t_fall2 - t_fall, lclk, (lclk * 1000 / 31528) / 1, (lclk * 10000 / 31528) % 10);
        for (f = 0; f < NF; f = f + 1) begin
            fall; t_fall = cyc; cur_f = f;
            ds_pre[f] = cur13; ds_post[f] = cur13;
            for (e = fstart[f]; e < fstart[f + 1]; e = e + 1) begin
                t0 = t_fall + ((d0 + (ev_r[e] == 25 ? s25 : 0) + (ev_r[e] == 27 ? s27 : 0)) * 31528) / 1000 + (per > 0 ? ev_n[e] * lclk : (ev_n[e] * frame_clk) / lines) + (ev_off[e] * 31528) / 1000;
                wr_at(t0, ev_r[e], ev_v[e]);
                if (ev_r[e] == 27 && ev_n[e] < 255) begin w27[f * 300 + ev_n[e]] = ev_v[e]; t27[f * 300 + ev_n[e]] = t0 - t_fall; end
                if (ev_r[e] == 13) begin ds_post[f] = ev_v[e]; cur13 = ev_v[e]; end
            end
        end
        fall;
        cur_f = -1;
        tot_good = 0; tot_rows = 0;
        for (f = 0; f < NF; f = f + 1) judge(f);
        if (dumpf != "") begin
            fd = $fopen(dumpf, "w");
            for (f = 0; f < NF; f = f + 1) for (i = 0; i < nrow[f]; i = i + 1) $fwrite(fd, "%0d %0d %0d %176x\n", f, i, rowlen[f * MAXROW + i], rows[f * MAXROW + i]);
            $fclose(fd);
        end
        check(tot_good == tot_rows, $sformatf("every displayed row of %0d frames is the memory line of the R27 running sum (%0d of %0d)", NF, tot_good, tot_rows));
        if (fails == 0) $display("PASS tb_c128_vdcsw"); else $display("FAILED tb_c128_vdcsw: %0d", fails);
        $finish;
    end
    always @(posedge clk) if (cyc > 200000000) begin $display("FAILED tb_c128_vdcsw: timeout"); $finish; end
endmodule
