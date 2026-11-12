// T168: the register block of the two DDT cores (ddt_mandel, ddt_novic), the part of the TRX IO block (trx64/trx_io.v, the bytes at 0x101A0000 in the
// app's window cw_*) that the generic core application uses. Written for this project (TREX), no upstream code; the layout is zx_regs.v's and null_regs.v's.
//   000-003 ID        the core's ID (parameter ID; "DDTM" / "DDTN"), byte 0 first
//   004 CAPS 0xC0     b6 page 6 (pixel layer), b7 RGB565 pixel layer: the host's, forwarded below
//   005 CAPS2 0x02    b1 BUILD is there
//   006 CONTRACT      the layer-2 contract this core was written for (2)
//   008-00B BUILD     the bitstream's build stamp (USR_ACCESS)
//   010 IRQ_EN, 014 IRQ_CAUSE (W1C)   b7 page flip done (px_ev_flip), b6 video frame start (px_ev_vsync)
//   020-027 DDR_CAL   the host's DDR2 calibration
//   035-036 GEOM_LINES  lines of the frame buffer (16 bit)    037-038 PIX_W pixels per line of it (16 bit)
//   0A0 CTRL     RW   b0 reset hold, b1 halt (Freeze)
//   0A4 STATUS   R    b0 in reset, b1 held (Freeze); the core's own bits above (b2 busy: a render is on its way)
//   0A8-0AB INFO R    the core's own 32 bit word (ddt_mandel: {render busy, 3'b0, max iterations}; ddt_novic: the frame counter)
//   0C0 FB_LATEST R   b7 valid, b1:0 the buffer of the newest complete frame (T119)
//   0C1 FRAME_SEQ R   8 bit counter that changes with every complete frame
//   0C2-0C3 FB_OVERRUNS R   lines that could not be handed to the DDR2 side in time
//   0C4-0C7 FB_BASE   RW  DDR2 byte address of frame buffer 0 (default 0x01D00000: the REU block of the host, as the C264's, the ZX's)
//   0C8-0CB FB_STRIDE RW  bytes between the four frame buffers (default 0x00100000)
//   page 6 (0x6000-0x6FFF)   forwarded to the host's pixel layer (px_we, px_re, px_rdata)
//   everything else reads 0 and is acknowledged.
`timescale 1ns / 1ps
module ddt_regs #(
    parameter [31:0] ID = 32'h4D544444,         // "DDTM", byte 0 first
    parameter [15:0] GEOM_LINES = 16'd256,
    parameter [15:0] PIX_W = 16'd352
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
    output wire [7:0]  ctrl,
    input  wire [7:0]  status,
    input  wire [31:0] info,
    input  wire [7:0]  fb_latest,                   // from the frame writer's clock: a value counts when two samples in a row agree
    input  wire [7:0]  fb_seq,
    input  wire [15:0] fb_over,
    output wire [31:0] fb_base,
    output wire [31:0] fb_stride
);
    reg [7:0]  ctrl_r = 8'h00;
    reg [31:0] fbb_r = 32'h01D00000, fbs_r = 32'h00100000;
    reg [7:0]  l_a = 8'd0, l_b = 8'd0, l_q = 8'd0, n_a = 8'd0, n_b = 8'd0, n_q = 8'd0;
    reg [15:0] o_a = 16'd0, o_b = 16'd0, o_q = 16'd0;
    reg [31:0] i_a = 32'd0, i_b = 32'd0, i_q = 32'd0;
    reg [7:0]  s_a = 8'd0, s_b = 8'd0, s_q = 8'd0;
    reg [7:0]  irq_en = 8'd0, cause = 8'd0;
    reg [7:0]  q = 8'd0;
    reg        q_p6 = 1'b0, ack_r = 1'b0;
    wire       win  = addr[16];
    wire [3:0] page = addr[15:12];
    wire [11:0] off = addr[11:0];
    wire [7:0] ev = {ev_flip, ev_vsync, 6'b000000};

    assign px_we = we & ~win & (page == 4'h6);
    assign px_re = re & ~win & (page == 4'h6);
    assign ctrl = ctrl_r;
    assign fb_base = fbb_r;
    assign fb_stride = fbs_r;
    assign ack = ack_r;
    assign irq = |(irq_en & cause);
    assign rdata = q_p6 ? px_rdata : q;

    always @(posedge clk) begin
        l_a <= fb_latest; l_b <= l_a; if (l_a == l_b) l_q <= l_b;
        n_a <= fb_seq;    n_b <= n_a; if (n_a == n_b) n_q <= n_b;
        o_a <= fb_over;   o_b <= o_a; if (o_a == o_b) o_q <= o_b;
        i_a <= info;      i_b <= i_a; if (i_a == i_b) i_q <= i_b;
        s_a <= status;    s_b <= s_a; if (s_a == s_b) s_q <= s_b;
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
                12'h0C4: fbb_r[7:0] <= wdata;
                12'h0C5: fbb_r[15:8] <= wdata;
                12'h0C6: fbb_r[23:16] <= wdata;
                12'h0C7: fbb_r[31:24] <= wdata;
                12'h0C8: fbs_r[7:0] <= wdata;
                12'h0C9: fbs_r[15:8] <= wdata;
                12'h0CA: fbs_r[23:16] <= wdata;
                12'h0CB: fbs_r[31:24] <= wdata;
                default: ;
            endcase
        end

        q <= 8'h00;
        if (!win && page == 4'h0) begin
            case (off)
                12'h000: q <= ID[7:0];
                12'h001: q <= ID[15:8];
                12'h002: q <= ID[23:16];
                12'h003: q <= ID[31:24];
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
                12'h035: q <= GEOM_LINES[7:0];
                12'h036: q <= GEOM_LINES[15:8];
                12'h037: q <= PIX_W[7:0];
                12'h038: q <= PIX_W[15:8];
                12'h0A0: q <= ctrl_r;
                12'h0A4: q <= s_q;
                12'h0A8: q <= i_q[7:0];
                12'h0A9: q <= i_q[15:8];
                12'h0AA: q <= i_q[23:16];
                12'h0AB: q <= i_q[31:24];
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
                default: ;
            endcase
        end
    end
endmodule
