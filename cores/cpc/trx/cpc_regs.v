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
                12'h010: irq_en <= wdata;
                12'h014: cause <= (cause & ~wdata) | ev;
                12'h0A0: ctrl_r <= wdata;
                12'h0A1: model_r <= wdata;
                12'h0A2: joy_r <= wdata;
                12'h0A3: crtc_r <= wdata;
                12'h0D4: page_r <= wdata[2:0];
                12'h0C4: base_r[7:0] <= wdata;
                12'h0C5: base_r[15:8] <= wdata;
                12'h0C6: base_r[23:16] <= wdata;
                12'h0C7: base_r[31:24] <= wdata;
                12'h0C8: stride_r[7:0] <= wdata;
                12'h0C9: stride_r[15:8] <= wdata;
                12'h0CA: stride_r[23:16] <= wdata;
                12'h0CB: stride_r[31:24] <= wdata;
                12'h140: tctrl_r <= wdata;
                12'h144: tbase_r[7:0] <= wdata;
                12'h145: tbase_r[15:8] <= wdata;
                12'h146: tbase_r[23:16] <= wdata;
                12'h147: tbase_r[31:24] <= wdata;
                12'h148: tpos_r[7:0] <= wdata;
                12'h149: tpos_r[15:8] <= wdata;
                12'h14A: tpos_r[23:16] <= wdata;
                12'h14B: tpos_r[31:24] <= wdata;
                12'h14C: tseek_t <= ~tseek_t;
                12'h180: d0b_r[7:0] <= wdata;
                12'h181: d0b_r[15:8] <= wdata;
                12'h182: d0b_r[23:16] <= wdata;
                12'h183: d0b_r[31:24] <= wdata;
                12'h184: d0s_r[7:0] <= wdata;
                12'h185: d0s_r[15:8] <= wdata;
                12'h186: d0s_r[23:16] <= wdata;
                12'h187: d0s_r[31:24] <= wdata;
                12'h188: d1b_r[7:0] <= wdata;
                12'h189: d1b_r[15:8] <= wdata;
                12'h18A: d1b_r[23:16] <= wdata;
                12'h18B: d1b_r[31:24] <= wdata;
                12'h18C: d1s_r[7:0] <= wdata;
                12'h18D: d1s_r[15:8] <= wdata;
                12'h18E: d1s_r[23:16] <= wdata;
                12'h18F: d1s_r[31:24] <= wdata;
                12'h190: begin
                    wp_r <= wdata[5:4];
                    if (wdata[0]) mount_t[0] <= ~mount_t[0];
                    if (wdata[1]) mount_t[1] <= ~mount_t[1];
                    if (wdata[2]) eject_t[0] <= ~eject_t[0];
                    if (wdata[3]) eject_t[1] <= ~eject_t[1];
                end
                default: begin
                    if (off >= 12'h160 && off < 12'h16A) inj_r[8 * (off - 12'h160) +: 8] <= wdata;
                    if (off >= 12'h1A0 && off < 12'h1B8 && !off[3]) begin
                        clr_t <= ~clr_t; clr_d <= off[4]; clr_i <= off[2:0]; clr_a <= wdata;
                    end
                end
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
                12'h006: q <= 8'd2;
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
                12'h030: q <= 8'h00;          // 628,992 = 0x99900
                12'h031: q <= 8'h99;
                12'h032: q <= 8'h09;
                12'h033: q <= 8'h00;
                12'h034: q <= 8'hD0;          // 720 = 0x2D0
                12'h035: q <= 8'h02;
                12'h036: q <= 8'h1C;          // 540 = 0x21C
                12'h037: q <= 8'h02;
                12'h038: q <= 8'd3;
                12'h0A0: q <= ctrl_r;
                12'h0A1: q <= model_r;
                12'h0A2: q <= joy_r;
                12'h0A3: q <= crtc_r;
                12'h0A4: q <= status;
                12'h0A8: q <= frames[7:0];
                12'h0A9: q <= frames[15:8];
                12'h0AC: q <= cpuaddr[7:0];
                12'h0AD: q <= cpuaddr[15:8];
                12'h0D4: q <= {5'd0, page_r};
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
                12'h140: q <= tctrl_r;
                12'h144: q <= tbase_r[7:0];
                12'h145: q <= tbase_r[15:8];
                12'h146: q <= tbase_r[23:16];
                12'h147: q <= tbase_r[31:24];
                12'h148: q <= tape_pos[7:0];
                12'h149: q <= tape_pos[15:8];
                12'h14A: q <= tape_pos[23:16];
                12'h14B: q <= {7'd0, tape_pos[24]};
                12'h150: q <= {4'd0, tape_stat};
                12'h151: q <= tape_under;
                12'h180: q <= d0b_r[7:0];
                12'h181: q <= d0b_r[15:8];
                12'h182: q <= d0b_r[23:16];
                12'h183: q <= d0b_r[31:24];
                12'h184: q <= d0s_r[7:0];
                12'h185: q <= d0s_r[15:8];
                12'h186: q <= d0s_r[23:16];
                12'h187: q <= d0s_r[31:24];
                12'h188: q <= d1b_r[7:0];
                12'h189: q <= d1b_r[15:8];
                12'h18A: q <= d1b_r[23:16];
                12'h18B: q <= d1b_r[31:24];
                12'h18C: q <= d1s_r[7:0];
                12'h18D: q <= d1s_r[15:8];
                12'h18E: q <= d1s_r[23:16];
                12'h18F: q <= d1s_r[31:24];
                12'h190: q <= {2'd0, wp_r, 4'd0};
                12'h191: q <= {6'd0, disk_stat};
                default: begin
                    if (off >= 12'h160 && off < 12'h16A) q <= inj_r[8 * (off - 12'h160) +: 8];
                    if (off >= 12'h1A0 && off < 12'h1A8) q <= dirty0[8 * (off - 12'h1A0) +: 8];
                    if (off >= 12'h1B0 && off < 12'h1B8) q <= dirty1[8 * (off - 12'h1B0) +: 8];
                end
            endcase
        end
    end
endmodule
