// T90: the TED picture into a frame buffer in the DDR2 for the host's pixel layer (pix_layer.v): RGB565, 384 x 288 source
// pixels, 768 bytes a row, four buffers (the layer shows one, the app flips on the frame-done report, the core writes the
// next buffer in the ring). Written for this project (TREX), no upstream code; the shape follows amiga_fb.v of the T85 core
// (line buffer in sys_clk, written to the DDR2 in ui_clk), the right-edge bug found there is not copied: the line is handed
// over when its last word is stored, not at the end of the blanking.
//
// Everything on the sys_clk side moves only on `dotst`, one strobe per TED dot (every fourth TED clock, at the phase where
// the colour table's output is settled), so the pause (no TED clocks) stops it too. A pixel is one dot; the window is COLS dots
// wide from the X0'th dot after hblank ends and LINES lines from the Y0'th line after vblank ends. Pixels are packed little
// endian, 8 to a 128-bit word (48 words a row, no tail: 384 is a multiple of 8). A completed line goes to the ui_clk side by a
// toggle; that side reads the word from the line buffer and writes it through the host's e_ port (MIG command, data with
// the command, one command at a time).
//
// T113: copy for the C128's VDC: lines doubled. Copy of cores/c264/rtl/trx/c264_pixw.v with these changes only:
//   - the source is the VDC's 640-dot picture (COLS = 640, 200 lines at T113, LINES = 240 since T257; 80 words a row; the dot strobe is the VDC's pixel_ce,
//     hblank/vblank are !active_h/!active_v) and every source line is written twice, to rows 2y and 2y+1 of a
//     640 x 480 buffer (1280 bytes a row, 614,400 bytes a buffer; rows 0..399 for a frame of 200 lines): the pixel layer
//     scales whole lines, the VDC's 200 lines fill the 400-line window of an 80-column screen;
//   - three buffers (the ring 0, 1, 2) instead of four;
//   - the word counter is 7 bits and the line buffer holds 2 x 128 words: the 6 bit counter of the original cannot count the 80
//     words of a 640 pixel row (the c264 instance with COLS = 640 in layer2_c128.v never completes a line).
//
// T113 D11: the interlaced VDC (R8[1:0] = 3) is woven. `il` and `field` come from the VDC (vdc_top `ilace`) and are taken at the
// start of every frame. A field of an interlaced frame has up to LINES_IL = 240 lines and goes to rows 2y + field of the 640 x 480 buffer
// (one write per line, no doubling); a frame that is not interlaced was LINES = 200 lines doubled to rows 2y and 2y+1 of the same buffer
// (rows 0..399); T257 below makes it LINES = 240. The two fields of one picture go to the SAME buffer (field 0 takes the next buffer of the ring, field 1 the one of
// field 0) and the picture is reported when field 1 is in; a field 1 without its field 0 before it is written but not reported.
// A frame that has fewer lines than the window is reported at the end of its active area (it was never reported before, the line
// counter was the only trigger). `latest` b2 says that all 480 rows of the reported picture were written (for a woven pair: both fields reached line 240):
// the application cuts the layer to 400 rows when it is clear, so a woven pair with fewer lines (200 a field = 400 rows, or 201..239) never shows
// rows that belong to an older, taller picture; of a woven pair with 201..239 lines the lines below row 400 are cut (a frame that is not interlaced is completed instead, T257). A buffer is 640 x 480 x 2 = 614,400 bytes (the stride of the buffers is the application's: 1 MiB).
//
// T257 (#122): a frame that is not interlaced is taken up to LINES = 240 lines too (the instance says LINES(240), LINES_IL(240)); every source line is
// still written twice, rows 2y and 2y+1, so rows 0..479 are used. A frame with LINES_BASE + 1 .. LINES - 1 lines (201..239) is completed by the writer: the
// rows after its last line are written (a blank job per line, in the colour of the first blanked dot, padc: T257 fault 7, it was black) before the frame is reported, so no row of
// the picture comes from an older frame. `latest` b2 ("480 rows are this picture") is now 1 for such a frame and for one of 240 lines (it was
// only ever 1 for a woven pair); a frame of up to LINES_BASE = 200 lines keeps b2 = 0 and the application cuts the layer to 400 rows as before.
// T257 fault 7 (rfovdc2, board 2026-11-24): (1) a line with fewer than COLS active dots (R1 < 80 characters) is carried on through the blanking to COLS dots in
// the colour of the first dot after the active part; before, its last word never came, no line of the frame was handed over and the picture of the older frame
// stayed; (2) a frame of fewer than LINES_BASE lines is completed to LINES_BASE lines in the same colour (the rows below are no longer the older, taller picture).
`timescale 1ns / 1ps
module c128_pixw #(
    parameter integer X0 = 0,
    parameter integer Y0 = 0,
    parameter integer COLS = 640,
    parameter integer LINES = 200,                    // lines of a frame that is not interlaced (shown doubled)
    parameter integer LINES_BASE = 200,               // lines the application shows without b2 (400 rows): a longer frame is completed to 240 and reported with b2
    parameter integer LINES_IL = 240                  // lines of one field of an interlaced frame (shown woven)
) (
    input  wire         sys_clk,
    input  wire         sys_rst,
    input  wire         dotst,
    input  wire [15:0]  rgb565,
    input  wire         hblank,
    input  wire         vblank,
    input  wire         il,                           // the VDC is in interlace sync and video (R8[1:0] = 3)
    input  wire         field,                        // the field being drawn (0: the first of the picture)
    output wire         quiet,                        // sys_clk: no line is on its way to the DDR2
    output reg          frame_tgl = 1'b0,             // toggles with every complete frame
    output reg  [7:0]   latest = 8'd0,                // b7 valid, b2 all 480 rows are this picture (a woven pair, or a frame of more than LINES_BASE lines), b1:0 the buffer of the newest complete picture
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
    localparam integer WORDS = COLS / 8;
    initial if (COLS != 640) $display("c128_pixw: the row address is built for COLS = 640");
    // ------------------------------------------------------------------ sys_clk side
    reg         hb_d = 1'b1, vb_d = 1'b1;
    reg         in_frame = 1'b0;
    reg [9:0]   x = 10'd0;                            // dots since hblank ended
    reg [9:0]   ly = 10'h3FF;                         // lines since vblank ended
    reg         cap = 1'b0;                           // this line is in the window
    reg [9:0]   cap_row = 10'd0;
    reg [127:0] acc = 128'd0;
    reg [2:0]   pk = 3'd0;
    reg [6:0]   grp = 7'd0;
    reg         half = 1'b0;
    reg [1:0]   fcnt = 2'd0;                          // buffer of the frame that starts next (0, 1, 2)
    reg [1:0]   fwb = 2'd0;                           // buffer of this frame
    reg         il_f = 1'b0, fld_f = 1'b0;            // interlace and field of this frame (taken at its start)
    reg         pair_open = 1'b0;                     // the frame before was field 0 of an interlaced picture, written to fwb
    reg         pair_ok = 1'b0;                       // this field 1 goes to the buffer of its field 0
    reg         fr_full = 1'b0, pf_full = 1'b0;      // this frame / the frame before wrote its line 239 (a field of 240 lines)
    reg [9:0]   last_row = 10'd0;                     // the row of the last line this frame handed over
    reg         fill_on = 1'b0, fill_done = 1'b0;     // the rows after last_row are being written (border colour) / have been
    reg [9:0]   fill_row = 10'd0;
    reg         ln_blank = 1'b0;
    reg         frame_open = 1'b0;                    // a line of this frame is on its way and the frame is not yet closed
    reg [127:0] lb [0:255];                           // two lines of up to 128 words
    reg         ln_tgl = 1'b0;
    reg [9:0]   ln_row = 10'd0;
    reg [1:0]   ln_wb = 2'd0;
    reg         ln_half = 1'b0;
    reg         ln_il = 1'b0, ln_fld = 1'b0;
    reg [15:0]  ln_bc = 16'd0;                        // the colour of a blank job
    reg         waiting_last = 1'b0;
    wire        ack_tgl;
    (* ASYNC_REG = "TRUE" *) reg [1:0] ack_s = 2'b00;
    assign quiet = (ack_s[1] == ln_tgl);
    // T257 fault 7: a frame that is not interlaced is completed to LINES_BASE rows' worth of lines (to LINES when it had more than LINES_BASE lines) with
    // padc; a frame of 100 lines (the outline part of rfovdc2) no longer shows the rows of the older, taller picture below it
    reg [15:0]  padc = 16'd0;                         // the colour of the first dot after the active part of the line: the VDC's background (R26) while its display enable (R34) is on, else black; valid for the rest of the row and the rows below
    wire [9:0]  fill_end = (last_row >= LINES_BASE) ? LINES : LINES_BASE;
    wire        fill_need = !il_f && (last_row + 10'd1) < fill_end;

    // the next state of one dot, combinational
    reg         ls, fs, fe, in_n, cap_n, last_word, il_c;
    reg [9:0]   lim_c;
    reg [9:0]   x_n, ly_n, row_n;
    reg [2:0]   pk_n;
    reg [6:0]   grp_n;
    reg         pix_cap;
    reg [15:0]  pxc;
    always @* begin
        ls = hb_d & ~hblank;
        fs = vb_d & ~vblank;
        fe = ~vb_d & vblank;
        il_c = fs ? il : il_f;                       // this frame's interlace: the new value on the dot that starts it
        lim_c = il_c ? LINES_IL : LINES;
        in_n = (in_frame | fs) & ~fe;
        x_n = x; ly_n = ly; cap_n = cap; pk_n = pk; grp_n = grp; row_n = cap_row;
        last_word = 1'b0;
        pix_cap = 1'b0;
        pxc = (hblank && hb_d) ? padc : rgb565;
        if (fs) begin ly_n = 10'h3FF; cap_n = 1'b0; end
        if (fe) cap_n = 1'b0;
        if (ls) begin
            x_n = 10'd0; pk_n = 3'd0; grp_n = 7'd0;
            if (in_n && !vblank) begin
                ly_n = ly_n + 10'd1;
                cap_n = (ly_n >= Y0) && (ly_n < Y0 + lim_c);
                row_n = ly_n - Y0;
            end else cap_n = 1'b0;
        end
        // T257 fault 7: a line with fewer than COLS active dots (the plasma part of rfovdc2: R1 = 56 characters = 448 dots) goes on through the blanking until the row
        // is COLS dots wide, the dots after the active part in padc; without it such a line never completed, no frame was handed over and
        // the picture of the older frame stayed
        if (!hblank || (cap_n && x_n < X0 + COLS)) begin
            if (cap_n && x_n >= X0 && x_n < X0 + COLS) begin
                pix_cap = 1'b1;
                pk_n = pk_n + 3'd1;
                if (pk_n == 3'd0) begin                     // the eighth pixel of the word
                    grp_n = grp_n + 7'd1;
                    if (grp_n == WORDS) last_word = 1'b1;
                end
            end
            x_n = x_n + 10'd1;
        end
    end

    always @(posedge sys_clk) begin
        ack_s <= {ack_s[0], ack_tgl};
        if (dotst) begin
            hb_d <= hblank; vb_d <= vblank;
            if (hblank && !hb_d && in_frame && !vblank) padc <= rgb565;
            if (fs) begin
                il_f <= il; fld_f <= field; pf_full <= fr_full; fr_full <= 1'b0; waiting_last <= 1'b0; frame_open <= 1'b0;
                pair_open <= il & ~field;
                pair_ok <= il & field & pair_open;
                if (!(il & field & pair_open)) begin fwb <= fcnt; fcnt <= (fcnt == 2'd2) ? 2'd0 : fcnt + 2'd1; end
            end
            if (fe && frame_open) begin waiting_last <= 1'b1; frame_open <= 1'b0; end
            if (pix_cap) begin
                acc <= {pxc, acc[127:16]};
                if (pk_n == 3'd0) lb[{half, grp[6:0]}] <= {pxc, acc[127:16]};
            end
            x <= x_n; ly <= ly_n; cap <= cap_n; pk <= pk_n; grp <= grp_n; cap_row <= row_n; in_frame <= in_n;
            if (last_word) begin                            // the line is complete: hand it to the ui side
                if (ack_s[1] != ln_tgl) overruns <= overruns + 16'd1;
                ln_tgl <= ~ln_tgl;
                ln_row <= row_n; ln_wb <= fwb; ln_half <= half; ln_il <= il_c; ln_fld <= fld_f; ln_blank <= 1'b0; last_row <= row_n;
                frame_open <= 1'b1;
                if (row_n == LINES_IL - 10'd1) fr_full <= 1'b1;
                half <= ~half;
                cap <= 1'b0;
                if (row_n == lim_c - 10'd1) begin waiting_last <= 1'b1; frame_open <= 1'b0; end
            end
        end
        // frame done: the last line is written (acknowledged by the ui side). A frame that is not interlaced and has fewer lines than LINES_BASE (or than LINES, when it
        // had more than LINES_BASE) first gets its rows up to that limit written in the border colour, one blank job per line, then it is reported.
        if (waiting_last && ack_s[1] == ln_tgl) begin
            if (fill_on) begin
                if (fill_row == fill_end) begin fill_on <= 1'b0; fill_done <= 1'b1; end
                else begin
                    ln_row <= fill_row; ln_wb <= fwb; ln_il <= 1'b0; ln_fld <= 1'b0; ln_blank <= 1'b1; ln_bc <= padc;
                    ln_tgl <= ~ln_tgl; fill_row <= fill_row + 10'd1;
                end
            end else if (fill_need && !fill_done) begin
                fill_on <= 1'b1; fill_row <= last_row + 10'd1;
            end else begin
                waiting_last <= 1'b0;
                if (!il_f || pair_ok) begin               // an interlaced picture counts when its field 1 is in the buffer of its field 0
                    frame_tgl <= ~frame_tgl;
                    latest <= {1'b1, 4'd0, il_f ? (fr_full & pf_full) : (last_row >= LINES_BASE), fwb};
                    seq <= seq + 8'd1;
                end
            end
        end
        if (dotst && fs) begin fill_on <= 1'b0; fill_done <= 1'b0; end     // a new frame ends a fill that is still running (after the done block: the last assignment wins)
        if (sys_rst) begin in_frame <= 1'b0; waiting_last <= 1'b0; cap <= 1'b0; fill_on <= 1'b0; end
    end

    // ------------------------------------------------------------------ ui_clk side
    (* ASYNC_REG = "TRUE" *) reg [2:0] ln_s = 3'b000;
    reg         ack_r = 1'b0;
    assign ack_tgl = ack_r;
    reg [127:0] lb_q = 128'd0;
    reg [7:0]   rd_a = 8'd0;
    reg [3:0]   us = 4'd0;
    reg [6:0]   ug = 7'd0;
    reg [9:0]   u_row = 10'd0;
    reg         u_dup = 1'b0;                              // the second write of the line (row 2y+1)
    reg         u_il = 1'b0, u_fld = 1'b0, u_blank = 1'b0;
    reg [15:0]  u_bc = 16'd0;
    reg [1:0]   u_wb = 2'd0;
    reg         u_half = 1'b0;
    // the registers cross from cpu_clk: a value counts when two samples in a row agree
    reg [31:0]  b_a = 32'd0, b_b = 32'd0, b_q = 32'h01D00000;
    reg [31:0]  s_a = 32'd0, s_b = 32'd0, s_q = 32'h00100000;
    reg [26:0]  buf_w = 27'd0;
    localparam U_IDLE = 4'd0, U_BASE = 4'd1, U_RD = 4'd2, U_W1 = 4'd3, U_W2 = 4'd4, U_PUT = 4'd5, U_NEXT = 4'd6;
    // 16-bit units: a buffer row is 640, a word 8; the row written is 2y + field (woven) or 2y + dup (doubled)
    wire [10:0] out_row = {u_row, u_il ? u_fld : u_dup};
    wire [26:0] row_w = ({16'd0, out_row} << 9) + ({16'd0, out_row} << 7);
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
            u_row <= ln_row; u_wb <= ln_wb; u_half <= ln_half; u_il <= ln_il; u_fld <= ln_fld; u_blank <= ln_blank; u_bc <= ln_bc; ug <= 7'd0; u_dup <= 1'b0;
            us <= U_BASE;
        end
        U_BASE: begin                                  // buffer base in 16-bit units
            buf_w <= base_w + (u_wb == 2'd0 ? 27'd0 : u_wb == 2'd1 ? stride_w : {stride_w[25:0], 1'b0});
            us <= U_RD;
        end
        U_RD: begin rd_a <= {u_half, ug}; us <= U_W1; end
        U_W1: us <= U_W2;
        U_W2: begin                                    // lb_q holds the word
            e_wdf_data <= lb_q;
            e_addr <= buf_w + row_w + {17'd0, ug, 3'd0};
            e_cmd <= 3'b000;
            e_en <= 1'b1;
            us <= U_PUT;
        end
        U_PUT: if (e_rdy) begin e_en <= 1'b0; us <= U_NEXT; end
        U_NEXT: begin
            if (ug == WORDS - 1) begin
                if (!u_il && !u_dup) begin             // not interlaced: the same line once more, one row down
                    u_dup <= 1'b1; ug <= 7'd0; us <= U_RD;
                end else begin
                    ack_r <= ln_s[2]; us <= U_IDLE;
                end
            end else begin ug <= ug + 7'd1; us <= U_RD; end
        end
        default: us <= U_IDLE;
        endcase
        if (u_blank) e_wdf_data <= {8{u_bc}};            // a blank job writes words of its colour (the frame's border colour)
    end
endmodule
