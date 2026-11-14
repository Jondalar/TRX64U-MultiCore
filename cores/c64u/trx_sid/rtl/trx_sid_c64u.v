// SPDX-License-Identifier: GPL-3.0-or-later
// ---------------------------------------------------------------------------
// trx_sid_c64u: the C64 side of the T25.1 SID engine (TRX64-Ultimate/docs/specs/T25.1-sid-engine.md
// §3.1): the app's UltiSID addressing (two bases and masks, the shared split),
// the bus read path, POT, the op latch per C64 cycle and the pass start for
// trx_sid_engine_c64u, which runs on clk2x (2 x dot4x, same MMCM, phase-locked).
//
// Bus (clk = the C64's gated dot4x): the arbiter's SID select covers
// $D400-$D7FF; addr = A9..A0. A10/A11 are 01 there, so a base outside
// $D400-$D7FF never hits (spec Q1: bases in $DE00-$DFFF are not decoded).
//   hit_u = ({01, A9..A4} & MASK_u) == BASE_u   (base compared unmasked)
//   UltiSID 1 wins when both hit (Q2); no hit: `hit` low, the arbiter keeps
//   the open bus.
//   split part p from C64_EMUSID_SPLIT: 0 none, 1 A5, 2 A6, 3 A7, 4 A8,
//   5 A6:A5, 6 A8:A5, 7 A8:A7 (LSB first); SID = 4*u + p.
// The CPU op of a cycle is latched at phi2_n (the C64 cycle's end) together
// with what a read returned; the engine's pass for the next cycle applies it,
// and that pass's read-back is what the next cycle's read sees.
// ---------------------------------------------------------------------------
`timescale 1 ns / 1 ps
module trx_sid_c64u #(
    parameter MEMDIR = "."
) (
    input  wire        clk,                 // C64 dot4x (gated)
    input  wire        rst,                 // SID /RES (clk)
    input  wire        clk2x,               // 2 x dot4x, not gated
    input  wire        phi2_n,              // end of a C64 cycle (clk, one clock)
    // bus
    input  wire        cen,                 // arbiter's SID select, low active ($D400-$D7FF)
    input  wire        we,
    input  wire  [9:0] addr,
    input  wire  [7:0] din,
    output wire  [7:0] dout,
    output wire        hit,                 // an UltiSID answers this access
    // T29: the turbo CPU's fast SID access (c64_turbo_c64u.v). A write is pushed into an op FIFO that feeds the op latch below
    // one op per C64 cycle (a bus op of the same cycle goes first); ps_* is a two clk2x clock wide pulse (seen by exactly one clk
    // edge) with the address and byte, ra the CPU's address for the decode and the read (fhit, frd).
    input  wire        ps_v,
    input  wire  [9:0] ps_a,
    input  wire  [7:0] ps_d,
    input  wire  [9:0] ra,
    output wire        fhit,
    output wire  [7:0] frd,
    output wire        f_full,              // 24 or more ops queued
    output wire        f_empty,
    output wire        f_quiet,             // empty, and the last op has been in the engine for two C64 cycles: a read sees it
    input  wire        pot_x,               // POT lines charged (joystick/paddle side)
    input  wire        pot_y,
    output wire        pot_dis,             // T103: the POT discharge phase (the first 256 cycles of the 512), for the paddle pins
    // app configuration (any clock, quasi-static: synchronized here)
    input  wire [15:0] cfg_base,            // {UltiSID 2, UltiSID 1} A11..A4
    input  wire [15:0] cfg_mask,
    input  wire  [2:0] cfg_split,
    input  wire  [1:0] cfg_8580,            // WAVES per UltiSID
    input  wire  [1:0] cfg_res_high,
    input  wire  [3:0] cfg_digi,
    // T25.5 filter control (reSID8): {bypass 2, offset 2 x 11, base 2 x 9, auto 2 x 2, filter source 2 x 2}
    input  wire [49:0] cfg_rs8,
    // filter curve RAM writes (app clock): only the T25.1 engine uses them; the 8-SID reDIP has no table (T25.5)
    input  wire        cv_clk,
    input  wire        cv_we,
    input  wire [11:0] cv_addr,
    input  wire  [7:0] cv_wdata,
    // envelope levels (asynchronous read, app side)
    input  wire  [4:0] lv_addr,
    output wire  [7:0] lv_data,
    // samples (clk2x)
    output wire        smp_valid,
    output wire  [2:0] smp_sid,
    output wire signed [19:0] smp_data,
    output wire        sum_valid,
    output wire signed [19:0] sum_u0,
    output wire signed [19:0] sum_u1
);

// ---- configuration into clk (decode) and clk2x (engine)
(* ASYNC_REG = "TRUE" *) reg [34:0] cfg_c1, cfg_c;
(* ASYNC_REG = "TRUE" *) reg  [7:0] cfg_e1, cfg_e;
(* ASYNC_REG = "TRUE" *) reg [49:0] cfg_r1, cfg_r;
always @(posedge clk) begin
    cfg_c1 <= {cfg_split, cfg_mask, cfg_base};
    cfg_c  <= cfg_c1;
end
always @(posedge clk2x) begin
    cfg_e1 <= {cfg_digi, cfg_res_high, cfg_8580};
    cfg_e  <= cfg_e1;
    cfg_r1 <= cfg_rs8;
    cfg_r  <= cfg_r1;
end
wire [7:0] base0 = cfg_c[7:0], base1 = cfg_c[15:8];
wire [7:0] mask0 = cfg_c[23:16], mask1 = cfg_c[31:24];
wire [2:0] split = cfg_c[34:32];

// ---- decode
wire [7:0] a11_4 = {2'b01, addr[9:4]};
wire       hit0  = !cen && ((a11_4 & mask0) == base0);
wire       hit1  = !cen && ((a11_4 & mask1) == base1);
assign hit = hit0 || hit1;
reg  [1:0] part;
always @(*)
    case (split)
    3'd1: part = {1'b0, addr[5]};
    3'd2: part = {1'b0, addr[6]};
    3'd3: part = {1'b0, addr[7]};
    3'd4: part = {1'b0, addr[8]};
    3'd5: part = {addr[6], addr[5]};
    3'd6: part = {addr[8], addr[5]};
    3'd7: part = {addr[8], addr[7]};
    default: part = 2'd0;
    endcase
wire [2:0] sid = {!hit0, part};

// the same decode for the fast path (the CPU's address ra, a pushed write's address ps_a)
function [2:0] fsel(input [9:0] a, input [7:0] m0, input [7:0] b0, input [2:0] sp);
    reg h0; reg [1:0] pt;
    begin
        h0 = ({2'b01, a[9:4]} & m0) == b0;
        case (sp)
        3'd1: pt = {1'b0, a[5]};
        3'd2: pt = {1'b0, a[6]};
        3'd3: pt = {1'b0, a[7]};
        3'd4: pt = {1'b0, a[8]};
        3'd5: pt = {a[6], a[5]};
        3'd6: pt = {a[8], a[5]};
        3'd7: pt = {a[8], a[7]};
        default: pt = 2'd0;
        endcase
        fsel = {!h0, pt};
    end
endfunction
wire       rh0 = ({2'b01, ra[9:4]} & mask0) == base0, rh1 = ({2'b01, ra[9:4]} & mask1) == base1;
wire [2:0] rsid = fsel(ra, mask0, base0, split);
assign fhit = rh0 || rh1;
wire       ph0 = ({2'b01, ps_a[9:4]} & mask0) == base0, ph1 = ({2'b01, ps_a[9:4]} & mask1) == base1;
wire [2:0] psid = fsel(ps_a, mask0, base0, split);
wire       phit = ph0 || ph1;

// ---- POT: 512-cycle periods, 256 discharging, then count until charged
reg  [8:0] pc = 9'd0;
assign pot_dis = !pc[8];
reg  [7:0] px_acc = 8'hff, py_acc = 8'hff, potx = 8'hff, poty = 8'hff;
reg        px_done = 1'b0, py_done = 1'b0;
always @(posedge clk) begin
    if (phi2_n) begin
        pc <= pc + 9'd1;
        if (!pc[8]) begin
            px_acc <= 8'hff; px_done <= 1'b0;
            py_acc <= 8'hff; py_done <= 1'b0;
        end else begin
            if (!px_done && pot_x) begin px_acc <= pc[7:0]; px_done <= 1'b1; end
            if (!py_done && pot_y) begin py_acc <= pc[7:0]; py_done <= 1'b1; end
        end
        if (pc == 9'd511) begin
            potx <= px_acc;
            poty <= py_acc;
        end
    end
end

// ---- read path
// read-back: the engine's registers (clk2x) copied into clk registers every
// clock, so the bus read path is clk only. The engine changes them in the
// first 46 clk2x clocks of a C64 cycle; the CPU samples at its end.
wire [191:0] rb_all;
reg  [191:0] rb_c = 192'd0;
always @(posedge clk) rb_c <= rb_all;
wire [23:0] rb_s = rb_c[24*sid +: 24];
wire [7:0] rb_osc3 = rb_s[7:0], rb_env3 = rb_s[15:8], rb_bus = rb_s[23:16];
wire       std_pot = addr[9:5] == 5'd0;     // $D419/$D41A themselves
reg  [7:0] rd;
always @(*)
    case (addr[4:0])
    5'h19: rd = std_pot ? potx : 8'hff;
    5'h1a: rd = std_pot ? poty : 8'hff;
    5'h1b: rd = rb_osc3;
    5'h1c: rd = rb_env3;
    default: rd = rb_bus;
    endcase
assign dout = rd;

// the fast read: the same bytes for the CPU's address
wire [23:0] frb_s = rb_c[24*rsid +: 24];
reg  [7:0] frd_r;
always @(*)
    case (ra[4:0])
    5'h19: frd_r = ra[9:5] == 5'd0 ? potx : 8'hff;
    5'h1a: frd_r = ra[9:5] == 5'd0 ? poty : 8'hff;
    5'h1b: frd_r = frb_s[7:0];
    5'h1c: frd_r = frb_s[15:8];
    default: frd_r = frb_s[23:16];
    endcase
assign frd = frd_r;

// ---- op FIFO (T29) and the op latch at the end of each C64 cycle; a toggle starts the pass
// The FIFO holds the turbo CPU's fast writes. Only the fast push writes it (one write port: the CPU's bus ops are
// never queued; the engine stays in turbo until the FIFO is empty, so a bus op does not meet queued ops except an
// app DMA, which goes first). It is read at the end of a C64 cycle that has no bus op.
reg [23:0] fq [0:31];                    // {sid 3, reg 5, wdata 8, rdata 8} per entry, we always 1
reg  [5:0] wp = 6'd0, rp = 6'd0;
wire [5:0] fcnt = wp - rp;
assign f_empty = fcnt == 6'd0;
assign f_full  = fcnt >= 6'd24;
reg  [1:0] qc = 2'd3;
assign f_quiet = f_empty && qc == 2'd3;
wire       fpush = ps_v && phit;
wire       fpop  = phi2_n && !hit && !f_empty;
wire [23:0] fhead = fq[rp[4:0]];
// A write to the register the last queued op wrote replaces that op's byte (a loop of writes to one register, digi
// volume or a sweep, would otherwise run at the drain rate of one op per C64 cycle); the registers whose writes are events
// (the voice controls: gate, test, sync, ring; AD and SR: the ADSR delay bug) are never merged. Not with only one op
// queued: that one may be popped on this very clock.
wire [23:0] ftail = fq[wp[4:0] - 5'd1];
wire        fev   = ps_a[4:0] == 5'h04 || ps_a[4:0] == 5'h05 || ps_a[4:0] == 5'h06 ||
                    ps_a[4:0] == 5'h0B || ps_a[4:0] == 5'h0C || ps_a[4:0] == 5'h0D ||
                    ps_a[4:0] == 5'h12 || ps_a[4:0] == 5'h13 || ps_a[4:0] == 5'h14;
wire        fmerge = fcnt >= 6'd2 && ftail[23:16] == {psid, ps_a[4:0]} && !fev;
always @(posedge clk) begin
    if (fpush) begin
        fq[fmerge ? wp[4:0] - 5'd1 : wp[4:0]] <= {psid, ps_a[4:0], ps_d, 8'd0};
        if (!fmerge) wp <= wp + 6'd1;
    end
    if (fpop) rp <= rp + 6'd1;
    if (fpop) qc <= 2'd0;
    else if (phi2_n && qc != 2'd3) qc <= qc + 2'd1;
    if (rst) begin wp <= 6'd0; rp <= 6'd0; qc <= 2'd3; end
end
reg        op_valid = 1'b0, op_we = 1'b0;
reg  [2:0] op_sid = 3'd0;
reg  [4:0] op_reg = 5'd0;
reg  [7:0] op_wdata = 8'd0, op_rdata = 8'd0;
reg        op_tgl = 1'b0;
always @(posedge clk) begin
    if (phi2_n) begin
        if (fpop) begin
            op_valid <= 1'b1;
            op_we    <= 1'b1;
            {op_sid, op_reg, op_wdata, op_rdata} <= fhead;
        end else begin
            op_valid <= hit;
            op_we    <= we;
            op_sid   <= sid;
            op_reg   <= addr[4:0];
            op_wdata <= din;
            op_rdata <= rd;
        end
        op_tgl   <= !op_tgl;
    end
    if (rst) op_valid <= 1'b0;
end
// clk -> clk2x: op_tgl is taken into a clk2x register first; start comes one
// clk2x clock after that, and the engine takes the op_* registers (clk, stable
// for a whole C64 cycle after phi2_n) in that clock: every crossing is
// register to register (both clocks are mmcm_sys outputs, edge-aligned).
reg  tgl_s = 1'b0, tgl_d = 1'b0;
always @(posedge clk2x) begin
    tgl_s <= op_tgl;
    tgl_d <= tgl_s;
end
wire start = tgl_s != tgl_d;
// the reset: held in clk, seen in clk2x
reg  [1:0] rst_e = 2'b11;
always @(posedge clk2x) rst_e <= {rst_e[0], rst};

// SID_MULTI: reDIP's pipelines for 8 SIDs (sid_multi, TRX64-Ultimate/docs/specs/T25.4-redip-ultisid.md)
// instead of the T25.1 engine
`ifdef SID_MULTI
`ifdef SID_MULTI_EQUIV
    // the bench against the original reDIP (host/sim/redip): its cutoff
    // curve, variant B, its pulse behaviour. Never in a build.
    sid_multi #(.NS(8), .LEGACY_FC(1), .VARIANT_B(1), .PULSE_FIX(0), .MEMDIR(MEMDIR)) eng (
`else
    sid_multi #(.NS(8), .LEGACY_FC(2), .VARIANT_B(0), .PULSE_FIX(0), .MEMDIR(MEMDIR)) eng (
`endif
        .clk(clk2x), .rst(rst_e[1]), .start(start),
        .op_valid_i(op_valid), .op_we_i(op_we), .op_all_i(1'b0), .op_sid_i(op_sid), .op_reg_i(op_reg),
        .op_wdata_i(op_wdata), .op_rdata_i(op_rdata),
        .cfg_8580(cfg_e[1:0]), .cfg_res_high(cfg_e[3:2]), .cfg_digi(cfg_e[7:4]),
        .cfg_fc_sel(cfg_r[3:0]), .cfg_auto(cfg_r[7:4]), .cfg_fc_base(cfg_r[25:8]), .cfg_fc_off(cfg_r[47:26]), .cfg_bypass(cfg_r[49:48]),
        .rb_all(rb_all),
        .lv_addr(lv_addr), .lv_data(lv_data),
        .smp_valid(smp_valid), .smp_sid(smp_sid), .smp_data(smp_data),
        .sum_valid(sum_valid), .sum_u0(sum_u0), .sum_u1(sum_u1),
        .busy()
    );
`else
    trx_sid_engine_c64u #(.MEMDIR(MEMDIR)) eng (
        .clk(clk2x), .rst(rst_e[1]), .start(start),
        .op_valid_i(op_valid), .op_we_i(op_we), .op_sid_i(op_sid), .op_reg_i(op_reg),
        .op_wdata_i(op_wdata), .op_rdata_i(op_rdata),
        .cfg_8580(cfg_e[1:0]), .cfg_res_high(cfg_e[3:2]), .cfg_digi(cfg_e[7:4]),
        .cv_clk(cv_clk), .cv_we(cv_we), .cv_addr(cv_addr), .cv_wdata(cv_wdata),
        .rb_sid(3'd0), .rb_osc3(), .rb_env3(), .rb_bus(), .rb_all(rb_all),
        .lv_addr(lv_addr), .lv_data(lv_data),
        .smp_valid(smp_valid), .smp_sid(smp_sid), .smp_data(smp_data),
        .sum_valid(sum_valid), .sum_u0(sum_u0), .sum_u1(sum_u1),
        .busy(), .st_addr(9'd0), .st_we(1'b0), .st_wdata(32'd0), .st_rdata()
    );
`endif

endmodule
