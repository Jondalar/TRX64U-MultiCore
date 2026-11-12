// T140 D24 item 1: which encoder word goes to which ball of the A/V port. The three groups of eight balls are the boundary scan's
// (1541ultimate-tools/docs/c64u-pinmap.md, 2026-09-30): group "A" = the balls it read as luma (B22 C20 D21 E22 F21 G22 + C19 E19 by elimination),
// group "B" = chroma (A19 B18 C17 C18 D19 D20 F19 F20), group "C" = its third channel (A18 A20 A21 B20 B21 D22 E21 G21). The scan named the
// groups by how the balls behave, not by what the port pins carry: the sort of group A against the VIC's 16 colours (video_colors.json) fits
// a luma word only to 79 % (best bit order 88 %), so it does not decide A against C. The board did (2026-10-31, Sony TV, S-Video cable, Y to
// the SCART video pin): with the Y word on group A the luma pin showed the composite word (coloured, with the lattice) - so group C is the
// port's luma pin and group A the composite pin. SWAP_YCV = 1 puts y on group C and cv on group A; 0 is the first build's order. The bit order
// inside a group (MSB first as listed) is unchanged: the levels looked right on the board.
module c64scpu_avpins #(parameter SWAP_YCV = 1) (
    input  wire [7:0] y, c, cv,                 // the encoder's words
    output wire [9:2] AVID_R, AVID_G, AVID_B
);
    wire [7:0] ga = SWAP_YCV ? cv : y;          // group A: G9 R9 G7 G6 G4 G3 B9 B6
    wire [7:0] gc = SWAP_YCV ? y : cv;          // group C: R8 R7 R6 R5 R2 G8 G5 G2
    assign {AVID_G[9], AVID_R[9], AVID_G[7], AVID_G[6], AVID_G[4], AVID_G[3], AVID_B[9], AVID_B[6]} = ga;
    assign {AVID_R[4], AVID_R[3], AVID_B[8], AVID_B[7], AVID_B[5], AVID_B[4], AVID_B[3], AVID_B[2]} = c;
    assign {AVID_R[8], AVID_R[7], AVID_R[6], AVID_R[5], AVID_R[2], AVID_G[8], AVID_G[5], AVID_G[2]} = gc;
endmodule
