// ----------------------------------------------------------------------------
// This file is part of reDIP SID, a MOS 6581/8580 SID FPGA emulation platform.
// Copyright (C) 2023  Dag Lem <resid@nimrod.no>
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

// SID control registers for NS chips (NS = 2: the reDIP SID, NS = 8: UltiSID
// level, see docs/specs/T25.4-redip-ultisid.md in the TRX64 Ultimate tree).
// Fully pipelined operation, shifting out registers for one voice per cycle.
//
// The CPU access is not decoded here: the caller presents one op per pipeline
// pass (op_*, stable for the pass), already decoded to a SID number.
module sid_control #(
    parameter NS = 2,
    localparam N = 3*NS,
    localparam DBUS_VALUE_TTL_MOS6581 = 10'd7,
    localparam DBUS_VALUE_TTL_MOS8580 = 10'd664
)(
    input  logic               clk,
    input  logic               tick_ms,
    input  sid::cycle_t        voice_cycle,
    input  logic               res,
    // The CPU access of the pass: SID number, register, data. A read
    // returned op_rdata (only OSC3/ENV3/POT reads change the bus value).
    input  logic               op_valid,
    input  logic               op_we,
    // The access goes to all SIDs (the SIDs share an address), or to op_sid.
    input  logic               op_all,
    input  logic [2:0]         op_sid,
    input  sid::reg5_t         op_reg,
    input  sid::reg8_t         op_data,
    input  sid::reg8_t         op_rdata,
    input  logic [NS-1:0]      model,
    // Write-only registers.
    output sid::freq_pw_t      freq_pw_1,
    output logic [2:0]         test,
    output logic [2:0]         sync,
    output sid::control_t      control_4,
    output sid::envelope_reg_t ereg_5,
    // Filter registers of the SID the filter pipeline is working on.
    input  logic [2:0]         freg_sid,
    output sid::filter_reg_t   freg,
    // Value on the data bus of each SID chip.
    output logic [NS*8-1:0]    dbus_value
);

    /* verilator lint_off LITENDIAN */
    typedef union packed {
        logic [0:6][7:0] bytes;
        sid::voice_reg_t regs;
    } voice_ctrl_t;

    /* verilator lint_on LITENDIAN */

    // Voice registers: a ring of N positions. v[0] is where a voice is written
    // and read for the oscillator (cycle j + 2 for voice j), v[1] and v[2]
    // are tapped for the synchronization test bits, v[3] feeds the waveform
    // generator and v[4] the envelope generator; v[5] .. v[N-2] only delay
    // (a shift register without taps), v[N-1] wraps around to v[0].
    voice_ctrl_t v [N];
    initial begin for (int i = 0; i < N; i++) v[i] = '0; end

    // Filter registers, one word per SID.
    sid::filter_reg_t f [NS];
    initial begin for (int i = 0; i < NS; i++) f[i] = '0; end

    // Voice and register of a voice register write.
    logic [1:0] vsel;     // voice 0..2 within the SID
    logic [2:0] vbyte;    // byte within the voice registers
    logic [5:0] vj;       // voice number 0..N-1 in ring order
    logic       vwrite;
    logic       fwrite;
    logic [1:0] fbyte;

    always_comb begin
        vsel   = (op_reg >= 5'd14) ? 2'd2 : (op_reg >= 5'd7) ? 2'd1 : 2'd0;
        vbyte  = 3'(op_reg - ((op_reg >= 5'd14) ? 5'd14 : (op_reg >= 5'd7) ? 5'd7 : 5'd0));
        vj     = 6'({ 1'b0, op_sid, 1'b0 } + { 2'b0, op_sid }) + 6'(vsel);
        vwrite = op_valid && op_we && op_reg < 5'd21;
        fwrite = op_valid && op_we && op_reg >= 5'd21 && op_reg <= 5'd24;
        fbyte  = 2'(op_reg - 5'd21);

        // Assign to / from ports.
        freq_pw_1 = v[0].regs.waveform.freq_pw;
        control_4 = v[3].regs.waveform.control;
        ereg_5    = v[4].regs.envelope;
        test      = { v[2].regs.waveform.control.test, v[1].regs.waveform.control.test, v[0].regs.waveform.control.test };
        sync      = { v[2].regs.waveform.control.sync, v[1].regs.waveform.control.sync, v[0].regs.waveform.control.sync };
        freg      = f[freg_sid[$clog2(NS > 1 ? NS : 2)-1:0]];
    end

    // Write-only voice registers.
    always_ff @(posedge clk) begin
        // Rotation on cycles 1 - 2N for writing. A second complete
        // freewheeling rotation keeps in sync with sid_waveform.sv and
        // sid_envelope.sv.
        if (voice_cycle >= 1 && voice_cycle <= 2*N) begin
            for (int i = 1; i < N; i++) begin
                v[i] <= v[i-1];
            end
            v[0] <= v[N-1];

            if (res) begin
                v[0].bytes <= '0;
            end else if (vwrite && voice_cycle <= N &&
                         (op_all ? 2'((voice_cycle - 6'd1) % 6'd3) == vsel : vj == 6'(voice_cycle - 1))) begin
                // Voice register write, when the voice is on its way into v[0].
                v[0].bytes[vbyte] <= op_data;
            end
        end

        // Filter registers: written or cleared by reset one SID per cycle.
        if (voice_cycle >= 1 && voice_cycle <= NS) begin
            if (res) begin
                f[voice_cycle - 1] <= '0;
            end else if (fwrite && (op_all || 4'(op_sid) == 4'(voice_cycle - 1))) begin
                f[voice_cycle - 1][8*(3 - fbyte) +: 8] <= op_data;
            end
        end
    end

    // Value on data bus for each SID chip, updated one SID per cycle
    // (cycle s + 1 for SID s). The age of the value is counted in ms ticks.
    logic [7:0] bval [NS];
    initial begin for (int i = 0; i < NS; i++) bval[i] = '0; end
    logic [9:0] bage [NS];
    initial begin for (int i = 0; i < NS; i++) bage[i] = '0; end

    // tick_ms is a level that changes at cycle 1: the SID at cycle 1 sees the
    // previous pass's tick, the others (as in reDIP, where all see it at cycle 1)
    // see the same value from the register.
    logic       tick_c1 = 0;
    always_ff @(posedge clk) begin
        if (voice_cycle == 1) begin
            tick_c1 <= tick_ms;
        end
    end

    logic [3:0] bs;
    logic [9:0] bage_cur;
    logic [9:0] bage_inc;
    logic [9:0] bttl;
    logic       bhit;

    always_comb begin
        bs       = 4'(voice_cycle - 1);
        bage_cur = bage[bs[$clog2(NS > 1 ? NS : 2)-1:0]];
        bage_inc = bage_cur + 10'(voice_cycle == 1 ? tick_ms : tick_c1);
        bttl     = model[bs[$clog2(NS > 1 ? NS : 2)-1:0]] == sid::MOS6581 ?
                   DBUS_VALUE_TTL_MOS6581 : DBUS_VALUE_TTL_MOS8580;
        // Keep last register read/write on data bus. Only reads of
        // POTX/POTY/OSC3/ENV3 return a new value.
        bhit     = op_valid && (op_all || 4'(op_sid) == bs) &&
                   (op_we || (op_reg >= 5'd25 && op_reg <= 5'd28));
    end

    always_ff @(posedge clk) begin
        if (voice_cycle >= 1 && voice_cycle <= NS) begin
            if (res) begin
                bval[bs[$clog2(NS > 1 ? NS : 2)-1:0]] <= '0;
                bage[bs[$clog2(NS > 1 ? NS : 2)-1:0]] <= '0;
            end else if (bhit) begin
                bval[bs[$clog2(NS > 1 ? NS : 2)-1:0]] <= op_we ? op_data : op_rdata;
                bage[bs[$clog2(NS > 1 ? NS : 2)-1:0]] <= 0;
            end else if (bage_cur == bttl) begin
                // Bus value has faded out.
                bval[bs[$clog2(NS > 1 ? NS : 2)-1:0]] <= 0;
            end else begin
                // Count up to complete fade-out of bus value.
                bage[bs[$clog2(NS > 1 ? NS : 2)-1:0]] <= bage_inc;
                if (bage_inc == bttl) begin
                    bval[bs[$clog2(NS > 1 ? NS : 2)-1:0]] <= 0;
                end
            end
        end
    end

    always_comb begin
        for (int i = 0; i < NS; i++) begin
            dbus_value[i*8 +: 8] = bval[i];
        end
    end
endmodule
