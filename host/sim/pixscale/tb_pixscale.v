// T164 WP5, scenario S7: the scaler of host/pix_layer.v (nearest and bilinear) in one HDMI mode (HTOT x VTOT, active HACT x VACT at XA / YA,
// pixel repetition GEAR: pix_clk is the TMDS clock, the input counters and every dut register move only on ce, one clock of GEAR).
// Infrastructure (MIG model, arbiter, background clients, framebuffers) is host/sim/pixlayer/tb_pixlayer.v's, as the A1200's bench was.
// The picture is SW x SH source pixels (format 1 or, FMT2, format 2) in buffer 0, with white (format 1: 0xFFFF) beside and below it.
// The steps come from the plan (+PLAN=file, $readmemh; model.py plan): per step the output size, position, global alpha, smoothing bit
// and colour key, written to the layer in frame i + 1 and shown in frame i + 2. This bench does not know what the picture should look
// like: it writes the source (+OUT/src.hex) and, for each frame, the output pixels of the layer's rectangle (+OUT/f<frame>.hex, 24 bit
// per line, row by row); model.py check works out the expected frame (nearest: exactly; smoothing: within one LSB) and compares. What it
// does itself: every pixel outside the layer's rectangle is the picture below.
`timescale 1ps/1ps
module tb;
    parameter FMT2 = 0;
    parameter SMOOTHP = 1;              // pix_layer's SMOOTH
    parameter HTOT = 2640, VTOT = 1125, XA = 720, YA = 45, HACT = 1920, VACT = 1080, GEAR = 1, PER = 6734, SW = 320, SH = 256;
    localparam [31:0] F = FMT2 ? 32'h04 : 32'h02;     // PX_CTRL b3:1, the format
    reg cpu_clk = 0, ui_clk = 0, pix_clk = 0, vclk = 0;
    always #5000  cpu_clk = ~cpu_clk;
    always #6667  ui_clk  = ~ui_clk;
    always #(PER/2) pix_clk = ~pix_clk;
    always #15859 vclk    = ~vclk;

    // the still's default slots 0x5050000 wrap to 0x050000-0x06FFFF in the 8 MB model
    localparam [31:0] BUF0 = 32'h080000, STILL = 32'h500000;

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

    // cart_fill: a line (four reads) every 1 us
    integer k_timer = 0, k_left = 0, k_got = 0, k_issued = 0;
    always @(posedge ui_clk) begin
        k_timer <= (k_timer == 2666) ? 0 : k_timer + 1;
        if (k_en && k_rdy) begin
            k_addr <= k_addr + 8; k_issued = k_issued + 1; k_left = k_left - 1;
            if (k_left <= 0) k_en <= 0;
        end else if (k_en && k_left <= 0)
            k_en <= 0;
        else if (!k_en && load && calib && k_timer % 75 == 0 && k_got == k_issued) begin
            k_en <= 1; k_left = 4;
        end
        if (k_valid) k_got = k_got + 1;
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

    // ---- the input chain: the mode's counters on ce, GEAR clocks per pixel ------------------------------
    reg [11:0] h = 0; reg [10:0] vv = 0;
    reg [2:0]  gph = 0; reg ce = 0;
    always @(posedge pix_clk) begin
        gph <= (gph == GEAR - 1) ? 0 : gph + 1;
        ce  <= (gph == GEAR - 2) || (GEAR == 1);
        if (ce) begin
            h <= (h == HTOT - 1) ? 0 : h + 1;
            if (h == HTOT - 1) vv <= (vv == VTOT - 1) ? 0 : vv + 1;
        end
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
    pix_layer #(.SMOOTH(SMOOTHP)) dut (.clk_sys(cpu_clk), .we(p6_we), .re(p6_re), .addr(s_addr[11:0]), .wdata(s_wdata),
        .rdata(p6_rdata), .ev_vsync(ev_vsync), .ev_flip(ev_flip),
        .ui_clk(ui_clk), .ui_rst(ui_rst), .v_addr(v_addr), .v_cmd(v_cmd), .v_en(v_en), .v_rdy(v_rdy),
        .v_wdf_data(v_wdf_data), .v_wdf_mask(v_wdf_mask), .v_wdf_wren(v_wdf_wren),
        .v_rd_data(k_data), .v_rd_data_valid(v_valid), .v_urgent(v_urgent),
        .vclk(vclk), .pix(pix), .px(px), .py(py), .v60(1'b0),
        .clk(pix_clk), .ce(ce), .hact(HACT[11:0]), .vact(VACT[10:0]), .xact0(XA[11:0]), .yact0(YA[10:0]),
        .rgb_in(bg(h, vv)), .de_in(h >= XA && vv >= YA), .hs_in(1'b0), .vs_in(1'b0),
        .cx_in(h), .cy_in(vv),
        .rgb(o_rgb), .de(o_de), .hs(o_hs), .vs(o_vs), .cx(o_cx), .cy(o_cy));

    // ---- the framebuffer: a pattern with the colour key's value in every seventh pixel, white beyond the picture -----------
    localparam [15:0] KEY = 16'hF81F;
    function [15:0] fbpix(input integer sx, input integer sy);
        reg [15:0] v;
        begin
            if (sx >= SW || sy >= SH)
                fbpix = 16'hFFFF;
            else if ((sx + 3 * sy) % 7 == 0)
                fbpix = KEY;
            else begin
                v = (sx == 0) ? 16'h0000 : (sx == 1) ? 16'hFFFF :
                    {sx[4:0], sy[5:0], sx[9:5]} ^ {sy[9:6], 12'd0} ^ (sx * 37 + sy * 11);
                fbpix = (v == KEY) ? v ^ 16'h0020 : v;
            end
        end
    endfunction
    function [31:0] fbpix32(input integer sx, input integer sy);
        reg [31:0] t;
        begin
            t = sx * 37 + sy * 11;
            fbpix32 = (sx >= SW || sy >= SH) ? 32'h5AFFFFFF : {8'h5A, sx[7:0], sy[7:0] ^ sx[9:8], t[7:0]};
        end
    endfunction
    integer x, y;
    initial if (FMT2) begin
        for (y = 0; y < SH + 1; y = y + 1)
            for (x = 0; x < ((SW + 3) / 4) * 4 + 4; x = x + 4)
                mig.mem[(BUF0 + y * 3840 + x * 4) >> 4] =
                    {fbpix32(x + 3, y), fbpix32(x + 2, y), fbpix32(x + 1, y), fbpix32(x, y)};
    end else begin
        for (y = 0; y < SH + 1; y = y + 1)
            for (x = 0; x < ((SW + 7) / 8) * 8 + 8; x = x + 8)
                mig.mem[(BUF0 + y * 1920 + x * 2) >> 4] =
                    {fbpix(x + 7, y), fbpix(x + 6, y), fbpix(x + 5, y), fbpix(x + 4, y),
                     fbpix(x + 3, y), fbpix(x + 2, y), fbpix(x + 1, y), fbpix(x, y)};
    end

    // ---- the plan, the dump -----------------------------------------------------------------
    reg [31:0] plan [0:1023];
    reg [8*256-1:0] outdir, planfile;
    integer ns, fd, fdd;
    reg        nx_en = 0, ex_en = 0;
    integer    nx_ow = 0, nx_oh = 0, nx_x0 = 0, nx_y0 = 0;
    integer    ex_ow = 0, ex_oh = 0, ex_x0 = 0, ex_y0 = 0;
    integer    frame = 0, errors = 0, outside = 0, dumped = 0;
    reg [23:0] b;
    integer    lx, ly;
    always @(negedge pix_clk) if (ce) begin
        if (o_cx == 0 && o_cy == 0) begin
            frame = frame + 1;
            if (fd != 0) begin $fclose(fd); fd = 0; end
            ex_en = nx_en; ex_ow = nx_ow; ex_oh = nx_oh; ex_x0 = nx_x0; ex_y0 = nx_y0;
            if (ex_en) begin
                $swrite(planfile, "%0s/f%0d.hex", outdir, frame);
                fd = $fopen(planfile, "w");
            end
        end
        if (o_de) begin
            lx = o_cx - XA; ly = o_cy - YA;
            b = bg(o_cx, o_cy);
            if (ex_en && lx >= ex_x0 && lx < ex_x0 + ex_ow && ly >= ex_y0 && ly < ex_y0 + ex_oh) begin
                $fwrite(fd, "%06x\n", o_rgb);
                dumped = dumped + 1;
            end else if (o_rgb != b) begin
                outside = outside + 1;
                if (outside < 6) $display("FAIL frame %0d pixel (%0d, %0d) outside the layer: %06x, want %06x", frame, lx, ly, o_rgb, b);
            end
        end
    end

    // ---- register access (the app's IO bus) ---------------------------------------------
    task wr8(input [16:0] a, input [7:0] d);
        begin @(negedge cpu_clk); s_addr = a; s_wdata = d; s_we = 1; @(negedge cpu_clk); s_we = 0; end
    endtask
    task wr32(input [16:0] a, input [31:0] d);
        begin wr8(a, d[7:0]); wr8(a + 1, d[15:8]); wr8(a + 2, d[23:16]); wr8(a + 3, d[31:24]); end
    endtask
    task at_line(input integer f, input integer line);
        begin while (!(frame == f && o_cy == line)) @(negedge pix_clk); end
    endtask
    localparam [16:0] P6 = 17'h06000;
    integer i, j;
    reg [31:0] pw;
    initial begin
        if (!$value$plusargs("OUT=%s", outdir)) outdir = ".";
        if (!$value$plusargs("PLAN=%s", planfile)) begin $display("FAIL no +PLAN"); $finish; end
        $readmemh(planfile, plan);
        ns = plan[0];
        // the source, for the model
        $swrite(planfile, "%0s/src.hex", outdir);
        fdd = $fopen(planfile, "w");
        for (y = 0; y < SH; y = y + 1)
            for (x = 0; x < SW; x = x + 1) begin
                pw = FMT2 ? fbpix32(x, y) : {16'd0, fbpix(x, y)};
                $fwrite(fdd, "%08x\n", pw);
            end
        $fclose(fdd);
        while (!calib) @(posedge ui_clk);
        load = 1;
        for (i = 0; i < ns; i = i + 1) begin
            // step i is written in frame i + 1 and shown in frame i + 2 (frame 1 runs with the layer off)
            at_line(i + 1, VACT/2 + YA);
            if (i == 0) begin
                wr32(P6 + 12'h084, BUF0);
                wr32(P6 + 12'h094, FMT2 ? 3840 : 1920);
                wr32(P6 + 12'h090, {SH[15:0], SW[15:0]});
            end
            j = 1 + 8 * i;
            wr32(P6 + 12'h0A8, {plan[j + 1][15:0], plan[j][15:0]});
            wr32(P6 + 12'h098, {plan[j + 3][15:0], plan[j + 2][15:0]});
            wr32(P6 + 12'h09C, plan[j + 4]);
            wr32(P6 + 12'h0A0, plan[j + 6] ? {15'd1, KEY} : 32'd0);
            wr32(P6 + 12'h080, 32'h01 | F | (plan[j + 5] << 6));
            nx_ow = plan[j]; nx_oh = plan[j + 1]; nx_x0 = plan[j + 2]; nx_y0 = plan[j + 3]; nx_en = 1;
        end
        at_line(ns + 2, 3);
        $display("frames %0d, %0d layer pixels, %0d pixels outside the layer wrong", frame, dumped, outside);
        $display("DONE tb");
        $finish;
    end
endmodule
