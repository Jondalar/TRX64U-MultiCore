// T135 D13: the box of the direct-boot line ("hold C=", ovtext.cc) in each mode row of hdmi_modes.vh. The app writes what ovtext.cc
// writes: 8 x 9 cells, CPL columns, NL rows, every cell a blank on colour 0x01 (white on black = opaque), then on. A frame generator with
// the counter convention of hdmi_timing.v feeds a flat grey picture to host/overlay.v; in the second frame the box (every pixel that is
// not grey) is measured. The scale is box width / (CPL x 8) and box height / (NL x 9); the test wants EXP_SX x EXP_SY (given by run.sh
// from the table of the spec, not computed here), the box centred, and no pixel cut at an edge of the active area.
// The layouts: CPL 41 x NL 21 (D13) and CPL 40 x NL 2 (before D13: control, it has to fail the 720p row).
`timescale 1ns/1ps
module tb;
    parameter MODE = 7;
    parameter CPL = 41;
    parameter NL = 21;
    parameter EXP_SX = 3;
    parameter EXP_SY = 3;

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

    reg clk = 0, sclk = 0;
    always #5.0 clk  = ~clk;
    always #3.5 sclk = ~sclk;

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

    reg         o_we = 1'b0;
    reg  [14:0] o_addr = 15'd0;
    reg  [7:0]  o_wdata = 8'd0;
    wire [7:0]  o_rdata;
    wire [23:0] o_rgb; wire o_de, o_hs, o_vs; wire [11:0] o_cx; wire [10:0] o_cy; wire o_own;
    overlay ovl (
        .clk_sys(sclk), .we(o_we), .addr(o_addr), .wdata(o_wdata), .rdata(o_rdata),
        .clk(clk), .ce(ce), .hact(hact), .vact(vact), .xact0(xact0), .yact0(yact0),
        .rgb_in(GREY), .de_in(de), .hs_in(hs), .vs_in(vs), .cx_in(cx), .cy_in(cy),
        .rgb(o_rgb), .de(o_de), .hs(o_hs), .vs(o_vs), .cx(o_cx), .cy(o_cy), .own_keyboard(o_own));

    task ow(input [14:0] a, input [7:0] d);
        begin @(posedge sclk); o_we <= 1; o_addr <= a; o_wdata <= d; @(posedge sclk); o_we <= 0; end
    endtask
    integer i;
    initial begin
        #100;
        ow(15'h000D, 8'h00);
        ow(15'h0002, 8'd8);
        ow(15'h0003, 8'd9);
        ow(15'h0004, CPL);
        ow(15'h0005, NL);
        ow(15'h000A, 8'd0);
        ow(15'h000B, 8'd0);
        for (i = 0; i < CPL * NL; i = i + 1) begin
            ow(15'h1000 + i, 8'h20);
            ow(15'h2000 + i, 8'h01);
        end
        ow(15'h000D, 8'h8F);
    end

    integer frames = 0;
    reg at0 = 1'b0;
    integer px, py;
    integer n = 0, minx = 99999, maxx = -1, miny = 99999, maxy = -1;
    always @(negedge clk) if (ce) begin
        if (o_cx === 12'd0 && o_cy === 11'd0 && !at0) frames = frames + 1;
        at0 = (o_cx === 12'd0 && o_cy === 11'd0);
        if (frames == 2 && o_de === 1'b1 && o_rgb !== GREY) begin
            n = n + 1;
            px = o_cx; py = o_cy; px = px - xact0; py = py - yact0;
            if (px < minx) minx = px;
            if (px > maxx) maxx = px;
            if (py < miny) miny = py;
            if (py > maxy) maxy = py;
        end
        if (frames == 3) begin
            report;
            $finish;
        end
    end

    integer bw, bh, sx, sy, errors;
    task report;
        begin
            errors = 0;
            bw = maxx - minx + 1; bh = maxy - miny + 1;
            sx = bw / (CPL * 8); sy = bh / (NL * 9);
            $display("mode %0d: %0d x %0d, box %0d x %0d cells, drawn x %0d..%0d y %0d..%0d = %0d x %0d, scale %0d x %0d",
                     MODE, hact, vact, CPL, NL, minx, maxx, miny, maxy, bw, bh, sx, sy);
            if (bw != CPL * 8 * EXP_SX || bh != NL * 9 * EXP_SY) begin errors = errors + 1; $display("  FAIL scale %0d x %0d, expected %0d x %0d", sx, sy, EXP_SX, EXP_SY); end
            if (n != bw * bh) begin errors = errors + 1; $display("  FAIL box is not a full rectangle: %0d pixels of %0d", n, bw * bh); end
            if (bw >= hact || bh >= vact) begin errors = errors + 1; $display("  FAIL box fills or exceeds the active area (cut or full width)"); end
            if (minx != (hact - bw) / 2 || miny != (vact - bh) / 2) begin errors = errors + 1; $display("  FAIL not centred: left %0d top %0d, expected %0d %0d", minx, miny, (hact - bw) / 2, (vact - bh) / 2); end
            $display("OVL_HOLD mode %0d %0s", MODE, errors == 0 ? "PASS" : "FAIL");
        end
    endtask
    initial begin #400_000_000; $display("OVL_HOLD mode %0d FAIL (timeout)", MODE); $finish; end
endmodule
