// vic_scaler against a synthetic VIC: colour = x[3:0] ^ y[3:0]. Checks that the
// output locks, has 1920x1080 active pixels per frame and shows, for every
// active pixel in the window, the colour of the VIC pixel it maps to.
// -DV60 (T40): the 60 Hz frame: VIC 520 x 263, 2200 x 1125 output clocks
// (61875/13676 of vclk), window from line 31 (wrapping at 263) at 4.5 output
// lines per VIC line; also counts the clocks per line and lines per frame.
// PAL (default): 2640 x 1125 output clocks (6875/1456 of vclk, CEA 1080p50 blanking:
// sync at cx 528-571, cx 0-695 then 744-2687). Both modes check the cx sequence
// (the jump to 744), the hs position and width and the clocks per line.
// T59: the palette. Frames 0-2 must show the reset palette (Colodore); then the
// bench writes a different one through the bus port (R, G, B bytes at 4n+0..2,
// the byte at 4n+3 written with 0xFF and ignored, a second bus clock) during the
// vertical blanking, and frames 3-4 must show every one of the 16 indices in the
// new colours (an index-to-RGB check on all 16: the pattern covers them all).
// T62 G09: frames 5-6 run with scan=1: the last output line of every VIC line (oy mod 4 = 3 at 50 Hz, the
// line before the VIC line changes at 60 Hz) must show the colour at 25 % (each channel >> 2), all other
// lines and the black outside the window unchanged; frames 0-4 (scan=0) are unchanged output.
//   iverilog -g2012 [-DV60] -o s tb_scaler.v ../vic_scaler.v ../pal_ram.v && vvp s
`timescale 1ns/1ps
module tb;
    reg vclk = 0, clk = 0;
`ifdef V60
    localparam XMAX = 519, YMAX = 262, V60 = 1;
    always #15.279 vclk = ~vclk;      // 32.7257 MHz (NTSC)
    always #3.3770 clk  = ~clk;       // 148.06 MHz (61875/13676 of vclk)
`else
    localparam XMAX = 503, YMAX = 311, V60 = 0;
    always #15.859 vclk = ~vclk;      // 31.5278 MHz
    always #3.3586 clk  = ~clk;       // 148.87 MHz (6875/1456 of vclk)
`endif

    // VIC: 504 x 312 (520 x 263) dots, one dot per 4 vclk
    reg [1:0] ph = 0; reg [9:0] x = 0; reg [8:0] y = 0;
    always @(posedge vclk) begin
        ph <= ph + 1;
        if (ph == 3) begin
            if (x == XMAX) begin x <= 0; y <= (y == YMAX) ? 0 : y + 1; end
            else x <= x + 1;
        end
    end
    wire [3:0] pix = x[3:0] ^ y[3:0];

    reg pclk = 0;
    always #6.1 pclk = ~pclk;            // the app's bus clock, unrelated to the others
    reg        pal_we = 0;
    reg  [5:0] pal_addr = 0;
    reg  [7:0] pal_wdata = 0;
    reg [23:0] epal [0:15];              // what the output must show
    integer pi;
    initial begin
        epal[0]=24'h000000; epal[1]=24'hFFFFFF; epal[2]=24'h813338; epal[3]=24'h75CEC8;
        epal[4]=24'h8E3C97; epal[5]=24'h56AC4D; epal[6]=24'h2E2C9B; epal[7]=24'hEDF171;
        epal[8]=24'h8E5029; epal[9]=24'h553800; epal[10]=24'hC46C71; epal[11]=24'h4A4A4A;
        epal[12]=24'h7B7B7B; epal[13]=24'hA9FF9F; epal[14]=24'h706DEB; epal[15]=24'hB2B2B2;
    end
    task padd(input [5:0] a, input [7:0] d);
        begin
            @(posedge pclk); pal_we <= 1; pal_addr <= a; pal_wdata <= d;
            @(posedge pclk); pal_we <= 0;
        end
    endtask
    task write_palette;
        reg [23:0] c;
        begin
            for (pi = 0; pi < 16; pi = pi + 1) begin
                c = {8'(pi * 16 + 5), 8'(255 - pi * 13), 8'(pi * 7 + 100)};
                epal[pi] = c;
                padd(pi * 4 + 0, c[23:16]);
                padd(pi * 4 + 1, c[15:8]);
                padd(pi * 4 + 2, c[7:0]);
                padd(pi * 4 + 3, 8'hFF);       // the fourth byte is not a colour
            end
        end
    endtask

    reg rst = 1;
    reg scan = 0;
    wire [23:0] rgb0; wire de, hs, vs, locked; wire [11:0] cx; wire [10:0] cy;
    vic_scaler #(.X0(80), .Y0(23), .ALIGN(27)) dut (
        .vclk(vclk), .pix(pix), .px(x), .py(y),
        .clk(clk), .rst(rst), .pal_clk(pclk), .pal_we(pal_we), .pal_addr(pal_addr), .pal_wdata(pal_wdata), .rgb(rgb0), .de(de), .hs(hs), .vs(vs), .cx(cx), .cy(cy),
        .locked(locked), .blank(1'b0), .x_text(), .v60(V60[0]), .scan(scan), .relock(1'b0));

    // the VIC line output line oy shows
    function integer vline(input integer oy);
        integer l;
        begin
            if (V60) begin
                l = 31 + (oy * 2) / 9;
                vline = l >= 263 ? l - 263 : l;
            end else
                vline = 23 + oy / 4;
        end
    endfunction
    // the last output line of its VIC line
    function last_line(input integer oy);
        last_line = V60 ? (((oy + 1) * 2) / 9 != (oy * 2) / 9) : (oy % 4 == 3);
    endfunction
    function [23:0] dim(input [23:0] c);
        dim = {2'b00, c[23:18], 2'b00, c[15:10], 2'b00, c[7:2]};
    endfunction
    integer dimmed = 0;
    // clocks per line (hs to hs) and lines per frame (hs between two vs)
    localparam HTOT = V60 ? 2200 : 2640, HJUMP = V60 ? 255 : 695, HS0 = V60 ? 88 : 528, HS1 = V60 ? 132 : 572;
    integer hclk = 0, hper = 0, hlines = 0, vlines = 0, hbad = 0, cxbad = 0, hsbad = 0;
    reg hs_q = 0, vs_q = 0;
    reg [11:0] cx_q = 0; reg cx_v = 0;
    always @(posedge clk) if (locked) begin
        hs_q <= hs; vs_q <= vs;
        cx_q <= cx; cx_v <= 1;
        if (cx_v && cx != (cx_q == HJUMP ? 744 : cx_q == 2687 ? 0 : cx_q + 1)) cxbad = cxbad + 1;
        if (hs != (cx >= HS0 && cx < HS1)) hsbad = hsbad + 1;
        hclk = hclk + 1;
        if (hs && !hs_q) begin
            if (hper != 0 && hclk != HTOT) hbad = hbad + 1;
            if (cx != HS0) hsbad = hsbad + 1;
            hper = hclk; hclk = 0; hlines = hlines + 1;
        end
        if (vs && !vs_q) begin vlines = hlines; hlines = 0; end
    end

    // follow the output
    integer ox = 0, oy = 0, frames = 0, errors = 0, checked = 0, de_count = 0;
    reg de_d = 0, vs_d = 0;
    function [23:0] pal(input [3:0] c); pal = epal[c]; endfunction
    always @(posedge clk) if (locked) begin
        de_d <= de; vs_d <= vs;
        if (de) begin
            de_count = de_count + 1;
            if (cx != 768 + ox || cy != 45 + oy) begin
                errors = errors + 1;
                if (errors < 5) $display("counter mismatch cx %0d cy %0d at x %0d y %0d", cx, cy, ox, oy);
            end
            if (ox >= 160 && ox < 1760) begin
                // expected: VIC pixel (80 + (ox-160)/4, vline(oy))
                if (rgb0 !== ((scan && last_line(oy)) ? dim(pal(((80 + (ox-160)/4) & 15) ^ (vline(oy) & 15)))
                                                      : pal(((80 + (ox-160)/4) & 15) ^ (vline(oy) & 15)))) begin
                    errors = errors + 1;
                    if (errors < 5) $display("mismatch frame %0d x %0d y %0d", frames, ox, oy);
                end
                checked = checked + 1;
                if (scan && last_line(oy)) dimmed = dimmed + 1;
            end else if (rgb0 !== 24'h0 && !(ox >= 16 && ox < 144 && oy >= 100 && oy < 356))
                errors = errors + 1;          // black outside the window (bar debug cells aside)
            ox = ox + 1;
        end
        if (de_d && !de) begin ox = 0; oy = oy + 1; end
        if (vs && !vs_d) begin
            if (frames > 0)
                $display("frame %0d: %0d active pixels, %0d lines, %0d checked, %0d errors; %0d clocks per line (%0d off), %0d lines per frame, cx jumps off %0d, hs off %0d",
                         frames, de_count, oy, checked, errors, hper, hbad, vlines, cxbad, hsbad);
            if (frames == 0) begin errors = 0; cxbad = 0; hsbad = 0; hbad = 0; end     // the frame cut short by the lock does not count
            if (frames == 2) begin
                if (errors == 0 && hbad == 0 && cxbad == 0 && hsbad == 0 && hper == HTOT && vlines == 1125 && de_count == 1920 * 1080)
                    $display("default palette ok");
                else begin $display("FAIL"); $finish; end
                write_palette;                  // in the vertical sync, before the active lines
                errors = 0;
            end
            if (frames == 4) begin
                if (errors == 0 && hbad == 0 && cxbad == 0 && hsbad == 0 && hper == HTOT && vlines == 1125 && de_count == 1920 * 1080)
                    $display("new palette ok"); else begin $display("FAIL"); $finish; end
                scan = 1; errors = 0;           // in the vertical sync, before the active lines
            end
            if (frames == 6) begin
                // one frame: 1080 lines, a quarter (50 Hz) or 2/9 (60 Hz) of the 1080 window lines dimmed, 1600 pixels each
                $display("scan lines: %0d dimmed pixels of %0d checked", dimmed, checked);
                if (errors == 0 && hbad == 0 && cxbad == 0 && hsbad == 0 && hper == HTOT && vlines == 1125 && de_count == 1920 * 1080
                    && dimmed > 0 && dimmed < checked / 3)
                    $display("PASS"); else $display("FAIL");
                $finish;
            end
            frames = frames + 1; oy = 0; de_count = 0; if (frames == 6) begin dimmed = 0; checked = 0; end
        end
    end

    initial begin
        #500 rst = 0;
        #250000000 $display("TIMEOUT locked=%b", locked); $finish;
    end
endmodule
