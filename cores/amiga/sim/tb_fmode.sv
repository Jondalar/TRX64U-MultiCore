// A1200 stripes: denise_bitplanes with FMODE=3 (64-bit fetch), lores, one plane. A fetch writes BPL1DAT with
// data_in = 0xFFFF (first word) and chip48 = X. The core passes chip48 into the shifter; amiga_core.v ties it
// to 0. Count 28 MHz clocks (4 per lores pixel) with the bit set of bpldata[1] per fetch: chip48=0 -> 16 pixels (3 of 4 words missing),
// chip48=all ones -> 64 pixels. Prints PASS only if both runs give the expected count (negative control).
`timescale 1ns/1ps
module tb_fmode;
    reg clk = 0; always #5 clk = ~clk;
    reg [1:0] p = 0; always @(posedge clk) p <= p + 1;
    wire c1 = (p == 1) | (p == 2), c3 = (p == 2) | (p == 3), clk7_en = (p == 3);
    reg [8:1] ra = 0; reg [15:0] din = 0; reg [47:0] c48 = 0; reg reset = 1;
    wire [8:1] bd;
    denise_bitplanes dut (.ce28(1'b1), .clk(clk), .clk7_en(clk7_en), .reset(reset), .c1(c1), .c3(c3), .aga(1'b1),
        .reg_address_in(ra), .data_in(din), .chip48(c48), .hires(1'b0), .shres(1'b0), .hpos(9'd0), .bpldata(bd));
    integer ones, n;
    task wr(input [8:0] a, input [15:0] d); begin
        @(posedge clk); while (!clk7_en) @(posedge clk);
        ra <= a[8:1]; din <= d; repeat (4) @(posedge clk); ra <= 0; end endtask
    task run(input [47:0] x, output integer cnt); begin
        c48 = x; cnt = 0;
        wr(9'h1fc, 16'h0003);
        wr(9'h110, 16'hFFFF);
        repeat (800) begin @(posedge clk); if (bd[1]) cnt = cnt + 1; end
    end endtask
    integer a, b;
    initial begin
        repeat (8) @(posedge clk); reset = 0; repeat (8) @(posedge clk);
        run(48'd0, a);
        repeat (400) @(posedge clk);
        run(48'hFFFFFFFFFFFF, b);
        $display("ones per 64-bit fetch: chip48=0 -> %0d, chip48=ones -> %0d", a, b);
        if (a == 64 && b == 256) $display("PASS: only word 0 reaches the picture while chip48 is 0");
        else $display("FAIL");
        $finish;
    end
endmodule
