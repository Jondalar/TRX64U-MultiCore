// T116 D5 / P5: the Next's picture into a frame buffer in the DDR2 for the host's pixel layer (pix_layer.v): RGB565, 640 x 256 source
// pixels (the Next's 320 x 256 display area at the 14 MHz dot clock: Layer 2's 640 mode, the Timex hi-res and the border all keep every
// column), every line written twice (640 x 512 in the buffer, so that one dot is half as wide as it is tall, the Next's aspect), four
// buffers in the DDR2 (the layer shows one, the app flips on the frame-done report, the core writes the next buffer in the ring).
// Written for this project (TREX), no upstream code; it is the circuit of nes_pixw.v (T95) with the Next's window and a second pass per line.
//
// Everything on the clk side moves only on `dotst`, one strobe per 14 MHz dot (zxn_core: two clocks after the machine's CLK_14 edge that
// produced it), so the pause (no enable, no strobe) stops it too. hblank is the machine's o_RGB_BK_n (blank: horizontal and vertical),
// vblank its vertical sync: a line starts when blank ends, a frame when the sync ends (the lines between the sync and the first active line
// have no blank end, so y counts active lines from 0). X0 and Y0 (dots and lines inside the active area) depend on the machine timing
// (zxn_core: 48K 0/24, 128K and +3 16/24, Pentagon 64/32; 60 Hz: Y0 0); they are sampled at the start of a frame.
// Pixels are packed little endian, 8 to a 128-bit word (80 words a row). A completed line goes to the ui_clk side by a toggle; that
// side reads the words from the line buffer and writes them through the host's e_ port (MIG command, data with the command, one at a time).
`timescale 1ns / 1ps
module zxn_pixw #(
    parameter integer COLS = 640,
    parameter integer LINES = 256
) (
    input  wire         sys_clk,                      // the machine clock (cpu_clk)
    input  wire         sys_rst,
    input  wire         dotst,
    input  wire [15:0]  rgb565,
    input  wire         hblank,
    input  wire         vblank,
    input  wire [9:0]   x0,
    input  wire [8:0]   y0,
    output wire         quiet,                        // sys_clk: no line is on its way to the DDR2
    output reg          frame_tgl = 1'b0,             // toggles with every complete frame
    output reg  [7:0]   latest = 8'd0,                // b7 valid, b1:0 the buffer of the newest complete frame
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
    // ------------------------------------------------------------------ sys_clk side
    reg         hb_d = 1'b1, vb_d = 1'b1;
    reg         in_frame = 1'b0;
    reg [9:0]   x = 10'd0;                            // dots since the blank ended
    reg [9:0]   ly = 10'h3FF;                         // lines since the frame started
    reg         cap = 1'b0;                           // this line is in the window
    reg [9:0]   cap_row = 10'd0;
    reg [127:0] acc = 128'd0;
    reg [2:0]   pk = 3'd0;
    reg [6:0]   grp = 7'd0;
    reg         half = 1'b0;
    reg [1:0]   fcnt = 2'd0;                          // buffer of the frame that starts next
    reg [1:0]   fwb = 2'd0;                           // buffer of this frame
    reg [9:0]   x0_q = 10'd0;
    reg [8:0]   y0_q = 9'd0;
    reg [127:0] lb [0:255];
    reg         ln_tgl = 1'b0;
    reg [9:0]   ln_row = 10'd0;
    reg [1:0]   ln_wb = 2'd0;
    reg         ln_half = 1'b0;
    reg         waiting_last = 1'b0;
    wire        ack_tgl;
    (* ASYNC_REG = "TRUE" *) reg [1:0] ack_s = 2'b00;
    assign quiet = (ack_s[1] == ln_tgl);

    // the next state of one dot, combinational
    reg         ls, fs, fe, in_n, cap_n, last_word;
    reg [9:0]   x_n, ly_n, row_n;
    reg [2:0]   pk_n;
    reg [6:0]   grp_n;
    reg         pix_cap;
    always @* begin
        ls = hb_d & ~hblank;
        fs = vb_d & ~vblank;
        fe = ~vb_d & vblank;
        in_n = (in_frame | fs) & ~fe;
        x_n = x; ly_n = ly; cap_n = cap; pk_n = pk; grp_n = grp; row_n = cap_row;
        last_word = 1'b0;
        pix_cap = 1'b0;
        if (fs) begin ly_n = 10'h3FF; cap_n = 1'b0; end
        if (fe) cap_n = 1'b0;
        if (ls) begin
            x_n = 10'd0; pk_n = 3'd0; grp_n = 7'd0;
            if (in_n && !vblank) begin
                ly_n = ly_n + 10'd1;
                cap_n = (ly_n >= {1'b0, y0_q}) && (ly_n < {1'b0, y0_q} + LINES);
                row_n = ly_n - {1'b0, y0_q};
            end else cap_n = 1'b0;
        end
        if (!hblank) begin
            if (cap_n && x_n >= x0_q && x_n < x0_q + COLS) begin
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

    wire [15:0] rgbu = rgb565;
    always @(posedge sys_clk) begin
        ack_s <= {ack_s[0], ack_tgl};
        if (dotst) begin
            hb_d <= hblank; vb_d <= vblank;
            if (fs) begin fwb <= fcnt; fcnt <= fcnt + 2'd1; waiting_last <= 1'b0; x0_q <= x0; y0_q <= y0; end
            if (pix_cap) begin
                acc <= {rgbu, acc[127:16]};
                if (pk_n == 3'd0) lb[{half, grp}] <= {rgbu, acc[127:16]};
            end
            x <= x_n; ly <= ly_n; cap <= cap_n; pk <= pk_n; grp <= grp_n; cap_row <= row_n; in_frame <= in_n;
            if (last_word) begin                            // the line is complete: hand it to the ui side
                if (ack_s[1] != ln_tgl) overruns <= overruns + 16'd1;
                ln_tgl <= ~ln_tgl;
                ln_row <= row_n; ln_wb <= fwb; ln_half <= half;
                half <= ~half;
                cap <= 1'b0;
                if (row_n == LINES - 1) waiting_last <= 1'b1;
            end
        end
        // frame done: the last line is written (acknowledged by the ui side)
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
    reg [7:0]   rd_a = 8'd0;
    reg [3:0]   us = 4'd0;
    reg [6:0]   ug = 7'd0;
    reg         u_pass = 1'b0;
    reg [9:0]   u_row = 10'd0;
    reg [1:0]   u_wb = 2'd0;
    reg         u_half = 1'b0;
    // the registers cross from cpu_clk: a value counts when two samples in a row agree
    reg [31:0]  b_a = 32'd0, b_b = 32'd0, b_q = 32'h01D00000;
    reg [31:0]  s_a = 32'd0, s_b = 32'd0, s_q = 32'h00100000;
    reg [26:0]  buf_w = 27'd0;
    localparam U_IDLE = 4'd0, U_BASE = 4'd1, U_RD = 4'd2, U_W1 = 4'd3, U_W2 = 4'd4, U_PUT = 4'd5, U_NEXT = 4'd6;
    // 16-bit units: a source row is 640, a buffer row (two per source row) 640
    wire [9:0]  row2 = {u_row[8:0], u_pass};
    wire [26:0] row_w = {8'd0, row2, 9'd0} + {10'd0, row2, 7'd0};
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
            u_row <= ln_row; u_wb <= ln_wb; u_half <= ln_half; ug <= 7'd0; u_pass <= 1'b0;
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
            e_addr <= buf_w + row_w + {17'd0, ug, 3'd0};
            e_cmd <= 3'b000;
            e_en <= 1'b1;
            us <= U_PUT;
        end
        U_PUT: if (e_rdy) begin e_en <= 1'b0; us <= U_NEXT; end
        U_NEXT: begin
            if (ug == WORDS - 1) begin
                if (!u_pass) begin u_pass <= 1'b1; ug <= 7'd0; us <= U_RD; end      // the same line once more, one row down
                else begin ack_r <= ln_s[2]; us <= U_IDLE; end
            end else begin ug <= ug + 7'd1; us <= U_RD; end
        end
        default: us <= U_IDLE;
        endcase
    end
endmodule
