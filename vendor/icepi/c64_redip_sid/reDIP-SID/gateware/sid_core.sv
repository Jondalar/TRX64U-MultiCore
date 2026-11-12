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

// The SID pipelines for NS chips: the part of sid_api that does not depend on
// the bus. One pass (a start pulse) runs the voice pipeline for 3*NS voices
// and the filter pipeline for NS filters, applies the CPU access of the pass
// (op_*), and ends with the 20 bit output of every SID and OSC3 / ENV3 / bus
// value read-back registers.
//
// Pass length in clock cycles after start: max(6*NS + 5, 5*NS + 12);
// NS = 2: 22, NS = 8: 53.
module sid_core #(
    parameter NS = 2,
    // See sid_filter and sid_waveform.
    parameter LEGACY_FC = 2,
    parameter VARIANT_B = 1,
    parameter PULSE_FIX = 0,
    localparam N = 3*NS,
    localparam FLAST = 5*NS + 5,
    localparam VLAST = (6*NS + 5 > FLAST + 7) ? 6*NS + 5 : FLAST + 7
)(
    input  logic               clk,
    input  logic               start,
    input  logic               res,
    // The CPU access of the pass, stable during the pass.
    input  logic               op_valid,
    input  logic               op_we,
    input  logic               op_all,
    input  logic [2:0]         op_sid,
    input  sid::reg5_t         op_reg,
    input  sid::reg8_t         op_data,
    input  sid::reg8_t         op_rdata,
    // Per SID configuration, quasi-static.
    input  logic [NS-1:0]      model,
    input  logic [NS-1:0]      variant,
    input  logic [NS-1:0]      res_hi,
    input  logic [2*NS-1:0]    digi,
    input  logic [NS*9-1:0]    fc_base,
    input  logic [NS*11-1:0]   fc_offset,
    // Filter cutoff source per SID, only with LEGACY_FC = 2 (see sid_filter).
    input  logic [2*NS-1:0]    fc_sel,
    // Filter bypass per SID (see sid_filter), quasi-static.
    input  logic [NS-1:0]      bypass,
    // EXT IN per SID (22 bits).
    input  logic [NS*22-1:0]   ext_in,
    // Results.
    output sid::cycle_t        voice_cycle = 0,
    output logic               tick_ms,
    output logic [NS*20-1:0]   audio_o,   // output of each SID, updated during the pass
    output logic               audio_valid_o,  // one clock: all NS outputs are new
    output logic [NS*8-1:0]    osc3,
    output logic [NS*8-1:0]    env3,
    output logic [NS*8-1:0]    dbus_value,
    input  logic [4:0]         lv_addr,   // { SID, voice }
    output logic [7:0]         lv_data    // envelope level of the voice
);

    // SID pipeline cycle counters.
    sid::cycle_t filter_cycle = 0;

    always_ff @(posedge clk) begin
        // Start the voice pipeline.
        if (start) begin
            voice_cycle <= 1;
        end else if (voice_cycle == VLAST) begin
            voice_cycle <= 0;
        end else if (voice_cycle != 0) begin
            voice_cycle <= voice_cycle + 1;
        end

        // Start the filter pipeline at voice pipeline cycle 8; the DCA output
        // of the first voice is ready at that cycle, and is buffered.
        // Keep counting until the last output has been captured.
        if (voice_cycle == 7) begin
            filter_cycle <= 1;
        end else if (filter_cycle == FLAST) begin
            filter_cycle <= 0;
        end else if (filter_cycle != 0) begin
            filter_cycle <= filter_cycle + 1;
        end
    end

    // Tick approximately every ms, for smaller counters in submodules.
    // ~1MHz / 1024 = ~1kHz
    logic  [9:0] count_us = 0;
    logic [10:0] count_us_next;

    always_comb begin
        // Use carry as tick.
        count_us_next = { 1'b0, count_us } + 1;
        tick_ms = count_us_next[10];
    end

    always_ff @(posedge clk) begin
        if (voice_cycle == 1) begin
            // Update counter, discarding carry.
            count_us <= count_us_next[9:0];
        end
    end

    // SID control registers.
    sid::freq_pw_t      freq_pw_1;
    logic [2:0]         test;
    logic [2:0]         sync;
    sid::control_t      control_4;
    sid::envelope_reg_t ereg_5;
    sid::filter_reg_t   freg;
    logic [2:0]         freg_sid;

    sid_control #(
        .NS (NS)
    ) control (
        .clk          (clk),
        .tick_ms      (tick_ms),
        .voice_cycle  (voice_cycle),
        .res          (res),
        .op_valid     (op_valid),
        .op_we        (op_we),
        .op_all       (op_all),
        .op_sid       (op_sid),
        .op_reg       (op_reg),
        .op_data      (op_data),
        .op_rdata     (op_rdata),
        .model        (model),
        .freq_pw_1    (freq_pw_1),
        .test         (test),
        .sync         (sync),
        .control_4    (control_4),
        .ereg_5       (ereg_5),
        .freg_sid     (freg_sid),
        .freg         (freg),
        .dbus_value   (dbus_value)
    );

    // SID waveform generator.
    sid::reg12_t   wav;

    sid_waveform #(
        .NS        (NS),
        .VARIANT_B (VARIANT_B),
        .PULSE_FIX (PULSE_FIX)
    ) waveform (
        .clk       (clk),
        .tick_ms   (tick_ms),
        .cycle     (voice_cycle),
        .res       (res),
        .model     (model),
        .variant   (variant),
        .freq_pw_1 (freq_pw_1),
        .test      (test),
        .sync      (sync),
        .control_4 (control_4),
        .wav       (wav)
    );

    // SID envelope generator.
    sid::reg8_t    env;

    sid_envelope #(
        .NS     (NS)
    ) envelope (
        .clk    (clk),
        .cycle  (voice_cycle),
        .res    (res),
        .ereg_5 (ereg_5),
        .env    (env)
    );

    // Store OSC3 and ENV3 for all SIDs (the voice 3 outputs of SID s are ready
    // at cycle 8 + 3s).
    always_ff @(posedge clk) begin
        for (int s = 0; s < NS; s++) begin
            if (voice_cycle == 6'(8 + 3*s)) begin
                osc3[s*8 +: 8] <= wav[11-:8];
                env3[s*8 +: 8] <= env;
            end
        end
    end

    // Envelope level of every voice for the user of the core, one write per
    // cycle while the envelope outputs pass by.
    logic [7:0] levels [32];
    initial begin for (int i = 0; i < 32; i++) levels[i] = '0; end
    logic [2:0] lv_s = 0;
    logic [1:0] lv_v = 0;

    always_ff @(posedge clk) begin
        if (voice_cycle == 5) begin
            lv_s <= 0;
            lv_v <= 0;
        end else if (voice_cycle >= 6 && voice_cycle <= N + 5) begin
            levels[{ lv_s, lv_v }] <= env;
            if (lv_v == 2) begin
                lv_v <= 0;
                lv_s <= lv_s + 1;
            end else begin
                lv_v <= lv_v + 1;
            end
        end
    end

    assign lv_data = levels[lv_addr];

    // Pipeline for voice outputs.
    sid::s22_t dca;

    sid_voice #(
        .NS    (NS)
    ) voice (
        .clk   (clk),
        .cycle (voice_cycle),
        .model (model),
        .digi  (digi),
        .wav   (wav),
        .env   (env),
        .dca   (dca)
    );

    // Pipeline for filter outputs.
    sid::s20_t filter_o;
    sid::s22_t ext_i;

    always_comb begin
        ext_i = ext_in[freg_sid[$clog2(NS > 1 ? NS : 2)-1:0]*22 +: 22];
    end

    sid_filter #(
        .NS        (NS),
        .LEGACY_FC (LEGACY_FC)
    ) filter (
        .clk        (clk),
        .cycle      (filter_cycle),
        .vcycle     (voice_cycle),
        .model      (model),
        .res_hi     (res_hi),
        .fc_base    (fc_base),
        .fc_offset  (fc_offset),
        .fc_sel     (fc_sel),
        .bypass     (bypass),
        .freg_sid   (freg_sid),
        .freg       (freg),
        .voice_i    (dca),
        .ext_i      (ext_i),
        .audio_o    (filter_o)
    );

    // The filter output of SID k is ready on filter pipeline cycle 9 + 5k.
    always_ff @(posedge clk) begin
        for (int k = 0; k < NS; k++) begin
            if (filter_cycle == 6'(9 + 5*k)) begin
                audio_o[k*20 +: 20] <= filter_o;
            end
        end
        audio_valid_o <= filter_cycle == 6'(FLAST - 1);
    end
endmodule
