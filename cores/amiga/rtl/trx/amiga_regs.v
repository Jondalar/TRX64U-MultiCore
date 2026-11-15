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
//   1D0-1D1 PAD2_WORD (W: T225, the USB pad on game port 2: bit 0 up, 1 down, 2 left, 3 right, 4 fire/red, 5 blue (2nd button), 6 green, 7 yellow, 8 play, 9 reverse, 10 forward; 1D1 commits)   1D2 PAD2_MODE (W b1:0, 0 off, 1 joy 1 button, 2 joy 2 buttons, 3 CD32)
//   1C4 RAWKEY (W: {release, raw code} of one Amiga key event, queued in amiga_input; R b0 = room for 8 more events -- write only while it is 1)  (T141, FEATURES b4)
//   1F0 DLOG_STATUS (R) b0 not empty b1 overflow (write clears b1)   1F1 DLOG_TRACK (R)   1F2 DLOG_DRIVE (R)
//   1F3 DLOG_POP (W)
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
    output reg         dlog_pop = 1'b0,
    output reg  [1:0]  wb_idx = 2'd0               // the buffer for the next frame (registered, quasi static)
);
    wire win  = addr[16];
    wire [3:0]  page = addr[15:12];
    wire [11:0] off  = addr[11:0];
    wire p0 = ~win & (page == 4'd0);
    assign px_we = we & ~win & (page == 4'd6);
    assign px_re = re & ~win & (page == 4'd6);

    // ---- synchronisers
    (* ASYNC_REG = "TRUE" *) reg [2:0] fs_s = 3'b000, fd_s = 3'b000, mb_s = 3'b000;
    (* ASYNC_REG = "TRUE" *) reg [1:0] de_s = 2'b11;
    (* ASYNC_REG = "TRUE" *) reg [1:0] rr_s = 2'b00;
    always @(posedge clk) begin
        fs_s <= {fs_s[1:0], frame_start_tgl};
        fd_s <= {fd_s[1:0], frame_done_tgl};
        mb_s <= {mb_s[1:0], mb_req_tgl};
        de_s <= {de_s[0], dlog_empty};
        rr_s <= {rr_s[0], rk_room};
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
    reg [31:0] l_q = 32'd0;
    reg        wr_q = 1'b0, dr_q = 1'b0;
    always @(posedge clk) begin
        st_a <= status; st_b <= st_a; if (st_a == st_b) st_q <= st_b;
        fr_a <= frames; fr_b <= fr_a; if (fr_a == fr_b) fr_q <= fr_b;
        ov_a <= overruns; ov_b <= ov_a; if (ov_a == ov_b) ov_q <= ov_b;
        sv_a <= stat_val; sv_b <= sv_a; if (sv_a == sv_b) sv_q <= sv_b;
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
            12'h010: irq_en <= wdata;
            12'h014: cause <= (cause & ~wdata) | {ev_flip, ev_vsync, 3'b000, mb_ev, (dne_d & ~de_s[1]), fd_ev};
            12'h0A0: begin ctrl <= wdata; cfg_tgl <= ~cfg_tgl; end
            12'h0A1: begin mem_cfg <= wdata; cfg_tgl <= ~cfg_tgl; end
            12'h0A2: begin fast_cfg <= wdata; cfg_tgl <= ~cfg_tgl; end
            12'h0A3: begin cpu_cfg <= wdata; cfg_tgl <= ~cfg_tgl; end
            12'h0A4: begin chipset_cfg <= wdata; cfg_tgl <= ~cfg_tgl; end
            12'h0A5: begin floppy_cfg <= wdata; cfg_tgl <= ~cfg_tgl; end
            12'h0A6: begin ide_cfg <= wdata; cfg_tgl <= ~cfg_tgl; end
            12'h0A7: joy_cfg <= wdata;
            12'h0E0: rtc_en <= wdata[0];
            12'h0E1: rtc_ld[3:0] <= wdata[3:0];
            12'h0E2: begin rtc_ld[7:4] <= wdata[3:0]; rtc_ld[8] <= ~rtc_ld[8]; end
            12'h0B8: stat_sel <= wdata[3:0];
            12'h100, 12'h110, 12'h120, 12'h130: begin
                adf_present[off[5:4]] <= wdata[0]; adf_mount_tgl[off[5:4]] <= ~adf_mount_tgl[off[5:4]]; cfg_tgl <= ~cfg_tgl;
            end
            12'h180: begin hdf_present[0] <= wdata[0]; hdf_mount_tgl[0] <= ~hdf_mount_tgl[0]; cfg_tgl <= ~cfg_tgl; end
            12'h188: begin hdf_present[1] <= wdata[0]; hdf_mount_tgl[1] <= ~hdf_mount_tgl[1]; cfg_tgl <= ~cfg_tgl; end
            12'h184: hdf_size0[7:0] <= wdata;   12'h185: hdf_size0[15:8] <= wdata;
            12'h186: hdf_size0[23:16] <= wdata; 12'h187: hdf_size0[31:24] <= wdata;
            12'h18C: hdf_size1[7:0] <= wdata;   12'h18D: hdf_size1[15:8] <= wdata;
            12'h18E: hdf_size1[23:16] <= wdata; 12'h18F: hdf_size1[31:24] <= wdata;
            12'h1A8: mb_done_tgl <= ~mb_done_tgl;
            12'h1C0: mouse_dx <= wdata;
            12'h1C1: begin mouse_dy <= wdata; mouse_tgl <= ~mouse_tgl; end
            12'h1C2: mouse_btn <= wdata[2:0];
            12'h1C4: begin rk_data <= wdata; rk_tgl <= ~rk_tgl; end
            12'h1D0: pad_stage <= wdata;
            12'h1D1: pad_word <= {wdata[2:0], pad_stage};
            12'h1D2: pad_mode <= wdata[1:0];
            12'h1F0: if (wdata[1]) dlog_ovf_clr_tgl <= ~dlog_ovf_clr_tgl;
            12'h1F3: dlog_pop <= 1'b1;
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
            10'h000: wq <= {"G", "I", "M", "A"};
            10'h001: wq <= {8'h00, 8'h01, 8'h02, 8'hC0};
            10'h002: wq <= build;
            10'h003: wq <= {26'd0, 1'b1, 1'b1, 1'b0, 1'b1, 1'b1, HDF_SECT != 0};   // b5: PAD2 (1D0, T225), b4: RAWKEY (1C4)
            10'h004: wq <= {24'd0, irq_en};
            10'h005: wq <= {24'd0, cause};
            10'h008: wq <= ddr_cal[31:0];
            10'h009: wq <= ddr_cal[63:32];
            10'h028: wq <= {cpu_cfg, fast_cfg, mem_cfg, ctrl};
            10'h029: wq <= {joy_cfg, ide_cfg, floppy_cfg, chipset_cfg};
            10'h02A: wq <= {fr_q[15:8], fr_q[7:0], 8'h00, st_q};
            10'h02B: wq <= {seq, 6'd0, writing, 6'd0, shown, latest_valid, 5'd0, latest};
            10'h02C: wq <= {16'd0, ov_q[15:8], ov_q[7:0]};
            10'h02E: wq <= {28'd0, stat_sel};
            10'h038: wq <= {7'd0, rtc_loaded, 4'd0, rtc_ld[7:4], 4'd0, rtc_ld[3:0], 7'd0, rtc_en};
            10'h02F: wq <= sv_q;
            10'h040: wq <= {24'd0, 7'd0, adf_present[0]};
            10'h044: wq <= {24'd0, 7'd0, adf_present[1]};
            10'h048: wq <= {24'd0, 7'd0, adf_present[2]};
            10'h04C: wq <= {24'd0, 7'd0, adf_present[3]};
            10'h060: wq <= {24'd0, 7'd0, hdf_present[0]};
            10'h062: wq <= {24'd0, 7'd0, hdf_present[1]};
            10'h068: wq <= {24'd0, 5'd0, dr_q, wr_q, mb_pend};
            10'h069: wq <= l_q;
            10'h071: wq <= {31'd0, rr_s[1]};
            10'h070: wq <= {8'd0, 5'd0, mouse_btn, 16'd0};
            10'h07C: wq <= {8'd0, 6'd0, d_hd[9:8], d_hd[7:0], 6'd0, o_q, ~de_s[1]};
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
