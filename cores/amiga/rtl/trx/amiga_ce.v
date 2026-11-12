// T85: the chipset's clock enable. 28.37516 MHz = sys_clk x 9/10 exactly (both are the PAL crystal 17.734475 MHz
// times a ratio: 16/9 and 8/5), so a phase accumulator that fires nine times in ten clocks is the Amiga's
// 28 MHz. The elastic stall of amiga_mem stops the tick that ends a 7 MHz bus cycle while its memory request is
// not answered: the whole machine stands still, then goes on, exact but slower than real time.
// phase = the 7 MHz divider of amiga_clk (clk7_cnt). A bus cycle is the four ticks that end phase 2, 3, 0, 1
// (the last one is the 7 MHz tick, clk7_en is high during phase 1). The CPU takes its data at the tick that
// ends phase 0 (cpu_wrapper: !c1 && c3), the chipset at the 7 MHz tick, so the data must be there before the
// tick that ends phase 0. The decision for a tick is made one clock earlier: block while phase = 0, and in
// phase 3 when the tick that ends phase 3 is executing now.
// Pause (Freeze, T85): `pause` holds ce28 low and freezes the phase accumulator, the same state the elastic stall
// leaves behind, so the machine stands exactly where it was and goes on from there. It takes effect at a clock
// boundary, between two ticks (ce28 is a one-clock pulse), never inside one, and the units that are still working on a
// DDR2 access (amiga_mem, amiga_fb, amiga_sdc) are not tied to ce28, so they finish what they started while the
// machine is held; amiga_core reports when all of them are quiet.
// Written for this project.
`timescale 1ns / 1ps
module amiga_ce #(
    parameter CATCHUP = 1                          // 0: the old behaviour (a blocked tick is lost), kept for the rate bench's negative control
) (
    input  wire       clk,
    input  wire [1:0] phase,
    input  wire       pend,
    input  wire       pause,                       // hold the machine (sys_clk)
    output reg        ce28 = 1'b1,
    output reg [31:0] st_hold = 32'd0,
    output reg [31:0] st_gap_max = 32'd0,          // the longest run of clocks without a tick (watchdog)
    output reg [31:0] st_debt = 32'd0              // {ticks lost to a full debt counter (16 bit, saturating), deepest debt in ticks (16 bit)}
);
    // Catch-up (T85): the accumulator below is real time; it runs on, also while the machine is held by a stall. A tick
    // that is due while the stall blocks it is not dropped but counted as debt, and the machine pays the debt back by ticking
    // in every clock (sys_clk = 10/9 of the nominal rate) until it is gone. Averaged over a few milliseconds the machine
    // keeps real time as long as the stalls take less than 10 % of the clocks; the stall is invisible to anything that
    // looks at it for longer than the debt lasts (audio, frames). A debt beyond DMAX ticks is dropped and counted (st_debt).
    // Pause (Freeze) holds the accumulator and the debt: the machine does not owe time it spent held on purpose.
    localparam DW = 12;
    reg [DW-1:0] debt = {DW{1'b0}};
    reg [DW-1:0] debt_max = {DW{1'b0}};
    reg [15:0]   lost = 16'd0;

    reg [3:0] acc = 4'd0;
    wire [4:0] sum  = {1'b0, acc} + 5'd9;
    wire       fire = (sum >= 5'd10);
    wire       block = pend & ((phase == 2'd0) | (phase == 2'd1) | ((phase == 2'd3) & ce28) | ((phase == 2'd0) & ce28));
    reg [31:0] gap = 32'd0;
    always @(posedge clk) begin
        if (ce28 || pause) gap <= 32'd0; else begin gap <= gap + 32'd1; if (gap + 32'd1 > st_gap_max) st_gap_max <= gap + 32'd1; end
        if (pause) begin
            ce28 <= 1'b0;
        end else begin
            acc <= fire ? sum[3:0] - 4'd10 : sum[3:0];
            if (block) begin
                ce28 <= 1'b0;
                st_hold <= st_hold + 32'd1;
                if (fire && CATCHUP) begin
                    if (&debt) begin if (!(&lost)) lost <= lost + 16'd1; end
                    else begin debt <= debt + 1'b1; if (debt + 1'b1 > debt_max) debt_max <= debt + 1'b1; end
                end
            end else begin
                ce28 <= fire | (CATCHUP && (|debt));
                if (!fire && (|debt)) debt <= debt - 1'b1;
            end
            st_debt <= {lost, 4'd0, debt_max};
        end
    end
endmodule
