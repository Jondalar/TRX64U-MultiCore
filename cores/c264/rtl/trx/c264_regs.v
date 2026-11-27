// T90: the register block the app sees at its core window (cw_*, cpu_clk, the bytes at 0x101A0000; the same window trx_io.v
// answers for the C64 core), laid out like vc20_regs.vhd. Written for this project (TREX), no upstream code.
//   000-003 ID        "C264" (a build with ID_TRX2 = 1 answers "TRX2")
//   004 CAPS 0xC0     b6 page 6 (pixel layer), b7 RGB565 pixel layer: the host's, forwarded below
//   005 CAPS2 0x02    b1 BUILD is there
//   008-00B BUILD     the bitstream's build stamp (USR_ACCESS)
//   010 IRQ_EN, 014 IRQ_CAUSE (W1C)   b7 page flip done (px_ev_flip), b6 video frame start (px_ev_vsync), b5 a TED frame is complete
//   020-027 DDR_CAL   the host's DDR2 calibration, as for the C64 core
//   0A0 CTRL     RW   b0 reset hold, b1 halt
//   0A1 MODEL    RW   b0 0 = C16 / C116, 1 = Plus/4 (the ROM bank register at $FDD0-$FDDF is live only in the Plus/4 model)
//   0A2 RAM_SIZE RW   b0 0 = 16 KB mirrored (C16, C116), 1 = 64 KB; MODEL and RAM_SIZE are taken at the core's next reset
//   0A3 JOY_SWAP RW   b0 swaps the host's joystick ports
//   0A4 STATUS   R    b0 machine in reset, b1 held (Freeze), b2 the TED's PAL flag
//   0A8-0A9 FRAMES    R   TED frame counter (low 16 bits)
//   0AC-0AD CPUADDR   R   the CPU's address bus (a sample, read twice to be sure)
//   0B0 ROM_SEL  RW   b2:0 the array behind the app's 16 KB ROM aperture: 0 KERNAL, 1 BASIC, 2 function low, 3 function high,
//                     4 cartridge low, 5 cartridge high
//   0B1 CART     RW   b0 a cartridge low image is loaded, b1 a cartridge high image is loaded (the machine sees them at $FDD0 bank 1)
//   0B2 TAPE_PIN RW   b0 MOTOR pin driven, b1 WRITE pin driven, b4 the datasette's READ and SENSE pins are the machine's inputs (reset 0 = every pin released), as PET_TAPE_PIN
//   0C0 FB_LATEST R   b7 valid, b1:0 the buffer of the newest complete frame
//   0C1 FRAME_SEQ R   8 bit counter that changes with every complete frame
//   0C2-0C3 FB_OVERRUNS R   lines that could not be handed to the DDR2 side in time
//   0C4-0C7 FB_BASE   RW  DDR2 byte address of frame buffer 0 (default 0x01D00000: the REU block of the host, as the Amiga core's)
//   0C8-0CB FB_STRIDE RW  bytes between the four frame buffers (default 0x00100000)
//   page 6 (0x6000-0x6FFF)   forwarded to the host's pixel layer (px_we, px_re, px_rdata)
//   everything else reads 0 and is acknowledged; the EEPROM window (addr[16]) is not here.
`timescale 1ns / 1ps
module c264_regs #(
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
    input  wire        ev_frame,                    // a TED frame is complete (any clock rate: a toggle seen from sys_clk is synchronised by the caller)
    // the core's side (the values cross from and to sys_clk through the caller's synchronisers)
    output wire [7:0]  ctrl,
    output wire        model,
    output wire        ram64,
    output wire        joy_swap,
    output wire [2:0]  rom_sel,
    output wire [1:0]  cart,
    output wire [7:0]  tape_pin,
    output wire [31:0] fb_base,
    output wire [31:0] fb_stride,
    input  wire [7:0]  status,
    input  wire [15:0] frames,
    input  wire [15:0] cpuaddr,
    input  wire [7:0]  fb_latest,
    input  wire [7:0]  fb_seq,
    input  wire [15:0] fb_over
);
    // >>> generated from cores/c264/app/gateware.toml (T264 S2): do not edit, `tools/toml_a_gen.py gen` rewrites it
    localparam [11:0] R_IDENT_ID = 12'h000;
    localparam [11:0] R_IDENT_CAPS = 12'h004;
    localparam [11:0] R_IDENT_CAPS2 = 12'h005;
    localparam [11:0] R_IDENT_BUILD = 12'h008;
    localparam [11:0] R_C264_IRQ_IRQ_EN = 12'h010;
    localparam [11:0] R_C264_IRQ_IRQ_CAUSE = 12'h014;
    localparam [11:0] R_C264_DDR_CAL_DDR_CAL0 = 12'h020;
    localparam [11:0] R_C264_DDR_CAL_DDR_CAL1 = 12'h024;
    localparam [11:0] R_C264_MACHINE_CTRL = 12'h0A0;
    localparam [11:0] R_C264_MACHINE_MODEL = 12'h0A1;
    localparam [11:0] R_C264_MACHINE_RAM_SIZE = 12'h0A2;
    localparam [11:0] R_C264_MACHINE_JOY_SWAP = 12'h0A3;
    localparam [11:0] R_C264_MACHINE_STATUS = 12'h0A4;
    localparam [11:0] R_C264_MACHINE_FRAMES = 12'h0A8;
    localparam [11:0] R_C264_MACHINE_CPUADDR = 12'h0AC;
    localparam [11:0] R_C264_MEDIA_ROM_SEL = 12'h0B0;
    localparam [11:0] R_C264_MEDIA_CART = 12'h0B1;
    localparam [11:0] R_C264_MEDIA_TAPE_PIN = 12'h0B2;
    localparam [11:0] R_C264_VIDEO_FB_LATEST = 12'h0C0;
    localparam [11:0] R_C264_VIDEO_FRAME_SEQ = 12'h0C1;
    localparam [11:0] R_C264_VIDEO_FB_OVERRUNS = 12'h0C2;
    localparam [11:0] R_C264_VIDEO_FB_BASE = 12'h0C4;
    localparam [11:0] R_C264_VIDEO_FB_STRIDE = 12'h0C8;
    // <<< generated
    reg [7:0]  ctrl_r = 8'h00;
    reg        model_r = 1'b0, ram64_r = 1'b0, swap_r = 1'b0;
    reg [2:0]  sel_r = 3'd0;
    reg [1:0]  cart_r = 2'd0;
    reg [7:0]  tpin_r = 8'd0;
    reg [31:0] base_r = 32'h01D00000;
    reg [31:0] stride_r = 32'h00100000;
    reg [7:0]  irq_en = 8'd0, cause = 8'd0;
    reg [7:0]  q = 8'd0;
    reg        q_p6 = 1'b0, ack_r = 1'b0;
    // the sys_clk values, accepted when two samples in a row agree
    reg [15:0] f_a = 16'd0, f_b = 16'd0, f_q = 16'd0;
    reg [15:0] c_a = 16'd0, c_b = 16'd0, c_q = 16'd0;
    reg [7:0]  s_a = 8'd0, s_b = 8'd0, s_q = 8'd0;
    reg [7:0]  l_a = 8'd0, l_b = 8'd0, l_q = 8'd0;
    reg [7:0]  n_a = 8'd0, n_b = 8'd0, n_q = 8'd0;
    reg [15:0] o_a = 16'd0, o_b = 16'd0, o_q = 16'd0;
    wire       win  = addr[16];
    wire [3:0] page = addr[15:12];
    wire [11:0] off = addr[11:0];
    wire [7:0] ev = {ev_flip, ev_vsync, ev_frame, 5'b00000};

    assign px_we = we & ~win & (page == 4'h6);
    assign px_re = re & ~win & (page == 4'h6);
    assign ctrl = ctrl_r;
    assign model = model_r;
    assign ram64 = ram64_r;
    assign joy_swap = swap_r;
    assign rom_sel = sel_r;
    assign cart = cart_r;
    assign tape_pin = tpin_r;
    assign fb_base = base_r;
    assign fb_stride = stride_r;
    assign ack = ack_r;
    assign irq = |(irq_en & cause);
    assign rdata = q_p6 ? px_rdata : q;

    wire [31:0] id = (ID_TRX2 != 0) ? 32'h32585254 : 32'h34363243;   // bytes "TRX2" / "C264", byte 0 first

    always @(posedge clk) begin
        f_a <= frames;   f_b <= f_a;   if (f_a == f_b) f_q <= f_b;
        c_a <= cpuaddr;  c_b <= c_a;   if (c_a == c_b) c_q <= c_b;
        s_a <= status;   s_b <= s_a;   if (s_a == s_b) s_q <= s_b;
        l_a <= fb_latest; l_b <= l_a;  if (l_a == l_b) l_q <= l_b;
        n_a <= fb_seq;   n_b <= n_a;   if (n_a == n_b) n_q <= n_b;
        o_a <= fb_over;  o_b <= o_a;   if (o_a == o_b) o_q <= o_b;
        cause <= cause | ev;
        ack_r <= 1'b0;
        if (re | we) ack_r <= 1'b1;
        q_p6 <= 1'b0;
        if (re && !win && page == 4'h6) q_p6 <= 1'b1;

        if (we && !win && page == 4'h0) begin
            case (off)
                R_C264_IRQ_IRQ_EN: irq_en <= wdata;
                R_C264_IRQ_IRQ_CAUSE: cause <= (cause & ~wdata) | ev;
                R_C264_MACHINE_CTRL: ctrl_r <= wdata;
                R_C264_MACHINE_MODEL: model_r <= wdata[0];
                R_C264_MACHINE_RAM_SIZE: ram64_r <= wdata[0];
                R_C264_MACHINE_JOY_SWAP: swap_r <= wdata[0];
                R_C264_MEDIA_ROM_SEL: sel_r <= wdata[2:0];
                R_C264_MEDIA_CART: cart_r <= wdata[1:0];
                R_C264_MEDIA_TAPE_PIN: tpin_r <= wdata;
                R_C264_VIDEO_FB_BASE: base_r[7:0] <= wdata;
                R_C264_VIDEO_FB_BASE + 12'd1: base_r[15:8] <= wdata;
                R_C264_VIDEO_FB_BASE + 12'd2: base_r[23:16] <= wdata;
                R_C264_VIDEO_FB_BASE + 12'd3: base_r[31:24] <= wdata;
                R_C264_VIDEO_FB_STRIDE: stride_r[7:0] <= wdata;
                R_C264_VIDEO_FB_STRIDE + 12'd1: stride_r[15:8] <= wdata;
                R_C264_VIDEO_FB_STRIDE + 12'd2: stride_r[23:16] <= wdata;
                R_C264_VIDEO_FB_STRIDE + 12'd3: stride_r[31:24] <= wdata;
                default: ;
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
                R_C264_IRQ_IRQ_EN: q <= irq_en;
                R_C264_IRQ_IRQ_CAUSE: q <= cause;
                R_C264_DDR_CAL_DDR_CAL0: q <= ddr_cal[7:0];
                R_C264_DDR_CAL_DDR_CAL0 + 12'd1: q <= ddr_cal[15:8];
                R_C264_DDR_CAL_DDR_CAL0 + 12'd2: q <= ddr_cal[23:16];
                R_C264_DDR_CAL_DDR_CAL0 + 12'd3: q <= ddr_cal[31:24];
                R_C264_DDR_CAL_DDR_CAL1: q <= ddr_cal[39:32];
                R_C264_DDR_CAL_DDR_CAL1 + 12'd1: q <= ddr_cal[47:40];
                R_C264_DDR_CAL_DDR_CAL1 + 12'd2: q <= ddr_cal[55:48];
                R_C264_DDR_CAL_DDR_CAL1 + 12'd3: q <= ddr_cal[63:56];
                R_C264_MACHINE_CTRL: q <= ctrl_r;
                R_C264_MACHINE_MODEL: q <= {7'd0, model_r};
                R_C264_MACHINE_RAM_SIZE: q <= {7'd0, ram64_r};
                R_C264_MACHINE_JOY_SWAP: q <= {7'd0, swap_r};
                R_C264_MACHINE_STATUS: q <= s_q;
                R_C264_MACHINE_FRAMES: q <= f_q[7:0];
                R_C264_MACHINE_FRAMES + 12'd1: q <= f_q[15:8];
                R_C264_MACHINE_CPUADDR: q <= c_q[7:0];
                R_C264_MACHINE_CPUADDR + 12'd1: q <= c_q[15:8];
                R_C264_MEDIA_ROM_SEL: q <= {5'd0, sel_r};
                R_C264_MEDIA_CART: q <= {6'd0, cart_r};
                R_C264_MEDIA_TAPE_PIN: q <= tpin_r;
                R_C264_VIDEO_FB_LATEST: q <= l_q;
                R_C264_VIDEO_FRAME_SEQ: q <= n_q;
                R_C264_VIDEO_FB_OVERRUNS: q <= o_q[7:0];
                R_C264_VIDEO_FB_OVERRUNS + 12'd1: q <= o_q[15:8];
                R_C264_VIDEO_FB_BASE: q <= base_r[7:0];
                R_C264_VIDEO_FB_BASE + 12'd1: q <= base_r[15:8];
                R_C264_VIDEO_FB_BASE + 12'd2: q <= base_r[23:16];
                R_C264_VIDEO_FB_BASE + 12'd3: q <= base_r[31:24];
                R_C264_VIDEO_FB_STRIDE: q <= stride_r[7:0];
                R_C264_VIDEO_FB_STRIDE + 12'd1: q <= stride_r[15:8];
                R_C264_VIDEO_FB_STRIDE + 12'd2: q <= stride_r[23:16];
                R_C264_VIDEO_FB_STRIDE + 12'd3: q <= stride_r[31:24];
                default: ;
            endcase
        end
    end
endmodule
