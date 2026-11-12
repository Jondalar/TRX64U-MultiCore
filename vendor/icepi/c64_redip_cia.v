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

// ---------------------------------------------------------------------------
// this module is just a wrapper for
// reDIP CIA, a MOS 6526/8520/8521 FPGA emulation platform.
// Copyright (C) 2025  Dag Lem <resid@nimrod.no>
// ---------------------------------------------------------------------------

// (vendor) `default_nettype none
`timescale 1 ns / 1 ps
module c64_redip_cia #(
  parameter GENERATE_TOD  = 1,
  parameter NTSC          = 0,
  parameter CLK_FREQUENCY = 31_527_777
) (
  input wire clk,
  input wire reset,

  input wire clk_phi,
  input wire phi2_p,
  input wire phi2_n,

  input wire cen,
  input wire we,

  input  wire [3:0] addr,
  input  wire [7:0] din,
  output wire [7:0] dout,

  input  wire [7:0] pa_in,
  output wire [7:0] pa_out,
  output wire [7:0] ddra,

  input  wire [7:0] pb_in,
  output wire [7:0] pb_out,
  output wire [7:0] ddrb,

  input  wire sp_in,
  output wire sp_out,

  input  wire cnt_in,
  output wire cnt_out,

  input  wire flag_n,
  output wire pc_n,

  output wire irq,

  output reg tod_out,
  input wire tod_in,

  input wire cia_model,
  // TRX64 (T40): the clock's rate in Hz at run time (the System Mode's dot4x)
  // and TOD at 60 Hz (the 60 Hz modes) instead of CLK_FREQUENCY and NTSC
  input wire [24:0] clk_freq,
  input wire tod60,

  // the 16 registers as a read returns them, register 0 in bits 127:120
  output wire [127:0] sb_regs,
  // state access (c64_core.v): every flip-flop as one vector, 336:0
  // cia_core's, 362:337 the 50 Hz TOD divider {tod_out, tod_cnt} (CIA 1;
  // 0 without GENERATE_TOD); sb_ld loads all of them on the next clock
  input  wire         sb_ld,
  input  wire [362:0] sb_stg,
  output wire [362:0] sb_cur
);

wire tod_clk;

generate
  if (GENERATE_TOD) begin
    localparam integer TOD_CNT_WIDTH = 25;  // up to 33.5 MHz
    wire [TOD_CNT_WIDTH-1:0] TOD_INCREMENT = (NTSC || tod60) ? 25'd120 : 25'd100;
    wire [TOD_CNT_WIDTH-1:0] CLK_HZ = clk_freq;

    reg [TOD_CNT_WIDTH-1:0] tod_cnt;

    always @(posedge clk) begin
      if (reset) begin
        tod_out <= 1'b0;
        tod_cnt <= 0;

      end else begin
        tod_cnt <= tod_cnt + TOD_INCREMENT;

        if (tod_cnt >= CLK_HZ) begin
          tod_cnt <= tod_cnt - CLK_HZ + TOD_INCREMENT;
          tod_out <= ~tod_out;
        end
      end
      if (sb_ld) {tod_out, tod_cnt} <= sb_stg[337 +: TOD_CNT_WIDTH + 1];
    end

    assign tod_clk = tod_out;
    assign sb_cur[362:337] = {tod_out, tod_cnt};
  end else begin
    assign tod_clk = tod_in;
    assign sb_cur[362:337] = 26'd0;
  end
endgenerate

wire irq_n;

assign irq = ~irq_n;

cia_core cia_core_0 (
  .clk(clk),
  .phi2_up(phi2_p),
  .phi2_dn(phi2_n),
  .rst(reset),
  .cia_model(cia_model),
  .bus_i_phi2(clk_phi),
  .bus_i_res_n(~reset),
  .bus_i_cs_n(cen),
  .bus_i_r_w_n(~we),
  .bus_i_addr(addr),
  .bus_i_data(din),
  .bus_i_pa(pa_in),
  .bus_i_pb(pb_in),
  .bus_i_flag_n(flag_n),
  .bus_i_tod(tod_clk),
  .bus_i_cnt(cnt_in),
  .bus_i_sp(sp_in),
  .bus_o_data(dout),
  .bus_o_ports_pra(pa_out),
  .bus_o_ports_prb(pb_out),
  .bus_o_ports_ddra(ddra),
  .bus_o_ports_ddrb(ddrb),
  .bus_o_pc_n(pc_n),
  .bus_o_cnt(cnt_out),
  .bus_o_sp(sp_out),
  .bus_o_irq_n(irq_n),
  .sb_regs(sb_regs),
  .sb_ld(sb_ld),
  .sb_stg(sb_stg[336:0]),
  .sb_cur(sb_cur[336:0])
);

endmodule
`default_nettype wire
// vim:ts=2 sw=2 tw=120 et
