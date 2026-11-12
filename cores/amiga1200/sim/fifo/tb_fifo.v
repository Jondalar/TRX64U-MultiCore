// T97 5.8: the A1200 copy of paula_floppy_fifo (block RAM read register) against the shared file, side by side.
// Random rd/wr/reset/enable stimulus; out, empty and full must match on every clock.
`timescale 1ns/1ps
module tb_fifo;
reg clk = 0, ce28 = 0, clk7_en = 0, reset = 1, rd = 0, wr = 0;
reg [15:0] in = 0;
wire [15:0] o_ref, o_new; wire e_ref, e_new, f_ref, f_new;
paula_floppy_fifo_ref r(.ce28(ce28),.clk(clk),.clk7_en(clk7_en),.reset(reset),.in(in),.out(o_ref),.rd(rd),.wr(wr),.empty(e_ref),.full(f_ref));
paula_floppy_fifo     n(.ce28(ce28),.clk(clk),.clk7_en(clk7_en),.reset(reset),.in(in),.out(o_new),.rd(rd),.wr(wr),.empty(e_new),.full(f_new));
wire [15:0] o_cmp = o_new;
integer i, errs = 0, reads = 0, bypass = 0, fulls = 0, phase;
always #5 clk = ~clk;
initial begin
  // both start with out = 0 (the shared file's out is X in simulation until the first write; load it alike)
  r.out = 16'd0;
  for (i = 0; i < 400000; i = i + 1) begin
    @(negedge clk);
    phase = (i / 50000);            // vary the fill pattern: write-heavy, read-heavy, balanced
    ce28    = ($random & 3) != 0;
    clk7_en = ($random & 3) != 0;
    reset   = (i < 4) || (($random & 16'hffff) == 0);
    wr = (phase % 3 == 0) ? (($random & 7) != 0) : (phase % 3 == 1) ? (($random & 7) == 0) : $random;
    rd = (phase % 3 == 1) ? (($random & 7) != 0) : (phase % 3 == 0) ? (($random & 7) == 0) : $random;
    in = $random;
    if (ce28 && clk7_en && !reset && rd && !e_ref) reads = reads + 1;
    @(posedge clk); #1;
    if (f_ref) fulls = fulls + 1;
    if (n.sel) bypass = bypass + 1;
    if (o_ref !== o_cmp || e_ref !== e_new || f_ref !== f_new) begin
      errs = errs + 1;
      if (errs <= 5) $display("MISMATCH cycle %0d: ref out=%h empty=%b full=%b  new out=%h empty=%b full=%b", i, o_ref, e_ref, f_ref, o_cmp, e_new, f_new);
    end
  end
  $display("cycles=%0d reads=%0d bypass_cycles=%0d full_cycles=%0d errors=%0d", i, reads, bypass, fulls, errs);
  if (errs == 0 && reads > 10000 && bypass > 100 && fulls > 100) $display("PASS tb_fifo");
  else $display("FAIL tb_fifo");
  $finish;
end
endmodule
