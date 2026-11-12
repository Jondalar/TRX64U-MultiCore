// T168: a flop that toggles on its clock and that nothing reads: the register load a clock port needs inside the partition (cores/null/layer2_null.v null_clk_load,
// T123: a clock port with no load is the grey box's HDPR-72 failure). Written for this project (TREX).
`timescale 1ns / 1ps
module ddt_clk_load (input wire clk);
    (* DONT_TOUCH = "TRUE" *) reg t = 1'b0;
    always @(posedge clk) t <= ~t;
endmodule
