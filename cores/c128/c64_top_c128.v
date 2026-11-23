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
module c64_top_c128 #(
  parameter SDRAM_BANK = 2'b11,
  parameter C1541_EXTERNAL_ROM = 1,
  parameter C1541 = 1,                  // 0: no drive inside; IEC and parallel lines released
  parameter [22:0] TURBO_HOLD = 23'h400000,   // T29: PHI2 cycles at normal speed after a reset (sims shorten it)
  parameter SYS_CLK_FREQUENCY = 31_527_777,
  parameter TMDS_CLK_FREQUENCY = 27_588_750
) (
  input wire clk,
  input wire rst,
  input wire vic_reset_req,
  input wire cpu_reset_req,
  // TRX64 (T40) System Mode: 0 PAL, 1 NTSC, 2 PAL-60, 3 NTSC-50 (quasi-static);
  // vic_trst restarts the VIC's timing with it (kawari reset_req) while
  // vic_trst_mask keeps kawari's reset away from the CPU and the chips
  input wire [1:0] vmode,
  input wire vic_trst,
  input wire vic_trst_mask,
  input  wire init_we,             // T113 5.6: the cold start writes 0 to the VIC's registers 0..init_a (the chip has no reset for them: registers_scaled.v keeps them through rst)
  input  wire [5:0] init_a,
  input  wire ram_hold,            // T140 D25: the RAM write FIFO is nearly full (c128_ramd.v): the CPU is held like for a miss
  output wire [2:0] mem_vic_tag,   // T140 D25: the bank the VIC addresses now
  output wire       mem_cmd_vic,   // T140 D25: the RAM command is the VIC's
  input  wire rom_miss,            // T113 5.7: the ROM read of the previous clock missed (c128_romc.v): the CPU that asked is held and repeats it; the VIC never waits
  input  wire rom_fill_busy,       // a line is being fetched (only the old-stall control of the bench uses it)
  input  wire chr_wclk,            // the character ROM store (c128_chr.v), written by the application (cpu_clk)
  input  wire chr_we,
  input  wire [12:0] chr_a,
  input  wire [7:0]  chr_d,
  output wire [7:0]  chr_q,
  input wire cpu_pause_req,

  // T29 turbo (c64_turbo_c64u.v, docs/specs/T29-turbo.md §12). gate_ce: c64_core_c128's
  // enable for the next gclk edge (ungated clock); turbo_inhibit: something
  // stops the machine or needs it exact, the engine falls back to normal
  // speed; the app's setting (quasi-static, the toggle after the bytes);
  // port B of c64_mem_c128 for the turbo CPU (clk2x falling edge) and its status
  input wire        gate_ce,
  input wire        turbo_inhibit,
  input wire  [7:0] tcfg_en,
  input wire  [7:0] tcfg_prefer,
  input wire        tcfg_upd_tgl,
  output wire [15:0] tb_addr,
  output wire [14:0] tb_raddr,
  output wire        tb_we,
  output wire  [7:0] tb_wdata,
  input  wire  [7:0] tb_ram_lo, tb_ram_hi, tb_rom_lo, tb_rom_hi,
  input  wire        tb_took,
  output wire [31:0] turbo_stat,
  output wire [31:0] turbo_w1,
  output wire [31:0] turbo_w2,
  output wire [31:0] turbo_pack,        // the state bus word
  input  wire        turbo_ld,          // a restore loads the word below at the next opening edge
  input  wire [31:0] turbo_ld_pack,
  input  wire        turbo_ran_clr,
  output wire        turbo_on,          // the engine drives the CPU

  // C64 SDRAM port (p0_* interface)
  output  reg [23:0] mem_cmd_addr,
  output  reg        mem_cmd_we,
  output  reg        mem_cmd_valid,
  input  wire        mem_cmd_ready,
  output  reg [15:0] mem_wdata,
  output  reg  [1:0] mem_wdata_we,
  input  wire        mem_wdata_ready,
  input  wire [15:0] mem_rdata,
  input  wire        mem_rdata_valid,

  // REU SDRAM port (p1_* interface)
  output  reg [23:0] reu_mem_cmd_addr,
  output  reg        reu_mem_cmd_we,
  output  reg        reu_mem_cmd_valid,
  input  wire        reu_mem_cmd_ready,
  output  reg [15:0] reu_mem_wdata,
  output  reg  [1:0] reu_mem_wdata_we,
  input  wire        reu_mem_wdata_ready,
  input  wire [15:0] reu_mem_rdata,
  input  wire        reu_mem_rdata_valid,

  // LEDs
  output wire [4:0] leds,

  // TMDS
  input  wire       tmds_clk,
  input  wire       tmds_rst,
  output wire [9:0] tmds_0,
  output wire [9:0] tmds_1,
  output wire [9:0] tmds_2,

  // USB port
  input  wire usb_clk,
  input  wire usb_rst,
  inout  wire usb_dp_0,
  inout  wire usb_dn_0,
  output wire usb_pullup_dp_0,
  output wire usb_pullup_dn_0,
  inout  wire usb_dp_1,
  inout  wire usb_dn_1,
  output wire usb_pullup_dp_1,
  output wire usb_pullup_dn_1,

  // Flags
  input  wire [15:0] flags,
  input  wire  [2:0] reu_size,          // Gideon's REU size 0 = 128K ... 7 = 16M (c64_reu)

  // IEC serial bus
  output wire iec_data_out,
  output wire iec_clk_out,
  output wire iec_atn_out,
  input  wire iec_data_in,
  input  wire iec_clk_in,
  input  wire iec_atn_in,
  // T170: the fast serial line (SRQ) and the direction of the fast serial port ($D505 bit 3, 1 = this machine sends); '0' = line low
  output wire iec_srq_out,
  input  wire iec_srq_in,
  output wire fs_dir,

  // 1541 drive ROM
  output reg         drive_rom_en,
  output reg  [12:0] drive_rom_addr,
  input  wire [31:0] drive_rom_dout,

  // 1541 drive shared memory
  output wire        drive_shmem_en,
  output wire [10:0] drive_shmem_addr,
  output wire [31:0] drive_shmem_din,
  input  wire [31:0] drive_shmem_dout,
  output reg   [3:0] drive_shmem_we,

  // Block device
  output wire [31:0] block_lba,
  output wire  [5:0] block_cnt,
  output wire        block_rd,
  output wire        block_wr,
  input  wire        block_ack,

  // Floppy disk image
  input  wire [15:0] img_id,
  input  wire        img_readonly,
  input  wire        img_present,
  input  wire        img_mounted,

  // Video overlay
  input  wire [23:0] overlay_pixel,
  output wire [10:0] overlay_pixel_x,
  output wire  [9:0] overlay_pixel_y,
  input  wire        overlay_pixel_valid,

  // HID key report
  output wire       hid_key_report_out,
  output wire [7:0] hid_key_modifiers_out,
  output wire [7:0] hid_key_0_out,

  // Tape (T103, c64u): the cassette port of the machine. tape_read is the level on the CIA 1 FLAG pin (the
  // datasette or the core's own TAP streamer), tape_sense_n the SENSE level the CPU port bit 4 reads (0 = a key
  // is down), tape_motor_n the CPU port bit 5 (0 = motor on), tape_write the CPU port bit 3 (the WRITE line).
  // The streamer and the recorder are outside (cores/c64u/c64u_tape.v); icepi's c64_tape is not used.
  input  wire       tape_read,
  input  wire       tape_sense_n,
  output wire       tape_cass_motor_n,
  output wire       tape_write,

  // POT (T103, c64u): the four comparator levels of the joystick ports (X1, Y1, X2, Y2; 1 = the capacitor has
  // reached the threshold, hypothesis T47 4.6) and the discharge request (the SID's discharge phase)
  input  wire [3:0] pot_cmp,
  output wire       pot_dis,

  // PS2 emulation
  output wire       ps2_fifo_rd_en,
  input  wire [7:0] ps2_fifo_rd_data,
  input  wire       ps2_fifo_rd_valid,

  // Cartridge
  input  wire [15:0] cart_flags,

  // Cartridge flash
  input  wire        flash_enable,
  input  wire [15:0] flash_magic1,
  input  wire [15:0] flash_magic2,
  input  wire [15:0] flash_mask,
  input  wire  [7:0] flash_id_mfr,
  input  wire  [7:0] flash_id_dev,
  input  wire  [7:0] flash_id_ofs,
  output wire [31:0] flash_evt_0,
  output wire        flash_evt_pulse,
  input  wire        flash_ack,
  output wire        eeprom_written,

  // Other
  output wire       kbd_overlay_pulse,
  output wire       kbd_reset_pulse,
  output wire       kbd_tape_play_pulse,

  // Native VIC pixel stream (clk): colour index, raster x, raster line
  output wire [3:0] vic_pix_color,
  output wire [9:0] vic_pix_x,
  output wire [8:0] vic_pix_y,

  // Board keyboard and joysticks (clk domain, 1 = pressed)
  input  wire [63:0] ext_keys,          // bit col*8 + row of the C64 matrix
  input  wire  [4:0] ext_joy1,          // up, down, left, right, fire
  input  wire  [4:0] ext_joy2,
  input  wire        ext_restore,

  // SID audio for an external HDMI encoder (tmds_clk domain): sample enable
  // in, the two sample words out
  input  wire        ext_audio_sample_en,
  output wire signed [23:0] audio_word_0,
  output wire signed [23:0] audio_word_1,

  // T25.1 SID engine (trx64/trx_sid_c64u; not used with SID_REDIP): its clock
  // (2 x clk, same MMCM), the app's UltiSID registers (quasi-static, any
  // clock), the filter curve RAM's write port and the envelope levels
  input  wire        clk2x,
  input  wire [15:0] sid_cfg_base,
  input  wire [15:0] sid_cfg_mask,
  input  wire  [2:0] sid_cfg_split,
  input  wire  [1:0] sid_cfg_8580,
  input  wire  [1:0] sid_cfg_res,
  input  wire  [3:0] sid_cfg_digi,
  input  wire [49:0] sid_cfg_rs8,
  input  wire        sid_cv_clk,
  input  wire        sid_cv_we,
  input  wire [11:0] sid_cv_addr,
  input  wire  [7:0] sid_cv_wdata,
  input  wire  [4:0] sid_lv_addr,
  output wire  [7:0] sid_lv_data,
  input  wire [31:0] sid_mix_gains,       // the app's U64_AUDIO_MIXER bytes 0-3
  input  wire [31:0] sid_mix_gains2,      // bytes 8-11: sampler L / R (T25.6)
  input  wire signed [22:0] smp_l,        // trx_sampler_c64u's sums (clk2x)
  input  wire signed [22:0] smp_r,
  // T37 SID sockets (clk): the app's socket addressing (C64_SID1/2_BASE, _MASK, _EN, quasi-static, in this clock), the
  // hit of this access for the port (slot_port_c64u.v) and the port's last D sample, which answers a socket read
  input  wire [15:0] sock_base,           // {socket 2, socket 1} A11..A4
  input  wire [15:0] sock_mask,
  input  wire  [1:0] sock_en,
  output wire  [1:0] sock_hit,
  input  wire  [7:0] sock_dout,

  // Cartridge IO2 ($DF00-$DFFF) writes while a cartridge serves IO2 (clk):
  // one pulse per write, with the low address byte and the data
  output wire        ext_io2_wr,
  output wire  [7:0] ext_io_addr,
  output wire  [7:0] ext_io_data,

  // DMA for the Ultimate app (clk): one C64 bus cycle per request, through the
  // PLA map of that moment, taken while the CPU is paused and BA is high.
  // done pulses once the cycle is over, rdata holds what was on the bus.
  input  wire        ext_dma_req,
  input  wire [15:0] ext_dma_addr,
  input  wire        ext_dma_we,
  input  wire  [7:0] ext_dma_wdata,
  output reg         ext_dma_done,
  output reg   [7:0] ext_dma_rdata,
  // GAME low, EXROM high whatever the cartridge does: the freezer's Ultimax map
  input  wire        ext_ultimax,
  // the DMA read's select forced, whatever the PLA maps (the app's lens unit,
  // T21 §3.4.1): 1 ROML, 2 ROMH, 3 I/O, 0 as the PLA says; read with the request
  input  wire  [1:0] ext_dma_force,
  // our own $DE00-$DFFF responder (the UCI, c64_core_c128.v), clk: ext_io_sel
  // when the bus master's access of this half is in that range with I/O
  // mapped and really happens (not a CPU held by a pause or the cartridge's
  // wait), ext_io_we its direction; a read takes ext_io_dout while ext_io_oe;
  // ext_io_irq is one more IRQ source
  output wire        ext_io_sel,
  output wire        ext_io_we,
  // T62 G17: the CPU writes $D7FF with I/O mapped (not a REU or app DMA cycle)
  output wire        ext_dbg_we,
  input  wire        ext_io_oe,
  input  wire  [7:0] ext_io_dout,
  input  wire        ext_io_irq,

  // External cartridge (clk): with ext_cart_en it replaces c64_cartridge. It
  // sees the cartridge port's bus and strobes and answers with EXROM/GAME,
  // the read data, NMI/IRQ; ext_cart_wait holds the CPU (RDY) in a read cycle.
  input  wire        ext_cart_en,
  output wire [15:0] ext_cart_addr,
  output wire  [7:0] ext_cart_din,
  output wire        ext_cart_we,
  output wire        ext_cart_roml,
  output wire        ext_cart_romh,
  output wire        ext_cart_io1_cen,
  output wire        ext_cart_io2_cen,
  output wire        ext_cart_phi2_p,
  output wire        ext_cart_phi2_h,
  output wire        ext_cart_phi2_n,
  output wire        ext_cart_reset,
  // for the real expansion port (T16): the VIC drives the address bus, its
  // 12-bit address as the character ROM sees it (valid from RAS on;
  // ext_cart_addr is the DRAM latch, complete only after CAS), and BA
  output wire        ext_cart_vic,
  output wire [11:0] ext_cart_vaddr,
  output wire        ext_cart_ba,
  input  wire        ext_cart_exrom,
  input  wire        ext_cart_game,
  input  wire  [7:0] ext_cart_dout,
  input  wire        ext_cart_nmi,
  input  wire        ext_cart_irq,
  input  wire        ext_cart_io2_we_valid,
  input  wire        ext_cart_wait,

  // For a pause of the whole machine outside (clk): an app DMA cycle is
  // requested and does not end with this clock
  output wire        ext_dma_busy,

  // State for the state bus outside (c64_core_c128.v; ARCH-lean-state-access §5),
  // straight from the chips' registers, so reading it has no side effect:
  // VIC $D000-$D02F as a read returns them ($D000 in bits 7:0); CIA registers
  // 0-15 as a read returns them (register 0 in bits 127:120) and their ports
  // {DDRB, DDRA, PB out, PA out} as they drive the pins; the 6510's A, X, Y,
  // S, P, PC, opcode-fetch cycle and port {$01 as read, $01 latch, $00}; the
  // cartridge's own lines {present, EXROM, GAME} (before ext_ultimax)
  output wire [383:0] sb_vic,
  output wire [39:0]  act_q,            // T194: c128_act.v, per VIC frame the CPU's writes to the VIC, the colour RAM and the VDC (c128_regs.v 0D0)
  output wire [127:0] sb_cia1,
  output wire [127:0] sb_cia2,
  output wire  [31:0] sb_cia1_port,
  output wire  [31:0] sb_cia2_port,
  output wire  [39:0] sb_cpu_regs,      // {P, S, Y, X, A}
  output wire  [39:0] sb_cpu_next,      // the same after the CPU's next update (cpu6510 st_nx)
  output wire  [15:0] sb_cpu_pc,
  output wire         sb_cpu_sync,
  output wire         sb_cpu_intg,      // an interrupt is decided at this fetch
  output wire         sb_cpu_rdy,       // the CPU's RDY (a read cycle completes)
  output wire         sb_cpu_bus,       // the CPU is the bus master (no REU, no app DMA)
  // the frame snap condition's parts (c64_core_c128.v): the VIC's (borders set,
  // no sprite pixel on its way), the CPU's cycle that is ending accessed the
  // VIC, the REU moves data
  // every flip-flop of the CPU (cpu6510.v) and of both CIAs (c64_redip_cia.v)
  // as vectors, and their loads at the next clock (c64_core_c128.v)
  input  wire         sb_ld_cpu,
  input  wire [208:0] sb_stg_cpu,
  output wire [208:0] sb_cur_cpu,
  input  wire         sb_ld_cia1,
  input  wire [362:0] sb_stg_cia1,
  output wire [362:0] sb_cur_cia1,
  input  wire         sb_ld_cia2,
  input  wire [362:0] sb_stg_cia2,
  output wire [362:0] sb_cur_cia2,
  output wire         sb_vic_snap_ok,
  output wire         sb_vic_access,
  output wire         sb_reu_busy,
  output wire  [23:0] sb_cpu_port,      // {$01 read, $01 latch, $00}
  output wire   [2:0] sb_cart,
  // the REU's side of a side-effect-free look at IO2 (T12 §11 Q2): whether a
  // read there would go to the REU now, and its register at pk_reu_addr
  input  wire   [3:0] pk_reu_addr,
  output wire         pk_reu,
  output wire   [7:0] pk_reu_dout,

  // Colour RAM second port (cram_clk, which keeps running while clk is
  // stopped by a machine pause): low nibble, read data one clock later
  input  wire         cram_clk,
  input  wire   [9:0] cram_addr,
  input  wire         cram_we,
  input  wire   [3:0] cram_wdata,
  output reg    [3:0] cram_rdata,
  // the CPU's (port A) colour RAM writes for the write log (trx_engine.v):
  // cram_tap_we high in the clock before the edge that writes, cram_tap_old
  // the nibble that edge overwrote (read-first), valid after it
  output wire         cram_tap_we,
  output wire   [9:0] cram_tap_addr,
  output wire   [3:0] cram_tap_din,
  output wire   [3:0] cram_tap_old,

  // T113: the C128's extras
  input  wire  [23:0] xkeys,            // extra keys, K0-K2 x 8 rows (1 = pressed)
  input  wire         x_capslk,         // the CAPS LOCK key is latched down (P6 of the 8502 reads low)
  input  wire         x_d4080,          // the 40/80 DISPLAY switch is down: 80 columns ($D505 bit 7 reads 0)
  input  wire         x_boot64,         // the machine starts like a C64 (MMU reset state C64 mode on the 8502)
  output wire   [3:0] vdc_rgbi,
  output wire         vdc_ce, vdc_hb, vdc_vb,
  output wire   [1:0] vdc_il,
  output wire         c128_fast,
  output wire   [2:0] c128_stat,
  input  wire         vd_clk,
  input  wire  [15:0] vd_addr,
  input  wire         vd_we,
  input  wire   [7:0] vd_wdata,
  output wire   [7:0] vd_rdata,
  input  wire  [17:0] vk_req,          // T257: the VDC read-back (c128_x.v)
  output wire   [8:0] vk_ans
);

// ---------------------------------------------------------------------------
// vicii_kawari_0
// ---------------------------------------------------------------------------

// T40: 65 x 263 in NTSC and PAL-60; the dot4x rate of the mode (mode_ctl.v)
wire        mode_v60 = (vmode == 2'd1) || (vmode == 2'd2);
reg  [24:0] clk_hz;
always @(posedge clk)
  case (vmode)
    2'd1:    clk_hz <= 25'd32725694;
    2'd2:    clk_hz <= 25'd32765152;
    2'd3:    clk_hz <= 25'd31584821;
    default: clk_hz <= 25'd31527778;
  endcase

wire  [3:0] vic_pix_color_i;
wire        vic_cpu_reset;
wire        vic_stall;
wire        vic_clk_phi;
wire        vic_phi2_l;
wire        vic_phi2_p;
wire        vic_phi2_h;
wire        vic_phi2_n;
wire        vic_hsync;
wire        vic_vsync;
wire  [5:0] vic_adi;
wire [11:0] vic_ado;
wire  [7:0] vic_dbi;
wire  [7:0] vic_dbo;
wire  [3:0] vic_dbh;
wire        vic_cen;
wire        vic_we;
wire        fw_we;                  // T29: the turbo CPU's posted VIC register write; T113 5.6: and the cold start's (zero to every register)
wire  [5:0] fw_a;
wire  [7:0] fw_d;
wire        fw_we_t;
wire  [5:0] fw_a_t;
wire  [7:0] fw_d_t;
assign fw_we = fw_we_t || init_we;
assign fw_a  = init_we ? init_a : fw_a_t;
assign fw_d  = init_we ? 8'd0 : fw_d_t;
wire        vic_irq;
wire        vic_aec;
wire        vic_ba;
wire        vic_ras;
wire        vic_cas;
wire        vic_cas_glitch;
wire        vic_write_db;
wire        vic_write_ab;
wire        vic_clk_usb;
wire        vic_rst_dvi;
wire [10:0] vic_h_count_dvi;
wire  [9:0] vic_v_count_dvi;
wire        vic_overlay_pixel_valid;
wire        vic_audio_sample_en;

wire signed [23:0] sid_audio_sample_word_0;
wire signed [23:0] sid_audio_sample_word_1;

vicii_kawari #(
  .VIDEO_CLK_FREQUENCY(TMDS_CLK_FREQUENCY),
  .AUDIO_RATE(48_000)
) video_kawari_0 (
  .clk_dot4x           (clk),
  .rst_dot4x           (rst),
  .standard_sw         (!mode_v60),
  .reset               (vic_cpu_reset),
  .reset_req           (vic_reset_req || vic_trst),
  .stall               (vic_stall),
  .clk_phi             (vic_clk_phi),
  .phi2_l              (vic_phi2_l),
  .phi2_p              (vic_phi2_p),
  .phi2_h              (vic_phi2_h),
  .phi2_n              (vic_phi2_n),
  .clk_dot_ext         (),
  .hsync               (vic_hsync),
  .vsync               (vic_vsync),
  .adi                 (vic_adi),
  .fw_we               (fw_we),
  .fw_a                (fw_a),
  .fw_d                (fw_d),
  .ado                 (vic_ado),
  .dbi                 (vic_dbi),
  .dbo                 (vic_dbo),
  .dbh                 (vic_dbh),
  .cen                 (vic_cen),
  .we                  (vic_we),
  .irq                 (vic_irq),
  .lp                  (1'b1),
  .aec                 (vic_aec),
  .ba                  (vic_ba),
  .ras                 (vic_ras),
  .cas                 (vic_cas),
  .cas_glitch          (vic_cas_glitch),
  .cas_glitch_disable  (1'b1),
  .vic_write_db        (vic_write_db),
  .vic_write_ab        (vic_write_ab),
  .clk_dvi             (tmds_clk),
  .rst_dvi             (tmds_rst),
  .tmds_0              (tmds_0),
  .tmds_1              (tmds_1),
  .tmds_2              (tmds_2),
  .h_count_dvi         (vic_h_count_dvi),
  .v_count_dvi         (vic_v_count_dvi),
  .overlay_pixel       (overlay_pixel),
  .overlay_pixel_valid (vic_overlay_pixel_valid),
  .audio_sample_word_0 (sid_audio_sample_word_0),
  .audio_sample_word_1 (sid_audio_sample_word_1),
  .audio_sample_en     (vic_audio_sample_en),
  .pix_color           (vic_pix_color_i),
  .pix_x               (vic_pix_x),
  .pix_y               (vic_pix_y),
  .sb_regs             (sb_vic),
  .snap_ok             (sb_vic_snap_ok)
);

assign overlay_pixel_x = vic_h_count_dvi;
assign audio_word_0 = sid_audio_sample_word_0;
assign audio_word_1 = sid_audio_sample_word_1;
assign overlay_pixel_y = vic_v_count_dvi;

assign vic_overlay_pixel_valid = overlay_pixel_valid && flags[6];

// ---------------------------------------------------------------------------
// cpu6510_0
// ---------------------------------------------------------------------------

wire        reu_dma_active;

wire        cpu_reset;
wire [15:0] cpu_addr;
wire  [7:0] cpu_din;
wire  [7:0] cpu_dout;
wire  [5:0] cpu_pin;
wire  [5:0] cpu_pout;
wire  [2:0] cpu_pout3_q;
wire        cpu_we;
wire [15:0] cpu_pc;
wire        cpu_irq;
wire        cpu_nmi;
wire        cpu_nmi_ack;
// dont_touch: the net stays, build.tcl times the paths through it as the two clk2x edges they have
(* dont_touch = "true" *) wire cpu_rdy;

reg         cpu_paused;

// the Ultimate app's DMA cycle (see below)
reg         ext_dma_pend = 1'b0;
reg         ext_dma_cyc  = 1'b0;
reg  [15:0] ext_dma_a;
reg   [1:0] ext_dma_f;
reg         ext_dma_w;
reg   [7:0] ext_dma_d;

wire [7:0] st_a, st_x, st_y, st_s, st_p, st_pdir, st_pout, st_pread;
assign sb_cpu_regs = {st_p, st_s, st_y, st_x, st_a};
assign sb_cpu_pc   = cpu_pc;
assign sb_cpu_port = {st_pread, st_pout, st_pdir};

// T29: the core and the CPU port on clk2x; turbo wires from c64_turbo_c64u (below)
wire        run2, g_edge, t_mode, t_ce, t_sel, skip_pl;
wire  [7:0] t_din, din_r_o, dout_early;
wire        dout_late;
wire        tr_oe;
wire  [7:0] tr_dout;
// T16 Q8: an app DMA write to $0000/$0001 (CPU held) goes into the CPU port as well as the RAM below
// it; the port comes back to the program's when the hold ends (the edge that clears cpu_paused).
wire dp_port_we = ext_dma_cyc && ext_dma_w && ext_dma_a[15:1] == 15'd0 && vic_phi2_n && !vic_stall;
wire dp_port_restore = cpu_paused && !cpu_pause_req && vic_phi2_n && !cpu_we;

wire [15:0] c8502_addr;
wire  [7:0] c8502_dout;
wire        c8502_we;
wire        cpu_ce_x, fast_cap;
wire        c128_n;
cpu6510_c128 #(
  .CPU_MODEL(1)
) cpu6510_0 (
  .ce_x    (cpu_ce_x),
  .cap_x   (fast_cap),
  .clk     (clk),
  .clk2x   (clk2x),
  .g_edge  (g_edge),
  .run2    (run2),
  .skip_pl (skip_pl),
  .t_mode  (t_mode),
  .t_ce    (t_ce),
  .t_sel   (t_sel),
  .t_din   (t_din),
  .din_r_o (din_r_o),
  .dout_early(dout_early), .dout_late(dout_late),
  .reset   (cpu_reset),
  .clk_phi (vic_clk_phi),
  .phi2_l  (vic_phi2_l),
  .phi2_n  (vic_phi2_n && !vic_stall),
  .rdy     (cpu_rdy),
  .irq     (cpu_irq),
  .nmi     (cpu_nmi),
  .nmi_ack (cpu_nmi_ack),
  .din     (cpu_din),
  .dout    (c8502_dout),
  .addr    (c8502_addr),
  .we      (c8502_we),
  .pin     (cpu_pin),
  .p6_in   (!x_capslk),
  .pout    (cpu_pout),
  .pout3_q (cpu_pout3_q),
  .pc      (cpu_pc),
  .st_a    (st_a), .st_x(st_x), .st_y(st_y), .st_s(st_s), .st_p(st_p),
  .st_sync (sb_cpu_sync), .st_intg(sb_cpu_intg),
  .st_pdir (st_pdir), .st_pout(st_pout), .st_pread(st_pread), .st_nx(sb_cpu_next),
  .sb_ld   (sb_ld_cpu), .sb_stg(sb_stg_cpu), .sb_cur(sb_cur_cpu),
  .dp_we   (dp_port_we), .dp_hi(ext_dma_a[0]), .dp_data(ext_dma_d), .dp_restore(dp_port_restore)
);
// T113: the Z80 (T80 + the glue of cpu_z80.vhd) shares the bus with the 8502 as the MMU's CPU bit says (z80_n_mmu: 0 = Z80). The 8502 is held
// (RDY) while the Z80 has the bus; the Z80 is asked to give the bus up (BUSRQ) while the MMU says 8502 and has it back when the bit goes
// 0. z80_act is who has the bus now. The Z80's T-state clock is the 1 MHz bus cycle (CE at the end of the CPU half, outputs latched two
// clocks later, so they are stable for the whole next cycle as the 8502's are): about half the speed of the real machine, spec 5.5.
wire        z80_n_mmu;
`ifdef Z80
wire [15:0] z80_addr;
wire  [7:0] z80_dout;
wire        z80_we, z80_io, z80_m1, z80_rd, z80_busak_n;
// The Z80 owns no bus cycle in which the VIC has the bus (badlines, sprite DMA): the 8502 is held for that by RDY = BA (cpu_rdy), the Z80 by its
// clock enable - a T-state is not taken while BA is low or was low in the cycle before (BA falls three cycles before AEC does and rises with it,
// so the second sample covers the last stolen cycle). Without it the Z80 read the VIC's screen bytes as program and port data whenever the VIC's
// display was on at its start - and it is after a machine reset, which does not reset the VIC (T113 5.6: the BIOS saw $DC01 = $00, "C= pressed",
// and started the C64 mode).
reg         z80_ba_q = 1'b1;
always @(posedge clk) if (vic_phi2_n && !vic_stall) z80_ba_q <= vic_ba;
// T113 5.7: a ROM read that missed the line cache holds the CPU that asked (never the VIC): rom_miss is the pulse in the clock after the command (the
// answer a hit would have given is not there), rom_wait_q keeps it to the end of the 1 MHz cycle. The 8502 sees it as RDY low (its data and RDY are
// taken at phi2_n and, in the 2 MHz mode, at the extra capture), the Z80 as a clock enable that does not come: both repeat the read in the next cycle.
reg         rom_wait_q = 1'b0;
always @(posedge clk) if (vic_phi2_p) rom_wait_q <= 1'b0; else if (rom_miss) rom_wait_q <= 1'b1;
wire        rom_hold = rom_miss || rom_wait_q || ram_hold;
wire        z80_ce  = vic_phi2_n && !vic_stall && vic_ba && z80_ba_q && !rom_wait_q && !ram_hold;
reg   [1:0] z80_lat_d = 2'b00;
always @(posedge clk) z80_lat_d <= {z80_lat_d[0], z80_ce};
wire        z80_lat = z80_lat_d[1];
reg         z80_act = 1'b1;
always @(posedge clk) begin
  if (cpu_reset) z80_act <= !x_boot64;
  else if (z80_lat) z80_act <= z80_busak_n;
end
cpu_z80 z80_0 (
  .clk(clk), .enable(z80_ce), .latch(z80_lat), .reset(cpu_reset), .irq_n(~cpu_irq),
  .io(z80_io), .busrq_n(!z80_n_mmu), .busak_n(z80_busak_n), .m1(z80_m1),
  .di(cpu_din), .dout(z80_dout), .addr(z80_addr), .rd(z80_rd), .we(z80_we));
// T140 5.19 (issue #67): ONE strobe per Z80 bus cycle for the chips whose access has a side effect (the VDC: R31 write/read moves UA and starts a RAM action).
// cpu_z80 holds addr/rd/we until the next latch and an IN/OUT cycle spans two latch periods (T80: IORQ for T2-T3 and the wait state), while bus_en
// (phi2_n) is a strobe at every 1 MHz cycle: every OUT to the VDC's data register was followed by a second strobe at the same address with we = 0 - a READ of
// R31, which moves UA again and replaces the pending write (a character cell never reaches the RAM). z80_fire is set once when a cycle is latched (every write;
// a read whose address, direction or IORQ differs from the previous latch) and taken by the next strobe. It also ends the repeats while ce is withheld (BA low).
reg         z80_lat_q = 1'b0, z80_fire = 1'b0, zp_rd = 1'b0, zp_io = 1'b0;
reg  [15:0] zp_a = 16'd0;
always @(posedge clk) begin
  z80_lat_q <= z80_lat;
  if (z80_lat_q) begin
    zp_a <= z80_addr; zp_rd <= z80_rd; zp_io <= z80_io;
    if (z80_we || (z80_rd && !(zp_rd && zp_a == z80_addr && zp_io == z80_io))) z80_fire <= 1'b1;
  end else if (vic_phi2_n && !vic_stall) z80_fire <= 1'b0;
end
wire        x_bus_en = vic_phi2_n && !vic_stall && (!z80_act || z80_fire);
assign cpu_addr = z80_act ? z80_addr : c8502_addr;
assign cpu_dout = z80_act ? z80_dout : c8502_dout;
assign cpu_we   = z80_act ? z80_we   : c8502_we;
wire z80_io_a = z80_act && z80_io;
`else
wire z80_act = 1'b0;
wire z80_io_a = 1'b0;
wire x_bus_en = vic_phi2_n && !vic_stall;
assign cpu_addr = c8502_addr;
assign cpu_dout = c8502_dout;
assign cpu_we   = c8502_we;
`endif
// ---- T113: the 2 MHz mode ($D030 bit 0; C128 mode, 8502) --------------------------------------------------------------------
// The VIC runs on at 1 MHz and shows nothing; the 8502 gets two bus cycles per 1 MHz cycle: one in the CPU half (the sdram_pending at
// ph 5) and one in the VIC half (at ph 21), where the arbiter gives the memory port to the CPU. Only RAM and ROM cycles can use the VIC
// half; chips, the MMU's registers, the cartridge and the CPU port (io_class) need the 1 MHz cycle and are done in the CPU half, as the
// machine slows to 1 MHz for them. ph counts the clocks since phi2_p. The CPU is clocked (ce_x) at ph 8 and 24 (phi2_h, phi2_l)
// after a capture of the bus data one clock earlier (cap_x); a cycle started at ph 8 whose address turns out to be io_class is not
// done in the VIC half: its capture and clock enable are skipped and it is done in the next CPU half like a 1 MHz cycle.
reg   [5:0] ph = 6'd0;
always @(posedge clk) if (!vic_stall) ph <= vic_phi2_p ? 6'd0 : ph + 6'd1;
wire        io_class;
reg         fast_en = 1'b0, fast_lcyc = 1'b0, fast_hok = 1'b0;
wire        fast_req = c128_fast && !c128_n && !z80_act;
always @(posedge clk) begin
  if (cpu_reset || cpu_paused || reu_dma_active || ext_dma_cyc) begin fast_en <= 1'b0; fast_lcyc <= 1'b0; fast_hok <= 1'b0; end
  else if (!vic_stall) begin
    if (ph == 6'd0) fast_en <= fast_req;
    if (ph == 6'd7) fast_hok <= fast_en && !io_class;
    if (ph == 6'd8 && fast_en && fast_hok) fast_lcyc <= 1'b1;
    if (ph == 6'd24) fast_lcyc <= 1'b0;
  end
end
assign fast_cap = fast_en && !vic_stall && ((ph == 6'd7 && !io_class) || (ph == 6'd23 && fast_lcyc && !io_class));
assign cpu_ce_x = fast_en ? (!vic_stall && ((ph == 6'd8 && fast_hok) || (ph == 6'd24 && !(fast_lcyc && io_class)))) : vic_phi2_l;
// the 2 MHz mode shows the border colour only (the real machine blanks the screen)
assign vic_pix_color = fast_en ? sb_vic[8'h20*8 +: 4] : vic_pix_color_i;

// the arbiter sees the CPU through a stage on gclk (c64_turbo_c64u.v): it follows
// the CPU every clock at normal speed; in turbo it is parked (a RAM read of
// $0000) unless a slow access holds it
wire        st_ld, st_park;
reg  [15:0] addr_a = 16'd0;
reg         we_a   = 1'b0;
reg   [7:0] dout_a = 8'd0;
reg   [5:0] pout_a = 6'd0;
always @(posedge clk) begin
  pout_a <= cpu_pout;
  if (!t_mode) begin
    addr_a <= cpu_addr; we_a <= cpu_we; dout_a <= cpu_dout;
  end else if (st_ld) begin
    addr_a <= cpu_addr; we_a <= cpu_we; dout_a <= cpu_dout;
  end else if (st_park) begin
    addr_a <= 16'd0; we_a <= 1'b0;
  end
end

assign c128_stat = {z80_act, fast_en, c128_n};
assign cpu_rdy = (vic_ba || fast_en) && !cpu_paused && !reu_dma_active && !(ext_cart_en && ext_cart_wait) && !z80_act && z80_n_mmu && !rom_hold;
assign sb_cpu_rdy = cpu_rdy;
assign sb_cpu_bus = !reu_dma_active && !ext_dma_cyc;

// a read of a CPU held by RDY (pause, cartridge wait) does not happen on the
// bus for ext_io; one held by BA does (the arbiter maps no I/O for it)
wire io_ext_sel, io_dbg_sel;
assign ext_dbg_we = io_dbg_sel && ext_io_we && cpu_rdy && !reu_dma_active && !ext_dma_cyc;
assign ext_io_sel = io_ext_sel && (cpu_rdy || ext_io_we || reu_dma_active || ext_dma_cyc);

// a VIC register access in the CPU half that ends at the next phi2 falling edge
reg vic_half = 1'b0, vic_acc = 1'b0;
always @(posedge clk) begin
  if (vic_phi2_p) begin
    vic_half <= 1'b1;
    vic_acc  <= 1'b0;
  end else if (vic_phi2_n)
    vic_half <= 1'b0;
  if (vic_half && !vic_cen)
    vic_acc <= 1'b1;
end
assign sb_vic_access = vic_acc || (vic_half && !vic_cen);
assign sb_reu_busy   = reu_dma_active;

// ---------------------------------------------------------------------------
// SID: the T25.1 engine (8 UltiSIDs, trx64/trx_sid_c64u), or reDIP with SID_REDIP
// ---------------------------------------------------------------------------

wire [9:0] sid_addr;
wire [7:0] sid_din;
wire [7:0] sid_dout;
wire       sid_cen;
wire       sid_we;
wire       sid_hit;
wire       sid_model     = flags[1];
wire       sid_dual      = flags[2];
wire       sid_pan       = flags[3];
wire       sid_auto_mono = flags[4];
wire       sid_pot_x;
wire       sid_pot_y;
// T29: the turbo CPU's fast SID path (c64_turbo_c64u.v, trx_sid_c64u.v)
wire       sid_ps_v;
wire [9:0] sid_ps_a;
wire [7:0] sid_ps_d;
wire       sid_fhit, sid_f_full, sid_f_empty, sid_f_quiet;
wire [7:0] sid_frd;
// T37: a socket hit inside $D400-$D7FF (sid_cen low), base compared unmasked as trx_sid_c64u.v does. The socket answers a
// read before an UltiSID at the same address (T37 Q5); a write goes to both. The turbo CPU's access to a socket address is
// a bus cycle: its fast SID path is closed there.
wire [7:0] sock_a = {2'b01, sid_addr[9:4]};
wire [7:0] sock_r = {2'b01, cpu_addr[9:4]};
assign sock_hit[0] = sock_en[0] && !sid_cen && ((sock_a & sock_mask[7:0]) == sock_base[7:0]);
assign sock_hit[1] = sock_en[1] && !sid_cen && ((sock_a & sock_mask[15:8]) == sock_base[15:8]);
wire       sock_ra = (sock_en[0] && ((sock_r & sock_mask[7:0]) == sock_base[7:0])) ||
                     (sock_en[1] && ((sock_r & sock_mask[15:8]) == sock_base[15:8]));
wire       sid_hit_bus  = sid_hit || sock_hit != 2'b00;
wire [7:0] sid_dout_bus = sock_hit != 2'b00 ? sock_dout : sid_dout;

`ifdef SID_REDIP
assign pot_dis = 1'b0;
assign sid_hit = 1'b1;
assign sid_fhit = 1'b0;      // the two-SID reDIP has no op FIFO: every SID access of the turbo CPU is a bus cycle
assign sid_frd = 8'd0;
assign sid_f_full = 1'b0;
assign sid_f_empty = 1'b1;
assign sid_f_quiet = 1'b1;
assign sid_lv_data = 8'd0;
c64_redip_sid c64_redip_sid_0 (
  .clk                 (clk),
  .reset               (cpu_reset),
  .clk_phi             (vic_clk_phi),
  .phi2_n              (vic_phi2_n && !vic_stall),
  .clk_sample          (tmds_clk),
  .reset_sample        (tmds_rst),
  .cen                 (sid_cen),
  .we                  (sid_we),
  .addr                (sid_addr[8:0]),
  .din                 (sid_din),
  .dout                (sid_dout),
  .audio_sample_en     (ext_audio_sample_en),
  .audio_sample_word_0 (sid_audio_sample_word_0),
  .audio_sample_word_1 (sid_audio_sample_word_1),
  .sid_model           (sid_model),
  .sid_dual            (sid_dual),
  .sid_pan             (sid_pan),
  .sid_auto_mono       (sid_auto_mono),
  .pot_x               (sid_pot_x),
  .pot_y               (sid_pot_y)
);
`else
c64_trx_sid_c64u c64_trx_sid_0 (
  .clk                 (clk),
  .reset               (cpu_reset),
  .clk2x               (clk2x),
  .vmode               (vmode),
  .phi2_n              (vic_phi2_n && !vic_stall),
  .clk_sample          (tmds_clk),
  .reset_sample        (tmds_rst),
  .cen                 (sid_cen),
  .we                  (sid_we),
  .addr                (sid_addr),
  .din                 (sid_din),
  .dout                (sid_dout),
  .hit                 (sid_hit),
  .ps_v                (sid_ps_v),
  .ps_a                (sid_ps_a),
  .ps_d                (sid_ps_d),
  .ra                  (cpu_addr[9:0]),
  .fhit                (sid_fhit),
  .frd                 (sid_frd),
  .f_full              (sid_f_full),
  .f_empty             (sid_f_empty),
  .f_quiet             (sid_f_quiet),
  .audio_sample_en     (ext_audio_sample_en),
  .audio_sample_word_0 (sid_audio_sample_word_0),
  .audio_sample_word_1 (sid_audio_sample_word_1),
  .pot_x               (sid_pot_x),
  .pot_y               (sid_pot_y),
  .pot_dis             (pot_dis),
  .cfg_base            (sid_cfg_base),
  .cfg_mask            (sid_cfg_mask),
  .cfg_split           (sid_cfg_split),
  .cfg_8580            (sid_cfg_8580),
  .cfg_res_high        (sid_cfg_res),
  .cfg_digi            (sid_cfg_digi),
  .cfg_rs8             (sid_cfg_rs8),
  .cv_clk              (sid_cv_clk),
  .cv_we               (sid_cv_we),
  .cv_addr             (sid_cv_addr),
  .cv_wdata            (sid_cv_wdata),
  .lv_addr             (5'd0),
  .lv_data             (),
  .mix_gains           (sid_mix_gains),
  .smp_l               (smp_l),
  .smp_r               (smp_r),
  .mix_gains2          (sid_mix_gains2)
);
// the voice levels stay 0: the host reads them combinationally across the partition edge in one cpu_clk
// (ultimate_sys.vhd), routed -1.36 ns in this floorplan; the app reads them only for the LED strip, which is
// out (ADR-0007). T103 §5, Alex 2026-10-18.
assign sid_lv_data = 8'd0;
`endif

// ---------------------------------------------------------------------------
// T29: a CIA access the turbo CPU has posted (c64_turbo_c64u.v) does not go through the
// arbiter's decode: while pk_g is set the CIA's inputs are the stage's, whatever the
// VIC does with the bus (BA low, AEC low) and whatever the PLA says by then. pk_g is set
// with the stage load and cleared at the PHI2 fall that completes the access.
// ---------------------------------------------------------------------------
wire [3:0] cia1_addr_a, cia2_addr_a;
wire [7:0] cia1_din_a, cia2_din_a;
wire       cia1_cen_a, cia2_cen_a, cia1_we_a, cia2_we_a;
wire       st_post;
wire       pk_ok = !reu_dma_active && !ext_dma_cyc;
reg        pk_g = 1'b0;
always @(posedge clk) begin
  if (cpu_reset || !t_mode) pk_g <= 1'b0;
  else if (st_ld && st_post) pk_g <= 1'b1;
  else if (vic_phi2_n && !vic_stall && pk_ok) pk_g <= 1'b0;
end
wire pk_c2 = addr_a[8];
assign cia1_cen  = pk_g ? pk_c2 : cia1_cen_a;
assign cia2_cen  = pk_g ? !pk_c2 : cia2_cen_a;
assign cia1_we   = pk_g ? we_a : cia1_we_a;
assign cia2_we   = pk_g ? we_a : cia2_we_a;
assign cia1_addr = pk_g ? addr_a[3:0] : cia1_addr_a;
assign cia2_addr = pk_g ? addr_a[3:0] : cia2_addr_a;
assign cia1_din  = pk_g ? dout_a : cia1_din_a;
assign cia2_din  = pk_g ? dout_a : cia2_din_a;

// ---------------------------------------------------------------------------
// c64_redip_cia_0  (CIA1, GENERATE_TOD=1)
// ---------------------------------------------------------------------------

wire [3:0] cia1_addr;
wire [7:0] cia1_din;
wire [7:0] cia1_dout;
wire       cia1_cen;
wire       cia1_we;
wire [7:0] cia1_pa_in;
wire [7:0] kbd_pa_in;
wire [7:0] kbd_pb_in;
wire [7:0] cia1_pa_out;
wire [7:0] cia1_ddra;
wire [7:0] cia1_pb_in;
wire [7:0] cia1_pb_out;
wire [7:0] cia1_ddrb;
wire       cia1_tod_out;
wire       cia1_flag_n;
wire       cia1_sp_in, cia1_cnt_in, cia1_sp_out, cia1_cnt_out;   // T170: CIA 1's serial register faces the bus's DATA and SRQ (c128_iecfs.v)
wire       cia1_irq;
wire       cia1_model = flags[0];

c64_redip_cia #(
  .GENERATE_TOD(1),
  .CLK_FREQUENCY(SYS_CLK_FREQUENCY)
) c64_redip_cia_0 (
  .clk       (clk),
  .reset     (cpu_reset),
  .clk_phi   (vic_clk_phi),
  .phi2_p    (vic_phi2_p && !vic_stall),
  .phi2_n    (vic_phi2_n && !vic_stall),
  .cen       (cia1_cen),
  .we        (cia1_we),
  .addr      (cia1_addr),
  .din       (cia1_din),
  .dout      (cia1_dout),
  .pa_in     (cia1_pa_in),
  .pa_out    (cia1_pa_out),
  .ddra      (cia1_ddra),
  .pb_in     (cia1_pb_in),
  .pb_out    (cia1_pb_out),
  .ddrb      (cia1_ddrb),
  .sp_in     (cia1_sp_in),
  .sp_out    (cia1_sp_out),
  .cnt_in    (cia1_cnt_in),
  .cnt_out   (cia1_cnt_out),
  .flag_n    (cia1_flag_n),
  .pc_n      (),
  .irq       (cia1_irq),
  .tod_out   (cia1_tod_out),
  .tod_in    (1'b0),
  .cia_model (cia1_model),
  .clk_freq  (clk_hz),
  .tod60     (mode_v60),
  .sb_regs   (sb_cia1),
  .sb_ld     (sb_ld_cia1), .sb_stg(sb_stg_cia1), .sb_cur(sb_cur_cia1)
);
assign sb_cia1_port = {cia1_ddrb, cia1_ddra, cia1_pb_out, cia1_pa_out};

// ---------------------------------------------------------------------------
// c64_redip_cia_1  (CIA2, GENERATE_TOD=0)
// ---------------------------------------------------------------------------

wire [3:0] cia2_addr;
wire [7:0] cia2_din;
wire [7:0] cia2_dout;
wire       cia2_cen;
wire       cia2_we;
wire [7:0] cia2_pa_in;
wire [7:0] cia2_pa_out;
wire [7:0] cia2_ddra;
wire [7:0] cia2_pb_in;
wire [7:0] cia2_pb_out;
wire [7:0] cia2_ddrb;
wire       cia2_flag_n;
wire       cia2_pc_n;
wire       cia2_irq;
wire       cia2_model = flags[0];

c64_redip_cia #(
  .GENERATE_TOD(0),
  .CLK_FREQUENCY(SYS_CLK_FREQUENCY)
) c64_redip_cia_1 (
  .clk       (clk),
  .reset     (cpu_reset),
  .clk_phi   (vic_clk_phi),
  .phi2_p    (vic_phi2_p && !vic_stall),
  .phi2_n    (vic_phi2_n && !vic_stall),
  .cen       (cia2_cen),
  .we        (cia2_we),
  .addr      (cia2_addr),
  .din       (cia2_din),
  .dout      (cia2_dout),
  .pa_in     (cia2_pa_in),
  .pa_out    (cia2_pa_out),
  .ddra      (cia2_ddra),
  .pb_in     (cia2_pb_in),
  .pb_out    (cia2_pb_out),
  .ddrb      (cia2_ddrb),
  .sp_in     (1'b1),
  .sp_out    (),
  .cnt_in    (1'b1),
  .cnt_out   (),
  .flag_n    (cia2_flag_n),
  .pc_n      (cia2_pc_n),
  .irq       (cia2_irq),
  .tod_out   (),
  .tod_in    (cia1_tod_out),
  .cia_model (cia2_model),
  .clk_freq  (clk_hz),
  .tod60     (mode_v60),
  .sb_regs   (sb_cia2),
  .sb_ld     (sb_ld_cia2), .sb_stg(sb_stg_cia2), .sb_cur(sb_cur_cia2)
);
assign sb_cia2_port = {cia2_ddrb, cia2_ddra, cia2_pb_out, cia2_pa_out};

// ---------------------------------------------------------------------------
// c64_keyboard_0
// ---------------------------------------------------------------------------

wire       kbd_restore_toggle;
wire       kbd_freeze_pulse;
wire       joy_pot_x;
wire       joy_pot_y;
wire [9:0] joy_a;
wire [9:0] joy_b;
wire       hid_key_report;
wire [7:0] hid_key_modifiers;
wire       hid_key_alt;
wire [7:0] hid_key_0;
wire [7:0] hid_key_1;
wire [7:0] hid_key_2;
wire [7:0] hid_key_3;
wire [7:0] hid_key_4;
wire [7:0] hid_key_5;

c64_keyboard #(
  .CLK_FREQUENCY(SYS_CLK_FREQUENCY)
) c64_keyboard_0 (
  .clk                  (clk),
  .reset                (rst || vic_reset_req),
  .clk_freq             (clk_hz),
  .enable               (!flags[6] && !hid_key_alt),
  .pa_out               (cia1_pa_out),
  .pa_in                (kbd_pa_in),
  .ddra                 (cia1_ddra),
  .pb_out               (cia1_pb_out),
  .pb_in                (kbd_pb_in),
  .ddrb                 (cia1_ddrb),
  .restore_toggle       (kbd_restore_toggle),
  .restore_toggle_ack   (cpu_nmi_ack),
  .tape_play_pulse      (kbd_tape_play_pulse),
  .overlay_pulse        (kbd_overlay_pulse),
  .reset_pulse          (kbd_reset_pulse),
  .freeze_pulse         (kbd_freeze_pulse),
  .pot_x                (joy_pot_x),
  .pot_y                (joy_pot_y),
  .joy_emulation        (flags[10:9]),
  .joy_invert           (flags[7]),
  .joy_button_space     (flags[8]),
  .joy_a                (joy_a | {5'b0, ext_joy1}),
  .joy_b                (joy_b | {5'b0, ext_joy2}),
  .hid_key_report       (hid_key_report),
  .hid_key_modifiers    (hid_key_modifiers),
  .hid_key_0            (hid_key_0),
  .hid_key_1            (hid_key_1),
  .hid_key_2            (hid_key_2),
  .hid_key_3            (hid_key_3),
  .hid_key_4            (hid_key_4),
  .hid_key_5            (hid_key_5),
  .ps2_fifo_rd_en       (ps2_fifo_rd_en),
  .ps2_fifo_rd_data     (ps2_fifo_rd_data),
  .ps2_fifo_rd_valid    (ps2_fifo_rd_valid)
);

// The paddle port in use is the one CIA 1 port A selects (bit 6 = port 1, bit 7 = port 2, as the keyboard's
// POT lines do); the comparator of a port is OR-ed with the joystick buttons that pull the line.
wire pot_sel1 = cia1_pa_out[6] || !cia1_ddra[6];
wire pot_sel2 = cia1_pa_out[7] || !cia1_ddra[7];
assign sid_pot_x = joy_pot_x || (pot_sel1 && pot_cmp[0]) || (pot_sel2 && pot_cmp[2]);
assign sid_pot_y = joy_pot_y || (pot_sel1 && pot_cmp[1]) || (pot_sel2 && pot_cmp[3]);

// ---------------------------------------------------------------------------
// the cassette port (c64u: the streamer and the recorder are outside, c64u_tape.v)
// ---------------------------------------------------------------------------

wire tape_cass_sense_n = tape_sense_n;
wire tape_act = !tape_cass_motor_n;
// T170: fast serial. SRQ and DATA into CIA 1's CNT / SP / FLAG, its CNT and SP out onto SRQ and DATA, by FSDIR (c128_iecfs.v)
wire fs_data_gate;
wire iec_data_out_a;                    // the machine's DATA pull before CIA 1's serial register adds its own
assign iec_data_out = iec_data_out_a & fs_data_gate;
c128_iecfs iecfs (
  .fs_dir(fs_dir), .c128_n(c128_n), .srq_bus(iec_srq_in), .data_bus(iec_data_in), .cnt_out(cia1_cnt_out), .sp_out(cia1_sp_out),
  .tape_read(tape_read), .cnt_in(cia1_cnt_in), .sp_in(cia1_sp_in), .flag_n(cia1_flag_n), .srq_out(iec_srq_out), .data_gate(fs_data_gate)
);
assign tape_write = cpu_pout[3];

// ---------------------------------------------------------------------------
// c64_c1541_0
// ---------------------------------------------------------------------------

wire c1541_led;
wire c1541_busy;
wire c1541_mtr;

wire [2:0] cia2_iec_lines;
wire c1541_iec_atn_out;
wire c1541_iec_data_out;
wire c1541_iec_clk_out;
wire c1541_iec_data_in;
wire c1541_iec_clk_in;

wire  [7:0] c1541_par_data_in;
wire        c1541_par_stb_in;
wire  [7:0] c1541_par_data_out;
wire        c1541_par_stb_out;
reg         c1541_ext_en;

wire        c1541_rom_en;
wire [14:0] c1541_rom_addr;
reg   [7:0] c1541_rom_dout;

wire [12:0] buff_addr;
reg   [7:0] buff_dout;
wire  [7:0] buff_din;
wire        buff_we;
wire        buff_en;

generate if (C1541) begin : g_c1541
c64_c1541 #(
  .EXTERNAL_ROM(C1541_EXTERNAL_ROM)
) c64_c1541_0 (
  .clk          (clk),
  .reset        (cpu_reset),
  .stall        (vic_stall),
  .img_id       (img_id),
  .img_readonly (img_readonly),
  .img_present  (img_present),
  .img_mounted  (img_mounted),
  .led          (c1541_led),
  .busy         (c1541_busy),
  .mtr          (c1541_mtr),
  .iec_atn_i    (c1541_iec_atn_out),
  .iec_data_i   (c1541_iec_data_out),
  .iec_clk_i    (c1541_iec_clk_out),
  .iec_data_o   (c1541_iec_data_in),
  .iec_clk_o    (c1541_iec_clk_in),
  .par_data_i   (c1541_par_data_out),
  .par_stb_i    (c1541_par_stb_out),
  .par_data_o   (c1541_par_data_in),
  .par_stb_o    (c1541_par_stb_in),
  .ext_en       (c1541_ext_en),
  .block_lba    (block_lba),
  .block_cnt    (block_cnt),
  .block_rd     (block_rd),
  .block_wr     (block_wr),
  .block_ack    (block_ack),
  .buff_addr    (buff_addr),
  .buff_dout    (buff_dout),
  .buff_din     (buff_din),
  .buff_we      (buff_we),
  .buff_en      (buff_en),
  .ext_rom_en   (c1541_rom_en),
  .ext_rom_addr (c1541_rom_addr),
  .ext_rom_dout (c1541_rom_dout)
);
end else begin : g_no_c1541
assign c1541_led          = 1'b0;
assign c1541_busy         = 1'b0;
assign c1541_mtr          = 1'b0;
assign c1541_iec_data_in  = 1'b1;
assign c1541_iec_clk_in   = 1'b1;
assign c1541_par_data_in  = 8'hff;
assign c1541_par_stb_in   = 1'b1;
assign c1541_rom_en       = 1'b0;
assign c1541_rom_addr     = 15'd0;
assign block_lba          = 32'd0;
assign block_cnt          = 6'd0;
assign block_rd           = 1'b0;
assign block_wr           = 1'b0;
assign buff_addr          = 13'd0;
assign buff_din           = 8'd0;
assign buff_we            = 1'b0;
assign buff_en            = 1'b0;
end endgenerate

always @(posedge clk) begin
  if (c1541_ext_en)
    // adjust 32KiB ROM address to 24KiB SRAM address
    drive_rom_addr <= (c1541_rom_addr[14:2] - 13'h800);
  else
    drive_rom_addr <= {1'b0, c1541_rom_addr[13:2]};

  drive_rom_en <= c1541_rom_en;
end

always @(*) begin
  if (c1541_ext_en && c1541_rom_addr < 15'h2000)
    // first 8192 bytes for DolphinDOS are set to 0xFF
    c1541_rom_dout = 8'hff;

  else begin
    case (c1541_rom_addr[1:0])
      2'b00: begin
        c1541_rom_dout = drive_rom_dout[7:0];
      end
      2'b01: begin
        c1541_rom_dout = drive_rom_dout[15:8];
      end
      2'b10: begin
        c1541_rom_dout = drive_rom_dout[23:16];
      end
      2'b11: begin
        c1541_rom_dout = drive_rom_dout[31:24];
      end
    endcase
  end
end

assign drive_shmem_en   = buff_en;
assign drive_shmem_addr = buff_addr[12:2];
assign drive_shmem_din  = {buff_din, buff_din, buff_din, buff_din};

always @(*) begin
  drive_shmem_we = 4'b0000;

  case (buff_addr[1:0])
    2'b00: begin
      buff_dout = drive_shmem_dout[7:0];
      drive_shmem_we[0] = buff_we;
    end
    2'b01: begin
      buff_dout = drive_shmem_dout[15:8];
      drive_shmem_we[1] = buff_we;
    end
    2'b10: begin
      buff_dout = drive_shmem_dout[23:16];
      drive_shmem_we[2] = buff_we;
    end
    2'b11: begin
      buff_dout = drive_shmem_dout[31:24];
      drive_shmem_we[3] = buff_we;
    end
  endcase
end

// ---------------------------------------------------------------------------
// usb_hid_host_dual_0
// ---------------------------------------------------------------------------

wire [1:0] usb_dm_i_bus;
wire [1:0] usb_dp_i_bus;
wire [1:0] usb_dm_o_bus;
wire [1:0] usb_dp_o_bus;
wire [1:0] usb_oe_bus;

wire [1:0] usb_connected;

wire [5:0] hid_key_mask;

assign usb_pullup_dp_0 = 1'b0;
assign usb_pullup_dn_0 = 1'b0;
assign usb_pullup_dp_1 = 1'b0;
assign usb_pullup_dn_1 = 1'b0;

assign usb_dm_i_bus = {usb_dn_1, usb_dn_0};
assign usb_dp_i_bus = {usb_dp_1, usb_dp_0};

assign usb_dn_0 = usb_oe_bus[0] ? usb_dm_o_bus[0] : 1'bz;
assign usb_dp_0 = usb_oe_bus[0] ? usb_dp_o_bus[0] : 1'bz;
assign usb_dn_1 = usb_oe_bus[1] ? usb_dm_o_bus[1] : 1'bz;
assign usb_dp_1 = usb_oe_bus[1] ? usb_dp_o_bus[1] : 1'bz;

usb_hid_host_dual usb_hid_host_dual_0 (
  .clk           (clk),
  .reset         (rst || vic_reset_req),
  .usb_clk       (usb_clk),
  .usb_rst       (usb_rst),
  .usb_dm_i      (usb_dm_i_bus),
  .usb_dp_i      (usb_dp_i_bus),
  .usb_dm_o      (usb_dm_o_bus),
  .usb_dp_o      (usb_dp_o_bus),
  .usb_oe        (usb_oe_bus),
  .connerr_0     (),
  .connerr_1     (),
  .connected_0   (usb_connected[0]),
  .connected_1   (usb_connected[1]),
  .game_u_0      (joy_a[0]),
  .game_d_0      (joy_a[1]),
  .game_l_0      (joy_a[2]),
  .game_r_0      (joy_a[3]),
  .game_a_0      (joy_a[4]),
  .game_b_0      (joy_a[5]),
  .game_x_0      (joy_a[6]),
  .game_y_0      (joy_a[7]),
  .game_sel_0    (joy_a[8]),
  .game_sta_0    (joy_a[9]),
  .game_u_1      (joy_b[0]),
  .game_d_1      (joy_b[1]),
  .game_l_1      (joy_b[2]),
  .game_r_1      (joy_b[3]),
  .game_a_1      (joy_b[4]),
  .game_b_1      (joy_b[5]),
  .game_x_1      (joy_b[6]),
  .game_y_1      (joy_b[7]),
  .game_sel_1    (joy_b[8]),
  .game_sta_1    (joy_b[9]),
  .key_report    (hid_key_report),
  .key_modifiers (hid_key_modifiers),
  .key_0         (hid_key_0),
  .key_1         (hid_key_1),
  .key_2         (hid_key_2),
  .key_3         (hid_key_3),
  .key_4         (hid_key_4),
  .key_5         (hid_key_5),
  .mouse_report  (),
  .mouse_btn     (),
  .mouse_dx      (),
  .mouse_dy      ()
);

assign hid_key_mask = {hid_key_0 != 0, hid_key_1 != 0, hid_key_2 != 0, hid_key_3 != 0, hid_key_4 != 0, hid_key_5 != 0};

assign hid_key_report_out = hid_key_report && (flags[6] || hid_key_alt) && ((hid_key_mask & (hid_key_mask - {5'b0, 1'b1})) == 0);

assign hid_key_modifiers_out = hid_key_modifiers;
assign hid_key_alt           = |(hid_key_modifiers & 8'h44);
assign hid_key_0_out         = hid_key_0 | hid_key_1 | hid_key_2 | hid_key_3 | hid_key_4 | hid_key_5;

// ---------------------------------------------------------------------------
// c64_reu_0
// ---------------------------------------------------------------------------

wire [3:0] reu_addr;
wire [7:0] reu_din;
wire [7:0] reu_dout;
wire       reu_we;
wire       reu_cen;
wire       reu_irq;

wire [15:0] reu_dma_addr;
wire        reu_dma_we;

wire [23:0] reu_mem_addr;                 // driven by c64_reu
wire  [7:0] reu_mem_din;
reg   [7:0] reu_mem_dout;

wire        reu_mem_we;
wire        reu_mem_valid;
reg         reu_mem_ready;
reg         reu_mem_rvalid;

`ifdef NO_REU
// T79 build switch: no REU. Its registers read $FF, it never takes the bus
// and never asks the DDR2 for anything.
assign reu_dout       = 8'hff;
assign pk_reu_dout    = 8'hff;
assign reu_dma_addr   = 16'd0;
assign reu_dma_we     = 1'b0;
assign reu_dma_active = 1'b0;
assign reu_mem_we     = 1'b0;
assign reu_irq        = 1'b0;
assign reu_mem_addr   = 24'd0;
assign reu_mem_din    = 8'd0;
assign reu_mem_valid  = 1'b0;
`else
c64_reu c64_reu_0 (
  .clk              (clk),
  .rst              (cpu_reset),
  .phi2_p           (vic_phi2_p && !vic_stall),
  .phi2_h           (vic_phi2_h),
  .phi2_n           (vic_phi2_n && !vic_stall),
  .phi2_l           (vic_phi2_l),
  .vic_ba           (vic_ba),
  .reu_addr         (reu_addr),
  .reu_din          (reu_din),
  .reu_dout         (reu_dout),
  .reu_we           (reu_we),
  .reu_cen          (reu_cen),
  .pk_addr          (pk_reu_addr),
  .pk_dout          (pk_reu_dout),
  .reu_dma_addr     (reu_dma_addr),
  .reu_dma_we       (reu_dma_we),
  .reu_dma_active   (reu_dma_active),
  .reu_mem_addr     (reu_mem_addr),
  .reu_mem_din      (reu_mem_din),
  .reu_mem_dout     (reu_mem_dout),
  .reu_mem_we       (reu_mem_we),
  .reu_mem_valid    (reu_mem_valid),
  .reu_mem_ready    (reu_mem_ready),
  .reu_mem_rvalid   (reu_mem_rvalid),
  .ff00_trigger     (addr_a == 16'hff00),
  .reu_size         (reu_size),
  .irq              (reu_irq)
);
`endif

always @(*) begin
  reu_mem_cmd_addr  = {1'b0, reu_mem_addr[23:1]};     // 16-bit word address of the 16 MB
  reu_mem_cmd_we    = reu_mem_we;
  reu_mem_cmd_valid = reu_mem_valid;

  reu_mem_wdata     = {reu_mem_din, reu_mem_din};
  reu_mem_wdata_we  = reu_mem_addr[0] ? 2'b10 : 2'b01;
  reu_mem_rvalid    = reu_mem_rdata_valid;

  reu_mem_dout      = reu_mem_addr[0] ? reu_mem_rdata[15:8] : reu_mem_rdata[7:0];
  reu_mem_ready     = reu_mem_cmd_ready;
end

// ---------------------------------------------------------------------------
// c64_cartridge_0
// ---------------------------------------------------------------------------

wire [21:0] rom_cart_addr;
wire        rom_cart_enable;
reg         rom_cart_ready;
reg  [7:0]  rom_cart_dout;
wire        rom_cart_select;

wire [14:0] ram_cart_addr;
wire  [7:0] ram_cart_wdata;
wire        ram_cart_wdata_own;
wire        ram_cart_enable;
reg         ram_cart_we;
reg         ram_cart_ready;
reg  [7:0]  ram_cart_dout;
wire        ram_cart_select;

wire        cart_nmi;
wire        cart_irq;
wire        cart_freeze;
wire        cart_io2_we_valid;

wire        sdram_pending;

wire        int_cart_exrom, int_cart_game, int_cart_nmi, int_cart_irq, int_cart_io2_we_valid;
wire  [7:0] int_cart_dout;

c64_cartridge c64_cartridge_0 (
  .clk           (clk),
  .reset         (cpu_reset),
  .phi2_p        (vic_phi2_p && !vic_stall),
  .phi2_h        (vic_phi2_h),
  .phi2_n        (vic_phi2_n && !vic_stall),
  .sdram_pending (sdram_pending),
  .desc          (344'd0),       // icepi's own cartridge: no descriptor (the TRX64 board uses the external one)
  .flash_enable    (flash_enable),
  .flash_magic1    (flash_magic1),
  .flash_magic2    (flash_magic2),
  .flash_mask      (flash_mask),
  .flash_id_mfr    (flash_id_mfr),
  .flash_id_dev    (flash_id_dev),
  .flash_id_ofs    (flash_id_ofs),
  .flash_evt_0     (flash_evt_0),
  .flash_evt_pulse (flash_evt_pulse),
  .flash_ack       (flash_ack),
  .eeprom_written  (eeprom_written),
  .addr          (cart_addr),
  .din           (cart_din),
  .dout          (int_cart_dout),
  .we            (cart_we),
  .ba            (cart_ba),
  .exrom         (int_cart_exrom),
  .game          (int_cart_game),
  .freeze        (cart_freeze),
  .roml          (cart_roml),
  .romh          (cart_romh),
  .io1_cen       (cart_io1_cen),
  .io2_cen       (cart_io2_cen),
  .io2_we_valid  (int_cart_io2_we_valid),
  .nmi           (int_cart_nmi),
  .irq           (int_cart_irq),
  .rom_addr      (rom_cart_addr),
  .rom_enable    (rom_cart_enable),
  .rom_ready     (rom_cart_ready),
  .rom_dout      (rom_cart_dout),
  .rom_select    (rom_cart_select),
  .ram_addr      (ram_cart_addr),
  .ram_enable    (ram_cart_enable),
  .ram_we        (ram_cart_we),
  .ram_wdata     (ram_cart_wdata),
  .ram_wdata_own (ram_cart_wdata_own),
  .ram_ready     (ram_cart_ready),
  .ram_dout      (ram_cart_dout),
  .ram_select    (ram_cart_select)
);

// ---------------------------------------------------------------------------
// c64_bus_arbiter_0
// ---------------------------------------------------------------------------

wire [9:0]  ram_color_addr;
wire [3:0]  ram_color_din;
wire [3:0]  ram_color_dout;
wire        ram_color_select;
wire        ram_color_we;

wire [15:0] ram_addr;
reg  [7:0]  ram_dout;
wire [7:0]  ram_din;
wire        ram_we;
wire        ram_enable;
reg         ram_ready;
wire        ram_select;

wire [12:0] rom_basic_addr;
wire        rom_basic_enable;
reg         rom_basic_ready;
reg  [7:0]  rom_basic_dout;
wire        rom_basic_select;

wire [16:0] rom_kernal_addr;
wire        rom_kernal_enable;
reg         rom_kernal_ready;
reg  [7:0]  rom_kernal_dout;
wire        rom_kernal_select;

wire [11:0] rom_char_addr;
wire        rom_char_enable;
reg         rom_char_ready;
reg  [7:0]  rom_char_dout;
wire        rom_char_select;

wire        cart_game;
wire        cart_exrom;
wire        cart_io1_cen;
wire        cart_io2_cen;
wire        cart_roml;
wire        cart_romh;
wire  [7:0] cart_dout;
wire  [7:0] cart_din;
wire        cart_ba;
wire        cart_we;
wire [15:0] cart_addr;
reg         cart_present;

assign cart_exrom        = ext_cart_en ? ext_cart_exrom        : int_cart_exrom;
assign cart_game         = ext_cart_en ? ext_cart_game         : int_cart_game;
assign cart_dout         = ext_cart_en ? ext_cart_dout         : int_cart_dout;
assign cart_nmi          = ext_cart_en ? ext_cart_nmi          : int_cart_nmi;
assign cart_irq          = ext_cart_en ? ext_cart_irq          : int_cart_irq;
assign cart_io2_we_valid = ext_cart_en ? ext_cart_io2_we_valid : int_cart_io2_we_valid;

assign ext_cart_addr    = cart_addr;
assign ext_cart_din     = cart_din;
assign ext_cart_we      = cart_we;
assign ext_cart_roml    = cart_roml;
assign ext_cart_romh    = cart_romh;
assign ext_cart_io1_cen = cart_io1_cen;
assign ext_cart_io2_cen = cart_io2_cen;
assign ext_cart_phi2_p  = vic_phi2_p && !vic_stall;
assign ext_cart_phi2_h  = vic_phi2_h;
assign ext_cart_phi2_n  = vic_phi2_n && !vic_stall;
assign ext_cart_reset   = cpu_reset;
assign ext_cart_vic     = vic_write_ab;
assign ext_cart_vaddr   = rom_char_addr;
assign ext_cart_ba      = vic_ba;

reg         reu_present;

wire        roml_select;
wire        romh_select;

wire        vic_game;
wire        vic_exrom;
wire        vic_loram;
wire        vic_hiram;
wire        vic_charen;

wire        ram_bank;
wire        ram_color_a10;
wire        vdc_cen, vdc_we_a, vdc_rs, vicie_cen, vicie_we_a, vicie_a0;
wire  [7:0] vdc_din, vdc_dout, vicie_din, vicie_dout;
// T140 5.19: db_out of the VDC follows its registers while cs is held (DA moves on as soon as the RAM action of a data-register read is done, ~0.5 us after
// the strobe). The 8502 takes the byte in the strobe's cycle; the Z80 takes it a T-state later and must get the one the strobe saw: held at the strobe.
reg   [7:0] z80_vdc_q = 8'h00;
always @(posedge clk) if (x_bus_en && !vdc_cen) z80_vdc_q <= vdc_dout;
wire  [7:0] vdc_dout_a = z80_act ? z80_vdc_q : vdc_dout;
c64_bus_arbiter_c128 c64_bus_arbiter_0 (
  .mmu_en(vic_phi2_n && !vic_stall), .ram_bank(ram_bank), .ram_vic(mem_cmd_vic), .vic_bank_tag(mem_vic_tag), .c128_n(c128_n), .fsdir(fs_dir), .z80_n(z80_n_mmu),
  .z80io(z80_io_a), .z80_act(z80_act), .fast_lcyc(fast_lcyc), .io_class(io_class), .d4080_n(!x_d4080), .boot64(x_boot64),
  .ram_color_a10(ram_color_a10),
  .vdc_cen(vdc_cen), .vdc_we(vdc_we_a), .vdc_rs(vdc_rs), .vdc_din(vdc_din), .vdc_dout(vdc_dout_a),
  .vicie_cen(vicie_cen), .vicie_we(vicie_we_a), .vicie_a0(vicie_a0), .vicie_din(vicie_din), .vicie_dout(vicie_dout),
  .vic_clk_dot4x  (clk),
  .vic_reset      (vic_reset_req || rst),
  .vic_phi2_n     (vic_phi2_n && !vic_stall),
  .vic_ado        (vic_ado),
  .vic_ras        (vic_ras),
  .vic_cas        (vic_cas),
  .vic_cas_glitch (vic_cas_glitch),
  .vic_write_db   (vic_write_db),
  .vic_write_ab   (vic_write_ab),
  .vic_aec        (vic_aec),
  .vic_ba         (vic_ba),
  .vic_we         (vic_we),
  .vic_adi        (vic_adi),
  .vic_dbi        (vic_dbi),
  .vic_dbo        (vic_dbo),
  .vic_dbh        (vic_dbh),
  .vic_cen        (vic_cen),

  .cpu_reset (cpu_reset),
  .cpu_addr  (addr_a),
  .cpu_din   (cpu_din),
  .cpu_dout  (ext_dma_cyc ? ext_dma_d : dout_a),
  .cpu_pout  (pout_a),
  .cpu_pin   (cpu_pin),
  .cpu_we    (we_a),
  .cpu_pc    (cpu_pc),
  .cpu_irq   (cpu_irq),
  .cpu_nmi   (cpu_nmi),

  .ram_color_addr   (ram_color_addr),
  .ram_color_din    (ram_color_din),
  .ram_color_dout   (ram_color_dout),
  .ram_color_select (ram_color_select),
  .ram_color_we     (ram_color_we),

  .ram_addr   (ram_addr),
  .ram_dout   (ram_dout),
  .ram_din    (ram_din),
  .ram_we     (ram_we),
  .ram_enable (ram_enable),
  .ram_ready  (ram_ready),
  .ram_select (ram_select),

  .rom_basic_addr   (rom_basic_addr),
  .rom_basic_enable (rom_basic_enable),
  .rom_basic_ready  (rom_basic_ready),
  .rom_basic_dout   (rom_basic_dout),
  .rom_basic_select (rom_basic_select),

  .rom_kernal_addr   (rom_kernal_addr),
  .rom_kernal_enable (rom_kernal_enable),
  .rom_kernal_ready  (rom_kernal_ready),
  .rom_kernal_dout   (rom_kernal_dout),
  .rom_kernal_select (rom_kernal_select),

  .rom_char_addr   (rom_char_addr),
  .rom_char_enable (rom_char_enable),
  .rom_char_ready  (rom_char_ready),
  .rom_char_dout   (rom_char_dout),
  .rom_char_select (rom_char_select),

  .sid_addr (sid_addr),
  .sid_dout (sid_dout_bus),
  .sid_din  (sid_din),
  .sid_cen  (sid_cen),
  .sid_we   (sid_we),
  .sid_hit  (sid_hit_bus),

  .cia1_addr (cia1_addr_a),
  .cia1_dout (cia1_dout),
  .cia1_din  (cia1_din_a),
  .cia1_cen  (cia1_cen_a),
  .cia1_we   (cia1_we_a),

  .cia2_addr   (cia2_addr_a),
  .cia2_dout   (cia2_dout),
  .cia2_din    (cia2_din_a),
  .cia2_cen    (cia2_cen_a),
  .cia2_we     (cia2_we_a),
  .cia2_pa_in  (cia2_pa_in),
  .cia2_pa_out (cia2_pa_out),
  .cia2_ddra   (cia2_ddra),
  .cia2_pb_in  (cia2_pb_in),
  .cia2_pb_out (cia2_pb_out),
  .cia2_ddrb   (cia2_ddrb),
  .cia2_flag_n (cia2_flag_n),
  .cia2_pc_n   (cia2_pc_n),

  .reu_addr (reu_addr),
  .reu_din  (reu_din),
  .reu_dout (ext_dma_cyc && ext_dma_w ? ext_dma_d : reu_dout),
  .reu_we   (reu_we),
  .reu_cen  (reu_cen),

  .reu_dma_addr   (ext_dma_cyc ? ext_dma_a : reu_dma_addr),
  .reu_dma_we     (ext_dma_cyc ? ext_dma_w : reu_dma_we),
  .reu_dma_active (reu_dma_active || ext_dma_cyc),

  .reu_present (reu_present),
  .pk_reu      (pk_reu),

  .cart_present      (cart_present || ext_ultimax),
  .cart_game         (cart_game && !ext_ultimax),
  .cart_exrom        (cart_exrom || ext_ultimax),
  .cart_io_present   (cart_present),
  .cart_io_game      (cart_game),
  .cart_io_exrom     (cart_exrom),
  .cart_io1_cen      (cart_io1_cen),
  .cart_io2_cen      (cart_io2_cen),
  .cart_io2_we_valid (cart_io2_we_valid),
  .cart_roml         (cart_roml),
  .cart_romh         (cart_romh),
  .cart_dout         (cart_dout),
  .cart_din          (cart_din),
  .cart_ba           (cart_ba),
  .cart_we           (cart_we),
  .cart_addr         (cart_addr),

  .roml_select (roml_select),
  .romh_select (romh_select),
  .force_sel   (ext_dma_cyc && !ext_dma_w ? ext_dma_f : 2'b00),

  .io_ext_sel  (io_ext_sel),
  .io_ext_we   (ext_io_we),
  .io_dbg_sel  (io_dbg_sel),
  .io_ext_oe   (ext_io_oe),
  .io_ext_dout (ext_io_dout),
  .tr_oe       (tr_oe),
  .tr_dout     (tr_dout),

  .cass_sense_n (tape_cass_sense_n),
  .cass_motor_n (tape_cass_motor_n),

  .game   (vic_game),
  .exrom  (vic_exrom),
  .loram  (vic_loram),
  .hiram  (vic_hiram),
  .charen (vic_charen),

  .sdram_pending (sdram_pending),

  .iec_data_out (iec_data_out_a),
  .iec_clk_out  (iec_clk_out),
  .iec_atn_out  (iec_atn_out),
  .cia2_iec_lines (cia2_iec_lines),
  .iec_data_in  (iec_data_in),
  .iec_clk_in   (iec_clk_in),
  .iec_atn_in   (iec_atn_in),

  .c1541_iec_atn_out  (c1541_iec_atn_out),
  .c1541_iec_data_out (c1541_iec_data_out),
  .c1541_iec_clk_out  (c1541_iec_clk_out),
  .c1541_iec_data_in  (c1541_iec_data_in),
  .c1541_iec_clk_in   (c1541_iec_clk_in),

  .c1541_par_data_in  (c1541_par_data_in),
  .c1541_par_stb_in   (c1541_par_stb_in),
  .c1541_par_data_out (c1541_par_data_out),
  .c1541_par_stb_out  (c1541_par_stb_out),

  .va_delay              (flags[5]),
  .iec_master_disconnect (flags[14])
);

assign sb_cart = {cart_present, cart_exrom, cart_game};

// ---------------------------------------------------------------------------
// the turbo engine (T29)
// ---------------------------------------------------------------------------

c64_turbo_c64u #(.HOLD(TURBO_HOLD)) turbo (
  .clk(clk), .clk2x(clk2x), .rst(rst),
  .gate_ce(gate_ce), .run2(run2), .g_edge(g_edge),
  .phi2_l(vic_phi2_l), .phi2_n(vic_phi2_n && !vic_stall),
  .vic_ba(vic_ba), .cpu_rdy(cpu_rdy), .reu_dma_active(reu_dma_active || ext_dma_cyc),
`ifdef NO_TURBO
  // T79 build switch (TRX_FEATURES without turbo, Alex 2026-10-11): the engine never leaves normal speed
  .cpu_reset(cpu_reset), .inhibit(1'b1),
`else
  .cpu_reset(cpu_reset), .inhibit(turbo_inhibit || cpu_pause_req || cpu_paused),
`endif
  .game(vic_game), .exrom(vic_exrom), .pout3(cpu_pout3_q),
  .vic_d011(sb_vic[17*8 +: 8]), .vic_d012(sb_vic[18*8 +: 8]),
  .vic_regs(sb_vic[383:0]), .cia1_regs(sb_cia1), .cia2_regs(sb_cia2),
  .cpu_addr(cpu_addr), .cpu_we(cpu_we), .cpu_dout(cpu_dout), .cpu_dout_early(dout_early), .cpu_dout_late(dout_late),
  .t_mode(t_mode), .t_ce(t_ce), .t_sel(t_sel), .t_din(t_din), .din_r(din_r_o), .skip_pl(skip_pl),
  .st_ld(st_ld), .st_park(st_park), .st_post(st_post), .pk_ok(pk_ok),
  .ps_v(sid_ps_v), .ps_a(sid_ps_a), .ps_d(sid_ps_d), .sid_fhit(sid_fhit && !sock_ra), .sid_frd(sid_frd), .sid_full(sid_f_full), .sid_empty(sid_f_empty), .sid_quiet(sid_f_quiet),
  .cq_tgl(cq_tgl), .cq_we(cq_we), .cq_a(cq_a), .cq_d(cq_d), .cq_ack(cq_done), .cq_q(color_q_a),
  .addr_a(addr_a), .we_a(we_a), .dout_a(dout_a),
  .nw_sel(!vic_cen && vic_we && !reu_dma_active && !ext_dma_cyc),
  .tr_oe(tr_oe), .tr_dout(tr_dout),
  .tb_addr(tb_addr), .tb_raddr(tb_raddr), .tb_we(tb_we), .tb_wdata(tb_wdata),
  .tb_ram_lo(tb_ram_lo), .tb_ram_hi(tb_ram_hi), .tb_rom_lo(tb_rom_lo), .tb_rom_hi(tb_rom_hi), .tb_took(tb_took),
  .fw_we(fw_we_t), .fw_a(fw_a_t), .fw_d(fw_d_t),
`ifdef NO_TURBO
  .cfg_en(8'd0), .cfg_prefer(tcfg_prefer), .cfg_upd_tgl(1'b0),
`else
  .cfg_en(tcfg_en), .cfg_prefer(tcfg_prefer), .cfg_upd_tgl(tcfg_upd_tgl),
`endif
  .stat(turbo_stat), .st_w1(turbo_w1), .st_w2(turbo_w2), .st_pack(turbo_pack),
`ifdef NO_TURBO
  .ld_en(1'b0), .ld_pack(turbo_ld_pack),
`else
  .ld_en(turbo_ld), .ld_pack(turbo_ld_pack),
`endif
  .ran_clr(turbo_ran_clr),
  .iec_lines(cia2_iec_lines)
);
assign turbo_on = t_mode;

always @(posedge clk) begin
  if (cpu_reset) begin
    cart_present <= flags[11];
    c1541_ext_en <= flags[13];
  end
end

// REU enable (the app's REU_ENABLE, flags[12]): live as on Gideon's core, held while the REU moves data
always @(posedge clk) begin
  if (cpu_reset || !reu_dma_active)
    reu_present <= flags[12];
end

// ---------------------------------------------------------------------------
// c64_ram_color_0
// ---------------------------------------------------------------------------

// c64_ram's behaviour on port A (enable, read-or-write), plus a second
// port on cram_clk for the state bus and the freeze responder (one block RAM)
// Port A is read-first (its output register also takes the old nibble of a
// write, for the write log); ram_color_dout keeps the last read as before.
(* ram_style = "block" *) reg [3:0] color_mem [0:2047];     // T113: two banks of 1 KiB (C128: CPU port bits 0 / 1 pick the CPU's / the VIC's)
reg [3:0] color_q_a;
reg [3:0] color_hold = 4'd0;
reg       color_rd_a = 1'b1;
// T29: the turbo CPU's colour RAM access (c64_turbo_c64u.v, cq_*) uses this port in every clock the bus and the VIC
// leave free (they take it at sdram_pending, twice per PHI2 cycle); one request at a time, a toggle each way. A turbo
// access leaves color_rd_a low, so ram_color_dout stays what the VIC read (color_hold follows it every clock);
// the turbo read takes color_q_a itself.
wire        cq_tgl, cq_we;
wire  [9:0] cq_a;
wire  [3:0] cq_d;
reg         cq_done = 1'b0;
wire        col_bus = ram_color_select && sdram_pending;
wire        col_t   = (cq_tgl != cq_done) && !col_bus;
wire [10:0] col_a   = col_bus ? {ram_color_a10, ram_color_addr} : {1'b0, cq_a};
wire        col_w   = col_bus ? ram_color_we : cq_we;
wire  [3:0] col_d   = col_bus ? ram_color_din : cq_d;
always @(posedge clk)
  if (col_bus || col_t) begin  // not needed but saves power
    if (col_w)
      color_mem[col_a] <= col_d;
    color_q_a  <= color_mem[col_a];
    color_rd_a <= col_bus ? !ram_color_we : 1'b0;
    if (!col_bus) cq_done <= cq_tgl;
  end
always @(posedge clk) color_hold <= ram_color_dout;
assign ram_color_dout = color_rd_a ? color_q_a : color_hold;
assign cram_tap_we    = ram_color_select && sdram_pending && ram_color_we;
assign cram_tap_addr  = ram_color_addr;
assign cram_tap_din   = ram_color_din;
assign cram_tap_old   = color_q_a;

always @(posedge cram_clk) begin
  if (cram_we)
    color_mem[{1'b0, cram_addr}] <= cram_wdata;
  cram_rdata <= color_mem[{1'b0, cram_addr}];
end

// ---------------------------------------------------------------------------
// Reset logic
// ---------------------------------------------------------------------------

reg cpu_reset_i = 1'b1;
reg cpu_reset_pending = 1'b1;

always @(posedge clk) begin
  if (rst)
    cpu_reset_pending <= 1'b1;
  else if (cpu_reset_req)
    cpu_reset_pending <= 1'b1;
  else if (vic_phi2_n)
    cpu_reset_pending <= 1'b0;
end

always @(posedge clk) begin
  if (rst)
    cpu_reset_i <= 1'b1;
  else if (vic_phi2_n)
    cpu_reset_i <= cpu_reset_pending;
end

assign cpu_reset = cpu_reset_i || (vic_cpu_reset && !vic_trst_mask) || rst;

// ---------------------------------------------------------------------------
// IRQ / NMI logic
// ---------------------------------------------------------------------------

assign cpu_irq = cia1_irq | vic_irq | (reu_irq && reu_present) | (cart_irq && cart_present) | ext_io_irq;
assign cpu_nmi = cia2_irq | kbd_restore_toggle | (cart_nmi && cart_present) | ext_restore;

// Board keyboard matrix: a key at (col, row) connects PA[col] and PB[row], so a
// line driven low on one side pulls the other side low through it.
wire [7:0] cia1_pa_low = cia1_ddra & ~cia1_pa_out;
wire [7:0] cia1_pb_low = cia1_ddrb & ~cia1_pb_out;
reg  [7:0] ext_pa_pull, ext_pb_pull, ext_pb_pull_x;
wire [2:0] k_sel;
integer ek_c, ek_r;
always @(*) begin
  ext_pa_pull = 8'h00;
  ext_pb_pull = 8'h00;
  for (ek_c = 0; ek_c < 8; ek_c = ek_c + 1)
    for (ek_r = 0; ek_r < 8; ek_r = ek_r + 1)
      if (ext_keys[ek_c*8 + ek_r]) begin
        if (cia1_pa_low[ek_c]) ext_pb_pull[ek_r] = 1'b1;
        if (cia1_pb_low[ek_r]) ext_pa_pull[ek_c] = 1'b1;
      end
end
// C128: K0-K2 ($D02F bits 0-2, low = selected) are three more matrix columns on the same PB rows
integer xk_c, xk_r;
always @(*) begin
  ext_pb_pull_x = 8'h00;
  for (xk_c = 0; xk_c < 3; xk_c = xk_c + 1)
    for (xk_r = 0; xk_r < 8; xk_r = xk_r + 1)
      if (xkeys[xk_c*8 + xk_r] && !k_sel[xk_c]) ext_pb_pull_x[xk_r] = 1'b1;
end
assign cia1_pa_in = kbd_pa_in & ~ext_pa_pull;
assign cia1_pb_in = kbd_pb_in & ~ext_pb_pull & ~ext_pb_pull_x;

// ---------------------------------------------------------------------------
// Action Replay freeze
// ---------------------------------------------------------------------------

assign cart_freeze = kbd_freeze_pulse;

assign ext_io2_wr  = !cart_io2_cen && cart_we && vic_phi2_h;
assign ext_io_addr = cart_addr[7:0];
assign ext_io_data = cart_din;

// ---------------------------------------------------------------------------
// RAM / ROM to SDRAM mapping
// ---------------------------------------------------------------------------

reg [21:0] mem_addr;
reg        mem_raddr_lsb;
wire [7:0] chr_rq;                  // the character store's read data (c128_chr.v, below)

always @(*) begin
  ram_ready        = 0;
  rom_char_ready   = 0;
  rom_kernal_ready = 0;
  rom_basic_ready  = 0;
  rom_cart_ready     = 0;
  ram_cart_ready     = 0;

  mem_addr = 0;

  mem_cmd_valid = 0;
  mem_cmd_we    = 0;

  mem_wdata     = {ram_din, ram_din};
  mem_wdata_we  = ram_addr[0] ? 2'b10 : 2'b01;

  if (ram_select) begin
    mem_cmd_valid = ram_enable;
    mem_cmd_we    = ram_we;
    mem_addr      = {5'b00000, ram_bank, ram_addr};
    ram_ready     = mem_cmd_ready;

  end else if (rom_char_select) begin
    // T113 5.7: the character ROMs (C64 and C128 set, 4 KiB each) are a block RAM of their own that the application fills (c128_chr.v): the VIC and
    // the CPU read them in the clock after the address like RAM, no command, no wait
    rom_char_ready = 1'b1;

  end else if (rom_kernal_select) begin
    mem_cmd_valid    = rom_kernal_enable;
    mem_addr         = {4'b0000, 1'b1, rom_kernal_addr};
    rom_kernal_ready = mem_cmd_ready;

  end else if (rom_basic_select) begin
    mem_cmd_valid   = rom_basic_enable;
    mem_addr        = 22'd0;                 // C128: BASIC is read through the system ROM select (arbiter)
    rom_basic_ready = mem_cmd_ready;

  end else if (rom_cart_select) begin
    mem_cmd_valid  = rom_cart_enable;
    mem_addr       = {1'b1, rom_cart_addr[20:0]};
    rom_cart_ready = mem_cmd_ready;

  end else if (ram_cart_select) begin
    mem_cmd_valid  = ram_cart_enable;
    mem_cmd_we     = ram_cart_we;
    mem_addr       = {6'b000001, 1'b1, ram_cart_addr};
    ram_cart_ready = mem_cmd_ready;
    mem_wdata_we   = ram_cart_addr[0] ? 2'b10 : 2'b01;
    // GMod2 EEPROM writes are not the CPU's store; everything else keeps
    // ram_din, which leaves the data bus (and the SDRAM read data) out
    if (ram_cart_wdata_own)
      mem_wdata    = {ram_cart_wdata, ram_cart_wdata};
  end

  mem_cmd_addr = {SDRAM_BANK, 1'b0, mem_addr[21:1]};

  ram_dout        = mem_raddr_lsb ? mem_rdata[15:8] : mem_rdata[7:0];
  rom_char_dout   = chr_rq;
  rom_kernal_dout = mem_raddr_lsb ? mem_rdata[15:8] : mem_rdata[7:0];
  rom_basic_dout  = mem_raddr_lsb ? mem_rdata[15:8] : mem_rdata[7:0];
  rom_cart_dout   = mem_raddr_lsb ? mem_rdata[15:8] : mem_rdata[7:0];
  ram_cart_dout   = mem_raddr_lsb ? mem_rdata[15:8] : mem_rdata[7:0];
end

wire mem_rdata_valid_combined;

reg mem_rdata_valid_r = 1'b1;

always @(posedge clk) begin
  if (cpu_reset)
    mem_rdata_valid_r <= 1'b1;
  else if (mem_rdata_valid)
    mem_rdata_valid_r <= 1'b1;
  else if (mem_cmd_valid && mem_cmd_ready && !mem_cmd_we) begin
    mem_rdata_valid_r <= 0;
    mem_raddr_lsb     <= mem_addr[0];
  end
end

assign mem_rdata_valid_combined = mem_rdata_valid || mem_rdata_valid_r;

// T113 5.7: nothing in the machine waits for the memory any more (RAM, the character store and a ROM lookup all answer in the clock after the command;
// a missed ROM line holds only the CPU: rom_hold), so the VIC's phi generator and dots are never stopped. The control of the bench puts the old
// wiring back: the VIC and everything on phi2 stand still while a line is being fetched.
assign vic_stall = 1'b0;

// the character ROM store: read for the VIC (and the CPU) with the address of the arbiter, the set by the machine's mode
wire [12:0] chr_ra = {!c128_n, rom_char_addr};
c128_chr chr (.wclk(chr_wclk), .we(chr_we), .wa(chr_a), .wd(chr_d), .wq(chr_q), .rclk(clk), .ra(chr_ra), .rq(chr_rq));

// ---------------------------------------------------------------------------
// Other
// ---------------------------------------------------------------------------

reg [2:0] led_dim_counter;
reg [4:0] leds_r;

always @(posedge clk) begin
  if (rst) begin
    led_dim_counter <= 0;
    leds_r          <= 5'b0;

  end else if (vic_phi2_p) begin
    led_dim_counter <= led_dim_counter + 1;
    leds_r          <= 5'b0;

    if (led_dim_counter == 0) begin
      leds_r <= {usb_connected[0] || usb_connected[1], tape_act, c1541_led,
        cpu_paused || reu_dma_active, !cpu_reset && !vic_reset_req};
    end
  end
end

always @(posedge clk) begin
  if (rst)
    cpu_paused <= 1'b0;
  else if (vic_phi2_n && !we_a)
    cpu_paused <= cpu_pause_req;
end

assign leds = leds_r;

// ---------------------------------------------------------------------------
// DMA for the Ultimate app: the REU's way onto the bus (address at phi2_p with
// BA high, data at phi2_n), for one byte, while the CPU is paused
// ---------------------------------------------------------------------------

always @(posedge clk) begin
  ext_dma_done <= 1'b0;
  if (ext_dma_req) begin
    ext_dma_pend <= 1'b1;
    ext_dma_a    <= ext_dma_addr;
    ext_dma_f    <= ext_dma_force;
    ext_dma_w    <= ext_dma_we;
    ext_dma_d    <= ext_dma_wdata;
  end
  // G06: only in a CPU read cycle (RDY holds those): a CPU write goes on in its own phi2 and
  // must not lose the bus to the DMA byte; cpu_paused alone is latched a cycle before RDY acts
  if (ext_dma_pend && !ext_dma_cyc && cpu_paused && !cpu_we && !reu_dma_active && vic_ba &&
      vic_phi2_p && !vic_stall)
    ext_dma_cyc <= 1'b1;
  if (ext_dma_cyc && vic_phi2_n && !vic_stall) begin
    ext_dma_cyc   <= 1'b0;
    if (ext_dma_w || !rom_wait_q) begin       // T140 D25: a read that the memory could not answer (ROM or RAM miss) is done again in the next cycle
      ext_dma_pend  <= 1'b0;
      ext_dma_rdata <= cpu_din;
      ext_dma_done  <= 1'b1;
    end
  end
  if (rst) begin
    ext_dma_pend <= 1'b0;
    ext_dma_cyc  <= 1'b0;
  end
end

assign ext_dma_busy = ext_dma_pend && !(ext_dma_cyc && vic_phi2_n && !vic_stall);

// ---------------------------------------------------------------------------
// T113: VIC-IIe registers and the VDC
// ---------------------------------------------------------------------------
// T194 / T140 section 18: which chip is being used (c128_act.v)
c128_act act (
  .clk(clk), .rst(rst),
  .vic_wr((!vic_cen && vic_we && !reu_dma_active && !ext_dma_cyc) || fw_we_t),
  .cram_wr(cram_tap_we),
  .vdc_wr(!vdc_cen && vdc_we_a && vdc_rs),
  .den(sb_vic[17*8+4]),
  .line(vic_pix_y),
  .q(act_q)
);

c128_x x (
  .clk(clk), .rst(rst), .warm(cpu_reset_i), .bus_en(x_bus_en),
  .vdc_cs(!vdc_cen), .vdc_we(vdc_we_a), .vdc_rs(vdc_rs), .vdc_din(vdc_din), .vdc_dout(vdc_dout),
  .vicie_cs(!vicie_cen), .vicie_we(vicie_we_a), .vicie_a0(vicie_a0), .vicie_din(vicie_din), .vicie_dout(vicie_dout),
  .k_sel(k_sel), .fast(c128_fast),
  .vdc_rgbi(vdc_rgbi), .vdc_ce(vdc_ce), .vdc_hb(vdc_hb), .vdc_vb(vdc_vb), .vdc_il(vdc_il),
  .vd_clk(vd_clk), .vd_addr(vd_addr), .vd_we(vd_we), .vd_wdata(vd_wdata), .vd_rdata(vd_rdata),
  .vk_req(vk_req), .vk_ans(vk_ans)
);

endmodule
`default_nettype wire
// vim:ts=2 sw=2 tw=120 et
