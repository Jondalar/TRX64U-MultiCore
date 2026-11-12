// ----------------------------------------------------------------------------
// This file is part of reDIP SID, a MOS 6581/8580 SID FPGA emulation platform.
// Copyright (C) 2022 - 2023  Dag Lem <resid@nimrod.no>
//
// This source describes Open Hardware and is licensed under the CERN-OHL-S v2.
//
// You may redistribute and modify this source and make products using it under
// the terms of the CERN-OHL-S v2 (https://ohwr.org/cern_ohl_s_v2.txt).
//
// This source is distributed WITHOUT ANY EXPRESS OR IMPLIED WARRANTY,
// INCLUDING OF MERCHANTABILITY, SATISFACTORY QUALITY AND FITNESS FOR A
// PARTICULAR PURPOSE. Please see the CERN-OHL-S v2 for applicable conditions.
//
// Source location: https://github.com/daglem/reDIP-SID
// ----------------------------------------------------------------------------

module sid_envelope #(
    // Number of SIDs; the envelope generator handles 3 voices for each.
    parameter NS = 2,
    localparam N = 3*NS,
    // Default to no init, since the counter will reach 0 after a maximum of
    // one exponential release cycle (at release = 0) after reset release.
    localparam INIT_ENV = 0,
    localparam ENV_INIT = INIT_ENV ? { 4{2'b10} } : 'hFF
)(
    input  logic               clk,
    input  sid::cycle_t        cycle,
    input  logic               res,
    input  sid::envelope_reg_t ereg_5,
    output sid::reg8_t         env
);

    // Initialization flag.
    logic primed = 0;

    // 8-bit envelope counter.
    // Odd bits are high on powerup. There is no immediate reset, however
    // env_cnt_en, rate_cnt_res, and exp_cnt_res are all set by reset.
    // This implies that, on the face of it, the counter will start out at
    // ~AA = 55, and will then count down in the release state while in reset,
    // wrapping around to FF each time 00 is reached. After reset release, the
    // counter will continue counting down until it finally reaches zero.
    // Note that the counter itself is FF at env=00, due to inversion in decay.
    typedef struct packed {
        sid::reg8_t  env_cnt;
        logic        prev_FF;
        logic        prev_00;
        logic        prev_gate;
        logic        attack;
        logic        sustain;
        sid::reg4_t  adr;
        logic        env_cnt_en;
        logic        env_cnt_up;
        logic        prev_env_cnt_up;
        logic        env_cnt_inv;
        // 15-bit rate counter.
        sid::reg15_t rate_cnt;
        logic        prev_rate_cnt_res;
        // 5-bit exponential decay counter.
        sid::reg5_t  exp_seg;
        sid::reg5_t  exp_cnt;
        logic        prev_exp_cnt_res;
    } envelope_t;

    // Ring of N envelope generators: e[N-1] is read and updated, the result
    // goes into e[0] and then moves on through the (delay only) positions
    // e[1] .. e[N-2], reaching e[N-1] again N cycles later.
    envelope_t e [N];
    initial begin for (int i = 0; i < N; i++) e[i] = '0; end

    logic        rise_gate;
    logic        rise_FF;
    logic        rise_00;
    sid::reg7_t  exp_step;
    logic        rate_cnt_res;
    logic        exp_cnt_res;

    always_comb begin
        // The counter output bits are inverted when the counter counts down.
        // In the real SID, the final result is latched by phi2. To save
        // storage, we instead invert the previous counter bits on the fly.
        // FIXME: The XOR with primed only works for INIT_ENV = 0.
        env = e[N-1].env_cnt ^ { 8{~e[N-1].prev_env_cnt_up ^ ~primed} };

        // Exponential decay curve segment steps.
        exp_step = {
            env == 8'h00,
            env == 8'h06,
            env == 8'h0E,
            env == 8'h1A,
            env == 8'h36,
            env == 8'h5D,
            env == 8'hFF
        };

        rise_gate = ~e[N-1].prev_gate & ereg_5.gate;
        rise_FF   = ~e[N-1].prev_FF & exp_step[0];
        rise_00   = ~e[N-1].prev_00 & exp_step[6];

`ifndef RIPPLE_COUNTERS
        // Envelope rate counter reset / count.
        rate_cnt_res =
                      (e[N-1].adr == 'h0 && e[N-1].rate_cnt == 'h7f00) ||
                      (e[N-1].adr == 'h1 && e[N-1].rate_cnt == 'h0006) ||
                      (e[N-1].adr == 'h2 && e[N-1].rate_cnt == 'h003c) ||
                      (e[N-1].adr == 'h3 && e[N-1].rate_cnt == 'h0330) ||
                      (e[N-1].adr == 'h4 && e[N-1].rate_cnt == 'h20c0) ||
                      (e[N-1].adr == 'h5 && e[N-1].rate_cnt == 'h6755) ||
                      (e[N-1].adr == 'h6 && e[N-1].rate_cnt == 'h3800) ||
                      (e[N-1].adr == 'h7 && e[N-1].rate_cnt == 'h500e) ||
                      (e[N-1].adr == 'h8 && e[N-1].rate_cnt == 'h1212) ||
                      (e[N-1].adr == 'h9 && e[N-1].rate_cnt == 'h0222) ||
                      (e[N-1].adr == 'hA && e[N-1].rate_cnt == 'h1848) ||
                      (e[N-1].adr == 'hB && e[N-1].rate_cnt == 'h59b8) ||
                      (e[N-1].adr == 'hC && e[N-1].rate_cnt == 'h3840) ||
                      (e[N-1].adr == 'hD && e[N-1].rate_cnt == 'h77e2) ||
                      (e[N-1].adr == 'hE && e[N-1].rate_cnt == 'h7625) ||
                      (e[N-1].adr == 'hF && e[N-1].rate_cnt == 'h0a93) ||
                      res;

        // Exponential counter reset / count.
        exp_cnt_res =
                     // Exponential decay.
                     (e[N-1].exp_seg[0] && e[N-1].exp_cnt == 'h1c) ||
                     (e[N-1].exp_seg[1] && e[N-1].exp_cnt == 'h11) ||
                     (e[N-1].exp_seg[2] && e[N-1].exp_cnt == 'h1b) ||
                     (e[N-1].exp_seg[3] && e[N-1].exp_cnt == 'h08) ||
                     (e[N-1].exp_seg[4] && e[N-1].exp_cnt == 'h0f) ||
                     // No exponential decay.
                     // Note that in this FPGA cycle we cannot use
                     // e[N-1].attack, which is precalculated for the next
                     // SID cycle. e[N-1].env_cnt_up = attack for this cycle.
                     (e[N-1].prev_rate_cnt_res && (e[N-1].env_cnt_up || e[N-1].exp_seg == '0)) ||
                     res;
`else
        // Envelope rate counter reset / count.
        rate_cnt_res =
                      (e[N-1].adr == 'h0 && e[N-1].rate_cnt ==     8) ||
                      (e[N-1].adr == 'h1 && e[N-1].rate_cnt ==    31) ||
                      (e[N-1].adr == 'h2 && e[N-1].rate_cnt ==    62) ||
                      (e[N-1].adr == 'h3 && e[N-1].rate_cnt ==    94) ||
                      (e[N-1].adr == 'h4 && e[N-1].rate_cnt ==   148) ||
                      (e[N-1].adr == 'h5 && e[N-1].rate_cnt ==   219) ||
                      (e[N-1].adr == 'h6 && e[N-1].rate_cnt ==   266) ||
                      (e[N-1].adr == 'h7 && e[N-1].rate_cnt ==   312) ||
                      (e[N-1].adr == 'h8 && e[N-1].rate_cnt ==   391) ||
                      (e[N-1].adr == 'h9 && e[N-1].rate_cnt ==   976) ||
                      (e[N-1].adr == 'hA && e[N-1].rate_cnt ==  1953) ||
                      (e[N-1].adr == 'hB && e[N-1].rate_cnt ==  3125) ||
                      (e[N-1].adr == 'hC && e[N-1].rate_cnt ==  3906) ||
                      (e[N-1].adr == 'hD && e[N-1].rate_cnt == 11719) ||
                      (e[N-1].adr == 'hE && e[N-1].rate_cnt == 19531) ||
                      (e[N-1].adr == 'hF && e[N-1].rate_cnt == 31250) ||
                      res;

        // Exponential counter reset / count.
        exp_cnt_res =
                     // Exponential decay.
                     (e[N-1].exp_seg[0] && e[N-1].exp_cnt ==  2) ||
                     (e[N-1].exp_seg[1] && e[N-1].exp_cnt ==  4) ||
                     (e[N-1].exp_seg[2] && e[N-1].exp_cnt ==  8) ||
                     (e[N-1].exp_seg[3] && e[N-1].exp_cnt == 16) ||
                     (e[N-1].exp_seg[4] && e[N-1].exp_cnt == 30) ||
                     // No exponential decay.
                     // Note that in this FPGA cycle we cannot use
                     // e[N-1].attack, which is precalculated for the next
                     // SID cycle. e[N-1].env_cnt_up = attack for this cycle.
                     (e[N-1].prev_rate_cnt_res && (e[N-1].env_cnt_up || e[N-1].exp_seg == '0)) ||
                     res;
`endif
    end

    always_ff @(posedge clk) begin
        // Update counters.
        if (cycle >= 6 && cycle <= N + 5) begin
            for (int i = 1; i < N; i++) begin
                e[i] <= e[i-1];
            end
            e[0] <= e[N-1];

            // Counter control logic.

            // In the real SID, gate, FF, and 00 are latched by phi1, phi2,
            // and are thus first available at the next cycle. Since the
            // combinations of the signals don't depend on any other signals
            // from the next cycle except for reset, they can be precalculated
            // and stored for the next cycle.
            e[0].prev_gate <= ereg_5.gate;
            e[0].prev_FF   <= exp_step[0];
            e[0].prev_00   <= exp_step[6];

            // The counter starts counting when the gate goes high or on reset,
            // and stops counting once it has reached zero.
            if (rise_gate | res) begin
                e[0].env_cnt_en <= 1;
            end else if (rise_00) begin
                e[0].env_cnt_en <= 0;
            end

            // The counter starts counting down after it has reached FF (decay),
            // or if the gate is low (release).
            // The counter starts counting up once the gate goes high (attack).
            if (rise_FF | ~ereg_5.gate) begin
                e[0].attack <= 0;
            end else if (rise_gate) begin
                e[0].attack <= 1;
            end

            // Counting direction is delayed by another cycle.
            e[0].env_cnt_up       <= e[N-1].attack;
            e[0].prev_env_cnt_up  <= e[N-1].env_cnt_up;
            // Invert counter bits on change of counting direction.
            // Yet another cycle delay.
            e[0].env_cnt_inv <= e[N-1].env_cnt_up ^ e[N-1].attack;

            // Both the high and low 4 bits of the envelope counter are compared
            // with the 4-bit sustain value.
            e[0].sustain <= (env == { ereg_5.sustain, ereg_5.sustain }) & ereg_5.gate & ~e[N-1].attack;

            // The counter counts up every time exp_cnt_res is set, as long as
            // the counter is not frozen at zero, and we're not in the sustain state.
            // In the real SID, the inversion and counter latch is done on different
            // clock phases.
            // The counter bits are inverted when the counting direction changes.
            // The count is delayed by yet another cycle.
            e[0].env_cnt <= primed ?
                          (e[N-1].env_cnt ^ { 8{e[N-1].env_cnt_inv} }) + { 7'b0, e[N-1].env_cnt_en & e[N-1].prev_exp_cnt_res & ~e[N-1].sustain } :
                          ENV_INIT;

`ifndef RIPPLE_COUNTERS
            // LFSR counters were used in the real SID, see
            // https://github.com/libsidplayfp/SID_schematics/wiki/Envelope-Overview

            if (rate_cnt_res || !primed) begin
                e[0].rate_cnt <= '1;
            end else begin
                e[0].rate_cnt <= { e[N-1].rate_cnt[13:0], e[N-1].rate_cnt[14] ^ e[N-1].rate_cnt[13] };
            end

            if (exp_cnt_res || !primed) begin
                e[0].exp_cnt <= '1;
            end else if (e[N-1].prev_rate_cnt_res) begin
                e[0].exp_cnt <= { e[N-1].exp_cnt[3:0], e[N-1].exp_cnt[4] ^ e[N-1].exp_cnt[2] };
            end
`else
            // LFSR counters were used in the real SID, however ripple counters
            // don't necessarily use any more resources thanks to FPGA carry
            // logic, and they also simplify testing.

            // The period of the LFSR15 is 2^15 - 1; wrap counter around at 2^15 - 2.
            if (rate_cnt_res || e[N-1].rate_cnt == 'h7FFE) begin
                e[0].rate_cnt <= 0;
            end else begin
                e[0].rate_cnt <= e[N-1].rate_cnt + 1;
            end

            // The period of the LFSR5 is 2^5 - 1; wrap counter around at 2^5 - 2.
            // Note that this check is redundant, since the counter can only
            // reach this value (30) when exp_seg[4] is set (famous last words).
            if (exp_cnt_res || e[N-1].exp_cnt == 'h1E) begin
                e[0].exp_cnt <= 0;
            end else if (e[N-1].prev_rate_cnt_res) begin
                e[0].exp_cnt <= e[N-1].exp_cnt + 1;
            end
`endif
            // Latch reset signals for next cycle.
            e[0].prev_rate_cnt_res <= rate_cnt_res;
            e[0].prev_exp_cnt_res  <= exp_cnt_res;

            // Set/reset selectors for exponential curve segments.
            // In the real SID, the selectors are both latched and input to
            // combinational logic for reset of the exponential counter at the
            // same phi1 cycle. However since the reset signal is latched by
            // phi2, we can postpone the use to the next cycle.
            for (int i = 0; i < $bits(e[0].exp_seg); i++) begin : seg
                if (exp_step[i] | exp_step[i + 2] | res) begin
                    e[0].exp_seg[i] <= 0;
                end else if (exp_step[i + 1]) begin
                    e[0].exp_seg[i] <= 1;
                end
            end

            // Multiplexer for rate period index (attack / decay / release).
            // FIXME: Yosys doesn't understand case (expr) inside.
            unique casez ({ ereg_5.gate, e[N-1].attack })
              2'b?1: e[0].adr <= ereg_5.attack;
              2'b10: e[0].adr <= ereg_5.decay;
              2'b00: e[0].adr <= ereg_5.release_;
            endcase
        end
    end

    always_ff @(posedge clk) begin
        if (cycle == N + 5 && !primed) begin
            // All counters and LFSRs are initialized.
            primed <= 1;
        end
    end
endmodule
