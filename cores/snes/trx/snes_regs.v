// T142: the register block the app sees at its core window (cw_*, cpu_clk, the bytes at 0x101A0000; the same window trx_io.v answers for the C64
// core), laid out like nes_regs.v (T95). The whole machine runs on cpu_clk, so nothing crosses a clock here. Written for this project (TREX), no
// upstream code.
//   000-003 ID        "SNES" (a build with ID_TRX2 = 1 answers "TRX2")
//   004 CAPS 0xC0     b6 page 6 (pixel layer), b7 RGB565 pixel layer: the host's, forwarded below
//   005 CAPS2 0x02    b1 BUILD is there
//   008-00B BUILD     the bitstream's build stamp (USR_ACCESS)
//   010 IRQ_EN, 014 IRQ_CAUSE (W1C)   b7 page flip done (px_ev_flip), b6 video frame start (px_ev_vsync), b5 a picture is complete
//   020-027 DDR_CAL   the host's DDR2 calibration, as for the C64 core
//   0A0 CTRL     RW   b0 reset hold, b1 halt
//   0A1 SYSTEM   RW   b0 PAL (the S-CPU/S-PPU frame and the 21.281 MHz master clock), b1 BLEND off (high resolution pseudo-hires kept as 512 wide;
//                     the machine's own "blend" is on after reset), b2 DSP frequency (the S-DSP's 32040 Hz variant); set while CTRL.b0 holds the reset
//   0A2 ROM_TYPE RW   the cartridge's mapper byte as SNES_MiSTer's rom_type (low 2 bits the mapping, bits 7:4 the chip); set while CTRL.b0 holds the reset
//   0A4 STATUS   R    b0 machine in reset, b1 held (Freeze)
//   0A8-0A9 FRAMES    R   S-PPU frame counter (low 16 bits)
//   0AC-0AD ROMADDR   R   the cartridge address bus (a sample, low 16 bits)
//   0B0-0BF MISS_ROM, MISS_WRAM, MISS_BS, STALL    R   line fetches of the three caches, clocks the machine was held (32 bit each, byte 0 first)
//   0C0 FB_LATEST R   b7 valid, b1:0 the buffer of the newest complete picture
//   0C1 FRAME_SEQ R   8 bit counter that changes with every complete picture
//   0C2-0C3 FB_OVERRUNS R   lines that could not be handed to the DDR2 side in time
//   0C4-0C7 FB_BASE   RW  DDR2 byte address of frame buffer 0 (default 0x01900000: CART_BASE + 0x900000)
//   0C8-0CB FB_STRIDE RW  bytes between the four frame buffers (default 0x00080000)
//   0D0-0D3 CART_BASE RW  DDR2 byte address of the cartridge image: ROM at +0, WRAM at +0x800000, battery RAM at +0x820000 (default 0x01000000; bits 3:0 ignored)
//   0D8-0DA ROM_MASK  RW  the image's size minus one as SNES.sv computes it, 24 bits; set while CTRL.b0 holds the reset
//   0DC-0DE RAM_MASK  RW  the battery RAM's size minus one (0 = none), 24 bits
//   0E0-0E8 RTC       RW  the S-RTC's clock in SNES_MiSTer's order: seconds, minutes, hours, day, month, year, weekday (BCD) in bytes 0-6 (0E0-0E6), byte 7 unused,
//                         0E8 b0 the toggle that makes the chip take the time
//   0F0-0F3 WRITES    R   write-throughs queued to the DDR2
//   0F4-0F5 PAD1, 0F6-0F7 PAD2  RW  T156: the USB pad of player 1 / 2 as a 16 bit word, bit n = the controller's serial bit n (0 B, 1 Y, 2 Select, 3 Start, 4 Up, 5 Down,
//                     6 Left, 7 Right, 8 A, 9 X, 10 L, 11 R; 15:12 unused). The application writes the low byte first: that byte waits in a staging register and the
//                     write of the high byte takes the pair, so a word that changes in both bytes is never half old. OR-ed by snes_pad.v with the joystick and key sources.
//   0F8-0F9 PEAK_DSP  R   16 bit: the largest |sample| (0..32768) of the S-DSP's own output pairs since the last write to 0F8; a write (any value) clears this and PEAK_OUT
//   0FA-0FB PEAK_OUT  R   the same for what snes_audio hands to the host's audio path (the cubic result, before x 128): equal to PEAK_DSP (resampling overshoot aside) = the core's sound
//                         reaches the hand-over; PEAK_DSP > 0 with HANDS stuck = the host's frame requests do not arrive; both 0 = the core makes no sound
//   0FC-0FD HANDS     R   16 bit, free running: pairs handed over to the host (one per host audio frame taken, 48 kHz)
//   100-13F IPL       W   the S-SMP's 64 byte boot ROM (the application's spc_ipl table, coreapp/cores/snes.cc), byte n at 100+n; written while CTRL.b0 holds the reset. The write
//                         strobe to the machine stays high for 8 clocks so that a machine tick (every 4th or 5th clock) is in it
//   140       W   DSP-n firmware: any write rewinds the stream. 141 W: the next byte of the 8192 byte firmware (2048 x 3 byte program words, 1024 x 2 byte data words, low byte first,
//                         ares/higan order); a word's last byte strobes it into the chip for 8 clocks. The application writes with 2 us between bytes while CTRL.b0 holds the reset
//   page 6 (0x6000-0x6FFF)   forwarded to the host's pixel layer (px_we, px_re, px_rdata)
//   everything else reads 0 and is acknowledged; the EEPROM window (addr[16]) is not here.
// The controller reads the contract's joystick and keyboard lines itself (snes_pad.v) and, since T156, the PAD1 / PAD2 registers.
`timescale 1ns / 1ps
module snes_regs #(
    parameter integer ID_TRX2 = 0
) (
    input  wire        clk,                         // cpu_clk
    input  wire        we,
    input  wire        re,
    input  wire [16:0] addr,
    input  wire [7:0]  wdata,
    output wire [7:0]  rdata,
    output wire        ack,
    output wire        irq,
    input  wire [31:0] build,
    input  wire [63:0] ddr_cal,
    output wire        px_we,
    output wire        px_re,
    input  wire [7:0]  px_rdata,
    input  wire        ev_vsync,
    input  wire        ev_flip,
    input  wire        ev_frame,
    output wire [7:0]  ctrl,
    output wire [7:0]  sys_cfg,
    output wire [7:0]  rom_type,
    output wire [23:0] rom_mask,
    output wire [23:0] ram_mask,
    output wire [64:0] rtc,
    output wire [31:0] cart_base,
    output wire [15:0] pad1,
    output wire [15:0] pad2,
    output wire        ipl_wr,
    output wire [5:0]  ipl_wa,
    output wire [7:0]  ipl_wd,
    output wire        fw_wr,
    output wire        fw_sel,
    output wire [10:0] fw_a,
    output wire [23:0] fw_d,
    input  wire [31:0] st_miss_rom,
    input  wire [31:0] st_miss_wram,
    input  wire [31:0] st_miss_bs,
    input  wire [31:0] st_stall,
    input  wire [31:0] st_wr,
    output wire [31:0] fb_base,
    output wire [31:0] fb_stride,
    input  wire [7:0]  status,
    input  wire [15:0] frames,
    input  wire [15:0] romaddr,
    input  wire [7:0]  fb_latest,
    input  wire [7:0]  fb_seq,
    input  wire [15:0] fb_over,
    output wire        pk_clr,
    input  wire [15:0] pk_dsp,
    input  wire [15:0] pk_out,
    input  wire [15:0] hands
);
    reg [7:0]  ctrl_r = 8'h00;
    reg [7:0]  sys_r = 8'h00;
    reg [7:0]  type_r = 8'h00;
    reg [23:0] rmask_r = 24'h3FFFFF, bmask_r = 24'h000000;
    reg [63:0] rtc_r = 64'd0;
    reg        rtct_r = 1'b0;
    reg [15:0] pad1_r = 16'd0, pad2_r = 16'd0;
    reg [7:0]  pad1_lo = 8'd0, pad2_lo = 8'd0;
    reg [31:0] cbase_r = 32'h01000000;
    reg [31:0] base_r = 32'h01900000;
    reg [31:0] stride_r = 32'h00080000;
    reg [7:0]  irq_en = 8'd0, cause = 8'd0;
    reg [7:0]  q = 8'd0;
    reg        q_p6 = 1'b0, ack_r = 1'b0;
    wire       win  = addr[16];
    wire [3:0] page = addr[15:12];
    wire [11:0] off = addr[11:0];
    wire [7:0] ev = {ev_flip, ev_vsync, ev_frame, 5'b00000};

    assign px_we = we & ~win & (page == 4'h6);
    assign px_re = re & ~win & (page == 4'h6);
    assign ctrl = ctrl_r;
    assign sys_cfg = sys_r;
    assign rom_type = type_r;
    assign rom_mask = rmask_r;
    assign ram_mask = bmask_r;
    assign rtc = {rtct_r, rtc_r};
    assign cart_base = cbase_r;
    assign pad1 = pad1_r;
    assign pad2 = pad2_r;
    assign fb_base = base_r;
    assign fb_stride = stride_r;
    reg [3:0]  ipl_c = 4'd0;
    reg [5:0]  ipl_a = 6'd0;
    reg [7:0]  ipl_d = 8'd0;
    assign ipl_wr = (ipl_c != 4'd0);
    assign ipl_wa = ipl_a;
    assign ipl_wd = ipl_d;
    // DSP-n firmware stream (T218): 0x140 (write, any value) rewinds, every write to 0x141 is the next byte of the 8192 byte file: 2048 program words of 3 bytes and then
    // 1024 data words of 2 bytes, low byte first. A word is complete with its last byte and goes to the machine as a strobe of 8 clocks (a machine tick is in it).
    reg [3:0]  fw_c = 4'd0;
    reg        fw_s = 1'b0;
    reg [10:0] fw_ar = 11'd0;
    reg [23:0] fw_dr = 24'd0;
    reg [13:0] fw_i = 14'd0;
    reg [1:0]  fw_ph = 2'd0;
    reg [7:0]  fw_b0 = 8'd0, fw_b1 = 8'd0;
    reg [10:0] fw_wa = 11'd0;
    assign fw_wr = (fw_c != 4'd0);
    assign fw_sel = fw_s;
    assign fw_a = fw_ar;
    assign fw_d = fw_dr;
    assign ack = ack_r;
    assign pk_clr = we & ~win & (page == 4'h0) & (off == 12'h0F8);
    assign irq = |(irq_en & cause);
    assign rdata = q_p6 ? px_rdata : q;

    wire [31:0] id = (ID_TRX2 != 0) ? 32'h32585254 : 32'h53454E53;   // bytes "TRX2" / "SNES", byte 0 first ('S' = 0x53)

    always @(posedge clk) begin
        cause <= cause | ev;
        if (ipl_c != 4'd0) ipl_c <= ipl_c - 4'd1;
        if (fw_c != 4'd0) fw_c <= fw_c - 4'd1;
        ack_r <= 1'b0;
        if (re | we) ack_r <= 1'b1;
        q_p6 <= 1'b0;
        if (re && !win && page == 4'h6) q_p6 <= 1'b1;

        if (we && !win && page == 4'h0) begin
            case (off)
                12'h010: irq_en <= wdata;
                12'h014: cause <= (cause & ~wdata) | ev;
                12'h0A0: ctrl_r <= wdata;
                12'h0A1: sys_r <= wdata;
                12'h0A2: type_r <= wdata;
                12'h0D0: cbase_r[7:0] <= wdata;
                12'h0D1: cbase_r[15:8] <= wdata;
                12'h0D2: cbase_r[23:16] <= wdata;
                12'h0D3: cbase_r[31:24] <= wdata;
                12'h0D8: rmask_r[7:0] <= wdata;
                12'h0D9: rmask_r[15:8] <= wdata;
                12'h0DA: rmask_r[23:16] <= wdata;
                12'h0DC: bmask_r[7:0] <= wdata;
                12'h0DD: bmask_r[15:8] <= wdata;
                12'h0DE: bmask_r[23:16] <= wdata;
                12'h0C4: base_r[7:0] <= wdata;
                12'h0C5: base_r[15:8] <= wdata;
                12'h0C6: base_r[23:16] <= wdata;
                12'h0C7: base_r[31:24] <= wdata;
                12'h0C8: stride_r[7:0] <= wdata;
                12'h0C9: stride_r[15:8] <= wdata;
                12'h0CA: stride_r[23:16] <= wdata;
                12'h0CB: stride_r[31:24] <= wdata;
                12'h0E0: rtc_r[7:0] <= wdata;
                12'h0E1: rtc_r[15:8] <= wdata;
                12'h0E2: rtc_r[23:16] <= wdata;
                12'h0E3: rtc_r[31:24] <= wdata;
                12'h0E4: rtc_r[39:32] <= wdata;
                12'h0E5: rtc_r[47:40] <= wdata;
                12'h0E6: rtc_r[55:48] <= wdata;
                12'h0E7: rtc_r[63:56] <= wdata;
                12'h0E8: rtct_r <= wdata[0];
                12'h0F4: pad1_lo <= wdata;
                12'h0F5: pad1_r <= {wdata, pad1_lo};
                12'h0F6: pad2_lo <= wdata;
                12'h0F7: pad2_r <= {wdata, pad2_lo};
                12'h140: begin fw_i <= 14'd0; fw_ph <= 2'd0; fw_wa <= 11'd0; end
                12'h141: if (fw_i < 14'd8192) begin
                    fw_i <= fw_i + 14'd1;
                    if (fw_i < 14'd6144) begin                       // program: 3 bytes
                        if (fw_ph == 2'd0) begin fw_b0 <= wdata; fw_ph <= 2'd1; end
                        else if (fw_ph == 2'd1) begin fw_b1 <= wdata; fw_ph <= 2'd2; end
                        else begin fw_dr <= {wdata, fw_b1, fw_b0}; fw_ar <= fw_wa; fw_s <= 1'b0; fw_c <= 4'd8; fw_wa <= fw_wa + 11'd1; fw_ph <= 2'd0; end
                    end else begin                                   // data: 2 bytes (the word counter restarts: 2048 words of program wrapped the 11 bits)
                        if (fw_ph == 2'd0) begin fw_b0 <= wdata; fw_ph <= 2'd1; end
                        else begin fw_dr <= {8'd0, wdata, fw_b0}; fw_ar <= fw_wa; fw_s <= 1'b1; fw_c <= 4'd8; fw_wa <= fw_wa + 11'd1; fw_ph <= 2'd0; end
                    end
                end
                default: if (off[11:6] == 6'b0001_00) begin ipl_a <= off[5:0]; ipl_d <= wdata; ipl_c <= 4'd8; end
            endcase
        end

        q <= 8'h00;
        if (!win && page == 4'h0) begin
            case (off)
                12'h000: q <= id[7:0];
                12'h001: q <= id[15:8];
                12'h002: q <= id[23:16];
                12'h003: q <= id[31:24];
                12'h004: q <= 8'hC0;
                12'h005: q <= 8'h02;
                12'h008: q <= build[7:0];
                12'h009: q <= build[15:8];
                12'h00A: q <= build[23:16];
                12'h00B: q <= build[31:24];
                12'h010: q <= irq_en;
                12'h014: q <= cause;
                12'h020: q <= ddr_cal[7:0];
                12'h021: q <= ddr_cal[15:8];
                12'h022: q <= ddr_cal[23:16];
                12'h023: q <= ddr_cal[31:24];
                12'h024: q <= ddr_cal[39:32];
                12'h025: q <= ddr_cal[47:40];
                12'h026: q <= ddr_cal[55:48];
                12'h027: q <= ddr_cal[63:56];
                12'h0A0: q <= ctrl_r;
                12'h0A1: q <= sys_r;
                12'h0A2: q <= type_r;
                12'h0A4: q <= status;
                12'h0A8: q <= frames[7:0];
                12'h0A9: q <= frames[15:8];
                12'h0AC: q <= romaddr[7:0];
                12'h0AD: q <= romaddr[15:8];
                12'h0B0: q <= st_miss_rom[7:0];
                12'h0B1: q <= st_miss_rom[15:8];
                12'h0B2: q <= st_miss_rom[23:16];
                12'h0B3: q <= st_miss_rom[31:24];
                12'h0B4: q <= st_miss_wram[7:0];
                12'h0B5: q <= st_miss_wram[15:8];
                12'h0B6: q <= st_miss_wram[23:16];
                12'h0B7: q <= st_miss_wram[31:24];
                12'h0B8: q <= st_miss_bs[7:0];
                12'h0B9: q <= st_miss_bs[15:8];
                12'h0BA: q <= st_miss_bs[23:16];
                12'h0BB: q <= st_miss_bs[31:24];
                12'h0BC: q <= st_stall[7:0];
                12'h0BD: q <= st_stall[15:8];
                12'h0BE: q <= st_stall[23:16];
                12'h0BF: q <= st_stall[31:24];
                12'h0C0: q <= fb_latest;
                12'h0C1: q <= fb_seq;
                12'h0C2: q <= fb_over[7:0];
                12'h0C3: q <= fb_over[15:8];
                12'h0C4: q <= base_r[7:0];
                12'h0C5: q <= base_r[15:8];
                12'h0C6: q <= base_r[23:16];
                12'h0C7: q <= base_r[31:24];
                12'h0C8: q <= stride_r[7:0];
                12'h0C9: q <= stride_r[15:8];
                12'h0CA: q <= stride_r[23:16];
                12'h0CB: q <= stride_r[31:24];
                12'h0D0: q <= cbase_r[7:0];
                12'h0D1: q <= cbase_r[15:8];
                12'h0D2: q <= cbase_r[23:16];
                12'h0D3: q <= cbase_r[31:24];
                12'h0D8: q <= rmask_r[7:0];
                12'h0D9: q <= rmask_r[15:8];
                12'h0DA: q <= rmask_r[23:16];
                12'h0DC: q <= bmask_r[7:0];
                12'h0DD: q <= bmask_r[15:8];
                12'h0DE: q <= bmask_r[23:16];
                12'h0E0: q <= rtc_r[7:0];
                12'h0E1: q <= rtc_r[15:8];
                12'h0E2: q <= rtc_r[23:16];
                12'h0E3: q <= rtc_r[31:24];
                12'h0E4: q <= rtc_r[39:32];
                12'h0E5: q <= rtc_r[47:40];
                12'h0E6: q <= rtc_r[55:48];
                12'h0E7: q <= rtc_r[63:56];
                12'h0E8: q <= {7'd0, rtct_r};
                12'h0F0: q <= st_wr[7:0];
                12'h0F1: q <= st_wr[15:8];
                12'h0F2: q <= st_wr[23:16];
                12'h0F3: q <= st_wr[31:24];
                12'h0F4: q <= pad1_r[7:0];
                12'h0F5: q <= pad1_r[15:8];
                12'h0F6: q <= pad2_r[7:0];
                12'h0F7: q <= pad2_r[15:8];
                12'h0F8: q <= pk_dsp[7:0];
                12'h0F9: q <= pk_dsp[15:8];
                12'h0FA: q <= pk_out[7:0];
                12'h0FB: q <= pk_out[15:8];
                12'h0FC: q <= hands[7:0];
                12'h0FD: q <= hands[15:8];
                default: ;
            endcase
        end
    end
endmodule
