// T113: the VDC's 4 bit RGBI colour number to RGB565, for the frame writer (c128_pixw). The 16 entries are the table that
// layer2_c128.v has as `rgbi565` and are kept identical to it.
// The colour numbers are the VDC's {R, G, B, I} (8563 programmer's reference: 2 dark blue, 4 dark green, 8 dark red, 12 dark yellow/brown,
// 13 light yellow, 14 light grey, 15 white); each component is 0xAA with its bit set, plus 0x55 with I (dark yellow: green 0x55, as CGA).
// (The table that layer2_c128.v had was indexed {B, R, G, I}: found by the S9 bench, corrected here and in the bench's own copy.)
`timescale 1ns / 1ps
module c128_rgbi (
    input  wire [3:0]  rgbi,
    output reg  [15:0] rgb565
);
    always @* begin
        case (rgbi)
            4'h0: rgb565 = 16'h0000; 4'h1: rgb565 = 16'h52AA; 4'h2: rgb565 = 16'h0015; 4'h3: rgb565 = 16'h52BF;
            4'h4: rgb565 = 16'h0540; 4'h5: rgb565 = 16'h57EA; 4'h6: rgb565 = 16'h0555; 4'h7: rgb565 = 16'h57FF;
            4'h8: rgb565 = 16'hA800; 4'h9: rgb565 = 16'hFAAA; 4'hA: rgb565 = 16'hA815; 4'hB: rgb565 = 16'hFABF;
            4'hC: rgb565 = 16'hAAA0; 4'hD: rgb565 = 16'hFFEA; 4'hE: rgb565 = 16'hAD55; default: rgb565 = 16'hFFFF;
        endcase
    end
endmodule
