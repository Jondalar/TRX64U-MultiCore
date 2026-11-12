// T194 S1 (the C128's copy of pix_layer, cores/c128/mono/host/pix_layer.v): the capture the 40-column picture is shown from. Rows are padded to 416
// pixels (13 words, nibble x mod 32 of word 13 y + x / 32, the pad nibbles 0), three slots in rotation 0, 1, 2, 0 ... (STILL_STATUS b0 valid, b1 60 Hz,
// b3:2 slot). Derived from host/sim/pixlayer/tb_still.v:
// pix_layer's still capture at 50 Hz and 60 Hz (T12 section 5.2), with mig_arb
// and the MIG UI model, against a synthetic VIC: 63 x 312 cycles (50 Hz), then
// 65 x 263 (60 Hz, v60), then 50 Hz again. Per phase: exactly one frame-done
// toggle per VIC frame, STILL_STATUS b2 = the mode, the frame counter follows
// the toggles, and every pixel of the slot STILL_STATUS names equals the VIC's
// pixel in the window (50 Hz: 400 x 270 from line 16; 60 Hz: 400 x 240, lines
// 31-262 are rows 0-231, lines 0-7 of the same frame rows 232-239), x from 96.
`timescale 1ps/1ps
module tb;
    reg cpu_clk = 0, ui_clk = 0, vclk = 0;
    always #5000  cpu_clk = ~cpu_clk;
    always #6667  ui_clk  = ~ui_clk;
    always #15859 vclk    = ~vclk;

    // the default slots 0x5050000 wrap to 0x050000-0x06FFFF in the 8 MB model
    localparam [31:0] STILL = 32'h040000;       // 0x05050000 with bits 17:0 dropped, wrapped into the 8 MB model

    wire ui_rst, calib;
    wire [26:0] app_addr; wire [2:0] app_cmd; wire app_en, app_rdy, app_wdf_wren, app_wdf_rdy;
    wire [127:0] app_wdf_data, app_rd_data; wire [15:0] app_wdf_mask; wire app_rd_data_valid;
    mig_ui_model #(.MEM_WORDS(1 << 19)) mig (.ui_clk(ui_clk), .ui_rst(ui_rst), .calib_done(calib),
        .app_addr(app_addr), .app_cmd(app_cmd), .app_en(app_en), .app_rdy(app_rdy),
        .app_wdf_data(app_wdf_data), .app_wdf_mask(app_wdf_mask), .app_wdf_wren(app_wdf_wren),
        .app_wdf_rdy(app_wdf_rdy), .app_rd_data(app_rd_data), .app_rd_data_valid(app_rd_data_valid));

    wire [2:0] req_tgl, cl_we; wire [77:0] cl_addr; wire [95:0] cl_wdata; wire [11:0] cl_be;
    wire [127:0] cl_line; wire [63:0] cl_rdata; wire [2:0] done_tgl;
    reg  [26:0] k_addr = 27'h0380000; reg k_en = 0; wire k_rdy, k_valid;
    wire [26:0] v_addr; wire [2:0] v_cmd; wire v_en, v_rdy, v_wdf_wren, v_wdf_rdy, v_valid, v_urgent;
    wire [127:0] v_wdf_data, k_data; wire [15:0] v_wdf_mask;
    mig_arb arb (.ui_clk(ui_clk), .ui_rst(ui_rst), .calib_done(calib),
        .req_tgl(req_tgl), .addr(cl_addr), .we(cl_we), .wdata(cl_wdata), .be(cl_be),
        .done_tgl(done_tgl), .rdata(cl_rdata), .rdata_line(cl_line),
        .c_addr(k_addr), .c_cmd(3'b001), .c_en(k_en), .c_rdy(k_rdy), .c_wdf_data(128'd0),
        .c_wdf_mask(16'hFFFF), .c_wdf_wren(1'b0), .c_wdf_rdy(), .c_rd_data(k_data), .c_rd_data_valid(k_valid),
        .v_addr(v_addr), .v_cmd(v_cmd), .v_en(v_en), .v_rdy(v_rdy), .v_wdf_data(v_wdf_data),
        .v_wdf_mask(v_wdf_mask), .v_wdf_wren(v_wdf_wren), .v_wdf_rdy(v_wdf_rdy), .v_rd_data_valid(v_valid), .v_urgent(v_urgent),
        .e_addr(27'd0), .e_cmd(3'd0), .e_en(1'b0), .e_rdy(), .e_wdf_data(128'd0), .e_wdf_mask(16'd0),
        .e_wdf_wren(1'b0), .e_rd_data_valid(),
        .app_addr(app_addr), .app_cmd(app_cmd), .app_en(app_en), .app_rdy(app_rdy),
        .app_wdf_data(app_wdf_data), .app_wdf_mask(app_wdf_mask), .app_wdf_wren(app_wdf_wren),
        .app_wdf_rdy(app_wdf_rdy), .app_rd_data(app_rd_data), .app_rd_data_valid(app_rd_data_valid));

    // The other DDR2 clients at full load, so that the still's writes wait as they do in the machine (as
    // tb_pixlayer's, all from 6 MB up; the still slots are at 0.3 MB): RISC-V (writes and reads), REU/JTAG
    // port, the drive (reads), and cart_fill lines (four reads) back to back, one right after the other
    // (tb_pixlayer: one per 1 us). Not the hog (32 us of 35.5 us): it starves the video client for longer
    // than the capture can buffer, in any design.
    reg load = 0;
    tgl_client #(.SEED(32'h1234567), .BASE(26'h0300000), .WE_PCT(60)) c0 (.clk(cpu_clk), .en(load && calib),
        .req_tgl(req_tgl[0]), .addr(cl_addr[25:0]), .we(cl_we[0]), .wdata(cl_wdata[31:0]), .be(cl_be[3:0]),
        .done_tgl(done_tgl[0]));
    tgl_client #(.SEED(32'h7654321), .BASE(26'h0340000), .WE_PCT(50)) c1 (.clk(vclk), .en(load && calib),
        .req_tgl(req_tgl[1]), .addr(cl_addr[51:26]), .we(cl_we[1]), .wdata(cl_wdata[63:32]), .be(cl_be[7:4]),
        .done_tgl(done_tgl[1]));
    tgl_client #(.SEED(32'h0BADF00D), .BASE(26'h0380000), .WE_PCT(30), .GAP(90)) c2 (.clk(cpu_clk), .en(load && calib),
        .req_tgl(req_tgl[2]), .addr(cl_addr[77:52]), .we(cl_we[2]), .wdata(cl_wdata[95:64]), .be(cl_be[11:8]),
        .done_tgl(done_tgl[2]));
    integer k_left = 0, k_got = 0, k_issued = 0;
    always @(posedge ui_clk) begin
        if (k_en && k_rdy) begin
            k_addr <= k_addr + 8; k_issued = k_issued + 1; k_left = k_left - 1;
            if (k_left <= 0) k_en <= 0;
        end else if (k_en && k_left <= 0)
            k_en <= 0;
        else if (!k_en && load && calib && k_got == k_issued) begin
            k_en <= 1; k_left = 4;
        end
        if (k_valid) k_got = k_got + 1;
    end

    // ---- synthetic VIC: four vclk per pixel, 8 pixels per cycle ------------------------
    reg        v60 = 0;
    reg        m60 = 0;                       // geometry of the VIC (changes at a frame start)
    reg [1:0]  ph = 0;
    reg [9:0]  px = 0;
    reg [8:0]  py = 0;
    integer    vframes = 0;
    wire [9:0] pxmax = m60 ? 10'd519 : 10'd503;
    wire [8:0] pymax = m60 ? 9'd262 : 9'd311;
    always @(posedge vclk) begin
        ph <= ph + 1;
        if (ph == 3) begin
            px <= (px == pxmax) ? 0 : px + 1;
            if (px == pxmax) begin
                py <= (py == pymax) ? 0 : py + 1;
                if (py == pymax) begin vframes <= vframes + 1; m60 <= v60; end
            end
        end
    end
    function [3:0] pv(input integer x, input integer y);
        pv = (x * 3 + y * 5 + (x >> 3) + (y >> 2) * 7) & 15;
    endfunction
    wire [3:0] pix = pv(px, py);

    reg         s_we = 0, s_re = 0; reg [11:0] s_addr = 0; reg [7:0] s_wdata = 0;
    wire [7:0]  s_rdata;
    wire [23:0] o_rgb; wire o_de, o_hs, o_vs; wire [11:0] o_cx; wire [10:0] o_cy;
    pix_layer dut (.clk_sys(cpu_clk), .we(s_we), .re(s_re), .addr(s_addr), .wdata(s_wdata),
        .rdata(s_rdata), .ev_vsync(), .ev_flip(),
        .ui_clk(ui_clk), .ui_rst(ui_rst), .v_addr(v_addr), .v_cmd(v_cmd), .v_en(v_en), .v_rdy(v_rdy),
        .v_wdf_data(v_wdf_data), .v_wdf_mask(v_wdf_mask), .v_wdf_wren(v_wdf_wren),
        .v_rd_data(k_data), .v_rd_data_valid(v_valid), .v_urgent(v_urgent),
        .vclk(vclk), .pix(pix), .px(px), .py(py), .v60(v60),
        .pal_clk(cpu_clk), .pal_we(1'b0), .pal_addr(6'd0), .pal_wdata(8'd0),
        .clk(cpu_clk), .ce(1'b1), .hact(12'd1920), .vact(11'd1080), .xact0(12'd768), .yact0(11'd45), .rgb_in(24'd0), .de_in(1'b0), .hs_in(1'b0), .vs_in(1'b0),
        .cx_in(12'd0), .cy_in(11'd0),
        .rgb(o_rgb), .de(o_de), .hs(o_hs), .vs(o_vs), .cx(o_cx), .cy(o_cy));

    // exact-output hash of every DDR2 write the video client makes, per VIC frame (the old module and a new
    // one must print the same), and the ui_clk from the write offered to it accepted (the 2-flop crossing
    // comes on top)
    reg [63:0] h_wr = 64'hCBF29CE484222325;
    integer    n_wr = 0, n_wr_tot = 0, wl_cnt = 0, wl_max = 0;
    always @(posedge ui_clk)
        if (v_en && v_cmd == 3'b000 && v_rdy) begin
            h_wr = (h_wr ^ {37'd0, v_addr}) * 64'h100000001B3;
            h_wr = (h_wr ^ v_wdf_data[63:0]) * 64'h100000001B3;
            h_wr = (h_wr ^ v_wdf_data[127:64]) * 64'h100000001B3;
            n_wr = n_wr + 1; n_wr_tot = n_wr_tot + 1;
            if (wl_cnt > wl_max) wl_max = wl_cnt;
            wl_cnt = 0;
        end else if (v_wdf_wren && !v_rdy) wl_cnt = wl_cnt + 1;
    always @(posedge vclk)
        if (dut.emit && dut.in_x && dut.start) begin
            $display("HASH still-writes %0d words %016x", n_wr, h_wr);
            n_wr = 0; h_wr = 64'hCBF29CE484222325;
        end

    task rd8(input [11:0] a, output [7:0] d);
        begin @(negedge cpu_clk); s_addr = a; s_re = 1; @(negedge cpu_clk); s_re = 0; d = s_rdata; end
    endtask
    task rd32(input [11:0] a, output [31:0] d);
        begin rd8(a, d[7:0]); rd8(a + 1, d[15:8]); rd8(a + 2, d[23:16]); rd8(a + 3, d[31:24]); end
    endtask

    // frame-done toggles per VIC frame
    reg settled = 0;
    integer toggles = 0, per_frame = 0, bad_frames = 0, last_vf = 0;
    reg     t_prev = 0;
    always @(posedge vclk) begin
        t_prev <= dut.sd_tgl;
        if (dut.sd_tgl != t_prev) begin toggles = toggles + 1; per_frame = per_frame + 1; end
        if (vframes != last_vf) begin
            // frames 1.. of a phase must show exactly one toggle (the first has none or one)
            if (settled && per_frame != 1) begin bad_frames = bad_frames + 1; $display("frame %0d: %0d toggles", vframes, per_frame); end
            per_frame = 0; last_vf = vframes;
        end
    end
    integer errors = 0;
    // the slot of each complete frame: 0, 1, 2, 0, ... (the first complete frame is slot 0)
    reg [1:0] exp_slot = 2'd0;
    integer   seq_bad = 0;
    reg       t_prev2 = 0;
    always @(posedge vclk) begin
        t_prev2 <= dut.sd_tgl;
        if (dut.sd_tgl != t_prev2) begin
            if (dut.sd_slot != exp_slot) seq_bad = seq_bad + 1;
            exp_slot = (dut.sd_slot == 2'd2) ? 2'd0 : dut.sd_slot + 2'd1;
        end
    end
    task check(input ok, input [8*56-1:0] what);
        begin
            if (!ok) begin errors = errors + 1; $display("FAIL %0s", what); end
            else $display("ok   %0s", what);
        end
    endtask

    reg [31:0] r, r0;
    integer i, j, x, y, bad, t0, words, line;
    reg [3:0] want;
    task phase(input integer is60, input integer frames);
        begin
            // switch at the next frame start, then let the first frames go by
            v60 = is60;
            while (!(py == 9'd0 && px == 10'd1)) @(posedge vclk);
            repeat (frames) begin
                while (!(py == 9'd5 && px == 10'd0)) @(posedge vclk);
                while (!(py == 9'd6)) @(posedge vclk);
                settled = 1;
            end
            while (!(py == 9'd150 && px == 10'd300)) @(posedge vclk);
            rd32(12'h068, r);
            $display("mode %0s: STILL_STATUS %08x, %0d frames, slot %0d", is60 ? "60 Hz" : "50 Hz", r, r[31:8], r[3:2]);
            check(r[0] == 1, "valid");
            check(r[1] == is60, "STILL_STATUS b1 = mode");
            check(r[31:8] == toggles[23:0], "frame counter = frame-done toggles");
            words = is60 ? 3120 : 3510;
            bad = 0;
            for (i = 0; i < words; i = i + 1)
                for (j = 0; j < 32; j = j + 1) begin
                    y = i / 13; x = (i % 13) * 32 + j;
                    if (is60) line = (y >= 232) ? y - 232 : y + 31; else line = y + 16;
                    want = (x < 400) ? pv(96 + x, line) : 4'd0;
                    if (mig.mem[(STILL + r[3:2] * 32'h10000 >> 4) + i][4*j +: 4] !== want) bad = bad + 1;
                end
            $display("%0d of %0d pixels differ", bad, words * 32);
            check(bad == 0, is60 ? "still = VIC window, 400 x 240, rows of 13 words" : "still = VIC window, 400 x 270, rows of 13 words");
            check(bad_frames == 0, "one frame-done toggle per VIC frame");
            settled = 0;
        end
    endtask

    initial begin
        while (!calib) @(posedge ui_clk);
        load = 1;
        phase(0, 3);
        phase(1, 3);
        phase(0, 3);
        $display("still write latency max %0d ui_clk (+3 crossing) for %0d words, 4.06 us = 304 ui_clk; RISC-V max %0d ns, port %0d ns, drive %0d ns",
                 wl_max, n_wr_tot, c0.lat_max / 1000, c1.lat_max / 1000, c2.lat_max / 1000);
        check(seq_bad == 0, "slots rotate 0, 1, 2, 0, ...");
        check(wl_max < 100, "write latency far inside the 304 ui_clk between words");
        $display("%0d toggles, %0d VIC frames, %0d errors", toggles, vframes, errors);
        if (errors) $display("FAILED"); else $display("PASS");
        $finish;
    end
endmodule
