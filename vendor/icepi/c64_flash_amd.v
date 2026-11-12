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

// Generic AMD-style parallel flash command responder (Am29F040, M29F160,
// MX29F800 and other parts with the same command set; the part is chosen by
// the configuration inputs).
//
// One instance serves both chips of a two-chip cartridge (EasyFlash). Each chip
// keeps its own sequence state, as software interleaves the unlock cycles of
// both chips (EAPIInit does); only one operation can be pending at a time.
//
// The array itself lives in SDRAM and is only read by the cartridge logic.
// This module decodes the command sequences, answers autoselect and status
// reads, and hands PROGRAM / SECTOR ERASE / CHIP ERASE to the firmware as an
// event. The chip stays busy (status reads) until the firmware has applied the
// operation to the SDRAM image and pulses ack.

// (vendor) `default_nettype none
`timescale 1 ns / 1 ps
module c64_flash_amd (
  input wire clk,
  input wire reset,  // C64 reset: aborts command sequences, keeps a pending operation
  input wire enable,

  // chip model configuration (firmware)
  input wire [15:0] cfg_magic1,  // first unlock address  (e.g. 16'h0555)
  input wire [15:0] cfg_magic2,  // second unlock address (e.g. 16'h02aa)
  input wire [15:0] cfg_mask,    // address bits compared against magic1/magic2
  input wire  [7:0] cfg_id_mfr,
  input wire  [7:0] cfg_id_dev,
  input wire  [7:0] cfg_id_ofs,  // autoselect address of the device ID

  // bus access, one strobe per CPU cycle
  input wire        strobe,
  input wire        cs,
  input wire        chip,  // which chip this access selects (0 = ROML, 1 = ROMH)
  input wire        we,
  input wire [22:0] addr,  // chip-relative address, up to 8 MiB
  input wire  [7:0] din,

  output wire       override,  // dout replaces the array data
  output reg  [7:0] dout,

  // firmware handshake
  output reg  [1:0] evt_cmd,  // 0 = none, 1 = program, 2 = sector erase, 3 = chip erase
  output reg        evt_chip,
  output reg [22:0] evt_addr,
  output reg  [7:0] evt_data,
  output reg        evt_pulse,
  input  wire       ack
);

localparam CMD_NONE         = 2'd0;
localparam CMD_PROGRAM      = 2'd1;
localparam CMD_SECTOR_ERASE = 2'd2;
localparam CMD_CHIP_ERASE   = 2'd3;

localparam S_READ      = 4'd0;
localparam S_UNLOCK1   = 4'd1;
localparam S_UNLOCK2   = 4'd2;
localparam S_PROGRAM   = 4'd3;
localparam S_ERASE     = 4'd4;
localparam S_ERASE1    = 4'd5;
localparam S_ERASE2    = 4'd6;
localparam S_AUTOSEL   = 4'd7;
localparam S_AUTOSEL1  = 4'd8;
localparam S_AUTOSEL2  = 4'd9;
localparam S_BUSY      = 4'd10;  // only as the result of next

reg [3:0] state_lo;  // command sequence state of each chip
reg [3:0] state_hi;
reg       busy;      // an operation of chip evt_chip is waiting for the firmware
reg       toggle;
reg [3:0] next;

wire [3:0] state = chip ? state_hi : state_lo;

wire is_magic1 = (addr[15:0] & cfg_mask) == (cfg_magic1 & cfg_mask);
wire is_magic2 = (addr[15:0] & cfg_mask) == (cfg_magic2 & cfg_mask);

wire busy_chip = busy && (chip == evt_chip);
wire bus_write = strobe && cs && we;
wire bus_read  = strobe && cs && !we;

wire in_autosel = (state == S_AUTOSEL || state == S_AUTOSEL1 || state == S_AUTOSEL2);

assign override = enable && (busy ? busy_chip : in_autosel);

always @(*) begin
  dout = 8'h00;

  if (busy) begin
    if (evt_cmd == CMD_PROGRAM)
      dout = {~evt_data[7], toggle, 6'b000000};  // DQ7# data polling, DQ6 toggle
    else
      dout = {1'b0, toggle, 2'b00, 1'b1, 3'b000};  // DQ7 = 0, DQ6 toggle, DQ3 = erase started
  end else if (addr[7:0] == 8'h00)
    dout = cfg_id_mfr;
  else if (addr[7:0] == cfg_id_ofs)
    dout = cfg_id_dev;
end

// next sequence state of the addressed chip for a write; S_BUSY starts an operation
always @(*) begin
  next = S_READ;

  if (state == S_PROGRAM)
    next = S_BUSY;  // the byte after A0 is always program data, even if it reads F0
  else if (din == 8'hf0)
    next = S_READ;  // reset command is accepted in every other state
  else
    case (state)
      S_READ,
      S_AUTOSEL: begin
        if (din == 8'haa && is_magic1)
          next = (state == S_AUTOSEL) ? S_AUTOSEL1 : S_UNLOCK1;
        else
          next = state;
      end

      S_UNLOCK1,
      S_AUTOSEL1: begin
        if (din == 8'h55 && is_magic2)
          next = (state == S_AUTOSEL1) ? S_AUTOSEL2 : S_UNLOCK2;
        else
          next = (state == S_AUTOSEL1) ? S_AUTOSEL : S_READ;
      end

      S_UNLOCK2,
      S_AUTOSEL2: begin
        if (is_magic1 && din == 8'ha0)
          next = S_PROGRAM;
        else if (is_magic1 && din == 8'h80)
          next = S_ERASE;
        else if (is_magic1 && din == 8'h90)
          next = S_AUTOSEL;
        else
          next = (state == S_AUTOSEL2) ? S_AUTOSEL : S_READ;
      end

      S_ERASE:  if (din == 8'haa && is_magic1) next = S_ERASE1;
      S_ERASE1: if (din == 8'h55 && is_magic2) next = S_ERASE2;
      S_ERASE2: if (din == 8'h30 || (din == 8'h10 && is_magic1)) next = S_BUSY;

      default: next = S_READ;
    endcase
end

always @(posedge clk) begin
  evt_pulse <= 1'b0;

  if (!enable) begin
    state_lo <= S_READ;
    state_hi <= S_READ;
    busy     <= 1'b0;
    toggle   <= 1'b0;
    evt_cmd  <= CMD_NONE;

  end else if (busy) begin
    // one operation at a time: writes to either chip are ignored until ack
    if (bus_read && busy_chip)
      toggle <= !toggle;

    if (ack) begin
      busy    <= 1'b0;
      evt_cmd <= CMD_NONE;
    end

  end else if (reset) begin
    state_lo <= S_READ;
    state_hi <= S_READ;

  end else if (bus_write) begin
    if (chip)
      state_hi <= (next == S_BUSY) ? S_READ : next;
    else
      state_lo <= (next == S_BUSY) ? S_READ : next;

    if (next == S_BUSY) begin
      busy      <= 1'b1;
      toggle    <= 1'b0;
      evt_cmd   <= (state == S_PROGRAM) ? CMD_PROGRAM : (din == 8'h30) ? CMD_SECTOR_ERASE : CMD_CHIP_ERASE;
      evt_addr  <= addr;
      evt_data  <= din;
      evt_chip  <= chip;
      evt_pulse <= 1'b1;
    end
  end
end

endmodule
`default_nettype wire
// vim:ts=2 sw=2 tw=120 et
