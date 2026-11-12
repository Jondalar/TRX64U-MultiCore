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

// 93C86 serial (Microwire) EEPROM, x16 organisation: 1024 words, 2 KiB, as on
// the GMod2 cartridge. The command decoding follows the 1541 Ultimate's
// microwire_eeprom.vhd; the array lives outside, in SDRAM, and is reached one
// byte at a time through mem_*. A byte access is served in a bus cycle of the
// cartridge's I/O area, and the C64 makes several of those between two clock
// edges while it bit-bangs the chip, so data is always there in time.
//
// Byte 2n is the high half of word n, byte 2n+1 the low half.

// (vendor) `default_nettype none
`timescale 1 ns / 1 ps
module c64_eeprom_93c86 (
  input wire clk,
  input wire reset,
  input wire enable,

  // microwire lines, as latched from the cartridge register
  input  wire sel,
  input  wire sclk,
  input  wire din,
  output reg  dout,

  // byte access to the array
  output reg         mem_req,
  output reg         mem_we,
  output reg  [10:0] mem_addr,
  output reg   [7:0] mem_wdata,
  input  wire        mem_ack,    // the access requested by mem_req is done
  input  wire  [7:0] mem_rdata,  // valid with mem_ack for a read

  output reg written              // pulse: the array was changed
);

localparam S_IDLE        = 4'd0;
localparam S_SELECTED    = 4'd1;
localparam S_INSTRUCTION = 4'd2;
localparam S_DECODE      = 4'd3;
localparam S_COLLECT     = 4'd4;
localparam S_WAIT_DESEL  = 4'd5;
localparam S_EXECUTE     = 4'd6;
localparam S_WRITE_HI    = 4'd7;
localparam S_WRITE_LO    = 4'd8;
localparam S_FILL        = 4'd9;
localparam S_READING     = 4'd10;
localparam S_ERROR       = 4'd11;

reg  [3:0] state;
reg  [3:0] count;
reg [11:0] instr;
reg [15:0] data_word;
reg  [7:0] shift;        // byte being shifted out while reading
reg        shift_valid;
reg        write_enable;
reg        sclk_d;

wire rising = sclk && !sclk_d;

always @(posedge clk) begin
  sclk_d  <= sclk;
  written <= 1'b0;

  if (reset || !enable) begin
    state        <= S_IDLE;
    write_enable <= 1'b0;
    mem_req      <= 1'b0;
    dout         <= 1'b1;
    count        <= 4'd0;

  end else begin
    case (state)
      S_IDLE: begin
        count     <= 4'd11;
        dout      <= 1'b1;  // ready
        data_word <= 16'hffff;
        if (sel)
          state <= S_SELECTED;
      end

      S_SELECTED: begin
        if (!sel)
          state <= S_IDLE;
        else if (rising)
          state <= din ? S_INSTRUCTION : S_ERROR;  // start bit
      end

      S_INSTRUCTION: begin
        if (!sel)
          state <= S_IDLE;
        else if (rising) begin
          instr <= {instr[10:0], din};
          if (count == 4'd0)
            state <= S_DECODE;
          else
            count <= count - 1'b1;
        end
      end

      S_DECODE: begin
        count <= 4'd15;
        if (instr[11:10] == 2'b01 || instr[11:8] == 4'b0001)  // WRITE, WRAL
          state <= S_COLLECT;
        else if (instr[11:10] == 2'b10) begin                // READ
          mem_addr    <= {instr[9:0], 1'b0};
          mem_we      <= 1'b0;
          mem_req     <= 1'b1;
          shift_valid <= 1'b0;
          state       <= S_READING;
        end else
          state <= S_WAIT_DESEL;
      end

      S_COLLECT: begin
        if (!sel)
          state <= S_IDLE;
        else if (rising) begin
          data_word <= {data_word[14:0], din};
          if (count == 4'd0)
            state <= S_WAIT_DESEL;
          else
            count <= count - 1'b1;
        end
      end

      S_WAIT_DESEL: begin
        if (rising)
          state <= S_ERROR;
        else if (!sel)
          state <= S_EXECUTE;
      end

      S_EXECUTE: begin
        dout <= 1'b0;  // busy
        if (instr[10]) begin                    // WRITE or ERASE of one word
          if (write_enable) begin
            mem_addr  <= {instr[9:0], 1'b0};
            mem_wdata <= data_word[15:8];
            mem_we    <= 1'b1;
            mem_req   <= 1'b1;
            state     <= S_WRITE_HI;
          end else
            state <= S_IDLE;
        end else if (instr[11:8] == 4'b0011) begin  // EWEN
          write_enable <= 1'b1;
          state        <= S_IDLE;
        end else if (instr[11:8] == 4'b0000) begin  // EWDS
          write_enable <= 1'b0;
          state        <= S_IDLE;
        end else if ((instr[11:8] == 4'b0010 || instr[11:8] == 4'b0001) && write_enable) begin  // ERAL, WRAL
          mem_addr  <= 11'd0;
          mem_wdata <= data_word[15:8];
          mem_we    <= 1'b1;
          mem_req   <= 1'b1;
          state     <= S_FILL;
        end else
          state <= S_IDLE;
      end

      S_WRITE_HI: begin
        if (mem_ack) begin
          mem_addr[0] <= 1'b1;
          mem_wdata   <= data_word[7:0];
          state       <= S_WRITE_LO;
        end
      end

      S_WRITE_LO: begin
        if (mem_ack) begin
          mem_req <= 1'b0;
          written <= 1'b1;
          state   <= S_IDLE;
        end
      end

      S_FILL: begin
        if (mem_ack) begin
          if (&mem_addr) begin
            mem_req <= 1'b0;
            written <= 1'b1;
            state   <= S_IDLE;
          end else begin
            mem_addr  <= mem_addr + 1'b1;
            mem_wdata <= mem_addr[0] ? data_word[15:8] : data_word[7:0];
          end
        end
      end

      S_READING: begin
        // fetch the next byte whenever the shift register has been used up
        if (mem_ack) begin
          shift       <= mem_rdata;
          shift_valid <= 1'b1;
          count       <= 4'd7;
          mem_req     <= 1'b0;
        end

        if (!sel) begin
          mem_req <= 1'b0;
          state   <= S_IDLE;
        end else if (rising && shift_valid && !mem_ack) begin
          dout <= shift[count[2:0]];
          if (count == 4'd0) begin
            shift_valid <= 1'b0;
            mem_addr    <= mem_addr + 1'b1;  // sequential read wraps at 2 KiB
            mem_req     <= 1'b1;
          end else
            count <= count - 1'b1;
        end
      end

      S_ERROR: begin
        if (!sel)
          state <= S_IDLE;
      end

      default: state <= S_IDLE;
    endcase
  end
end

endmodule
`default_nettype wire
// vim:ts=2 sw=2 tw=120 et
