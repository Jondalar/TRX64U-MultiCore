// T92 D11: the register block the app sees at its core window (cw_*, cpu_clk, the bytes at 0x101A0000), laid out like zx_regs.v /
// c264_regs.v. Written for this project (TREX), no upstream code.
//   000-003 ID        "CPC_" (a build with ID_TRX2 = 1 answers "TRX2")
//   004 CAPS 0xC0     b6 page 6 (pixel layer), b7 RGB565 pixel layer: the host's, forwarded below
//   005 CAPS2 0x02    b1 BUILD is there        006 CONTRACT  the layer-2 contract this core was written for (2)
//   008-00B BUILD     the bitstream's build stamp (USR_ACCESS)
//   010 IRQ_EN, 014 IRQ_CAUSE (W1C)   b7 page flip done (px_ev_flip), b6 video frame start (px_ev_vsync), b5 a CPC frame is complete
//   020-027 DDR_CAL   the host's DDR2 calibration, as for the C64 core
//   030-033 FRAME_CLKS  sys_clk per standard CPC frame (628,992: 312 lines of 1,024 Gate Array clocks at 16 MHz x 32/63 of sys_clk, D4)
//   034-035 SRC_W 720, 036-037 SRC_H 540 (the frame buffer: every CPC line twice)    038 BUFS 3
//   0A0 CTRL     RW   b0 reset hold, b1 halt (Freeze)
//   0A1 MODEL    RW   0 = CPC 464 (64 KB, no FDC), 1 = 664 (64 KB), 2 = 6128 (128 KB); taken in reset
//   0A2 JOY      RW   b0 swaps the host's two joystick ports (default: port 2 is joystick 0, port 1 is joystick 1)
//   0A3 CRTC     RW   b0 CRTC type (0 HD6845S/UM6845, 1 UM6845R)
//   0A4 STATUS   R    b0 machine in reset, b1 held (Freeze), b3:2 the model running
//   0A8-0A9 FRAMES    R   CPC frame counter (CRTC vertical syncs, low 16 bits)
//   0AC-0AD CPUADDR   R   the CPU's address bus (a sample)
//   0D4 WIN_PAGE RW   b0 the 64 KB page of the RAM window (cpc_win): 0 the base RAM, 1 the extension RAM; b2:1 the 16 KB bank the ROM window
//                     shows (ROM_SEL: 0 lower ROM, 1 upper ROM 0, 2 upper ROM 7; the host's ROM aperture is 16 KB wide, see cpc_win.v)
//   0C0 FB_LATEST R   b7 valid, b1:0 the buffer of the newest complete frame      0C1 FRAME_SEQ R   8 bit counter, changes with every complete frame
//   0C2-0C3 FB_OVERRUNS R   lines that could not be handed to the DDR2 side in time
//   0C4-0C7 FB_BASE   RW  DDR2 byte address of frame buffer 0 (default 0x01D00000)      0C8-0CB FB_STRIDE RW  bytes between the three buffers (default 0x00100000)
//   140 TAPE_CTRL RW  b0 play: a rising edge starts the tape, the PPI's motor bit gates it, a falling edge stops it
//   144-147 TAPE_BASE RW  DDR2 byte address of the pulse stream (16 byte aligned)
//   148-14B TAPE_POS  W: the word position a seek loads; R: the position now      14C TAPE_SEEK W: any write loads TAPE_POS
//   150 TAPE_STATUS R b0 playing, b1 end of tape, b2 stop marker, b3 DDR2 read stuck      151 TAPE_UNDERRUN R
//   160-169 INJECT    RW  the app's keys: an 80 bit CPC matrix OR-ed into the keyboard (index = line * 8 + bit)
//   180-183 D0_BASE RW, 184-187 D0_SIZE RW, 188-18B D1_BASE RW, 18C-18F D1_SIZE RW   DDR2 byte address and size in bytes of the disk images
//   190 DISK_CTRL W   b0/b1 mount drive A/B (the FDC re-reads the image header: write after base, size and write-protect), b2/b3 eject A/B
//                     (size 0), b4/b5 write-protect A/B (taken at mount)
//   191 DISK_STATUS R b0 a sector transfer is on its way, b1 a transfer read beyond the image (zeros returned)
//   1A0-1A7 D0_DIRTY, 1B0-1B7 D1_DIRTY  R/W1C  one bit per 16 KB of the image written by the FDC (bit n of byte m: chunk 8 m + n); a write of a
//                     byte clears the bits that are set in it
//   page 6 (0x6000-0x6FFF)   forwarded to the host's pixel layer (px_we, px_re, px_rdata)
//   everything else reads 0 and is acknowledged.
`timescale 1ns / 1ps
module cpc_regs #(
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
    output wire [1:0]  model,
    output wire        joy_swap,
    output wire        crtc_type,
    output wire [2:0]  win_page,
    output wire [31:0] fb_base,
    output wire [31:0] fb_stride,
    output wire        tape_play,
    output wire [31:0] tape_base,
    output wire [24:0] tape_seek_pos,
    output wire        tape_seek_tgl,
    output wire [79:0] inject,
    output wire [31:0] d0_base,
    output wire [31:0] d0_size,
    output wire [31:0] d1_base,
    output wire [31:0] d1_size,
    output wire [1:0]  mount_tgl,                   // toggles: the caller turns each into one mount pulse in sys_clk
    output wire [1:0]  eject_tgl,
    output wire [1:0]  disk_wp,
    input  wire [63:0] dirty0,
    input  wire [63:0] dirty1,
    output wire [7:0]  dirty_clr_a,                 // cpu_clk: clear the bits of byte dirty_clr_i in drive dirty_clr_d (the mask dirty_clr_a, a toggle announces it)
    output wire        dirty_clr_d,
    output wire        dirty_clr_tgl,
    output wire [2:0]  dirty_clr_i,
    input  wire [7:0]  status,
    input  wire [15:0] frames,
    input  wire [15:0] cpuaddr,
    input  wire [7:0]  fb_latest,
    input  wire [7:0]  fb_seq,
    input  wire [15:0] fb_over,
    input  wire [24:0] tape_pos,
    input  wire [3:0]  tape_stat,
    input  wire [7:0]  tape_under,
    input  wire [1:0]  disk_stat
);
    // >>> generated from cores/cpc/app/gateware.toml (T264 S2): do not edit, `tools/toml_a_gen.py gen` rewrites it
    localparam [11:0] R_IDENT_ID = 12'h000;
    localparam [11:0] R_IDENT_CAPS = 12'h004;
    localparam [11:0] R_IDENT_CAPS2 = 12'h005;
    localparam [11:0] R_IDENT_CONTRACT = 12'h006;
    localparam [11:0] R_IDENT_BUILD = 12'h008;
    localparam [11:0] R_IRQ_IRQ_EN = 12'h010;
    localparam [11:0] R_IRQ_IRQ_CAUSE = 12'h014;
    localparam [11:0] R_DDR_CAL_DDR_CAL0 = 12'h020;
    localparam [11:0] R_DDR_CAL_DDR_CAL1 = 12'h024;
    localparam [11:0] R_GEOM_FRAME_CLKS = 12'h030;
    localparam [11:0] R_GEOM_SRC_W = 12'h034;
    localparam [11:0] R_GEOM_SRC_H = 12'h036;
    localparam [11:0] R_GEOM_BUFS = 12'h038;
    localparam [11:0] R_MACHINE_CTRL = 12'h0A0;
    localparam [11:0] R_MACHINE_MODEL = 12'h0A1;
    localparam [11:0] R_MACHINE_JOY = 12'h0A2;
    localparam [11:0] R_MACHINE_CRTC = 12'h0A3;
    localparam [11:0] R_MACHINE_STATUS = 12'h0A4;
    localparam [11:0] R_MACHINE_FRAMES = 12'h0A8;
    localparam [11:0] R_MACHINE_CPUADDR = 12'h0AC;
    localparam [11:0] R_VIDEO_FB_LATEST = 12'h0C0;
    localparam [11:0] R_VIDEO_FRAME_SEQ = 12'h0C1;
    localparam [11:0] R_VIDEO_FB_OVERRUNS = 12'h0C2;
    localparam [11:0] R_VIDEO_FB_BASE = 12'h0C4;
    localparam [11:0] R_VIDEO_FB_STRIDE = 12'h0C8;
    localparam [11:0] R_WIN_WIN_PAGE = 12'h0D4;
    localparam [11:0] R_TAPE_CTRL = 12'h140;
    localparam [11:0] R_TAPE_BASE = 12'h144;
    localparam [11:0] R_TAPE_POS = 12'h148;
    localparam [11:0] R_TAPE_SEEK = 12'h14C;
    localparam [11:0] R_TAPE_STATUS = 12'h150;
    localparam [11:0] R_TAPE_UNDERRUN = 12'h151;
    localparam [11:0] R_INJECT_KEYS = 12'h160;
    localparam [11:0] R_DISK_D0_BASE = 12'h180;
    localparam [11:0] R_DISK_D0_SIZE = 12'h184;
    localparam [11:0] R_DISK_D1_BASE = 12'h188;
    localparam [11:0] R_DISK_D1_SIZE = 12'h18C;
    localparam [11:0] R_DISK_CTRL = 12'h190;
    localparam [11:0] R_DISK_STATUS = 12'h191;
    localparam [11:0] R_DIRTY0_MAP = 12'h1A0;
    localparam [11:0] R_DIRTY1_MAP = 12'h1B0;
    // <<< generated
    reg [7:0]  ctrl_r = 8'h00, model_r = 8'h02, joy_r = 8'h00, crtc_r = 8'h01;
    reg [2:0]  page_r = 3'b000;
    reg [31:0] base_r = 32'h01D00000;
    reg [31:0] stride_r = 32'h00100000;
    reg [7:0]  tctrl_r = 8'h00;
    reg [31:0] tbase_r = 32'h01800000;
    reg [31:0] tpos_r = 32'd0;
    reg        tseek_t = 1'b0;
    reg [79:0] inj_r = 80'd0;
    reg [31:0] d0b_r = 32'h02000000, d0s_r = 32'd0, d1b_r = 32'h02100000, d1s_r = 32'd0;
    reg [1:0]  mount_t = 2'b00, eject_t = 2'b00, wp_r = 2'b00;
    reg [7:0]  irq_en = 8'd0, cause = 8'd0;
    reg [7:0]  q = 8'd0;
    reg        q_p6 = 1'b0, ack_r = 1'b0;
    reg        clr_t = 1'b0, clr_d = 1'b0;
    reg [2:0]  clr_i = 3'd0;
    reg [7:0]  clr_a = 8'd0;
    wire       win  = addr[16];
    wire [3:0] page = addr[15:12];
    wire [11:0] off = addr[11:0];
    wire [7:0] ev = {ev_flip, ev_vsync, ev_frame, 5'b00000};

    assign px_we = we & ~win & (page == 4'h6);
    assign px_re = re & ~win & (page == 4'h6);
    assign ctrl = ctrl_r;
    assign model = model_r[1:0];
    assign joy_swap = joy_r[0];
    assign crtc_type = crtc_r[0];
    assign win_page = page_r;
    assign fb_base = base_r;
    assign fb_stride = stride_r;
    assign tape_play = tctrl_r[0];
    assign tape_base = tbase_r;
    assign tape_seek_pos = tpos_r[24:0];
    assign tape_seek_tgl = tseek_t;
    assign inject = inj_r;
    assign d0_base = d0b_r; assign d0_size = d0s_r; assign d1_base = d1b_r; assign d1_size = d1s_r;
    assign mount_tgl = mount_t; assign eject_tgl = eject_t; assign disk_wp = wp_r;
    assign dirty_clr_tgl = clr_t; assign dirty_clr_d = clr_d; assign dirty_clr_i = clr_i; assign dirty_clr_a = clr_a;
    assign ack = ack_r;
    assign irq = |(irq_en & cause);
    assign rdata = q_p6 ? px_rdata : q;

    wire [31:0] id = (ID_TRX2 != 0) ? 32'h32585254 : 32'h5F435043;   // bytes "TRX2" / "CPC_", byte 0 first

    always @(posedge clk) begin
        cause <= cause | ev;
        ack_r <= 1'b0;
        if (re | we) ack_r <= 1'b1;
        q_p6 <= 1'b0;
        if (re && !win && page == 4'h6) q_p6 <= 1'b1;

        if (we && !win && page == 4'h0) begin
            case (off)
                R_IRQ_IRQ_EN: irq_en <= wdata;
                R_IRQ_IRQ_CAUSE: cause <= (cause & ~wdata) | ev;
                R_MACHINE_CTRL: ctrl_r <= wdata;
                R_MACHINE_MODEL: model_r <= wdata;
                R_MACHINE_JOY: joy_r <= wdata;
                R_MACHINE_CRTC: crtc_r <= wdata;
                R_WIN_WIN_PAGE: page_r <= wdata[2:0];
                R_VIDEO_FB_BASE: base_r[7:0] <= wdata;
                R_VIDEO_FB_BASE + 12'd1: base_r[15:8] <= wdata;
                R_VIDEO_FB_BASE + 12'd2: base_r[23:16] <= wdata;
                R_VIDEO_FB_BASE + 12'd3: base_r[31:24] <= wdata;
                R_VIDEO_FB_STRIDE: stride_r[7:0] <= wdata;
                R_VIDEO_FB_STRIDE + 12'd1: stride_r[15:8] <= wdata;
                R_VIDEO_FB_STRIDE + 12'd2: stride_r[23:16] <= wdata;
                R_VIDEO_FB_STRIDE + 12'd3: stride_r[31:24] <= wdata;
                R_TAPE_CTRL: tctrl_r <= wdata;
                R_TAPE_BASE: tbase_r[7:0] <= wdata;
                R_TAPE_BASE + 12'd1: tbase_r[15:8] <= wdata;
                R_TAPE_BASE + 12'd2: tbase_r[23:16] <= wdata;
                R_TAPE_BASE + 12'd3: tbase_r[31:24] <= wdata;
                R_TAPE_POS: tpos_r[7:0] <= wdata;
                R_TAPE_POS + 12'd1: tpos_r[15:8] <= wdata;
                R_TAPE_POS + 12'd2: tpos_r[23:16] <= wdata;
                R_TAPE_POS + 12'd3: tpos_r[31:24] <= wdata;
                R_TAPE_SEEK: tseek_t <= ~tseek_t;
                R_DISK_D0_BASE: d0b_r[7:0] <= wdata;
                R_DISK_D0_BASE + 12'd1: d0b_r[15:8] <= wdata;
                R_DISK_D0_BASE + 12'd2: d0b_r[23:16] <= wdata;
                R_DISK_D0_BASE + 12'd3: d0b_r[31:24] <= wdata;
                R_DISK_D0_SIZE: d0s_r[7:0] <= wdata;
                R_DISK_D0_SIZE + 12'd1: d0s_r[15:8] <= wdata;
                R_DISK_D0_SIZE + 12'd2: d0s_r[23:16] <= wdata;
                R_DISK_D0_SIZE + 12'd3: d0s_r[31:24] <= wdata;
                R_DISK_D1_BASE: d1b_r[7:0] <= wdata;
                R_DISK_D1_BASE + 12'd1: d1b_r[15:8] <= wdata;
                R_DISK_D1_BASE + 12'd2: d1b_r[23:16] <= wdata;
                R_DISK_D1_BASE + 12'd3: d1b_r[31:24] <= wdata;
                R_DISK_D1_SIZE: d1s_r[7:0] <= wdata;
                R_DISK_D1_SIZE + 12'd1: d1s_r[15:8] <= wdata;
                R_DISK_D1_SIZE + 12'd2: d1s_r[23:16] <= wdata;
                R_DISK_D1_SIZE + 12'd3: d1s_r[31:24] <= wdata;
                R_DISK_CTRL: begin
                    wp_r <= wdata[5:4];
                    if (wdata[0]) mount_t[0] <= ~mount_t[0];
                    if (wdata[1]) mount_t[1] <= ~mount_t[1];
                    if (wdata[2]) eject_t[0] <= ~eject_t[0];
                    if (wdata[3]) eject_t[1] <= ~eject_t[1];
                end
                default: begin
                    if (off >= R_INJECT_KEYS && off < R_INJECT_KEYS + 12'd10) inj_r[8 * (off - R_INJECT_KEYS) +: 8] <= wdata;
                    if (off >= R_DIRTY0_MAP && off < R_DIRTY0_MAP + 12'd24 && !off[3]) begin
                        clr_t <= ~clr_t; clr_d <= off[4]; clr_i <= off[2:0]; clr_a <= wdata;
                    end
                end
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
                R_IDENT_CONTRACT: q <= 8'd2;
                R_IDENT_BUILD: q <= build[7:0];
                R_IDENT_BUILD + 12'd1: q <= build[15:8];
                R_IDENT_BUILD + 12'd2: q <= build[23:16];
                R_IDENT_BUILD + 12'd3: q <= build[31:24];
                R_IRQ_IRQ_EN: q <= irq_en;
                R_IRQ_IRQ_CAUSE: q <= cause;
                R_DDR_CAL_DDR_CAL0: q <= ddr_cal[7:0];
                R_DDR_CAL_DDR_CAL0 + 12'd1: q <= ddr_cal[15:8];
                R_DDR_CAL_DDR_CAL0 + 12'd2: q <= ddr_cal[23:16];
                R_DDR_CAL_DDR_CAL0 + 12'd3: q <= ddr_cal[31:24];
                R_DDR_CAL_DDR_CAL1: q <= ddr_cal[39:32];
                R_DDR_CAL_DDR_CAL1 + 12'd1: q <= ddr_cal[47:40];
                R_DDR_CAL_DDR_CAL1 + 12'd2: q <= ddr_cal[55:48];
                R_DDR_CAL_DDR_CAL1 + 12'd3: q <= ddr_cal[63:56];
                R_GEOM_FRAME_CLKS: q <= 8'h00;          // 628,992 = 0x99900
                R_GEOM_FRAME_CLKS + 12'd1: q <= 8'h99;
                R_GEOM_FRAME_CLKS + 12'd2: q <= 8'h09;
                R_GEOM_FRAME_CLKS + 12'd3: q <= 8'h00;
                R_GEOM_SRC_W: q <= 8'hD0;          // 720 = 0x2D0
                R_GEOM_SRC_W + 12'd1: q <= 8'h02;
                R_GEOM_SRC_H: q <= 8'h1C;          // 540 = 0x21C
                R_GEOM_SRC_H + 12'd1: q <= 8'h02;
                R_GEOM_BUFS: q <= 8'd3;
                R_MACHINE_CTRL: q <= ctrl_r;
                R_MACHINE_MODEL: q <= model_r;
                R_MACHINE_JOY: q <= joy_r;
                R_MACHINE_CRTC: q <= crtc_r;
                R_MACHINE_STATUS: q <= status;
                R_MACHINE_FRAMES: q <= frames[7:0];
                R_MACHINE_FRAMES + 12'd1: q <= frames[15:8];
                R_MACHINE_CPUADDR: q <= cpuaddr[7:0];
                R_MACHINE_CPUADDR + 12'd1: q <= cpuaddr[15:8];
                R_WIN_WIN_PAGE: q <= {5'd0, page_r};
                R_VIDEO_FB_LATEST: q <= fb_latest;
                R_VIDEO_FRAME_SEQ: q <= fb_seq;
                R_VIDEO_FB_OVERRUNS: q <= fb_over[7:0];
                R_VIDEO_FB_OVERRUNS + 12'd1: q <= fb_over[15:8];
                R_VIDEO_FB_BASE: q <= base_r[7:0];
                R_VIDEO_FB_BASE + 12'd1: q <= base_r[15:8];
                R_VIDEO_FB_BASE + 12'd2: q <= base_r[23:16];
                R_VIDEO_FB_BASE + 12'd3: q <= base_r[31:24];
                R_VIDEO_FB_STRIDE: q <= stride_r[7:0];
                R_VIDEO_FB_STRIDE + 12'd1: q <= stride_r[15:8];
                R_VIDEO_FB_STRIDE + 12'd2: q <= stride_r[23:16];
                R_VIDEO_FB_STRIDE + 12'd3: q <= stride_r[31:24];
                R_TAPE_CTRL: q <= tctrl_r;
                R_TAPE_BASE: q <= tbase_r[7:0];
                R_TAPE_BASE + 12'd1: q <= tbase_r[15:8];
                R_TAPE_BASE + 12'd2: q <= tbase_r[23:16];
                R_TAPE_BASE + 12'd3: q <= tbase_r[31:24];
                R_TAPE_POS: q <= tape_pos[7:0];
                R_TAPE_POS + 12'd1: q <= tape_pos[15:8];
                R_TAPE_POS + 12'd2: q <= tape_pos[23:16];
                R_TAPE_POS + 12'd3: q <= {7'd0, tape_pos[24]};
                R_TAPE_STATUS: q <= {4'd0, tape_stat};
                R_TAPE_UNDERRUN: q <= tape_under;
                R_DISK_D0_BASE: q <= d0b_r[7:0];
                R_DISK_D0_BASE + 12'd1: q <= d0b_r[15:8];
                R_DISK_D0_BASE + 12'd2: q <= d0b_r[23:16];
                R_DISK_D0_BASE + 12'd3: q <= d0b_r[31:24];
                R_DISK_D0_SIZE: q <= d0s_r[7:0];
                R_DISK_D0_SIZE + 12'd1: q <= d0s_r[15:8];
                R_DISK_D0_SIZE + 12'd2: q <= d0s_r[23:16];
                R_DISK_D0_SIZE + 12'd3: q <= d0s_r[31:24];
                R_DISK_D1_BASE: q <= d1b_r[7:0];
                R_DISK_D1_BASE + 12'd1: q <= d1b_r[15:8];
                R_DISK_D1_BASE + 12'd2: q <= d1b_r[23:16];
                R_DISK_D1_BASE + 12'd3: q <= d1b_r[31:24];
                R_DISK_D1_SIZE: q <= d1s_r[7:0];
                R_DISK_D1_SIZE + 12'd1: q <= d1s_r[15:8];
                R_DISK_D1_SIZE + 12'd2: q <= d1s_r[23:16];
                R_DISK_D1_SIZE + 12'd3: q <= d1s_r[31:24];
                R_DISK_CTRL: q <= {2'd0, wp_r, 4'd0};
                R_DISK_STATUS: q <= {6'd0, disk_stat};
                default: begin
                    if (off >= R_INJECT_KEYS && off < R_INJECT_KEYS + 12'd10) q <= inj_r[8 * (off - R_INJECT_KEYS) +: 8];
                    if (off >= R_DIRTY0_MAP && off < R_DIRTY0_MAP + 12'd8) q <= dirty0[8 * (off - R_DIRTY0_MAP) +: 8];
                    if (off >= R_DIRTY1_MAP && off < R_DIRTY1_MAP + 12'd8) q <= dirty1[8 * (off - R_DIRTY1_MAP) +: 8];
                end
            endcase
        end
    end
endmodule
