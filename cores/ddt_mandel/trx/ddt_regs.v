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
    // >>> generated from cores/ddt_novic/app/gateware.toml (T264 S2): do not edit, `tools/toml_a_gen.py gen` rewrites it
    localparam [11:0] R_IDENT_ID = 12'h000;
    localparam [11:0] R_IDENT_CAPS = 12'h004;
    localparam [11:0] R_IDENT_CAPS2 = 12'h005;
    localparam [11:0] R_IDENT_CONTRACT = 12'h006;
    localparam [11:0] R_IDENT_BUILD = 12'h008;
    localparam [11:0] R_IRQ_IRQ_EN = 12'h010;
    localparam [11:0] R_IRQ_IRQ_CAUSE = 12'h014;
    localparam [11:0] R_DDR_CAL_DDR_CAL0 = 12'h020;
    localparam [11:0] R_DDR_CAL_DDR_CAL1 = 12'h024;
    localparam [11:0] R_GEOM_LINES = 12'h035;
    localparam [11:0] R_GEOM_PIX_W = 12'h037;
    localparam [11:0] R_MACHINE_CTRL = 12'h0A0;
    localparam [11:0] R_MACHINE_STATUS = 12'h0A4;
    localparam [11:0] R_MACHINE_FRAMES = 12'h0A8;
    localparam [11:0] R_VIDEO_FB_LATEST = 12'h0C0;
    localparam [11:0] R_VIDEO_FRAME_SEQ = 12'h0C1;
    localparam [11:0] R_VIDEO_FB_OVERRUNS = 12'h0C2;
    localparam [11:0] R_VIDEO_FB_BASE = 12'h0C4;
    localparam [11:0] R_VIDEO_FB_STRIDE = 12'h0C8;
    // <<< generated
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
                R_IRQ_IRQ_EN: irq_en <= wdata;
                R_IRQ_IRQ_CAUSE: cause <= (cause & ~wdata) | ev;
                R_MACHINE_CTRL: ctrl_r <= wdata;
                R_VIDEO_FB_BASE: fbb_r[7:0] <= wdata;
                R_VIDEO_FB_BASE + 12'd1: fbb_r[15:8] <= wdata;
                R_VIDEO_FB_BASE + 12'd2: fbb_r[23:16] <= wdata;
                R_VIDEO_FB_BASE + 12'd3: fbb_r[31:24] <= wdata;
                R_VIDEO_FB_STRIDE: fbs_r[7:0] <= wdata;
                R_VIDEO_FB_STRIDE + 12'd1: fbs_r[15:8] <= wdata;
                R_VIDEO_FB_STRIDE + 12'd2: fbs_r[23:16] <= wdata;
                R_VIDEO_FB_STRIDE + 12'd3: fbs_r[31:24] <= wdata;
                default: ;
            endcase
        end

        q <= 8'h00;
        if (!win && page == 4'h0) begin
            case (off)
                R_IDENT_ID: q <= ID[7:0];
                R_IDENT_ID + 12'd1: q <= ID[15:8];
                R_IDENT_ID + 12'd2: q <= ID[23:16];
                R_IDENT_ID + 12'd3: q <= ID[31:24];
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
                R_GEOM_LINES: q <= GEOM_LINES[7:0];
                R_GEOM_LINES + 12'd1: q <= GEOM_LINES[15:8];
                R_GEOM_PIX_W: q <= PIX_W[7:0];
                R_GEOM_PIX_W + 12'd1: q <= PIX_W[15:8];
                R_MACHINE_CTRL: q <= ctrl_r;
                R_MACHINE_STATUS: q <= s_q;
                R_MACHINE_FRAMES: q <= i_q[7:0];
                R_MACHINE_FRAMES + 12'd1: q <= i_q[15:8];
                R_MACHINE_FRAMES + 12'd2: q <= i_q[23:16];
                R_MACHINE_FRAMES + 12'd3: q <= i_q[31:24];
                R_VIDEO_FB_LATEST: q <= l_q;
                R_VIDEO_FRAME_SEQ: q <= n_q;
                R_VIDEO_FB_OVERRUNS: q <= o_q[7:0];
                R_VIDEO_FB_OVERRUNS + 12'd1: q <= o_q[15:8];
                R_VIDEO_FB_BASE: q <= fbb_r[7:0];
                R_VIDEO_FB_BASE + 12'd1: q <= fbb_r[15:8];
                R_VIDEO_FB_BASE + 12'd2: q <= fbb_r[23:16];
                R_VIDEO_FB_BASE + 12'd3: q <= fbb_r[31:24];
                R_VIDEO_FB_STRIDE: q <= fbs_r[7:0];
                R_VIDEO_FB_STRIDE + 12'd1: q <= fbs_r[15:8];
                R_VIDEO_FB_STRIDE + 12'd2: q <= fbs_r[23:16];
                R_VIDEO_FB_STRIDE + 12'd3: q <= fbs_r[31:24];
                default: ;
            endcase
        end
    end
endmodule
