// ---------------------------------------------------------------------------
// Copyright 2026 Mateusz Nalewajski
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// ---------------------------------------------------------------------------

// this module wraps either 6502N CPU core by Gideon Zweijtzer
// or T65 (multiple authors, listed in T65/T65.vhd)

// (vendor) `default_nettype none
`timescale 1 ns / 1 ps
module cpu6510 #(
  parameter CPU_MODEL = 1,
  parameter [18:0] FALL_OFF_CYCLES = 19'd350000
) (
  input  wire clk,          // the C64's (gated) clock: PHI2-side registers
  input  wire reset,

  // T29 (docs/specs/T29-turbo.md §12.3): the 6502N core and the CPU port
  // run on clk2x (2 x clk, same MMCM, not gated) with clock enables. g_edge:
  // this clk2x edge is one a gclk edge passes on; run2: any clk2x edge that
  // passes; at normal speed the core's enable is g_edge && phi2_l (the edge
  // of today) and skip_pl swallows the first phi2_l after a turbo span.
  // t_mode: the turbo engine drives the core: t_ce its clock enable, t_sel
  // that this cycle's data is t_din (a fast access) and not din_r.
  input  wire clk2x,
  input  wire g_edge,
  input  wire run2,
  input  wire skip_pl,
  input  wire t_mode,
  input  wire t_ce,
  input  wire t_sel,
  input  wire [7:0] t_din,
  output wire [7:0] dout_early,     // dout without the late sources (c64_turbo.v), and
  output wire       dout_late,      // whether this cycle's byte is one of them
  output wire [7:0] din_r_o,        // the byte the bus left at the last PHI2 fall (a slow access's data)
  output reg  [2:0] pout3_q,        // pout[2:0] as a register: what pout[2:0] is after the clk2x edge that updates pdir/pout_i

  input  wire rdy,
  input  wire irq,
  input  wire nmi,
  output wire nmi_ack,

  input  wire clk_phi,
  input  wire phi2_l,
  input  wire phi2_n,

  input  wire [ 7:0] din,
  output wire [ 7:0] dout,
  output wire [15:0] addr,
  output wire        we,

  input  wire [5:0] pin,
  output wire [5:0] pout,

  output wire [15:0] pc,

  // the registers for the state bus (c64_core.v): A, X, Y, S, P, opcode
  // fetch cycle, CPU port direction ($00), output latch ($01), $01 as read
  output wire [7:0] st_a, st_x, st_y, st_s, st_p,
  output wire       st_sync,
  output wire       st_intg,     // 6502N: an interrupt is decided (this fetch is dropped)
  output wire [7:0] st_pdir, st_pout, st_pread,
  // {P, S, Y, X, A} as the core's next update leaves them (6502N
  // proc_registers nx_regs): stopped at an instruction boundary, the
  // registers with the previous instruction's write-back done
  output wire [39:0] st_nx,
  // state access (c64_core.v, ARCH-lean-state-access §5): every flip-flop of
  // the CPU as one vector, 129:0 the 6502N core (proc_core.vhd), 137:130
  // din_r, 143:138 pin_r, 151:144 $00, 159:152 $01 latch, 167:160 floating
  // bits, 186:168 / 205:187 their fall-off counters (bits 6, 7), 206 rdy_r,
  // 207 irq_r, 208 nmi_r; sb_ld loads all of them on the next clock
  input  wire         sb_ld,
  input  wire [208:0] sb_stg,
  output wire [208:0] sb_cur,
  // the app's DMA while the CPU is held (T16 Q8): dp_we writes $00 (dp_hi 0) or
  // $01 (dp_hi 1) of the CPU port with dp_data. The first write of a hold keeps
  // the port as it was (port, floating bits and their fall-off counters);
  // dp_restore (the hold ends) puts it back, so the program's port comes back
  // exactly. A state load (sb_ld) or a reset takes the saved copy away.
  input  wire         dp_we,
  input  wire         dp_hi,
  input  wire   [7:0] dp_data,
  input  wire         dp_restore
);

wire access_io;
wire select_pdir;

wire rdy_latched;
wire irq_latched;
wire nmi_latched;

wire [7:0] din_i;
wire       rw_i;

reg rdy_r = 1'b0;
reg irq_r = 1'b0;
reg nmi_r = 1'b0;

reg [7:0] din_r;
reg [5:0] pin_r;

// The port registers are 8 bits wide. Bits 3, 6 and 7 float while they are
// inputs (no pin for 6/7, nothing on the board drives 3) and read back the
// level last driven; 6 and 7 fall to 0 after a while. Bit 5 reads low as an
// input (the motor line's pull-down).
reg [7:0] pdir   = 8'h00;
reg [7:0] pout_i = 8'hFF;
reg [7:0] pfloat = 8'h00;
reg [18:0] fall6 = 19'd0;
reg [18:0] fall7 = 19'd0;

wire [7:0] pread = (pout_i & pdir) | ({pfloat[7:6], 1'b0, pin_r[4], pfloat[3], pin_r[2:0]} & ~pdir);

assign access_io   = (addr[15:1] == 15'b0);
assign select_pdir = (addr[0]    == 1'b0);

// the early sources (the write cycle's own byte, the port, the bus's din_r) first, the
// turbo engine's late one (block RAM bytes) in the last mux
wire [7:0] din_early = we ? dout : access_io ? (select_pdir ? pdir : pread) : din_r;
assign din_i = (we || access_io || !t_sel) ? din_early : t_din;

assign pout = (pout_i[5:0] & pdir[5:0]) | (pin_r & ~pdir[5:0]);
assign din_r_o = din_r;

assign we = !rw_i;

assign st_pdir  = pdir;
assign st_pout  = pout_i;
assign st_pread = pread;

always @(posedge clk) begin
  if (reset) begin
    rdy_r <= 1'b0;
    irq_r <= 1'b0;
    nmi_r <= 1'b0;
  end else if (phi2_n) begin
    rdy_r <= rdy;
    irq_r <= irq;
    nmi_r <= nmi;

    din_r <= din;
    pin_r <= pin;
  end
  if (sb_ld) begin
    din_r <= sb_stg[137:130];
`ifdef SB_FULL  // else sampled again from the pins on the load edge
    pin_r <= sb_stg[143:138];
`endif
    rdy_r <= sb_stg[206];
    irq_r <= sb_stg[207];
    nmi_r <= sb_stg[208];
  end
end

// turbo: the lines are the core's own, taken at its own rate (spec §10a Q6)
assign rdy_latched = t_mode ? 1'b1 : (clk_phi ? rdy : rdy_r);
assign irq_latched = t_mode ? irq  : (clk_phi ? irq : irq_r);
assign nmi_latched = t_mode ? nmi  : (clk_phi ? nmi : nmi_r);
wire   core_ce     = t_mode ? t_ce : (g_edge && phi2_l && !skip_pl);

// the port registers are written in the write cycle: at normal speed while
// PHI2 is high, in turbo at the step that ends the cycle
// pout[2:0] of the state the next edge leaves (the same write condition as below): the turbo engine's
// PLA decodes a register and not the port's two LUT levels in front of the address (c64_turbo.v)
wire       port_wr = (t_mode ? t_ce : (clk_phi && run2)) && we && access_io;
wire [7:0] pdir_n  = reset ? 8'h00 : (sb_ld && g_edge) ? sb_stg[151:144] : (port_wr && select_pdir)  ? dout : pdir;
wire [7:0] pout_n  = reset ? 8'hFF : (sb_ld && g_edge) ? sb_stg[159:152] : (port_wr && !select_pdir) ? dout : pout_i;
always @(posedge clk2x)
  pout3_q <= (pout_n[2:0] & pdir_n[2:0]) | (pin_r[2:0] & ~pdir_n[2:0]);

// T16 Q8 on clk2x: dp_we/dp_restore come from the gclk domain, so they count only on the clk2x edges a
// gclk edge passes (g_edge): the same moments the port saw them when it ran on clk. The turbo engine
// is off while the app's DMA writes the port (a stop leaves turbo first), pout3_q follows a clk2x later.
reg         pt_valid = 1'b0;
reg  [61:0] pt_sv = 62'd0;      // {$00, $01 latch, floating bits, fall6, fall7}
wire        dpw = g_edge && dp_we;

always @(posedge clk2x) begin
  if (reset) begin
    pdir   <= 8'h00;
    pout_i <= 8'hFF;
  end else if (dpw) begin
    if (dp_hi)
      pout_i <= dp_data;
    else
      pdir <= dp_data;
  end else if ((t_mode ? t_ce : (clk_phi && run2)) && we && access_io) begin
    if (select_pdir)
      pdir <= dout;
    else
      pout_i <= dout;
  end
  if (dpw && !pt_valid) begin
    pt_sv    <= {pdir, pout_i, pfloat, fall6, fall7};
    pt_valid <= 1'b1;
  end
  if (g_edge && dp_restore && pt_valid) begin
    pdir     <= pt_sv[61:54];
    pout_i   <= pt_sv[53:46];
    pt_valid <= 1'b0;
  end
  if (sb_ld && g_edge) begin
    pdir   <= sb_stg[151:144];
    pout_i <= sb_stg[159:152];
  end
  if ((sb_ld && g_edge) || reset) pt_valid <= 1'b0;
end

always @(posedge clk) begin
  if (reset) begin
    pfloat <= 8'h00;
  end else if (phi2_n) begin
    pfloat[3] <= pdir[3] ? pout_i[3] : pfloat[3];
    pfloat[6] <= pdir[6] ? pout_i[6] : pfloat[6] && fall6 != 0;
    pfloat[7] <= pdir[7] ? pout_i[7] : pfloat[7] && fall7 != 0;
    fall6 <= pdir[6] ? FALL_OFF_CYCLES : fall6 - (fall6 != 0);
    fall7 <= pdir[7] ? FALL_OFF_CYCLES : fall7 - (fall7 != 0);
  end
  if (dp_restore && pt_valid) begin
    pfloat <= pt_sv[45:38];
    fall6  <= pt_sv[37:19];
    fall7  <= pt_sv[18:0];
  end
  if (sb_ld) begin
    pfloat <= sb_stg[167:160];
    fall6  <= sb_stg[186:168];
    fall7  <= sb_stg[205:187];
  end
end

wire [129:0] core_cur;
assign sb_cur = {nmi_r, irq_r, rdy_r, fall7, fall6, pfloat, pout_i, pdir, pin_r, din_r, core_cur};

generate
  if (CPU_MODEL) begin : CPU_6502N
`ifdef SB_FULL
`ifndef VERILATOR
    // every flip-flop loaded (A/B against the subset); the sims set the
    // generic in their ghdl synth (-gg_sb_full=true) instead
    proc_core #(.g_sb_full(1)) cpu (
`else
    proc_core cpu (
`endif
`else
    proc_core cpu (
`endif
      .clock(clk2x),
      .clock_en(core_ce),
      .reset(reset),
      .ready(rdy_latched),
      .irq_n(~irq_latched),
      .nmi_n(~nmi_latched),
      .so_n(1'b1),
      .addr_out(addr),
      .data_in(din_i),
      .data_out(dout),
      .data_early(dout_early), .data_late(dout_late),
      .read_write_n(rw_i),
      .interrupt_ack(nmi_ack),
      .pc_out(pc),
      .sync_out(st_sync), .intg_out(st_intg),
      .sb_ld(sb_ld && g_edge), .sb_stg(sb_stg[129:0]), .sb_cur(core_cur),
      .a_out(st_a), .x_out(st_x), .y_out(st_y), .s_out(st_s), .p_out(st_p),
      .nx_out(st_nx)
    );
  end else begin : CPU_T65
    assign pc = 15'b0;
    assign dout_early = dout;
    assign dout_late  = 1'b0;
    assign {st_a, st_x, st_y, st_s, st_p, st_sync, st_intg} = 42'd0;
    assign st_nx = 40'd0;
    assign core_cur = 130'd0;

    T65 cpu (
      .Mode(2'b00),
      .Res_n(~reset),
      .Enable(core_ce),
      .Clk(clk2x),
      .Rdy(rdy_latched),
      .Abort_n(1'b1),
      .IRQ_n(~irq_latched),
      .NMI_n(~nmi_latched),
      .SO_n(1'b1),
      .R_W_n(rw_i),
      .A(addr),
      .DI(din_i),
      .DO(dout),
      .NMI_ack(nmi_ack)
    );
  end
endgenerate

endmodule
`default_nettype wire
// vim:ts=2 sw=2 tw=120 et
