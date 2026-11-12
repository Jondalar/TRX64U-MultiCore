// T116 D8 / P8: the Next's five related clocks as enables on the partition's 100 MHz clock (cpu_clk), for cores/zxnext/gen/zxnext.v
// (tools/zxnext_gen.py put an enable pair on every clock of the machine: X_cep = a rising edge of X is now, X_cen = a falling edge).
//
// The master clock is CLK_28 (the original's MMCM makes the others from it, zxnext_top_issue5.vhd:960-1052):
//   CLK_28    rises at every r28 pulse, falls two cpu_clk later (f28); CLK_28_n is its inverse (rises at f28, falls at r28)
//   CLK_14    edges at the r28 pulses: rises on every second one (r14), falls on the others (f14)
//   CLK_7     edges at the r28 pulses: rises at m == 0 (r7), falls at m == 2 (f7)
//   CLK_CPU   speed 3.5 MHz: the flop CLK_3M5_CONT of the original's top, clocked by CLK_7 (held high while the ULA contends: lsb = 1 and
//             contend = 1); 7 MHz: CLK_7; 14 MHz: CLK_14; 28 MHz: CLK_28 (the BUFGMUX tree of the original)
// A pulse is a registered net that is high for one cpu_clk; a flop of the machine changes in the cpu_clk after the pulse. r28 comes every 3
// or 4 cpu_clk (28 MHz / 100 MHz = 0.28), f28 two cpu_clk after it; so a half-cycle path of the machine (CLK_28 edge to CLK_28_n edge) has
// two cpu_clk and a full-cycle path three (cores/zxnext/core_timing.tcl).
//
// The NCO makes r28 from `step` (2^32 x master / 100 MHz). `hold` stops new master cycles at the next r28 (the f28 of a started cycle
// still follows): Freeze, a cache miss and the reset sequence use it; the machine stands exactly where it was and resumes there.
// PSG enable: the original's top derives it from a counter of CLK_28 (clk_28_div, 1/16): a flop that follows r28 here.
`timescale 1ns / 1ps
module zxn_clk (
    input  wire        clk,
    input  wire        rst,
    input  wire        hold,
    input  wire [31:0] step,
    input  wire [1:0]  cpu_speed,        // o_CPU_SPEED: 00 3.5, 01 7, 10 14, 11 28 MHz
    input  wire        cpu_lsb,          // o_CPU_CLK_LSB
    input  wire        cpu_contend,      // o_CPU_CONTEND
    output reg         r28 = 1'b0,
    output reg         f28 = 1'b0,
    output reg         r14 = 1'b0,
    output reg         f14 = 1'b0,
    output reg         r7 = 1'b0,
    output reg         f7 = 1'b0,
    output reg         cpu_r = 1'b0,
    output reg         cpu_f = 1'b0,
    output reg         psg_en = 1'b0,
    output wire        idle                // no master cycle is half done: the machine stands
);
    reg [31:0] acc = 32'd0;
    reg [1:0]  m = 2'd0;                  // index of the next r28 within the 7 MHz period
    reg        c35 = 1'b1;                // CLK_3M5_CONT
    reg        lvl = 1'b1;                // the CPU clock level
    reg [3:0]  cdiv = 4'd0;
    wire [32:0] acc_n = {1'b0, acc} + {1'b0, step};
    wire        t_r28 = acc_n[32] & ~hold & ~rst;
    // the CPU clock level after the r28 about to be issued (index m)
    wire        n_c35 = (m == 2'd0) ? ((cpu_lsb & ~cpu_contend) ? 1'b0 : (~cpu_lsb ? 1'b1 : c35)) : c35;
    wire        n_l7  = ~m[1];
    wire        n_l14 = ~m[0];
    wire        n_cpu = (cpu_speed == 2'd0) ? n_c35 : (cpu_speed == 2'd1) ? n_l7 : (cpu_speed == 2'd2) ? n_l14 : 1'b1;
    assign idle = ~r28 & ~f28;
    always @(posedge clk) begin
        acc <= acc_n[31:0];
        r28 <= t_r28;
        f28 <= r28;
        r14 <= t_r28 & ~m[0];
        f14 <= t_r28 &  m[0];
        r7  <= t_r28 & (m == 2'd0);
        f7  <= t_r28 & (m == 2'd2);
        cpu_r <= 1'b0;
        cpu_f <= 1'b0;
        if (t_r28) begin
            m <= m + 2'd1;
            c35 <= n_c35;
            lvl <= n_cpu;
            cpu_r <= n_cpu & ~lvl;
            cpu_f <= ~n_cpu & lvl;
        end else if (r28 && cpu_speed == 2'd3) begin          // CLK_28 falls: the CPU clock at 28 MHz with it
            lvl <= 1'b0;
            cpu_f <= lvl;
        end
        if (r28) begin
            cdiv <= cdiv + 4'd1;
            psg_en <= (cdiv == 4'hF);
        end
        if (rst) begin
            m <= 2'd0; c35 <= 1'b1; lvl <= 1'b1; cdiv <= 4'd0; psg_en <= 1'b0;
        end
    end
endmodule
