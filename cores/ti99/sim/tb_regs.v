`timescale 1ns/1ps
module tb_regs;
  reg clk=0; always #5 clk=~clk;
  reg we=0,re=0; reg [16:0] a=0; reg [7:0] wd=0; wire [7:0] rd; wire ack, irq, pxw, pxr; wire [7:0] ctrl,cfg,wbhi,wbadd; wire [31:0] fb,fs;
  ti99_regs d(.clk(clk),.we(we),.re(re),.addr(a),.wdata(wd),.rdata(rd),.ack(ack),.irq(irq),.build(32'h12345678),.ddr_cal(64'h1122334455667788),
    .px_we(pxw),.px_re(pxr),.px_rdata(8'hEE),.ev_vsync(1'b0),.ev_flip(1'b0),.ev_frame(1'b0),.ctrl(ctrl),.cfg(cfg),.wbhi(wbhi),.wbadd(wbadd),.fb_base(fb),.fb_stride(fs),
    .status(8'h03),.frames(16'h0102),.fb_latest(8'h81),.fb_seq(8'h07),.fb_over(16'h0),.dbg_pc(16'hA1B2),.dbg_st(16'h0),.dbg_wp(16'h0),.dbg_ga(16'h0),.dbg_va(16'h0));
  integer e=0;
  task w(input [11:0] o, input [7:0] v); begin @(posedge clk); #1 a={5'd0,o}; wd=v; we=1; @(posedge clk); #1 we=0; end endtask
  task r(input [11:0] o, input [7:0] x); begin @(posedge clk); #1 a={5'd0,o}; re=1; @(posedge clk); #1 re=0; @(posedge clk); #1 if (rd!==x) begin e=e+1; $display("FAIL %h got %h want %h",o,rd,x); end end endtask
  initial begin
    repeat(4) @(posedge clk);
    r(12'h000,"T"); r(12'h003,"9"); r(12'h004,8'hC0); r(12'h008,8'h78); r(12'h0C4,8'h00); r(12'h0C6,8'hD0);
    w(12'h0A3,8'h10); r(12'h0A3,8'h10); w(12'h0A2,8'h40); r(12'h0A2,8'h40); if (wbadd!==8'h40) e=e+1; if (wbhi!==8'h10) e=e+1;
    w(12'h0A1,8'h05); #1 if (cfg!==8'h05) e=e+1;
    repeat(4) @(posedge clk); r(12'h0A4,8'h03); r(12'h0A8,8'h02); r(12'h0C0,8'h81); r(12'h0B0,8'hB2); r(12'h0B1,8'hA1);
    @(posedge clk); a=17'h06010; re=1; #1 if(!pxr) e=e+1; @(posedge clk); #1 re=0; if (rd!==8'hEE) begin e=e+1; $display("FAIL px"); end
    if (e==0) $display("PASS ti99_regs"); else $display("FAILED %0d",e); $finish;
  end
endmodule
