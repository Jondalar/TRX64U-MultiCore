// ---------------------------------------------------------------------------
// The C64's PLA for the CPU half of a bus cycle: what the address at the top
// nibble selects, as the arbiter (c64_bus_arbiter.v) decodes it. Two copies
// are used: the arbiter's, with the VIC's BA, and the turbo engine's
// (c64_turbo.v), which looks at the CPU's address in every CPU cycle and has
// BA tied high (BA acts on the CPU's stalls there, not on the map).
//
// ba: a read of $D000-$DFFF reaches I/O only with BA high, else the RAM
// underneath answers (the 6510 is held in that cycle).
//
// SPDX-License-Identifier: GPL-3.0-or-later (derived from the arbiter)
// ---------------------------------------------------------------------------
`timescale 1 ns / 1 ps
module c64_pla (
  input  wire        game,
  input  wire        exrom,
  input  wire        charen,
  input  wire        hiram,
  input  wire        loram,
  input  wire  [3:0] msn,           // address bits 15:12
  input  wire        we,
  input  wire        ba,
  output reg         io,
  output reg         ram,
  output reg         basic,
  output reg         kernal,
  output reg         chr,
  output reg         roml,
  output reg         romh
);
  always @(*) begin
    io = 1'b0; ram = 1'b0; basic = 1'b0; kernal = 1'b0; chr = 1'b0; roml = 1'b0; romh = 1'b0;
    casez ({game, exrom, charen, hiram, loram})
      5'b10111,  /* fall-through */
      5'b11111,  /* fall-through */
      5'b00111: begin
        if (we) begin
          if (msn == 4'hd) io = 1'b1;
          else ram = 1'b1;
        end else if (msn == 4'hf || msn == 4'he) begin
          kernal = 1'b1;
        end else if (msn == 4'hd) begin
          if (ba) io = 1'b1;
          else ram = 1'b1;
        end else if ((msn == 4'hb || msn == 4'ha) && game) begin
          basic = 1'b1;
        end else if ((msn == 4'hb || msn == 4'ha) && !game) begin
          romh = 1'b1;
        end else if ((msn == 4'h9 || msn == 4'h8) && !exrom) begin
          roml = 1'b1;
        end else begin
          ram = 1'b1;
        end
      end
      5'b10011,  /* fall-through */
      5'b11011,  /* fall-through */
      5'b00011: begin
        if (we) begin
          ram = 1'b1;
        end else if (msn == 4'hf || msn == 4'he) begin
          kernal = 1'b1;
        end else if (msn == 4'hd) begin
          chr = 1'b1;
        end else if ((msn == 4'hb || msn == 4'ha) && game) begin
          basic = 1'b1;
        end else if ((msn == 4'hb || msn == 4'ha) && !game) begin
          romh = 1'b1;
        end else if ((msn == 4'h9 || msn == 4'h8) && !exrom) begin
          roml = 1'b1;
        end else begin
          ram = 1'b1;
        end
      end
      5'b00110: begin
        if (we) begin
          if (msn == 4'hd) io = 1'b1;
          else ram = 1'b1;
        end else if (msn == 4'hf || msn == 4'he) begin
          kernal = 1'b1;
        end else if (msn == 4'hd) begin
          if (ba) io = 1'b1;
          else ram = 1'b1;
        end else if (msn == 4'hb || msn == 4'ha) begin
          romh = 1'b1;
        end else begin
          ram = 1'b1;
        end
      end
      5'b00010: begin
        if (we) begin
          ram = 1'b1;
        end else if (msn == 4'hf || msn == 4'he) begin
          kernal = 1'b1;
        end else if (msn == 4'hd) begin
          chr = 1'b1;
        end else if (msn == 4'hb || msn == 4'ha) begin
          romh = 1'b1;
        end else begin
          ram = 1'b1;
        end
      end
      5'b1z101,  /* fall-through */
      5'b00101: begin
        if (we) begin
          if (msn == 4'hd) io = 1'b1;
          else ram = 1'b1;
        end else if (msn == 4'hd) begin
          if (ba) io = 1'b1;
          else ram = 1'b1;
        end else begin
          ram = 1'b1;
        end
      end
      5'b1z001: begin
        if (we) begin
          ram = 1'b1;
        end else if (msn == 4'hd) begin
          chr = 1'b1;
        end else begin
          ram = 1'b1;
        end
      end
      5'b1z110: begin
        if (we) begin
          if (msn == 4'hd) io = 1'b1;
          else ram = 1'b1;
        end else if (msn == 4'hf || msn == 4'he) begin
          kernal = 1'b1;
        end else if (msn == 4'hd) begin
          if (ba) io = 1'b1;
          else ram = 1'b1;
        end else begin
          ram = 1'b1;
        end
      end
      5'b1z010: begin
        if (we) begin
          ram = 1'b1;
        end else if (msn == 4'hf || msn == 4'he) begin
          kernal = 1'b1;
        end else if (msn == 4'hd) begin
          chr = 1'b1;
        end else begin
          ram = 1'b1;
        end
      end
      5'b1zz00,  /* fall-through */
      5'b00z00,  /* fall-through */
      5'b00001: begin
        ram = 1'b1;
      end
      5'b01zzz: begin
        if (we) begin
          if (msn == 4'hf || msn == 4'he) begin
            romh = 1'b1;
          end else if (msn == 4'hd) begin
            io = 1'b1;
          end else if (msn == 4'h9 || msn == 4'h8) begin
            roml = 1'b1;
          end else if (msn == 4'h0) begin
            ram = 1'b1;
          end
        end else if (msn == 4'hf || msn == 4'he) begin
          romh = 1'b1;
        end else if (msn == 4'hd) begin
          if (ba) io = 1'b1;
          else ram = 1'b1;
        end else if (msn == 4'h9 || msn == 4'h8) begin
          roml = 1'b1;
        end else if (msn == 4'h0) begin
          ram = 1'b1;
        end
      end
      default: begin
        /* no-op */
      end
    endcase
  end
endmodule
