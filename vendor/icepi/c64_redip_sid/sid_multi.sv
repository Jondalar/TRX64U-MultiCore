// SPDX-License-Identifier: GPL-3.0-or-later
// ---------------------------------------------------------------------------
// sid_multi: reDIP's SID pipelines (sid_core, NS SIDs, 8 = two UltiSIDs split
// in four) behind the interface of the T25.1 engine (trx64/trx_sid/rtl/
// trx_sid_engine.v), so that trx_sid.v (the app's address decode, split, POT,
// op latch, read-back) and trx_audio (T25.3) work with either. Spec:
// docs/specs/T25.4-redip-ultisid.md.
//
// One pass per executed C64 cycle on `clk` (2 x dot4x: 64 clocks), started by
// `start`; the pass takes max(6 NS + 5, 5 NS + 12) clocks (NS = 8: 53). The
// read-back registers are final earlier: OSC3 / ENV3 of SID s at clock 8 + 3 s
// (29 for SID 7), the bus values at clock 8.
// SID s = 4 u + p: u = UltiSID (settings WAVES, RES, DIGI, filter), p = part.
// ---------------------------------------------------------------------------
`timescale 1 ns / 1 ps
module sid_multi #(
    parameter NS = 8,
    // 1: reDIP's own cutoff curve model only (bit-exact bench against the original reDIP;
    // cfg_fc_*, cfg_auto are ignored), 2: the same model, per UltiSID chosen by cfg_fc_sel
    // (T25.5; the reset values of the cfg_* below give the original reDIP). There is no
    // loadable curve table (T25.5, Alex 2026-10-07).
    parameter LEGACY_FC = 2,
    // 1: SIDs with an odd number use variant B of the 6581 combined waveforms
    // (reDIP's default for its SID 2). 0: all variant A, the tables of B are
    // not built.
    parameter VARIANT_B = 0,
    // 0: the final mix-in of the 8580 waveform of the last voices of each group of two
    // SIDs reads the pulse of the wrong voice, as in reDIP (bit-exact bench); 1: corrected
    parameter PULSE_FIX = 0,   // Alex 2026-10-07: the original reDIP sound for any number of SIDs
    parameter MEMDIR = "."
) (
    input  wire        clk,
    input  wire        rst,                 // SID /RES, level
    input  wire        start,               // one clock: a C64 cycle happened
    // the CPU op of the previous C64 cycle, stable from start to the end of the pass
    input  wire        op_valid_i,
    input  wire        op_we_i,
    input  wire        op_all_i,            // the op goes to every SID (bench; the app never does this)
    input  wire  [2:0] op_sid_i,
    input  wire  [4:0] op_reg_i,
    input  wire  [7:0] op_wdata_i,
    input  wire  [7:0] op_rdata_i,          // what the read returned (OSC3/ENV3/POT/bus)
    // per UltiSID u (bit u): WAVES (1 = 8580), RES (1 = high), DIGI 0..3
    input  wire  [1:0] cfg_8580,
    input  wire  [1:0] cfg_res_high,
    input  wire  [3:0] cfg_digi,
    // T25.5 (LEGACY_FC = 2), per UltiSID u: filter source (0 model's own, 1 own 6581 curve, 2 own
    // 8580, 3 reserved = 2), {digi auto, resonance auto} (auto: follows the chip model, as in reDIP),
    // 6581 curve base (9 bit) and offset (11 bit signed; the odd SIDs of an UltiSID get -5 on both,
    // reDIP's difference between its two chips)
    input  wire  [3:0] cfg_fc_sel,
    input  wire  [3:0] cfg_auto,
    input  wire [17:0] cfg_fc_base,
    input  wire [21:0] cfg_fc_off,
    // T25.5: per UltiSID, 1 = the filter is bypassed (reSID's enable_filter(false)); 0 = the original reDIP
    input  wire  [1:0] cfg_bypass,
    // read-back registers per SID {bus, ENV3, OSC3}, 24 bits each
    output wire [NS*24-1:0] rb_all,
    // envelope levels for the app (+0x80..+0x9F): {sid, voice}, asynchronous
    input  wire  [4:0] lv_addr,
    output wire  [7:0] lv_data,
    // samples: one per SID per pass (8 clocks after the pass), and the two UltiSID sums
    output reg         smp_valid,
    output reg   [2:0] smp_sid,
    output reg  signed [19:0] smp_data,
    output reg         sum_valid,           // one clock, all NS outputs of the pass are new
    output reg  signed [19:0] sum_u0,       // SIDs 0-3, saturated
    output reg  signed [19:0] sum_u1,       // SIDs 4-7
    output wire        busy
);

// ---- configuration per SID
reg  [NS-1:0]    model, res_hi, variant, bypass;
reg  [2*NS-1:0]  digi, fc_sel;
reg  [NS*9-1:0]  fc_base;
reg  [NS*11-1:0] fc_offset;
integer i, u;
reg signed [11:0] ofs;
reg signed [10:0] bas;
always @(*) begin
    for (i = 0; i < NS; i = i + 1) begin
        u = i >> 2;
        model[i]            = cfg_8580[u];
        res_hi[i]           = cfg_res_high[u];
        digi[2*i +: 2]      = cfg_digi[2*u +: 2];
        variant[i]          = VARIANT_B ? i[0] : 1'b0;
        bypass[i]           = cfg_bypass[u];
        fc_sel[2*i +: 2]    = 2'd0;
        fc_base[9*i +: 9]   = i[0] ? 9'd240 : 9'd245;
        fc_offset[11*i +: 11] = i[0] ? -11'sd785 : -11'sd780;
        ofs = 12'sd0;
        bas = 11'sd0;
        if (LEGACY_FC == 2) begin
            if (cfg_auto[2*u])     res_hi[i]      = cfg_8580[u];
            if (cfg_auto[2*u + 1]) digi[2*i +: 2] = cfg_8580[u] ? 2'd0 : 2'd3;
            fc_sel[2*i +: 2] = cfg_fc_sel[2*u +: 2];
            // the second chip of reDIP's pair: -5 on base and offset, saturated
            bas = $signed({2'b00, cfg_fc_base[9*u +: 9]}) - (i[0] ? 11'sd5 : 11'sd0);
            if (bas < 0) bas = 11'sd0;
            ofs = $signed({cfg_fc_off[11*u + 10], cfg_fc_off[11*u +: 11]}) - (i[0] ? 12'sd5 : 12'sd0);
            if (ofs < -12'sd1024) ofs = -12'sd1024;
            fc_base[9*i +: 9]     = bas[8:0];
            fc_offset[11*i +: 11] = ofs[10:0];
        end
    end
end

// ---- a pass always runs to the end (the rings must make complete rotations):
// a start while one is running is ignored, and /RES is sampled when a pass starts
wire       core_idle;
wire       start_i = start && core_idle;
reg        res_q = 1'b1;
always @(posedge clk) if (start_i) res_q <= rst;

// ---- the pipelines
wire [5:0]  voice_cycle;
wire [NS*20-1:0] audio_o;
wire        audio_valid;
wire [NS*8-1:0] osc3, env3, dbus;
wire        tick_ms;

sid_core #(.NS(NS), .LEGACY_FC(LEGACY_FC), .VARIANT_B(VARIANT_B), .PULSE_FIX(PULSE_FIX)) core (
    .clk(clk), .start(start_i), .res(res_q),
    .op_valid(op_valid_i), .op_we(op_we_i), .op_all(op_all_i), .op_sid(op_sid_i), .op_reg(op_reg_i),
    .op_data(op_wdata_i), .op_rdata(op_rdata_i),
    .model(model), .variant(variant), .res_hi(res_hi), .digi(digi),
    .fc_base(fc_base), .fc_offset(fc_offset), .fc_sel(fc_sel), .bypass(bypass), .ext_in({NS*22{1'b0}}),
    .voice_cycle(voice_cycle), .tick_ms(tick_ms),
    .audio_o(audio_o), .audio_valid_o(audio_valid),
    .osc3(osc3), .env3(env3), .dbus_value(dbus),
    .lv_addr(lv_addr), .lv_data(lv_data)
);
assign core_idle = voice_cycle == 6'd0;
assign busy = !core_idle;

genvar g;
generate
    for (g = 0; g < NS; g = g + 1) begin : rb
        assign rb_all[24*g +: 24] = { dbus[8*g +: 8], env3[8*g +: 8], osc3[8*g +: 8] };
    end
endgenerate

// ---- outputs: the two UltiSID sums (saturated to 20 bit) and the per-SID stream
reg signed [21:0] s0, s1;
always @(*) begin
    s0 = 22'sd0;
    s1 = 22'sd0;
    for (i = 0; i < NS; i = i + 1)
        if (i < 4) s0 = s0 + $signed(audio_o[20*i +: 20]);
        else       s1 = s1 + $signed(audio_o[20*i +: 20]);
end
function signed [19:0] sat20(input signed [21:0] x);
    sat20 = (x > 22'sd524287) ? 20'sh7ffff : (x < -22'sd524288) ? 20'sh80000 : x[19:0];
endfunction

reg [3:0] emit = 4'd8;     // 0..NS-1: sample of SID `emit` is on the output
always @(posedge clk) begin
    sum_valid <= audio_valid;
    if (audio_valid) begin
        sum_u0 <= sat20(s0);
        sum_u1 <= sat20(s1);
        emit   <= 4'd0;
    end else if (emit < NS) begin
        emit   <= emit + 4'd1;
    end
    smp_valid <= emit < NS;
    smp_sid   <= emit[2:0];
    smp_data  <= $signed(audio_o[20*emit[2:0] +: 20]);
end

endmodule
