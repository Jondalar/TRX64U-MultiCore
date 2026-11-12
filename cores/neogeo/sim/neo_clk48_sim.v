// T143: the 48 MHz clock of the benches (trx/neo_clk48.v is an MMCM): a free-running clock, locked at once.
`timescale 1ns / 1ps
module neo_clk48 (
    input  wire cpu_clk,
    output reg  clk48 = 1'b0,
    output wire locked
);
    always #10.41667 clk48 = ~clk48;
    assign locked = 1'b1;
endmodule
