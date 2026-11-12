// T175: the open-drain joystick pads (diag_pads.v). Released (low = 0), every pin is the plain input it always was: with each pin held low from outside, pulled up, or toggling,
// the level the host sees (j*_i) is the pad's level, and nothing drives the pad (the external side keeps control). low[k] pulls exactly that pin low and nothing else; the
// pad is never driven high; core_decouple releases everything. The pads carry the pull-up of pins.xdc here as tri1 nets.
`timescale 1ns / 1ps
module tb_pads;
    tri1 [4:0] p1, p2;                          // the board's pull-ups
    reg  [4:0] x1 = 5'b11111, x2 = 5'b11111;    // the outside: bit set = not driven (Z), clear = held low
    reg  [9:0] low = 10'd0;
    reg        dec = 0;
    wire [4:0] j1, j2;
    genvar g;
    generate for (g = 0; g < 5; g = g + 1) begin : ext
        assign p1[g] = x1[g] ? 1'bz : 1'b0;
        assign p2[g] = x2[g] ? 1'bz : 1'b0;
    end endgenerate
    diag_pads dut (.KBJOY_1_JOY(p1), .KBJOY_2_JOY(p2), .low(low), .decouple(dec), .j1_i(j1), .j2_i(j2));
    integer errors = 0, checks = 0, a, b, k;
    task check(input c, input [8*80-1:0] msg); begin checks = checks + 1; if (!c) begin errors = errors + 1; $display("FAIL %0s", msg); end end endtask
    initial begin
        // released: all 1024 combinations of the outside; the host sees exactly what a pure input would see
        for (a = 0; a < 32; a = a + 1) for (b = 0; b < 32; b = b + 1) begin
            x1 = a[4:0]; x2 = b[4:0]; #1;
            check(j1 === a[4:0] && j2 === b[4:0], "released pins read as the plain input (1024 combinations)");
            check(p1 === a[4:0] && p2 === b[4:0], "released: the pad level is the outside's, nothing is driven");
        end
        // driven: exactly one pin at a time (outside not driving)
        x1 = 5'b11111; x2 = 5'b11111;
        for (k = 0; k < 10; k = k + 1) begin
            low = 10'd1 << k; #1;
            check({j2, j1} === ~(10'd1 << k), "low[k] pulls pin k low and no other pin");
            check({p2, p1} === ~(10'd1 << k), "the pad itself is low, the others float high");
        end
        low = 10'h3FF; #1; check({j2, j1} === 10'h000, "all ten pulled low");
        // decouple releases
        dec = 1; #1; check({j2, j1} === 10'h3FF, "core_decouple releases every pin");
        dec = 0; low = 10'd0; #1;
        // held low from outside and drive requested: still low, no contention
        x1 = 5'b11110; low = 10'd1; #1; check(j1 === 5'b11110, "outside low and low[0]: low");
        low = 10'd0; #1; check(j1 === 5'b11110, "drive released: the outside's low stays");
        if (errors == 0) $display("PASS tb_pads errors=0 checks=%0d", checks); else $display("FAIL tb_pads errors=%0d", errors);
        $finish;
    end
endmodule
