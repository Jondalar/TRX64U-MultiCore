// T91 D11: the register block the app sees at its core window (cw_*, cpu_clk, the bytes at 0x101A0000; the same window trx_io.v
// answers for the C64 core), laid out like nes_regs.v / c264_regs.v. Written for this project (TREX), no upstream code.
//   000-003 ID        "ZXSP" (a build with ID_TRX2 = 1 answers "TRX2")
//   004 CAPS 0xC0     b6 page 6 (pixel layer), b7 RGB565 pixel layer: the host's, forwarded below
//   005 CAPS2 0x02    b1 BUILD is there
//   006 CONTRACT      the layer-2 contract this core was written for (2)
//   008-00B BUILD     the bitstream's build stamp (USR_ACCESS)
//   010 IRQ_EN, 014 IRQ_CAUSE (W1C)   b7 page flip done (px_ev_flip), b6 video frame start (px_ev_vsync), b5 a Spectrum frame is complete
//   020-027 DDR_CAL   the host's DDR2 calibration, as for the C64 core
//   030-033 FRAME_CLKS  sys_clk per picture frame (628,992: the scaler's frame, D4)       034 DOT0  0 (no scaler dot: the picture is a frame buffer)
//   035-036 GEOM_LINES  lines of the frame buffer (280, 16 bit)    037-038 PIX_W pixels per line of it (344, 16 bit)
//   0A0 CTRL     RW   b0 reset hold, b1 halt (Freeze), b2 the tape's ear signal on the speaker
//   0A1 MODEL    RW   b0 0 = ZX Spectrum 48K, 1 = 128K (taken in reset)
//   0A2 JOY1     RW   what the host's joystick port 1 acts as: 0 none, 1 Kempston, 2 Sinclair 1 (keys 6-0), 3 Sinclair 2 (keys 1-5), 4 Cursor (5 6 7 8 0)
//   0A3 JOY2     RW   the same for port 2
//   0A4 STATUS   R    b0 machine in reset, b1 held (Freeze), b2 snapshot load busy, b3 128K model running
//   0A5 SNAP_GO  W    any write starts the snapshot load (below) while CTRL.b0 holds the reset; STATUS.b2 (busy) rises within 1 us and falls when the Z80 runs
//   0A8-0A9 FRAMES    R   Spectrum frame counter (low 16 bits)
//   0AC-0AD CPUADDR   R   the CPU's address bus (a sample)
//   0C0 FB_LATEST R   b7 valid, b1:0 the buffer of the newest complete frame (T119)
//   0C1 FRAME_SEQ R   8 bit counter that changes with every complete frame
//   0C2-0C3 FB_OVERRUNS R   lines that could not be handed to the DDR2 side in time
//   0C4-0C7 FB_BASE   RW  DDR2 byte address of frame buffer 0 (default 0x01D00000: the REU block of the host, as the C264's)
//   0C8-0CB FB_STRIDE RW  bytes between the four frame buffers (default 0x00100000)
//   0D4 WIN_PAGE RW   b0 the 64 KB page of the RAM window (zx_win); b2:1 the 16 KB bank the ROM window shows (ROM_SEL: the host's ROM aperture
//                     is 16 KB wide, see zx_win.v)
//   100-11A SNAP_DIR  RW  the Z80's register image for the load (DIR, 212 bits, byte 0 = DIR[7:0]: A, F, A', F', I, R, SP, PC, BC, DE, HL, IX, BC', DE', HL', IY, IM/IFF)
//   11B SNAP_7FFD RW   11C SNAP_BORDER RW (b2:0)   120-12F SNAP_AY RW (the AY's 16 registers)   130 SNAP_AYSEL RW
//   140 TAPE_CTRL RW  b0 play (a rising edge starts the tape, a falling edge stops it)
//   144-147 TAPE_BASE RW  DDR2 byte address of the pulse stream (16 byte aligned)
//   148-14B TAPE_POS  W: the word position a seek loads; R: the position now      14C TAPE_SEEK W: any write loads TAPE_POS
//   150 TAPE_STATUS R b0 playing, b1 end of tape, b2 stop marker, b3 DDR2 read stuck      151 TAPE_UNDERRUN R
//   160-164 INJECT    RW  the app's keys: a 40 bit Spectrum matrix OR-ed into the keyboard (index = half-row * 5 + bit)
//   page 6 (0x6000-0x6FFF)   forwarded to the host's pixel layer (px_we, px_re, px_rdata)
//   everything else reads 0 and is acknowledged.
`timescale 1ns / 1ps
module zx_regs #(
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
    output wire        model,
    output wire [2:0]  joy1_mode,
    output wire [2:0]  joy2_mode,
    output wire [2:0]  win_page,
    output wire        snap_go_tgl,
    output wire [215:0] snap_dir,
    output wire [7:0]  snap_7ffd,
    output wire [2:0]  snap_border,
    output wire [127:0] snap_ay,
    output wire [3:0]  snap_aysel,
    output wire        tape_play,
    output wire [31:0] tape_base,
    output wire [24:0] tape_seek_pos,
    output wire        tape_seek_tgl,
    output wire [39:0] inject,
    input  wire [7:0]  status,
    input  wire [15:0] frames,
    input  wire [15:0] cpuaddr,
    input  wire [24:0] tape_pos,
    input  wire [3:0]  tape_stat,
    input  wire [7:0]  tape_under,
    input  wire [7:0]  fb_latest,
    input  wire [7:0]  fb_seq,
    input  wire [15:0] fb_over,
    output wire [31:0] fb_base,
    output wire [31:0] fb_stride
);
    reg [7:0]  ctrl_r = 8'h00, model_r = 8'h00, j1_r = 8'h00, j2_r = 8'h00;
    reg [2:0]  page_r = 3'b000;
    reg        snap_t = 1'b0;
    reg [215:0] dir_r = 216'd0;
    reg [7:0]  p7_r = 8'h00, bord_r = 8'h00, aysel_r = 8'h00;
    reg [127:0] ay_r = 128'd0;
    reg [7:0]  tctrl_r = 8'h00;
    reg [31:0] tbase_r = 32'h01800000;
    reg [31:0] tpos_r = 32'd0;
    reg        tseek_t = 1'b0;
    reg [39:0] inj_r = 40'd0;
    reg [31:0] fbb_r = 32'h01D00000, fbs_r = 32'h00100000;
    // the frame buffer's report crosses from sys_clk: a value counts when two samples in a row agree
    reg [7:0]  l_a = 8'd0, l_b = 8'd0, l_q = 8'd0, n_a = 8'd0, n_b = 8'd0, n_q = 8'd0;
    reg [15:0] o_a = 16'd0, o_b = 16'd0, o_q = 16'd0;
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
    assign model = model_r[0];
    assign joy1_mode = j1_r[2:0];
    assign joy2_mode = j2_r[2:0];
    assign win_page = page_r;
    assign snap_go_tgl = snap_t;
    assign snap_dir = dir_r;
    assign snap_7ffd = p7_r;
    assign snap_border = bord_r[2:0];
    assign snap_ay = ay_r;
    assign snap_aysel = aysel_r[3:0];
    assign tape_play = tctrl_r[0];
    assign tape_base = tbase_r;
    assign tape_seek_pos = tpos_r[24:0];
    assign tape_seek_tgl = tseek_t;
    assign inject = inj_r;
    assign fb_base = fbb_r;
    assign fb_stride = fbs_r;
    assign ack = ack_r;
    assign irq = |(irq_en & cause);
    assign rdata = q_p6 ? px_rdata : q;

    wire [31:0] id = (ID_TRX2 != 0) ? 32'h32585254 : 32'h5053585A;   // bytes "TRX2" / "ZXSP", byte 0 first

    always @(posedge clk) begin
        l_a <= fb_latest; l_b <= l_a; if (l_a == l_b) l_q <= l_b;
        n_a <= fb_seq;    n_b <= n_a; if (n_a == n_b) n_q <= n_b;
        o_a <= fb_over;   o_b <= o_a; if (o_a == o_b) o_q <= o_b;
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
                12'h0A2: j1_r <= wdata;
                12'h0A3: j2_r <= wdata;
                12'h0A5: snap_t <= ~snap_t;
                12'h0C4: fbb_r[7:0] <= wdata;
                12'h0C5: fbb_r[15:8] <= wdata;
                12'h0C6: fbb_r[23:16] <= wdata;
                12'h0C7: fbb_r[31:24] <= wdata;
                12'h0C8: fbs_r[7:0] <= wdata;
                12'h0C9: fbs_r[15:8] <= wdata;
                12'h0CA: fbs_r[23:16] <= wdata;
                12'h0CB: fbs_r[31:24] <= wdata;
                12'h0D4: page_r <= wdata[2:0];
                12'h11B: p7_r <= wdata;
                12'h11C: bord_r <= wdata;
                12'h130: aysel_r <= wdata;
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
                default: begin
                    if (off >= 12'h100 && off < 12'h11B) dir_r[8 * (off - 12'h100) +: 8] <= wdata;
                    if (off >= 12'h120 && off < 12'h130) ay_r[8 * (off - 12'h120) +: 8] <= wdata;
                    if (off >= 12'h160 && off < 12'h165) inj_r[8 * (off - 12'h160) +: 8] <= wdata;
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
                12'h034: q <= 8'd0;
                12'h035: q <= 8'h18;          // 280 lines in the frame buffer
                12'h036: q <= 8'h01;
                12'h037: q <= 8'h58;          // 344 picture pixels per line
                12'h038: q <= 8'h01;
                12'h0A0: q <= ctrl_r;
                12'h0A1: q <= model_r;
                12'h0A2: q <= j1_r;
                12'h0A3: q <= j2_r;
                12'h0A4: q <= status;
                12'h0A8: q <= frames[7:0];
                12'h0A9: q <= frames[15:8];
                12'h0AC: q <= cpuaddr[7:0];
                12'h0AD: q <= cpuaddr[15:8];
                12'h0C0: q <= l_q;
                12'h0C1: q <= n_q;
                12'h0C2: q <= o_q[7:0];
                12'h0C3: q <= o_q[15:8];
                12'h0C4: q <= fbb_r[7:0];
                12'h0C5: q <= fbb_r[15:8];
                12'h0C6: q <= fbb_r[23:16];
                12'h0C7: q <= fbb_r[31:24];
                12'h0C8: q <= fbs_r[7:0];
                12'h0C9: q <= fbs_r[15:8];
                12'h0CA: q <= fbs_r[23:16];
                12'h0CB: q <= fbs_r[31:24];
                12'h0D4: q <= {5'd0, page_r};
                12'h11B: q <= p7_r;
                12'h11C: q <= bord_r;
                12'h130: q <= aysel_r;
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
                default: begin
                    if (off >= 12'h100 && off < 12'h11B) q <= dir_r[8 * (off - 12'h100) +: 8];
                    if (off >= 12'h120 && off < 12'h130) q <= ay_r[8 * (off - 12'h120) +: 8];
                    if (off >= 12'h160 && off < 12'h165) q <= inj_r[8 * (off - 12'h160) +: 8];
                end
            endcase
        end
    end
endmodule
