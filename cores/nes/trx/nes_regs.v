// T95: the register block the app sees at its core window (cw_*, cpu_clk, the bytes at 0x101A0000; the same window trx_io.v
// answers for the C64 core), laid out like c264_regs.v / vc20_regs.vhd. The whole machine runs on cpu_clk, so nothing crosses a
// clock here. Written for this project (TREX), no upstream code.
//   000-003 ID        "NES_" (a build with ID_TRX2 = 1 answers "TRX2")
//   004 CAPS 0xC0     b6 page 6 (pixel layer), b7 RGB565 pixel layer: the host's, forwarded below
//   005 CAPS2 0x02    b1 BUILD is there
//   008-00B BUILD     the bitstream's build stamp (USR_ACCESS)
//   010 IRQ_EN, 014 IRQ_CAUSE (W1C)   b7 page flip done (px_ev_flip), b6 video frame start (px_ev_vsync), b5 a NES frame is complete
//   020-027 DDR_CAL   the host's DDR2 calibration, as for the C64 core
//   0A0 CTRL     RW   b0 reset hold, b1 halt
//   0B0-0B3 MISS_PRG, 0B4-0B7 MISS_CHR, 0B8-0BB STALL   R   line fetches of the PRG and CHR caches, clocks the machine was held for a fetch
//   0D0-0D3 CART_BASE RW  DDR2 byte address of the cartridge image: PRG at +0, CHR at +0x200000 (default 0x01000000; bits 3:0 ignored)
//   0D4 WIN_PAGE RW   b4:0 the 64 KB page of the DMA window (nes_win): image offset = {WIN_PAGE, window address}
//   0E0-0EF INES RW   the 16 bytes of the iNES header of the image (byte 0 at 0E0): nes_flags makes the machine's mapper_flags word,
//                     PRG/CHR masks and mirroring from them, as NES_MiSTer's GameLoader does; written while CTRL.b0 holds the reset
//   0A1 SYSTEM   RW   b1:0 console type: 0 NTSC (21.477 MHz master, 60.10 Hz), 1 PAL (21.281 MHz, 50.0 Hz), 2 Dendy (NTSC clock, PAL frame); set while CTRL.b0 holds the reset
//   0A2 JOY      RW   b0 = 1: the host's joystick ports 1 and 2 are swapped before the two controller ports (T157, nes_pad.v); live
//   0F8 PAD1, 0FA PAD2  RW  T156: the USB pad of player 1 / 2, written by the application on every report and cleared when the pad leaves; OR-ed by nes_pad.v with
//                     the joystick and key sources. Bit order is the controller's own: 0 A, 1 B, 2 Select, 3 Start, 4 Up, 5 Down, 6 Left, 7 Right. (0F9 / 0FB: the
//                     16 bit pair's high byte, the SNES core's layout; ignored here)
//   0A4 STATUS   R    b0 machine in reset, b1 held (Freeze)
//   0A8-0A9 FRAMES    R   NES frame counter (low 16 bits)
//   0AC-0AD CPUADDR   R   the CPU's address bus (a sample)
//   0C0 FB_LATEST R   b7 valid, b1:0 the buffer of the newest complete frame
//   0C1 FRAME_SEQ R   8 bit counter that changes with every complete frame
//   0C2-0C3 FB_OVERRUNS R   lines that could not be handed to the DDR2 side in time
//   0C4-0C7 FB_BASE   RW  DDR2 byte address of frame buffer 0 (default 0x01D00000: the REU block of the host, as the Amiga core's)
//   0C8-0CB FB_STRIDE RW  bytes between the four frame buffers (default 0x00100000)
//   page 6 (0x6000-0x6FFF)   forwarded to the host's pixel layer (px_we, px_re, px_rdata)
//   everything else reads 0 and is acknowledged; the EEPROM window (addr[16]) is not here.
// The controller reads the contract's joystick and keyboard lines itself (nes_pad.v, T95 5.18) and, since T156, the PAD1 / PAD2 registers.
`timescale 1ns / 1ps
module nes_regs #(
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
    output wire [127:0] hdr,
    output wire [1:0]  sys_type,
    output wire        joy_swap,
    output wire [7:0]  pad1,
    output wire [7:0]  pad2,
    output wire [31:0] cart_base,
    input  wire [31:0] st_miss_prg,
    input  wire [31:0] st_miss_chr,
    input  wire [31:0] st_stall,
    output wire [4:0]  win_page,
    output wire [31:0] fb_base,
    output wire [31:0] fb_stride,
    input  wire [7:0]  status,
    input  wire [15:0] frames,
    input  wire [15:0] cpuaddr,
    input  wire [7:0]  fb_latest,
    input  wire [7:0]  fb_seq,
    input  wire [15:0] fb_over
);
    // >>> generated from cores/nes/app/gateware.toml (T264 S2): do not edit, `tools/toml_a_gen.py gen` rewrites it
    localparam [11:0] R_IDENT_ID = 12'h000;
    localparam [11:0] R_IDENT_CAPS = 12'h004;
    localparam [11:0] R_IDENT_CAPS2 = 12'h005;
    localparam [11:0] R_IDENT_BUILD = 12'h008;
    localparam [11:0] R_NES_IRQ_IRQ_EN = 12'h010;
    localparam [11:0] R_NES_IRQ_IRQ_CAUSE = 12'h014;
    localparam [11:0] R_NES_DDR_CAL_DDR_CAL0 = 12'h020;
    localparam [11:0] R_NES_DDR_CAL_DDR_CAL1 = 12'h024;
    localparam [11:0] R_NES_MACHINE_CTRL = 12'h0A0;
    localparam [11:0] R_NES_MACHINE_SYSTEM = 12'h0A1;
    localparam [11:0] R_NES_MACHINE_JOY = 12'h0A2;
    localparam [11:0] R_NES_MACHINE_STATUS = 12'h0A4;
    localparam [11:0] R_NES_MACHINE_FRAMES = 12'h0A8;
    localparam [11:0] R_NES_MACHINE_CPUADDR = 12'h0AC;
    localparam [11:0] R_NES_CACHE_MISS_PRG = 12'h0B0;
    localparam [11:0] R_NES_CACHE_MISS_CHR = 12'h0B4;
    localparam [11:0] R_NES_CACHE_STALL = 12'h0B8;
    localparam [11:0] R_NES_VIDEO_FB_LATEST = 12'h0C0;
    localparam [11:0] R_NES_VIDEO_FRAME_SEQ = 12'h0C1;
    localparam [11:0] R_NES_VIDEO_FB_OVERRUNS = 12'h0C2;
    localparam [11:0] R_NES_VIDEO_FB_BASE = 12'h0C4;
    localparam [11:0] R_NES_VIDEO_FB_STRIDE = 12'h0C8;
    localparam [11:0] R_NES_CART_CART_BASE = 12'h0D0;
    localparam [11:0] R_NES_CART_WIN_PAGE = 12'h0D4;
    localparam [11:0] R_NES_INES_HDR = 12'h0E0;
    localparam [11:0] R_NES_PAD_PAD1 = 12'h0F8;
    localparam [11:0] R_NES_PAD_PAD2 = 12'h0FA;
    // <<< generated
    reg [7:0]  ctrl_r = 8'h00;
    reg [127:0] hdr_r = 128'd0;
    reg [4:0]  page_r = 5'd0;
    reg [1:0]  sys_r = 2'd0;
    reg        joy_r = 1'b0;
    reg [7:0]  pad1_r = 8'd0, pad2_r = 8'd0;
    reg [31:0] cbase_r = 32'h01000000;
    reg [31:0] base_r = 32'h01D00000;
    reg [31:0] stride_r = 32'h00100000;
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
    assign hdr = hdr_r;
    assign win_page = page_r;
    assign sys_type = sys_r;
    assign joy_swap = joy_r;
    assign pad1 = pad1_r;
    assign pad2 = pad2_r;
    assign cart_base = cbase_r;
    assign fb_base = base_r;
    assign fb_stride = stride_r;
    assign ack = ack_r;
    assign irq = |(irq_en & cause);
    assign rdata = q_p6 ? px_rdata : q;

    wire [31:0] id = (ID_TRX2 != 0) ? 32'h32585254 : 32'h5F53454E;   // bytes "TRX2" / "NES_", byte 0 first

    always @(posedge clk) begin
        cause <= cause | ev;
        ack_r <= 1'b0;
        if (re | we) ack_r <= 1'b1;
        q_p6 <= 1'b0;
        if (re && !win && page == 4'h6) q_p6 <= 1'b1;

        if (we && !win && page == 4'h0) begin
            case (off)
                R_NES_IRQ_IRQ_EN: irq_en <= wdata;
                R_NES_IRQ_IRQ_CAUSE: cause <= (cause & ~wdata) | ev;
                R_NES_MACHINE_CTRL: ctrl_r <= wdata;
                R_NES_MACHINE_SYSTEM: sys_r <= wdata[1:0];
                R_NES_MACHINE_JOY: joy_r <= wdata[0];
                R_NES_PAD_PAD1: pad1_r <= wdata;
                R_NES_PAD_PAD2: pad2_r <= wdata;
                R_NES_CART_CART_BASE: cbase_r[7:0] <= wdata;
                R_NES_CART_CART_BASE + 12'd1: cbase_r[15:8] <= wdata;
                R_NES_CART_CART_BASE + 12'd2: cbase_r[23:16] <= wdata;
                R_NES_CART_CART_BASE + 12'd3: cbase_r[31:24] <= wdata;
                R_NES_CART_WIN_PAGE: page_r <= wdata[4:0];
                R_NES_VIDEO_FB_BASE: base_r[7:0] <= wdata;
                R_NES_VIDEO_FB_BASE + 12'd1: base_r[15:8] <= wdata;
                R_NES_VIDEO_FB_BASE + 12'd2: base_r[23:16] <= wdata;
                R_NES_VIDEO_FB_BASE + 12'd3: base_r[31:24] <= wdata;
                R_NES_VIDEO_FB_STRIDE: stride_r[7:0] <= wdata;
                R_NES_VIDEO_FB_STRIDE + 12'd1: stride_r[15:8] <= wdata;
                R_NES_VIDEO_FB_STRIDE + 12'd2: stride_r[23:16] <= wdata;
                R_NES_VIDEO_FB_STRIDE + 12'd3: stride_r[31:24] <= wdata;
                default: if (off[11:4] == (R_NES_INES_HDR + 0) >> 4) hdr_r[8*off[3:0] +: 8] <= wdata;
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
                R_IDENT_BUILD: q <= build[7:0];
                R_IDENT_BUILD + 12'd1: q <= build[15:8];
                R_IDENT_BUILD + 12'd2: q <= build[23:16];
                R_IDENT_BUILD + 12'd3: q <= build[31:24];
                R_NES_IRQ_IRQ_EN: q <= irq_en;
                R_NES_IRQ_IRQ_CAUSE: q <= cause;
                R_NES_DDR_CAL_DDR_CAL0: q <= ddr_cal[7:0];
                R_NES_DDR_CAL_DDR_CAL0 + 12'd1: q <= ddr_cal[15:8];
                R_NES_DDR_CAL_DDR_CAL0 + 12'd2: q <= ddr_cal[23:16];
                R_NES_DDR_CAL_DDR_CAL0 + 12'd3: q <= ddr_cal[31:24];
                R_NES_DDR_CAL_DDR_CAL1: q <= ddr_cal[39:32];
                R_NES_DDR_CAL_DDR_CAL1 + 12'd1: q <= ddr_cal[47:40];
                R_NES_DDR_CAL_DDR_CAL1 + 12'd2: q <= ddr_cal[55:48];
                R_NES_DDR_CAL_DDR_CAL1 + 12'd3: q <= ddr_cal[63:56];
                R_NES_MACHINE_CTRL: q <= ctrl_r;
                R_NES_MACHINE_SYSTEM: q <= {6'd0, sys_r};
                R_NES_MACHINE_JOY: q <= {7'd0, joy_r};
                R_NES_PAD_PAD1: q <= pad1_r;
                R_NES_PAD_PAD2: q <= pad2_r;
                R_NES_CACHE_MISS_PRG: q <= st_miss_prg[7:0];
                R_NES_CACHE_MISS_PRG + 12'd1: q <= st_miss_prg[15:8];
                R_NES_CACHE_MISS_PRG + 12'd2: q <= st_miss_prg[23:16];
                R_NES_CACHE_MISS_PRG + 12'd3: q <= st_miss_prg[31:24];
                R_NES_CACHE_MISS_CHR: q <= st_miss_chr[7:0];
                R_NES_CACHE_MISS_CHR + 12'd1: q <= st_miss_chr[15:8];
                R_NES_CACHE_MISS_CHR + 12'd2: q <= st_miss_chr[23:16];
                R_NES_CACHE_MISS_CHR + 12'd3: q <= st_miss_chr[31:24];
                R_NES_CACHE_STALL: q <= st_stall[7:0];
                R_NES_CACHE_STALL + 12'd1: q <= st_stall[15:8];
                R_NES_CACHE_STALL + 12'd2: q <= st_stall[23:16];
                R_NES_CACHE_STALL + 12'd3: q <= st_stall[31:24];
                R_NES_CART_CART_BASE: q <= cbase_r[7:0];
                R_NES_CART_CART_BASE + 12'd1: q <= cbase_r[15:8];
                R_NES_CART_CART_BASE + 12'd2: q <= cbase_r[23:16];
                R_NES_CART_CART_BASE + 12'd3: q <= cbase_r[31:24];
                R_NES_CART_WIN_PAGE: q <= {3'd0, page_r};
                R_NES_MACHINE_STATUS: q <= status;
                R_NES_MACHINE_FRAMES: q <= frames[7:0];
                R_NES_MACHINE_FRAMES + 12'd1: q <= frames[15:8];
                R_NES_MACHINE_CPUADDR: q <= cpuaddr[7:0];
                R_NES_MACHINE_CPUADDR + 12'd1: q <= cpuaddr[15:8];
                R_NES_VIDEO_FB_LATEST: q <= fb_latest;
                R_NES_VIDEO_FRAME_SEQ: q <= fb_seq;
                R_NES_VIDEO_FB_OVERRUNS: q <= fb_over[7:0];
                R_NES_VIDEO_FB_OVERRUNS + 12'd1: q <= fb_over[15:8];
                R_NES_VIDEO_FB_BASE: q <= base_r[7:0];
                R_NES_VIDEO_FB_BASE + 12'd1: q <= base_r[15:8];
                R_NES_VIDEO_FB_BASE + 12'd2: q <= base_r[23:16];
                R_NES_VIDEO_FB_BASE + 12'd3: q <= base_r[31:24];
                R_NES_VIDEO_FB_STRIDE: q <= stride_r[7:0];
                R_NES_VIDEO_FB_STRIDE + 12'd1: q <= stride_r[15:8];
                R_NES_VIDEO_FB_STRIDE + 12'd2: q <= stride_r[23:16];
                R_NES_VIDEO_FB_STRIDE + 12'd3: q <= stride_r[31:24];
                default: if (off[11:4] == (R_NES_INES_HDR + 0) >> 4) q <= hdr_r[8*off[3:0] +: 8];
            endcase
        end
    end
endmodule
