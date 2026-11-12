// T142 D7: the SNES picture into a frame buffer in the DDR2 for the host's pixel layer (pix_layer.v): RGB565, 512 x 448, 1 KiB a row, four
// buffers (the layer shows one, the app flips on the frame-done report, the core writes the next buffer in the ring). The circuit of
// cores/nes/trx/nes_pixw.v (T95: the line buffer, the toggle to the ui_clk side, the e_ port writes) with the SNES's geometry:
//   - the PPU makes 512 pixels a line in every mode: 256 dots of two half dots; snes_core.v hands them over as one strobe per pixel, in
//     low resolution the main pixel twice;
//   - 224 lines are the picture; the 239 line overscan mode (V224_MODE low) loses its first 8 and its last 7 lines;
//   - every line is 2 rows of the buffer: without interlace the same line twice (rows 2y and 2y+1), with interlace the field's row (2y + FIELD), so
//     the two fields weave into one 448 row picture. The buffer advances with every frame without interlace and with the even field with it; the odd
//     field completes the same buffer, so the buffer the layer shows is always a whole picture.
// Everything on the clk side moves only on `px`, one strobe per pixel, so a held machine stops it too. A completed line goes to the ui_clk side by a
// toggle; that side reads the words from the line buffer and writes them through the host's e_ port (MIG command, data with the command, one command
// at a time), once, or twice for the second row of a doubled line.
// Written for this project (TREX), no upstream code.
//
`timescale 1ns / 1ps
module snes_pixw (
    input  wire         sys_clk,                      // the machine clock (cpu_clk)
    input  wire         sys_rst,
    input  wire         px,                           // one strobe per pixel of the stream; hblank, vblank and the mode are valid with it
    input  wire [15:0]  rgb565,
    input  wire         hblank,
    input  wire         vblank,
    input  wire         interlace,
    input  wire         field,
    input  wire         v224,
    output wire         quiet,                        // sys_clk: no line is on its way to the DDR2
    output reg          frame_tgl = 1'b0,             // toggles with every complete picture
    output reg  [7:0]   latest = 8'd0,                // b7 valid, b1:0 the buffer of the newest complete picture
    output reg  [7:0]   seq = 8'd0,
    output reg  [15:0]  overruns = 16'd0,
    // quasi static, from the app (cpu_clk)
    input  wire [31:0]  fb_base,                      // DDR2 byte address of buffer 0
    input  wire [31:0]  fb_stride,                    // bytes between the buffers
    // ui_clk: the host's e_ port
    input  wire         ui_clk,
    input  wire         ui_rst,
    output reg  [26:0]  e_addr = 27'd0,
    output reg  [2:0]   e_cmd = 3'd0,
    output reg          e_en = 1'b0,
    input  wire         e_rdy,
    output reg  [127:0] e_wdf_data = 128'd0,
    output wire [15:0]  e_wdf_mask,
    output wire         e_wdf_wren
);
    localparam integer WORDS = 64;                    // 128 bit words in a row
    localparam integer LINES = 224;
    // ------------------------------------------------------------------ sys_clk side
    wire [15:0] pix = rgb565;
    reg         hb_d = 1'b1, vb_d = 1'b1;
    reg         in_frame = 1'b0;
    reg [9:0]   ly = 10'h3FF;                         // lines since vblank ended
    reg         cap = 1'b0;                           // this line is in the window
    reg [9:0]   cap_row = 10'd0;
    reg         cap_dup = 1'b0, cap_last = 1'b0;
    reg [127:0] acc = 128'd0;
    reg [2:0]   pk = 3'd0;
    reg [6:0]   grp = 7'd0;                           // words of this line so far (64 at the end of it)
    reg         half = 1'b0;
    reg [1:0]   fcnt = 2'd0;                          // buffer of the picture that starts next
    reg [1:0]   fwb = 2'd0;                           // buffer of this picture
    reg         fl_il = 1'b0, fl_field = 1'b0, fl_v224 = 1'b1;   // the picture's mode, taken at the start of the frame
    (* ram_style = "block" *) reg [127:0] lb [0:127];
    reg         ln_tgl = 1'b0;
    reg [9:0]   ln_row = 10'd0;
    reg [1:0]   ln_wb = 2'd0;
    reg         ln_half = 1'b0, ln_dup = 1'b0;
    reg         waiting_last = 1'b0;
    wire        ack_tgl;
    (* ASYNC_REG = "TRUE" *) reg [1:0] ack_s = 2'b00;
    assign quiet = (ack_s[1] == ln_tgl);

    // the next state of one pixel, combinational
    reg         ls, fs, fe, in_n, cap_n, last_word, dup_n, lastl_n;
    reg [9:0]   ly_n, row_n, ly_adj;
    reg [2:0]   pk_n;
    reg [6:0]   grp_n;
    reg         pix_cap;
    always @* begin
        ls = hb_d & ~hblank;
        fs = vb_d & ~vblank;
        fe = ~vb_d & vblank;
        in_n = (in_frame | fs) & ~fe;
        ly_n = ly; cap_n = cap; pk_n = pk; grp_n = grp; row_n = cap_row; dup_n = cap_dup; lastl_n = cap_last;
        last_word = 1'b0;
        pix_cap = 1'b0;
        ly_adj = 10'd0;
        if (fs) begin ly_n = 10'h3FF; cap_n = 1'b0; end
        if (fe) cap_n = 1'b0;
        if (ls) begin
            pk_n = 3'd0; grp_n = 7'd0;
            if (in_n && !vblank) begin
                ly_n = ly_n + 10'd1;
                ly_adj = fl_v224 ? ly_n : ly_n - 10'd8;
                cap_n = (fl_v224 || ly_n >= 10'd8) && (ly_adj < LINES);
                row_n = {ly_adj[8:0], 1'b0} + (fl_il ? {9'd0, fl_field} : 10'd0);
                dup_n = ~fl_il;
                lastl_n = (ly_adj == LINES - 1) && (!fl_il || fl_field);
            end else cap_n = 1'b0;
        end
        if (!hblank && cap_n) begin
            pix_cap = 1'b1;
            pk_n = pk_n + 3'd1;
            if (pk_n == 3'd0) begin                       // the eighth pixel of the word
                grp_n = grp_n + 7'd1;
                if (grp_n == WORDS) last_word = 1'b1;
            end
        end
    end

    always @(posedge sys_clk) begin
        ack_s <= {ack_s[0], ack_tgl};
        if (px) begin
            hb_d <= hblank; vb_d <= vblank;
            if (fs) begin
                fl_il <= interlace; fl_field <= field; fl_v224 <= v224;
                // a new buffer: with every frame, or with the even field of an interlaced picture (the odd field completes the same buffer)
                if (!interlace || !field) begin fwb <= fcnt; fcnt <= fcnt + 2'd1; end
                waiting_last <= 1'b0;
            end
            if (pix_cap) begin
                acc <= {pix, acc[127:16]};
                if (pk_n == 3'd0) lb[{half, grp[5:0]}] <= {pix, acc[127:16]};
            end
            // the position of this pixel is the registers' state, the next one's what they take now
            ly <= ly_n; cap <= cap_n; pk <= pk_n; grp <= grp_n; cap_row <= row_n; cap_dup <= dup_n; cap_last <= lastl_n; in_frame <= in_n;
            if (last_word) begin                            // the line is complete: hand it to the ui side
                if (ack_s[1] != ln_tgl) overruns <= overruns + 16'd1;
                ln_tgl <= ~ln_tgl;
                ln_row <= row_n; ln_wb <= fwb; ln_half <= half; ln_dup <= dup_n;
                half <= ~half;
                cap <= 1'b0;
                if (lastl_n) waiting_last <= 1'b1;
            end
        end
        // picture done: the last line is written (acknowledged by the ui side)
        if (waiting_last && ack_s[1] == ln_tgl) begin
            waiting_last <= 1'b0;
            frame_tgl <= ~frame_tgl;
            latest <= {1'b1, 5'd0, fwb};
            seq <= seq + 8'd1;
        end
        if (sys_rst) begin in_frame <= 1'b0; waiting_last <= 1'b0; cap <= 1'b0; end
    end

    // ------------------------------------------------------------------ ui_clk side
    (* ASYNC_REG = "TRUE" *) reg [2:0] ln_s = 3'b000;
    reg         ack_r = 1'b0;
    assign ack_tgl = ack_r;
    reg [127:0] lb_q = 128'd0;
    reg [6:0]   rd_a = 7'd0;
    reg [3:0]   us = 4'd0;
    reg [5:0]   ug = 6'd0;
    reg [9:0]   u_row = 10'd0;
    reg [1:0]   u_wb = 2'd0;
    reg         u_half = 1'b0, u_dup = 1'b0, u_second = 1'b0;
    // the registers cross from cpu_clk: a value counts when two samples in a row agree
    reg [31:0]  b_a = 32'd0, b_b = 32'd0, b_q = 32'h01900000;
    reg [31:0]  s_a = 32'd0, s_b = 32'd0, s_q = 32'h00080000;
    reg [26:0]  buf_w = 27'd0;
    localparam U_IDLE = 4'd0, U_BASE = 4'd1, U_RD = 4'd2, U_W1 = 4'd3, U_W2 = 4'd4, U_PUT = 4'd5, U_NEXT = 4'd6;
    // 16-bit units: a row is 512
    wire [26:0] row_w = {8'd0, u_row + {9'd0, u_second}, 9'd0};
    wire [26:0] base_w = b_q[27:1];
    wire [26:0] stride_w = s_q[27:1];
    assign e_wdf_mask = 16'h0000;
    assign e_wdf_wren = e_en & (e_cmd == 3'b000);

    always @(posedge ui_clk) begin
        ln_s <= {ln_s[1:0], ln_tgl};
        lb_q <= lb[rd_a];
        b_a <= fb_base;   b_b <= b_a;   if (b_a == b_b) b_q <= b_b;
        s_a <= fb_stride; s_b <= s_a;   if (s_a == s_b) s_q <= s_b;
        if (ui_rst) begin
            us <= U_IDLE; e_en <= 1'b0;
        end else case (us)
        U_IDLE: if (ln_s[2] != ln_s[1]) begin
            u_row <= ln_row; u_wb <= ln_wb; u_half <= ln_half; u_dup <= ln_dup; u_second <= 1'b0; ug <= 6'd0;
            us <= U_BASE;
        end
        U_BASE: begin                                  // buffer base in 16-bit units
            buf_w <= base_w + (u_wb == 2'd0 ? 27'd0 : u_wb == 2'd1 ? stride_w : u_wb == 2'd2 ? {stride_w[25:0], 1'b0} : {stride_w[25:0], 1'b0} + stride_w);
            us <= U_RD;
        end
        U_RD: begin rd_a <= {u_half, ug}; us <= U_W1; end
        U_W1: us <= U_W2;
        U_W2: begin                                    // lb_q holds the word
            e_wdf_data <= lb_q;
            e_addr <= buf_w + row_w + {18'd0, ug, 3'd0};
            e_cmd <= 3'b000;
            e_en <= 1'b1;
            us <= U_PUT;
        end
        U_PUT: if (e_rdy) begin e_en <= 1'b0; us <= U_NEXT; end
        U_NEXT: begin
            if (ug == WORDS - 1) begin
                if (u_dup && !u_second) begin u_second <= 1'b1; ug <= 6'd0; us <= U_RD; end
                else begin ack_r <= ln_s[2]; us <= U_IDLE; end
            end else begin ug <= ug + 6'd1; us <= U_RD; end
        end
        default: us <= U_IDLE;
        endcase
    end
endmodule
