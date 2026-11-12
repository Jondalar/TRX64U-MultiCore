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

// ----------------------------------------------------------------------------
// this file is based on reDIP-SID sid_api.sv
// with changes to remove SystemVerilog-specific
// code in module I/O section and adapted so it can be
// used with rest of FPGA implementation of Icepi Zero C64
// by m1nl
// ----------------------------------------------------------------------------

// (vendor) `default_nettype none
`timescale 1 ns / 1 ps

module sid_api (
    input  wire        clk,
    // bus_i_t mapped to individual signals
    input  wire [4:0]  bus_addr,
    input  wire [7:0]  bus_data,
    input  wire        bus_phi2,
    input  wire        bus_phi2_n,
    input  wire        bus_r_w_n,
    input  wire        bus_res,
    // cs_t mapped to individual signals
    input  wire        cs_n,
    input  wire        cs_io1_n,
    input  wire        a8,
    input  wire        a5,
    output reg  [7:0]  data_o,
    // pot_i_t mapped to individual signals
    input  wire [1:0]  pot_charged,
    // pot_o_t mapped to individual signals
    output wire        pot_discharge,
    // audio_t mapped to individual signals
    input  wire [23:0] audio_i_left,
    input  wire [23:0] audio_i_right,
    output reg  [19:0] audio_o_left,
    output reg  [19:0] audio_o_right,
    // extra control
    input wire         sid_model,
    input wire         sid_mono,
    input wire         sid_variants
);

    // Two SIDs: SID 1 is always located at D400, SID 2 at D420 / D500 (or
    // at D400 as well in mono). The pipelines are the ones in sid_core.sv,
    // which handles any number of SIDs.
    localparam NS = 2;

    sid::cfg_t sid1_cfg, sid2_cfg;

    // Pick requested SID variant
    assign sid1_cfg.model     = sid_model ? sid::MOS8580 : sid::MOS6581;
    assign sid1_cfg.variant   = sid::VARIANT_A;
    assign sid1_cfg.addr      = sid::D400;
    assign sid1_cfg.fc_base   = 9'd245;
    assign sid1_cfg.fc_offset = -11'sd780;

    assign sid2_cfg.model     = sid_model ? sid::MOS8580 : sid::MOS6581;
    assign sid2_cfg.variant   = sid_variants ? sid::VARIANT_B : sid::VARIANT_A;
    assign sid2_cfg.addr      = sid_mono ? (sid::D400 | sid::D420 | sid::D500) : (sid::D420 | sid::D500);
    assign sid2_cfg.fc_base   = 9'd240;
    assign sid2_cfg.fc_offset = -11'sd785;

    // Map input signals to internal bus structure
    sid::bus_i_t bus_i;
    assign bus_i.addr = bus_addr;
    assign bus_i.data = bus_data;
    assign bus_i.phi2 = bus_phi2;
    assign bus_i.r_w_n = bus_r_w_n;
    assign bus_i.res = bus_res;

    // Map input signals to internal cs structure
    sid::cs_t cs;
    assign cs.cs_n = cs_n;
    assign cs.cs_io1_n = cs_io1_n;
    assign cs.a8 = a8;
    assign cs.a5 = a5;

    // Map input signals to internal pot structure
    sid::pot_i_t pot_i;
    assign pot_i.charged = pot_charged;

    // Map output pot signal
    sid::pot_o_t pot_o;
    assign pot_discharge = pot_o.discharge;

    // NB! Don't put multi-bit variables in arrays, as Yosys handles that incorrectly.
    logic [1:0] sid_cs;
    logic [1:0] model;
    logic [1:0] variant;

    always_comb begin
        model   = { sid2_cfg.model, sid1_cfg.model };
        variant = { sid2_cfg.variant, sid1_cfg.variant };

        // Chip select decode.
        // SID 2 address is configurable.
        // SID 1 is always located at D400.
        sid_cs[1] = sid2_cfg.addr[sid::D400_BIT] & ~cs.cs_n |
                    sid2_cfg.addr[sid::D420_BIT] & ~cs.cs_n & cs.a5 |
                    sid2_cfg.addr[sid::D500_BIT] & ~cs.cs_n & cs.a8 |
                    sid2_cfg.addr[sid::DE00_BIT] & ~cs.cs_io1_n;
        sid_cs[0] = sid2_cfg.addr[sid::D400_BIT] & ~cs.cs_n |
                    ~cs.cs_n & ~sid_cs[1];
    end

    // SID read-only registers.
    sid::pot_reg_t sid1_pot;
    logic [NS*8-1:0] osc3, env3, dbus_value;
    sid::reg8_t    r_value;
    sid::reg8_t    r_value_q = 0;
    logic          r;
    logic signed [5:0] maddr;

    always_comb begin
        // The read-only registers: POTX, POTY, OSC3, ENV3.
        // Default to SID 1. Return FF for SID 2 POTX/Y.
        maddr = signed'(6'(bus_i.addr)) - 'sh19;
        r = bus_i.phi2 & bus_i.r_w_n && maddr >= 0 && maddr <= 3;
        unique case (maddr[1:0])
          2'd0:    r_value = (sid_cs == 2'b10) ? 8'hff : sid1_pot.xy[0];
          2'd1:    r_value = (sid_cs == 2'b10) ? 8'hff : sid1_pot.xy[1];
          2'd2:    r_value = (sid_cs == 2'b10) ? osc3[15:8] : osc3[7:0];
          default: r_value = (sid_cs == 2'b10) ? env3[15:8] : env3[7:0];
        endcase

        // Data output, default to SID 1 (for both SIDs at D400).
        data_o = |sid_cs && r ? r_value : dbus_value[(sid_cs == 2'b10)*8 +: 8];
    end

    // The value that a read returned, for the bus value of the SID.
    always_ff @(posedge clk) begin
        if (bus_i.phi2) begin
            r_value_q <= r_value;
        end
    end

    // The CPU access, latched when the pipelines start after the falling
    // edge of phi2.
    logic        op_valid = 0;
    logic        op_we    = 0;
    logic        op_all   = 0;
    logic [2:0]  op_sid   = 0;
    sid::reg5_t  op_reg   = 0;
    sid::reg8_t  op_data  = 0;
    sid::reg8_t  op_rdata = 0;

    always_ff @(posedge clk) begin
        if (bus_phi2_n) begin
            op_valid <= |sid_cs;
            op_we    <= ~bus_i.r_w_n;
            op_all   <= &sid_cs;
            op_sid   <= 3'(sid_cs[1]);
            op_reg   <= bus_i.addr;
            op_data  <= bus_i.data;
            op_rdata <= r_value_q;
        end
    end

    // Start the pipelines after the falling edge of phi2.
    logic        start = 0;
    always_ff @(posedge clk) begin
        start <= bus_phi2_n;
    end

    // SID 1 and 2 in the same pipelines.
    logic        tick_ms;
    sid::cycle_t voice_cycle;
    logic [NS*20-1:0] audio_o;
    logic [7:0]  lv_data;

    sid_core #(
        .NS        (NS),
        .LEGACY_FC (1),
        .VARIANT_B (1)
    ) core (
        .clk           (clk),
        .start         (start),
        .res           (bus_i.res),
        .op_valid      (op_valid),
        .op_we         (op_we),
        .op_all        (op_all),
        .op_sid        (op_sid),
        .op_reg        (op_reg),
        .op_data       (op_data),
        .op_rdata      (op_rdata),
        .model         (model),
        .variant       (variant),
        .res_hi        (model),
        // The 6581 has a voice DC offset (digis), the 8580 has not.
        .digi          ({ model[1] ? 2'd0 : 2'd3, model[0] ? 2'd0 : 2'd3 }),
        .fc_base       ({ sid2_cfg.fc_base, sid1_cfg.fc_base }),
        .fc_offset     ({ sid2_cfg.fc_offset, sid1_cfg.fc_offset }),
        .fc_sel        (4'd0),
        .bypass        (2'd0),
        .ext_in        ({ audio_i_right[23-:22], audio_i_left[23-:22] }),
        .voice_cycle   (voice_cycle),
        .tick_ms       (tick_ms),
        .audio_o       (audio_o),
        .audio_valid_o (),
        .osc3          (osc3),
        .env3          (env3),
        .dbus_value    (dbus_value),
        .lv_addr       (5'd0),
        .lv_data       (lv_data)
    );

    always_comb begin
        audio_o_left  = audio_o[19:0];
        audio_o_right = audio_o[39:20];
    end

    // SID POTX / POTY.
    sid_pot potxy (
       .clk   (clk),
       .cycle (voice_cycle),
       .pot_i (pot_i),
       .pot_o (pot_o),
       .pot   (sid1_pot)
    );
endmodule
