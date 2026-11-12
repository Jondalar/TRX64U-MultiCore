// T160 S1: trx_gain, the host's master volume (host/top.v, every core). Unity (128) passes every sample unchanged, 0 is silent, 64 halves (arithmetic shift),
// 255 saturates at both ends instead of wrapping, left and right are independent, a changed volume byte is taken within a few clocks.
`timescale 1ns / 1ps
module tb_gain;
    reg clk = 0; always #5 clk = ~clk;
    reg [7:0] vol = 8'd128;
    reg signed [23:0] il = 0, ir = 0;
    wire signed [23:0] ol, or_;
    trx_gain dut (.clk(clk), .vol(vol), .in_l(il), .in_r(ir), .out_l(ol), .out_r(or_));
    integer errors = 0, i;
    task check(input cond, input [255:0] what); begin
        if (!cond) begin errors = errors + 1; $display("FAIL %0s: vol=%0d in=%0d/%0d out=%0d/%0d", what, vol, il, ir, ol, or_); end
    end endtask
    task put(input signed [23:0] l, input signed [23:0] r); begin il = l; ir = r; repeat (6) @(posedge clk); #1; end endtask
    task setvol(input [7:0] v); begin vol = v; repeat (8) @(posedge clk); #1; end endtask
    reg signed [23:0] x;
    initial begin
        #100;
        setvol(8'd128);
        put(24'sd0, 24'sd0);                         check(ol == 0 && or_ == 0, "unity zero");
        put(24'sd1, -24'sd1);                        check(ol == 1 && or_ == -1, "unity one");
        put(24'sh7FFFFF, 24'sh800000);               check(ol == 24'sh7FFFFF && or_ == 24'sh800000, "unity extremes");
        put(24'sd256000, -24'sd512000);              check(ol == 24'sd256000 && or_ == -24'sd512000, "unity amiga sums");
        for (i = 0; i < 200; i = i + 1) begin
            x = $random; put(x, ~x);                 check(ol == x && or_ == ~x, "unity random");
        end
        setvol(8'd0);
        put(24'sh123456, -24'sh654321);              check(ol == 0 && or_ == 0, "zero is silent");
        put(24'sh7FFFFF, 24'sh800000);               check(ol == 0 && or_ == 0, "zero is silent at the extremes");
        setvol(8'd64);
        put(24'sd1000, -24'sd1000);                  check(ol == 24'sd500 && or_ == -24'sd500, "half");
        put(24'sh7FFFFF, 24'sh800000);               check(ol == 24'sh3FFFFF && or_ == 24'shC00000, "half extremes");
        setvol(8'd32);
        put(24'sd4000, -24'sd8000);                  check(ol == 24'sd1000 && or_ == -24'sd2000, "quarter, channels apart");
        setvol(8'd255);
        put(24'sd1000, -24'sd1000);                  check(ol == 24'sd1992 && or_ == -24'sd1993, "255 is just under twice (floor)");
        put(24'sh600000, 24'sh900000);               check(ol == 24'sh7FFFFF && or_ == 24'sh800000, "saturates, does not wrap");
        put(24'sh500000, -24'sh500000);              check(ol == 24'sh7FFFFF && or_ == 24'sh800000, "saturates on both signs");
        setvol(8'd128);
        put(24'sd777, -24'sd777);                    check(ol == 24'sd777 && or_ == -24'sd777, "back to unity");
        if (errors == 0) $display("PASS tb_gain errors=0"); else $display("FAIL tb_gain errors=%0d", errors);
        $finish;
    end
endmodule
