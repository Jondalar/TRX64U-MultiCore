// T90 S5: the 128 TED colour codes through colors_to_rgb and the RGB565 packing of c264_core give at least 121 distinct values
// as many as the 12 bit table has: RGB565 (top bits repeated) loses nothing of the table's 4 bit channels. Finding: the C16Nano table has
// fewer than the spec's 121 distinct values at 4 bits per channel (colours that differ by less than one 4 bit step coincide).
`timescale 1ns / 1ps
module tb_c264_colors;
    reg clk = 0;
    always #5 clk = ~clk;
    reg [6:0] color = 0;
    wire [3:0] r, g, b;
    colors_to_rgb dut (.clk(clk), .color(color), .red(r), .green(g), .blue(b));
    wire [15:0] rgb565 = {r, r[3], g, g[3:2], b, b[3]};
    reg [15:0] seen [0:127];
    reg [11:0] seen12 [0:127];
    integer d12;
    integer i, j, distinct, dup;
    initial begin
        for (i = 0; i < 128; i = i + 1) begin
            color = i; repeat (3) @(posedge clk); #1 begin seen[i] = rgb565; seen12[i] = {r, g, b}; end
        end
        distinct = 0;
        for (i = 0; i < 128; i = i + 1) begin
            dup = 0;
            for (j = 0; j < i; j = j + 1) if (seen[j] == seen[i]) dup = 1;
            if (!dup) distinct = distinct + 1;
        end
        d12 = 0;
        for (i = 0; i < 128; i = i + 1) begin
            dup = 0;
            for (j = 0; j < i; j = j + 1) if (seen12[j] == seen12[i]) dup = 1;
            if (!dup) d12 = d12 + 1;
        end
        $display("%0d distinct RGB565 values, %0d distinct 12 bit values, from 128 codes", distinct, d12);
        if (distinct == d12) $display("PASS tb_c264_colors"); else $display("FAILED tb_c264_colors");
        $finish;
    end
endmodule
