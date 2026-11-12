// T97 5.11: trx_afifo_rr (registered read, block RAM) against the shared trx_afifo, two unrelated clocks, random push/pop and rare
// resets. full is compared on every wclk edge, empty on every rclk edge, and rdata on every rclk edge where the FIFO is not empty
// (the only time a consumer reads it). Negative control: the same bench against a mutated trx_afifo_rr must FAIL.
`timescale 1ns/1ps
module tb_afifo #(parameter WP = 10, parameter RP = 7, parameter AW = 2, parameter W = 156);
reg wclk = 0, rclk = 0, wrst = 1, rrst = 1, wr = 0, rd = 0;
reg [W-1:0] wdata = 0;
wire full_r, full_n, empty_r, empty_n; wire [W-1:0] rd_r, rd_n;
trx_afifo    #(.W(W), .AW(AW)) r (.wclk(wclk), .wrst(wrst), .wr(wr), .wdata(wdata), .full(full_r), .rclk(rclk), .rrst(rrst), .rd(rd), .rdata(rd_r), .empty(empty_r));
trx_afifo_rr #(.W(W), .AW(AW)) n (.wclk(wclk), .wrst(wrst), .wr(wr), .wdata(wdata), .full(full_n), .rclk(rclk), .rrst(rrst), .rd(rd), .rdata(rd_n), .empty(empty_n));
integer errs = 0, pushes = 0, pops = 0, fulls = 0, back2back = 0, i, phase = 0;
always #(WP/2.0) wclk = ~wclk;
initial begin #1.3; forever #(RP/2.0) rclk = ~rclk; end
// stimulus changes shortly after the edges
always @(posedge wclk) begin
  #1;
  wr <= (phase % 3 == 0) ? (($random & 3) != 0) : (phase % 3 == 1) ? (($random & 7) == 0) : $random;
  wdata <= {$random, $random, $random, $random, $random, $random};
end
always @(posedge rclk) begin
  #1;
  rd <= (phase % 3 == 1) ? (($random & 3) != 0) : (phase % 3 == 0) ? (($random & 7) == 0) : $random;
end
always @(posedge wclk) begin
  if (full_r !== full_n) begin errs = errs + 1; if (errs < 6) $display("full mismatch %0t", $time); end
  if (wr && !full_r && !wrst) pushes = pushes + 1;
  if (full_r) fulls = fulls + 1;
end
reg popped_last = 0;
always @(posedge rclk) begin
  if (empty_r !== empty_n) begin errs = errs + 1; if (errs < 6) $display("empty mismatch %0t", $time); end
  if (!empty_r && !rrst && rd_r !== rd_n) begin errs = errs + 1; if (errs < 6) $display("rdata mismatch %0t ref=%h new=%h", $time, rd_r, rd_n); end
  if (rd && !empty_r && !rrst) begin pops = pops + 1; if (popped_last) back2back = back2back + 1; popped_last <= 1; end else popped_last <= 0;
end
initial begin
  repeat (6) @(posedge wclk);
  wrst = 0; rrst = 0;
  for (i = 0; i < 60000; i = i + 1) begin
    @(posedge wclk);
    phase = i / 4000;
    if (($random & 16'h3fff) == 0) begin   // a reset of both sides now and then
      wrst = 1; rrst = 1; repeat (4) @(posedge wclk); wrst = 0; rrst = 0;
    end
  end
  if (pushes < 5000 || pops < 5000 || fulls < 500 || back2back < 500) begin errs = errs + 1; $display("COVERAGE too thin"); end
  $display("tb_afifo WP=%0d RP=%0d AW=%0d W=%0d: pushes=%0d pops=%0d full-cycles=%0d back-to-back-pops=%0d errs=%0d", WP, RP, AW, W, pushes, pops, fulls, back2back, errs);
  if (errs == 0) $display("PASS tb_afifo"); else $display("FAIL tb_afifo");
  $finish;
end
endmodule
