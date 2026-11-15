// SPDX-License-Identifier: GPL-3.0-or-later
// U16 D6 bench: c64_text.v's geometry on hdmi_timing.v's counters, one row of U16 D2 and one scale per run (+mode=M +sc=S;
// sim/text/run.sh runs all 21). The C64 side is a model of the RAM / colour RAM ports (granted at once, data a clock later): screen
// byte = the cell's row, glyph byte 0x55 on even and 0xAA on odd character lines, colour 1; the machine is paused, the view is
// TX_VIEW's reset value, TX_CTRL.show is set through the register port. For one whole frame after the first, every output pixel must
// be exactly what the geometry of hm_geom(mode, scale) says: inside the text area (tw x th from (xt0, yt0) of the active area,
// tw = ceil(320 hd / hn), th = ceil(200 tvd / tvn)) C64 pixel j = floor((x - xt0) hn / hd) of C64 line l = floor((y - yt0) tvn / tvd)
// is the glyph bit 7 - (j mod 8) of line l: white where set, the picture below (a constant) where not; outside it the picture below.
// No line may be late (TX_STATUS lines not drawn = 0). +mut=1 (control, must FAIL): hd one larger.
`timescale 1ns/1ps
module tb;
`include "hdmi_modes.vh"
    integer m, sc, mut, fails = 0;
    reg clk = 0, cclk = 0, sclk = 0;
    always #3.35 clk = ~clk;           // the pixel clock (its exact rate does not matter here)
    always #15.86 cclk = ~cclk;        // the C64 clock (dot4x)
    always #5.0 sclk = ~sclk;          // the app's bus clock
    initial begin
        if (!$value$plusargs("mode=%d", m)) m = 2;
        if (!$value$plusargs("sc=%d", sc)) sc = 1;
        if (!$value$plusargs("mut=%d", mut)) mut = 0;
    end

    reg rst = 1;
    wire ce, de_t, hs_t, vs_t, running;
    wire [11:0] cx_t, hact, xact0, htot_m1; wire [10:0] cy_t, vact, yact0, vtot_m1; wire [3:0] mode_q;
    hdmi_timing tim (.clk(clk), .rst(rst), .mode(m[3:0]), .align(1'b0), .valid(), .aligned(), .ce(ce), .rgb(),
        .de(de_t), .hs(hs_t), .vs(vs_t), .cx(cx_t), .cy(cy_t), .running(running), .hact(hact), .vact(vact), .xact0(xact0),
        .yact0(yact0), .gear(), .htot_m1(htot_m1), .vtot_m1(vtot_m1), .vic(), .rate(), .sx_e(), .fw_m1(), .sy_e(), .ey_e(),
        .ph_e(), .mode_q(mode_q));
    reg [81:0] geom;
    initial begin
        #1 geom = hm_geom(m, sc);
        if (mut == 1) geom[75:70] = geom[75:70] + 6'd1;
    end

    // the register port (TX_CTRL at 0x0C0: byte 0 staged, byte 3 commits; TX_STATUS at 0x0C4)
    reg we = 0, re = 0; reg [11:0] addr = 0; reg [7:0] wdata = 0; wire [7:0] rdata;
    // the C64 side
    wire port_req, port_rom; wire [15:0] port_addr; wire [9:0] cram_addr;
    reg [7:0] port_rdata = 0; reg [3:0] cram_rdata = 0;
    always @(posedge cclk) begin
        port_rdata <= (port_addr >= 16'h0400 && port_addr < 16'h07E8) ? (port_addr - 16'h0400) / 40   // screen: the row
                    : port_addr[0] ? 8'hAA : 8'h55;                                                   // glyph: by character line
        cram_rdata <= 4'd1;
    end
    wire [23:0] rgb; wire de, hs, vs; wire [11:0] cx; wire [10:0] cy;
    localparam [23:0] BG = 24'h123456;
    c64_text dut (
        .clk_sys(sclk), .we(we), .re(re), .addr(addr), .wdata(wdata), .rdata(rdata),
        .pal_clk(sclk), .pal_we(1'b0), .pal_addr(6'd0), .pal_wdata(8'd0),
        .cclk(cclk), .halted(1'b1), .vic_view(16'h0243),       // TX_VIEW's reset: text, $D018 = $12 (screen $0400, charset $0800), bank 0
        .port_req(port_req), .port_addr(port_addr), .port_rom(port_rom), .port_grant(port_req),
        .port_rdata(port_rdata), .cram_addr(cram_addr), .cram_grant(1'b1), .cram_rdata(cram_rdata),
        .clk(clk), .ce(ce), .xact0(xact0), .yact0(yact0), .xt0(geom[22:11]), .yt0(geom[10:0]),
        .hn(geom[81:76]), .hd(geom[75:70]), .tvn(geom[57:52]), .tvd(geom[51:46]),
        .rgb_in(BG), .de_in(de_t), .hs_in(hs_t), .vs_in(vs_t), .cx_in(cx_t), .cy_in(cy_t),
        .rgb(rgb), .de(de), .hs(hs), .vs(vs), .cx(cx), .cy(cy));

    // expected
    integer hn, hd, tvn, tvd, xt0, yt0, tw, th, ox, oy, j, l, gb, fg, n_on, n_px, n_samp = 0, chk = 0, bad = 0;
    reg [23:0] want;
    always @(posedge clk) if (chk && ce) begin
        ox = cx - xact0; oy = cy - yact0;
        want = BG;
        if (de && ox >= xt0 && ox < xt0 + tw && oy >= yt0 && oy < yt0 + th) begin
            j = ((ox - xt0) * hn) / hd; l = ((oy - yt0) * tvn) / tvd;
            gb = l[0] ? 8'hAA : 8'h55;
            fg = (gb >> (7 - j % 8)) & 1;
            if (fg) begin want = 24'hFFFFFF; n_on = n_on + 1; end
        end
        if (de) n_px = n_px + 1;
        if (de && rgb !== want) begin
            bad = bad + 1;
            if (bad <= 5) $display("FAIL mode %0d scale %0d: pixel (%0d, %0d) rgb %h want %h", m, sc, ox, oy, rgb, want);
        end
        n_samp = n_samp + 1;
    end

    reg [7:0] st0;
    initial begin
        #2;
        hn = geom[81:76]; hd = geom[75:70]; tvn = geom[57:52]; tvd = geom[51:46]; xt0 = geom[22:11]; yt0 = geom[10:0];
        if (mut == 1) hd = hd - 1;                    // the expectation stays the table's
        tw = (320 * hd + hn - 1) / hn; th = (200 * tvd + tvn - 1) / tvn;
        repeat (10) @(posedge clk); rst = 0;
        wait (running);
        // TX_CTRL.show = 1
        @(posedge sclk); we <= 1; addr <= 12'h0C0; wdata <= 8'h01; @(posedge sclk); addr <= 12'h0C3; wdata <= 8'h00;
        @(posedge sclk); we <= 0;
        // one frame to start, then check one whole frame: htot x vtot pixels from an output (0, 0) (sampled between the edges)
        repeat (2) begin @(negedge clk); while (cx !== 0 || cy !== 0) @(negedge clk); @(negedge clk); while (cx === 0 && cy === 0) @(negedge clk); end
        @(negedge clk); while (cx !== 0 || cy !== 0) @(negedge clk);
        n_on = 0; n_px = 0; n_samp = 0; chk = 1;
        while (n_samp < (htot_m1 + 1) * (vtot_m1 + 1)) @(negedge clk);
        chk = 0;
        if (n_px != hact * vact) begin bad = bad + 1; $display("FAIL mode %0d scale %0d: %0d active pixels", m, sc, n_px); end
        if (n_on == 0) begin bad = bad + 1; $display("FAIL mode %0d scale %0d: no text pixel", m, sc); end
        if (xt0 + tw > hact || yt0 + th > vact) begin bad = bad + 1; $display("FAIL mode %0d scale %0d: text area outside the active area", m, sc); end
        // TX_STATUS: lines not drawn for a late fetch
        @(posedge sclk); re <= 1; addr <= 12'h0C4; @(posedge sclk); re <= 0; addr <= 12'h0C5; @(posedge sclk); @(posedge sclk); st0 = rdata;
        if (st0 != 0) begin bad = bad + 1; $display("FAIL mode %0d scale %0d: %0d lines late", m, sc, st0); end
        if (bad == 0) $display("PASS text mode %0d scale %0d: %0dx%0d from (%0d, %0d) of %0dx%0d, steps %0d/%0d %0d/%0d, %0d text pixels on",
                               m, sc, tw, th, xt0, yt0, hact, vact, hn, hd, tvn, tvd, n_on);
        else $display("FAIL text mode %0d scale %0d: %0d", m, sc, bad);
        $finish;
    end
endmodule
