// T85: the Amiga picture into a frame buffer in the DDR2 for the host's pixel layer (RGB565, 720 x 540 source
// pixels, scaled 2x by the layer to 1440 x 1080). Written for this project.
//
// Input: the raw 15 kHz stream of Denise (12 bit RGB per 28 MHz tick, hblank, vblank, lace, field1); every
// register of this module moves only on ce28 like the chipset. A hires pixel is two ticks; a lores pixel
// is the same colour for two hires pixels, so the 720 columns are hires pixels. The window starts SKIP_X hires
// pixels after hblank ends and SKIP_Y lines after vblank ends and is 720 x 270 lines; every line is written
// to two rows (the layer scales both axes alike, so the lines are doubled here); in an interlaced screen each
// field is shown alone, the odd field one row lower ("bob").
// Pixels are packed little endian, 8 pixels to a 128-bit word, 90 words to a row. A line is assembled in a
// line buffer (two halves) in sys_clk and written to the DDR2 in ui_clk through the e_ port.
// The application owns the choice of the buffer to write and the flip (amiga_regs); wb_idx is the buffer of
// the frame that starts next.
`timescale 1ns / 1ps
module amiga_fb #(
    parameter [26:0] FB_BASE_W = 27'h0E80000,       // word address of buffer 0 (DDR2 byte 0x1D00000)
    parameter [26:0] BUF_STRIDE_W = 27'h80000,      // words between buffers (byte 0x100000)
    parameter integer SKIP_X = 36,
    parameter integer SKIP_Y = 10,
    parameter integer COLS = 720,
    parameter integer LINES = 270
) (
    input  wire        sys_clk,
    input  wire        sys_rst,
    input  wire        ce28,
    input  wire [3:0]  r,
    input  wire [3:0]  g,
    input  wire [3:0]  b,
    input  wire        hblank,
    input  wire        vblank,
    input  wire        lace,
    input  wire        field1,
    input  wire        enable,
    output wire        quiet,                        // sys_clk: no line is on its way to the DDR2 (Freeze)
    input  wire [1:0]  wb_idx,                       // the buffer to write the next frame into (quasi static)
    output reg         frame_start_tgl = 1'b0,
    output reg         frame_done_tgl = 1'b0,
    output reg  [15:0] frames = 16'd0,
    output reg  [15:0] overruns = 16'd0,
    // ui_clk: writes to the e_ port (via amiga_eport client A)
    input  wire        ui_clk,
    input  wire        ui_rst,
    output reg         a_valid = 1'b0,
    output reg  [26:0] a_addr = 27'd0,
    output reg  [127:0] a_data = 128'd0,
    input  wire        a_ready
);
    // ------------------------------------------------------------------ sys_clk side
    reg hb_d = 1'b1, vb_d = 1'b1;
    reg        phase = 1'b0;                          // 0: first tick of a hires pixel
    reg [11:0] xcnt = 12'd0;                          // hires pixels since hblank ended
    reg [9:0]  ly = 10'h3FF;                          // line index since vblank ended (-1 before the first line)
    reg        in_frame = 1'b0;
    reg        cap = 1'b0;                            // this line is in the window
    reg        tail = 1'b0;                           // the line ended inside a word: it is being padded
    reg [9:0]  cap_ly = 10'd0;
    reg [1:0]  fwb = 2'd0;                            // buffer of this frame
    reg [127:0] acc = 128'd0;
    reg [2:0]  pk = 3'd0;
    reg [6:0]  grp = 7'd0;
    reg        half = 1'b0;                           // line buffer half being written
    reg [127:0] lb [0:255];
    reg        ln_tgl = 1'b0;
    reg [9:0]  ln_row = 10'd0;
    reg [1:0]  ln_wb = 2'd0;
    reg        ln_half = 1'b0;
    wire       ack_tgl;                               // from the ui side
    (* ASYNC_REG = "TRUE" *) reg [1:0] ack_s = 2'b00;
    reg        waiting_last = 1'b0;

    assign quiet = (ack_s[1] == ln_tgl);              // the last line handed over is written (acknowledged)

    wire [15:0] rgb565 = {r, r[3], g, g[3:2], b, b[3]};
    wire        ls = hb_d & ~hblank;                  // line start (this tick ends the blanking)
    wire        le = ~hb_d & hblank;                  // line end
    wire [9:0]  ly_new = (ly + 10'd1);
    wire        win_new = in_frame & ~vblank & (ly_new >= SKIP_Y) & (ly_new < SKIP_Y + LINES);
    wire        in_col = (xcnt >= SKIP_X) && (xcnt < SKIP_X + COLS);

    always @(posedge sys_clk) begin
        ack_s <= {ack_s[0], ack_tgl};
        if (ce28) begin
            hb_d <= hblank; vb_d <= vblank;
            if (vb_d && !vblank) begin                // frame start
                in_frame <= enable; ly <= 10'h3FF; fwb <= wb_idx; cap <= 1'b0; tail <= 1'b0;
                frame_start_tgl <= ~frame_start_tgl;
                waiting_last <= 1'b0;
            end else if (!vb_d && vblank) begin       // frame end: the last line may still be on its way
                if (in_frame) waiting_last <= 1'b1;
                in_frame <= 1'b0; cap <= 1'b0;
            end
            if (ls) begin
                xcnt <= 12'd0; phase <= 1'b0; pk <= 3'd0; grp <= 7'd0;
                if (!vblank && !(vb_d && !vblank)) begin
                    ly <= ly_new; cap <= win_new; cap_ly <= ly_new;
                end
            end else if (hblank && tail) begin        // the line ended inside a word: finish it with the last pixel repeated
                acc <= {acc[127:112], acc[127:16]};
                pk <= pk + 3'd1;
                if (pk == 3'd7) begin
                    lb[{half, grp}] <= {acc[127:112], acc[127:16]};
                    tail <= 1'b0;
                end
            end else if (!hblank) begin
                phase <= ~phase;
                if (phase) xcnt <= xcnt + 12'd1;
                if (phase && cap && in_col) begin
                    acc <= {rgb565, acc[127:16]};
                    pk <= pk + 3'd1;
                    if (pk == 3'd7) begin
                        lb[{half, grp}] <= {rgb565, acc[127:16]};
                        grp <= grp + 7'd1;
                    end
                end
            end
            if (le && cap) begin
                cap <= 1'b0;
                if (pk != 3'd0) tail <= 1'b1;         // the window is wider than the line (the Minimig's line holds 753 hires
            end                                       // pixels after its blanking, the window needs 756): never leave a word unwritten
            if ((le && cap && pk == 3'd0) || (tail && hblank && pk == 3'd7)) begin   // line complete: hand it to the ui side
                if (ack_s[1] != ln_tgl) overruns <= overruns + 16'd1;
                ln_tgl <= ~ln_tgl;
                ln_row <= {cap_ly - SKIP_Y[9:0], 1'b0} + ((lace && field1) ? 10'd1 : 10'd0);
                ln_wb <= fwb; ln_half <= half;
                half <= ~half;
            end
        end
        // frame done: all lines written (acknowledged by the ui side)
        if (waiting_last && ack_s[1] == ln_tgl) begin
            waiting_last <= 1'b0;
            frame_done_tgl <= ~frame_done_tgl;
            frames <= frames + 16'd1;
        end
        if (sys_rst) begin in_frame <= 1'b0; waiting_last <= 1'b0; cap <= 1'b0; tail <= 1'b0; end
    end

    // ------------------------------------------------------------------ ui_clk side
    (* ASYNC_REG = "TRUE" *) reg [2:0] ln_s = 3'b000;
    reg        ack_r = 1'b0;
    assign ack_tgl = ack_r;
    reg [127:0] lb_q = 128'd0;
    reg [7:0]  rd_a = 8'd0;
    reg [2:0]  us = 3'd0;
    reg [6:0]  ug = 7'd0;
    reg        rep = 1'b0;
    reg [9:0]  u_row = 10'd0;
    reg [1:0]  u_wb = 2'd0;
    reg        u_half = 1'b0;
    reg [26:0] row_base = 27'd0;
    localparam U_IDLE = 3'd0, U_RD = 3'd1, U_W1 = 3'd2, U_W2 = 3'd3, U_PUT = 3'd4, U_NEXT = 3'd5;
    wire [9:0]  cur_row = u_row + {9'd0, rep};
    wire [26:0] buf_w = FB_BASE_W + (u_wb == 2'd0 ? 27'd0 : (u_wb == 2'd1 ? BUF_STRIDE_W :
                        (u_wb == 2'd2 ? {BUF_STRIDE_W[25:0], 1'b0} : {BUF_STRIDE_W[25:0], 1'b0} + BUF_STRIDE_W)));
    // 720 words per row: 512 + 128 + 64 + 16
    wire [26:0] row_w = ({17'd0, cur_row} << 9) + ({17'd0, cur_row} << 7) + ({17'd0, cur_row} << 6) + ({17'd0, cur_row} << 4);

    always @(posedge ui_clk) begin
        ln_s <= {ln_s[1:0], ln_tgl};
        lb_q <= lb[rd_a];
        if (ui_rst) begin
            us <= U_IDLE; a_valid <= 1'b0;
        end else case (us)
        U_IDLE: if (ln_s[2] != ln_s[1]) begin
            u_row <= ln_row; u_wb <= ln_wb; u_half <= ln_half; rep <= 1'b0; ug <= 7'd0;
            us <= U_RD;
        end
        U_RD: begin rd_a <= {u_half, ug}; us <= U_W1; end
        U_W1: us <= U_W2;
        U_W2: begin                                   // lb_q holds the word
            if (cur_row < 10'd540) begin
                a_data <= lb_q; a_addr <= buf_w + row_w + {17'd0, ug, 3'd0}; a_valid <= 1'b1; us <= U_PUT;
            end else us <= U_NEXT;
        end
        U_PUT: if (a_ready) begin a_valid <= 1'b0; us <= U_NEXT; end
        U_NEXT: begin
            if (ug == 7'd89) begin
                ug <= 7'd0;
                if (!rep) begin rep <= 1'b1; us <= U_RD; end
                else begin ack_r <= ln_s[2]; us <= U_IDLE; end
            end else begin ug <= ug + 7'd1; us <= U_RD; end
        end
        default: us <= U_IDLE;
        endcase
    end
endmodule
