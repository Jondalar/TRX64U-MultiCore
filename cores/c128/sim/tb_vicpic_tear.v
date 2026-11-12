// T194 S2 (tear): the C128's pix_layer copy shows the VIC capture (format 3, nearest, 1:1 at (0, 0)) while the VIC's picture changes every frame: pixel (x, y) of VIC frame n
// has index (pv(x, y) + n) mod 16. Every frame the layer puts out must be ONE VIC frame, entire (the offset n read from its first pixel, every other pixel agrees); a frame
// assembled from two captures is a tear. The display clock is slow (a small mode: HTOT x VTOT clocks of PER ps), so that a run is a few seconds: PER sets the display's
// frame time against the VIC frame (equal = the locked rows of the board; +-3 % = a row that is not locked and slides through every phase), H0 / V0 the starting phase.
// The frames shown must also be different frames (the pattern moves) and each must be the newest complete capture or the one before.
`timescale 1ps/1ps
module tb;
    parameter V60 = 0;
    parameter NF = 14;                  // output frames to look at
    parameter H0 = 0, V0 = 0;           // the display's starting phase
    parameter SMOOTHP = 1;              // pix_layer's SMOOTH
    parameter HTOT = 2640, VTOT = 1125, XA = 720, YA = 45, HACT = 1920, VACT = 1080, GEAR = 1, PER = 6734;
    localparam [31:0] F = 32'h06;                     // PX_CTRL b3:1 = 3
    reg cpu_clk = 0, ui_clk = 0, pix_clk = 0, vclk = 0;
    always #5000  cpu_clk = ~cpu_clk;
    always #6667  ui_clk  = ~ui_clk;
    always #(PER/2) pix_clk = ~pix_clk;
    always #15859 vclk    = ~vclk;

    // the default STILL_BASE 0x05050000 (bits 17:0 dropped) wraps to 0x040000-0x06FFFF in the 8 MB model

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
    reg [11:0] h = H0; reg [10:0] vv = V0;
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
    localparam [9:0] PXMAX = V60 ? 10'd519 : 10'd503;
    localparam [8:0] PYMAX = V60 ? 9'd262 : 9'd311;
    always @(posedge vclk) begin
        ph <= ph + 1;
        if (ph == 3) begin
            px <= (px == PXMAX) ? 0 : px + 1;
            if (px == PXMAX) py <= (py == PYMAX) ? 0 : py + 1;
        end
    end
    function [3:0] pv(input integer x, input integer y);
        pv = (x * 3 + y * 5 + (x >> 3) + (y >> 2) * 7) & 15;
    endfunction
    integer vfr = 0;
    always @(posedge vclk) if (ph == 3 && px == PXMAX && py == PYMAX) vfr <= vfr + 1;
    wire [3:0] pix = (pv(px, py) + vfr) & 15;
    // Colodore, as pal_ram.v starts
    function [23:0] colodore(input [3:0] i);
        case (i)
        4'd0: colodore = 24'h000000; 4'd1: colodore = 24'hFFFFFF; 4'd2: colodore = 24'h813338; 4'd3: colodore = 24'h75CEC8;
        4'd4: colodore = 24'h8E3C97; 4'd5: colodore = 24'h56AC4D; 4'd6: colodore = 24'h2E2C9B; 4'd7: colodore = 24'hEDF171;
        4'd8: colodore = 24'h8E5029; 4'd9: colodore = 24'h553800; 4'd10: colodore = 24'hC46C71; 4'd11: colodore = 24'h4A4A4A;
        4'd12: colodore = 24'h7B7B7B; 4'd13: colodore = 24'hA9FF9F; 4'd14: colodore = 24'h706DEB; default: colodore = 24'hB2B2B2;
        endcase
    endfunction
    wire [23:0] o_rgb; wire o_de, o_hs, o_vs; wire [11:0] o_cx; wire [10:0] o_cy;
    pix_layer #(.SMOOTH(SMOOTHP)) dut (.clk_sys(cpu_clk), .we(p6_we), .re(p6_re), .addr(s_addr[11:0]), .wdata(s_wdata),
        .rdata(p6_rdata), .ev_vsync(ev_vsync), .ev_flip(ev_flip),
        .ui_clk(ui_clk), .ui_rst(ui_rst), .v_addr(v_addr), .v_cmd(v_cmd), .v_en(v_en), .v_rdy(v_rdy),
        .v_wdf_data(v_wdf_data), .v_wdf_mask(v_wdf_mask), .v_wdf_wren(v_wdf_wren),
        .v_rd_data(k_data), .v_rd_data_valid(v_valid), .v_urgent(v_urgent),
        .vclk(vclk), .pix(pix), .px(px), .py(py), .v60(V60[0]),
        .pal_clk(cpu_clk), .pal_we(1'b0), .pal_addr(6'd0), .pal_wdata(8'd0),
        .clk(pix_clk), .ce(ce), .hact(HACT[11:0]), .vact(VACT[10:0]), .xact0(XA[11:0]), .yact0(YA[10:0]),
        .rgb_in(bg(h, vv)), .de_in(h >= XA && vv >= YA), .hs_in(1'b0), .vs_in(1'b0),
        .cx_in(h), .cy_in(vv),
        .rgb(o_rgb), .de(o_de), .hs(o_hs), .vs(o_vs), .cx(o_cx), .cy(o_cy));


    function [3:0] rev(input [23:0] c);     // colour -> palette index (all sixteen differ)
        integer i;
        begin rev = 4'hx; for (i = 0; i < 16; i = i + 1) if (colodore(i[3:0]) == c) rev = i[3:0]; end
    endfunction
    localparam SW = 400, SH = V60 ? 240 : 270;
    // ---- the check: per output frame the offset of its first layer pixel, every other pixel must agree
    integer frame = 0, errors = 0, bad = 0, f0 = -1, nlay = 0, nfr = 0, distinct = 0, last_f = -1, lx, ly, sx, sy, ln, d;
    reg [3:0] ix;
    reg nx_on = 0, ex_on = 0;
    always @(negedge pix_clk) if (ce) begin
        if (o_cx == 0 && o_cy == 0) begin
            if (ex_on && nlay > 0) begin
                nfr = nfr + 1;
                if (bad != 0) begin errors = errors + 1; $display("FAIL frame %0d (offset %0d): %0d pixels of another frame (torn)", frame, f0, bad); end
                else $display("ok   frame %0d shows VIC frame offset %0d", frame, f0);
                if (f0 != last_f) distinct = distinct + 1;
                last_f = f0;
            end
            frame = frame + 1; f0 = -1; bad = 0; nlay = 0;
            ex_on = nx_on;
        end
        if (o_de && ex_on) begin
            lx = o_cx - XA; ly = o_cy - YA;
            if (lx < SW && ly < SH) begin
                sx = lx; sy = ly;
                if (V60) ln = (sy >= 232) ? sy - 232 : sy + 31; else ln = sy + 16;
                ix = rev(o_rgb);
                d = (ix - pv(96 + sx, ln)) & 15;
                if (nlay == 0) f0 = d;
                else if (d != f0) bad = bad + 1;
                nlay = nlay + 1;
            end
        end
    end
    task wr8(input [16:0] a, input [7:0] d_);
        begin @(negedge cpu_clk); s_addr = a; s_wdata = d_; s_we = 1; @(negedge cpu_clk); s_we = 0; end
    endtask
    task wr32(input [16:0] a, input [31:0] d_);
        begin wr8(a, d_[7:0]); wr8(a + 1, d_[15:8]); wr8(a + 2, d_[23:16]); wr8(a + 3, d_[31:24]); end
    endtask
    localparam [16:0] P6 = 17'h06000;
    initial begin
        while (!calib) @(posedge ui_clk);
        load = 1;
        // the VIC runs; the layer is set up after the first VIC frame (a capture is complete), 1:1 at (0, 0), nearest
        while (vfr < 1) @(posedge vclk);
        wr32(P6 + 12'h094, 208);
        wr32(P6 + 12'h090, {SH[15:0], SW[15:0]});
        wr32(P6 + 12'h0A8, {SH[15:0], SW[15:0]});
        wr32(P6 + 12'h098, 32'd0);
        wr32(P6 + 12'h09C, 255);
        wr32(P6 + 12'h080, 32'h01 | F);
        nx_on = 1;
        while (frame < NF + 2) @(negedge pix_clk);
        if (nfr < NF - 1) begin errors = errors + 1; $display("FAIL only %0d frames looked at", nfr); end
        if (distinct < NF / 2) begin errors = errors + 1; $display("FAIL the shown frames do not change (%0d different)", distinct); end
        $display("%0d frames, %0d different, %0d torn", nfr, distinct, errors);
        if (errors) $display("FAILED"); else $display("PASS");
        $finish;
    end
endmodule
