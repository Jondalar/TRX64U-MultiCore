// pix_layer (pixel layer + still capture) with trx_io's page 6, mig_arb and the
// MIG UI model, against a synthetic 1080p picture and a synthetic VIC, with
// the other DDR2 clients loading the memory (run.sh). The buffers are RGB565
// (stride 1920), with every seventh pixel or so the colour key's value.
// Frames:
// The scale S of a frame is PX_OSIZE = S times the source (the old PX_CTRL b5:4 is gone); the mode is the 1080p one of the host
// (xact0 768, yact0 45, ce always high); the scaler's own bench is host/sim/pixscale.
//   A  960 x 540 at scale 2, buffer 0, global alpha 255, key off (the key's
//      pixels shown); PX_BACK written
//   B  buffer 1 (the flip), global alpha 128, key on; flip and vsync causes
//   C  scale 1 at (100, 50), global alpha 255, key on
//   D  320 x 180 at scale 3 at (640, 300), key off
//   E  960 x 540 at scale 2 while cart_fill hogs MIG 32 or 16 (in turn) of every 35.5 us:
//      lines are late; each must show fully or not at all, never in part,
//      and PX_STATUS counts them
//   F  as A again (recovery)
// Every active pixel of every frame is checked against the blend of the
// buffer's pixel (565 widened by bit replication; the picture below where it
// is the key and the key is on) over the picture below (exact), outside the layer against
// the picture itself. A-D and F: no line may be missing (underrun), with the
// RISC-V, REU/JTAG port, drive and cart_fill at full load. Then the still
// slot STILL_STATUS names is compared with the VIC pattern.
// FMT2=1 (format 2, T136): the same frames with 32-bit pixels
// (format 2, stride 3840): every pixel's 24 bits must come out exactly, its top byte is ignored, the colour key does nothing.
`timescale 1ps/1ps
module tb;
    parameter FMT2 = 0;
    localparam [31:0] F = FMT2 ? 32'h04 : 32'h02;     // PX_CTRL b3:1, the format
    reg cpu_clk = 0, ui_clk = 0, pix_clk = 0, vclk = 0;
    always #5000  cpu_clk = ~cpu_clk;
    always #6667  ui_clk  = ~ui_clk;
    always #3299  pix_clk = ~pix_clk;
    always #15859 vclk    = ~vclk;

    // the still's default slots 0x5050000 wrap to 0x050000-0x06FFFF in the 8 MB model
    localparam [31:0] BUF0 = 32'h080000, BUF1 = 32'h280000, STILL = 32'h500000;

    // ---- MIG, arbiter ------------------------------------------------------------
    wire ui_rst, calib;
    wire [26:0] app_addr; wire [2:0] app_cmd; wire app_en, app_rdy, app_wdf_wren, app_wdf_rdy;
    wire [127:0] app_wdf_data, app_rd_data; wire [15:0] app_wdf_mask; wire app_rd_data_valid;
    mig_ui_model #(.MEM_WORDS(1 << 19)) mig (.ui_clk(ui_clk), .ui_rst(ui_rst), .calib_done(calib),
        .app_addr(app_addr), .app_cmd(app_cmd), .app_en(app_en), .app_rdy(app_rdy),
        .app_wdf_data(app_wdf_data), .app_wdf_mask(app_wdf_mask), .app_wdf_wren(app_wdf_wren),
        .app_wdf_rdy(app_wdf_rdy), .app_rd_data(app_rd_data), .app_rd_data_valid(app_rd_data_valid));

    wire [2:0] req_tgl, done_tgl, cl_we; wire [77:0] cl_addr; wire [95:0] cl_wdata; wire [11:0] cl_be;
    wire [63:0] cl_rdata; wire [127:0] cl_line;
    reg  [26:0] k_addr = 27'h0380000; reg k_en = 0; wire k_rdy, k_valid; wire [127:0] k_data;
    wire [26:0] v_addr; wire [2:0] v_cmd; wire v_en, v_rdy, v_wdf_wren, v_wdf_rdy, v_valid, v_urgent;
    wire [127:0] v_wdf_data; wire [15:0] v_wdf_mask;
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

    // the toggle clients, from 6 MB up (16-bit word addresses): RISC-V (writes
    // and reads, as LVGL drawing), REU/JTAG port, the drive (reads)
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

    // cart_fill: a line (four reads) every 1 us; hog: reads back to back for
    // 2400 or 1200 (in turn) of every 2667 ui_clk
    reg hog = 0;
    integer k_timer = 0, k_left = 0, k_got = 0, k_issued = 0, k_lines = 0; reg k_meas = 0, k_odd = 0;
    time k_t0, k_max = 0;
    always @(posedge ui_clk) begin
        k_timer <= (k_timer == 2666) ? 0 : k_timer + 1;
        if (k_timer == 2666) k_odd <= ~k_odd;
        if (hog) begin
            k_en <= k_timer < (k_odd ? 2400 : 1200);
            if (k_en && k_rdy) begin k_addr <= k_addr + 8; k_issued = k_issued + 1; end
        end else begin
            if (k_en && k_rdy) begin
                k_addr <= k_addr + 8; k_issued = k_issued + 1; k_left = k_left - 1;
                if (k_left <= 0) k_en <= 0;
            end else if (k_en && k_left <= 0)
                k_en <= 0;
            else if (!k_en && load && calib && k_timer % 75 == 0 && k_got == k_issued) begin
                k_en <= 1; k_left = 4; k_t0 = $time; k_meas = k_got == k_issued;
            end
        end
        if (k_valid) begin
            k_got = k_got + 1;
            if (hog) k_meas = 0;
            else if (k_meas && k_got == k_issued && k_left == 0 && !k_en) begin
                k_meas = 0;
                k_lines = k_lines + 1;
                if ($time - k_t0 > k_max) k_max = $time - k_t0;
            end
        end
    end

    // ---- the app's IO block -------------------------------------------------------
    reg         s_we = 0, s_re = 0; reg [16:0] s_addr = 0; reg [7:0] s_wdata = 0;
    wire [7:0]  s_rdata, p6_rdata; wire irq, p6_we, p6_re, ev_vsync, ev_flip;
    trx_io tio (.clk(cpu_clk), .we(s_we), .re(s_re), .addr(s_addr), .wdata(s_wdata), .rdata(s_rdata), .irq(irq),
        .ack(), .rq_tgl(), .rq_addr(), .rq_we(), .rq_wdata(), .rs_tgl(1'b0), .rs_rdata(8'd0),
        .stop_tgl(1'b0), .cap_tgl(1'b0), .eng_tgl(1'b0), .gap_tgl(1'b0),
        .fl_enable(), .fl_magic1(), .fl_magic2(), .fl_mask(), .fl_id(), .evt_tgl(1'b0), .evt_word(32'd0),
        .fl_ack_tgl(), .inval_tgl(), .ee_tgl(1'b0), .ee_addr(), .ee_we(), .ee_wdata(), .ee_rdata(8'd0),
        .p6_we(p6_we), .p6_re(p6_re), .p6_rdata(p6_rdata), .ev_vsync(ev_vsync), .ev_flip(ev_flip), .build(32'd0), .ddr_cal(64'd0));

    // ---- picture below and VIC ----------------------------------------------------
    reg [11:0] h = 0; reg [10:0] vv = 0;
    always @(posedge pix_clk) begin
        h <= (h == 2687) ? 0 : h + 1;
        if (h == 2687) vv <= (vv == 1124) ? 0 : vv + 1;
    end
    function [23:0] bg(input [11:0] x, input [10:0] y);
        bg = {x[7:0], y[7:0] ^ 8'h3C, x[11:8], y[10:7]};
    endfunction
    reg [1:0] ph = 0; reg [9:0] px = 0; reg [8:0] py = 0;
    always @(posedge vclk) begin
        ph <= ph + 1;
        if (ph == 3) begin
            px <= (px == 503) ? 0 : px + 1;
            if (px == 503) py <= (py == 311) ? 0 : py + 1;
        end
    end
    wire [3:0] pix = px[3:0] ^ py[3:0];

    wire [23:0] o_rgb; wire o_de, o_hs, o_vs; wire [11:0] o_cx; wire [10:0] o_cy;
    pix_layer dut (.clk_sys(cpu_clk), .we(p6_we), .re(p6_re), .addr(s_addr[11:0]), .wdata(s_wdata),
        .rdata(p6_rdata), .ev_vsync(ev_vsync), .ev_flip(ev_flip),
        .ui_clk(ui_clk), .ui_rst(ui_rst), .v_addr(v_addr), .v_cmd(v_cmd), .v_en(v_en), .v_rdy(v_rdy),
        .v_wdf_data(v_wdf_data), .v_wdf_mask(v_wdf_mask), .v_wdf_wren(v_wdf_wren),
        .v_rd_data(k_data), .v_rd_data_valid(v_valid), .v_urgent(v_urgent),
        .vclk(vclk), .pix(pix), .px(px), .py(py), .v60(1'b0),
        .clk(pix_clk), .ce(1'b1), .hact(12'd1920), .vact(11'd1080), .xact0(12'd768), .yact0(11'd45), .rgb_in(bg(h, vv)), .de_in(h >= 768 && vv >= 45), .hs_in(1'b0), .vs_in(1'b0),
        .cx_in(h), .cy_in(vv),
        .rgb(o_rgb), .de(o_de), .hs(o_hs), .vs(o_vs), .cx(o_cx), .cy(o_cy));

    // ---- the framebuffers ---------------------------------------------------------------
    localparam [15:0] KEY = 16'hF81F;
    function [15:0] fbpix(input integer id, input integer sx, input integer sy);
        reg [15:0] v;
        begin
            if ((sx + 3 * sy + id) % 7 == 0)
                fbpix = KEY;
            else begin
                v = (sx == 0) ? 16'h0000 : (sx == 1) ? 16'hFFFF :
                    {sx[4:0] ^ (id ? 5'h15 : 5'h00), sy[5:0], sx[9:5]} ^ {sy[9:6], 12'd0} ^ (sx * 37 + sy * 11);
                fbpix = (v == KEY) ? v ^ 16'h0020 : v;
            end
        end
    endfunction
    function [31:0] fbpix32(input integer id, input integer sx, input integer sy);
        reg [31:0] t;
        begin t = sx * 37 + sy * 11; fbpix32 = {8'h5A, sx[7:0] ^ (id ? 8'h55 : 8'h00), sy[7:0] ^ sx[9:8], t[7:0]}; end
    endfunction
    integer id, x, y;
    initial if (FMT2) begin
        for (id = 0; id < 2; id = id + 1)
            for (y = 0; y < 540; y = y + 1)
                for (x = 0; x < 960; x = x + 4)
                    mig.mem[((id ? BUF1 : BUF0) + y * 3840 + x * 4) >> 4] =
                        {fbpix32(id, x + 3, y), fbpix32(id, x + 2, y), fbpix32(id, x + 1, y), fbpix32(id, x, y)};
    end else begin
        for (id = 0; id < 2; id = id + 1)
            for (y = 0; y < 540; y = y + 1)
                for (x = 0; x < 960; x = x + 8)
                    mig.mem[((id ? BUF1 : BUF0) + y * 1920 + x * 2) >> 4] =
                        {fbpix(id, x + 7, y), fbpix(id, x + 6, y), fbpix(id, x + 5, y), fbpix(id, x + 4, y),
                         fbpix(id, x + 3, y), fbpix(id, x + 2, y), fbpix(id, x + 1, y), fbpix(id, x, y)};
    end

    function [23:0] blend(input [31:0] p, input [23:0] b, input [7:0] ga, input ken);
        integer a, c, d;
        reg [23:0] fg;
        begin
            fg = FMT2 ? p[23:0] : {p[15:11], p[15:13], p[10:5], p[10:9], p[4:0], p[4:2]};
            a = ga + ga[7];
            if (ken && !FMT2 && p[15:0] == KEY)
                blend = b;
            else
                for (c = 0; c < 3; c = c + 1) begin
                    d = fg[8*c +: 8] - b[8*c +: 8];
                    blend[8*c +: 8] = b[8*c +: 8] + ((d * a) >>> 8);
                end
        end
    endfunction

    // ---- expected settings: nx_ as written, ex_ from the next frame start --------------
    reg        nx_en = 0, ex_en = 0, nx_ken = 0, ex_ken = 0;
    integer    nx_s = 2, nx_w = 960, nx_h = 540, nx_x0 = 0, nx_y0 = 0, nx_ga = 255, nx_id = 0;
    integer    ex_s, ex_w, ex_h, ex_x0, ex_y0, ex_ga, ex_id;
    reg        strict = 1;
    integer    frame = 0, errors = 0, bad = 0, pass_lines = 0, frame_pass = 0;
    integer    n_match, n_pass, n_bad, n_lay;
    reg [23:0] e, b;
    integer    lx, ly;
    always @(negedge pix_clk) begin
        if (o_cx == 0 && o_cy == 0) begin
            frame = frame + 1;
            ex_en = nx_en; ex_s = nx_s; ex_w = nx_w; ex_h = nx_h; ex_x0 = nx_x0; ex_y0 = nx_y0;
            ex_ga = nx_ga; ex_id = nx_id; ex_ken = nx_ken; frame_pass = 0;
        end
        if (o_cx == 768) begin n_match = 0; n_pass = 0; n_bad = 0; n_lay = 0; end
        if (o_de) begin
            lx = o_cx - 768; ly = o_cy - 45;
            b = bg(o_cx, o_cy);
            if (ex_en && lx >= ex_x0 && lx < ex_x0 + ex_w * ex_s && ly >= ex_y0 && ly < ex_y0 + ex_h * ex_s) begin
                e = blend(FMT2 ? fbpix32(ex_id, (lx - ex_x0) / ex_s, (ly - ex_y0) / ex_s) : {16'd0, fbpix(ex_id, (lx - ex_x0) / ex_s, (ly - ex_y0) / ex_s)}, b, ex_ga, ex_ken);
                if (e != b) begin
                    n_lay = n_lay + 1;
                    if (o_rgb == e) n_match = n_match + 1;
                    else if (o_rgb == b) n_pass = n_pass + 1;
                    else n_bad = n_bad + 1;
                end else if (o_rgb != b) n_bad = n_bad + 1;
            end else if (o_rgb != b) n_bad = n_bad + 1;
            if (n_bad == 1 && bad < 10 && o_rgb != e && o_rgb != b) begin
                bad = bad + 1;
                $display("FAIL frame %0d pixel (%0d, %0d): %06x, want %06x (below %06x)", frame, lx, ly, o_rgb, e, b);
            end
        end
        if (o_cx == 2687 && o_cy >= 45) begin
            if (n_bad) errors = errors + 1;
            if (n_pass && n_match) begin
                errors = errors + 1;
                $display("FAIL frame %0d line %0d shown in part: %0d of %0d pixels", frame, o_cy - 45, n_match, n_lay);
            end
            if (n_pass && !n_match) begin
                pass_lines = pass_lines + 1; frame_pass = frame_pass + 1;
                if (strict) begin
                    errors = errors + 1;
                    if (frame_pass < 4) $display("FAIL frame %0d line %0d missing (underrun)", frame, o_cy - 45);
                end
            end
        end
    end

    // ---- exact-output hashes (the old module and a new one must print the same) ----
    // scanout: every output word of a frame; still: every DDR2 write the video client makes, per VIC frame;
    // latency: ui_clk from the still write offered to it accepted (the 2-flop crossing comes on top)
    reg [63:0] h_scan = 64'hCBF29CE484222325, h_wr = 64'hCBF29CE484222325;
    integer    n_scan_frames = 0, n_wr = 0, n_wr_tot = 0, wl_cnt = 0, wl_max = 0, wl_max_hog = 0;
    reg        wl_hog = 0;
    always @(negedge pix_clk) begin
        if (o_cx == 0 && o_cy == 0) begin
            n_scan_frames = n_scan_frames + 1;
            $display("HASH scan %0d %016x", n_scan_frames, h_scan);
            h_scan = 64'hCBF29CE484222325;
        end
        h_scan = (h_scan ^ {14'd0, o_rgb, o_de, o_hs, o_vs, o_cx, o_cy}) * 64'h100000001B3;
    end
    always @(posedge ui_clk) begin
        if (v_en && v_cmd == 3'b000 && v_rdy) begin
            h_wr = (h_wr ^ {37'd0, v_addr}) * 64'h100000001B3;
            h_wr = (h_wr ^ v_wdf_data[63:0]) * 64'h100000001B3;
            h_wr = (h_wr ^ v_wdf_data[127:64]) * 64'h100000001B3;
            n_wr = n_wr + 1; n_wr_tot = n_wr_tot + 1;
            if (wl_hog) begin if (wl_cnt > wl_max_hog) wl_max_hog = wl_cnt; end
            else if (wl_cnt > wl_max) wl_max = wl_cnt;
            wl_cnt = 0; wl_hog = 0;
        end else if (v_wdf_wren && !v_rdy) begin wl_cnt = wl_cnt + 1; if (hog) wl_hog = 1; end
    end
    always @(posedge vclk)
        if (dut.emit && dut.in_x && dut.start) begin
            $display("HASH still-writes %0d words %016x", n_wr, h_wr);
            n_wr = 0; h_wr = 64'hCBF29CE484222325;
        end

    // ---- register access (the app's IO bus) ---------------------------------------------
    task wr8(input [16:0] a, input [7:0] d);
        begin @(negedge cpu_clk); s_addr = a; s_wdata = d; s_we = 1; @(negedge cpu_clk); s_we = 0; end
    endtask
    task rd8(input [16:0] a, output [7:0] d);
        begin @(negedge cpu_clk); s_addr = a; s_re = 1; @(negedge cpu_clk); s_re = 0; d = s_rdata; end
    endtask
    task wr32(input [16:0] a, input [31:0] d);
        begin wr8(a, d[7:0]); wr8(a + 1, d[15:8]); wr8(a + 2, d[23:16]); wr8(a + 3, d[31:24]); end
    endtask
    task rd32(input [16:0] a, output [31:0] d);
        begin rd8(a, d[7:0]); rd8(a + 1, d[15:8]); rd8(a + 2, d[23:16]); rd8(a + 3, d[31:24]); end
    endtask
    task check(input ok, input [8*48-1:0] what);
        begin
            if (!ok) begin errors = errors + 1; $display("FAIL %0s", what); end
            else $display("ok   %0s", what);
        end
    endtask
    task at_line(input integer f, input integer line);
        begin while (!(frame == f && o_cy == line)) @(negedge pix_clk); end
    endtask
    localparam [16:0] P0 = 17'h00000, P6 = 17'h06000;

    reg [31:0] r, st0, st1;
    reg [7:0]  c8;
    integer    i, j, k, under0, pass0, frame0;
    reg [3:0]  want;
    time       l0, l1, l2, lk;
    initial begin
        while (!calib) @(posedge ui_clk);
        load = 1;
        rd32(P0, r); check(r == "2XRT", "ID");
        rd8(P0 + 4, c8); check(c8 == 8'hFF, "CAPS pages 0-6, RGB565 layer");
        rd32(P6 + 12'h080, r); check(r == 32'h0, "PX_CTRL reads 0 (write-only)");
        rd32(P6 + 12'h090, r); check(r == 32'h0, "PX_SIZE reads 0 (write-only)");
        rd32(P6 + 12'h094, r); check(r == 32'h0, "PX_STRIDE reads 0 (write-only)");
        rd32(P6 + 12'h0A0, r); check(r == 32'h0, "PX_KEY reads 0 (write-only)");
        rd32(P6 + 12'h060, r); check(r == 32'h0, "STILL_CTRL reads 0 (write-only)");
        // settings for frame 2 (A); frame 1 runs with the layer off
        at_line(1, 600);
        wr32(P6 + 12'h064, STILL);
        wr32(P6 + 12'h084, BUF0);
        if (FMT2) wr32(P6 + 12'h094, 3840);
        wr32(P6 + 12'h080, 32'h01 | F);
        wr8(P0 + 12'h010, 8'hC0);
        nx_en = 1; nx_id = 0;
        rd32(P6 + 12'h084, r); check(r == 32'h0, "PX_FRONT reads 0 (write-only)");
        // A: flip to buffer 1 and global alpha 128 for B
        at_line(2, 600);
        $display("frame A at scale 2: done to line 555");
        wr32(P6 + 12'h09C, 128); nx_ga = 128;
        wr32(P6 + 12'h0A0, {15'd1, KEY}); nx_ken = 1;
        rd32(P6 + 12'h0A0, r); check(r == 32'h0, "PX_KEY reads 0 after a write");
        rd32(P6 + 12'h0A4, st0); check(st0[0] == 0, "no flip pending");
        wr8(P0 + 12'h014, 8'hF0);           // clear the causes so far
        rd8(P0 + 12'h014, c8); check(c8 == 8'h00 && !irq, "causes cleared");
        wr32(P6 + 12'h088, BUF1); nx_id = 1;
        rd32(P6 + 12'h0A4, st0); check(st0[0] == 1, "flip pending after PX_BACK");
        rd32(P6 + 12'h088, r); check(r == 32'h0, "PX_BACK reads 0 (write-only)");
        at_line(3, 100);
        rd8(P0 + 12'h014, c8); check(c8 == 8'hC0 && irq, "vsync and flip-done causes, IRQ");
        rd32(P6 + 12'h084, r); check(r == 32'h0, "PX_FRONT reads 0 after the flip");
        rd32(P6 + 12'h0A4, st1); check(st1[0] == 0, "flip done");
        check(st1[31:16] - st0[31:16] == 1, "frame count +1");
        wr8(P0 + 12'h014, 8'hC0);
        rd8(P0 + 12'h014, c8); check(c8 == 8'h00 && !irq, "W1C");
        // B -> C: scale 1 at (100, 50)
        at_line(3, 600);
        wr32(P6 + 12'h09C, 255); nx_ga = 255;
        wr32(P6 + 12'h098, {16'd50, 16'd100}); nx_x0 = 100; nx_y0 = 50;
        wr32(P6 + 12'h0A8, {16'd540, 16'd960}); wr32(P6 + 12'h080, 32'h01 | F); nx_s = 1;
        // C -> D: 320 x 180 at scale 3 at (640, 300)
        at_line(4, 600);
        wr32(P6 + 12'h090, {16'd180, 16'd320}); nx_w = 320; nx_h = 180;
        wr32(P6 + 12'h098, {16'd300, 16'd640}); nx_x0 = 640; nx_y0 = 300;
        wr32(P6 + 12'h0A8, {16'd540, 16'd960}); wr32(P6 + 12'h080, 32'h31 | F); nx_s = 3;   // b5:4 (the old scale) are not read
        wr32(P6 + 12'h0A0, KEY); nx_ken = 0;
        // D -> E: back to A's layout, cart_fill hogs MIG from E's start
        at_line(5, 600);
        wr32(P6 + 12'h090, {16'd540, 16'd960}); nx_w = 960; nx_h = 540;
        wr32(P6 + 12'h098, 0); nx_x0 = 0; nx_y0 = 0;
        wr32(P6 + 12'h0A8, {16'd1080, 16'd1920}); wr32(P6 + 12'h080, 32'h01 | F); nx_s = 2;
        while (!(frame == 6 && o_cy == 0)) @(negedge pix_clk);
        rd32(P6 + 12'h0A4, st0); pass0 = pass_lines;
        l0 = c0.lat_max; l1 = c1.lat_max; l2 = c2.lat_max; lk = k_max;
        strict = 0; hog = 1;
        at_line(6, 1124);
        hog = 0;
        while (!(frame == 7 && o_cy == 2)) @(negedge pix_clk);
        strict = 1;
        rd32(P6 + 12'h0A4, st1);
        $display("frame E (hog): %0d lines not shown, PX_STATUS underruns +%0d", pass_lines - pass0,
                 (st1[15:8] - st0[15:8]) & 255);
        check(pass_lines - pass0 > 0, "hog makes lines late");
        check(((pass_lines - pass0) / 2) % 256 == ((st1[15:8] - st0[15:8]) & 255) && (pass_lines - pass0) % 2 == 0,
              "underrun count = late lines / 2");
        check(pass_lines - pass0 < 1080, "some lines shown under the hog");
        at_line(8, 2);
        // still: the slot STILL_STATUS names holds the VIC window
        rd32(P6 + 12'h068, r);
        $display("STILL_STATUS %08x: %0d frames, slot %0d", r, r[31:8], r[1]);
        check(r[0] && r[31:8] >= 3, "still frames captured");
        k = 0;
        for (i = 0; i < 3375; i = i + 1)
            for (j = 0; j < 32; j = j + 1) begin
                x = (32 * i + j) % 400; y = (32 * i + j) / 400;
                want = (x + 96) ^ (y + 16);
                if (mig.mem[(STILL + r[1] * 32'h10000 >> 4) + i][4*j +: 4] !== want) k = k + 1;
            end
        check(k == 0, "still slot = VIC window, 400 x 270");
        $display("frames A-D (no hog): max latency drive %0d, RISC-V %0d, port %0d ns, cart line %0d ns",
                 l2 / 1000, l0 / 1000, l1 / 1000, lk / 1000);
        $display("still write latency max %0d ui_clk without the hog (+3 crossing), %0d under it, %0d words; 4.06 us = 304 ui_clk",
                 wl_max, wl_max_hog, n_wr_tot);
        $display("frames %0d, late lines %0d, %0d line errors", frame, pass_lines, errors);
        if (errors) $display("FAILED"); else $display("PASS");
        $finish;
    end
endmodule
