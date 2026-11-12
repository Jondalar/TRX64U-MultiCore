//============================================================================
//  SNK NeoGeo for MiSTer
//
//  Copyright (C) 2018 Sean 'Furrtek' Gonsalves
//
//  This program is free software; you can redistribute it and/or modify it
//  under the terms of the GNU General Public License as published by the Free
//  Software Foundation; either version 2 of the License, or (at your option)
//  any later version.
//
//  This program is distributed in the hope that it will be useful, but WITHOUT
//  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
//  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
//  more details.
//
//  You should have received a copy of the GNU General Public License along
//  with this program; if not, write to the Free Software Foundation, Inc.,
//  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//============================================================================
// T143: the machine of cores/neogeo/rtl/neogeo.sv (module "emu" of MiSTer's NeoGeo core) as a module of TRX64U-MultiCore. What was taken out and what
// was put in is marked "TRX" below; the chip models themselves are the vendored files, unchanged. Out: the MiSTer framework (hps_io, the scaler,
// the OSD), the PLL, the Neo Geo CD, the SDRAM controller and the DDR3 controller (ddram), the HPS save engine. In: the ports below. The memory
// is one DDR2 (neo_mem.v): the machine asks for the 68k program, the fix graphics and the sprites through the three request channels
// (neo_mux.v: toggle request, data back with a toggle), for the ADPCM samples and the Z80 program through three byte channels. A late sprite or
// fix word holds the 24 MHz clock enables (`stall`, from neo_mem.v): the machine stands where it is and goes on when the word is there.
// The 68k program has no hold: the machine's own wait state (PROM_DATA_READY into nDTACK) covers it, as it did for the SDRAM.
// The block RAMs of the machine are here (work RAM, VRAM, palette, LO ROM, Z80 RAM, backup RAM, memory card, XRAM); LO ROM, backup RAM, memory
// card and XRAM have a second port in the clock `img_clk` (ui_clk), through which neo_mem.v copies them from and to the DDR2 (the application
// reads and writes the DDR2, never a RAM of the machine).
`timescale 1ns / 1ps
module neo_core (
	input  wire        CLK_48M,
	input  wire        reset_in,          // TRX: level, 48 MHz domain: the machine is held in reset (the app loads ROMs and RAM images meanwhile)
	input  wire        hold_in,           // TRX: level, 48 MHz domain: Freeze - the 24 MHz enables stop where they are

	// configuration, quasi static while the machine runs (the romset: bit layout of the upstream `cfg`, neogeo.sv:`cfg[31:0]`)
	input  wire [31:0] cfg,
	input  wire        sys_mvs_in,        // 1 arcade (MVS), 0 console (AES); latched at reset
	input  wire        vmode_pal_in,      // 0 NTSC, 1 PAL (upstream status[3])
	input  wire        card_absent_in,    // memory card unplugged (upstream status[4])
	input  wire [2:0]  dip_in,            // upstream status[9:7]: settings, freeplay, freeze
	input  wire [26:0] P2ROM_MASK_in,     // TRX: the masks the upstream learnt from the download (smeared address masks), from the app
	input  wire [26:0] CROM_MASK_in,
	input  wire [26:0] V1ROM_MASK_in,
	input  wire [26:0] V2ROM_MASK_in,
	input  wire [26:0] MROM_MASK_in,
	input  wire [6:0]  CROM_START_in,     // first MB of the sprite ROM above the P ROMs (upstream CROM_START)
	input  wire [7:0]  c_deadline_in,     // TRX: machine clocks a sprite / fix word may be late before the machine is held (neo_mux.v)
	input  wire [7:0]  s_deadline_in,

	// controls (MiSTer joystick layout: 21--HNLS DCBAUDLR, see neogeo.sv J1 line: dirs 3:0, A B C D 7:4, Start 8, Select 9, Coin 10, ABC 11, A+B 12, C+D 13)
	input  wire [15:0] joystick_0,
	input  wire [15:0] joystick_1,
	input  wire [64:0] rtc,               // uPD4990 time, MiSTer RTC layout (upstream hps_io RTC)

	// picture (ce_pix: one pixel; the values are valid on the clock ce_pix is high)
	output wire        ce_pix,
	output wire [7:0]  pix_r,
	output wire [7:0]  pix_g,
	output wire [7:0]  pix_b,
	output wire        hblank,
	output wire        vblank,
	output wire        hsync,
	output wire        vsync,
	// sound: YM2610 mix, signed 16 bit
	output wire signed [15:0] snd_l,
	output wire signed [15:0] snd_r,

	// 68k program / system ROM channel: a toggle of pm_req_tgl asks for the word at pm_addr (virtual byte address >> 1); pm_ack_tgl follows with pm_data
	output wire        pm_req_tgl,
	output wire [26:1] pm_addr,
	input  wire        pm_ack_tgl,
	input  wire [15:0] pm_data,
	// fix graphics channel (one word)
	output wire        s_req_tgl,
	output wire [26:1] s_addr,
	input  wire        s_ack_tgl,
	input  wire [15:0] s_data,
	// sprite channel (four words = 8 bytes at an 8 byte aligned address)
	output wire        c_req_tgl,
	output wire [26:3] c_addr,
	input  wire        c_ack_tgl,
	input  wire [63:0] c_data,
	// ADPCM-A, ADPCM-B and Z80 program byte channels (the V ROM / M1 ROM spaces of neogeo.sv: ddram's three read ports)
	output reg         va_req_tgl = 1'b0,
	output reg  [24:0] va_addr = 25'd0,
	input  wire        va_ack_tgl,
	input  wire [7:0]  va_data,
	output reg         vb_req_tgl = 1'b0,
	output reg  [24:0] vb_addr = 25'd0,
	input  wire        vb_ack_tgl,
	input  wire [7:0]  vb_data,
	output reg         mz_req_tgl = 1'b0,
	output reg  [24:0] mz_addr = 25'd0,
	input  wire        mz_ack_tgl,
	input  wire [7:0]  mz_data,
	// the early addresses of the next sprite / fix fetch (neo_mux.v hint generators; toggle + address held, answered by nothing)
	output wire        ch_hint_tgl,
	output wire [26:3] ch_hint_addr,
	output wire        sh_hint_tgl,
	output wire [26:1] sh_hint_addr,
	// statistics of the memory channels (the register block reads them)
	output wire [31:0] st_stall_clk,
	output wire [31:0] st_c_late,
	output wire [31:0] st_s_late,
	output wire [31:0] st_p_reads,
	output wire [7:0]  st_c_max,
	output wire [7:0]  st_s_max,

	// the backup RAM in the DDR2 (neo_bk.v): toggle request, line / write-through, toggle answer
	output wire        bk_req_tgl,
	output wire        bk_req_wr,
	output wire [15:1] bk_req_addr,
	output wire [15:0] bk_req_wdata,
	output wire [1:0]  bk_req_be,
	input  wire        bk_ack_tgl,
	input  wire [127:0] bk_ack_line,
	output wire        halted_o,          // the enables are stopped (hold or stall)
	// the RAM images (img_clk domain; sel 0 LO ROM, 2 memory card, 3 XRAM; words of 16 bit (LO ROM: 8 bit, low byte); read data one clock after the address)
	input  wire        img_clk,
	input  wire [1:0]  img_sel,
	input  wire [15:0] img_addr,
	input  wire [15:0] img_wdata,
	input  wire        img_we,
	output wire [15:0] img_rdata,
	output reg         sv_dirty = 1'b0,   // backup RAM or memory card changed since sv_dirty_clr_tgl last moved
	input  wire        sv_dirty_clr_tgl,

	// bring-up
	output wire        running,           // nRESET_CORE: the machine is out of its power-on counter
	output wire [7:0]  dbg_pck_age        // TRX: the age, in 48 MHz clocks of machine time, at which the last sprite word was in (saturating)
);

// ---------------------------------------------------------------- clocks
// TRX: the 24 MHz enables, as neogeo.sv:CLK_EN_24M_*, held by `stall`. One of them is high in every clock the machine runs and none while it is held.
wire stall;
reg stall_r = 1'b0;
reg ph = 1'b0;
always @(posedge CLK_48M) begin
	stall_r <= stall | hold_in;
	if (!stall_r) ph <= ~ph;
end
assign halted_o = stall_r;
wire CLK_EN_24M_N = ph & ~stall_r;
wire CLK_EN_24M_P = ~ph & ~stall_r;

reg [14:0] TRASH_ADDR = 15'd0;
reg SYSTEM_TYPE = 1'b0;
wire SYSTEM_CD_TYPE = 1'b0;

reg nRESET_CORE = 1'b0;
reg rst_n = 1'b0;
always @(posedge CLK_48M) begin
	nRESET_CORE <= rst_n;
	rst_n <= &TRASH_ADDR;
	if (CLK_EN_24M_N && ~&TRASH_ADDR) TRASH_ADDR <= TRASH_ADDR + 1'b1;

	if (reset_in) begin
		TRASH_ADDR <= 0;
		SYSTEM_TYPE <= sys_mvs_in;
	end
end
assign running = nRESET_CORE;

// ---------------------------------------------------------------- Her Majesty
wire [31:0] cfg_w = cfg;
wire [15:0] snd_right;
wire [15:0] snd_left;

wire nRESET, nRESETP, nSYSTEM, CARD_WE, SHADOW, nVEC, nREGEN, nSRAMWEN, PALBNK;

// Clocks
wire CLK_EN_12M, CLK_EN_12M_N, CLK_68KCLK, CLK_68KCLKB, CLK_EN_6MB, CLK_EN_1HB, CLK_EN_4M_P, CLK_EN_4M_N, CLK_EN_68K_P, CLK_EN_68K_N;

// 68k stuff
// M68K_DATA is a wired bus (see the data bus section below)
wire [15:0] M68K_DATA;
wire [23:1] M68K_ADDR;
wire A22Z, A23Z;
wire M68K_RW, nAS, nLDS, nUDS, nDTACK, nBR, nBG, nBGACK;
wire [15:0] M68K_DATA_BYTE_MASK;
wire [15:0] FX68K_DATAIN;
wire [15:0] FX68K_DATAOUT;
wire IPL0, IPL1;
wire FC0, FC1, FC2;
reg [3:0] P_BANK;

// RTC stuff
wire RTC_DOUT, RTC_DIN, RTC_CLK, RTC_STROBE, RTC_TP;

// OEs and WEs
wire nSROMOEL, nSROMOEU, nSROMOE;
wire nROMOEL, nROMOEU;
wire nPORTOEL, nPORTOEU, nPORTWEL, nPORTWEU, nPORTADRS;
wire nSRAMOEL, nSRAMOEU, nSRAMWEL, nSRAMWEU;
wire nWRL, nWRU, nWWL, nWWU;
wire nLSPOE, nLSPWE;
wire nPAL, nPAL_WE;
wire nBITW0, nBITW1, nBITWD0, nDIPRD0, nDIPRD1;
wire nSDROE, nSDPOE;

// RAM outputs
wire [7:0] WRAML_OUT;
wire [7:0] WRAMU_OUT;
wire [15:0] SRAM_OUT;

wire [14:0] WRAM_ADDR;
wire [7:0] WRAML_DATA, WRAMU_DATA;
wire WRAML_WREN, WRAMU_WREN;

// Memory card stuff
wire [23:0] CDA;
wire [2:0] BNK;
wire [7:0] CDD;
wire nCD1, nCD2;
wire nCRDO, nCRDW, nCRDC;
wire nCARDWEN, CARDWENB;

// Z80 stuff
wire [7:0] SDD_IN, SDD_OUT, Z80_SDD_OUT;
wire [7:0] SDD_RD_C1;
wire [15:0] SDA, Z80_SDA;
wire nSDRD, nSDWR, nMREQ, nIORQ, nBUSAK;
wire Z80_nSDRD, Z80_nSDWR, Z80_nMREQ;
wire Z80_nINT, Z80_nNMI, nSDW, nSDZ80R, nSDZ80W, nSDZ80CLR;
wire nSDROM, nSDMRD, nSDMWR, SDRD0, SDRD1, nZRAMCS;
wire n2610CS, n2610RD, n2610WR;

// Graphics stuff
wire [23:0] PBUS;
wire [7:0] LO_ROM_DATA;
wire nPBUS_OUT_EN;

wire [19:0] C_LATCH;
reg   [3:0] C_LATCH_EXT;
wire [63:0] CR_DOUBLE;
wire [26:0] CROM_ADDR;

wire [1:0] FIX_BANK;
wire [15:0] S_LATCH;
wire [7:0] FIXD;
wire [10:0] FIXMAP_ADDR;

wire CWE, BWE, BOE;

wire [14:0] SLOW_VRAM_ADDR;
wire [15:0] SLOW_VRAM_DATA_IN;
wire [15:0] SLOW_VRAM_DATA_OUT;

wire [10:0] FAST_VRAM_ADDR;
wire [15:0] FAST_VRAM_DATA_IN;
wire [15:0] FAST_VRAM_DATA_OUT;

wire [11:0] PAL_RAM_ADDR;
wire [15:0] PAL_RAM_DATA;
reg [15:0] PAL_RAM_REG;

wire PCK1, PCK2, EVEN1, EVEN2, LOAD, H;
wire PCK1_EN_P, PCK2_EN_P;
wire PCK1_EN_N, PCK2_EN_N;
wire DOTA, DOTB;
wire CA4, S1H1, S2H1;
wire CHBL, nBNKB, VCS;
wire CHG, LD1, LD2, SS1, SS2;
wire [3:0] GAD;
wire [3:0] GBD;
wire [3:0] WE;
wire [3:0] CK;

// SDRAM multiplexing stuff
wire [15:0] SROM_DATA;
wire [15:0] PROM_DATA;
wire PROM_DATA_READY;

wire       ms5p_bank  = cfg_w[17];
wire       xram       = cfg_w[18];
wire       adpcma_ext = cfg_w[19];
wire [2:0] cart_pchip = cfg_w[22:20];
wire       use_pcm    = cfg_w[23];
wire [1:0] cart_chip  = cfg_w[25:24]; // legacy option: 0 - none, 1 - PRO-CT0, 2 - Link MCU
wire [1:0] cmc_chip   = cfg_w[27:26]; // type 1/2
wire       rom_wait   = cfg_w[28];    // ROMWAIT from cart. 0 - Full speed, 1 - 1 wait cycle
wire [1:0] p_wait     = cfg_w[30:29]; // PWAIT from cart. 0 - Full speed, 1 - 1 wait cycle, 2 - 2 cycles

wire SYSTEM_MVS = SYSTEM_TYPE;      // TRX: no Neo Geo CD
wire SYSTEM_CDx = 1'b0;

// TRX: the system menu bits the machine reads
wire video_mode = vmode_pal_in;
wire [26:0] P2ROM_MASK = P2ROM_MASK_in;
wire [26:0] CROM_MASK  = CROM_MASK_in;
// TRX (T143 3b P1): the addresses the next sprite / fix fetch will ask for, from the LSPC's registers (about 25 machine clocks before PCK1 latches them)
wire [23:0] TRX_C_CAND;
wire [15:0] TRX_S_CAND;
wire [26:0] CROM_CAND = {TRX_C_CAND, 3'b000} & CROM_MASK;
wire [26:0] V1ROM_MASK = V1ROM_MASK_in;
wire [26:0] V2ROM_MASK = V2ROM_MASK_in;
wire [26:0] MROM_MASK  = MROM_MASK_in;
wire [6:0]  CROM_START = CROM_START_in;

wire nROMOE = nROMOEL & nROMOEU;
wire nPORTOE = nPORTOEL & nPORTOEU;

// ---------------------------------------------------------------- the memory channels
// TRX: replaces sdram_mux.sv, the SDRAM and the DDR3 controller. Same requests, same wait state.
neo_mux MUX (
	.C_DEADLINE(c_deadline_in), .S_DEADLINE(s_deadline_in),
	.CLK(CLK_48M),
	.HELD(stall_r),
	.nRESET(nRESET),
	.nSYSTEM_G(nSYSTEM_G),

	.M68K_ADDR(M68K_ADDR[20:1]),
	.nAS(nAS | (FC1 == FC0)),
	.nROMOE(nROMOE),
	.nPORTOE(nPORTOE),
	.nSROMOE(nSROMOE),
	.P2ROM_ADDR(P2ROM_ADDR & P2ROM_MASK),
	.PROM_DATA(PROM_DATA),
	.PROM_DATA_READY(PROM_DATA_READY),

	.PCK1(PCK1),
	.CROM_ADDR({CROM_ADDR[26:20] + CROM_START, CROM_ADDR[19:0]}),
	.CROM_CAND({CROM_CAND[26:20] + CROM_START, CROM_CAND[19:0]}),
	.S_CAND(TRX_S_CAND),
	.ch_hint_tgl(ch_hint_tgl), .ch_hint_addr(ch_hint_addr), .sh_hint_tgl(sh_hint_tgl), .sh_hint_addr(sh_hint_addr),
	.CR_DOUBLE(CR_DOUBLE),

	.PCK2(PCK2),
	.S_LATCH(S_LATCH),
	.FIX_BANK(FIX_BANK),
	.SROM_DATA(SROM_DATA),

	.pm_req_tgl(pm_req_tgl), .pm_addr(pm_addr), .pm_ack_tgl(pm_ack_tgl), .pm_data(pm_data),
	.s_req_tgl(s_req_tgl), .s_addr(s_addr), .s_ack_tgl(s_ack_tgl), .s_data(s_data),
	.c_req_tgl(c_req_tgl), .c_addr(c_addr), .c_ack_tgl(c_ack_tgl), .c_data(c_data),
	.c_pend(), .s_pend(), .stall(stall), .dbg_pck_age(dbg_pck_age),
	.st_stall_clk(st_stall_clk), .st_c_late(st_c_late), .st_s_late(st_s_late), .st_p_reads(st_p_reads), .st_c_max(st_c_max), .st_s_max(st_s_max)
);

wire [2:0] P1_OUT, P2_OUT;

neo_d0 D0(
	.CLK(CLK_48M),
	.CLK_EN_24M_P(CLK_EN_24M_P),
	.CLK_EN_24M_N(CLK_EN_24M_N),
	.nRESET(nRESET), .nRESETP(nRESETP),
	.CLK_68KCLK(CLK_68KCLK), .CLK_68KCLKB(CLK_68KCLKB), .CLK_EN_12M(CLK_EN_12M), .CLK_EN_12M_N(CLK_EN_12M_N), .CLK_EN_6MB(CLK_EN_6MB), .CLK_EN_1HB(CLK_EN_1HB),
	.CLK_EN_68K_P(CLK_EN_68K_P), .CLK_EN_68K_N(CLK_EN_68K_N),
	.M68K_ADDR_A4(M68K_ADDR[4]),
	.M68K_DATA(M68K_DATA[5:0]),
	.nBITWD0(nBITWD0),
	.SDA_H(SDA[15:11]), .SDA_L(SDA[4:2]),
	.nSDRD(nSDRD),	.nSDWR(nSDWR), .nMREQ(nMREQ),	.nIORQ(nIORQ),
	.nZ80NMI(Z80_nNMI),
	.nSDW(nSDW), .nSDZ80R(nSDZ80R), .nSDZ80W(nSDZ80W),	.nSDZ80CLR(nSDZ80CLR),
	.nSDROM(nSDROM), .nSDMRD(nSDMRD), .nSDMWR(nSDMWR), .nZRAMCS(nZRAMCS),
	.SDRD0(SDRD0),	.SDRD1(SDRD1),
	.n2610CS(n2610CS), .n2610RD(n2610RD), .n2610WR(n2610WR),
	.P1_OUT(P1_OUT), .P2_OUT(P2_OUT),
	.BNK(BNK)
);

// Because of the SDRAM latency, nDTACK is handled differently for ROM zones
// If the address is in a ROM zone, PROM_DATA_READY is used to extend the normal nDTACK output by NEO-C1
wire nDTACK_ADJ = ~&{nSROMOE, nROMOE, nPORTOE} ? ~PROM_DATA_READY | nDTACK : nDTACK | bk_wait;

cpu_68k M68KCPU(
	.CLK(CLK_48M),
	.CLK_EN_68K_P(CLK_EN_68K_P),
	.CLK_EN_68K_N(CLK_EN_68K_N),
	.nRESET(nRESET),
	.M68K_ADDR(M68K_ADDR),
	.FX68K_DATAIN(FX68K_DATAIN), .FX68K_DATAOUT(FX68K_DATAOUT),
	.nLDS(nLDS), .nUDS(nUDS), .nAS(nAS), .M68K_RW(M68K_RW),
	.nDTACK(nDTACK_ADJ),	// nDTACK
	.IPL2(1'b1), .IPL1(IPL1), .IPL0(IPL0),
	.FC2(FC2), .FC1(FC1), .FC0(FC0),
	.nBG(nBG), .nBR(nBR), .nBGACK(nBGACK),
	.SYSTEM_CDx(SYSTEM_CDx)
);
assign nBR = 1'b1;
assign nBGACK = 1'b1;

// FX68K doesn't like byte masking with Z's, replace with 0's:
assign M68K_DATA_BYTE_MASK = (~|{nLDS, nUDS}) ? M68K_DATA :
										(~nLDS) ? {8'h00, M68K_DATA[7:0]} :
										(~nUDS) ? {M68K_DATA[15:8], 8'h00} :
										16'h0000;

assign FX68K_DATAIN = M68K_RW ? M68K_DATA_BYTE_MASK : 16'h0000;
assign FIXD = S2H1 ? SROM_DATA[15:8] : SROM_DATA[7:0];

// ---------------------------------------------------------------- the data bus
// M68K_DATA is a wired bus in the original: every driver puts Z on it when it is not the one answering (the chip models do the same inside).
assign M68K_DATA = M68K_RW ? 16'bzzzzzzzz_zzzzzzzz : FX68K_DATAOUT;

// Disable ROM read in PORT zone if the game uses a special chip
assign M68K_DATA = (nROMOE & nSROMOE & |{nPORTOE, cart_chip, cart_pchip, xram}) ? 16'bzzzzzzzzzzzzzzzz : PROM_DATA;

// Work RAM read (the Neo Geo CD's extended RAM paths are gone)
assign M68K_DATA[7:0]  = nWRL ? 8'bzzzzzzzz : WRAML_OUT;
assign M68K_DATA[15:8] = nWRU ? 8'bzzzzzzzz : WRAMU_OUT;

assign WRAM_ADDR = M68K_ADDR[15:1];
assign { WRAMU_DATA, WRAML_DATA } = { M68K_DATA[15:0] };
assign WRAMU_WREN = ~nWWU;
assign WRAML_WREN = ~nWWL;

// 68k work RAM. TRX: no second port (the original wiped the RAM with TRASH_ADDR while the core was in reset; a block RAM starts at zero, the BIOS clears the rest)
spram #(15) WRAML(.clock(CLK_48M), .address(WRAM_ADDR), .data(WRAML_DATA), .wren(WRAML_WREN), .q(WRAML_OUT));
spram #(15) WRAMU(.clock(CLK_48M), .address(WRAM_ADDR), .data(WRAMU_DATA), .wren(WRAMU_WREN), .q(WRAMU_OUT));

wire [23:0] P2ROM_ADDR_PVC, P2ROM_ADDR_SMA;
wire [23:0] P2ROM_ADDR = cart_pchip ? (P2ROM_ADDR_PVC | P2ROM_ADDR_SMA) : {P_BANK, M68K_ADDR[19:1], 1'b0};

wire [15:0] PVC_DOUT, SMA_DOUT;
neo_pvc neo_pvc
(
	.nRESET(nRESET),
	.CLK_48M(CLK_48M),

	.ENABLE(cart_pchip == 2),

	.M68K_ADDR(M68K_ADDR),
	.M68K_DATA(M68K_DATA),
	.PROM_DATA(PROM_DATA),
	.nPORTOEL(nPORTOEL),
	.nPORTOEU(nPORTOEU),
	.nPORTWEL(nPORTWEL),
	.nPORTWEU(nPORTWEU),
	.P2_ADDR(P2ROM_ADDR_PVC)
);

neo_sma neo_sma
(
	.nRESET(nRESET),
	.CLK_48M(CLK_48M),

	.TYPE(cart_pchip),

	.M68K_ADDR(M68K_ADDR),
	.M68K_DATA(M68K_DATA),
	.PROM_DATA(PROM_DATA),
	.nPORTOEL(nPORTOEL),
	.nPORTOEU(nPORTOEU),
	.nPORTWEL(nPORTWEL),
	.nPORTWEU(nPORTWEU),
	.P2_ADDR(P2ROM_ADDR_SMA)
);

wire XRAM_CS = ~nPORTADRS && !M68K_ADDR[19:13] && xram;
wire [15:0] XRAM_OUT;
assign M68K_DATA[7:0]  = (XRAM_CS & ~nPORTOEL) ? XRAM_OUT[7:0]  : 8'bZ;
assign M68K_DATA[15:8] = (XRAM_CS & ~nPORTOEU) ? XRAM_OUT[15:8] : 8'bZ;

assign M68K_DATA[7:0] = (~nPORTOEL && M68K_ADDR[19] && !M68K_ADDR[18:1] && xram) ? {~joystick_0[8],1'b1,~joystick_0[9],~joystick_0[11],2'b11, ~joystick_1[10],~joystick_0[10]} : 8'bZ;

wire [15:0] xram_buff_dout;
wire xram_img_wr = img_we & (img_sel == 2'd3);
wire [11:0] img_a12 = img_addr[11:0];

dpram #(12) XRAML(
	.clock_a(CLK_48M),
	.address_a(M68K_ADDR[12:1]),
	.data_a(M68K_DATA[7:0]),
	.wren_a(~nPORTWEL & XRAM_CS),
	.q_a(XRAM_OUT[7:0]),

	.clock_b(img_clk),
	.address_b(img_a12),
	.wren_b(xram_img_wr),
	.data_b(img_wdata[7:0]),
	.q_b(xram_buff_dout[7:0])
);

dpram #(12) XRAMU(
	.clock_a(CLK_48M),
	.address_a(M68K_ADDR[12:1]),
	.data_a(M68K_DATA[15:8]),
	.wren_a(~nPORTWEU & XRAM_CS),
	.q_a(XRAM_OUT[15:8]),

	.clock_b(img_clk),
	.address_b(img_a12),
	.wren_b(xram_img_wr),
	.data_b(img_wdata[15:8]),
	.q_b(xram_buff_dout[15:8])
);

// ---------------------------------------------------------------- backup RAM and memory card
// TRX: the upstream copies them to the SD card through the HPS; here the second port is neo_mem's image mover (img_*)
wire mc_img_wr = img_we & (img_sel == 2'd2);
wire [9:0] memcard_addr = img_addr[9:0];

// Backup RAM is only for MVS
assign M68K_DATA[7:0] = (nSRAMOEL | ~SYSTEM_MVS) ? 8'bzzzzzzzz : SRAM_OUT[7:0];
assign M68K_DATA[15:8] = (nSRAMOEU | ~SYSTEM_MVS) ? 8'bzzzzzzzz : SRAM_OUT[15:8];

// Backup RAM: in the DDR2 behind a cache (neo_bk.v, spec D4c)
wire nBWL = nSRAMWEL | nSRAMWEN_G;
wire nBWU = nSRAMWEU | nSRAMWEN_G;
wire bk_wait;
neo_bk BACKUP
(
	.CLK(CLK_48M), .clr(reset_in), .en(SYSTEM_MVS),
	.M68K_ADDR(M68K_ADDR[15:1]), .M68K_DATA(M68K_DATA),
	.nSRAMOEL(nSRAMOEL), .nSRAMOEU(nSRAMOEU), .nBWL(nBWL), .nBWU(nBWU),
	.SRAM_OUT(SRAM_OUT), .bk_wait(bk_wait),
	.req_tgl(bk_req_tgl), .req_wr(bk_req_wr), .req_addr(bk_req_addr), .req_wdata(bk_req_wdata), .req_be(bk_req_be),
	.ack_tgl(bk_ack_tgl), .ack_line(bk_ack_line)
);

// Memory card
assign {nCD1, nCD2} = {2{card_absent_in}};
assign CARD_WE = (~nCARDWEN & CARDWENB) & ~nCRDW;

wire [15:0] memcard_buff_dout;
neo_memcard MEMCARD(
	.CLK(CLK_48M),
	.CDA(CDA[10:1]), .CDA0(CDA[0]), .CDD(CDD),
	.CARD_WE(CARD_WE),
	.M68K_DATA(M68K_DATA[7:0]),
	.clk_sys(img_clk),
	.memcard_addr(memcard_addr),
	.memcard_wr(mc_img_wr),
	.memcard_din(img_wdata),
	.memcard_dout(memcard_buff_dout)
);

// the images' read side: one clock after the address
reg [1:0] img_sel_q = 2'd0;
always @(posedge img_clk) img_sel_q <= img_sel;
wire [7:0] lo_img_q;
assign img_rdata = (img_sel_q == 2'd0) ? {8'h00, lo_img_q} :
                   (img_sel_q == 2'd2) ? memcard_buff_dout : xram_buff_dout;

// the change flag of the saves (neogeo.sv:bk_change)
reg sram_slot_we;
always @(posedge CLK_48M) begin
	sram_slot_we <= 0;
	if(~nBWL | ~nBWU) begin
		sram_slot_we <= (M68K_ADDR[15:1] >= 'h190 && M68K_ADDR[15:1] < 'h4190);
	end
end
wire bk_change = sram_slot_we | CARD_WE;
(* ASYNC_REG = "TRUE" *) reg [2:0] dclr_s = 3'b000;
always @(posedge CLK_48M) begin
	dclr_s <= {dclr_s[1:0], sv_dirty_clr_tgl};
	if (dclr_s[2] ^ dclr_s[1]) sv_dirty <= 1'b0;
	if (bk_change) sv_dirty <= 1'b1;
end

assign CROM_ADDR = {C_LATCH_EXT, C_LATCH, 3'b000} & CROM_MASK;

zmc ZMC(
	.CLK(CLK_48M),
	.nRESET(nRESET),
	.nSDRD0(SDRD0),
	.SDA_L(SDA[1:0]), .SDA_U(SDA[15:8]),
	.MA(MA)
);

// Bankswitching for the PORT zone, do all games use a 1MB window ?
always @(posedge CLK_48M)
begin
	reg nPORTWEL_d;
	nPORTWEL_d <= nPORTWEL;

	if (!nRESET) P_BANK <= 0;
	else if (~nPORTWEL & nPORTWEL_d) begin
		if(~ms5p_bank) P_BANK <= M68K_DATA[3:0];
		else if(&M68K_ADDR[19:4] && M68K_ADDR[3:1] == 2) P_BANK <= M68K_DATA[7:4] - 1'd1;
	end
end

// PRO-CT0 used as security chip
wire [3:0] GAD_SEC;
wire [3:0] GBD_SEC;

reg nPORTWEL_D;
always @(posedge CLK_48M) nPORTWEL_D <= nPORTWEL;

zmc2_dot ZMC2DOT(
	.CLK(CLK_48M),
	.CLK_EN_12M_N(nPORTWEL_D & ~nPORTWEL),
	.EVEN(M68K_ADDR[2]), .LOAD(M68K_ADDR[1]), .H(M68K_ADDR[3]),
	.CR({
		M68K_ADDR[19], M68K_ADDR[15], M68K_ADDR[18], M68K_ADDR[14],
		M68K_ADDR[17], M68K_ADDR[13], M68K_ADDR[16], M68K_ADDR[12],
		M68K_ADDR[11], M68K_ADDR[7], M68K_ADDR[10], M68K_ADDR[6],
		M68K_ADDR[9], M68K_ADDR[5], M68K_ADDR[8], M68K_ADDR[4],
		M68K_DATA[15], M68K_DATA[11], M68K_DATA[14], M68K_DATA[10],
		M68K_DATA[13], M68K_DATA[9], M68K_DATA[12], M68K_DATA[8],
		M68K_DATA[7], M68K_DATA[3], M68K_DATA[6], M68K_DATA[2],
		M68K_DATA[5], M68K_DATA[1], M68K_DATA[4], M68K_DATA[0]
		}),
	.GAD(GAD_SEC), .GBD(GBD_SEC)
);

neo_273 NEO273(
	.CLK(CLK_48M),
	.PBUS(PBUS[19:0]),
	.PCK1B_EN(PCK1_EN_N), .PCK2B_EN(PCK2_EN_N),
	.C_LATCH(C_LATCH), .S_LATCH(S_LATCH)
);

// 4 MSBs not handled by NEO-273
always @(posedge CLK_48M) begin
	if (PCK1_EN_N) C_LATCH_EXT <= PBUS[23:20];
end

neo_cmc neo_cmc
(
	.CLK(CLK_48M),
	.PCK2B_EN(PCK2_EN_N),
	.PBUS(PBUS[14:0]),
	.TYPE(cmc_chip),
	.ADDR(FIXMAP_ADDR),
	.BANK(FIX_BANK)
);

assign M68K_DATA[7:0] = ((cart_chip == 1) & ~nPORTOEL) ?
								{GBD_SEC[1], GBD_SEC[0], GBD_SEC[3], GBD_SEC[2],
								GAD_SEC[1], GAD_SEC[0], GAD_SEC[3], GAD_SEC[2]} : 8'bzzzzzzzz;

// Fake COM MCU
wire [15:0] COM_DOUT;

com COM(
	.nRESET(nRESET),
	.CLK_48M(CLK_48M),
	.nPORTOEL(nPORTOEL), .nPORTOEU(nPORTOEU), .nPORTWEL(nPORTWEL),
	.M68K_DIN(COM_DOUT)
);

assign M68K_DATA = (cart_chip == 2) ? COM_DOUT : 16'bzzzzzzzz_zzzzzzzz;

syslatch SL(
	.nRESET(nRESET),
	.CLK(CLK_48M),
	.CLK_EN_68K_P(CLK_EN_68K_P),
	.M68K_ADDR(M68K_ADDR[4:1]),
	.nBITW1(nBITW1),
	.SHADOW(SHADOW), .nVEC(nVEC), .nCARDWEN(nCARDWEN),	.CARDWENB(CARDWENB), .nREGEN(nREGEN), .nSYSTEM(nSYSTEM), .nSRAMWEN(nSRAMWEN), .PALBNK(PALBNK)
);

wire nSRAMWEN_G = SYSTEM_MVS ? nSRAMWEN : 1'b1;	// nSRAMWEN is only for MVS
wire nSYSTEM_G = SYSTEM_MVS ? nSYSTEM : 1'b1;	// nSYSTEM is only for MVS

neo_e0 E0(
	.M68K_ADDR(M68K_ADDR[23:1]),
	.BNK(BNK),
	.nSROMOEU(nSROMOEU),	.nSROMOEL(nSROMOEL), .nSROMOE(nSROMOE),
	.nVEC(nVEC),
	.A23Z(A23Z), .A22Z(A22Z),
	.CDA(CDA)
);

neo_f0 F0(
	.CLK(CLK_48M),
	.nRESET(nRESET),
	.nDIPRD0(nDIPRD0), .nDIPRD1(nDIPRD1),
	.nBITW0(nBITW0), .nBITWD0(nBITWD0),
	.DIPSW({~dip_in[2:1], 5'b11111, ~dip_in[0]}),
	.COIN1(~joystick_0[10]), .COIN2(~joystick_1[10]),
	.M68K_ADDR(M68K_ADDR[7:4]),
	.M68K_DATA(M68K_DATA[7:0]),
	.SYSTEMB(~nSYSTEM_G),
	.RTC_DOUT(RTC_DOUT), .RTC_TP(RTC_TP), .RTC_DIN(RTC_DIN), .RTC_CLK(RTC_CLK), .RTC_STROBE(RTC_STROBE),
	.SYSTEM_TYPE(SYSTEM_MVS)
);

uPD4990 RTC(
	.rtc(rtc),
	.nRESET(nRESET),
	.CLK(CLK_48M),
	.CLK_EN_12M(CLK_EN_12M),
	.DATA_CLK(RTC_CLK), .STROBE(RTC_STROBE),
	.DATA_IN(RTC_DIN), .DATA_OUT(RTC_DOUT),
	.CS(1'b1), .OE(1'b1),
	.TP(RTC_TP)
);

neo_g0 G0(
	.M68K_DATA(M68K_DATA),
	.G0(nCRDC), .G1(nPAL), .DIR(M68K_RW), .WE(nPAL_WE),
	.CDD({8'hFF, CDD}), .PC(PAL_RAM_DATA)
);

wire [11:0] joy_0 = joystick_0[11:0];
wire [11:0] joy_1 = joystick_1[11:0];

neo_c1 C1(
	.CLK(CLK_48M),
	.M68K_ADDR(M68K_ADDR[21:17]),
	.M68K_DATA(M68K_DATA[15:8]), .A22Z(A22Z), .A23Z(A23Z),
	.nLDS(nLDS), .nUDS(nUDS), .RW(M68K_RW), .nAS(nAS),
	.nROMOEL(nROMOEL), .nROMOEU(nROMOEU),
	.nPORTOEL(nPORTOEL), .nPORTOEU(nPORTOEU), .nPORTWEL(nPORTWEL), .nPORTWEU(nPORTWEU),
	.nPORT_ZONE(nPORTADRS),
	.nWRL(nWRL), .nWRU(nWRU), .nWWL(nWWL), .nWWU(nWWU),
	.nSROMOEL(nSROMOEL), .nSROMOEU(nSROMOEU),
	.nSRAMOEL(nSRAMOEL), .nSRAMOEU(nSRAMOEU), .nSRAMWEL(nSRAMWEL), .nSRAMWEU(nSRAMWEU),
	.nLSPOE(nLSPOE), .nLSPWE(nLSPWE),
	.nCRDO(nCRDO), .nCRDW(nCRDW), .nCRDC(nCRDC),
	.nSDW(nSDW),
	.P1_IN(~{ joy_0[9:8], {joy_0[7:4]|{3{~xram & joy_0[11]}}|{{2{joystick_0[13]}},{2{joystick_0[12]}}}, joy_0[0], joy_0[1], joy_0[2], joy_0[3]}}),
	.P2_IN(~{ joy_1[9:8], {joy_1[7:4]|{3{~xram & joy_1[11]}}|{{2{joystick_1[13]}},{2{joystick_1[12]}}}, joy_1[0], joy_1[1], joy_1[2], joy_1[3]}}),
	.nCD1(nCD1), .nCD2(nCD2),
	.nWP(0),			// Memory card is never write-protected
	.nROMWAIT(~rom_wait), .nPWAIT0(~p_wait[0]), .nPWAIT1(~p_wait[1]), .PDTACK(1),
	.SDD_WR(SDD_OUT),
	.SDD_RD(SDD_RD_C1),
	.nSDZ80R(nSDZ80R), .nSDZ80W(nSDZ80W), .nSDZ80CLR(nSDZ80CLR),
	.CLK_EN_68K_P(CLK_EN_68K_P),
	.nDTACK(nDTACK),
	.nBITW0(nBITW0), .nBITW1(nBITW1),
	.nDIPRD0(nDIPRD0), .nDIPRD1(nDIPRD1),
	.nPAL_ZONE(nPAL),
	.SYSTEM_TYPE({SYSTEM_CDx, SYSTEM_MVS})
);

zmc2_dot ZMC2(
	.CLK(CLK_48M),
	.CLK_EN_12M_N(CLK_EN_12M_N),
	.EVEN(EVEN1), .LOAD(LOAD), .H(H),
	.CR(CA4 ? CR_DOUBLE[63:32] : CR_DOUBLE[31:0]),
	.GAD(GAD), .GBD(GBD),
	.DOTA(DOTA), .DOTB(DOTB)
);

// LO ROM (64 KB, loaded from the DDR2 by neo_mem through port a)
wire lo_img_wr = img_we & (img_sel == 2'd0);
dpram #(16) LO(
	.clock_a(img_clk),
	.address_a(img_addr),
	.data_a(img_wdata[7:0]),
	.wren_a(lo_img_wr),
	.q_a(lo_img_q),
	.clock_b(CLK_48M),
	.address_b(PBUS[15:0]),
	.data_b(8'h00),
	.wren_b(1'b0),
	.q_b(LO_ROM_DATA)
);

// VCS is normally used as the LO ROM's nOE but the NeoGeo relies on the fact that the LO ROM
// will still have its output active for a short moment (~50ns) after nOE goes high
// nPBUS_OUT_EN is used internally by LSPC2 but it's broken out here to use the additional
// half mclk cycle it provides compared to VCS. This makes sure LO_ROM_DATA is valid when latched.
// TRX: PBUS[23:16] was a wired bus (LSPC2 drives it as an output at other times): LO ROM data is one driver
assign PBUS[23:16] = nPBUS_OUT_EN ? LO_ROM_DATA : 8'bzzzzzzzz;

spram #(11,16) UFV(
	.clock(CLK_48M),
	.address(FAST_VRAM_ADDR),
	.data(FAST_VRAM_DATA_OUT),
	.wren(~CWE),
	.q(FAST_VRAM_DATA_IN)
);

spram #(15,16) USV(
	.clock(CLK_48M),
	.address(SLOW_VRAM_ADDR),
	.data(SLOW_VRAM_DATA_OUT),
	.wren(~BWE),
	.q(SLOW_VRAM_DATA_IN)
);

wire [18:11] MA;
wire [7:0] Z80_RAM_DATA;

// 2KB Z80 work RAM (Cart)
spram #(11) Z80RAM(.clock(CLK_48M), .address(SDA[10:0]), .data(SDD_OUT), .wren(~(nZRAMCS | nSDMWR)), .q(Z80_RAM_DATA));

assign SDD_IN = (~nSDZ80R)             ? SDD_RD_C1 :
				 (~nSDMRD & ~nSDROM)    ? M1_ROM_DATA :
				 (~nSDMRD & ~nZRAMCS)   ? Z80_RAM_DATA :
				 (~n2610CS & ~n2610RD)  ? YM2610_DOUT :
				 8'b00000000;

wire Z80_nRESET = nRESET;

// ---------------------------------------------------------------- Z80 program (M1 ROM) and ADPCM samples
// TRX: neogeo.sv put the Z80 program read on the DDR3 (ddram rd_req3/rd_ack3, address {7'b10_0000_0, MA & MROM_MASK, SDA[10:0]}); here it is a byte channel
// of neo_mem with the same toggle handshake. The wait line of the Z80 is the same: it waits while a request is outstanding.
wire [7:0] M1_ROM_DATA = mz_data_q;
reg [7:0] mz_data_q = 8'h00;
reg z80rd_req;
reg z80rd_ack = 1'b0;
wire z80_rom_rd = ~(nSDMRD | nSDROM);
reg z80_old_rd, z80_old_rd1;
reg [2:0] mz_ack_s = 3'b000;
always @(posedge CLK_48M) begin
	z80_old_rd <= z80_rom_rd;
	if(z80_old_rd == z80_rom_rd) z80_old_rd1 <= z80_old_rd;

	// nRESET is used here because otherwise z80rd_req is toggled at core boot
	if(~z80_old_rd1 & z80_old_rd & nRESET) z80rd_req <= ~z80rd_req;

	mz_ack_s <= {mz_ack_s[1:0], mz_ack_tgl};
	if (mz_ack_s[2] ^ mz_ack_s[1]) begin
		mz_data_q <= mz_data;
		z80rd_ack <= z80rd_req_sent;
	end
end
// the request toggles towards neo_mem with the address of that moment
reg z80rd_req_sent = 1'b0;
always @(posedge CLK_48M) begin
	if (z80rd_req != z80rd_req_sent && (mz_req_tgl == mz_ack_s[1])) begin
		mz_addr <= {7'b10_0000_0, MA & MROM_MASK[18:11], SDA[10:0]} & 25'h1FFFFFF;
		mz_req_tgl <= ~mz_req_tgl;
		z80rd_req_sent <= z80rd_req;
	end
end
wire Z80_nWAIT = (z80rd_req == z80rd_ack);

cpu_z80 Z80CPU(
	.CLK(CLK_48M),
	.CLK4P_EN(CLK_EN_4M_P),
	.CLK4N_EN(CLK_EN_4M_N),
	.nRESET(Z80_nRESET),
	.SDA(Z80_SDA), .SDD_IN(SDD_IN), .SDD_OUT(Z80_SDD_OUT),
	.nIORQ(nIORQ),	.nMREQ(Z80_nMREQ), .nRD(Z80_nSDRD), .nWR(Z80_nSDWR),
	.nBUSRQ(1'b1), .nBUSAK(nBUSAK),
	.nINT(Z80_nINT), .nNMI(Z80_nNMI), .nWAIT(Z80_nWAIT)
);

assign { SDA, SDD_OUT } = { Z80_SDA, Z80_SDD_OUT };
assign { nSDRD, nSDWR } = { Z80_nSDRD, Z80_nSDWR };
assign { nMREQ }        = { Z80_nMREQ            };

wire [19:0] ADPCMA_ADDR;
wire  [4:0] ADPCMA_BANK;
wire [23:0] ADPCMB_ADDR;

// The ADPCM-A / ADPCM-B reads: the request and acknowledge are toggles; the YM2610 holds its enable while a word is late (adpcm_en).
// TRX: the counters of neogeo.sv counted 96 MHz clocks (128 and 1580); the machine runs on 48 MHz here, half the numbers.
reg ADPCMA_READ_REQ, ADPCMB_READ_REQ;
reg ADPCMA_READ_ACK, ADPCMB_READ_ACK;
reg [24:0] ADPCMA_ADDR_LATCH;	// 16MB(32MB)
reg [24:0] ADPCMB_ADDR_LATCH;	// 32MB
reg [23:0] ADPCMB_MASK;
reg [7:0] ADPCMA_ACK_COUNTER;
reg [10:0] ADPCMB_ACK_COUNTER;
wire ADPCMA_DATA_READY = ~((ADPCMA_READ_REQ ^ ADPCMA_READ_ACK) & (ADPCMA_ACK_COUNTER == 8'd0));
wire ADPCMB_DATA_READY = ~((ADPCMB_READ_REQ ^ ADPCMB_READ_ACK) & (ADPCMB_ACK_COUNTER == 11'd0));

reg [1:0] ADPCMA_OE_SR = 2'b11;
reg [1:0] ADPCMB_OE_SR = 2'b11;
reg [7:0] ADPCMA_DOUT_r = 8'h80, ADPCMB_DOUT_r = 8'h80;
reg [2:0] va_ack_s = 3'b000, vb_ack_s = 3'b000;
always @(posedge CLK_48M) begin
	ADPCMA_OE_SR <= {ADPCMA_OE_SR[0], nSDROE};
	ADPCMA_ACK_COUNTER <= ADPCMA_ACK_COUNTER == 8'd0 ? 8'd0 : ADPCMA_ACK_COUNTER - 8'd1;
	ADPCMB_ACK_COUNTER <= ADPCMB_ACK_COUNTER == 11'd0 ? 11'd0 : ADPCMB_ACK_COUNTER - 11'd1;
	// Trigger ADPCM A data read on nSDROE falling edge
	if (ADPCMA_OE_SR == 2'b10) begin
		ADPCMA_READ_REQ <= ~ADPCMA_READ_REQ;
		ADPCMA_ADDR_LATCH <= {ADPCMA_BANK, ADPCMA_ADDR} & V1ROM_MASK[24:0];
		// Data is needed on one previous 8MHz clk before next 666KHz clock->(48MHz/666KHz = 72)-6-2=64
		ADPCMA_ACK_COUNTER <= 8'd64;
	end

	ADPCMB_MASK <= use_pcm ? V1ROM_MASK[23:0] : V2ROM_MASK[23:0];

	// Trigger ADPCM B data read on nSDPOE falling edge
	ADPCMB_OE_SR <= {ADPCMB_OE_SR[0], nSDPOE};
	if (ADPCMB_OE_SR == 2'b10) begin
		ADPCMB_READ_REQ <= ~ADPCMB_READ_REQ;
		ADPCMB_ADDR_LATCH <= {~use_pcm, ADPCMB_ADDR & ADPCMB_MASK};
		// Data is needed on one previous 8MHz clk before next 55KHz clock->(48MHz/55KHz = 864)-72-2=790
		ADPCMB_ACK_COUNTER <= 11'd790;
	end

	// towards neo_mem: one request open per channel
	va_ack_s <= {va_ack_s[1:0], va_ack_tgl};
	vb_ack_s <= {vb_ack_s[1:0], vb_ack_tgl};
	if (va_ack_s[2] ^ va_ack_s[1]) begin ADPCMA_DOUT_r <= va_data; ADPCMA_READ_ACK <= va_sent; end
	if (vb_ack_s[2] ^ vb_ack_s[1]) begin ADPCMB_DOUT_r <= vb_data; ADPCMB_READ_ACK <= vb_sent; end
	if (ADPCMA_READ_REQ != va_sent && va_req_tgl == va_ack_s[1]) begin
		va_addr <= ADPCMA_ADDR_LATCH; va_req_tgl <= ~va_req_tgl; va_sent <= ADPCMA_READ_REQ;
	end
	if (ADPCMB_READ_REQ != vb_sent && vb_req_tgl == vb_ack_s[1]) begin
		vb_addr <= ADPCMB_ADDR_LATCH; vb_req_tgl <= ~vb_req_tgl; vb_sent <= ADPCMB_READ_REQ;
	end
end
reg va_sent = 1'b0, vb_sent = 1'b0;
initial begin ADPCMA_READ_REQ = 1'b0; ADPCMB_READ_REQ = 1'b0; ADPCMA_READ_ACK = 1'b0; ADPCMB_READ_ACK = 1'b0; ADPCMA_ACK_COUNTER = 8'd0; ADPCMB_ACK_COUNTER = 11'd0; z80rd_req = 1'b0; end

wire [7:0] ADPCMA_DATA = (!V1ROM_MASK) ? 8'h80 : ADPCMA_DOUT_r;
wire [7:0] ADPCMB_DATA = (!ADPCMB_MASK) ? 8'h80 : ADPCMB_DOUT_r;

wire [7:0] YM2610_DOUT;

reg adpcm_en;
reg [7:0] adpcma_d, adpcmb_d;
always @(posedge CLK_48M) begin
	reg en;
	en <= ADPCMA_DATA_READY & ADPCMB_DATA_READY;
	adpcm_en <= en;

	adpcma_d <= ADPCMA_DATA;
	adpcmb_d <= ADPCMB_DATA;
end

jt10 YM2610(
	.rst(~Z80_nRESET),
	.clk(CLK_48M), .cen((CLK_EN_4M_P | CLK_EN_4M_N) & adpcm_en),
	.addr(SDA[1:0]),
	.din(SDD_OUT), .dout(YM2610_DOUT),
	.cs_n(n2610CS), .wr_n(n2610WR),
	.irq_n(Z80_nINT),
	.adpcma_addr(ADPCMA_ADDR), .adpcma_bank(ADPCMA_BANK), .adpcma_roe_n(nSDROE), .adpcma_data(adpcma_d),
	.adpcmb_addr(ADPCMB_ADDR), .adpcmb_roe_n(nSDPOE), .adpcmb_data(adpcmb_d),
	.snd_right(snd_right), .snd_left(snd_left), .snd_enable(4'b1111), .ch_enable(6'b111111)
);

assign snd_l = snd_left;
assign snd_r = snd_right;

// ---------------------------------------------------------------- video
wire DOTA_GATED = DOTA;
wire DOTB_GATED = DOTB;
wire HSync; //,VSync;

lspc2_a2_sync	LSPC(
	.CLK(CLK_48M),
	.CLK_EN_24M_P(CLK_EN_24M_P),
	.CLK_EN_24M_N(CLK_EN_24M_N),
	.RESET(nRESET),
	.nRESETP(nRESETP),
	.LSPC_8M(), .LSPC_4M(),
	.LSPC_EN_4M_P(CLK_EN_4M_P), .LSPC_EN_4M_N(CLK_EN_4M_N),
	.M68K_ADDR(M68K_ADDR[3:1]), .M68K_DATA(M68K_DATA),
	.IPL0(IPL0), .IPL1(IPL1),
	.CD_VBLANK_IRQ_EN(1'b1),
	.CD_TIMER_IRQ_EN(1'b1),
	.LSPOE(nLSPOE), .LSPWE(nLSPWE),
	.PBUS_OUT(PBUS[15:0]), .PBUS_IO(PBUS[23:16]),
	.nPBUS_OUT_EN(nPBUS_OUT_EN),
	.DOTA(DOTA_GATED), .DOTB(DOTB_GATED),
	.CA4(CA4), .S2H1(S2H1), .S1H1(S1H1),
	.LOAD(LOAD), .H(H), .EVEN1(EVEN1), .EVEN2(EVEN2),
	.PCK1(PCK1), .PCK2(PCK2),
	.TRX_C_CAND(TRX_C_CAND), .TRX_S_CAND(TRX_S_CAND),
	.PCK1_EN_N(PCK1_EN_N), .PCK2_EN_N(PCK2_EN_N),
	.PCK1_EN_P(PCK1_EN_P), .PCK2_EN_P(PCK2_EN_P),
	.CHG(CHG),
	.LD1(LD1), .LD2(LD2),
	.WE(WE), .CK(CK),	.SS1(SS1), .SS2(SS2),
	.HSYNC(HSync), //.VSYNC(VSync),
	.CHBL(CHBL), .BNKB(nBNKB),
	.VCS(VCS),
	.SVRAM_ADDR(SLOW_VRAM_ADDR),
	.SVRAM_DATA_IN(SLOW_VRAM_DATA_IN), .SVRAM_DATA_OUT(SLOW_VRAM_DATA_OUT),
	.BOE(BOE), .BWE(BWE),
	.FVRAM_ADDR(FAST_VRAM_ADDR),
	.FVRAM_DATA_IN(FAST_VRAM_DATA_IN), .FVRAM_DATA_OUT(FAST_VRAM_DATA_OUT),
	.CWE(CWE),
	.VMODE(video_mode),
	.FIXMAP_ADDR(FIXMAP_ADDR)	// Extracted for NEO-CMC
);

neo_b1 B1(
	.CLK(CLK_48M),	.CLK_EN_6MB(CLK_EN_6MB), .CLK_EN_1HB(CLK_EN_1HB),
	.S1H1(S1H1),
	.A23I(A23Z), .A22I(A22Z),
	.M68K_ADDR_U(M68K_ADDR[21:17]), .M68K_ADDR_L(M68K_ADDR[12:1]),
	.nLDS(nLDS), .RW(M68K_RW), .nAS(nAS),
	.PBUS(PBUS),
	.FIXD(FIXD),
	.PCK1_EN(PCK1_EN_P), .PCK2_EN(PCK2_EN_P),
	.CHBL(CHBL), .BNKB(nBNKB),
	.GAD(GAD), .GBD(GBD),
	.WE(WE), .CK(CK),
	.TMS0(CHG), .LD1(LD1), .LD2(LD2), .SS1(SS1), .SS2(SS2),
	.PA(PAL_RAM_ADDR),
	.EN_FIX(1'b1),
	.DOGE(1'b1),
	.nRST(nRESET_CORE),
	.nRESET(nRESET)
);

spram #(13,16) PALRAM(
	.clock(CLK_48M), 	// Was CLK_12M
	.address({PALBNK, PAL_RAM_ADDR}),
	.data(M68K_DATA),
	.wren(~nPAL_WE),
	.q(PAL_RAM_DATA)
);

// DAC latches
reg ce_pix_r;
reg [2:0] HBlank;
always @(posedge CLK_48M) begin
	ce_pix_r <= 0;
	if(CLK_EN_6MB) begin
		ce_pix_r <= 1;
		PAL_RAM_REG <= nRESET ? PAL_RAM_DATA : 16'h8000;
	end

	if(ce_pix_r) begin
		HBlank <= (HBlank<<1) | CHBL;
	end
end
assign ce_pix = ce_pix_r;

//Re-create VSync as original one is barely equals to VBlank
reg VSync;
always @(posedge CLK_48M) begin
	reg       old_hs;
	reg       old_vbl;
	reg [2:0] vbl;
	reg [7:0] vblcnt, vspos;

	if(ce_pix_r) begin
		old_hs <= HSync;
		if(~old_hs & HSync) begin
			old_vbl <= nBNKB;

			if(~nBNKB) vblcnt <= vblcnt+1'd1;
			if(old_vbl & ~nBNKB) vblcnt <= 0;
			if(~old_vbl & nBNKB) begin
				vspos <= (vblcnt>>1) - 8'd7;
			end

			{VSync,vbl} <= {vbl,1'b0};
			if(vblcnt == vspos) {VSync,vbl} <= '1;
		end
	end
end

wire [6:0] R6 = {1'b0, PAL_RAM_REG[11:8], PAL_RAM_REG[14], PAL_RAM_REG[11]} - PAL_RAM_REG[15];
wire [6:0] G6 = {1'b0, PAL_RAM_REG[7:4],  PAL_RAM_REG[13], PAL_RAM_REG[7] } - PAL_RAM_REG[15];
wire [6:0] B6 = {1'b0, PAL_RAM_REG[3:0],  PAL_RAM_REG[12], PAL_RAM_REG[3] } - PAL_RAM_REG[15];

wire [7:0] R8 = R6[6] ? 8'd0 : {R6[5:0],  R6[4:3]};
wire [7:0] G8 = G6[6] ? 8'd0 : {G6[5:0],  G6[4:3]};
wire [7:0] B8 = B6[6] ? 8'd0 : {B6[5:0],  B6[4:3]};

assign pix_r = ~SHADOW ? R8 : {1'b0, R8[7:1]};
assign pix_g = ~SHADOW ? G8 : {1'b0, G8[7:1]};
assign pix_b = ~SHADOW ? B8 : {1'b0, B8[7:1]};
assign hblank = HBlank[0];
assign vblank = ~nBNKB;
assign hsync = HSync;
assign vsync = VSync;

endmodule
