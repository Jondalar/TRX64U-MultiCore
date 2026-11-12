// T134 (from cores/c264/rtl/trx/pet_pixw.v, T90, same author): the PET picture into a frame buffer in the DDR2 for the host's pixel layer
// (pix_layer.v): RGB565, 640 x 400 source pixels, 1280 bytes a row, four buffers (the layer shows one, the app flips on the frame-done report, the
// core writes the next buffer in the ring). Differences from the C264 original: the window is 640 x 400 from dot 0 / line 0, a row is 80 words
// of 8 pixels (7 bit word counter, 256 entry line buffer, two lines), the row address is row * 640 16-bit units. Everything on the sys_clk side
// moves on `dotst`; the ui_clk side writes the words through the host's e_ port, one command at a time.
`timescale 1ns / 1ps
module pet_pixw #(
    parameter integer X0 = 0,
    parameter integer Y0 = 0,
    parameter integer COLS = 640,
    parameter integer LINES = 400
) (
    input  wire         sys_clk,
    input  wire         sys_rst,
    input  wire         dotst,
    input  wire [15:0]  rgb565,
    input  wire         hblank,
    input  wire         vblank,
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
    reg [9:0]   x = 10'd0;                            // dots since hblank ended
    reg [9:0]   ly = 10'h3FF;                         // lines since vblank ended
    reg         cap = 1'b0;                           // this line is in the window
    reg [9:0]   cap_row = 10'd0;
    reg [127:0] acc = 128'd0;
    reg [2:0]   pk = 3'd0;
    reg [6:0]   grp = 7'd0;
    reg         half = 1'b0;
    reg [1:0]   fcnt = 2'd0;                          // buffer of the frame that starts next
    reg [1:0]   fwb = 2'd0;                           // buffer of this frame
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
                cap_n = (ly_n >= Y0) && (ly_n < Y0 + LINES);
                row_n = ly_n - Y0;
            end else cap_n = 1'b0;
        end
        if (!hblank) begin
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
            if (fs) begin fwb <= fcnt; fcnt <= fcnt + 2'd1; waiting_last <= 1'b0; end
            if (pix_cap) begin
                acc <= {rgb565, acc[127:16]};
                if (pk_n == 3'd0) lb[{half, grp}] <= {rgb565, acc[127:16]};
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
    reg [9:0]   u_row = 10'd0;
    reg [1:0]   u_wb = 2'd0;
    reg         u_half = 1'b0;
    // the registers cross from cpu_clk: a value counts when two samples in a row agree
    reg [31:0]  b_a = 32'd0, b_b = 32'd0, b_q = 32'h01D00000;
    reg [31:0]  s_a = 32'd0, s_b = 32'd0, s_q = 32'h00100000;
    reg [26:0]  buf_w = 27'd0;
    localparam U_IDLE = 4'd0, U_BASE = 4'd1, U_RD = 4'd2, U_W1 = 4'd3, U_W2 = 4'd4, U_PUT = 4'd5, U_NEXT = 4'd6;
    // 16-bit units: a row is 640, a word 8
    wire [26:0] row_w = ({17'd0, u_row} << 9) + ({17'd0, u_row} << 7);
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
            u_row <= ln_row; u_wb <= ln_wb; u_half <= ln_half; ug <= 7'd0;
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
                ack_r <= ln_s[2]; us <= U_IDLE;
            end else begin ug <= ug + 7'd1; us <= U_RD; end
        end
        default: us <= U_IDLE;
        endcase
    end
endmodule
