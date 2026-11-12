// T85: Paula's two 15 bit channels to the host's 24 bit audio, through a FIFO with rate adaption.
// Why: the elastic stall of amiga_mem stops the machine (ce28) while a chip bus request waits for the DDR2, so Paula makes fewer
// samples per second of wall time than the host takes (48 kHz, `sample_en`, mclk/512). Without a buffer every stall is a gap in
// the sound. Here Paula's sums are taken in MACHINE time (one pair every 28.37516 MHz / 48 kHz ce28 ticks, a phase accumulator
// that only runs on ce28), written into a 1024 pair block RAM (sys_clk), and read in WALL time at every `sample_en` (mclk) by
// a linear interpolating resampler whose step is 1 +- a small amount that follows the fill level:
//   step = 1 + clamp(GAIN * (fill_filtered - TARGET) / 65536), |clamp| <= MAXDEV/65536 (2 %), GAIN = 3,
// fill_filtered = a one pole average of the fill (1/512 per frame, about 11 ms), so a burst moves the pitch only slowly.
// Fill below TARGET (the machine is behind) -> the step falls below 1 and the sound plays slightly slower; above -> faster.
// Underflow (fewer than two pairs): the read side stops and the output runs down to zero like a mute (a slew, no click), waits
// until PRIME pairs are in and starts again with the same slew up. Overflow (more than OVF pairs): the read pointer jumps to TARGET.
// mute (Freeze, T85): the read side stands still (the FIFO keeps its pairs: Freeze stops the writes too, so the sound goes on
// where it stopped), the output runs down to zero in a few milliseconds and stays there, and comes back the same way.
// Nominal fill TARGET = 480 pairs at 48 kHz = 10.0 ms is the latency (Alex, 2026-10-18: "make it 10 ms"); the RAM holds 1024
// pairs (21.3 ms). Underflow = fill < 2 pairs, overflow = fill >= OVF = TARGET + 192 pairs (4 ms above the nominal fill).
// KPIs for the application (amiga_core -> amiga_regs STAT_SEL 8..10): st_under, st_over, st_fill, and the lowest / highest
// fill seen while playing since `clr` (the machine's HOLD). Written for this project.
`timescale 1ns / 1ps
module amiga_audio #(
    parameter [24:0] CE_HZ  = 25'd28375160,             // ce28 ticks per second (sys_clk x 9/10)
    parameter [15:0] OUT_HZ = 16'd48000,
    parameter integer TARGET = 480,
    parameter integer PRIME  = 480,
    parameter integer OVF    = 672,
    parameter integer GAIN   = 3,
    parameter integer FSH    = 9,                      // the fill average: 1/2^FSH per frame
    parameter integer MAXDEV = 1311                     // 2 % of 65536
) (
    input  wire               sys_clk,
    input  wire               ce28,                     // the machine's tick (sys_clk): time stands still without it
    input  wire               mclk,
    input  wire               sample_en,                // mclk, once per host frame (48 kHz)
    input  wire signed [14:0] left,                     // sys_clk, changes with ce28
    input  wire signed [14:0] right,
    input  wire               mute,                     // sys_clk level, crossed here
    input  wire               clr,                      // sys_clk level (HOLD): clears the min / max fill, crossed here
    output reg  signed [23:0] audio_l = 24'sd0,
    output reg  signed [23:0] audio_r = 24'sd0,
    output reg  [15:0]        st_under = 16'd0,         // underflows
    output reg  [15:0]        st_over = 16'd0,          // overflows
    output wire [10:0]        st_fill,                  // pairs in the FIFO (mclk)
    output reg  [10:0]        st_fmin = 11'h7FF,        // lowest fill while playing since clr (mclk)
    output reg  [10:0]        st_fmax = 11'd0           // highest fill while playing since clr
);
    // ------------------------------------------------------------ write side: machine time, sys_clk
    (* ram_style = "block" *) reg [29:0] mem [0:1023];
    reg [24:0] wacc = 25'd0;
    reg [10:0] wptr = 11'd0, wgray = 11'd0;
    wire [24:0] wsum  = wacc + {9'd0, OUT_HZ};
    wire        wtick = ce28 && (wsum >= CE_HZ);
    wire [10:0] wnext = wptr + 11'd1;
    always @(posedge sys_clk) if (ce28) begin
        if (wtick) begin
            wacc <= wsum - CE_HZ;
            mem[wptr[9:0]] <= {left, right};
            wptr <= wnext;
            wgray <= wnext ^ (wnext >> 1);
        end else wacc <= wsum;
    end

    // ------------------------------------------------------------ read side: wall time, mclk
    (* ASYNC_REG = "TRUE" *) reg [10:0] wg0 = 11'd0, wg1 = 11'd0;
    (* ASYNC_REG = "TRUE" *) reg [1:0] mu = 2'b00;
    (* ASYNC_REG = "TRUE" *) reg [1:0] cs = 2'b00;
    function [10:0] g2b(input [10:0] g);
        integer i;
        begin g2b[10] = g[10]; for (i = 9; i >= 0; i = i - 1) g2b[i] = g2b[i+1] ^ g[i]; end
    endfunction
    wire [10:0] wbin = g2b(wg1);
    reg  [10:0] rptr = 11'd0;
    wire [10:0] fill = wbin - rptr;
    assign st_fill = fill;

    reg [29:0] rd = 30'd0;
    reg [9:0]  ra = 10'd0;
    always @(posedge mclk) rd <= mem[ra];

    reg        run = 1'b0;
    reg [15:0] ph = 16'd0;                              // fraction between s0 and s1
    reg [24:0] fa = 25'd0;                              // filtered fill, 14 fractional bits
    reg [3:0]  st = 4'd0;                               // sequencer: 0 idle, 1.. one frame's work
    reg signed [14:0] s0l = 0, s0r = 0, s1l = 0, s1r = 0;
    reg signed [15:0] opd = 0;
    reg signed [40:0] pr = 0;
    reg signed [23:0] tl = 24'sd0, tr = 24'sd0;         // the sample the machine plays
    reg [16:0] inc = 17'd65536;
    reg [10:0] fl = 11'd0;

    wire signed [11:0] err  = $signed({1'b0, fa[24:14]}) - TARGET;
    wire signed [14:0] dev0 = err * GAIN;                           // GAIN / 65536 of rate per pair of error
    wire signed [14:0] dev  = (dev0 > MAXDEV) ? MAXDEV : (dev0 < -MAXDEV) ? -MAXDEV : dev0;
    wire [17:0] phn = {2'b00, ph} + {1'b0, inc};                    // the phase after this frame: 1 or 2 pairs on at most
    wire signed [26:0] fd = $signed({1'b0, fl, 14'd0}) - $signed({1'b0, fa});
    wire signed [26:0] fds = fd >>> FSH;                            // arithmetic shift in its own signed wire (inside `fa + (fd >>> FSH)` the unsigned fa made the shift logical)

    wire mq = mu[1] | ~run;                                          // muted by Freeze, or nothing to play (underflow, start)
    reg  [8:0] gain = 9'd0;                                          // 0..256, one step per frame: 5.3 ms from silence to full
    // one multiplier for all four products: the interpolation (difference x phase) and the gain (sample x gain)
    wire signed [23:0] ma = (st == 4'd9) ? tl : (st == 4'd10) ? tr : {{8{opd[15]}}, opd};
    wire signed [16:0] mb = (st == 4'd9 || st == 4'd10) ? $signed({8'd0, gain}) : $signed({1'b0, ph});
    always @(posedge mclk) begin
        wg0 <= wgray; wg1 <= wg0;
        mu <= {mu[0], mute};
        cs <= {cs[0], clr};
        if (cs[1]) begin st_fmin <= 11'h7FF; st_fmax <= 11'd0; end
        else if (st == 4'd1 && run && !mu[1]) begin
            if (fill < st_fmin) st_fmin <= fill;
            if (fill > st_fmax) st_fmax <= fill;
        end
        // ---- one frame, about 14 clocks after sample_en: the resampler (it stands still while muted), then the gain
        case (st)
            4'd0: if (sample_en) st <= 4'd1;
            4'd1: if (mu[1]) st <= 4'd9;
            else begin
                fl <= fill;
                inc <= 17'd65536 + {{2{dev[14]}}, dev};
                if (fill >= OVF) begin                       // far too full: back to the nominal fill
                    rptr <= wbin - TARGET; ph <= 16'd0; st_over <= st_over + 16'd1; st <= 4'd9;
                end else if (!run) begin
                    if (fill >= PRIME) begin run <= 1'b1; ph <= 16'd0; fa <= {fill, 14'd0}; ra <= rptr[9:0]; st <= 4'd2; end
                    else st <= 4'd9;
                end else if (fill < 11'd2) begin             // underflow: hold the last sample
                    run <= 1'b0; st_under <= st_under + 16'd1; st <= 4'd9;
                end else begin ra <= rptr[9:0]; st <= 4'd2; end
            end
            4'd2: begin ra <= rptr[9:0] + 10'd1; st <= 4'd3; end
            4'd3: begin s0l <= rd[29:15]; s0r <= rd[14:0]; st <= 4'd4; end
            4'd4: begin s1l <= rd[29:15]; s1r <= rd[14:0]; st <= 4'd5; end
            4'd5: begin opd <= s1l - s0l; st <= 4'd6; end
            4'd6: begin pr <= ma * mb; opd <= s1r - s0r; st <= 4'd7; end
            4'd7: begin
                tl <= $signed({s0l, 8'd0}) + (pr >>> 8);
                pr <= ma * mb;
                st <= 4'd8;
            end
            4'd8: begin
                tr <= $signed({s0r, 8'd0}) + (pr >>> 8);
                fa <= fa + fds[24:0];
                if (phn[17:16] >= fl) begin                  // would run past the newest pair
                    rptr <= rptr + fl - 11'd1; ph <= 16'd0; run <= 1'b0; st_under <= st_under + 16'd1;
                end else begin
                    rptr <= rptr + {9'd0, phn[17:16]}; ph <= phn[15:0];
                end
                st <= 4'd9;
            end
            4'd9: begin
                gain <= mq ? (gain - {8'd0, gain != 9'd0}) : (gain + {8'd0, gain != 9'd256});
                pr <= ma * mb; st <= 4'd10;
            end
            4'd10: begin audio_l <= pr >>> 8; pr <= ma * mb; st <= 4'd11; end
            4'd11: begin audio_r <= pr >>> 8; st <= 4'd0; end
            default: st <= 4'd0;
        endcase
    end
endmodule
