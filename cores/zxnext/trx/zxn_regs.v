// T116 D11 / P11: the register block the app sees at its core window (cw_*, cpu_clk, the bytes at 0x101A0000; the window the other cores answer),
// laid out like nes_regs.v and cpc_regs.v; the SD sector service and the mouse sit where the Amiga core puts them (amiga_regs.h), so that the
// application's sector-server and mouse libraries (T104 5.2) drive them unchanged. The whole machine runs on cpu_clk, so nothing crosses a
// clock here. Written for this project (TREX), no upstream code.
//   000-003 ID        "ZXN_" (a build with ID_TRX2 = 1 answers "TRX2")
//   004 CAPS 0xC0     b6 page 6 (pixel layer), b7 RGB565 pixel layer: the host's, forwarded below
//   005 CAPS2 0x02    b1 BUILD is there            006 CONTRACT  the layer-2 contract this core was written for (2)
//   008-00B BUILD     the bitstream's build stamp (USR_ACCESS)
//   00C FEATURES      b0 the SD size register (184) is in 512 byte sectors
//   010 IRQ_EN, 014 IRQ_CAUSE (W1C)   b7 page flip done (px_ev_flip), b6 video frame start (px_ev_vsync), b5 a Next frame is complete, b4 a block request
//   020-027 DDR_CAL   the host's DDR2 calibration, as for the C64 core
//   0A0 CTRL     RW   b0 reset hold, b1 halt (Freeze)
//   0A1 JOY      RW   b0 swaps the host's two joystick ports (default: port 2 is the Next's left stick, port 1 the right)
//   0A2 LOCK     RW   b0 frame lock off (default 0: the machine runs 0.09 % (48K timing) to 2.7 % (Pentagon) fast so that its frame is the C64 PAL frame the host shows, T116 Q6)
//   0A3 BOARD_ID RW   b3:0 the id NextReg 0x0F reports (T116 D10; reset 3 = issue 5; 0 = issue 2, 2 = issue 4); only the report, the machine's structure stays issue 5
//   0A4 STATUS   R    b0 machine in reset, b1 held (Freeze), b2 an SD image is mounted, b3 the card model is busy
//   0A5 MACHINE  R    b2:0 the machine timing (000 48K, 010 128K, 011 +3, 100 Pentagon), b3 60 Hz, b5:4 the CPU speed (3.5, 7, 14, 28 MHz)
//   0A8-0A9 FRAMES    R   Next frame counter (low 16 bits)
//   0B0-0B3 MISS_A, 0B4-0B7 MISS_B (Layer 2 lines that were not there in time), 0B8-0BB STALL (clocks the machine was held), 0BC-0BF PREFETCH   R
//   0C0 FB_LATEST R   b7 valid, b1:0 the buffer of the newest complete frame      0C1 FRAME_SEQ R   changes with every complete frame
//   0C2-0C3 FB_OVERRUNS R   lines that could not be handed to the DDR2 side in time
//   0C4-0C7 FB_BASE   RW  DDR2 byte address of frame buffer 0 (default 0x01D00000)      0C8-0CB FB_STRIDE RW  bytes between the four buffers (default 0x00100000)
//   0D0-0D3 RAM_BASE   RW DDR2 byte address of the machine's 2 MB (default 0x01000000, bits 20:0 ignored)
//   0D4-0D7 STAGE_BASE RW DDR2 byte address of the SD staging sector (default 0x01200000, 512 aligned)
//   0E0-0E6 RTC       RW  the app's clock as the DS1307 holds it, BCD: seconds, minutes, hours (24 h), weekday 1-7, day, month, year 00-99
//   0F0-0F1 FKEYS     RW  the Next's special keys F1-F10 (bit n = F(n+1)); 0F2 BUTTONS RW b0 the Multiface NMI button (the DivMMC "drive" button is the C64 RESTORE key)
//   180 SD_MOUNT      RW  b0 a card (an image) is present      184-187 SD_SIZE RW  its size in sectors
//   1A0 MBOX_STATUS   R   b0 pending, b1 write, b2 unit (0), b7:4 request tag    1A4-1A7 MBOX_LBA R    1A8 MBOX_DONE W   answers the request whose tag (b7:4 of the status) the write carries in b7:4
//   1B0-1B3 SD_READS, 1B4-1B7 SD_WRITES R, 1B8 SD_LASTCMD R
//   1C0 MOUSE_DX W (signed, x counter), 1C1 MOUSE_DY W (signed; the write sends the pair, y counter counts the other way), 1C2 MOUSE_BTN RW (b0 left, b1 right, b2 middle)
//   page 6 (0x6000-0x6FFF)   forwarded to the host's pixel layer (px_we, px_re, px_rdata)
//   everything else reads 0 and is acknowledged.
`timescale 1ns / 1ps
module zxn_regs #(
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
    input  wire        ev_block,
    output wire [7:0]  ctrl,
    output wire [3:0]  board_id,
    output wire        joy_swap,
    output wire        lock_off,
    output wire [31:0] fb_base,
    output wire [31:0] fb_stride,
    output wire [31:0] ram_base,
    output wire [31:0] stage_base,
    output wire [55:0] rtc,
    output wire [9:0]  fkeys,
    output wire        mf_btn,
    output wire        sd_present,
    output wire [31:0] sd_sectors,
    input  wire        mb_pend,
    input  wire        mb_wr,
    input  wire [31:0] mb_lba,
    input  wire [3:0]  mb_seq,
    output wire        mb_ack,
    output wire [7:0]  mouse_x,
    output wire [7:0]  mouse_y,
    output wire [2:0]  mouse_btn,
    input  wire [7:0]  status,
    input  wire [7:0]  machine,
    input  wire [15:0] frames,
    input  wire [31:0] st_miss_a,
    input  wire [31:0] st_miss_b,
    input  wire [31:0] st_stall,
    input  wire [31:0] st_pref,
    input  wire [31:0] st_reads,
    input  wire [31:0] st_writes,
    input  wire [7:0]  st_last_cmd,
    input  wire [7:0]  fb_latest,
    input  wire [7:0]  fb_seq,
    input  wire [15:0] fb_over
);
    reg [7:0]  ctrl_r = 8'h01;
    reg        joy_r = 1'b0, lock_r = 1'b0;
    reg [3:0]  bid_r = 4'd3;
    reg [31:0] base_r = 32'h01D00000;
    reg [31:0] stride_r = 32'h00100000;
    reg [31:0] rbase_r = 32'h01000000;
    reg [31:0] sbase_r = 32'h01200000;
    reg [55:0] rtc_r = 56'd0;
    reg [9:0]  fk_r = 10'd0;
    reg        mf_r = 1'b0;
    reg        mnt_r = 1'b0;
    reg [31:0] sz_r = 32'd0;
    reg        ack_pulse = 1'b0;
    reg [7:0]  mx_r = 8'd0, my_r = 8'd0;
    reg [7:0]  dx_r = 8'd0;
    reg [2:0]  mb_r = 3'd0;
    reg [7:0]  irq_en = 8'd0, cause = 8'd0;
    reg [7:0]  q = 8'd0;
    reg        q_p6 = 1'b0, ack_r = 1'b0;
    wire       win  = addr[16];
    wire [3:0] page = addr[15:12];
    wire [11:0] off = addr[11:0];
    wire [7:0] ev = {ev_flip, ev_vsync, ev_frame, ev_block, 4'b0000};

    assign px_we = we & ~win & (page == 4'h6);
    assign px_re = re & ~win & (page == 4'h6);
    assign ctrl = ctrl_r;
    assign board_id = bid_r;
    assign joy_swap = joy_r;
    assign lock_off = lock_r;
    assign fb_base = base_r;
    assign fb_stride = stride_r;
    assign ram_base = rbase_r;
    assign stage_base = sbase_r;
    assign rtc = rtc_r;
    assign fkeys = fk_r;
    assign mf_btn = mf_r;
    assign sd_present = mnt_r;
    assign sd_sectors = sz_r;
    assign mb_ack = ack_pulse;
    assign mouse_x = mx_r;
    assign mouse_y = my_r;
    assign mouse_btn = mb_r;
    assign ack = ack_r;
    assign irq = |(irq_en & cause);
    assign rdata = q_p6 ? px_rdata : q;

    wire [31:0] id = (ID_TRX2 != 0) ? 32'h32585254 : 32'h5F4E585A;   // bytes "TRX2" / "ZXN_", byte 0 first

    always @(posedge clk) begin
        cause <= cause | ev;
        ack_r <= 1'b0;
        ack_pulse <= 1'b0;
        if (re | we) ack_r <= 1'b1;
        q_p6 <= 1'b0;
        if (re && !win && page == 4'h6) q_p6 <= 1'b1;

        if (we && !win && page == 4'h0) begin
            case (off)
                12'h010: irq_en <= wdata;
                12'h014: cause <= (cause & ~wdata) | ev;
                12'h0A0: ctrl_r <= wdata;
                12'h0A1: joy_r <= wdata[0];
                12'h0A2: lock_r <= wdata[0];
                12'h0A3: bid_r <= wdata[3:0];
                12'h0C4: base_r[7:0] <= wdata;
                12'h0C5: base_r[15:8] <= wdata;
                12'h0C6: base_r[23:16] <= wdata;
                12'h0C7: base_r[31:24] <= wdata;
                12'h0C8: stride_r[7:0] <= wdata;
                12'h0C9: stride_r[15:8] <= wdata;
                12'h0CA: stride_r[23:16] <= wdata;
                12'h0CB: stride_r[31:24] <= wdata;
                12'h0D0: rbase_r[7:0] <= wdata;
                12'h0D1: rbase_r[15:8] <= wdata;
                12'h0D2: rbase_r[23:16] <= wdata;
                12'h0D3: rbase_r[31:24] <= wdata;
                12'h0D4: sbase_r[7:0] <= wdata;
                12'h0D5: sbase_r[15:8] <= wdata;
                12'h0D6: sbase_r[23:16] <= wdata;
                12'h0D7: sbase_r[31:24] <= wdata;
                12'h0F0: fk_r[7:0] <= wdata;
                12'h0F1: fk_r[9:8] <= wdata[1:0];
                12'h0F2: mf_r <= wdata[0];
                12'h180: mnt_r <= wdata[0];
                12'h184: sz_r[7:0] <= wdata;
                12'h185: sz_r[15:8] <= wdata;
                12'h186: sz_r[23:16] <= wdata;
                12'h187: sz_r[31:24] <= wdata;
                12'h1A8: if (wdata[7:4] == mb_seq) ack_pulse <= 1'b1;      // only the answer to the request that is pending counts: the tag is the one read with the status
                12'h1C0: dx_r <= wdata;
                12'h1C1: begin mx_r <= mx_r + dx_r; my_r <= my_r - wdata; dx_r <= 8'd0; end
                12'h1C2: mb_r <= wdata[2:0];
                default: if (off >= 12'h0E0 && off < 12'h0E7) rtc_r[8 * (off - 12'h0E0) +: 8] <= wdata;
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
                12'h006: q <= 8'h02;
                12'h008: q <= build[7:0];
                12'h009: q <= build[15:8];
                12'h00A: q <= build[23:16];
                12'h00B: q <= build[31:24];
                12'h00C: q <= 8'h01;
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
                12'h0A1: q <= {7'd0, joy_r};
                12'h0A2: q <= {7'd0, lock_r};
                12'h0A3: q <= {4'd0, bid_r};
                12'h0A4: q <= status;
                12'h0A5: q <= machine;
                12'h0A8: q <= frames[7:0];
                12'h0A9: q <= frames[15:8];
                12'h0B0: q <= st_miss_a[7:0];   12'h0B1: q <= st_miss_a[15:8];   12'h0B2: q <= st_miss_a[23:16];   12'h0B3: q <= st_miss_a[31:24];
                12'h0B4: q <= st_miss_b[7:0];   12'h0B5: q <= st_miss_b[15:8];   12'h0B6: q <= st_miss_b[23:16];   12'h0B7: q <= st_miss_b[31:24];
                12'h0B8: q <= st_stall[7:0];    12'h0B9: q <= st_stall[15:8];    12'h0BA: q <= st_stall[23:16];    12'h0BB: q <= st_stall[31:24];
                12'h0BC: q <= st_pref[7:0];     12'h0BD: q <= st_pref[15:8];     12'h0BE: q <= st_pref[23:16];     12'h0BF: q <= st_pref[31:24];
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
                12'h0D0: q <= rbase_r[7:0];
                12'h0D1: q <= rbase_r[15:8];
                12'h0D2: q <= rbase_r[23:16];
                12'h0D3: q <= rbase_r[31:24];
                12'h0D4: q <= sbase_r[7:0];
                12'h0D5: q <= sbase_r[15:8];
                12'h0D6: q <= sbase_r[23:16];
                12'h0D7: q <= sbase_r[31:24];
                12'h0F0: q <= fk_r[7:0];
                12'h0F1: q <= {6'd0, fk_r[9:8]};
                12'h0F2: q <= {7'd0, mf_r};
                12'h180: q <= {7'd0, mnt_r};
                12'h184: q <= sz_r[7:0];
                12'h185: q <= sz_r[15:8];
                12'h186: q <= sz_r[23:16];
                12'h187: q <= sz_r[31:24];
                12'h1A0: q <= {mb_seq, 2'b00, mb_wr, mb_pend};
                12'h1A4: q <= mb_lba[7:0];
                12'h1A5: q <= mb_lba[15:8];
                12'h1A6: q <= mb_lba[23:16];
                12'h1A7: q <= mb_lba[31:24];
                12'h1B0: q <= st_reads[7:0];    12'h1B1: q <= st_reads[15:8];    12'h1B2: q <= st_reads[23:16];    12'h1B3: q <= st_reads[31:24];
                12'h1B4: q <= st_writes[7:0];   12'h1B5: q <= st_writes[15:8];   12'h1B6: q <= st_writes[23:16];   12'h1B7: q <= st_writes[31:24];
                12'h1B8: q <= st_last_cmd;
                12'h1C2: q <= {5'd0, mb_r};
                default: if (off >= 12'h0E0 && off < 12'h0E7) q <= rtc_r[8 * (off - 12'h0E0) +: 8];
            endcase
        end
    end
endmodule
