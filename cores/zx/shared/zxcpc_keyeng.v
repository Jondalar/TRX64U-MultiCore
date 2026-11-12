// T91 D8 (shared with the CPC core, T92): the key translator's engine. keys[] is the host's C64 matrix (keys[col * 8 + row], 1 = down);
// the machine's key set is the OR of one table row per pressed key, the row taken from T0 (SHIFT up) or T1 (SHIFT down). The tables are
// parameters, written by tools/zxcpc_keys.py from the key NAMES of the two keyboards (a generated module per machine, zx_keys.v), so the
// table is data, not logic written by hand. The SHIFT keys themselves have no row (SHIFTMASK): they select the table, and `other` says
// whether any other key is down (a machine whose SHIFT key is a key of its own gives it only when nothing else is, see zx_keys.v).
// Written for this project (TREX), no upstream code.
`timescale 1ns / 1ps
module zxcpc_keyeng #(
    parameter integer W = 40,
    parameter [64*W-1:0] T0 = {64*W{1'b0}},
    parameter [64*W-1:0] T1 = {64*W{1'b0}},
    parameter [63:0] SHIFTMASK = 64'd0
) (
    input  wire [63:0]  keys,
    input  wire         shift,
    output wire [W-1:0] out,
    output wire         other
);
    integer i;
    reg [W-1:0] a;
    always @* begin
        a = {W{1'b0}};
        for (i = 0; i < 64; i = i + 1)
            if (keys[i] && !SHIFTMASK[i]) a = a | (shift ? T1[i * W +: W] : T0[i * W +: W]);
    end
    assign out = a;
    assign other = |(keys & ~SHIFTMASK);
endmodule
