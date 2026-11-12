// T175: the pads of the two joystick ports (pins 1-4 and 6 of each, KBJOY_1_JOY / KBJOY_2_JOY): open drain. low[i] = 1 drives the pin low, anything else releases it, which is
// the input the host always had (the PULLUP stays in pins.xdc); `i` is the pad's level in both cases. A released pin is the old pure input; the pad is never driven high. While the
// host decouples the core (core_decouple) every pin is released, as the other pads of top.v are.
`timescale 1ns / 1ps
module diag_pads (
    inout  wire [4:0] KBJOY_1_JOY,
    inout  wire [4:0] KBJOY_2_JOY,
    input  wire [9:0] low,
    input  wire       decouple,
    output wire [4:0] j1_i,
    output wire [4:0] j2_i
);
    genvar k;
    generate for (k = 0; k < 5; k = k + 1) begin : g
        IOBUF b_j1 (.IO(KBJOY_1_JOY[k]), .I(1'b0), .T(decouple | ~low[k]),     .O(j1_i[k]));
        IOBUF b_j2 (.IO(KBJOY_2_JOY[k]), .I(1'b0), .T(decouple | ~low[k + 5]), .O(j2_i[k]));
    end endgenerate
endmodule
