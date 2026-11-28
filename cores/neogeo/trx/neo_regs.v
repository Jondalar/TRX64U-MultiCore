// T143: the register block the app sees at its core window (cw_*, cpu_clk, the bytes at 0x101A0000; the window the other cores answer), laid out like nes_regs.v
// and zxn_regs.v. Written for this project (TREX), no upstream code. The machine runs on its own 48 MHz clock (neo_clk48), so the values the machine reads are
// quasi static (written while the reset is held) and are sampled there; what the app reads back from the machine's domain is a flag or a counter.
//   000-003 ID        "NEO_"          004 CAPS 0xC0 (page 6 = the pixel layer, RGB565)      005 CAPS2 0x02 (BUILD is there)      006 CONTRACT 2
//   008-00B BUILD     the bitstream's build stamp (USR_ACCESS)
//   010 IRQ_EN, 014 IRQ_CAUSE (W1C)   b7 page flip done, b6 video frame start, b5 a Neo Geo frame is complete, b4 the mover finished
//   020-027 DDR_CAL   the host's DDR2 calibration, as for the C64 core
//   0A0 CTRL     RW   b0 reset hold (power-on 1), b1 halt (Freeze)
//   0A1 SYSTEM   RW   b0 1 = MVS (arcade), 0 = AES (console); b1 PAL video; b2 memory card unplugged; set while the reset is held
//   0A2 DIP      RW   b0 settings, b1 freeplay, b2 freeze (the MVS soft DIPs of the MiSTer menu)
//   0A3 PAD      RW   b0 swaps the host's two joystick ports
//   0F0-0F1 PAD1, 0F2-0F3 PAD2  RW  T156: the USB pad of player 1 / 2 as a 16 bit word in the machine's own joystick layout (b0 right, 1 left, 2 down, 3 up, 4 A, 5 B, 6 C, 7 D,
//                     8 Start, 9 Select, 10 Coin; 15:11 unused). The application writes the low byte first: it waits in a staging register and the write of the high byte takes the pair.
//                     OR-ed by neo_pad.v with the joystick and key sources (the port swap of 0A3 does not move it: the first pad plugged is player 1)
//   0A4 STATUS   R    b0 machine in reset, b1 held (Freeze), b2 machine running (out of its power-on counter), b3 backup RAM / memory card changed (see 0A5), b4 mover busy, b5 PLL locked
//   0A5 SAVE_ACK W    any write: clears b3 of STATUS (the application has taken the saves)
//   0A8-0AB CFG       RW  the romset's cfg word (neogeo.sv: b17 ms5p bank, b18 XRAM, b19 ADPCM-A ext, b22:20 P chip, b23 PCM, b25:24 ct0 / link, b27:26 CMC, b28 ROM wait, b30:29 P wait)
//   0B0-0B3 STALL_CLK, 0B4-0B7 C_LATE, 0B8-0BB S_LATE, 0BC-0BF P_READS   R   clocks the machine was held, late sprite words, late fix words, 68k reads asked
//   0C0 FB_LATEST R b7 valid, b1:0 the buffer of the newest complete frame    0C1 FRAME_SEQ R    0C2-0C3 FB_OVERRUNS R    0C4-0C7 FB_BASE RW (default 0x01D00000)   0C8-0CB FB_STRIDE RW (default 0x00100000)
//   0CC-0CD SEEN_COLS R (widest line between blanks), 0CE-0CF SEEN_LINES R (lines with pixels in the last frame)
//   0D0-0D3 ROM_BASE, 0D4-0D7 V1_BASE, 0D8-0DB V2_BASE, 0DC-0DF M1_BASE   RW  DDR2 byte addresses (16 byte aligned) of the machine's ROM spaces (neo_mem.v)
//   0E0-0E3 IMG_BASE RW  DDR2 byte address of the RAM images (LO ROM +0, backup RAM +0x10000, memory card +0x20000, XRAM +0x22000)
//   0E4 MV_CMD   RW   b1:0 image (0 LO ROM, 1 backup RAM = nothing to move, 2 memory card, 3 XRAM), b2 1 = save (machine to DDR2), 0 = load; b3 1 = copy CP_LEN bytes in the DDR2 from CP_SRC to CP_DST, b1:0 then the byte mask: 0 all bytes, 1 bytes 0,1 of every four, 2 bytes 2,3
//                     0E5 MV_GO W (any write starts it)     0E6 MV_DONE R (counts finished moves)
//   140-143 CP_SRC, 144-147 CP_DST, 148-14B CP_LEN   RW   the copy: byte addresses (16 aligned) and a length (multiple of 16) in the DDR2; the application's CPU writes only below 64 MB, the core moves it above
//   0E8-0EE RTC  RW   the app's clock, BCD: seconds, minutes, hours (24 h), weekday 1-7, day, month, year 00-99
//   100-103 P2ROM_MASK, 104-107 CROM_MASK, 108-10B V1ROM_MASK, 10C-10F V2ROM_MASK, 110-113 MROM_MASK, 114 CROM_START   RW   address masks of the loaded ROMs (smeared: 2^n - 1) and the first MB of the C ROM above the P ROMs
//   118 C_DL, 119 S_DL   RW   machine clocks a sprite / fix word may be late before the machine is held (neo_mux.v; reset 24, set by the application's descriptor)
//   150-17B PF   R   11 counters (32 bit each) of the line caches: sprites hit, pend, miss, hint; fix hit, pend, miss, hint; 68k program hit, pend, miss (neo_mem.v)
//   120-123 C_CMDS, 124-127 E_CMDS, 128-12B VA_MISS, 12C-12F MZ_MISS, 130 C_LAT_MAX, 131 C_MAX, 132 S_MAX, 133 PCK_AGE   R   bring-up counters
//   page 6 (0x6000-0x6FFF)   forwarded to the host's pixel layer (px_we, px_re, px_rdata)
//   everything else reads 0 and is acknowledged.
`timescale 1ns / 1ps
module neo_regs (
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
    input  wire        ev_mover,
    output wire [7:0]  ctrl,
    output wire [7:0]  system,
    output wire [2:0]  dip,
    output wire        pad_swap,
    output wire [15:0] pad1,
    output wire [15:0] pad2,
    output wire [31:0] cfg,
    output wire [31:0] fb_base,
    output wire [31:0] fb_stride,
    output wire [31:0] rom_base,
    output wire [31:0] v1_base,
    output wire [31:0] v2_base,
    output wire [31:0] m1_base,
    output wire [31:0] img_base,
    output wire [3:0]  mv_cmd,
    output wire [31:0] cp_src,
    output wire [31:0] cp_dst,
    output wire [31:0] cp_len,
    output wire        mv_go_tgl,
    input  wire [7:0]  mv_done,
    output wire [55:0] rtc,
    output wire [26:0] p2_mask,
    output wire [26:0] c_mask,
    output wire [26:0] v1_mask,
    output wire [26:0] v2_mask,
    output wire [26:0] m_mask,
    output wire [6:0]  c_start,
    output wire [7:0]  c_dl,
    output wire [7:0]  s_dl,
    output wire        save_ack_tgl,
    input  wire [7:0]  status,
    input  wire [7:0]  fb_latest,
    input  wire [7:0]  fb_seq,
    input  wire [15:0] fb_over,
    input  wire [15:0] seen_cols,
    input  wire [15:0] seen_lines,
    input  wire [31:0] st_stall_clk,
    input  wire [31:0] st_c_late,
    input  wire [31:0] st_s_late,
    input  wire [31:0] st_p_reads,
    input  wire [31:0] st_c_cmds,
    input  wire [31:0] st_e_cmds,
    input  wire [31:0] st_va_miss,
    input  wire [31:0] st_mz_miss,
    input  wire [7:0]  st_c_lat_max,
    input  wire [7:0]  st_c_max,
    input  wire [7:0]  st_s_max,
    input  wire [7:0]  pck_age,
    input  wire [351:0] st_pf                       // 11 counters of neo_mem.v: C hit, pend, miss, hint; S hit, pend, miss, hint; P hit, pend, miss
);
    // >>> generated from cores/neogeo/app/gateware.toml (T264 S2): do not edit, `tools/toml_a_gen.py gen` rewrites it
    localparam [11:0] R_IDENT_ID = 12'h000;
    localparam [11:0] R_IDENT_CAPS = 12'h004;
    localparam [11:0] R_IDENT_CAPS2 = 12'h005;
    localparam [11:0] R_IDENT_CONTRACT = 12'h006;
    localparam [11:0] R_IDENT_BUILD = 12'h008;
    localparam [11:0] R_NEO_IRQ_IRQ_EN = 12'h010;
    localparam [11:0] R_NEO_IRQ_IRQ_CAUSE = 12'h014;
    localparam [11:0] R_NEO_DDR_CAL_DDR_CAL0 = 12'h020;
    localparam [11:0] R_NEO_DDR_CAL_DDR_CAL1 = 12'h024;
    localparam [11:0] R_NEO_MACHINE_CTRL = 12'h0A0;
    localparam [11:0] R_NEO_MACHINE_SYSTEM = 12'h0A1;
    localparam [11:0] R_NEO_MACHINE_DIP = 12'h0A2;
    localparam [11:0] R_NEO_MACHINE_JOY = 12'h0A3;
    localparam [11:0] R_NEO_MACHINE_STATUS = 12'h0A4;
    localparam [11:0] R_NEO_MACHINE_SAVE_ACK = 12'h0A5;
    localparam [11:0] R_NEO_MACHINE_CFG = 12'h0A8;
    localparam [11:0] R_NEO_STATS_STALL_CLK = 12'h0B0;
    localparam [11:0] R_NEO_STATS_C_LATE = 12'h0B4;
    localparam [11:0] R_NEO_STATS_S_LATE = 12'h0B8;
    localparam [11:0] R_NEO_STATS_P_READS = 12'h0BC;
    localparam [11:0] R_NEO_VIDEO_FB_LATEST = 12'h0C0;
    localparam [11:0] R_NEO_VIDEO_FRAME_SEQ = 12'h0C1;
    localparam [11:0] R_NEO_VIDEO_FB_OVERRUNS = 12'h0C2;
    localparam [11:0] R_NEO_VIDEO_FB_BASE = 12'h0C4;
    localparam [11:0] R_NEO_VIDEO_FB_STRIDE = 12'h0C8;
    localparam [11:0] R_NEO_VIDEO_SEEN_COLS = 12'h0CC;
    localparam [11:0] R_NEO_VIDEO_SEEN_LINES = 12'h0CE;
    localparam [11:0] R_NEO_MEM_ROM_BASE = 12'h0D0;
    localparam [11:0] R_NEO_MEM_V1_BASE = 12'h0D4;
    localparam [11:0] R_NEO_MEM_V2_BASE = 12'h0D8;
    localparam [11:0] R_NEO_MEM_M1_BASE = 12'h0DC;
    localparam [11:0] R_NEO_MEM_IMG_BASE = 12'h0E0;
    localparam [11:0] R_NEO_MOVER_MV_CMD = 12'h0E4;
    localparam [11:0] R_NEO_MOVER_MV_GO = 12'h0E5;
    localparam [11:0] R_NEO_MOVER_MV_DONE = 12'h0E6;
    localparam [11:0] R_NEO_RTC_RTC = 12'h0E8;
    localparam [11:0] R_NEO_PAD_PAD1 = 12'h0F0;
    localparam [11:0] R_NEO_PAD_PAD2 = 12'h0F2;
    localparam [11:0] R_NEO_MASKS_P2_MASK = 12'h100;
    localparam [11:0] R_NEO_MASKS_C_MASK = 12'h104;
    localparam [11:0] R_NEO_MASKS_V1_MASK = 12'h108;
    localparam [11:0] R_NEO_MASKS_V2_MASK = 12'h10C;
    localparam [11:0] R_NEO_MASKS_M_MASK = 12'h110;
    localparam [11:0] R_NEO_MASKS_C_START = 12'h114;
    localparam [11:0] R_NEO_MASKS_C_DL = 12'h118;
    localparam [11:0] R_NEO_MASKS_S_DL = 12'h119;
    localparam [11:0] R_NEO_PERF_C_CMDS = 12'h120;
    localparam [11:0] R_NEO_PERF_E_CMDS = 12'h124;
    localparam [11:0] R_NEO_PERF_VA_MISS = 12'h128;
    localparam [11:0] R_NEO_PERF_MZ_MISS = 12'h12C;
    localparam [11:0] R_NEO_PERF_C_LAT_MAX = 12'h130;
    localparam [11:0] R_NEO_PERF_C_MAX = 12'h131;
    localparam [11:0] R_NEO_PERF_S_MAX = 12'h132;
    localparam [11:0] R_NEO_PERF_PCK_AGE = 12'h133;
    localparam [11:0] R_NEO_COPY_CP_SRC = 12'h140;
    localparam [11:0] R_NEO_COPY_CP_DST = 12'h144;
    localparam [11:0] R_NEO_COPY_CP_LEN = 12'h148;
    localparam [11:0] R_NEO_PF_PF = 12'h150;
    // <<< generated
    reg [7:0]  ctrl_r = 8'h01;
    reg [7:0]  sys_r = 8'h00;
    reg [2:0]  dip_r = 3'd0;
    reg        pad_r = 1'b0;
    reg [15:0] pad1_r = 16'd0, pad2_r = 16'd0;
    reg [7:0]  pad1_lo = 8'd0, pad2_lo = 8'd0;
    reg [31:0] cfg_r = 32'd0;
    reg [31:0] base_r = 32'h01D00000;
    reg [31:0] stride_r = 32'h00100000;
    reg [31:0] rom_r = 32'h01000000, v1_r = 32'h03000000, v2_r = 32'h04000000, m1_r = 32'h05000000, img_r = 32'h05100000;
    reg [3:0]  mvc_r = 4'd0;
    reg [31:0] cps_r = 32'd0, cpd_r = 32'd0, cpl_r = 32'd0;
    reg        go_r = 1'b0, sack_r = 1'b0;
    reg [55:0] rtc_r = 56'd0;
    reg [26:0] p2m_r = 27'd0, cm_r = 27'd0, v1m_r = 27'd0, v2m_r = 27'd0, mm_r = 27'd0;
    reg [6:0]  cst_r = 7'd3;
    reg [7:0]  cdl_r = 8'd8, sdl_r = 8'd8;
    reg [7:0]  irq_en = 8'd0, cause = 8'd0;
    reg [7:0]  q = 8'd0;
    reg        q_p6 = 1'b0, ack_r = 1'b0;
    wire       win  = addr[16];
    wire [3:0] page = addr[15:12];
    wire [11:0] off = addr[11:0];
    wire [7:0] ev = {ev_flip, ev_vsync, ev_frame, ev_mover, 4'b0000};

    assign px_we = we & ~win & (page == 4'h6);
    assign px_re = re & ~win & (page == 4'h6);
    assign ctrl = ctrl_r;
    assign system = sys_r;
    assign dip = dip_r;
    assign pad_swap = pad_r;
    assign pad1 = pad1_r;
    assign pad2 = pad2_r;
    assign cfg = cfg_r;
    assign fb_base = base_r;
    assign fb_stride = stride_r;
    assign rom_base = rom_r;
    assign v1_base = v1_r;
    assign v2_base = v2_r;
    assign m1_base = m1_r;
    assign img_base = img_r;
    assign mv_cmd = mvc_r;
    assign cp_src = cps_r;
    assign cp_dst = cpd_r;
    assign cp_len = cpl_r;
    assign mv_go_tgl = go_r;
    assign rtc = rtc_r;
    assign p2_mask = p2m_r;
    assign c_mask = cm_r;
    assign v1_mask = v1m_r;
    assign v2_mask = v2m_r;
    assign m_mask = mm_r;
    assign c_start = cst_r;
    assign c_dl = cdl_r;
    assign s_dl = sdl_r;
    assign save_ack_tgl = sack_r;
    assign ack = ack_r;
    assign irq = |(irq_en & cause);
    assign rdata = q_p6 ? px_rdata : q;

    wire [31:0] id = 32'h5F4F454E;   // bytes "NEO_", byte 0 first

    function [7:0] b32(input [31:0] v, input [1:0] i);
        b32 = v[8 * i +: 8];
    endfunction

    always @(posedge clk) begin
        cause <= cause | ev;
        ack_r <= 1'b0;
        if (re | we) ack_r <= 1'b1;
        q_p6 <= 1'b0;
        if (re && !win && page == 4'h6) q_p6 <= 1'b1;

        if (we && !win && page == 4'h0) begin
            case (off)
                R_NEO_IRQ_IRQ_EN: irq_en <= wdata;
                R_NEO_IRQ_IRQ_CAUSE: cause <= (cause & ~wdata) | ev;
                R_NEO_MACHINE_CTRL: ctrl_r <= wdata;
                R_NEO_MACHINE_SYSTEM: sys_r <= wdata;
                R_NEO_MACHINE_DIP: dip_r <= wdata[2:0];
                R_NEO_MACHINE_JOY: pad_r <= wdata[0];
                R_NEO_PAD_PAD1: pad1_lo <= wdata;
                R_NEO_PAD_PAD1 + 12'd1: pad1_r <= {wdata, pad1_lo};
                R_NEO_PAD_PAD2: pad2_lo <= wdata;
                R_NEO_PAD_PAD2 + 12'd1: pad2_r <= {wdata, pad2_lo};
                R_NEO_MACHINE_SAVE_ACK: sack_r <= ~sack_r;
                R_NEO_MACHINE_CFG: cfg_r[7:0] <= wdata;
                R_NEO_MACHINE_CFG + 12'd1: cfg_r[15:8] <= wdata;
                R_NEO_MACHINE_CFG + 12'd2: cfg_r[23:16] <= wdata;
                R_NEO_MACHINE_CFG + 12'd3: cfg_r[31:24] <= wdata;
                R_NEO_VIDEO_FB_BASE: base_r[7:0] <= wdata;
                R_NEO_VIDEO_FB_BASE + 12'd1: base_r[15:8] <= wdata;
                R_NEO_VIDEO_FB_BASE + 12'd2: base_r[23:16] <= wdata;
                R_NEO_VIDEO_FB_BASE + 12'd3: base_r[31:24] <= wdata;
                R_NEO_VIDEO_FB_STRIDE: stride_r[7:0] <= wdata;
                R_NEO_VIDEO_FB_STRIDE + 12'd1: stride_r[15:8] <= wdata;
                R_NEO_VIDEO_FB_STRIDE + 12'd2: stride_r[23:16] <= wdata;
                R_NEO_VIDEO_FB_STRIDE + 12'd3: stride_r[31:24] <= wdata;
                R_NEO_MEM_ROM_BASE: rom_r[7:0] <= wdata;   R_NEO_MEM_ROM_BASE + 12'd1: rom_r[15:8] <= wdata;   R_NEO_MEM_ROM_BASE + 12'd2: rom_r[23:16] <= wdata;   R_NEO_MEM_ROM_BASE + 12'd3: rom_r[31:24] <= wdata;
                R_NEO_MEM_V1_BASE: v1_r[7:0] <= wdata;    R_NEO_MEM_V1_BASE + 12'd1: v1_r[15:8] <= wdata;    R_NEO_MEM_V1_BASE + 12'd2: v1_r[23:16] <= wdata;    R_NEO_MEM_V1_BASE + 12'd3: v1_r[31:24] <= wdata;
                R_NEO_MEM_V2_BASE: v2_r[7:0] <= wdata;    R_NEO_MEM_V2_BASE + 12'd1: v2_r[15:8] <= wdata;    R_NEO_MEM_V2_BASE + 12'd2: v2_r[23:16] <= wdata;    R_NEO_MEM_V2_BASE + 12'd3: v2_r[31:24] <= wdata;
                R_NEO_MEM_M1_BASE: m1_r[7:0] <= wdata;    R_NEO_MEM_M1_BASE + 12'd1: m1_r[15:8] <= wdata;    R_NEO_MEM_M1_BASE + 12'd2: m1_r[23:16] <= wdata;    R_NEO_MEM_M1_BASE + 12'd3: m1_r[31:24] <= wdata;
                R_NEO_MEM_IMG_BASE: img_r[7:0] <= wdata;   R_NEO_MEM_IMG_BASE + 12'd1: img_r[15:8] <= wdata;   R_NEO_MEM_IMG_BASE + 12'd2: img_r[23:16] <= wdata;   R_NEO_MEM_IMG_BASE + 12'd3: img_r[31:24] <= wdata;
                R_NEO_MOVER_MV_CMD: mvc_r <= wdata[3:0];
                R_NEO_COPY_CP_SRC: cps_r[7:0] <= wdata;  R_NEO_COPY_CP_SRC + 12'd1: cps_r[15:8] <= wdata;  R_NEO_COPY_CP_SRC + 12'd2: cps_r[23:16] <= wdata;  R_NEO_COPY_CP_SRC + 12'd3: cps_r[31:24] <= wdata;
                R_NEO_COPY_CP_DST: cpd_r[7:0] <= wdata;  R_NEO_COPY_CP_DST + 12'd1: cpd_r[15:8] <= wdata;  R_NEO_COPY_CP_DST + 12'd2: cpd_r[23:16] <= wdata;  R_NEO_COPY_CP_DST + 12'd3: cpd_r[31:24] <= wdata;
                R_NEO_COPY_CP_LEN: cpl_r[7:0] <= wdata;  R_NEO_COPY_CP_LEN + 12'd1: cpl_r[15:8] <= wdata;  R_NEO_COPY_CP_LEN + 12'd2: cpl_r[23:16] <= wdata;  R_NEO_COPY_CP_LEN + 12'd3: cpl_r[31:24] <= wdata;
                R_NEO_MOVER_MV_GO: go_r <= ~go_r;
                R_NEO_MASKS_P2_MASK: p2m_r[7:0] <= wdata;   R_NEO_MASKS_P2_MASK + 12'd1: p2m_r[15:8] <= wdata;   R_NEO_MASKS_P2_MASK + 12'd2: p2m_r[23:16] <= wdata;   R_NEO_MASKS_P2_MASK + 12'd3: p2m_r[26:24] <= wdata[2:0];
                R_NEO_MASKS_C_MASK: cm_r[7:0] <= wdata;    R_NEO_MASKS_C_MASK + 12'd1: cm_r[15:8] <= wdata;    R_NEO_MASKS_C_MASK + 12'd2: cm_r[23:16] <= wdata;    R_NEO_MASKS_C_MASK + 12'd3: cm_r[26:24] <= wdata[2:0];
                R_NEO_MASKS_V1_MASK: v1m_r[7:0] <= wdata;   R_NEO_MASKS_V1_MASK + 12'd1: v1m_r[15:8] <= wdata;   R_NEO_MASKS_V1_MASK + 12'd2: v1m_r[23:16] <= wdata;   R_NEO_MASKS_V1_MASK + 12'd3: v1m_r[26:24] <= wdata[2:0];
                R_NEO_MASKS_V2_MASK: v2m_r[7:0] <= wdata;   R_NEO_MASKS_V2_MASK + 12'd1: v2m_r[15:8] <= wdata;   R_NEO_MASKS_V2_MASK + 12'd2: v2m_r[23:16] <= wdata;   R_NEO_MASKS_V2_MASK + 12'd3: v2m_r[26:24] <= wdata[2:0];
                R_NEO_MASKS_M_MASK: mm_r[7:0] <= wdata;    R_NEO_MASKS_M_MASK + 12'd1: mm_r[15:8] <= wdata;    R_NEO_MASKS_M_MASK + 12'd2: mm_r[23:16] <= wdata;    R_NEO_MASKS_M_MASK + 12'd3: mm_r[26:24] <= wdata[2:0];
                R_NEO_MASKS_C_START: cst_r <= wdata[6:0];
                R_NEO_MASKS_C_DL: cdl_r <= wdata;
                R_NEO_MASKS_S_DL: sdl_r <= wdata;
                default: if (off >= R_NEO_RTC_RTC && off < R_NEO_RTC_RTC + 12'd7) rtc_r[8 * (off - R_NEO_RTC_RTC) +: 8] <= wdata;
            endcase
        end

        q <= 8'h00;
        if (!win && page == 4'h0) begin
            case (off)
                R_IDENT_ID: q <= id[7:0];
                R_IDENT_ID + 12'd1: q <= id[15:8];
                R_IDENT_ID + 12'd2: q <= id[23:16];
                R_IDENT_ID + 12'd3: q <= id[31:24];
                R_IDENT_CAPS: q <= 8'hC0;
                R_IDENT_CAPS2: q <= 8'h02;
                R_IDENT_CONTRACT: q <= 8'h02;
                R_IDENT_BUILD: q <= build[7:0];
                R_IDENT_BUILD + 12'd1: q <= build[15:8];
                R_IDENT_BUILD + 12'd2: q <= build[23:16];
                R_IDENT_BUILD + 12'd3: q <= build[31:24];
                R_NEO_IRQ_IRQ_EN: q <= irq_en;
                R_NEO_IRQ_IRQ_CAUSE: q <= cause;
                R_NEO_DDR_CAL_DDR_CAL0: q <= ddr_cal[7:0];
                R_NEO_DDR_CAL_DDR_CAL0 + 12'd1: q <= ddr_cal[15:8];
                R_NEO_DDR_CAL_DDR_CAL0 + 12'd2: q <= ddr_cal[23:16];
                R_NEO_DDR_CAL_DDR_CAL0 + 12'd3: q <= ddr_cal[31:24];
                R_NEO_DDR_CAL_DDR_CAL1: q <= ddr_cal[39:32];
                R_NEO_DDR_CAL_DDR_CAL1 + 12'd1: q <= ddr_cal[47:40];
                R_NEO_DDR_CAL_DDR_CAL1 + 12'd2: q <= ddr_cal[55:48];
                R_NEO_DDR_CAL_DDR_CAL1 + 12'd3: q <= ddr_cal[63:56];
                R_NEO_MACHINE_CTRL: q <= ctrl_r;
                R_NEO_MACHINE_SYSTEM: q <= sys_r;
                R_NEO_MACHINE_DIP: q <= {5'd0, dip_r};
                R_NEO_MACHINE_JOY: q <= {7'd0, pad_r};
                R_NEO_PAD_PAD1: q <= pad1_r[7:0];
                R_NEO_PAD_PAD1 + 12'd1: q <= pad1_r[15:8];
                R_NEO_PAD_PAD2: q <= pad2_r[7:0];
                R_NEO_PAD_PAD2 + 12'd1: q <= pad2_r[15:8];
                R_NEO_MACHINE_STATUS: q <= status;
                R_NEO_MACHINE_CFG: q <= cfg_r[7:0];   R_NEO_MACHINE_CFG + 12'd1: q <= cfg_r[15:8];   R_NEO_MACHINE_CFG + 12'd2: q <= cfg_r[23:16];   R_NEO_MACHINE_CFG + 12'd3: q <= cfg_r[31:24];
                R_NEO_STATS_STALL_CLK: q <= st_stall_clk[7:0];   R_NEO_STATS_STALL_CLK + 12'd1: q <= st_stall_clk[15:8];   R_NEO_STATS_STALL_CLK + 12'd2: q <= st_stall_clk[23:16];   R_NEO_STATS_STALL_CLK + 12'd3: q <= st_stall_clk[31:24];
                R_NEO_STATS_C_LATE: q <= st_c_late[7:0];      R_NEO_STATS_C_LATE + 12'd1: q <= st_c_late[15:8];      R_NEO_STATS_C_LATE + 12'd2: q <= st_c_late[23:16];      R_NEO_STATS_C_LATE + 12'd3: q <= st_c_late[31:24];
                R_NEO_STATS_S_LATE: q <= st_s_late[7:0];      R_NEO_STATS_S_LATE + 12'd1: q <= st_s_late[15:8];      R_NEO_STATS_S_LATE + 12'd2: q <= st_s_late[23:16];      R_NEO_STATS_S_LATE + 12'd3: q <= st_s_late[31:24];
                R_NEO_STATS_P_READS: q <= st_p_reads[7:0];     R_NEO_STATS_P_READS + 12'd1: q <= st_p_reads[15:8];     R_NEO_STATS_P_READS + 12'd2: q <= st_p_reads[23:16];     R_NEO_STATS_P_READS + 12'd3: q <= st_p_reads[31:24];
                R_NEO_VIDEO_FB_LATEST: q <= fb_latest;
                R_NEO_VIDEO_FRAME_SEQ: q <= fb_seq;
                R_NEO_VIDEO_FB_OVERRUNS: q <= fb_over[7:0];
                R_NEO_VIDEO_FB_OVERRUNS + 12'd1: q <= fb_over[15:8];
                R_NEO_VIDEO_FB_BASE: q <= base_r[7:0];   R_NEO_VIDEO_FB_BASE + 12'd1: q <= base_r[15:8];   R_NEO_VIDEO_FB_BASE + 12'd2: q <= base_r[23:16];   R_NEO_VIDEO_FB_BASE + 12'd3: q <= base_r[31:24];
                R_NEO_VIDEO_FB_STRIDE: q <= stride_r[7:0]; R_NEO_VIDEO_FB_STRIDE + 12'd1: q <= stride_r[15:8]; R_NEO_VIDEO_FB_STRIDE + 12'd2: q <= stride_r[23:16]; R_NEO_VIDEO_FB_STRIDE + 12'd3: q <= stride_r[31:24];
                R_NEO_VIDEO_SEEN_COLS: q <= seen_cols[7:0];  R_NEO_VIDEO_SEEN_COLS + 12'd1: q <= seen_cols[15:8];  R_NEO_VIDEO_SEEN_LINES: q <= seen_lines[7:0];  R_NEO_VIDEO_SEEN_LINES + 12'd1: q <= seen_lines[15:8];
                R_NEO_MEM_ROM_BASE: q <= rom_r[7:0];   R_NEO_MEM_ROM_BASE + 12'd1: q <= rom_r[15:8];   R_NEO_MEM_ROM_BASE + 12'd2: q <= rom_r[23:16];   R_NEO_MEM_ROM_BASE + 12'd3: q <= rom_r[31:24];
                R_NEO_MEM_V1_BASE: q <= v1_r[7:0];    R_NEO_MEM_V1_BASE + 12'd1: q <= v1_r[15:8];    R_NEO_MEM_V1_BASE + 12'd2: q <= v1_r[23:16];    R_NEO_MEM_V1_BASE + 12'd3: q <= v1_r[31:24];
                R_NEO_MEM_V2_BASE: q <= v2_r[7:0];    R_NEO_MEM_V2_BASE + 12'd1: q <= v2_r[15:8];    R_NEO_MEM_V2_BASE + 12'd2: q <= v2_r[23:16];    R_NEO_MEM_V2_BASE + 12'd3: q <= v2_r[31:24];
                R_NEO_MEM_M1_BASE: q <= m1_r[7:0];    R_NEO_MEM_M1_BASE + 12'd1: q <= m1_r[15:8];    R_NEO_MEM_M1_BASE + 12'd2: q <= m1_r[23:16];    R_NEO_MEM_M1_BASE + 12'd3: q <= m1_r[31:24];
                R_NEO_MEM_IMG_BASE: q <= img_r[7:0];   R_NEO_MEM_IMG_BASE + 12'd1: q <= img_r[15:8];   R_NEO_MEM_IMG_BASE + 12'd2: q <= img_r[23:16];   R_NEO_MEM_IMG_BASE + 12'd3: q <= img_r[31:24];
                R_NEO_MOVER_MV_CMD: q <= {4'd0, mvc_r};
                R_NEO_COPY_CP_SRC: q <= cps_r[7:0];  R_NEO_COPY_CP_SRC + 12'd1: q <= cps_r[15:8];  R_NEO_COPY_CP_SRC + 12'd2: q <= cps_r[23:16];  R_NEO_COPY_CP_SRC + 12'd3: q <= cps_r[31:24];
                R_NEO_COPY_CP_DST: q <= cpd_r[7:0];  R_NEO_COPY_CP_DST + 12'd1: q <= cpd_r[15:8];  R_NEO_COPY_CP_DST + 12'd2: q <= cpd_r[23:16];  R_NEO_COPY_CP_DST + 12'd3: q <= cpd_r[31:24];
                R_NEO_COPY_CP_LEN: q <= cpl_r[7:0];  R_NEO_COPY_CP_LEN + 12'd1: q <= cpl_r[15:8];  R_NEO_COPY_CP_LEN + 12'd2: q <= cpl_r[23:16];  R_NEO_COPY_CP_LEN + 12'd3: q <= cpl_r[31:24];
                R_NEO_MOVER_MV_DONE: q <= mv_done;
                R_NEO_MASKS_P2_MASK: q <= p2m_r[7:0];   R_NEO_MASKS_P2_MASK + 12'd1: q <= p2m_r[15:8];   R_NEO_MASKS_P2_MASK + 12'd2: q <= p2m_r[23:16];   R_NEO_MASKS_P2_MASK + 12'd3: q <= {5'd0, p2m_r[26:24]};
                R_NEO_MASKS_C_MASK: q <= cm_r[7:0];    R_NEO_MASKS_C_MASK + 12'd1: q <= cm_r[15:8];    R_NEO_MASKS_C_MASK + 12'd2: q <= cm_r[23:16];    R_NEO_MASKS_C_MASK + 12'd3: q <= {5'd0, cm_r[26:24]};
                R_NEO_MASKS_V1_MASK: q <= v1m_r[7:0];   R_NEO_MASKS_V1_MASK + 12'd1: q <= v1m_r[15:8];   R_NEO_MASKS_V1_MASK + 12'd2: q <= v1m_r[23:16];   R_NEO_MASKS_V1_MASK + 12'd3: q <= {5'd0, v1m_r[26:24]};
                R_NEO_MASKS_V2_MASK: q <= v2m_r[7:0];   R_NEO_MASKS_V2_MASK + 12'd1: q <= v2m_r[15:8];   R_NEO_MASKS_V2_MASK + 12'd2: q <= v2m_r[23:16];   R_NEO_MASKS_V2_MASK + 12'd3: q <= {5'd0, v2m_r[26:24]};
                R_NEO_MASKS_M_MASK: q <= mm_r[7:0];    R_NEO_MASKS_M_MASK + 12'd1: q <= mm_r[15:8];    R_NEO_MASKS_M_MASK + 12'd2: q <= mm_r[23:16];    R_NEO_MASKS_M_MASK + 12'd3: q <= {5'd0, mm_r[26:24]};
                R_NEO_MASKS_C_START: q <= {1'd0, cst_r};
                R_NEO_MASKS_C_DL: q <= cdl_r;
                R_NEO_MASKS_S_DL: q <= sdl_r;
                R_NEO_PERF_C_CMDS: q <= st_c_cmds[7:0];  R_NEO_PERF_C_CMDS + 12'd1: q <= st_c_cmds[15:8];  R_NEO_PERF_C_CMDS + 12'd2: q <= st_c_cmds[23:16];  R_NEO_PERF_C_CMDS + 12'd3: q <= st_c_cmds[31:24];
                R_NEO_PERF_E_CMDS: q <= st_e_cmds[7:0];  R_NEO_PERF_E_CMDS + 12'd1: q <= st_e_cmds[15:8];  R_NEO_PERF_E_CMDS + 12'd2: q <= st_e_cmds[23:16];  R_NEO_PERF_E_CMDS + 12'd3: q <= st_e_cmds[31:24];
                R_NEO_PERF_VA_MISS: q <= st_va_miss[7:0]; R_NEO_PERF_VA_MISS + 12'd1: q <= st_va_miss[15:8]; R_NEO_PERF_VA_MISS + 12'd2: q <= st_va_miss[23:16]; R_NEO_PERF_VA_MISS + 12'd3: q <= st_va_miss[31:24];
                R_NEO_PERF_MZ_MISS: q <= st_mz_miss[7:0]; R_NEO_PERF_MZ_MISS + 12'd1: q <= st_mz_miss[15:8]; R_NEO_PERF_MZ_MISS + 12'd2: q <= st_mz_miss[23:16]; R_NEO_PERF_MZ_MISS + 12'd3: q <= st_mz_miss[31:24];
                R_NEO_PERF_C_LAT_MAX: q <= st_c_lat_max;
                R_NEO_PERF_C_MAX: q <= st_c_max;
                R_NEO_PERF_S_MAX: q <= st_s_max;
                R_NEO_PERF_PCK_AGE: q <= pck_age;
                default: if (off >= R_NEO_RTC_RTC && off < R_NEO_RTC_RTC + 12'd7) q <= rtc_r[8 * (off - R_NEO_RTC_RTC) +: 8];
                         else if (off >= R_NEO_PF_PF && off < R_NEO_PF_PF + 12'd44) q <= st_pf[8 * (off - R_NEO_PF_PF) +: 8];
            endcase
        end
    end
endmodule
