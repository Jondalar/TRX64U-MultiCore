// T113 S9 "both pictures reach the host": the C128's VDC (c128_x -DVDC_WHOLE -DVDC64) -> c128_rgbi -> c128_pixw -> a model of the host's
// e_ port (a command is taken when e_en and e_rdy are both high; DDR2 as 128-bit words). The bench shadows the VDC's own stream
// (pixel_ce / active_h / active_v / rgbi) frame by frame and checks, for every frame the writer reports:
//   - the buffer named in `latest` holds that frame: 640 x 400 RGB565, source line y in rows 2y and 2y+1 (the palette is this
//     bench's own copy of the 16 entries), 1280 bytes a row, buffers fb_stride apart
//   - frame_tgl comes once per VDC frame, seq counts them, the buffers go round 0, 1, 2, no line was late (overruns == 0)
//   - after the run the last three frames are still in their buffers, and nothing was written outside the three buffers
`timescale 1ns / 1ps
module tb_c128_pixw #(parameter integer REPEAT = 2);
    reg clk = 1'b0, ui_clk = 1'b0;
    always #15.859 clk = ~clk;                        // sys_clk 31.5278 MHz
    always #6.667 ui_clk = ~ui_clk;
    reg rst = 1'b1;
    wire [7:0] dout;
    wire [3:0] rgbi;
    wire ce, hb, vb;
    c128_x dut (.clk(clk), .rst(rst), .bus_en(bus_en), .vdc_cs(cs), .vdc_we(we), .vdc_rs(rs), .vdc_din(din), .vdc_dout(dout),
        .vicie_cs(1'b0), .vicie_we(1'b0), .vicie_a0(1'b0), .vicie_din(8'd0), .vicie_dout(), .k_sel(), .fast(),
        .vdc_rgbi(rgbi), .vdc_ce(ce), .vdc_hb(hb), .vdc_vb(vb),
        .vd_clk(1'b0), .vd_addr(16'd0), .vd_we(1'b0), .vd_wdata(8'd0), .vd_rdata());
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
    c128_pixw #(.X0(0), .Y0(0), .COLS(640), .LINES(200), .REPEAT(REPEAT)) pw (
        .sys_clk(clk), .sys_rst(rst), .dotst(ce & wr_en), .rgb565(px), .hblank(hb), .vblank(vb),
        .quiet(quiet), .frame_tgl(frame_tgl), .latest(latest), .seq(seq), .overruns(overruns),
        .fb_base(fb_base), .fb_stride(fb_stride), .ui_clk(ui_clk), .ui_rst(1'b0),
        .e_addr(e_addr), .e_cmd(e_cmd), .e_en(e_en), .e_rdy(e_rdy), .e_wdf_data(e_wdf_data),
        .e_wdf_mask(e_wdf_mask), .e_wdf_wren(e_wdf_wren));

    // ---- the host's e_ port: DDR2 as 128-bit words, a bit set per word written
    localparam integer NW = 262144;                   // 4 MiB
    localparam integer BW = 65536;                    // the bench expects buffers 0x100000 bytes apart = 65536 words
    localparam integer BUFWORDS = 400 * 80;           // 640 x 400 x 2 bytes / 16
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
    localparam integer FS = 640 * 200;
    reg [3:0] sh [0:4*FS-1];                          // four shadow slots, frame f in slot f % 4
    always @(posedge clk) if (ce && wr_en) begin
        if (av && !av_prev) begin vf = vf + 1; sln = -1; end
        if (!av && av_prev) vdone = vdone + 1;
        if (av) begin
            if (ah && !ah_prev) begin sln = sln + 1; sxx = 0; end
            if (ah && sln >= 0 && sln < 200 && sxx < 640) sh[(vf % 4) * FS + sln * 640 + sxx] = rgbi;
            if (ah) sxx = sxx + 1;
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

    reg armed = 1'b0;                                 // the event at time 0 (initial value of frame_tgl) is not a report
    always @(frame_tgl) if (armed) begin
        nrep = nrep + 1;
        lat_k[nrep % 16] = latest;
        if (seq != nrep[7:0]) seq_bad = seq_bad + 1;
        if (latest[7] !== 1'b1) lat_bad = lat_bad + 1;
        if (latest[1:0] != (nrep - 1) % 3) buf_bad = buf_bad + 1;
        if (nrep <= 8) begin
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

    integer maxcyc, nf, n;
    integer stray, last3_bad, distinct, ndist, kk2;
    reg [15:0] seen_mask;
    initial begin
        if (!$value$plusargs("maxcyc=%d", maxcyc)) maxcyc = 40000000;
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
            check(stray == 0, "S9 no word written outside the three 640 x 400 buffers");
            check(bad_cmd == 0 && bad_addr == 0, "S9 every command is a plain write: cmd 0, data with it, mask 0, 16 byte aligned");
            // the picture carried colours
            seen_mask = 16'd0;
            for (k = 0; k < FS; k = k + 1) seen_mask[sh[(nrep % 4) * FS + k]] = 1'b1;
            ndist = 0;
            for (k = 0; k < 16; k = k + 1) if (seen_mask[k]) ndist = ndist + 1;
            $display("     %0d frames, %0d writes, %0d distinct colours in the last frame, seq %0d, overruns %0d", nrep, writes, ndist, seq, overruns);
            check(ndist >= 8, "S9 the test picture carries at least 8 colours");
        end
        if (fails == 0) $display("PASS tb_c128_pixw"); else $display("FAILED tb_c128_pixw: %0d", fails);
        $finish;
    end
    always @(posedge clk) if (cyc > maxcyc) begin $display("FAILED tb_c128_pixw: timeout after %0d clocks", cyc); $finish; end
endmodule
