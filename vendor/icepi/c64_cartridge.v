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

// (vendor) `default_nettype none
`timescale 1 ns / 1 ps
// The cartridge, driven by a mapping descriptor (TREX-PROP-002, TRX64-Ultimate
// docs/specs/T33-generic-cart-decoder.md). This block knows no cartridge type:
// what the registers do, which bits bank and switch the lines, how ROML/ROMH
// map onto the image and the flash chips, and what IO1/IO2 answer is all in
// `desc', which the app writes per type (trx_io page 4, 0x101A4040-0x101A4064;
// one byte per offset, byte n at desc[8n +: 8]). The one dedicated device is
// the Action Replay / Retro Replay (c64_action_replay below), chosen by a
// descriptor bit.
//
// desc byte (offset 0x40 + n):
//   0, 1    R0_INIT, R1_INIT      register values at reset
//   2, 3    R0_WMASK, R1_WMASK    bits a rule may set (the loaded value is ANDed)
//   4-19    rules 0-3, 4 bytes each, the first matching rule fires (phi2_h):
//           [0] b7 valid, b0 area (0 IO1, 1 IO2), b1 on (0 write, 1 read),
//               b2 target (0 R0, 1 R1), b4:3 source (0 bus data, 1 A7-A0, 2 value)
//           [1] match, [2] mask: fires when (A7-A0 ^ match) & mask == 0
//           [3] value
//   20, 21  BANK_A, BANK_B        b7:4 width (0-8), b3:1 lsb, b0 register;
//                                 bank = {A, B}, 8 bits
//   22      MODE                  b7:4 width (0-3), b3:1 lsb, b0 register: the
//                                 index into the mode table
//   23      IO                    b1:0 IO1 read, b3:2 IO2 read (0 open bus,
//                                 1 R0 at A0 = 0 / R1 at A0 = 1, 2 RAM, 3 the
//                                 EEPROM's data out bit, open bus otherwise),
//                                 b4 IO1 writes go to RAM, b5 IO2 writes go to
//                                 RAM, b6 IO2 writes are the cartridge's also
//                                 with a REU (io2_we_valid)
//   24-31   mode table 0-7        b0 EXROM, b1 GAME, b2 HOLD (keep the lines of
//                                 the last other entry), b3 GAMEW (GAME low in a
//                                 CPU write to $8000-$FFFF), b4 KILL (the
//                                 cartridge switches itself off until reset)
//   32      LAYOUT                b0 ROMH reads the upper 8 KiB of the bank's
//                                 16 KiB image slot (else the lower: a mirror),
//                                 b1 flash chip address {BANK[6:0], ROMH, A12-A0}
//                                 (one linear chip; else {BANK, A12-A0}),
//                                 b2 ROMH is flash chip 1, b3 93C86 EEPROM on
//                                 R0, b4 Action Replay
//   33, 34  EE                    b2:0 / b5:3: R0 bits of CS, CLK; DI, DO
//                                 (DO = the bit position in the IO read)
//   35      FLASH_SEL             the accesses the flash responder sees:
//                                 b0 ROML read, b1 ROML write, b2 ROMH read,
//                                 b3 ROMH write
//   36      VALID                 b0 (cart_slot: a cartridge without it is off)
//   37      REGX                  b0/b1/b2: bit 1 of the register number of the
//                                 BANK_A / BANK_B / MODE field (0-3: R0-R3; the
//                                 field byte's b0 is bit 0); b7:4: bit 1 of the
//                                 target register of rule 0-3 (the rule byte's
//                                 b2 is bit 0). R3 is not built (R2 is the
//                                 third register: a number 3 reads R2's bits too)
//   38, 39  R2_INIT, R2_WMASK     the third register: reset value, bits a rule
//                                 may set (0: it never loads)
//   40, 41  RD_MATCH, RD_MASK     an IO1 read with (A7-A0 ^ match) & mask == 0
//                                 answers R2 (when EXT b0)
//   42      EXT                   b0 IO1 read of R2 (RD_*), b1 BANK9 (the bank is
//                                 9 bits wide, else 8), b2 FSPLIT3 (flash chip
//                                 address {BANK[8:3], ROMH, BANK[2:0], A12-A0})
//                                 Bytes 37-42 = 0: the descriptor of before (T51)//
// The image (DDR2, the app's layout) has 16 KiB per bank: ROML of bank n at
// n * 16K, ROMH 8 KiB above. rom_addr is the byte offset in it (up to 8 MiB with
// a 9-bit bank: EasyFlash 3, T51).
module c64_cartridge (
  input wire clk,
  input wire reset,
  input wire phi2_p,
  input wire phi2_h,
  input wire phi2_n,  // end of the CPU half of the cycle, after any SDRAM stall
  input wire sdram_pending,

  input wire [343:0] desc,

  // flash chip model (EasyFlash ROML / ROMH)
  input  wire        flash_enable,
  input  wire [15:0] flash_magic1,
  input  wire [15:0] flash_magic2,
  input  wire [15:0] flash_mask,
  input  wire  [7:0] flash_id_mfr,
  input  wire  [7:0] flash_id_dev,
  input  wire  [7:0] flash_id_ofs,
  output wire [33:0] flash_evt_0,
  output wire        flash_evt_pulse,

  // GMod2 serial EEPROM: the array was written
  output wire        eeprom_written,
  input  wire        flash_ack,

  input  wire [15:0] addr,
  input  wire  [7:0] din,
  output wire  [7:0] dout,
  output wire  [7:0] dout_mask,  // the bits of dout the cartridge drives (else open bus)
  output wire        io_serve,   // an IO1/IO2 read the cartridge answers from dout (not RAM)
  output wire        kill,       // the mode table says: switch off

  input wire we,
  input wire ba,

  output reg exrom,
  output reg game,

  input wire freeze,
  input wire roml,
  input wire romh,
  input wire io1_cen,
  input wire io2_cen,

  output wire nmi,
  output wire irq,

  output reg  io2_we_valid,

  output wire [22:0] rom_addr,
  output reg         rom_enable,
  input  wire        rom_ready,
  input  wire [7:0]  rom_dout,
  output wire        rom_select,

  output wire [14:0] ram_addr,
  output reg         ram_enable,
  output wire        ram_we,
  output wire  [7:0] ram_wdata,
  output wire        ram_wdata_own,  // write ram_wdata, not the CPU's / REU's store data
  input  wire        ram_ready,
  input  wire [7:0]  ram_dout,
  output wire        ram_select,

  // the banking registers for the state bus (c64_core.v): {reg_1, reg_0}
  output wire [15:0] st_bank,

  // a side-effect-free look (TRX64 freeze session, T12 §11 Q2): what a read
  // of pk_addr with the select pk_roml/pk_romh/pk_io1/pk_io2 (1 = selected)
  // would return now, from the registers as they are: an IO register value
  // (pk_io_sel; the EEPROM's DO bit for an IO area read as DEV, the other
  // bits 0), or the image / RAM address (the AMD responder's status and ID
  // modes are not shown: the array is). The same descriptor decode as a bus
  // access; an area the descriptor leaves open selects nothing
  input  wire [15:0] pk_addr,
  input  wire        pk_roml,
  input  wire        pk_romh,
  input  wire        pk_io1,
  input  wire        pk_io2,
  output wire        pk_io_sel,
  output wire  [7:0] pk_io_dout,
  output wire        pk_rom_sel,
  output wire [22:0] pk_rom_addr,
  output wire        pk_ram_sel,
  output wire [14:0] pk_ram_addr
);

// byte n of the descriptor (the descriptor is an argument, so that a simulator
// sees the dependency)
function [7:0] db(input [343:0] v, input integer n);
  db = v[8*n +: 8];
endfunction

// a field of R0/R1/R2: {width, lsb, register} as in BANK_A/BANK_B/MODE; hi is bit 1 of the register number (REGX)
function [7:0] field(input [7:0] f, input hi, input [7:0] r0, input [7:0] r1, input [7:0] r2);
  reg [7:0] r;
  begin
    r     = hi ? r2 : f[0] ? r1 : r0;
    r     = r >> f[3:1];
    field = r & ~(8'hFF << f[7:4]);
  end
endfunction

reg [7:0] reg_0;
reg [7:0] reg_1;
reg [7:0] reg_2;

wire [7:0] lay    = db(desc, 32);
wire [7:0] io     = db(desc, 23);
wire [7:0] fl_sel = db(desc, 35);
wire [7:0] regx   = db(desc, 37);
wire [7:0] ext    = db(desc, 42);
wire       ar_enable = lay[4];
wire       ee_enable = lay[3];

// ---- registers: the first rule that matches this IO access loads one -------
reg       r_hit;
reg [1:0] r_tgt;
reg [7:0] r_val;
integer   n;
reg [7:0] rb;
always @(*) begin
  r_hit = 1'b0;
  r_tgt = 1'b0;
  r_val = 8'd0;
  rb = 8'd0;
  for (n = 3; n >= 0; n = n - 1) begin
    rb = db(desc, 4 + 4*n);
    if (db(desc, 4 + 4*n) >> 7 &&
        (db(desc, 4 + 4*n) & 8'h01 ? !io2_cen : !io1_cen) &&
        (db(desc, 4 + 4*n) & 8'h02 ? !we : we) &&
        ((addr[7:0] ^ db(desc, 5 + 4*n)) & db(desc, 6 + 4*n)) == 8'd0) begin
      r_hit = 1'b1;
      r_tgt = {regx[4 + n], rb[2]};
      case (db(desc, 4 + 4*n) >> 3 & 8'd3)
        8'd0:    r_val = din;
        8'd1:    r_val = addr[7:0];
        default: r_val = db(desc, 7 + 4*n);
      endcase
    end
  end
end

always @(posedge clk) begin
  if (reset) begin
    reg_0 <= db(desc, 0);
    reg_1 <= db(desc, 1);
    reg_2 <= db(desc, 38);
  end else if (phi2_h && r_hit) begin
    if (r_tgt[1])      reg_2 <= r_val & db(desc, 39);
    else if (r_tgt[0]) reg_1 <= r_val & db(desc, 3);
    else               reg_0 <= r_val & db(desc, 2);
  end
end

// the banking registers for the state bus: {R1, R0}; with an R2 (EasyFlash 3): {R2[2:0], R1[7], R1[2:0], R0}
assign st_bank = ext[0] ? {1'b0, reg_2[2:0], reg_1[7], reg_1[2:0], reg_0} : {reg_1, reg_0};

// ---- bank, mode, lines --------------------------------------------------------
wire [7:0] bank_a = field(db(desc, 20), regx[0], reg_0, reg_1, reg_2);
wire [7:0] bank_b = field(db(desc, 21), regx[1], reg_0, reg_1, reg_2);
wire [8:0] bank_w = ({1'b0, bank_a} << (db(desc, 21) >> 4)) | {1'b0, bank_b};
wire [8:0] bank   = {ext[1] && bank_w[8], bank_w[7:0]};   // 8 bits unless BANK9
wire [2:0] mode   = field(db(desc, 22), regx[2], reg_0, reg_1, reg_2);
wire [7:0] entry  = db(desc, 24 + mode);

// HOLD: the lines of the last entry that was not HOLD
reg [2:0] held;  // exrom, game, gamew
always @(posedge clk)
  if (reset)
    held <= 3'b110;
  else if (!entry[2])
    held <= {entry[0], entry[1], entry[3]};

wire l_exrom = entry[2] ? held[2] : entry[0];
wire l_game  = entry[2] ? held[1] : entry[1];
wire l_gamew = entry[2] ? held[0] : entry[3];
assign kill  = !ar_enable && entry[4];

wire ar_exrom;
wire ar_game;

wire ar_irq;
wire ar_nmi;

wire ar_io2_we_valid;

wire [7:0] ar_dout;

wire [16:0] ar_rom_addr;
wire        ar_rom_select;

wire [14:0] ar_ram_addr;
wire        ar_ram_we;
wire        ar_ram_select;

always @(*) begin
  if (ar_enable) begin
    exrom        = ar_exrom;
    game         = ar_game;
    io2_we_valid = ar_io2_we_valid;
  end else begin
    exrom        = l_exrom;
    game         = l_game && !(l_gamew && we && addr[15]);
    io2_we_valid = io[6];
  end
end

// ---- IO1 / IO2 ------------------------------------------------------------------
wire        ee_dout;
wire        ee_mem_req;
wire        ee_mem_we;
wire [10:0] ee_mem_addr;
wire  [7:0] ee_mem_wdata;

wire       ee_cycle = ee_enable && ee_mem_req && !io1_cen;
wire [1:0] io_rd    = !io1_cen ? io[1:0] : !io2_cen ? io[3:2] : 2'd0;
wire       io_wram  = (!io1_cen && io[4]) || (!io2_cen && io[5]);
wire       io_ram   = we ? io_wram : io_rd == 2'd2;
wire       ee_cs    = reg_0[db(desc, 33) & 8'd7];
wire [7:0] do_bit   = 8'd1 << (db(desc, 34) >> 3 & 8'd7);

// an IO1 read of R2 (EasyFlash 3's slot register at $DE01)
wire       rd2_hit  = ext[0] && !io1_cen && ((addr[7:0] ^ db(desc, 40)) & db(desc, 41)) == 8'd0;
assign io_serve  = ar_enable ? (!io1_cen && addr[7:1] == 7'b0) :
                   !we && (rd2_hit || io_rd == 2'd1 || (io_rd == 2'd3 && ee_cs));
assign dout_mask = (!ar_enable && !rd2_hit && io_rd == 2'd3) ? do_bit : 8'hFF;
wire [7:0] io_dout_g = io_rd == 2'd3 ? (ee_dout ? do_bit : 8'd0) : !addr[0] ? reg_0 : reg_1;
wire [7:0] io_dout   = rd2_hit ? reg_2 : io_dout_g;

assign irq = ar_enable ? ar_irq : 1'b0;
assign nmi = ar_enable ? ar_nmi : 1'b0;

// ---- ROML / ROMH: the image ------------------------------------------------------
wire        romh_acc  = !romh;
wire [22:0] img       = {bank, romh_acc && lay[0], addr[12:0]};

assign rom_select = ar_enable ? ar_rom_select : (!romh || !roml);
assign rom_addr   = ar_enable ? {6'b0, ar_rom_addr} : img;

assign ram_select = ar_enable ? ar_ram_select : (io_ram || ee_cycle);
assign ram_we     = ar_enable ? ar_ram_we     : ee_cycle ? ee_mem_we : we;
assign ram_addr   = ar_enable ? ar_ram_addr   : ee_cycle ? {4'b1111, ee_mem_addr} :
                                                {6'b0, io2_cen, addr[7:0]};  // IO2 at 0, IO1 at 0x100
assign ram_wdata  = ee_mem_wdata;
assign ram_wdata_own = ee_cycle;

// ---------------------------------------------------------------------------
// 93C86 serial EEPROM (GMod2: CS bit 6, CLK bit 5, DI bit 4, DO read in bit 7)
//
// Its 2 KiB sit in the top of the cartridge RAM area and are reached in the
// cycles in which the C64 accesses IO1 anyway.
// ---------------------------------------------------------------------------

reg         ee_issued;

wire ee_ack   = ee_issued && phi2_n;

always @(posedge clk) begin
  if (reset)
    ee_issued <= 1'b0;
  else if (sdram_pending)
    ee_issued <= ee_cycle;
  else if (phi2_n)
    ee_issued <= 1'b0;
end

c64_eeprom_93c86 c64_eeprom_0 (
  .clk       (clk),
  .reset     (reset),
  .enable    (ee_enable),
  .sel       (ee_cs),
  .sclk      (reg_0[db(desc, 33) >> 3 & 8'd7]),
  .din       (reg_0[db(desc, 34) & 8'd7]),
  .dout      (ee_dout),
  .mem_req   (ee_mem_req),
  .mem_we    (ee_mem_we),
  .mem_addr  (ee_mem_addr),
  .mem_wdata (ee_mem_wdata),
  .mem_ack   (ee_ack),
  .mem_rdata (ram_dout),
  .written   (eeprom_written)
);

// ---------------------------------------------------------------------------
// flash chips
//
// One generic AMD responder serves both chips. FLASH_SEL says which bus
// cycles select it, LAYOUT how the chip and the chip-relative address are
// formed; the part itself (unlock addresses, IDs) is configured by the app,
// and the sector geometry lives entirely in the app.
//
// A write reaches ROML/ROMH only in Ultimax mode (the PLA routes writes to RAM
// otherwise), so the select alone limits writes to the cartridge's flash mode.
// ---------------------------------------------------------------------------

wire        flash_cs   = !ar_enable &&
                         ((!roml && (we ? fl_sel[1] : fl_sel[0])) || (romh_acc && (we ? fl_sel[3] : fl_sel[2])));
wire        flash_chip = lay[2] && romh_acc;
// EXT b2 FSPLIT3: ROMH sits above the lowest three bank bits, {BANK[8:3], ROMH, BANK[2:0], A12-A0}: the chip order of
// the MX29LV640 on an EasyFlash 3 (slot, bank_hi, ROMH, bank_lo); without it the linear chip of before
wire [22:0] flash_addr = ext[2] ? {bank[8:3], romh_acc, bank[2:0], addr[12:0]} :
                         lay[1] ? {2'b0, bank[6:0], romh_acc, addr[12:0]} : {2'b0, bank[7:0], addr[12:0]};

wire        flash_override_i;
wire  [7:0] flash_dout;
wire  [1:0] flash_evt_cmd;
wire        flash_evt_chip;
wire [22:0] flash_evt_addr;
wire  [7:0] flash_evt_data;

c64_flash_amd c64_flash_0 (
  .clk        (clk),
  .reset      (reset),
  .enable     (flash_enable),
  .cfg_magic1 (flash_magic1),
  .cfg_magic2 (flash_magic2),
  .cfg_mask   (flash_mask),
  .cfg_id_mfr (flash_id_mfr),
  .cfg_id_dev (flash_id_dev),
  .cfg_id_ofs (flash_id_ofs),
  .strobe     (phi2_h),
  .cs         (flash_cs),
  .chip       (flash_chip),
  .we         (we),
  .addr       (flash_addr),
  .din        (din),
  .override   (flash_override_i),
  .dout       (flash_dout),
  .evt_cmd    (flash_evt_cmd),
  .evt_chip   (flash_evt_chip),
  .evt_addr   (flash_evt_addr),
  .evt_data   (flash_evt_data),
  .evt_pulse  (flash_evt_pulse),
  .ack        (flash_ack)
);

// event word: [33:32] chip address bits 22:21, [31:30] cmd, [29] chip, [28:8] chip address bits 20:0, [7:0] data
assign flash_evt_0 = {flash_evt_addr[22:21], flash_evt_cmd, flash_evt_chip, flash_evt_addr[20:0], flash_evt_data};

wire flash_override = flash_cs && flash_override_i;
wire io_select      = !ar_enable && io_serve;

// the look (above): the same decode for pk_addr
wire        ar_pk_io_sel, ar_pk_rom_sel, ar_pk_ram_sel;
wire [16:0] ar_pk_rom_addr;
wire [14:0] ar_pk_ram_addr;
wire  [7:0] ar_pk_io_dout;
wire [1:0] pk_io_rd = pk_io1 ? io[1:0] : pk_io2 ? io[3:2] : 2'd0;
wire       pk_rd2_hit = ext[0] && pk_io1 && ((pk_addr[7:0] ^ db(desc, 40)) & db(desc, 41)) == 8'd0;
assign pk_io_sel   = ar_enable ? ar_pk_io_sel : (pk_rd2_hit || pk_io_rd == 2'd1 || (pk_io_rd == 2'd3 && ee_cs));
assign pk_io_dout  = ar_enable ? ar_pk_io_dout :
                     pk_rd2_hit ? reg_2 :
                     pk_io_rd == 2'd3 ? (ee_dout ? do_bit : 8'd0) : !pk_addr[0] ? reg_0 : reg_1;
assign pk_rom_sel  = ar_enable ? ar_pk_rom_sel : (pk_romh || pk_roml);
assign pk_rom_addr = ar_enable ? {6'b0, ar_pk_rom_addr} : {bank, pk_romh && lay[0], pk_addr[12:0]};
assign pk_ram_sel  = ar_enable ? ar_pk_ram_sel : pk_io_rd == 2'd2;
assign pk_ram_addr = ar_enable ? ar_pk_ram_addr : {6'b0, !pk_io2, pk_addr[7:0]};  // IO2 at 0, IO1 at 0x100

assign dout = ar_enable  ? ar_dout  :
              io_select  ? io_dout  :
              flash_override ? flash_dout :
              rom_select ? rom_dout :
              ram_select ? ram_dout : 8'hXX;

always @(posedge clk) begin
  if (reset) begin
    rom_enable <= 1'b0;
    ram_enable <= 1'b0;

  end else begin
    if (rom_enable && rom_ready)
      rom_enable <= 1'b0;

    if (sdram_pending)
      rom_enable <= rom_select;

    if (ram_enable && ram_ready)
      ram_enable <= 1'b0;

    if (sdram_pending)
      ram_enable <= ram_select;
  end
end

c64_action_replay c64_action_replay_0 (
  .clk(clk),
  .reset(reset),
  .enable(ar_enable),
  .phi2_p(phi2_p),
  .phi2_h(phi2_h),

  .addr(addr),
  .din(din),
  .dout(ar_dout),

  .we(we),
  .ba(ba),

  .exrom(ar_exrom),
  .game(ar_game),

  .freeze(freeze),
  .roml(roml),
  .romh(romh),
  .io1_cen(io1_cen),
  .io2_cen(io2_cen),
  .io2_we_valid(ar_io2_we_valid),

  .nmi(ar_nmi),
  .irq(ar_irq),

  .rom_addr(ar_rom_addr),
  .rom_dout(rom_dout),
  .rom_select(ar_rom_select),

  .ram_addr(ar_ram_addr),
  .ram_we(ar_ram_we),
  .ram_dout(ram_dout),
  .ram_select(ar_ram_select),

  .pk_addr(pk_addr),
  .pk_roml(pk_roml),
  .pk_romh(pk_romh),
  .pk_io1(pk_io1),
  .pk_io2(pk_io2),
  .pk_io_sel(ar_pk_io_sel),
  .pk_io_dout(ar_pk_io_dout),
  .pk_rom_sel(ar_pk_rom_sel),
  .pk_rom_addr(ar_pk_rom_addr),
  .pk_ram_sel(ar_pk_ram_sel),
  .pk_ram_addr(ar_pk_ram_addr)
);

endmodule

module c64_action_replay (
  input wire clk,
  input wire reset,
  input wire enable,
  input wire phi2_p,
  input wire phi2_h,

  input  wire [15:0] addr,
  input  wire  [7:0] din,
  output wire  [7:0] dout,

  input wire we,
  input wire ba,

  output wire exrom,
  output wire game,

  input wire freeze,
  input wire roml,
  input wire romh,
  input wire io1_cen,
  input wire io2_cen,

  output wire nmi,
  output wire irq,

  output wire io2_we_valid,

  output wire [16:0] rom_addr,
  input  wire [7:0]  rom_dout,
  output wire        rom_select,

  output wire [14:0] ram_addr,
  output wire        ram_we,
  input  wire [7:0]  ram_dout,
  output wire        ram_select,

  // a side-effect-free look (c64_cartridge's pk_*): the decode below for pk_addr
  input  wire [15:0] pk_addr,
  input  wire        pk_roml,
  input  wire        pk_romh,
  input  wire        pk_io1,
  input  wire        pk_io2,
  output wire        pk_io_sel,
  output wire  [7:0] pk_io_dout,
  output wire        pk_rom_sel,
  output wire [16:0] pk_rom_addr,
  output wire        pk_ram_sel,
  output wire [14:0] pk_ram_addr
);

wire reset_n;
wire freeze_pending_n;

wire u6a_q;
wire u6b_qn;

wire u7c_out;
wire u7d_out;

reg freeze_pending;

wire [3:0] u5_q;

assign reset_n          = !reset;
assign freeze_pending_n = !freeze_pending;

LS74_D_FF u6a (
  .clk(clk),
  .en(freeze_pending && phi2_h),
  .d(1'b1),
  .pr_n(1'b1),
  .clr_n(reset_n && u6b_qn),
  .q(u6a_q),
  .qn()
);

LS74_D_FF u6b (
  .clk(clk),
  .en(u5_q[2]),
  .d(1'b1),
  .pr_n(1'b1),
  .clr_n(reset_n && u7c_out),
  .q(),
  .qn(u6b_qn)
);

assign nmi = u6a_q;
assign irq = u6a_q;

LS163_4BIT_COUNTER u5 (
  .clk(clk),
  .clr_n(1'b1),
  .load_n(u6a_q && ba),
  .enp(phi2_p),
  .ent(phi2_p),
  .d(4'b0000),
  .q(u5_q),
  .rco()
);

wire [7:0] io_dout;
wire       io_select;

wire [7:0] u4a_q;
wire [7:0] u4a_d;

// https://rr.c64.org/wiki/CyberpunX_Replay_Manual
// $de00 write: This register is reset to $00 on a hard reset if not in flash
//              mode. If in flash mode, it is set to $02 in order to prevent the
//              computer from starting the normal cartridge.
//
//              Bit 7 - controls bank-address 15 for ROM banking
//              Bit 6 - must be set once to "1" after a successful freeze in order to set the correct memory map
//                      and enable Bits 0 and 1 of this register. Otherwise no effect.
//              Bit 5 - switches between ROM and RAM: 0=ROM, 1=RAM
//              Bit 4 - controls bank-address 14 for ROM and RAM banking
//              Bit 3 - controls bank-address 13 for ROM and RAM banking
//              Bit 2 - Setting this bit will disable further write accesses to all registers & reset the c64 memory map
//                      to standard, as if there is no cartridge installed at all.
//              Bit 1 controls the EXROM line: A 0 will assert it, a 1 will deassert it.
//              Bit 0 controls the GAME  line: A 1 asserts the line, a 0 will deassert it.

LS273_D_FF_8BIT u4 (
  .clk(clk),
  .en(we && phi2_h && io_select && enable),
  .clr_n(freeze_pending_n && reset_n),
  .d(u4a_d),
  .q(u4a_q)
);

assign io2_we_valid = !u4a_q[2] && u4a_q[5];

assign u4a_d = (addr[0] == 1'b0) ? din : {din[7], u4a_q[6:5], din[4:3], u4a_q[2:0]};

wire [7:0] u4b_q;

// https://rr.c64.org/wiki/CyberpunX_Replay_Manual
// $de01 write: Extended control register. If not in Flash mode, this register
//              can only be written to once. If in Flash mode, the REUcomp bit
//              cannot be set, but the register will not be disabled by the
//              first write. Bit 5 is always set to 0 if not in flash mode.
//
//              Bit 7 - bank-address 15 for ROM (mirror of $de00)
//              Bit 6 - REU compatibility bit. 0=standard memory map
//                                             1=REU compatible memory map
//              Bit 5 - bank-address 16 for ROM (only in flash mode)
//              Bit 4 - bank-address 14 for RAM and ROM (mirror of $de00)
//              Bit 3 - bank-address 13 for RAM and ROM (mirror of $de00)
//              Bit 2 - NoFreeze   (1 disables Freeze function)
//              Bit 1 - AllowBank  (1 allows banking of RAM in $df00/$de02 area)
//              Bit 0 - enable accessory connector (SilverSurfer).

LS273_D_FF_8BIT u4a (
  .clk(clk),
  .en(we && phi2_h && io_select && enable && addr[0] == 1'b1),
  .clr_n(reset_n),
  .d(din),
  .q(u4b_q)
);

// https://rr.c64.org/wiki/CyberpunX_Replay_Manual
// $de00 read or $de01 read:
//              Bit 7 - feedback of banking bit 15
//              Bit 6 - 1=REU compatible memory map active
//              Bit 5 - feedback of banking bit 16
//              Bit 4 - feedback of banking bit 14
//              Bit 3 - feedback of banking bit 13
//              Bit 2 - 1=Freeze button pressed
//              Bit 1 - feedback of AllowBank bit
//              Bit 0 - 1=Flashmode active (jumper set)

assign io_dout = {u4a_q[7], u4b_q[6], 1'b0, u4a_q[4:3], 1'b0, u4b_q[1], 1'b0};

assign u7c_out = ~(u4a_q[6]);
assign u7d_out = ~(io1_cen || u4a_q[2]);

assign io_select = u7d_out && addr[7:1] == 7'b0;

wire o1, o2, o3, o4;
wire pla_oe;

assign pla_oe = !u4a_q[2];

c64_action_replay_pla u3_pla (
  .addr({reset_n && u6b_qn, addr[14], addr[15], addr[13], (io2_cen || u4b_q[6]) && io1_cen, u4a_q[0], u4a_q[1], u4a_q[5]}),
  .o1(o1),
  .o2(o2),
  .o3(o3),
  .o4(o4)
);

assign ram_select = (pla_oe ? !o2 : 1'b0) && !io_select;
assign ram_we     = we;
assign ram_addr   = {u4b_q[1] ? u4a_q[4:3] : 2'b00, addr[12:0]};

wire rom_oe;

assign rom_oe     = !romh || !roml || (!io2_cen && !u4b_q[6]) || !io1_cen;
assign rom_select = (pla_oe ? !o1 : 1'b0) && rom_oe && !io_select;
assign rom_addr   = {u4a_q[7], u4a_q[4:3], 1'b0, addr[12:0]};

assign dout = io_select  ? io_dout  :  // $DE00 / $DE01 read
              ram_select ? ram_dout :
              rom_select ? rom_dout : 8'hXX;

assign game  = pla_oe ? o3 : 1'b1;
assign exrom = pla_oe ? o4 : 1'b1;

// the look: the same decode for pk_addr, no register written
wire pk_o1, pk_o2;
c64_action_replay_pla u3_pla_pk (
  .addr({reset_n && u6b_qn, pk_addr[14], pk_addr[15], pk_addr[13], (!pk_io2 || u4b_q[6]) && !pk_io1,
         u4a_q[0], u4a_q[1], u4a_q[5]}),
  .o1(pk_o1),
  .o2(pk_o2),
  .o3(),
  .o4()
);
assign pk_io_sel   = pk_io1 && !u4a_q[2] && pk_addr[7:1] == 7'b0;
assign pk_io_dout  = io_dout;
assign pk_ram_sel  = (pla_oe ? !pk_o2 : 1'b0) && !pk_io_sel;
assign pk_rom_sel  = (pla_oe ? !pk_o1 : 1'b0) &&
                     (pk_romh || pk_roml || (pk_io2 && !u4b_q[6]) || pk_io1) && !pk_io_sel;
assign pk_rom_addr = {u4a_q[7], u4a_q[4:3], 1'b0, pk_addr[12:0]};
assign pk_ram_addr = {u4b_q[1] ? u4a_q[4:3] : 2'b00, pk_addr[12:0]};

always @(posedge clk) begin
  if (reset || !enable)
    freeze_pending <= 1'b0;
  else if (freeze && !u4b_q[2])
    freeze_pending <= 1'b1;
  else if (phi2_h)
    freeze_pending <= 1'b0;
end

endmodule

module LS163_4BIT_COUNTER (
  input wire clk,
  input wire clr_n,
  input wire load_n,
  input wire enp,
  input wire ent,
  input wire [3:0] d,
  output reg [3:0] q,
  output wire rco
);

always @(posedge clk) begin
  if (!clr_n)
    q <= 4'b0;
  else if (!load_n)
    q <= d;
  else if (enp && ent)
    q <= q + 1;
end

assign rco = (q == 4'b1111) && ent;

endmodule

module LS273_D_FF_8BIT (
  input wire clk,
  input wire en,
  input wire clr_n,
  input wire [7:0] d,
  output reg [7:0] q
);

always @(posedge clk) begin
  if (!clr_n)
    q <= 8'b0;
  else if (en)
    q <= d;
end

endmodule

module LS74_D_FF (
  input wire clk,
  input wire en,
  input wire d,
  input wire pr_n,
  input wire clr_n,
  output reg q,
  output wire qn
);

always @(posedge clk) begin
  if (!pr_n)
    q <= 1'b1;
  else if (!clr_n)
    q <= 1'b0;
  else if (en)
    q <= d;
end

assign qn = !q;

endmodule
`default_nettype wire
// vim:ts=2 sw=2 tw=120 et
