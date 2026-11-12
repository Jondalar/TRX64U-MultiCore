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

// Waveform selector, voice DCA (digitally controlled amplifier).
// Modeling of non-linearities in MOS6581 waveform and envelope DACs.
module sid_voice #(
    // Number of SIDs; the voice pipeline handles 3 voices for each.
    parameter NS = 2,
    localparam N = 3*NS,
    localparam signed [15:0] WAVEFORM_DC_6581 = -16'sh380,  // OSC3 = 'h38 at 5.94V.
    localparam signed [15:0] WAVEFORM_DC_8580 = -16'sh800,  // No DC offsets in the MOS8580.
    // FIXME: The 6581 voice DC offset has been measured as low as 'h340 on one
    // SID chip, and should thus probably be configurable.
    localparam signed [31:0] VOICE_DC_6581    = 32'('h800*'hff)  // 1/2 the dynamic range.
)(
    input  logic        clk,
    input  sid::cycle_t cycle,
    input  logic [NS-1:0] model,
    // Voice DC offset (digis): 0 none, 1: 1/4, 2: 1/2, 3: 1/1 of the 6581 value.
    // The MOS8580 has no DC offset, i.e. digi = 0.
    input  logic [2*NS-1:0] digi,
    input  sid::reg12_t wav,
    input  sid::reg8_t  env,
    output sid::s22_t   dca
);

    // Keep track of the current SID model.
    logic model_6;
    logic [1:0] digi_6;

    localparam logic [64*3-1:0] SID_TAB = sid::sid_next_tab(7, N);
    logic [2:0] sid_6 = 0;

    // registered: the SID of the next cycle
    always_ff @(posedge clk) begin
        sid_6 <= SID_TAB[cycle*3 +: 3];
    end

    always_comb begin
        model_6 = model[sid_6];
        digi_6  = digi[2*sid_6 +: 2];
    end

    // MOS6581 waveform DAC output.
    sid::reg12_t wav_6581;
    sid::reg12_t wav_8580 = 0;

    sid_dac #(
        .BITS (12)
    ) waveform_dac (
        .vin  (wav_8580),
        .vout (wav_6581)
    );

    // MOS6581 envelope DAC output.
    sid::reg8_t env_6581;
    sid::reg8_t env_8580 = 0;

    sid_dac #(
        .BITS (8)
    ) envelope_dac (
        .vin  (env_8580),
        .vout (env_6581)
    );

    always_ff @(posedge clk) begin
        if (cycle >= 6 && cycle <= N + 5) begin
            wav_8580 <= wav;
            env_8580 <= env;
        end
    end

    // Registered inputs to voice DCA.
    sid::s32_t dca_DC  = 0;
    sid::s16_t wav_dac = 0;
    sid::s16_t env_dac = 0;

    // Output from voice DCA.
    /* verilator lint_off UNUSED */
    sid::s32_t dca_out;
    /* verilator lint_on UNUSED */

    // Voice DCA (digitally controlled amplifier).
    // dca_out = dca_DC + wav_dac*env_dac
    // The result fits in 22 bits.
    muladd voice_dca (
        .c (dca_DC),
        .s (1'b0),
        .a (wav_dac),
        .b (env_dac),
        .o (dca_out)
    );

    always_ff @(posedge clk) begin
        if (cycle >= 7 && cycle <= N + 6) begin
            // Setup for voice DCA multiply-add, ready on cycle 2.
            unique case (digi_6)
              2'd0:    dca_DC <= 0;
              2'd1:    dca_DC <= VOICE_DC_6581 >>> 2;
              2'd2:    dca_DC <= VOICE_DC_6581 >>> 1;
              default: dca_DC <= VOICE_DC_6581;
            endcase

            wav_dac <= (model_6 == sid::MOS6581) ?
                       16'(wav_6581) + WAVEFORM_DC_6581 :
                       16'(wav_8580) + WAVEFORM_DC_8580;

            env_dac <= (model_6 == sid::MOS6581) ?
                       16'(env_6581) :
                       16'(env_8580);
        end
    end

    always_comb begin
        // The DCA output fits in 22 bits.
        // Register output.
        dca = dca_out[21:0];
    end
endmodule
