// T164 WP6 / S9: overlay.v and c64_text.v follow the mode's frame and the pixel repetition clock enable.
// One mode row of hdmi_modes.vh per run (MODE = family * 6 + hdmi). A frame generator with the counter convention of hdmi_timing.v
// (cx = 0 at the first clock after the active area, then front porch, sync, back porch, then the active area; ce high in the
// last of the `gear` clocks of a pixel) feeds a flat grey picture to both modules. Measured in the third frame:
//   overlay  : a 960 x 540 canvas (120 x 30 cells of 8 x 18, black opaque) -> factor max(1, floor(vact / 540)), centred, cut at the
//              active area (never shifted), nothing outside the active area
//   c64_text : the screen byte and glyph byte 0xAA everywhere, colour 1 -> 40 x 25 cells, every even C64 pixel column lit:
//              scale min(4, hact / 320, vact / 200), centred, the lit pixels are exactly ST wide on every even column
// and for both: the outputs change only after a clock in which ce was high, and de follows the counters.
// NEG = 1: the modules are told the 1080p frame while the generator runs the mode (control).
`timescale 1ns/1ps
module tb;
    parameter MODE = 2;
    parameter NEG  = 0;

`include "hdmi_modes.vh"

    wire [104:0] row   = hm_row(MODE);
    wire [3:0]  gear   = row[97:94];
    wire [11:0] hact   = row[93:82];
    wire [11:0] hfp    = row[81:70];
    wire [11:0] hsync  = row[69:58];
    wire [11:0] hbp    = row[57:46];
    wire [10:0] vact   = row[44:34];
    wire [10:0] vfp    = row[33:23];
    wire [10:0] vsync  = row[22:12];
    wire [10:0] vbp    = row[11:1];
    wire [11:0] xact0  = hfp + hsync + hbp;
    wire [10:0] yact0  = vfp + vsync + vbp;
    wire [11:0] htot_m1 = xact0 + hact - 12'd1;
    wire [10:0] vtot_m1 = yact0 + vact - 11'd1;

    reg clk = 0, sclk = 0, cclk = 0;
    always #5.0   clk  = ~clk;
    always #3.5   sclk = ~sclk;
    always #15.85 cclk = ~cclk;

    // ---- the frame generator
    reg         ce = 1'b0;
    reg  [3:0]  ph = 4'd0;
    reg  [11:0] hc = 12'd0, cx = 12'd0;
    reg  [10:0] vc = 11'd0, cy = 11'd0;
    reg         de = 1'b0, hs = 1'b0, vs = 1'b0;
    wire [3:0]  ph_n = (ph == gear - 4'd1) ? 4'd0 : ph + 4'd1;
    always @(posedge clk) begin
        ph <= ph_n;
        ce <= (ph_n == gear - 4'd1);
        if (ce) begin
            de <= (hc >= xact0) && (vc >= yact0);
            hs <= (hc >= hfp) && (hc < hfp + hsync);
            vs <= (vc >= vfp) && (vc < vfp + vsync);
            cx <= hc;
            cy <= vc;
            if (hc == htot_m1) begin
                hc <= 12'd0;
                vc <= (vc == vtot_m1) ? 11'd0 : vc + 11'd1;
            end else
                hc <= hc + 12'd1;
        end
    end
    initial #1 if (gear == 4'd1) ce = 1'b1;

    localparam [23:0] GREY = 24'h808080;

    // what the modules are told
    wire [11:0] m_hact  = NEG ? 12'd1920 : hact;
    wire [10:0] m_vact  = NEG ? 11'd1080 : vact;
    wire [11:0] m_xact0 = NEG ? 12'd768  : xact0;
    wire [10:0] m_yact0 = NEG ? 11'd45   : yact0;

    // ---- overlay
    reg         o_we = 1'b0;
    reg  [14:0] o_addr = 15'd0;
    reg  [7:0]  o_wdata = 8'd0;
    wire [7:0]  o_rdata;
    wire [23:0] o_rgb; wire o_de, o_hs, o_vs; wire [11:0] o_cx; wire [10:0] o_cy; wire o_own;
    overlay ovl (
        .clk_sys(sclk), .we(o_we), .addr(o_addr), .wdata(o_wdata), .rdata(o_rdata),
        .clk(clk), .ce(ce), .hact(m_hact), .vact(m_vact), .xact0(m_xact0), .yact0(m_yact0),
        .rgb_in(GREY), .de_in(de), .hs_in(hs), .vs_in(vs), .cx_in(cx), .cy_in(cy),
        .rgb(o_rgb), .de(o_de), .hs(o_hs), .vs(o_vs), .cx(o_cx), .cy(o_cy), .own_keyboard(o_own));

    // ---- c64_text, the C64 RAM answers 0xAA to every read, the colour RAM 1
    reg         t_we = 1'b0;
    reg  [11:0] t_addr = 12'd0;
    reg  [7:0]  t_wdata = 8'd0;
    wire [7:0]  t_rdata;
    wire        tp_req; wire [15:0] tp_addr; wire tp_rom; wire [9:0] tp_cram;
    reg  [7:0]  tp_rd = 8'd0;
    always @(posedge cclk) tp_rd <= 8'hAA;
    wire [23:0] t_rgb; wire t_de, t_hs, t_vs; wire [11:0] t_cx; wire [10:0] t_cy;
    c64_text txt (
        .clk_sys(sclk), .we(t_we), .re(1'b0), .addr(t_addr), .wdata(t_wdata), .rdata(t_rdata),
        .pal_clk(sclk), .pal_we(1'b0), .pal_addr(6'd0), .pal_wdata(8'd0),
        .cclk(cclk), .halted(1'b1), .vic_view({3'b000, 7'h0A, 4'd6, 2'd3}),
        .port_req(tp_req), .port_addr(tp_addr), .port_rom(tp_rom), .port_grant(tp_req), .port_rdata(tp_rd),
        .cram_addr(tp_cram), .cram_grant(1'b1), .cram_rdata(4'd1),
        .clk(clk), .ce(ce), .hact(m_hact), .vact(m_vact), .xact0(m_xact0), .yact0(m_yact0),
        .rgb_in(GREY), .de_in(de), .hs_in(hs), .vs_in(vs), .cx_in(cx), .cy_in(cy),
        .rgb(t_rgb), .de(t_de), .hs(t_hs), .vs(t_vs), .cx(t_cx), .cy(t_cy));

    task ow(input [14:0] a, input [7:0] d);
        begin @(posedge sclk); o_we <= 1; o_addr <= a; o_wdata <= d; @(posedge sclk); o_we <= 0; end
    endtask
    task tw(input [11:0] a, input [7:0] d);
        begin @(posedge sclk); t_we <= 1; t_addr <= a; t_wdata <= d; @(posedge sclk); t_we <= 0; end
    endtask
    initial begin
        #100;
        ow(15'h0002, 8'd8);            // 8 pixel wide cells
        ow(15'h0003, 8'd18);           // 18 lines high, the 8-pixel font
        ow(15'h0004, 8'd120);          // 120 cells a row: 960 pixels
        ow(15'h0005, 8'd30);           // 30 rows: 540 lines
        ow(15'h000D, 8'h85);           // on, transparent colour 5; the screen is spaces on colour 0 = opaque black
        tw(12'h0C0, 8'h01);            // TX_CTRL.show
        tw(12'h0C3, 8'h00);
    end

    // ---- monitors, sampled in the middle of the clock
    reg        ce_q = 1'b0;
    reg [23:0] po_rgb, pt_rgb; reg po_de, po_hs, po_vs, pt_de, pt_hs, pt_vs;
    reg [11:0] po_cx, pt_cx; reg [10:0] po_cy, pt_cy;
    integer cad_err = 0;
    integer frames [0:1];
    integer n      [0:1];
    integer minx   [0:1];
    integer maxx   [0:1];
    integer miny   [0:1];
    integer maxy   [0:1];
    integer outside[0:1];
    integer pat_bad[0:1];
    integer de_n   [0:1];
    integer de_err [0:1];
    reg     at0 [0:1];
    integer s_;
    initial for (s_ = 0; s_ < 2; s_ = s_ + 1) begin
        frames[s_] = 0; at0[s_] = 1'b0; n[s_] = 0; minx[s_] = 99999; maxx[s_] = -1; miny[s_] = 99999; maxy[s_] = -1;
        outside[s_] = 0; pat_bad[s_] = 0; de_n[s_] = 0; de_err[s_] = 0;
    end

    // expected values, from the mode row alone
    integer f_, e_w, e_h, e_x0, e_y0, e_vw, e_vh;
    integer st_, tx0, ty0;
    integer ihact, ivact, ixact0, iyact0;
    always @* begin
        ihact = hact; ivact = vact; ixact0 = xact0; iyact0 = yact0;
        f_ = ivact / 540; if (f_ < 1) f_ = 1;
        e_w = 960 * f_; e_h = 540 * f_;
        e_x0 = (e_w < ihact) ? (ihact - e_w) / 2 : 0;
        e_y0 = (e_h < ivact) ? (ivact - e_h) / 2 : 0;
        e_vw = (e_w < ihact - e_x0) ? e_w : ihact - e_x0;
        e_vh = (e_h < ivact - e_y0) ? e_h : ivact - e_y0;
        st_ = 4;
        if (ihact / 320 < st_) st_ = ihact / 320;
        if (ivact / 200 < st_) st_ = ivact / 200;
        if (st_ < 1) st_ = 1;
        tx0 = (ihact - 320 * st_) / 2; ty0 = (ivact - 200 * st_) / 2;
    end

    task obs(input integer s, input [23:0] rgb, input dd, input [11:0] ccx, input [10:0] ccy);
        integer x, y;
        begin
            // a frame starts at the pixel (0, 0); it is one pixel (the generator holds the reset value for two ticks at start)
            if (ccx === 12'd0 && ccy === 11'd0 && !at0[s]) frames[s] = frames[s] + 1;
            at0[s] = (ccx === 12'd0 && ccy === 11'd0);
            if (frames[s] >= 1 && frames[s] <= 3) begin
                if (dd !== ((ccx >= ixact0) && (ccy >= iyact0))) de_err[s] = de_err[s] + 1;
                x = ccx - ixact0; y = ccy - iyact0;
                if (rgb !== GREY && !(dd === 1'b1)) outside[s] = outside[s] + 1;
                if (frames[s] == 2) begin
                    if (dd === 1'b1) de_n[s] = de_n[s] + 1;
                    if (rgb !== GREY && dd === 1'b1) begin
                        n[s] = n[s] + 1;
                        if (x < minx[s]) minx[s] = x;
                        if (x > maxx[s]) maxx[s] = x;
                        if (y < miny[s]) miny[s] = y;
                        if (y > maxy[s]) maxy[s] = y;
                        if (s == 1 && (x < tx0 || ((x - tx0) / st_) % 2 != 0)) pat_bad[s] = pat_bad[s] + 1;
                    end
                end
            end
        end
    endtask

    integer errors;
    task chk(input ok, input [8*40:1] what, input integer got, input integer exp);
        begin
            if (!ok) begin errors = errors + 1; $display("  FAIL %0s: %0d, expected %0d", what, got, exp); end
        end
    endtask

    always @(negedge clk) begin
        if (!ce_q) begin    // no ce at the last clock edge: nothing may have changed
            if (o_rgb !== po_rgb || o_de !== po_de || o_hs !== po_hs || o_vs !== po_vs || o_cx !== po_cx || o_cy !== po_cy ||
                t_rgb !== pt_rgb || t_de !== pt_de || t_hs !== pt_hs || t_vs !== pt_vs || t_cx !== pt_cx || t_cy !== pt_cy)
                cad_err = cad_err + 1;
        end else begin
            obs(0, o_rgb, o_de, o_cx, o_cy);
            obs(1, t_rgb, t_de, t_cx, t_cy);
        end
        po_rgb = o_rgb; po_de = o_de; po_hs = o_hs; po_vs = o_vs; po_cx = o_cx; po_cy = o_cy;
        pt_rgb = t_rgb; pt_de = t_de; pt_hs = t_hs; pt_vs = t_vs; pt_cx = t_cx; pt_cy = t_cy;
        ce_q <= ce;
        if (frames[0] == 3 && frames[1] == 3) begin
            errors = 0;
            $display("mode %0d: %0d x %0d gear %0d, xact0 %0d yact0 %0d%s", MODE, ihact, ivact, gear, ixact0, iyact0, NEG ? " (modules told 1080p)" : "");
            $display("  overlay : %0d pixels, x %0d..%0d, y %0d..%0d", n[0], minx[0], maxx[0], miny[0], maxy[0]);
            $display("  text    : %0d pixels, x %0d..%0d, y %0d..%0d, scale %0d", n[1], minx[1], maxx[1], miny[1], maxy[1], st_);
            chk(cad_err == 0, "outputs changed without ce", cad_err, 0);
            chk(de_err[0] == 0 && de_err[1] == 0, "de differs from the counters", de_err[0] + de_err[1], 0);
            chk(de_n[0] == ihact * ivact && de_n[1] == ihact * ivact, "active pixels of a frame", de_n[0], ihact * ivact);
            chk(outside[0] == 0 && outside[1] == 0, "drawn outside the active area", outside[0] + outside[1], 0);
            chk(minx[0] == e_x0, "overlay left edge", minx[0], e_x0);
            chk(maxx[0] == e_x0 + e_vw - 1, "overlay right edge", maxx[0], e_x0 + e_vw - 1);
            chk(miny[0] == e_y0, "overlay top edge", miny[0], e_y0);
            chk(maxy[0] == e_y0 + e_vh - 1, "overlay bottom edge", maxy[0], e_y0 + e_vh - 1);
            chk(n[0] == e_vw * e_vh, "overlay pixels", n[0], e_vw * e_vh);
            chk(minx[1] == tx0, "text left edge", minx[1], tx0);
            chk(maxx[1] == tx0 + 318 * st_ + st_ - 1, "text right edge", maxx[1], tx0 + 318 * st_ + st_ - 1);
            chk(miny[1] == ty0, "text top edge", miny[1], ty0);
            chk(maxy[1] == ty0 + 200 * st_ - 1, "text bottom edge", maxy[1], ty0 + 200 * st_ - 1);
            chk(n[1] == 32000 * st_ * st_, "text pixels", n[1], 32000 * st_ * st_);
            chk(pat_bad[1] == 0, "text pixels off the lit columns", pat_bad[1], 0);
            $display("OVL_GEOM mode %0d %0s", MODE, errors == 0 ? "PASS" : "FAIL");
            $finish;
        end
    end
    initial begin #400_000_000; $display("OVL_GEOM mode %0d FAIL (timeout)", MODE); $finish; end
endmodule
