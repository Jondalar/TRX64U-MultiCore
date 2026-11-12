// T175 bench models of the two Xilinx pad primitives diag_pads.v instantiates (the library's behaviour: I to IO unless T, O is always the pad).
`timescale 1ns / 1ps
module IOBUF (inout IO, input I, input T, output O);
    assign IO = T ? 1'bz : I;
    assign O = IO;
endmodule
