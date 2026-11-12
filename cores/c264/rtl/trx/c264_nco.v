// T90: the TED clock enable. TED clock = 17.734475 MHz x 8/5 = 28.37516 MHz, sys_clk = 17.734475 MHz x 16/9 = 31.527778 MHz,
// so the TED clock is sys_clk x 9/10 exactly: a phase accumulator that fires nine times in ten clocks. The pause (hold)
// freezes the accumulator, so a resume continues with the phase it stopped in (the same state a skipped clock leaves).
// Written for this project (TREX), no upstream code.
`timescale 1ns / 1ps
module c264_nco (
    input  wire clk,
    input  wire hold,       // pause: no enable, accumulator frozen
    output wire en
);
    reg [3:0] acc = 4'd0;   // 0, 9, 8, ... 1: fires unless 0 (acc + 9 >= 10)
    assign en = (acc != 4'd0) & ~hold;
    always @(posedge clk) begin
        if (!hold) acc <= (acc != 4'd0) ? acc - 4'd1 : 4'd9;
    end
endmodule
