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

// ./waveform-sop.pl waveforms/6581wf70.dat
// ./waveform-sop.pl waveforms/8580wf70.dat
// ./waveform-sop.pl waveforms/6581wf30.dat
// ./waveform-sop.pl waveforms/8580wf30.dat
`include "sid_waveform_PST.svh"
`include "sid_waveform__ST.svh"

module sid_waveform #(
    // Number of SIDs; the waveform generator handles 3 voices for each.
    parameter NS = 2,
    // 0: all SIDs use variant A of the 6581 combined waveforms, and the
    // tables of variant B are not built.
    parameter VARIANT_B = 1,
    // The final mix-in of the 8580 sawtooth / triangle (second rotation) reads
    // the pulse ring after its rotation has stopped for the last three voices
    // of a six voice pipeline, which then see the pulse of the third voice
    // instead of their own. 0: reproduce this (for each group of two SIDs, bit
    // exact with the six voice pipeline), 1: each voice reads its own pulse.
    parameter PULSE_FIX = 0,
    localparam N = 3*NS,
    // Even bits are high on powerup, and there is no reset.
    localparam OSC_INIT       = { 12{2'b01} },
    // Time for noise LFSR to be filled with 1 bits when reset or test is held.
    localparam NOISE_TTL_6581 = 14'd33,   // ~30ms
    localparam NOISE_TTL_8580 = 14'd9765, // ~10s
    // Time for waveform 0 to fade out.
    localparam WF_0_TTL_6581  = 13'd200,  // ~200ms
    localparam WF_0_TTL_8580  = 13'd5000  // ~5s
)(
    input  logic          clk,
    input  logic          tick_ms,
    input  sid::cycle_t   cycle,
    input  logic          res,
    input  logic [NS-1:0] model,
    input  logic [NS-1:0] variant,
    input  sid::freq_pw_t freq_pw_1,
    input  sid::control_t control_4,
    input logic [2:0]     test,
    input logic [2:0]     sync,
    output sid::reg12_t   wav
);

    // Initialization flag.
    logic primed = 0;

    // Keep track of the current SID model for cycles 5 - N+4 and 6 - N+5
    // (and the second rotation, 2N cycles in all).
    logic model_4;
    logic model_5 = 0;

    // Keep track of the current SID variant for cycles 5 - N+4 and 6 - N+5.
    logic variant_4;
    logic variant_5 = 0;

    // Waveform selector for cycle 6 - N+5.
    sid::reg4_t waveform_5 = 0;

    // Waveform selector of the second rotation, from the ring of previous
    // selectors (wp_waveform below).
    sid::reg4_t wf_this_4;

    localparam logic [64*3-1:0] SID_TAB = sid::sid_next_tab(5, N);
    localparam logic [63:0] SYNC_TAB = sid::mod3_tab(4);
    localparam logic [63:0] RING_TAB = sid::mod3_tab(5);
    logic [2:0] sid_4 = 0;

    // registered: the SID of the next cycle
    always_ff @(posedge clk) begin
        sid_4 <= SID_TAB[cycle*3 +: 3];
    end

    always_comb begin
        model_4   = model[sid_4];
        variant_4 = VARIANT_B ? variant[sid_4] : 1'b0;
    end

    always_ff @(posedge clk) begin
        if (cycle >= 5 && cycle <= 2*N + 4) begin
            model_5    <= model_4;
            variant_5  <= variant_4;
            // The second rotation takes the selector from the ring of previous
            // selectors, which holds this pass's value by then; the control
            // registers need not rotate for a third time.
            waveform_5 <= (cycle >= N + 5) ? wf_this_4 :
                          { control_4.noise, control_4.pulse, control_4.sawtooth, control_4.triangle };
        end
    end

    // ------------------------------------------------------------------------
    // Oscillators
    // ------------------------------------------------------------------------
    // Phase-accumulating oscillators.
    // Ring of N oscillators: o[N-1] is updated, the result goes into o[0] and
    // moves on to o[1] .. o[4], which are tapped; o[5] .. o[N-2] only delay.
    sid::reg24_t o [N];
    initial begin for (int i = 0; i < N; i++) o[i] = '0; end
    sid::reg24_t osc_next;
    // Inter-oscillator synchronization.
    logic        o5_msb_up;
    logic        o0_msb_up = 0, o1_msb_up = 0;
    logic        o5_synced, o0_synced, o1_synced;
    logic        o5_reset,  o0_reset,  o1_reset;
    // Latched signals for noise and pulse.
    logic [N-1:0] osc19_prev    = '0;
    logic [N-1:0] osc19_prev_up = '0;
    logic [N-1:0] pulse         = '0;
    logic [N-1:0] pulse_prev    = '0;
    logic        pulse_next;

    // Ring position of the pulse bit that the waveform of the voice at
    // `cycle` (cycle 6 + j, or N + 6 + j in the second rotation) is mixed
    // with. The pulse ring has made min(cycle - 2, 2N) shifts by then; a voice
    // m is in position 0 after shift m + 1. Position 3 for every voice that
    // is read while the ring is rotating.
    function automatic logic [64*5-1:0] pulse_taps();
        int s, j, m, p;
        logic [64*5-1:0] t;
        t = '0;
        for (int c = 0; c < 64; c++) begin
            // entry c is the tap for cycle c + 1 (the index is registered)
            s = (c + 1 - 2 > 2*N) ? 2*N : c + 1 - 2;
            j = c + 1 - 6;
            m = j;
            if (j >= N) begin
                j = j - N;
                m = j;
                if (!PULSE_FIX && j % 6 >= 3) begin
                    m = (j / 6) * 6 + 2;
                end
            end
            p = s - m - 1;
            p = ((p % N) + N) % N;
            t[c*5 +: 5] = 5'(p);
        end
        pulse_taps = t;
    endfunction

    localparam logic [64*5-1:0] PULSE_TAP = pulse_taps();
    logic [4:0] ptap = 5'd3;
    logic       pulse_cur;

    always_ff @(posedge clk) begin
        ptap <= PULSE_TAP[cycle*5 +: 5];
    end

    always_comb begin
        pulse_cur = pulse[ptap];
    end

    always_comb begin
        // Constant value / ripple carry add can in theory be combined in single iCE40 LCs.
        osc_next  = primed ? o[N-1] + { 8'b0, freq_pw_1.freq_hi, freq_pw_1.freq_lo } : OSC_INIT;
        o5_msb_up = ~o[N-1][23] & osc_next[23];

        // A sync source will normally sync its destination when the MSB of the
        // sync source rises. However if the sync source is itself synced on the
        // same cycle, its MSB is zeroed, and the destination will not be synced.
        // In the real SID, the sync dependencies form a circular combinational
        // circuit, which takes the form of a ring oscillator when all three
        // oscillators are synced on the same cycle. Here, like in reSID, no
        // oscillator will be synced in this special case of the special case.
        //
        // We sync all oscillators as soon as possible, in order to get
        // correct msbs for ring modulation.
        //
        // At cycle 4, 7, ... (SID 1, SID 2, ...):
        // voice 1 osc = o1
        // voice 2 osc = o0
        // voice 3 osc = o5
        //
        //  ---------<----------      ---------<----------
        // |                    | =  |                    |
        //  -> v1 -> v2 -> v3 ->      -> o1 -> o0 -> o5 ->
        //
        o5_synced = test[0] || (sync[0] && o0_msb_up);
        o0_synced = test[1] || (sync[1] && o1_msb_up);
        o1_synced = test[2] || (sync[2] && o5_msb_up);

        o5_reset = test[0] || (sync[0] && o0_msb_up && !o0_synced);
        o0_reset = test[1] || (sync[1] && o1_msb_up && !o1_synced);
        o1_reset = test[2] || (sync[2] && o5_msb_up && !o5_synced);

        // The pulse width comparison is done at phi2, before the oscillator
        // is updated at phi1. Thus the pulse waveform is delayed by one cycle
        // with respect to the oscillator.
        pulse_next = (o[N-1][23-:12] >= { freq_pw_1.pw_hi[3:0], freq_pw_1.pw_lo }) | test[0];
    end

    always_ff @(posedge clk) begin
        // Rotation on cycles 2 - N+1 for update of oscillators.
        // Rotation on N additional cycles for oscillator synchronization and
        // writeback of the oscillator MSB, and to keep other processes in sync.
        if (cycle >= 2 && cycle <= 2*N + 1) begin
            for (int i = 1; i < N; i++) begin
                o[i] <= o[i-1];
            end
            o[0] <= o[N-1];

            osc19_prev    <= { osc19_prev[N-2:0],    osc19_prev[N-1] };
            osc19_prev_up <= { osc19_prev_up[N-2:0], osc19_prev_up[N-1] };
            pulse         <= { pulse[N-2:0],         pulse[N-1] };
            pulse_prev    <= { pulse_prev[N-2:0],    pulse_prev[N-1] };

            if (cycle >= 2 && cycle <= N + 1) begin
                // Update oscillators.
                // In the real SID, an intermediate sum is latched by phi2, and this
                // sum is simultaneously synced, output to waveform generation bits,
                // and written back to osc on phi1.
                o[0] <= osc_next;
                { o1_msb_up, o0_msb_up } <= { o0_msb_up, o5_msb_up };

                // Save previous rise of OSC bit 19 for generation of the noise waveform.
                // OSC bit 19 is read before the update of OSC at phi1, i.e.
                // it's delayed by one cycle.
                osc19_prev[0]    <= o[N-1][19];
                osc19_prev_up[0] <= ~osc19_prev[N-1] & o[N-1][19];

                // Pulse.
                pulse[0]      <= pulse_next;
                pulse_prev[0] <= pulse[N-1];
            end

            if (cycle >= 4 && cycle <= N + 1 && SYNC_TAB[cycle]) begin
                // Reset of oscillators by test bit or synchronization.
                if (o5_reset) begin
                    o[0] <= 0;
                end

                if (o0_reset) begin
                    o[1] <= 0;
                end

                if (o1_reset) begin
                    o[2] <= 0;
                end
            end

            if (cycle >= 6 && cycle <= N + 5) begin
                // Writeback to next cycle add for the 6581 oscillator MSB via
                // the sawtooth waveform selector.
                if (model_5 == sid::MOS6581 && waveform_5[1]) begin
                    o[4][23] <= wav[11];
                end
            end
        end
    end

    // ------------------------------------------------------------------------
    // Waveform generators
    // ------------------------------------------------------------------------

    // Waveform selector and waveform output from the previous cycle, with
    // aging for waveform 0.
    // FIXME: Yosys doesn't support multidimensional packed arrays outside
    // of structs, nor arrays of structs.
    // Rings of N entries, [N-1] is the entry of the previous cycle for the
    // voice at hand, [0] is written.
    sid::reg4_t  wp_waveform [N];
    initial begin for (int i = 0; i < N; i++) wp_waveform[i] = '0; end
    sid::reg12_t wp_wav [N];
    initial begin for (int i = 0; i < N; i++) wp_wav[i] = '0; end
    logic [12:0] wp_age [N];
    initial begin for (int i = 0; i < N; i++) wp_age[i] = '0; end
    logic        wav_prev_faded;

    // Noise.
    typedef struct packed {
        sid::reg23_t lfsr;
        logic [13:0] age;
        logic        nres_prev;
        logic        nclk_prev;
    } noise_t;

    // Ring of N noise generators, n[N-1] is read and updated, the result
    // goes into n[0].
    noise_t     n [N];
    initial begin for (int i = 0; i < N; i++) n[i] = '0; end
    logic       nres;
    logic       nclk;
    logic       nclk_prev = 0;
    logic       nset;
    sid::reg8_t noise;
    sid::reg8_t noise_lsl;
    sid::reg8_t noise_lsl_writeback;

    // Noise.
    always_comb begin
        // Noise LFSR reset and clock.
        nres = res | control_4.test;
        nclk = ~(nres | osc19_prev_up[2]);
        nset = (n[N-1].age == ((model_4 == sid::MOS6581) ?
                               NOISE_TTL_6581 :
                               NOISE_TTL_8580));

        // Noise bits are taken out from the noise LFSR.
        noise     = { n[0].lfsr[20], n[0].lfsr[18], n[0].lfsr[14], n[0].lfsr[11],
                      n[0].lfsr[9], n[0].lfsr[5], n[0].lfsr[2], n[0].lfsr[0] };
        noise_lsl = { n[0].lfsr[21], n[0].lfsr[19], n[0].lfsr[15], n[0].lfsr[12],
                      n[0].lfsr[10], n[0].lfsr[6], n[0].lfsr[3], n[0].lfsr[1] };
    end

    always_ff @(posedge clk) begin
        // Rotation on cycles 5 - N+4 for update of the noise LFSR.
        // Rotation on N additional cycles for writeback from combined
        // waveforms.
        if (cycle >= 5 && cycle <= 2*N + 4) begin
            for (int i = 1; i < N; i++) begin
                n[i] <= n[i-1];
            end
            n[0] <= n[N-1];

            if (cycle >= 5 && cycle <= N + 4) begin
                // The noise LFSR is clocked after OSC bit 19 goes high, or when
                // reset or test is released.
                // The LFSR stays at 'h7ffffe after reset (clocked on reset release).
                n[0].nres_prev <= nres;
                n[0].nclk_prev <= nclk;
                nclk_prev      <= n[N-1].nclk_prev;

                if (~nclk || !primed) begin
                    // LFSR shift phase 1.
                    if (nset || !primed) begin
                        // Reset LFSR.
                        // Note that the test bit in combination with combined
                        // waveforms should probably yield a different result, since
                        // zero bits can enter the LFSR.
                        n[0].lfsr <= '1;
                    end else begin
                        n[0].age <= n[N-1].age + { 13'b0, tick_ms };
                    end
                end else begin
                    n[0].age <= 0;

                    if (~n[N-1].nclk_prev & nclk) begin
                        // LFSR shift phase 2.
                        // Completion of the shift is delayed by 2 cycles after OSC
                        // bit 19 goes high.
                        n[0].lfsr <= { n[N-1].lfsr[21:0], (n[N-1].nres_prev | n[N-1].lfsr[22]) ^ n[N-1].lfsr[17] };
                    end
                end
            end

            // Writeback to noise LFSR from combined waveforms.
            if (cycle >= 6 && cycle <= N + 5) begin
                if (n[0].nclk_prev) begin
                    // Shift phase != 1 - writeback to current bits.
                    if (waveform_5 > 'h8) begin
                        { n[1].lfsr[20], n[1].lfsr[18], n[1].lfsr[14], n[1].lfsr[11],
                          n[1].lfsr[9], n[1].lfsr[5], n[1].lfsr[2], n[1].lfsr[0] } <=
                        wav[11-:8];
                    end

                    // Shift phase 1 -> 2: Writeback to shifted bits.
                    // The transition from shift phase 1 to phase 2 coincides
                    // with the transition of waveforms and waveform selectors
                    // at the rising edge of phi1. This implies that in theory,
                    // both the previous and current waveforms and waveform
                    // selectors can influence the result. The 8580 sawtooth
                    // and triangle waveforms are special since they are
                    // latched by phi2 at the end of the previous cycle,
                    // i.e. the 8580 sawtooth and triangle waveforms are
                    // constant during the transition. The sawtooth and
                    // triangle waveform selectors can always transition,
                    // however.
                    // FIXME: The check for the pulse waveform is adapted from
                    // VICE in order to pass more tests, and should probably be
                    // replaced with a generic model for bit zeroing (see other
                    // FIXME below).
                    if (~nclk_prev &&
                        wp_waveform[N-1] > 'h8 &&         // Combined waveforms in the previous cycle.
                        !(wp_waveform[N-1] == 'hc &&      // Not only noise + pulse high in the
                          pulse_prev[3]) &&               // previous cycle.
                        waveform_5 > 'h8)                 // Combined waveforms in the current cycle.
                    begin
                        { n[1].lfsr[21], n[1].lfsr[19], n[1].lfsr[15], n[1].lfsr[12],
                          n[1].lfsr[10], n[1].lfsr[6], n[1].lfsr[3], n[1].lfsr[1] } <=
                        noise_lsl_writeback;
                    end
                end
            end
        end
    end

    // Pulse is generated earlier.

    // Sawtooth / triangle.
    // Ring of N latched sawtooth / triangle values: st[0] is written, st[N-1]
    // is read.
    sid::reg12_t st [N];
    initial begin for (int i = 0; i < N; i++) st[i] = '0; end
    logic        o2_msb_i;  // For ring modulation.
    logic        o2_tri_xor;
    sid::reg12_t saw_tri_next;
    sid::reg12_t saw_tri;
    sid::reg12_t saw_tri_5 = 0;

    always_comb begin
        // The sawtooth and triangle waveforms are constructed from the
        // upper 12 bits of the oscillator. When sawtooth is not selected,
        // and the MSB is high, the lower 11 of these 12 bits are
        // inverted. When triangle is selected, the lower 11 bits are
        // shifted up to produce the final output. The MSB may be modulated
        // by the preceding oscillator for ring modulation.
        // The first voice of each SID takes the MSB from the third voice, which
        // has already been updated (cycle 5, 8, ...).
        o2_msb_i   = RING_TAB[cycle] ? o[0][23] : o[3][23];
        o2_tri_xor = ~control_4.sawtooth & ((control_4.ring_mod & ~o2_msb_i) ^ o[2][23]);

        // In the 8580, sawtooth / triangle is latched by phi2, and is thus
        // delayed by one SID cycle.
        saw_tri_next = { o[2][23], o[2][22:12] ^ { 11{o2_tri_xor} } };
        saw_tri      = (model_4 == sid::MOS6581) ? saw_tri_next : st[N-1];
    end

    always_ff @(posedge clk) begin
        // Rotation on cycles 5 - N+4 for update of 8580 sawtooth / triangle.
        // Freewheeling rotation on N additional cycles for final mix-in of
        // 8580 sawtooth / triangle at phi2.
        if (cycle >= 5 && cycle <= 2*N + 4) begin
            for (int i = 1; i < N; i++) begin
                st[i] <= st[i-1];
            end
            st[0] <= st[N-1];

            if (cycle >= 5 && cycle <= N + 4) begin
                st[0] <= saw_tri_next;
            end
        end
    end

    // Power-on initialization.
    always_ff @(posedge clk) begin
        if (cycle == N + 5 && !primed) begin
            // All oscillators and noise LFSRs are initialized.
            primed <= 1;
        end
    end

    // ------------------------------------------------------------------------
    // Waveforms
    // ------------------------------------------------------------------------

    // With respect to the oscillator, the OSC3 cycle delays are:
    // * noise:   2
    // * pulse:   1
    // * saw_tri: 0 (6581) / 1 (8580)

    // Pre-calculated waveforms for waveform selection.
    always_comb begin
        wf_this_4 = wp_waveform[N-2];
    end

    sid::reg8_t pst        = 0;  // Combined waveforms
    sid::reg8_t ps__6581   = 0;
    sid::reg8_t ps__8580   = 0;
    sid::reg8_t p_t_6581   = 0;
    sid::reg8_t p_t_8580   = 0;
    sid::reg8_t ps__6581_1 = 0;
    sid::reg8_t ps__8580_1 = 0;
    sid::reg8_t p_t_6581_1 = 0;
    sid::reg8_t p_t_8580_1 = 0;
    sid::reg8_t _st        = 0;

    // Combined waveform lookup tables.
    sid::reg8_t sid_waveform_PS__6581[4096];
    sid::reg8_t sid_waveform_PS__8580[4096];
    sid::reg8_t sid_waveform_P_T_6581[2048];
    sid::reg8_t sid_waveform_P_T_8580[2048];
    sid::reg8_t sid_waveform_PS__6581_1[4096];
    sid::reg8_t sid_waveform_PS__8580_1[4096];
    sid::reg8_t sid_waveform_P_T_6581_1[2048];
    sid::reg8_t sid_waveform_P_T_8580_1[2048];

    always_ff @(posedge clk) begin
        // Cycles 5 - N+4 for OSC3 and audio output. Cycles N+5 - 2N+4 for final
        // mix-in of 8580 sawtooth / triangle, used in 8580 noise LFSR
        // writeback and 8580 waveform 0 output in the next SID cycle.
        if (cycle >= 5 && cycle <= 2*N + 4) begin
            // Waveform candidates: Combined waveforms from BRAM and
            // combinational logic, plus sawtooth / triangle.
            // Noise and pulse are mixed in below.
            pst         <= sid_waveform_PST(model_4, saw_tri, variant_4);
            ps__6581    <= sid_waveform_PS__6581[saw_tri];
            ps__8580    <= sid_waveform_PS__8580[saw_tri];
            p_t_6581    <= sid_waveform_P_T_6581[saw_tri[10:0]];
            p_t_8580    <= sid_waveform_P_T_8580[saw_tri[10:0]];
            ps__6581_1  <= sid_waveform_PS__6581_1[saw_tri];
            ps__8580_1  <= sid_waveform_PS__8580_1[saw_tri];
            p_t_6581_1  <= sid_waveform_P_T_6581_1[saw_tri[10:0]];
            p_t_8580_1  <= sid_waveform_P_T_8580_1[saw_tri[10:0]];
            _st         <= sid_waveform__ST(model_4, saw_tri, variant_4);
            saw_tri_5   <= saw_tri;
        end
    end

    sid::reg8_t noise_mask;
    sid::reg8_t noise_lsl_mask;

    always_comb begin
        // Final waveform selection / mixing, excluding noise.
        unique case (waveform_5[2:0])
          'b111: wav = { pst & { 8{pulse_cur} }, 4'b0 };
          'b110: wav = { ((model_5 == sid::MOS6581) ?
              (variant_5 == sid::VARIANT_A ? ps__6581 : ps__6581_1) :
              (variant_5 == sid::VARIANT_A ? ps__8580 : ps__8580_1)) & { 8{pulse_cur} }, 4'b0 };
          'b101: wav = { ((model_5 == sid::MOS6581) ?
              (variant_5 == sid::VARIANT_A ? p_t_6581 : p_t_6581_1) :
              (variant_5 == sid::VARIANT_A ? p_t_8580 : p_t_8580_1)) & { 8{pulse_cur} }, 4'b0 };
          'b100: wav = { 12{pulse_cur} };
          'b011: wav = { _st, 4'b0 };
          'b010: wav = saw_tri_5;
          'b001: wav = { saw_tri_5[10:0], 1'b0 };
          'b000: wav = wp_wav[N-1];  // Waveform 0
        endcase

        // Zero noise outputs always zero the corresponding waveform bits, and
        // can also zero neighboring bits when pulse is selected.
        // FIXME: All combinations of noise and waveforms should be
        // investigated for bit zeroing. For now, we adapt the 8580 pulse bit
        // mask from VICE.
        // We perform writeback to both shifted and current bits after any LFSR
        // shift, in order to avoid a further delay of the waveform
        // output. Note that in theory, the zeroing of bits may be different
        // for shifted (latched) and current (SRAM) bits.
        if (cycle >= 6 && cycle <= N + 5 && waveform_5[2]) begin
            // The four lowermost bits of the noise waveform are grounded, and
            // always pull the neighboring two bits down via the pulse line.
            noise_mask     = (noise < 'hfc) ? { noise[7:1] & noise[6:0], 1'b0 } : 'hfc;
            noise_lsl_mask = (noise_lsl < 'hfc) ? { noise_lsl[7:1] & noise_lsl[6:0], 1'b0 } : 'hfc;
        end else begin
            // Pulse not selected.
            noise_mask     = noise;
            noise_lsl_mask = noise_lsl;
        end

        // Noise LFSR writeback to shifted bits at shift phase 1 -> 2.
        // We assume that the corresponding previous and current waveform bits
        // must both be zero for a latched noise bit to be pulled down.
        noise_lsl_writeback =
            (wp_wav[N-1][11-:8] | (wav[11-:8] & noise_lsl_mask));

        // Mix in noise.
        if (waveform_5[3]) begin
            if (waveform_5[2:0] == '0) begin
                // Only noise is selected.
                wav = { noise, 4'b0 };
            end else begin
                // Noise outputs can zero waveform output bits. The result is
                // also written back to noise LFSR bits at shift phase != 1.
                wav = { wav[11-:8] & noise_mask, 4'b0 };
            end
        end

        // Update of waveform 0 for next cycle.
        wav_prev_faded = (wp_age[N-1] == ((model_5 == sid::MOS6581) ?
                                              WF_0_TTL_6581 :
                                              WF_0_TTL_8580));
    end

    always_ff @(posedge clk) begin
        // Update of previous waveform selector and waveform output, for
        // noise writeback and waveform 0 output.
        if (cycle >= 6 && cycle <= 2*N + 5) begin
            for (int i = 1; i < N; i++) begin
                wp_waveform[i] <= wp_waveform[i-1];
                wp_wav[i]      <= wp_wav[i-1];
                wp_age[i]      <= wp_age[i-1];
            end
            wp_waveform[0] <= waveform_5;
            wp_wav[0]      <= wp_wav[N-1];
            wp_age[0]      <= wp_age[N-1];

            // The 6581 waveform is ready at phi1, while the final mix-in of 8580
            // sawtooth / triangle is not done until phi2.
            if (model_5 == sid::MOS6581 && cycle >= 6 && cycle <= N + 5 ||
                model_5 == sid::MOS8580 && (cycle >= N + 6 && cycle <= 2*N + 5))
            begin
                if (waveform_5 == '0) begin
                    if (wav_prev_faded) begin
                        wp_wav[0] <= 0;
                    end else begin
                        wp_age[0] <= wp_age[N-1] + { 12'b0, tick_ms };
                    end
                end else begin
                    wp_wav[0] <= wav;
                    wp_age[0] <= 0;
                end
            end
        end
    end

    // dd ibs=1 skip=2 < waveforms/6581wf60.dat | od -An -tx1 -v |             cut -b2- > sid_waveform_PS__6581.mem
    // dd ibs=1 skip=2 < waveforms/8580wf60.dat | od -An -tx1 -v |             cut -b2- > sid_waveform_PS__8580.mem
    // dd ibs=1 skip=2 < waveforms/6581wf50.dat | od -An -tx1 -v | head -128 | cut -b2- > sid_waveform_P_T_6581.mem
    // dd ibs=1 skip=2 < waveforms/8580wf50.dat | od -An -tx1 -v | head -128 | cut -b2- > sid_waveform_P_T_8580.mem
    initial begin
        $readmemh("sid_waveform_PS__6581.mem", sid_waveform_PS__6581);
        $readmemh("sid_waveform_PS__8580.mem", sid_waveform_PS__8580);
        $readmemh("sid_waveform_P_T_6581.mem", sid_waveform_P_T_6581);
        $readmemh("sid_waveform_P_T_8580.mem", sid_waveform_P_T_8580);
        $readmemh("sid_waveform_PS__6581_1.mem", sid_waveform_PS__6581_1);
        $readmemh("sid_waveform_PS__8580_1.mem", sid_waveform_PS__8580_1);
        $readmemh("sid_waveform_P_T_6581_1.mem", sid_waveform_P_T_6581_1);
        $readmemh("sid_waveform_P_T_8580_1.mem", sid_waveform_P_T_8580_1);
    end
endmodule
