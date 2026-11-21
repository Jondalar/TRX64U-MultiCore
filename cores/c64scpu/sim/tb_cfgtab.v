// T248 S1: scpu_cfgtab at all 4096 addresses, one per clock. After each clock edge q must be the class of the address taken at that edge.
// The expected classes (cfgtab_exp.hex) come from scpu_cfgtab.txt via cfgtab_exp.py, not from scpu_cfgtab.v.
`timescale 1ns / 1ps
module tb_cfgtab;
  reg clk = 1'b0;
  reg [11:0] a = 12'd0;
  wire [3:0] q;
  reg [3:0] exp [0:4095];
  integer i, bad;
  scpu_cfgtab dut (.clk(clk), .a(a), .q(q));
  always #5 clk = ~clk;
  initial begin
    $readmemh("cfgtab_exp.hex", exp);
    bad = 0;
    for (i = 0; i < 4096; i = i + 1) begin
      a = i[11:0];
      @(posedge clk); #1;
      if (q !== exp[i]) begin
        bad = bad + 1;
        if (bad <= 10) $display("S1 MISMATCH addr %03x: q=%0d expected %0d", i, q, exp[i]);
      end
      a = ~i[11:0];   // the address changes after the edge: q must not follow it before the next edge
      #2;
      if (q !== exp[i]) begin
        bad = bad + 1;
        if (bad <= 10) $display("S1 MISMATCH addr %03x: q follows the address between edges", i);
      end
      @(negedge clk);
    end
    if (bad == 0) $display("S1 PASS 4096/4096"); else $display("S1 FAIL %0d mismatches", bad);
    $finish;
  end
endmodule
