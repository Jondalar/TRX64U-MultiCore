// rv_dbg.v: PC stall run, longest stall, IRQ edges, snapshot hold.
`timescale 1ns/1ps
module tb_rv_dbg;
    reg clk = 0, rst = 1;
    reg [127:0] tap = 128'd0;
    reg snap = 0, clr = 0;
    wire [255:0] hold;
    rv_dbg dut (.clk(clk), .rst(rst), .tap(tap), .snap_req(snap), .clr_req(clr), .hold(hold));
    always #5 clk = ~clk;
    integer fails = 0;
    task check(input [255:0] got, input [255:0] want, input [127:0] what);
        if (got !== want) begin fails = fails + 1; $display("FAIL %0s: got %0d want %0d", what, got, want); end
    endtask
    task snapshot; begin snap = ~snap; repeat (6) @(posedge clk); end endtask
    function [31:0] w(input integer i); w = hold[32*i +: 32]; endfunction
    integer i;
    initial begin
        repeat (4) @(posedge clk); rst = 0;
        // a moving PC: no run
        for (i = 0; i < 20; i = i + 1) begin tap[31:0] = 32'h1000 + 4 * i; @(posedge clk); end
        snapshot;
        check(w(0), 32'h1000 + 4 * 19, "pc moving");
        if (w(4) > 10) begin fails = fails + 1; $display("FAIL run while moving: %0d", w(4)); end
        // stand still on a load: I/O busy, address 0x1A0104, read, IRQ rises twice
        tap[31:0] = 32'hDEAD0;
        tap[32] = 1; tap[34] = 1; tap[93:70] = 24'h1A0104; tap[94] = 0;
        @(posedge clk);
        repeat (100) @(posedge clk);
        tap[35] = 1; @(posedge clk); @(posedge clk); tap[35] = 0; repeat (3) @(posedge clk);
        tap[35] = 1; repeat (3) @(posedge clk); tap[35] = 0;
        repeat (100) @(posedge clk);
        snapshot;
        check(w(0), 32'hDEAD0, "pc stuck");
        if (w(4) < 200 || w(4) > 230) begin fails = fails + 1; $display("FAIL run %0d", w(4)); end
        check(w(3) & 32'hFFFFFF, 2, "irq edges");
        check(w(3) >> 24, 8'd0, "dma timeouts");
        check(w(5) >= 200, 1, "max run follows the run");
        check(w(6), 32'hDEAD0, "max pc");
        check(w(7), {4'd0, 1'b0, 1'b1, 1'b0, 1'b1, 24'h1A0104}, "ctx");
        check(w(1) >> 0 & 1, 1, "io busy bit");
        // move on: the run ends, the max stays
        tap[32] = 0; tap[31:0] = 32'hBEEF0; @(posedge clk); repeat (3) @(posedge clk);
        snapshot;
        if (w(4) > 20) begin fails = fails + 1; $display("FAIL run after move %0d", w(4)); end
        if (w(5) < 200) begin fails = fails + 1; $display("FAIL max lost %0d", w(5)); end
        // clr
        clr = ~clr; repeat (6) @(posedge clk);
        snapshot;
        if (w(5) > 40) begin fails = fails + 1; $display("FAIL clr, max %0d", w(5)); end
        if (fails) $display("FAIL"); else $display("PASS");
        $finish;
    end
endmodule
