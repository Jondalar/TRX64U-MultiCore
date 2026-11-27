// T85: the Amiga core's register block at the application's core window (cw_*, cpu_clk, the bytes at
// 0x101A0000; the window trx_io.v answers for the C64 core). Written for this project.
//
// Page 0 (cw_addr[16] = 0, cw_addr[15:12] = 0), byte registers:
//   000-003 ID "AMIG"            004 CAPS 0xC0 (page 6 = the host's pixel layer, RGB565)   005 CAPS2 0x02 (BUILD)
//   006 CORE_VER 0x01            008-00B BUILD (USR_ACCESS)
//   00C FEATURES (R) b2 the catch-up KPI is in STAT_SEL 11. b1 the audio KPIs are in STAT_SEL 8..10. b0 the HDF size registers are in 512-byte sectors, not bytes (HDF_SECT; T102 D1). 0 = bytes (the Amiga core)
//   010 IRQ_EN   014 IRQ_CAUSE (write 1 clears)   b0 a frame is complete (FB_LATEST new), b1 a floppy write is in the log,
//                                b2 an HDF request is posted, b6 output frame start (host), b7 page flip done (host)
//   020-027 DDR_CAL (the host's DDR2 calibration)
//   0A0 CTRL   b0 HOLD (machine in reset, cache flushed; 1 after power-up: load Kickstart / images, then write 0)
//              b1 VIDEO_OFF (no frame writes)
//   0A1 MEM_CFG (memory_config: [1:0] chip 0=512K 1=1M 2=1.5M 3=2M, [3:2] slow 0/512K/1M/1.5M)
//   0A2 FAST_CFG (fastram_config)   0A3 CPU_CFG (0 = 68000)   0A4 CHIPSET_CFG ([3] ECS Agnus+Denise; bit 1 stays 0: PAL)
//   0E0 RTC_EN (T191: b0 = the clock is on)   0E1 RTC_VAL (a nibble)   0E2 RTC_IDX (the write commits: 0-12 register, 14 loader hold, 15 zero the second + loaded)
//   0E3 R b0 loaded (amiga_rtc.v)
//   0A7 JOY_CFG (T157: b0 = 1 swaps the host's joystick ports 1 and 2 before the Amiga's two ports; written at once, no commit)
//   0A5 FLOPPY_CFG ([0] fast, [1] write protect all, [3:2] number of drives - 1)   0A6 IDE_CFG (0x21 = IDE on)
//   0A8 STATUS (R) b0 hold b1 running b2 power LED b3 floppy LED b4 hard disk LED b5 CPU reset out
//                         b6 PAUSED (Freeze: the machine is held, every DDR2 access it began is done)
//   0AA-0AB FRAMES (R, 16 bit)   0AC FB_LATEST (R) b7 valid, b1:0 buffer   0AD FB_SHOWN (R)   0AE FB_WRITING (R)
//   0AF FRAME_SEQ (R, 8 bit)      0B0-0B1 FB_OVERRUNS (R)
//   0B8 STAT_SEL (RW, 0 reads, 1 misses, 2 writes, 3 prefetches, 4 stall clocks, 5 stall info, 6 longest wait, 7 longest tick gap,
//               8 audio {over[15:0], under[15:0]}, 9 audio fill {5'd0, max[10:0], 5'd0, min[10:0]} while playing since HOLD, 10 audio fill now, 11 catch-up {lost ticks[15:0], 4'd0, deepest debt[11:0]};
//               the FIFO target is 480 pairs = 10 ms)   0BC-0BF STAT_VAL (R, 32 bit, the selected counter)
//   (the counters used to be five 32-bit registers of their own: the read mux of that made the router fail, run 393)
//   Any write to CTRL, MEM_CFG..IDE_CFG or a MOUNT register also copies all configuration into the core (one commit toggle).
//   100+16*d  FLOPPY d (0..3): +0 MOUNT (RW) b0 present; any write re-announces the drive to the Minimig
//   180+8*d   HDF d (0..1):    +0 MOUNT (RW) b0 present; +4..+7 SIZE (W only, 32 bit) in bytes, or in sectors when FEATURES b0 is set -- set before mounting
//   1A0 MBOX_STATUS (R) b0 pending b1 write b2 drive   1A4-1A7 MBOX_LBA (R)   1A8 MBOX_DONE (W, any value)
//   1C0 MOUSE_DX (W)   1C1 MOUSE_DY (W: sends the pair)   1C2 MOUSE_BTN (RW b2:0)
//   1F0 DLOG_STATUS (R) b0 not empty b1 overflow (write clears b1)   1F1 DLOG_TRACK (R)   1F2 DLOG_DRIVE (R)
//   1F3 DLOG_POP (W)
//   1D0-1D1 PAD2_WORD (W: T225, the USB pad on game port 2: bit 0 up, 1 down, 2 left, 3 right, 4 fire/red, 5 blue (2nd button), 6 green, 7 yellow, 8 play, 9 reverse, 10 forward; 1D1 commits)   1D2 PAD2_MODE (W b1:0, 0 off, 1 joy 1 button, 2 joy 2 buttons, 3 CD32)
//   1C4 RAWKEY (W: {release, raw code} of one Amiga key event, queued in amiga_input; R b0 = room for 8 more events -- write only while it is 1)  (T141, FEATURES b4)
//   T97 5.10 (this file is the A1200's own copy, cores/amiga1200/rtl/amiga_regs.diff):
//   0C0-0C3 DIW_H (R)  {7'd0, HDIWSTOP[8:0], 7'd0, HDIWSTRT[8:0]}   0C4-0C7 DIW_V (R)  {5'd0, VDIWSTOP[10:0], 5'd0, VDIWSTRT[10:0]}
//         the decoded display window (DIWSTRT/DIWSTOP/DIWHIGH as Agnus holds them: lores pixels, lines), live
//   0D0-0D1 FB_SKIP_X (W only, 9 bit, hires pixels after hblank)  0D2 FB_SKIP_Y (W, 8 bit, lines after vblank)
//   0D4 FB_COLS_W (W, 7 bit, words of 8 hires pixels, <= 90)  0D6-0D7 FB_LINES (W, 9 bit, <= 270)
//         the cropped window (it lands at the top left of the 720 x 540 buffer; the application positions the pixel layer); defaults 36, 10, 90, 270
// Page 6 (0x6000-0x6FFF): forwarded to the host's pixel layer (PX_*), PX_BACK (088-08B) is snooped to know which
// buffer the application has chosen to show.
// Everything else reads 0 and is acknowledged.
`timescale 1ns / 1ps
module amiga_regs #(
    parameter integer FB_BUFS = 3,
    parameter [31:0] FB_BYTE0 = 32'h01D00000,
    parameter integer FB_SHIFT = 20,                // log2 of the byte distance of the buffers
    parameter integer HDF_SECT = 0                  // the HDF size registers hold sectors (amiga_sdc), reported in FEATURES b0
) (
    input  wire        clk,                          // cpu_clk
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
    // configuration (cpu_clk values, quasi static)
    output reg  [7:0]  ctrl = 8'h01,
    output reg  [7:0]  mem_cfg = 8'h01,
    output reg  [7:0]  fast_cfg = 8'h00,
    output reg  [7:0]  cpu_cfg = 8'h00,
    output reg  [7:0]  chipset_cfg = 8'h00,
    output reg  [7:0]  floppy_cfg = 8'h00,
    output reg  [7:0]  ide_cfg = 8'h21,
    output reg  [7:0]  joy_cfg = 8'h00,             // T157: b0 swaps the two joystick ports (no commit toggle: quasi static)
    output reg         rtc_en = 1'b0,               // T191: 0E0 b0 the setting Real-time clock (no commit toggle)
    output reg  [8:0]  rtc_ld = 9'd0,               // T191: 0E1 nibble, 0E2 index (the write is the commit: flips the toggle)
    input  wire        rtc_loaded,                  // T191: 0E3 b0 (R), the clock holds the time
    output reg  [3:0]  adf_present = 4'd0,
    output reg  [3:0]  adf_mount_tgl = 4'd0,
    output reg  [1:0]  hdf_present = 2'd0,
    output reg  [1:0]  hdf_mount_tgl = 2'd0,
    output reg  [31:0] hdf_size0 = 32'd0,
    output reg  [31:0] hdf_size1 = 32'd0,
    output reg  [7:0]  mouse_dx = 8'd0,
    output reg  [7:0]  mouse_dy = 8'd0,
    output reg  [2:0]  mouse_btn = 3'd0,
    output reg         mouse_tgl = 1'b0,
    output reg  [7:0]  rk_data = 8'd0,              // T141: a raw Amiga key event, taken by amiga_input at the toggle
    output reg         rk_tgl = 1'b0,
    output reg  [10:0] pad_word = 11'd0,           // T225: 1D0 low byte (staged), 1D1 b2:0 commits the 11 bit pad state of game port 2
    output reg  [1:0]  pad_mode = 2'd0,            // T225: 1D2 b1:0 0 port 2 as before, 1 one-button joystick, 2 two-button, 3 CD32 pad (no commit toggle: quasi static)
    input  wire        rk_room,                      // T141: amiga_input's queue has room for 8 more (sys_clk, one bit)
    output reg         mb_done_tgl = 1'b0,
    output reg         dlog_ovf_clr_tgl = 1'b0,
    output reg         cfg_tgl = 1'b0,                // a write that the core has to copy the configuration for
    // from the core (sys_clk values; synchronised here)
    input  wire [7:0]  status,
    input  wire        frame_start_tgl,
    input  wire        frame_done_tgl,
    input  wire [15:0] frames,
    input  wire [15:0] overruns,
    output reg  [3:0]  stat_sel = 4'd0,
    input  wire [31:0] stat_val,
    input  wire        mb_req_tgl,
    input  wire        mb_req_we,
    input  wire        mb_req_drv,
    input  wire [31:0] mb_req_lba,
    input  wire        dlog_empty,
    input  wire [9:0]  dlog_head,                    // {drive[1:0], track[7:0]}
    input  wire        dlog_ovf,
    input  wire [39:0] diw,                          // {vdiwstop[10:0], vdiwstrt[10:0], hdiwstop[8:0], hdiwstrt[8:0]} (sys_clk, live)
    output wire [32:0] fbwin,                        // {lines[8:0], cols_w[6:0], skip_y[7:0], skip_x[8:0]}
    output reg         dlog_pop = 1'b0,
    output reg  [1:0]  wb_idx = 2'd0               // the buffer for the next frame (registered, quasi static)
);
    // >>> generated from cores/amiga1200/app/gateware.toml (T264 S2): do not edit, `tools/toml_a_gen.py gen` rewrites it
    localparam [11:0] R_IDENT_ID = 12'h000;
    localparam [11:0] R_IDENT_CAPS = 12'h004;
    localparam [11:0] R_IDENT_CAPS2 = 12'h005;
    localparam [11:0] R_IDENT_CORE_VER = 12'h006;
    localparam [11:0] R_IDENT_BUILD = 12'h008;
    localparam [11:0] R_IDENT_FEATURES = 12'h00C;
    localparam [11:0] R_IRQ_IRQ_EN = 12'h010;
    localparam [11:0] R_IRQ_IRQ_CAUSE = 12'h014;
    localparam [11:0] R_DDR_CAL_DDR_CAL0 = 12'h020;
    localparam [11:0] R_DDR_CAL_DDR_CAL1 = 12'h024;
    localparam [11:0] R_MACHINE_CTRL = 12'h0A0;
    localparam [11:0] R_MACHINE_MEM_CFG = 12'h0A1;
    localparam [11:0] R_MACHINE_FAST_CFG = 12'h0A2;
    localparam [11:0] R_MACHINE_CPU_CFG = 12'h0A3;
    localparam [11:0] R_MACHINE_CHIPSET_CFG = 12'h0A4;
    localparam [11:0] R_MACHINE_FLOPPY_CFG = 12'h0A5;
    localparam [11:0] R_MACHINE_IDE_CFG = 12'h0A6;
    localparam [11:0] R_MACHINE_JOY_CFG = 12'h0A7;
    localparam [11:0] R_MACHINE_STATUS = 12'h0A8;
    localparam [11:0] R_MACHINE_FRAMES = 12'h0AA;
    localparam [11:0] R_VIDEO_FB_LATEST = 12'h0AC;
    localparam [11:0] R_VIDEO_FB_SHOWN = 12'h0AD;
    localparam [11:0] R_VIDEO_FB_WRITING = 12'h0AE;
    localparam [11:0] R_VIDEO_FRAME_SEQ = 12'h0AF;
    localparam [11:0] R_VIDEO_FB_OVERRUNS = 12'h0B0;
    localparam [11:0] R_STAT_STAT_SEL = 12'h0B8;
    localparam [11:0] R_STAT_STAT_VAL = 12'h0BC;
    localparam [11:0] R_DIW_DIW_H = 12'h0C0;
    localparam [11:0] R_DIW_DIW_V = 12'h0C4;
    localparam [11:0] R_CROP_SKIP_X = 12'h0D0;
    localparam [11:0] R_CROP_SKIP_Y = 12'h0D2;
    localparam [11:0] R_CROP_COLS = 12'h0D4;
    localparam [11:0] R_CROP_LINES = 12'h0D6;
    localparam [11:0] R_RTC_RTC_EN = 12'h0E0;
    localparam [11:0] R_RTC_RTC_VAL = 12'h0E1;
    localparam [11:0] R_RTC_RTC_IDX = 12'h0E2;
    localparam [11:0] R_RTC_RTC_STAT = 12'h0E3;
    localparam [11:0] R_FLOPPY_MOUNT = 12'h100;
    localparam [11:0] R_HDF_MOUNT = 12'h180;
    localparam [11:0] R_HDF_SIZE = 12'h184;
    localparam [11:0] R_MBOX_STATUS = 12'h1A0;
    localparam [11:0] R_MBOX_LBA = 12'h1A4;
    localparam [11:0] R_MBOX_DONE = 12'h1A8;
    localparam [11:0] R_MOUSE_DX = 12'h1C0;
    localparam [11:0] R_MOUSE_DY = 12'h1C1;
    localparam [11:0] R_MOUSE_BTN = 12'h1C2;
    localparam [11:0] R_RAWKEY_RAWKEY = 12'h1C4;
    localparam [11:0] R_PAD2_WORD = 12'h1D0;
    localparam [11:0] R_PAD2_MODE = 12'h1D2;
    localparam [11:0] R_DLOG_STATUS = 12'h1F0;
    localparam [11:0] R_DLOG_TRACK = 12'h1F1;
    localparam [11:0] R_DLOG_DRIVE = 12'h1F2;
    localparam [11:0] R_DLOG_POP = 12'h1F3;
    // <<< generated
    wire win  = addr[16];
    wire [3:0]  page = addr[15:12];
    wire [11:0] off  = addr[11:0];
    wire p0 = ~win & (page == 4'd0);
    assign px_we = we & ~win & (page == 4'd6);
    assign px_re = re & ~win & (page == 4'd6);

    // ---- synchronisers
    (* ASYNC_REG = "TRUE" *) reg [2:0] fs_s = 3'b000, fd_s = 3'b000, mb_s = 3'b000;
    (* ASYNC_REG = "TRUE" *) reg [1:0] de_s = 2'b11;
    always @(posedge clk) begin
        fs_s <= {fs_s[1:0], frame_start_tgl};
        fd_s <= {fd_s[1:0], frame_done_tgl};
        mb_s <= {mb_s[1:0], mb_req_tgl};
        de_s <= {de_s[0], dlog_empty};
    end
    wire fs_ev = fs_s[2] ^ fs_s[1];
    wire fd_ev = fd_s[2] ^ fd_s[1];
    wire mb_ev = mb_s[2] ^ mb_s[1];
    // multi-bit values from sys_clk: accepted when two samples in a row agree (few and narrow: status, frames, one
    // selectable 32-bit counter); the mailbox request is copied at its toggle, the dirty log head is in this clock
    reg [7:0] st_a, st_b, st_q;
    reg [15:0] fr_a, fr_b, fr_q, ov_a, ov_b, ov_q;
    reg [31:0] sv_a, sv_b, sv_q;
    reg        o_a, o_b, o_q;
    reg [39:0] d_a, d_q;                            // the display window, two flops deep (it changes on a register write; the application reads it twice)
    reg [31:0] l_q = 32'd0;
    (* ASYNC_REG = "TRUE" *) reg [1:0] rr_s = 2'b00;
    reg        wr_q = 1'b0, dr_q = 1'b0;
    always @(posedge clk) begin
        st_a <= status; st_b <= st_a; if (st_a == st_b) st_q <= st_b;
        fr_a <= frames; fr_b <= fr_a; if (fr_a == fr_b) fr_q <= fr_b;
        ov_a <= overruns; ov_b <= ov_a; if (ov_a == ov_b) ov_q <= ov_b;
        sv_a <= stat_val; sv_b <= sv_a; if (sv_a == sv_b) sv_q <= sv_b;
        d_a <= diw; d_q <= d_a;
        rr_s <= {rr_s[0], rk_room};
        o_a <= dlog_ovf; o_b <= o_a; if (o_a == o_b) o_q <= o_b;
        if (mb_ev) begin l_q <= mb_req_lba; wr_q <= mb_req_we; dr_q <= mb_req_drv; end   // stable since before the toggle
    end

    // ---- frame buffers
    reg [1:0] shown = 2'd0, committed = 2'd0, latest = 2'd0, writing = 2'd0;
    reg       latest_valid = 1'b0;
    reg [7:0] seq = 8'd0;
    reg [23:0] pxb_stg = 24'd0;
    // the buffer for the frame that starts next, from the state of this moment (latest = the frame that just ended):
    // not the one on screen, not the one the application committed, if possible not the latest
    function [1:0] pick(input [1:0] sh, input [1:0] co, input [1:0] la);
        integer i;
        reg done;
        begin
            pick = 2'd0; done = 1'b0;
            for (i = 0; i < FB_BUFS; i = i + 1)
                if (!done && i != sh && i != co && i != la) begin pick = i[1:0]; done = 1'b1; end
            for (i = 0; i < FB_BUFS; i = i + 1)
                if (!done && i != sh && i != co) begin pick = i[1:0]; done = 1'b1; end
        end
    endfunction
    wire [1:0] wb_n = pick(shown, committed, latest);
    always @(posedge clk) wb_idx <= wb_n;
    wire [31:0] px_val = {wdata, pxb_stg};
    wire [31:0] px_rel = px_val - FB_BYTE0;
    wire        px_back_commit = px_we & (off[11:0] == 12'h08B);

    // ---- registers
    reg [8:0] w_skx = 9'd36;
    reg [7:0] w_sky = 8'd10;
    reg [6:0] w_cols = 7'd90;
    reg [8:0] w_lines = 9'd270;
    assign fbwin = {w_lines, w_cols, w_sky, w_skx};
    reg [7:0] irq_en = 8'd0, cause = 8'd0;
    reg [7:0] q = 8'd0;
    reg [31:0] wq = 32'd0;
    reg [1:0] wsel = 2'd0;
    reg       q_p6 = 1'b0, ack_r = 1'b0, ack1 = 1'b0;
    wire [9:0] d_hd = dlog_head;
    assign irq = |(irq_en & cause);
    assign rdata = q_p6 ? px_rdata : q;
    assign ack = ack_r;
    reg [7:0] pad_stage = 8'd0;
    reg mb_pend = 1'b0;
    reg dne_d = 1'b1;

    always @(posedge clk) begin
        dlog_pop <= 1'b0;
        dne_d <= de_s[1];
        cause <= cause | {ev_flip, ev_vsync, 3'b000, mb_ev, (dne_d & ~de_s[1]), fd_ev};
        // frame buffers
        if (fs_ev) writing <= wb_n;
        if (fd_ev) begin latest <= writing; latest_valid <= 1'b1; seq <= seq + 8'd1; end
        if (ev_flip) shown <= committed;
        if (px_we && off == 12'h088) pxb_stg[7:0] <= wdata;
        if (px_we && off == 12'h089) pxb_stg[15:8] <= wdata;
        if (px_we && off == 12'h08A) pxb_stg[23:16] <= wdata;
        if (px_back_commit && px_rel[31:FB_SHIFT + 2] == 0 && px_val >= FB_BYTE0) committed <= px_rel[FB_SHIFT +: 2];

        if (we && p0) begin
            case (off)
            R_IRQ_IRQ_EN: irq_en <= wdata;
            R_IRQ_IRQ_CAUSE: cause <= (cause & ~wdata) | {ev_flip, ev_vsync, 3'b000, mb_ev, (dne_d & ~de_s[1]), fd_ev};
            R_MACHINE_CTRL: begin ctrl <= wdata; cfg_tgl <= ~cfg_tgl; end
            R_MACHINE_MEM_CFG: begin mem_cfg <= wdata; cfg_tgl <= ~cfg_tgl; end
            R_MACHINE_FAST_CFG: begin fast_cfg <= wdata; cfg_tgl <= ~cfg_tgl; end
            R_MACHINE_CPU_CFG: begin cpu_cfg <= wdata; cfg_tgl <= ~cfg_tgl; end
            R_MACHINE_CHIPSET_CFG: begin chipset_cfg <= wdata; cfg_tgl <= ~cfg_tgl; end
            R_MACHINE_FLOPPY_CFG: begin floppy_cfg <= wdata; cfg_tgl <= ~cfg_tgl; end
            R_MACHINE_IDE_CFG: begin ide_cfg <= wdata; cfg_tgl <= ~cfg_tgl; end
            R_MACHINE_JOY_CFG: joy_cfg <= wdata;
            R_RTC_RTC_EN: rtc_en <= wdata[0];
            R_RTC_RTC_VAL: rtc_ld[3:0] <= wdata[3:0];
            R_RTC_RTC_IDX: begin rtc_ld[7:4] <= wdata[3:0]; rtc_ld[8] <= ~rtc_ld[8]; end
            R_STAT_STAT_SEL: stat_sel <= wdata[3:0];
            R_FLOPPY_MOUNT, R_FLOPPY_MOUNT + 12'd16, R_FLOPPY_MOUNT + 12'd32, R_FLOPPY_MOUNT + 12'd48: begin
                adf_present[off[5:4]] <= wdata[0]; adf_mount_tgl[off[5:4]] <= ~adf_mount_tgl[off[5:4]]; cfg_tgl <= ~cfg_tgl;
            end
            R_HDF_MOUNT: begin hdf_present[0] <= wdata[0]; hdf_mount_tgl[0] <= ~hdf_mount_tgl[0]; cfg_tgl <= ~cfg_tgl; end
            R_HDF_MOUNT + 12'd8: begin hdf_present[1] <= wdata[0]; hdf_mount_tgl[1] <= ~hdf_mount_tgl[1]; cfg_tgl <= ~cfg_tgl; end
            R_HDF_SIZE: hdf_size0[7:0] <= wdata;   R_HDF_SIZE + 12'd1: hdf_size0[15:8] <= wdata;
            R_HDF_SIZE + 12'd2: hdf_size0[23:16] <= wdata; R_HDF_SIZE + 12'd3: hdf_size0[31:24] <= wdata;
            R_HDF_SIZE + 12'd8: hdf_size1[7:0] <= wdata;   R_HDF_SIZE + 12'd9: hdf_size1[15:8] <= wdata;
            R_HDF_SIZE + 12'd10: hdf_size1[23:16] <= wdata; R_HDF_SIZE + 12'd11: hdf_size1[31:24] <= wdata;
            R_CROP_SKIP_X: begin w_skx[7:0] <= wdata; cfg_tgl <= ~cfg_tgl; end
            R_CROP_SKIP_X + 12'd1: begin w_skx[8] <= wdata[0]; cfg_tgl <= ~cfg_tgl; end
            R_CROP_SKIP_Y: begin w_sky <= wdata; cfg_tgl <= ~cfg_tgl; end
            R_CROP_COLS: begin w_cols <= wdata[6:0]; cfg_tgl <= ~cfg_tgl; end
            R_CROP_LINES: begin w_lines[7:0] <= wdata; cfg_tgl <= ~cfg_tgl; end
            R_CROP_LINES + 12'd1: begin w_lines[8] <= wdata[0]; cfg_tgl <= ~cfg_tgl; end
            R_MBOX_DONE: mb_done_tgl <= ~mb_done_tgl;
            R_MOUSE_DX: mouse_dx <= wdata;
            R_MOUSE_DY: begin mouse_dy <= wdata; mouse_tgl <= ~mouse_tgl; end
            R_MOUSE_BTN: mouse_btn <= wdata[2:0];
            R_RAWKEY_RAWKEY: begin rk_data <= wdata; rk_tgl <= ~rk_tgl; end
            R_PAD2_WORD: pad_stage <= wdata;
            R_PAD2_WORD + 12'd1: pad_word <= {wdata[2:0], pad_stage};
            R_PAD2_MODE: pad_mode <= wdata[1:0];
            R_DLOG_STATUS: if (wdata[1]) dlog_ovf_clr_tgl <= ~dlog_ovf_clr_tgl;
            R_DLOG_POP: dlog_pop <= 1'b1;
            default: ;
            endcase
        end

        // read: the word (32 bit) is chosen by off[8:2] and registered, the byte by off[1:0] one clock later;
        // the acknowledge comes two clocks after the request, so the data is there
        if (re) begin
        q_p6 <= (~win && page == 4'd6);
        wsel <= off[1:0];
        wq <= 32'h0;
        if (p0) begin
            case (off[11:2])
            R_IDENT_ID >> 2: wq <= {"G", "I", "M", "A"};
            R_IDENT_CAPS >> 2: wq <= {8'h00, 8'h01, 8'h02, 8'hC0};
            R_IDENT_BUILD >> 2: wq <= build;
            R_IDENT_FEATURES >> 2: wq <= {26'd0, 1'b1, 1'b1, 1'b1, 1'b1, 1'b1, HDF_SECT != 0};   // b3: the display window (0C0) and crop window (0D0) registers, b5: PAD2 (1D0, T225), b4: RAWKEY (1C4)
            R_IRQ_IRQ_EN >> 2: wq <= {24'd0, irq_en};
            R_IRQ_IRQ_CAUSE >> 2: wq <= {24'd0, cause};
            R_DDR_CAL_DDR_CAL0 >> 2: wq <= ddr_cal[31:0];
            R_DDR_CAL_DDR_CAL1 >> 2: wq <= ddr_cal[63:32];
            R_MACHINE_CTRL >> 2: wq <= {cpu_cfg, fast_cfg, mem_cfg, ctrl};
            R_MACHINE_CHIPSET_CFG >> 2: wq <= {joy_cfg, ide_cfg, floppy_cfg, chipset_cfg};
            R_MACHINE_STATUS >> 2: wq <= {fr_q[15:8], fr_q[7:0], 8'h00, st_q};
            R_VIDEO_FB_LATEST >> 2: wq <= {seq, 6'd0, writing, 6'd0, shown, latest_valid, 5'd0, latest};
            R_VIDEO_FB_OVERRUNS >> 2: wq <= {16'd0, ov_q[15:8], ov_q[7:0]};
            R_STAT_STAT_SEL >> 2: wq <= {28'd0, stat_sel};
            R_RTC_RTC_EN >> 2: wq <= {7'd0, rtc_loaded, 4'd0, rtc_ld[7:4], 4'd0, rtc_ld[3:0], 7'd0, rtc_en};
            R_STAT_STAT_VAL >> 2: wq <= sv_q;
            R_DIW_DIW_H >> 2: wq <= {7'd0, d_q[17:9], 7'd0, d_q[8:0]};
            R_DIW_DIW_V >> 2: wq <= {5'd0, d_q[39:29], 5'd0, d_q[28:18]};
            R_FLOPPY_MOUNT >> 2: wq <= {24'd0, 7'd0, adf_present[0]};
            (R_FLOPPY_MOUNT + 16) >> 2: wq <= {24'd0, 7'd0, adf_present[1]};
            (R_FLOPPY_MOUNT + 32) >> 2: wq <= {24'd0, 7'd0, adf_present[2]};
            (R_FLOPPY_MOUNT + 48) >> 2: wq <= {24'd0, 7'd0, adf_present[3]};
            R_HDF_MOUNT >> 2: wq <= {24'd0, 7'd0, hdf_present[0]};
            (R_HDF_MOUNT + 8) >> 2: wq <= {24'd0, 7'd0, hdf_present[1]};
            R_MBOX_STATUS >> 2: wq <= {24'd0, 5'd0, dr_q, wr_q, mb_pend};
            R_MBOX_LBA >> 2: wq <= l_q;
            R_MOUSE_DX >> 2: wq <= {8'd0, 5'd0, mouse_btn, 16'd0};
            R_RAWKEY_RAWKEY >> 2: wq <= {31'd0, rr_s[1]};
            R_DLOG_STATUS >> 2: wq <= {8'd0, 6'd0, d_hd[9:8], d_hd[7:0], 6'd0, o_q, ~de_s[1]};
            default: ;
            endcase
        end
        end
        q <= wq >> (8 * wsel);
        ack1 <= re | we;
        ack_r <= ack1;
    end
    // a mailbox request is pending from its post until the application writes MBOX_DONE
    reg done_d = 1'b0;
    always @(posedge clk) begin
        done_d <= mb_done_tgl;
        if (mb_ev) mb_pend <= 1'b1;
        else if (done_d != mb_done_tgl) mb_pend <= 1'b0;
    end
endmodule
