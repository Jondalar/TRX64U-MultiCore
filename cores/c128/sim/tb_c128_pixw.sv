// T113 S9 "both pictures reach the host": the C128's VDC (c128_x -DVDC_WHOLE -DVDC64) -> c128_rgbi -> c128_pixw -> a model of the host's
// e_ port (a command is taken when e_en and e_rdy are both high; DDR2 as 128-bit words). The bench shadows the VDC's own stream
// (pixel_ce / active_h / active_v / rgbi) frame by frame and checks, for every frame the writer reports:
//   - the buffer named in `latest` holds that frame: 640 x 400 RGB565 (phase 1; phase 2 uses 640 x 480 buffers), source line y in rows 2y and 2y+1 (the palette is this
//     bench's own copy of the 16 entries), 1280 bytes a row, buffers fb_stride apart
//   - frame_tgl comes once per VDC frame, seq counts them, the buffers go round 0, 1, 2, no line was late (overruns == 0)
//   - after the run the last three frames are still in their buffers, and nothing was written outside the three buffers
// Phase 1b (T257): the VDC is not interlaced and shows 240 lines (R6 = 30 rows of 8 lines) and then 224 lines (R6 = 28): the frame must go through whole, source line y in
// rows 2y and 2y+1 up to y = 239, reported with b2 = 1 (all 480 rows are this picture); of the 224-line frame the rows after line 223 are in the colour of the first blanked dot (written by the
// writer's blank jobs), not what an older frame left there.
// Phase 2 (T113 D11): the VDC is put into interlace sync and video (R8 = 3, Busted Boulders' registers: 10-line glyphs, 48 rows, 240 lines a
// field) with a synthetic 10-line font, text first, then a bitmap; every picture the writer reports as woven (latest b2) is compared with a
// model that takes field 0 of the picture to rows 2y and field 1 to rows 2y+1 (the fields are the bench's shadow of the VDC's stream and its
// own field flag), and the glyphs are read back whole from the woven rows (text). +dump=<file> writes the first woven picture after
// settling as a PPM.
`timescale 1ns / 1ps
module tb_c128_pixw;
    reg clk = 1'b0, ui_clk = 1'b0;
    always #15.859 clk = ~clk;                        // sys_clk 31.5278 MHz
    always #6.667 ui_clk = ~ui_clk;
    reg rst = 1'b1;
    wire [7:0] dout;
    wire [3:0] rgbi;
    wire ce, hb, vb;
    wire [1:0] vdc_il;
    c128_x dut (.clk(clk), .rst(rst), .warm(1'b0), .bus_en(bus_en), .vdc_cs(cs), .vdc_we(we), .vdc_rs(rs), .vdc_din(din), .vdc_dout(dout),
        .vicie_cs(1'b0), .vicie_we(1'b0), .vicie_a0(1'b0), .vicie_din(8'd0), .vicie_dout(), .k_sel(), .fast(),
        .vdc_rgbi(rgbi), .vdc_ce(ce), .vdc_hb(hb), .vdc_vb(vb), .vdc_il(vdc_il),
        .vd_clk(1'b0), .vd_addr(16'd0), .vd_we(1'b0), .vd_wdata(8'd0), .vd_rdata(), .vk_clk(clk), .vk_req(18'd0), .vk_ans());
`include "vdc_bus.svh"

    // ---- the frame writer
    reg wr_en = 1'b0;                                 // the core's VDC_CTRL b0: gates the dot strobe
    wire [15:0] px;
    c128_rgbi rgbi_i (.rgbi(rgbi), .rgb565(px));
    reg [31:0] fb_base = 32'h0000_0000, fb_stride = 32'h0010_0000;
    wire quiet, frame_tgl;
    wire [7:0] latest, seq;
    wire [15:0] overruns;
    wire [26:0] e_addr;
    wire [2:0] e_cmd;
    wire e_en, e_wdf_wren;
    wire [127:0] e_wdf_data;
    wire [15:0] e_wdf_mask;
    reg e_rdy = 1'b1;
    c128_pixw #(.X0(0), .Y0(0), .COLS(640), .LINES(240), .LINES_IL(240), .LINES_BASE(200)) pw (
        .sys_clk(clk), .sys_rst(rst), .dotst(ce & wr_en), .rgb565(px), .hblank(hb), .vblank(vb), .il(vdc_il[1]), .field(vdc_il[0]),
        .quiet(quiet), .frame_tgl(frame_tgl), .latest(latest), .seq(seq), .overruns(overruns),
        .fb_base(fb_base), .fb_stride(fb_stride), .ui_clk(ui_clk), .ui_rst(1'b0),
        .e_addr(e_addr), .e_cmd(e_cmd), .e_en(e_en), .e_rdy(e_rdy), .e_wdf_data(e_wdf_data),
        .e_wdf_mask(e_wdf_mask), .e_wdf_wren(e_wdf_wren));

    // ---- the host's e_ port: DDR2 as 128-bit words, a bit set per word written
    localparam integer NW = 262144;                   // 4 MiB
    localparam integer BW = 65536;                    // the bench expects buffers 0x100000 bytes apart = 65536 words
    localparam integer BUFWORDS = 480 * 80;           // 640 x 480 x 2 bytes / 16
    reg [127:0] ddr [0:NW-1];
    reg wrote [0:NW-1];
    integer writes = 0, bad_cmd = 0, bad_addr = 0;
    integer k;
    initial for (k = 0; k < NW; k = k + 1) begin ddr[k] = 128'hDEAD; wrote[k] = 1'b0; end
    always @(posedge ui_clk) begin
        e_rdy <= ($random & 7) != 0;                  // the port is busy one clock in eight
        if (e_en && e_rdy) begin
            if (e_cmd != 3'b000 || !e_wdf_wren || e_wdf_mask != 16'd0) bad_cmd = bad_cmd + 1;
            if (e_addr[2:0] != 3'd0 || e_addr[26:3] >= NW) bad_addr = bad_addr + 1;
            else begin ddr[e_addr[26:3]] = e_wdf_data; wrote[e_addr[26:3]] = 1'b1; end
            writes = writes + 1;
        end
    end

    // ---- the bench's palette (its own copy of the 16 entries) and the shadow of the VDC's stream
    function [15:0] pal(input [3:0] c);
        case (c)
            4'h0: pal = 16'h0000; 4'h1: pal = 16'h52AA; 4'h2: pal = 16'h0015; 4'h3: pal = 16'h52BF;
            4'h4: pal = 16'h0540; 4'h5: pal = 16'h57EA; 4'h6: pal = 16'h0555; 4'h7: pal = 16'h57FF;
            4'h8: pal = 16'hA800; 4'h9: pal = 16'hFAAA; 4'hA: pal = 16'hA815; 4'hB: pal = 16'hFABF;
            4'hC: pal = 16'hAAA0; 4'hD: pal = 16'hFFEA; 4'hE: pal = 16'hAD55; default: pal = 16'hFFFF;
        endcase
    endfunction
    wire av = ~vb, ah = ~hb;
    reg av_prev = 1'b0, ah_prev = 1'b0;
    integer vf = 0;                                   // VDC frames started since the writer was switched on
    integer vdone = 0;                                // ... and finished
    integer sln = 0, sxx = 0;
    localparam integer FS = 640 * 240;
    reg [3:0] sh [0:4*FS-1];                          // four shadow slots, frame f in slot f % 4
    reg       sfld [0:3];                             // the VDC's field flag of that frame
    reg       sil  [0:3];                             // its interlace flag
    integer   scnt [0:3];                             // lines it had
    integer   swd  [0:3];                             // active dots of its lines (the last one counts)
    reg [3:0] sbd  [0:3];                             // the colour of the first dot after the active part of a line (the VDC's background while its display enable is on, else black; the last line counts)
    always @(posedge clk) if (ce && wr_en) begin
        if (av && !av_prev) begin vf = vf + 1; sln = -1; sfld[vf % 4] = vdc_il[0]; sil[vf % 4] = vdc_il[1]; scnt[vf % 4] = 0; end
        if (!av && av_prev) vdone = vdone + 1;
        if (av) begin
            if (ah && !ah_prev) begin sln = sln + 1; sxx = 0; scnt[vf % 4] = sln + 1; end
            if (ah && sln >= 0 && sln < 240 && sxx < 640) sh[(vf % 4) * FS + sln * 640 + sxx] = rgbi;
            if (ah) begin sxx = sxx + 1; swd[vf % 4] = sxx; end
            if (!ah && ah_prev) sbd[vf % 4] = rgbi;
        end
        av_prev <= av; ah_prev <= ah;
    end

    // ---- the checks
    integer fails = 0;
    task check(input ok, input [8*110-1:0] what);
        begin
            if (ok) $display("ok   %0s", what);
            else begin $display("FAIL %0s", what); fails = fails + 1; end
        end
    endtask

    integer nrep = 0;                                 // frame reports seen
    integer seq_bad = 0, buf_bad = 0, lat_bad = 0;
    reg [7:0] lat_k [0:15];
    integer r, c, f, bad, dbl_bad, first_r, first_c, nd;
    reg [127:0] word;
    reg [15:0] got, exp;
    reg [3:0] seen_col [0:15];
    // does buffer b hold frame f (source line y in rows 2y and 2y+1)? returns the mismatch count, remembers the first one
    task cmp_frame(input integer b, input integer fr);
        begin
            bad = 0; dbl_bad = 0; first_r = -1; first_c = -1;
            for (r = 0; r < 400; r = r + 1) begin
                for (c = 0; c < 640; c = c + 1) begin
                    word = ddr[b * BW + r * 80 + c / 8];
                    got = word >> ((c % 8) * 16);
                    exp = pal(sh[(fr % 4) * FS + (r / 2) * 640 + c]);
                    if (got !== exp) begin
                        if (bad == 0) begin first_r = r; first_c = c; $display("     row %0d col %0d: %h, expected %h (frame %0d buffer %0d)", r, c, got, exp, fr, b); end
                        bad = bad + 1;
                    end
                end
                if (r % 2 == 1) for (c = 0; c < 80; c = c + 1) if (ddr[b * BW + r * 80 + c] !== ddr[b * BW + (r - 1) * 80 + c]) dbl_bad = dbl_bad + 1;
            end
        end
    endtask

    // does buffer b hold the woven picture whose field 1 is VDC frame fr (field 0 = frame fr-1): row 2y = line y of the field 0, row 2y+1 = line y
    // of the field 1, for the lines both fields had? returns the mismatch count in `bad`, the number of rows compared in `nrows`
    integer wv_bad = 0, nrows = 0, s0, s1, nl;
    reg fld_ok;
    task cmp_woven(input integer b, input integer fr);
        begin
            bad = 0; nrows = 0; first_r = -1;
            s0 = (fr + 3) % 4; s1 = fr % 4;
            fld_ok = (sfld[s0] === 1'b0) && (sfld[s1] === 1'b1) && sil[s0] && sil[s1];
            nl = scnt[s0] < scnt[s1] ? scnt[s0] : scnt[s1];
            for (r = 0; r < 2 * nl; r = r + 1)
                for (c = 0; c < 640; c = c + 1) begin
                    word = ddr[b * BW + r * 80 + c / 8];
                    got = word >> ((c % 8) * 16);
                    exp = pal(sh[((r % 2) ? s1 : s0) * FS + (r / 2) * 640 + c]);
                    if (got !== exp) begin
                        if (bad == 0) begin first_r = r; $display("     woven row %0d col %0d: %h, expected %h (frame %0d buffer %0d)", r, c, got, exp, fr, b); end
                        bad = bad + 1;
                    end
                    if (c == 0) nrows = nrows + 1;
                end
        end
    endtask

    integer il_n = 0, il_buf_bad = 0, il_prev = -1, il_fld_bad = 0, il_cmp = 0, il_glyph_bad = 0, il_cmp_bad = 0, il_rows_min = 100000, il_seq_bad = 0, il_stale_bad = 0;
    integer il_skip = 4;                                 // pairs to let go by before comparing (the VDC is being reprogrammed; C needs more: the picture after B is B's)
    reg [7:0] il_seq_prev = 8'd0;
    integer ph = 1;                                     // 1: not interlaced, 2: text, 3: bitmap
    integer gk, gr, gc, gpat, glyph_chk;
    // a glyph read back from the woven buffer: text row tr (of 10 lines), column cell cc: line k must show the font's line k
    function [7:0] font_line(input integer chr, input integer k);
        begin font_line = ((8'h80 >> ((k + chr) % 8)) | ((k == 0 || k == 9) ? 8'hFF : 8'h00)); end
    endfunction
    // glyphs read back whole from the woven buffer: text row tr, a column in the middle of the line: the buffer row tr * 10 + k must show the font's
    // line k (white = lit). The first lit pixel of line 1 (one pixel per cell) gives the cell's x, which is the same for the other lines.
    integer glyph_rows = 0;
    integer gl_n = 10, gd_n = 9;                        // character lines of the picture (R9 + 1) and the lines that show the glyph (R23 + 1)
    task glyph_check(input integer b);
        integer tr, k, px2, xs;
        reg [7:0] want;
        reg [15:0] pv;
        begin
            glyph_chk = 0;
            for (tr = 5; tr <= 6; tr = tr + 1) begin
                xs = -1;
                for (px2 = 24; px2 < 56 && xs < 0; px2 = px2 + 1) begin
                    word = ddr[b * BW + (tr * gl_n + 1) * 80 + px2 / 8];
                    pv = word >> ((px2 % 8) * 16);
                    if (pv == 16'hFFFF) xs = px2 - ((1 + 8'h40 + (tr % 8)) % 8);
                end
                if (xs < 0) glyph_chk = glyph_chk + 100;
                else for (k = 0; k < gl_n; k = k + 1) begin
                    want = (k < gd_n) ? font_line(8'h40 + (tr % 8), k) : 8'h00;
                    glyph_rows = glyph_rows + 1;
                    for (px2 = 0; px2 < 8; px2 = px2 + 1) begin
                        word = ddr[b * BW + (tr * gl_n + k) * 80 + (xs + px2) / 8];
                        pv = word >> (((xs + px2) % 8) * 16);
                        if ((pv == 16'hFFFF) != want[7 - px2]) glyph_chk = glyph_chk + 1;
                    end
                end
            end
        end
    endtask

    // the bitmap test picture: memory line y holds y & 255 in bytes 0..39 and y >> 8 in bytes 40..79; R25 low nibble 7, so byte j is exactly pixels 8j .. 8j+7
    function integer bm_id(input integer b, input integer row);
        integer j, p2, lo, hi;
        reg [15:0] pv2;
        reg [127:0] w2;
        begin
            lo = 0; hi = 0;
            for (p2 = 0; p2 < 8; p2 = p2 + 1) begin
                w2 = ddr[b * BW + row * 80 + (10 * 8 + p2) / 8]; pv2 = w2 >> (((10 * 8 + p2) % 8) * 16); lo = lo | ((pv2 == 16'hFFFF) ? (128 >> p2) : 0);
                w2 = ddr[b * BW + row * 80 + (50 * 8 + p2) / 8]; pv2 = w2 >> (((50 * 8 + p2) % 8) * 16); hi = hi | ((pv2 == 16'hFFFF) ? (128 >> p2) : 0);
            end
            bm_id = lo + 256 * hi;
        end
    endfunction
    integer bmn, bm_n = 0, bm_abad = 0, bm_bbad = 0;
    reg [8*200-1:0] dumpf = "", dumpf2 = "", dumpf3 = "";
    task dump_ppm(input integer b);
        integer fd, rr, cc;
        reg [15:0] p;
        begin
            fd = $fopen(dumpcur, "wb");
            $fwrite(fd, "P6\n640 480\n255\n");
            for (rr = 0; rr < 480; rr = rr + 1)
                for (cc = 0; cc < 640; cc = cc + 1) begin
                    word = ddr[b * BW + rr * 80 + cc / 8];
                    p = word >> ((cc % 8) * 16);
                    $fwrite(fd, "%c%c%c", {p[15:11], p[15:13]}, {p[10:5], p[10:9]}, {p[4:0], p[4:2]});
                end
            $fclose(fd);
        end
    endtask

    reg armed = 1'b0;                                 // the event at time 0 (initial value of frame_tgl) is not a report
    always @(frame_tgl) if (armed) begin
        nrep = nrep + 1;
        lat_k[nrep % 16] = latest;
        if (seq != nrep[7:0]) seq_bad = seq_bad + 1;
        if (latest[7] !== 1'b1) lat_bad = lat_bad + 1;
        if (ph == 1 && latest[1:0] != (nrep - 1) % 3) buf_bad = buf_bad + 1;
        if (ph == 1 && latest[2]) lat_bad = lat_bad + 1;                      // not interlaced: never reported as woven
        if (ph == 2 && vdc_il[1]) begin
            il_n = il_n + 1;
            if (latest[1:0] > 2 || latest[1:0] == il_prev) il_buf_bad = il_buf_bad + 1;
            il_prev = latest[1:0];
            if (seq != il_seq_prev + 8'd1 && il_n > 1) il_seq_bad = il_seq_bad + 1;
            il_seq_prev = seq;
            if (il_n >= il_skip && il_cmp < 6) begin
                il_cmp = il_cmp + 1;
                cmp_woven(latest[1:0], vf);
                if (nrows < il_rows_min) il_rows_min = nrows;
                if (nrows < (latest[2] ? 480 : 400) || latest[2] != (exp_rows == 480)) il_stale_bad = il_stale_bad + 1;   // the app is told 480 rows only when 480 were written
                if (!fld_ok) il_fld_bad = il_fld_bad + 1;
                if (bad != 0) il_cmp_bad = il_cmp_bad + 1;
                if (bmmode) glyph_chk = 0; else glyph_check(latest[1:0]);   // the glyph check reads text
                if (glyph_chk != 0) il_glyph_bad = il_glyph_bad + 1;
                $display("     woven picture %0d: buffer %0d, frame %0d, %0d rows compared, %0d pixel mismatches, glyph mismatches %0d", il_cmp, latest[1:0], vf, nrows, bad, glyph_chk);
                if (il_cmp == 4 && dumpcur != "") dump_ppm(latest[1:0]);
                if (bmmode) begin
                    // field 0 (even rows) shows memory lines 0, 1, 2 ..., field 1 carries on right after them: line 240 + y (T257 fault 2, the layout
                    // of the real-hardware programs); the 5th line of every 5-line cell is blank (R23 = 8: 9 of the 10 lines show)
                    for (bmn = 0; bmn < 240; bmn = bmn + 1) begin
                        if (bm_id(latest[1:0], 2 * bmn) != bmn) bm_abad = bm_abad + 1;
                        if (bm_id(latest[1:0], 2 * bmn + 1) != ((bmn % 5 == 4) ? 0 : 240 + bmn)) bm_bbad = bm_bbad + 1;
                    end
                    bm_n = bm_n + 1;
                end
            end
        end
        if (ph == 5) begin
            nw_n = nw_n + 1;
            if (nw_n > nw_skip && nw_cmp < 5) begin
                nw_cmp = nw_cmp + 1;
                if (latest[2] !== 1'b0) nw_b2_bad = nw_b2_bad + 1;
                if (latest[1:0] > 2 || latest[1:0] == nw_prev) nw_buf_bad = nw_buf_bad + 1;
                cmp_narrow(latest[1:0], vf);
            end
            nw_prev = latest[1:0];
        end
        if (ph == 4) begin
            tl_n = tl_n + 1;
            if (tl_n > tl_skip && tl_cmp < 5) begin
                tl_cmp = tl_cmp + 1;
                if (latest[2] !== 1'b1) tl_b2_bad = tl_b2_bad + 1;
                if (latest[1:0] > 2 || latest[1:0] == tl_prev) tl_buf_bad = tl_buf_bad + 1;
                cmp_tall(latest[1:0], vf);
            end
            tl_prev = latest[1:0];
        end
        if (ph == 1 && nrep <= 8) begin
            cmp_frame(latest[1:0], nrep);
            check(bad == 0, "S9 report: every pixel of the reported frame is in its buffer, both rows of every source line");
            check(dbl_bad == 0, "S9 report: row 2y+1 equals row 2y (lines doubled)");
        end
    end

    // ---- the VDC's picture: 80 x 25 text with attributes, the screen made with fills and copies
    integer ua_n, wcnt;
    task automatic set_ua(input [15:0] a);
        begin wr_reg(8'd18, a[15:8]); wr_reg(8'd19, a[7:0]); end
    endtask
    task automatic put(input [7:0] v);
        begin wr_sel(8'd31); wr_dat(v); wait_ready; end
    endtask
    task automatic fill(input [15:0] a, input [7:0] v, input integer n);
        integer rest, kk;
        begin
            set_ua(a); put(v);
            rest = n - 1;
            while (rest > 0) begin
                kk = rest > 256 ? 256 : rest;
                wr_reg(8'd30, kk[7:0]);
                rest = rest - kk;
            end
        end
    endtask
    integer ci;
    reg [7:0] init_r [0:63];
    initial begin
        init_r[0]=126; init_r[1]=80; init_r[2]=102; init_r[3]=8'h49; init_r[4]=32; init_r[5]=0; init_r[6]=25; init_r[7]=29;
        init_r[8]=0; init_r[9]=7; init_r[10]=8'h20; init_r[11]=7; init_r[12]=0; init_r[13]=0; init_r[14]=0; init_r[15]=0;
        init_r[20]=8; init_r[21]=0; init_r[22]=8'h78; init_r[23]=8; init_r[24]=8'h20; init_r[25]=8'h47; init_r[26]=8'hF4;
        init_r[27]=0; init_r[28]=8'h30; init_r[29]=7; init_r[34]=125; init_r[35]=100; init_r[36]=5;
    end
    task automatic program_vdc;
        begin
            for (ci = 0; ci < 37; ci = ci + 1)
                if ((ci < 16) || (ci >= 20 && ci <= 29) || (ci >= 34)) wr_reg(ci[7:0], init_r[ci]);
            fill(16'h2000, 8'h5A, 4096);                       // every glyph: vertical stripes
            for (ci = 0; ci < 25; ci = ci + 1) fill(ci * 80, 8'h30 + ci, 80);
            set_ua(16'h0800);                                   // one row of attributes, then copied down: byte-wise overlap copy
            for (ci = 0; ci < 80; ci = ci + 1) put(((ci % 16) | ((ci % 5 == 0) ? 8'h40 : 8'h00)));
            wr_reg(8'd32, 8'h08); wr_reg(8'd33, 8'h00); wr_reg(8'd24, 8'hA0);
            set_ua(16'h0850);
            for (ci = 0; ci < 8; ci = ci + 1) wr_reg(8'd30, (ci == 7) ? 8'd128 : 8'd0);   // 7 x 256 + 128 = 1920 = 24 rows x 80
            wr_reg(8'd24, 8'h20);
        end
    endtask

    // ---- phase 2: interlace sync and video, Busted Boulders' registers, a synthetic 10-line font (code 0x40 + i, line k: a diagonal and bars), text
    task automatic program_il_text(input [4:0] ctv, input [7:0] rows, input [7:0] r23);
        integer ch, kk, ri;
        reg [7:0] regs_il [0:29];
        begin
            regs_il[0]=8'h7E; regs_il[1]=8'h50; regs_il[2]=8'h66; regs_il[3]=8'h49; regs_il[4]=8'h33; regs_il[5]=8'd7; regs_il[6]=rows; regs_il[7]=8'h32;
            regs_il[8]=8'd3; regs_il[9]={3'd0, ctv}; regs_il[10]=8'h20; regs_il[11]=8'd9; regs_il[12]=8'h00; regs_il[13]=8'h00; regs_il[14]=8'h00; regs_il[15]=8'h00;
            regs_il[20]=8'h10; regs_il[21]=8'h00; regs_il[22]=8'h78; regs_il[23]=r23; regs_il[24]=8'h00; regs_il[25]=8'h40; regs_il[26]=8'hF0; regs_il[27]=8'd0; regs_il[28]=8'h30; regs_il[29]=8'd7;
            fill(16'h2000, 8'h00, 4096);
            for (ch = 0; ch < 8; ch = ch + 1) begin
                set_ua(16'h2000 + (8'h40 + ch) * 16);
                for (kk = 0; kk < 16; kk = kk + 1) put(kk <= ctv ? font_line(8'h40 + ch, kk) : 8'h00);
            end
            for (ri = 0; ri < rows; ri = ri + 1) fill(ri * 80, 8'h40 + (ri % 8), 80);
            fill(16'h1000, 8'h0F, rows * 80);
            gl_n = ctv + 1; gd_n = r23 + 1;
            for (ri = 0; ri < 30; ri = ri + 1) if ((ri < 16) || (ri >= 20)) wr_reg(ri[7:0], regs_il[ri]);
        end
    endtask

    task automatic program_il_bitmap;
        integer yy;
        reg [7:0] regs_il [0:29];
        begin
            regs_il[0]=8'h7E; regs_il[1]=8'h50; regs_il[2]=8'h66; regs_il[3]=8'h49; regs_il[4]=8'h33; regs_il[5]=8'd7; regs_il[6]=8'h30; regs_il[7]=8'h32;
            regs_il[8]=8'd3; regs_il[9]=8'd9; regs_il[10]=8'h20; regs_il[11]=8'd9; regs_il[12]=8'h40; regs_il[13]=8'h00; regs_il[14]=8'h00; regs_il[15]=8'h00;
            regs_il[20]=8'h10; regs_il[21]=8'h00; regs_il[22]=8'h78; regs_il[23]=8'd8; regs_il[24]=8'h00; regs_il[25]=8'h87; regs_il[26]=8'hF0; regs_il[27]=8'd0; regs_il[28]=8'h30; regs_il[29]=8'd7;
            for (yy = 0; yy < 480; yy = yy + 1) begin fill(16'h4000 + yy * 80, yy[7:0], 40); fill(16'h4000 + yy * 80 + 40, yy >> 8, 40); end
            for (yy = 0; yy < 30; yy = yy + 1) if ((yy < 16) || (yy >= 20)) wr_reg(yy[7:0], regs_il[yy]);
        end
    endtask
    integer exp_rows = 480, cfg_n = 0;
    reg bmmode = 1'b0;
    reg [8*200-1:0] dumpcur = "";
    task automatic run_il(input integer cfg);
        integer nn, nstray, kq;
        begin
            ph = 2; il_n = 0; il_cmp = 0; il_buf_bad = 0; il_fld_bad = 0; il_glyph_bad = 0; il_cmp_bad = 0; il_rows_min = 100000; il_seq_bad = 0; il_stale_bad = 0; il_prev = -1;
            glyph_rows = 0; il_skip = (cfg == 2) ? 7 : 4;
            dumpcur = (cfg == 0) ? dumpf : dumpf2;
            if (cfg == 0) begin program_il_text(5'd9, 8'h30, 8'd8); exp_rows = 480; end         // Busted Boulders: 10-line characters, 48 rows, R23 = 8
            else if (cfg == 1) begin program_il_text(5'd7, 8'h32, 8'd7); exp_rows = 400; end
            else          begin program_il_text(5'd9, 8'h2C, 8'd8); exp_rows = 400; end         // C: 44 rows x 10 = 220 lines a field: not 240, so 400 rows are shown (440 written)         // 8-line characters, 50 rows: 640 x 400 interlaced
            nn = 0;
            while (il_cmp < 6 && nn < 60000000) begin @(posedge clk); nn = nn + 1; end
            repeat (30000) @(posedge clk);
            check(il_cmp >= 6, cfg == 0 ? "S9 interlace A: six woven pictures were reported (latest b2, one per field pair)" : "S9 interlace B: six woven pictures were reported (latest b2, one per field pair)");
            check(il_fld_bad == 0, cfg == 0 ? "S9 interlace A: every picture is field 0 followed by field 1 (the field flag alternates)" : "S9 interlace B: every picture is field 0 followed by field 1 (the field flag alternates)");
            check(il_cmp_bad == 0, cfg == 0 ? "S9 interlace A: row 2y = line y of field 0, row 2y+1 = line y of field 1, every pixel" : "S9 interlace B: row 2y = line y of field 0, row 2y+1 = line y of field 1, every pixel");
            check(il_rows_min >= exp_rows, cfg == 0 ? "S9 interlace A: 240 lines a field = 480 rows compared" : "S9 interlace B: 200 lines a field = 400 rows compared");
            check(il_stale_bad == 0, cfg == 0 ? "S9 interlace A: reports b2 = 1 (480 rows) and all 480 rows are this picture" : "S9 interlace B/C (after the taller A): reports b2 = 0 (400 rows), rows 400..479 hold the older picture and are not shown");
            check(il_glyph_bad == 0, cfg == 0 ? "S9 interlace A: a 10-line glyph is whole in the woven rows (lines 0..9 in order, R23 = 8)" : "S9 interlace B: an 8-line glyph is whole in the woven rows (lines 0..7 in order)");
            check(il_buf_bad == 0 && il_seq_bad == 0, cfg == 0 ? "S9 interlace A: one report per field pair, the buffers go round, seq +1 each" : "S9 interlace B: one report per field pair, the buffers go round, seq +1 each");
            check(overruns == 16'd0, cfg == 0 ? "S9 interlace A: no late line (overruns == 0)" : "S9 interlace B: no late line (overruns == 0)");
            nstray = 0;
            for (kq = 0; kq < NW; kq = kq + 1)
                if (wrote[kq] && !((kq % BW) < BUFWORDS && (kq / BW) < 3)) nstray = nstray + 1;
            check(nstray == 0 && bad_cmd == 0 && bad_addr == 0, "S9 interlace: nothing outside the three 640 x 480 buffers, every command a plain write");
            $display("     config %0d: %0d woven pictures, %0d glyph rows checked, %0d reports in all, seq %0d, overruns %0d", cfg, il_n, glyph_rows, nrep, seq, overruns);
        end
    endtask

    // ---- phase 1b: a frame that is not interlaced with more than 200 lines
    integer tl_n = 0, tl_cmp = 0, tl_bad = 0, tl_b2_bad = 0, tl_buf_bad = 0, tl_lines = 0, tl_seen_lines = 0, tl_prev = -1, tl_skip = 3, tl_first_r = -1;
    integer tr, tc, tfr, tshow;
    task cmp_tall(input integer b, input integer fr);
        integer y, ln2;
        begin
            ln2 = scnt[fr % 4]; if (ln2 > 240) ln2 = 240;
            tl_seen_lines = scnt[fr % 4];
            for (tr = 0; tr < 480; tr = tr + 1) begin
                y = tr / 2;
                for (tc = 0; tc < 640; tc = tc + 1) begin
                    word = ddr[b * BW + tr * 80 + tc / 8];
                    got = word >> ((tc % 8) * 16);
                    exp = (y < ln2) ? pal(sh[(fr % 4) * FS + y * 640 + tc]) : pal(sbd[fr % 4]);
                    if (got !== exp) begin
                        if (tl_bad == 0) $display("     tall row %0d col %0d: %h, expected %h (frame %0d buffer %0d, %0d lines)", tr, tc, got, exp, fr, b, ln2);
                        tl_bad = tl_bad + 1;
                    end
                end
            end
        end
    endtask
    task automatic run_tall(input integer rows, input [7:0] r7, input integer lines_expected);
        integer nn;
        begin
            ph = 4; tl_n = 0; tl_cmp = 0; tl_bad = 0; tl_b2_bad = 0; tl_buf_bad = 0; tl_prev = -1; tl_skip = 3; tl_lines = lines_expected;
            wr_reg(8'd7, r7); wr_reg(8'd6, rows[7:0]);
            nn = 0;
            while (tl_cmp < 5 && nn < 40000000) begin @(posedge clk); nn = nn + 1; end
            repeat (30000) @(posedge clk);
            check(tl_cmp >= 5, "S9 tall: five frames of more than 200 lines were reported");
            check(tl_seen_lines == lines_expected, "S9 tall: the VDC frame has the expected number of lines");
            check(tl_bad == 0, "S9 tall: every one of the 480 rows is the frame (source line y in rows 2y, 2y+1; rows after the last line in the border colour)");
            check(tl_b2_bad == 0, "S9 tall: latest b2 = 1 (480 rows are this picture)");
            check(tl_buf_bad == 0, "S9 tall: the buffers go round");
            check(overruns == 16'd0, "S9 tall: no late line (overruns == 0)");
        end
    endtask
    // ---- phase 1c (T257 fault 7): a frame narrower than 640 dots and/or shorter than 200 lines, as rfovdc2's plasma (R1 = 56, 192 lines) and outline (100 lines) parts
    // have them: every row 0..399 of the buffer is the frame (source line y in rows 2y, 2y+1), the dots after the active part and the rows after the last line are the
    // colour of the first dot after the active part
    integer nw_n = 0, nw_cmp = 0, nw_bad = 0, nw_lines = 0, nw_width = 0, nw_rows_bad = 0, nw_b2_bad = 0, nw_buf_bad = 0, nw_prev = -1, nw_skip = 3;
    integer nr_, nc_, ny_;
    task cmp_narrow(input integer b, input integer fr);
        integer wd, ln3;
        begin
            wd = swd[fr % 4]; ln3 = scnt[fr % 4]; nw_width = wd; nw_lines = ln3;
            for (nr_ = 0; nr_ < 400; nr_ = nr_ + 1) begin
                ny_ = nr_ / 2;
                for (nc_ = 0; nc_ < 640; nc_ = nc_ + 1) begin
                    word = ddr[b * BW + nr_ * 80 + nc_ / 8];
                    got = word >> ((nc_ % 8) * 16);
                    exp = (ny_ < ln3 && nc_ < wd) ? pal(sh[(fr % 4) * FS + ny_ * 640 + nc_]) : pal(sbd[fr % 4]);
                    if (got !== exp) begin
                        if (nw_bad == 0) $display("     narrow row %0d col %0d: %h, expected %h (frame %0d buffer %0d, %0d lines of %0d dots)", nr_, nc_, got, exp, fr, b, ln3, wd);
                        nw_bad = nw_bad + 1;
                    end
                end
            end
        end
    endtask
    task automatic run_narrow(input [7:0] r1, input [7:0] rows, input integer lines_expected, input integer width_expected, input [8*40-1:0] name);
        integer nn;
        begin
            ph = 5; nw_n = 0; nw_cmp = 0; nw_bad = 0; nw_b2_bad = 0; nw_buf_bad = 0; nw_prev = -1; nw_skip = 4;
            wr_reg(8'd1, r1); wr_reg(8'd6, rows);
            nn = 0;
            while (nw_cmp < 5 && nn < 40000000) begin @(posedge clk); nn = nn + 1; end
            repeat (30000) @(posedge clk);
            check(nw_cmp >= 5, "S9 narrow: five frames of the narrow / short picture were reported");
            check(nw_lines == lines_expected && nw_width == width_expected, "S9 narrow: the VDC frame has the expected lines and active dots a line");
            check(nw_bad == 0, "S9 narrow: rows 0..399 are the frame, the dots and rows beyond it are the border colour (no older picture)");
            check(nw_b2_bad == 0, "S9 narrow: latest b2 = 0 for a frame of up to 200 lines (400 rows)");
            check(nw_buf_bad == 0, "S9 narrow: the buffers go round");
            check(overruns == 16'd0, "S9 narrow: no late line (overruns == 0)");
            $display("     narrow %0s: %0d lines of %0d dots, %0d reports checked", name, nw_lines, nw_width, nw_cmp);
        end
    endtask
    integer maxcyc, nf, n;
    integer stray, last3_bad, distinct, ndist, kk2, nstray;
    reg [15:0] seen_mask;
    initial begin
        if (!$value$plusargs("maxcyc=%d", maxcyc)) maxcyc = 160000000;
        if (!$value$plusargs("dump=%s", dumpf)) dumpf = "";
        if (!$value$plusargs("dump2=%s", dumpf2)) dumpf2 = "";
        if (!$value$plusargs("dump3=%s", dumpf3)) dumpf3 = "";
        nf = 6;
        repeat (16) @(posedge clk);
        rst <= 1'b0;
        repeat (70000) @(posedge clk);
        program_vdc;
        repeat (3) begin                                    // let the VDC show the new picture; switch the writer on in the blanking
            n = 0; while (!(vb === 1'b1) && n < 2000000) begin @(posedge clk); n = n + 1; end
            n = 0; while (!(vb === 1'b0) && n < 2000000) begin @(posedge clk); n = n + 1; end
        end
        n = 0; while (!(vb === 1'b1) && n < 2000000) begin @(posedge clk); n = n + 1; end
        armed = 1'b1;
        wr_en = 1'b1;
        n = 0;
        while (nrep < nf && n < 20000000) begin
            @(posedge clk); n = n + 1;
            if (nrep == 2 && wr_en && init_r[26] == 8'hF4) begin init_r[26] = 8'h2B; wr_reg(8'd26, 8'h2B); end   // later frames differ
        end
        if (wr_en) begin
            repeat (30000) @(posedge clk);                  // the last report is in
            check(nrep >= nf, "S9 six frames were reported");
            check(nrep == vdone, "S9 one report per VDC frame");
            check(seq_bad == 0, "S9 seq counts the frames 1, 2, 3, ...");
            check(lat_bad == 0, "S9 latest is valid at every report");
            check(buf_bad == 0, "S9 the buffers go round 0, 1, 2, 0, ...");
            check(overruns == 16'd0, "S9 no underrun / late line (overruns == 0)");
            // the last three frames are still in their three buffers
            last3_bad = 0;
            for (kk2 = nrep - 2; kk2 <= nrep; kk2 = kk2 + 1) begin
                cmp_frame((kk2 - 1) % 3, kk2);
                if (bad != 0 || dbl_bad != 0) last3_bad = last3_bad + 1;
            end
            check(last3_bad == 0, "S9 the last three frames are in their buffers (stride)");
            // nothing outside the three buffers
            stray = 0;
            for (k = 0; k < NW; k = k + 1)
                if (wrote[k] && !((k % BW) < BUFWORDS && (k / BW) < 3)) stray = stray + 1;
            check(stray == 0, "S9 no word written outside the three buffers (BUFWORDS = 640 x 480)");
            check(bad_cmd == 0 && bad_addr == 0, "S9 every command is a plain write: cmd 0, data with it, mask 0, 16 byte aligned");
            // the picture carried colours
            seen_mask = 16'd0;
            for (k = 0; k < FS; k = k + 1) seen_mask[sh[(nrep % 4) * FS + k]] = 1'b1;
            ndist = 0;
            for (k = 0; k < 16; k = k + 1) if (seen_mask[k]) ndist = ndist + 1;
            $display("     %0d frames, %0d writes, %0d distinct colours in the last frame, seq %0d, overruns %0d", nrep, writes, ndist, seq, overruns);
            check(ndist >= 8, "S9 the test picture carries at least 8 colours");
        end
        // ---- phase 2: interlace (text), two configurations
        stray = 0;
        for (k = 0; k < NW; k = k + 1) if (wrote[k] && (k % BW) >= 400 * 80) stray = stray + 1;
        check(stray == 0, "S9 a frame that is not interlaced writes rows 0..399 only (nothing in rows 400..479)");
        run_tall(30, 8'd31, 240);                           // 30 rows x 8 lines: the whole 480 rows are written by the frame itself
        run_tall(28, 8'd30, 224);                           // 28 rows: the rows after line 223 are the writer's blanks (border colour)
        nstray = 0;
        for (k = 0; k < NW; k = k + 1) if (wrote[k] && !((k % BW) < BUFWORDS && (k / BW) < 3)) nstray = nstray + 1;
        check(nstray == 0, "S9 tall: nothing written outside the three 640 x 480 buffers");
        run_narrow(8'd56, 8'd24, 192, 448, "plasma 56 x 24");            // narrow and short
        run_narrow(8'd80, 8'd12, 96, 640, "outline 80 x 12");            // full width, 96 lines
        run_il(0);
        run_il(1);
        run_il(2);
        // phase 3: the interlaced bitmap (R25[7] = 1, R8 = 3), the memory line of every woven row
        bmmode = 1'b1;
        dumpcur = dumpf3;
        program_il_bitmap;
        exp_rows = 480; il_stale_bad = 0;
        ph = 2; il_n = 0; il_cmp = 0; il_prev = -1; il_cmp_bad = 0; il_buf_bad = 0; il_fld_bad = 0; il_seq_bad = 0;
        n = 0;
        while (il_cmp < 6 && n < 60000000) begin @(posedge clk); n = n + 1; end
        repeat (30000) @(posedge clk);
        check(il_cmp >= 6 && bm_n >= 3, "S9 interlaced bitmap: six woven pictures were reported");
        check(il_fld_bad == 0 && il_buf_bad == 0 && il_seq_bad == 0, "S9 interlaced bitmap: field 0 then field 1, one report per pair, buffers go round, seq +1");
        check(il_cmp_bad == 0, "S9 interlaced bitmap: row 2y = line y of field 0, row 2y+1 = line y of field 1, every pixel");
        check(bm_abad == 0, "S9 interlaced bitmap: field 0 shows memory lines 0..239 in order (it starts at the screen address)");
        check(bm_bbad == 0, "S9 interlaced bitmap: field 1 carries on right after field 0 (line 240 + y, T257 fault 2)");
        check(il_stale_bad == 0, "S9 interlaced bitmap: b2 = 1 and all 480 rows are this picture");
        check(overruns == 16'd0, "S9 interlaced bitmap: no late line (overruns == 0)");
        $display("     bitmap: %0d woven pictures checked, field 0 mismatches %0d, field 1 mismatches %0d", bm_n, bm_abad, bm_bbad);
        if (fails == 0) $display("PASS tb_c128_pixw"); else $display("FAILED tb_c128_pixw: %0d", fails);
        $finish;
    end
    always @(posedge clk) if (cyc > maxcyc) begin $display("FAILED tb_c128_pixw: timeout after %0d clocks", cyc); $finish; end
endmodule
