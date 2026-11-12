// T91 D1/D4 (shared with the CPC core, T92): a fractional clock enable. Every sys_clk the accumulator takes `num`; when it reaches
// `den` the enable fires and `den` is taken off. The rate is num/den of sys_clk, exact over `den` clocks (no jitter correction, the
// enable is as even as an integer ratio can be). `hold` stops the enable and freezes the accumulator, so a resume continues in the
// phase it stopped in; `restart` clears the phase. num and den are quasi static (changed only while the core is in reset).
// Written for this project (TREX), no upstream code.
`timescale 1ns / 1ps
module zxcpc_nco #(
    parameter integer W = 15
) (
    input  wire         clk,
    input  wire         hold,
    input  wire         restart,
    input  wire [W-1:0] num,
    input  wire [W-1:0] den,
    output wire         en
);
    reg  [W-1:0] acc = {W{1'b0}};
    wire [W:0]   s    = {1'b0, acc} + {1'b0, num};
    wire         fire = (s >= {1'b0, den});
    wire [W:0]   sd   = s - {1'b0, den};
    assign en = fire & ~hold;
    always @(posedge clk) begin
        if (restart) acc <= {W{1'b0}};
        else if (!hold) acc <= fire ? sd[W-1:0] : s[W-1:0];
    end
endmodule
