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

// Clamp to 16 bits.
function sid::s16_t clamp(sid::s17_t x);
    unique case (x[16:15])
      2'b10: clamp = -32768;
      2'b01: clamp =  32767;
      default:
             clamp =  x[15:0];
    endcase
endfunction

// Clamp index to [-1023, 1023].
// A simple bit check cannot be used since -1024 must not be included.
function sid::s11_t tanh_x_clamp(sid::s13_t x);
    tanh_x_clamp = (x < -1023) ? -1023 :
                   (x >  1023) ?  1023 :
                   11'(x);
endfunction

// We take advantage of the fact that tanh(-x) = -tanh(x) to access table data
// for x >= 0 only.
function sid::reg10_t tanh_x_mirror(sid::s11_t x);
    tanh_x_mirror = 10'(x < 0 ? -x : x);
endfunction

function sid::s16_t tanh_y_mirror(logic x_neg, sid::s16_t y);
    tanh_y_mirror = x_neg ? -y : y;
endfunction


module sid_filter #(
    // Number of SIDs; the filter pipeline handles one SID every 5 cycles.
    parameter NS = 2,
    // 1: the cutoff comes from reDIP's own curve model (6581: DAC, offset and
    // tanh table, 8580: 5*fc) and 1/Q is chosen by the SID model, as in the
    // original reDIP. 2: the same cutoff model, and fc_sel chooses per SID
    // between its two forms (the 1/Q table comes from res_hi). There is no
    // loadable curve table any more (T25.5, Alex 2026-10-07).
    parameter LEGACY_FC = 2,
    localparam N = 3*NS,
    // FC offset for average 6581 filter curve.
    localparam FC_OFFSET_6581 = 12'sh600,
    // The 6581 mixer DC offset is approximately -1/18 of the dynamic range of one voice.
    localparam signed [23:0] MIXER_DC_6581 = 24'(-(1 << 20)/18),
    localparam signed [23:0] MIXER_DC_8580 = 24'(0),
    localparam real PI = $acos(-1)
)(
    input  logic             clk,
    // Filter pipeline cycle, 1 .. 5*NS + 3. SID k works on cycles 1 + 5k .. 9 + 5k.
    input  sid::cycle_t      cycle,
    // Voice pipeline cycle: the voice DCA outputs are buffered from cycle 8.
    input  sid::cycle_t      vcycle,
    input  logic [NS-1:0]    model,
    // 1/Q table: 0 = 6581 (reSID 0.16), 1 = 8580. Not used with LEGACY_FC = 1,
    // there the model chooses.
    input  logic [NS-1:0]    res_hi,
    // reDIP's 6581 cutoff curve model: base and offset per SID.
    input  logic [NS*9-1:0]  fc_base,
    input  logic [NS*11-1:0] fc_offset,
    // Cutoff source per SID, only used with LEGACY_FC = 2:
    // 0 the model's own (6581: DAC curve, 8580: 5*fc), 1 own 6581 curve,
    // 2 own 8580. 3 is reserved and acts as 2.
    input  logic [2*NS-1:0]  fc_sel,
    // 1: the SID's filter is bypassed (reSID's enable_filter(false)): all voices and EXT IN go straight
    // to the mixer, FC / RES / FILT and the filter mode bits (LP, BP, HP, 3 OFF) have no effect, the
    // master volume still applies. 0 for every SID gives the original reDIP.
    input  logic [NS-1:0]    bypass,
    // The SID whose registers the pipeline needs (cycles 1 - 5 of the SID).
    output logic [2:0]       freg_sid,
    input  sid::filter_reg_t freg,
    // Direct audio / filter input: voice DCA outputs (cycle 8 + j for voice j)
    // and EXT IN of the SID the pipeline works on.
    input  sid::s22_t        voice_i,
    input  sid::s22_t        ext_i,
    output sid::s20_t        audio_o
);

    // Pipeline stages of SID k are on cycles stage + 5k. The phase counter
    // fph = (cycle - 1) mod 5 and fk = (cycle - 1) / 5 follow the cycle.
    logic [2:0] fph = 0;
    logic [2:0] fk  = 0;

    always_ff @(posedge clk) begin
        if (cycle == 0) begin
            fph <= 0;
            fk  <= 0;
        end else if (fph == 4) begin
            fph <= 0;
            fk  <= fk + 1;
        end else begin
            fph <= fph + 1;
        end
    end

    // stage(c, cycle, phase): cycle c of the SID whose first cycle is now or earlier.
    function automatic logic stage(input int c, input sid::cycle_t cyc, input logic [2:0] ph);
        stage = (cyc >= 6'(c)) && (cyc < 6'(c + 5*NS)) && (ph == 3'((c - 1) % 5));
    endfunction

    logic stage1, stage2, stage3, stage4, stage5, stage6, stage7, stage8;

    always_comb begin
        stage1 = stage(1, cycle, fph);
        stage2 = stage(2, cycle, fph);
        stage3 = stage(3, cycle, fph);
        stage4 = stage(4, cycle, fph);
        stage5 = stage(5, cycle, fph);
        stage6 = stage(6, cycle, fph);
        stage7 = stage(7, cycle, fph);
        stage8 = stage(8, cycle, fph);
        freg_sid = fk;
    end

    // The SID the pipeline is in the first cycles (1 - 5) of.
    logic cur_model;
    logic cur_res_hi;
    logic cur_bypass;

    always_comb begin
        cur_model  = model[fk[$clog2(NS > 1 ? NS : 2)-1:0]];
        cur_res_hi = res_hi[fk[$clog2(NS > 1 ? NS : 2)-1:0]];
        cur_bypass = bypass[fk[$clog2(NS > 1 ? NS : 2)-1:0]];
    end

    // Model and master volume of the SID that has reached stage 3, for the later stages.
    sid::reg4_t  vol     = 0;
    sid::model_e model_r = sid::MOS6581;

    // Filter cutoff: reDIP's own curve model, see below for the scaling that it
    // uses and the formulas for the cutoff itself.
    sid::s16_t w0_T_lsl17;
    sid::s16_t w0_own = 0;
    // fc_sel of the SID that has reached stage 3 (valid in stage 4, like model_r).
    logic [1:0] fc_sel_r = 0;
    logic       own_6581;

    // The maximum value of the scaled w0*T is 1.048576/8*2*pi*12500 = 10294,
    // which is approximately 5 times the maximum fc (2^11 - 1 = 2047),
    // and may be calculated as 5*fc = 4*fc + fc (shift and add).

    // MOS6581 filter cutoff DAC output.
    sid::reg11_t fc_8580;

    always_comb begin
        // Filter cutoff register value.
        fc_8580 = { freg.fc_hi, freg.fc_lo[2:0] };
    end

    generate if (1) begin : legacy_fc
        sid::s16_t w0_T_lsl17_8580;
        sid::s16_t w0_T_lsl17_6581;
        sid::s16_t w0_T_lsl17_6581_base = 0;

        // Since tanh(-x) = -tanh(x), we store table data for x >= 0 only, and use
        // the functions tanh_x_mirror and tanh_y_mirror for mirroring.
        sid::s16_t w0_T_lsl17_6581_tanh[1024];
        sid::s16_t w0_T_lsl17_6581_y0;
        initial begin
            for (int i = 0; i < 1024; i++) begin
                w0_T_lsl17_6581_tanh[i] = 16'($rtoi(1.048576/8*2*PI*12000*$tanh(i/350.0) + 0.5));
            end
            // NB! Can't lookup from table here, as this precludes the use of BRAM.
            // w0_T_lsl17_6581_y0 = w0_T_lsl17_6581_tanh[0];
            w0_T_lsl17_6581_y0 = 16'($rtoi(1.048576/8*2*PI*12000*1 + 0.5));
        end

        sid::reg11_t fc_6581;
        sid::s12_t   fc_offset_k;
        sid::reg11_t fc_x = 0;

        sid_dac #(
            .BITS(11)
        ) fc_dac (
            .vin  (fc_8580),
            .vout (fc_6581)
        );

        always_comb begin
            // Filter cutoff offset.
            fc_offset_k = 12'(signed'(fc_offset[fk[$clog2(NS > 1 ? NS : 2)-1:0]*11 +: 11])) + FC_OFFSET_6581;
        end

        always_ff @(posedge clk) begin
            if (stage2) begin
                // MOS6581: w0 = filter curve
                // 1.048576/8*fc_base is approximated by fc_base >> 3.
                w0_T_lsl17_6581_base <= { 10'b0, fc_base[fk[$clog2(NS > 1 ? NS : 2)-1:0]*9 + 3 +: 6] };
                // We have to register fc_x in order to meet timing.
                fc_x <= tanh_x_clamp(signed'(13'(fc_6581)) - fc_offset_k);

                // MOS8580: w0 = 5*fc = 4*fc + fc
                w0_T_lsl17_8580 <= { 3'b0, fc_8580, 2'b0 } + { 5'b0, fc_8580 };
            end

            if (stage3) begin
                // Read from BRAM.
                w0_T_lsl17_6581 <= w0_T_lsl17_6581_tanh[tanh_x_mirror(fc_x)];
            end
        end

        always_comb begin
            w0_own = own_6581 ?
                     w0_T_lsl17_6581_base + w0_T_lsl17_6581_y0 + tanh_y_mirror(fc_x[10], w0_T_lsl17_6581) :
                     w0_T_lsl17_8580;  // w0*T << 17
        end
    end endgenerate

    always_comb begin
        // 6581 curve for fc_sel 1, or 0 with the 6581 model; the original reDIP is fc_sel 0.
        own_6581 = (fc_sel_r == 2'd1) || (fc_sel_r == 2'd0 && model_r == sid::MOS6581);
        w0_T_lsl17 = w0_own;
    end

    // Filter resonance.
    //
    // From die photographs, assuming ideal op-amps:
    //
    // MOS6581: 1/Q =~ ~res/8
    // MOS8580: 1/Q =~ 2^((4 - res)/8)
    //
    // The actual range of 1/Q in the MOS6581 is quite different, partly
    // because of low gain op-amps. For now, we use the formula from reSID 0.16.
    //
    // The values are multiplied by 1024 (1 << 10).
    // The coefficient 1024 is dispensed of later by right-shifting 10 times.
    sid::reg11_t _1_Q_lsl10;

    // 1/Q * 1024 for RES = 0 .. 15.
    // MOS6581: 1024.0/(0.707 + res/15.0) + 0.5, truncated.
    function automatic sid::reg11_t _1_Q_6581_lsl10(sid::reg4_t res);
        unique case (res)
          4'd0: _1_Q_6581_lsl10 = 11'd1448;
          4'd1: _1_Q_6581_lsl10 = 11'd1324;
          4'd2: _1_Q_6581_lsl10 = 11'd1219;
          4'd3: _1_Q_6581_lsl10 = 11'd1129;
          4'd4: _1_Q_6581_lsl10 = 11'd1052;
          4'd5: _1_Q_6581_lsl10 = 11'd984;
          4'd6: _1_Q_6581_lsl10 = 11'd925;
          4'd7: _1_Q_6581_lsl10 = 11'd872;
          4'd8: _1_Q_6581_lsl10 = 11'd826;
          4'd9: _1_Q_6581_lsl10 = 11'd783;
          4'd10: _1_Q_6581_lsl10 = 11'd745;
          4'd11: _1_Q_6581_lsl10 = 11'd711;
          4'd12: _1_Q_6581_lsl10 = 11'd679;
          4'd13: _1_Q_6581_lsl10 = 11'd651;
          4'd14: _1_Q_6581_lsl10 = 11'd624;
          default: _1_Q_6581_lsl10 = 11'd600;
        endcase
    endfunction

    // MOS8580: 1024.0*2^((4 - res)/8.0) + 0.5, truncated.
    function automatic sid::reg11_t _1_Q_8580_lsl10(sid::reg4_t res);
        unique case (res)
          4'd0: _1_Q_8580_lsl10 = 11'd1448;
          4'd1: _1_Q_8580_lsl10 = 11'd1328;
          4'd2: _1_Q_8580_lsl10 = 11'd1218;
          4'd3: _1_Q_8580_lsl10 = 11'd1117;
          4'd4: _1_Q_8580_lsl10 = 11'd1024;
          4'd5: _1_Q_8580_lsl10 = 11'd939;
          4'd6: _1_Q_8580_lsl10 = 11'd861;
          4'd7: _1_Q_8580_lsl10 = 11'd790;
          4'd8: _1_Q_8580_lsl10 = 11'd724;
          4'd9: _1_Q_8580_lsl10 = 11'd664;
          4'd10: _1_Q_8580_lsl10 = 11'd609;
          4'd11: _1_Q_8580_lsl10 = 11'd558;
          4'd12: _1_Q_8580_lsl10 = 11'd512;
          4'd13: _1_Q_8580_lsl10 = 11'd470;
          4'd14: _1_Q_8580_lsl10 = 11'd431;
          default: _1_Q_8580_lsl10 = 11'd395;
        endcase
    endfunction

    // Hardware 16x16->32 multiply-add:
    // o = c +- (a * b)
    sid::s32_t o;
    sid::s32_t c;
    logic      s;
    sid::s16_t a;
    sid::s16_t b;

    muladd opamp (
        .c (c),
        .s (s),
        .a (a),
        .b (b),
        .o (o)
    );

    // Filter states for NS SID chips, updated as follows:
    // vlp = vlp - w0*vbp
    // vbp = vbp - w0*vhp
    // vhp = 1/Q*vbp - vlp - vi
    //
    // (vlp, vbp, vhp) x NS in a ring of 3*NS: v[N-1], v[N-2], v[N-3] are the
    // state of the SID that the pipeline is working on (vlp, vbp, vhp), the
    // new state is shifted in at v[0]; v[1] .. v[N-4] only delay.
    sid::s16_t v [N];
    initial begin for (int i = 0; i < N; i++) v[i] = '0; end
    sid::s17_t dv;
    sid::s16_t v_next;

    // Buffer for the voice DCA outputs: the voice pipeline produces three
    // voices every three cycles, the filter pipeline takes three voices
    // every five cycles.
    sid::s22_t dcabuf [N];
    sid::s22_t voice_in;
    logic [5:0] slot;

    always_ff @(posedge clk) begin
        if (vcycle >= 8 && vcycle <= N + 7) begin
            dcabuf[vcycle - 8] <= voice_i;
        end
    end

    always_comb begin
        // Voice 0 .. 2 of the SID at stages 2 .. 4, EXT IN at stage 5.
        slot     = 6'({ 1'b0, fk, 1'b0 } + { 2'b0, fk }) + 6'(fph) - 6'd1;
        voice_in = stage5 ? ext_i : dcabuf[slot];
    end

    // Mux and sum for direct audio path and filter input path.
    // Each voice is 22 bits, i.e. the sum of four voices is 24 bits.
    sid::s24_t  vd   = 0;
    sid::s24_t  vi   = 0;
    sid::reg4_t filt = 0;
    sid::s24_t  vd1  = 0;

    always_ff @(posedge clk) begin
        if (stage1) begin
            vd   <= 0;
            vi   <= 0;
            filt <= cur_bypass ? 4'd0 : freg.filt;
        end else if (stage2 || stage3 || stage4 || stage5) begin
            // Direct audio path.
            // 3 OFF (mode[3]) disconnects voice 3 from the direct audio path.
            if (~(filt[0] || (freg.mode[3] && !cur_bypass && stage4))) begin
                vd <= vd + 24'(voice_in);
            end

            // Filter path.
            if (filt[0]) begin
                vi <= vi + 24'(voice_in);
            end

            filt <= filt >> 1;
        end

        if (stage6) begin
            // Buffer input to master volume, adding in mixer DC at the same
            // time.
            vd1 <= vd + ((model_r == sid::MOS6581) ?
                         MIXER_DC_6581 :
                         MIXER_DC_8580);
        end
    end

    // Mux and sum for filter outputs.
    // Each filter output is 16 bits, i.e. the sum of three outputs is 17.5
    // bits. We assume that filter outputs are out of phase, so that we don't
    // need more than 17 bits.
    sid::s17_t  vf   = 0;
    sid::reg4_t mode = 0;

    // Audio mixers.
    always_ff @(posedge clk) begin
        if (stage4) begin
            vf   <= 0;
            mode <= cur_bypass ? 4'd0 : freg.mode;
        end else if (stage5 || stage6 || stage7) begin
            if (mode[0]) begin
                vf <= vf + v_next;
            end

            mode <= mode >> 1;
        end
    end

    always_comb begin
        // Intermediate results for filter.
        // Shifts -w0*vbp and -w0*vlp right by 17.
        dv = 17'(o >>> 17);

        // Next hp or lp/bp.
        v_next = clamp(stage7 ? o[10+:17] : 17'(v[N-1]) + dv);

        // Final result for audio output, at cycle 9 + 5k.
        // The effective width is 20 bits (4 bit volume * 16 bit audio).
        audio_o = o[19:0];
    end

    // Calculation of filter outputs. TDM to use only one multiplier.
    always_ff @(posedge clk) begin
        if (stage5 || stage6 || stage7) begin
            // (vlp, vbp, vhp) x NS
            for (int i = 1; i < N; i++) begin
                v[i] <= v[i-1];
            end
            v[0] <= v_next;
        end

        if (stage2) begin
            // MOS6581: 1/Q =~ ~res/8 (not used - op-amps are not ideal)
            // MOS8580: 1/Q =~ 2^((4 - res)/8)
            _1_Q_lsl10 <= ((LEGACY_FC == 1 ? cur_model : cur_res_hi) == sid::MOS6581) ?
                          _1_Q_6581_lsl10(freg.res) :
                          _1_Q_8580_lsl10(freg.res);
        end

        if (stage3) begin
            // Save model and volume for later stages.
            model_r <= sid::model_e'(cur_model);
            fc_sel_r <= (LEGACY_FC == 2) ? fc_sel[fk[$clog2(NS > 1 ? NS : 2)-1:0]*2 +: 2] : 2'd0;
            vol     <= freg.vol;
        end

        if (stage4) begin
            // vlp = vlp - w0*vbp
            // We first calculate -w0*vbp
            c <= 0;
            s <= 1'b1;
            a <= w0_T_lsl17;       // w0*T << 17
            b <= v[N-2];           // vbp
        end else if (stage5) begin
            // Result for vlp ready.

            // vbp = vbp - w0*vhp
            // We first calculate -w0*vhp
            // c <= 0;
            // s <= 1'b1;
            // a <= ...
            b <= v[N-3];           // vhp
        end else if (stage6) begin
            // Result for vbp ready.

            // vhp = 1/Q*vbp - vlp - vi
            // c <= -(32'(v0) + (32'(vi) >>> 7)) << 10;
            c <= -(((32'(v[0]) << 7) + 32'(vi)) << 3);
            s <= 1'b0;
            a <= 16'(_1_Q_lsl10);  // 1/Q << 10
            b <= v_next;           // vbp
        end else if (stage8) begin
            // 7: Result for vhp ready

            // Audio output: aout = vol*amix
            // In the real SID, the signal is inverted first in the mixer
            // op-amp, and then again in the volume control op-amp.
            c <= 0;
            // s <= 1'b0;
            a <= { 12'b0, vol };         // Master volume
            b <= clamp(17'(vd1 >>> 7) +  // Audio mixer / master volume input
                       vf);
        end
    end
endmodule
