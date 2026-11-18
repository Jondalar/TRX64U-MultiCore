// TI-99/4A core: the TMS9918's picture into a frame buffer in the DDR2 for the host's pixel layer (pix_layer.v): RGB565, COLS x LINES pixels,
// COLS*2 bytes a row, four buffers (the layer shows one, the app flips on the frame-done report, the core writes the next one in the ring).
// Written for this project; the shape (line buffer in sys_clk, a line handed to the ui_clk side by a toggle, 128-bit words through the host's e_
// port) is that of cores/vc20/rtl/trx/vc20_pixw.vhd, the pixel handling is 1:1 (the TI's 256 x 192 is 4:3 with square pixels).
//
// The VDP raster is 342 x 262 (tms9918_vdp.v, NTSC); the visible part is 13 + 256 + 15 = 284 pixels by 27 + 192 + 24 = 243 lines. The window drops X0 = 2
// pixels at the left and 2 at the right (COLS = 280) and Y0 = 3 lines at the top and none at the bottom (LINES = 240).
// The row address is built for COLS = 280 (256 + 16 + 8). A frame is shown by the pixel layer 4 x: 1120 x 960.
`timescale 1ns / 1ps
module ti99_pixw #(
    parameter COLS  = 280,
    parameter LINES = 240,
    parameter X0    = 2,
    parameter Y0    = 3,
    parameter OVERLAY = 0        // 1: the first 64 rows show eight 16-bit probe words as white (1) / blue (0) blocks, 16 px wide (diagnostics)
) (
    input  wire        sys_clk,
    input  wire        sys_rst,
    input  wire        ce_pix,          // one clock per VDP pixel; colour/hblank/vblank are those of this pixel
    input  wire [3:0]  colour,
    input  wire        hblank,
    input  wire        vblank,
    input  wire [127:0] dbgw,       // probe words, word 0 = [127:112] on the top row group (latched at the start of each frame)
    output wire        frame_tgl,
    output wire [7:0]  latest,          // b7 valid, b1:0 the buffer of the newest complete frame
    output wire [7:0]  seq,
    output wire [15:0] overruns,
    input  wire [31:0] fb_base,
    input  wire [31:0] fb_stride,
    input  wire        ui_clk,
    input  wire        ui_rst,
    output reg  [26:0] e_addr = 27'd0,
    output wire [2:0]  e_cmd,
    output wire        e_en,
    input  wire        e_rdy,
    output reg  [127:0] e_wdf_data = 128'd0,
    output wire [15:0] e_wdf_mask,
    output wire        e_wdf_wren
);
    localparam WORDS = COLS / 8;

    // the TMS9918A's colours (transparent shows as black), RGB565
    function [15:0] pal(input [3:0] c);
        case (c)
            4'd0: pal = 16'h0000;     // transparent
            4'd1: pal = 16'h0000;
            4'd2: pal = 16'h2648;
            4'd3: pal = 16'h5EEF;
            4'd4: pal = 16'h52BD;
            4'd5: pal = 16'h7BBF;
            4'd6: pal = 16'hD289;
            4'd7: pal = 16'h475E;
            4'd8: pal = 16'hFAAA;
            4'd9: pal = 16'hFBCF;
            4'd10: pal = 16'hD60A;
            4'd11: pal = 16'hE670;
            4'd12: pal = 16'h2587;
            4'd13: pal = 16'hCAD7;
            4'd14: pal = 16'hCE79;
            4'd15: pal = 16'hFFFF;
        endcase
    endfunction

    // ------------------------------------------------------------------ sys_clk side
    reg         np = 1'b0, fpix = 1'b0;
    reg  [15:0] col = 16'd0;
    reg         hbl = 1'b1, vbl = 1'b1;
    reg         hb_d = 1'b1, vb_d = 1'b1, in_frame = 1'b0, cap = 1'b0;
    reg  [9:0]  x = 10'd0, ly = 10'h3FF, cap_row = 10'd0;
    reg  [127:0] acc = 128'd0;
    reg  [2:0]  pk = 3'd0;
    reg  [5:0]  grp = 6'd0;
    reg         half = 1'b0;
    reg  [1:0]  fcnt = 2'd0, fwb = 2'd0;
    reg  [127:0] lb [0:127];
    reg         ln_tgl = 1'b0, ln_half = 1'b0, waiting_last = 1'b0;
    reg  [9:0]  ln_row = 10'd0;
    reg  [1:0]  ln_wb = 2'd0;
    (* ASYNC_REG = "TRUE" *) reg [1:0] ack_s = 2'b00;
    wire        ack_tgl;
    reg  [15:0] over_i = 16'd0;
    reg  [7:0]  seq_i = 8'd0, latest_i = 8'd0;
    reg         ftgl_i = 1'b0;

    reg         ls, fs, fe, n_cap, n_in, pix_cap, last_word;
    reg  [9:0]  n_x, n_ly, n_row;
    reg  [2:0]  n_pk;
    reg  [5:0]  n_grp;

    assign frame_tgl = ftgl_i;
    assign seq = seq_i;
    assign overruns = over_i;
    assign latest = latest_i;

    always @(posedge sys_clk) begin
        fpix <= 1'b0;
        if (ce_pix) begin
            col <= pal(colour);
            hbl <= hblank;
            vbl <= vblank;
            np  <= 1'b1;
        end else if (np) begin
            fpix <= 1'b1;
            np   <= 1'b0;
        end
    end

    always @* begin
        ls = hb_d && !hbl;
        fs = vb_d && !vbl;
        fe = !vb_d && vbl;
        n_in = (in_frame || fs) && !fe;
        n_x = x; n_ly = ly; n_cap = cap; n_pk = pk; n_grp = grp; n_row = cap_row;
        last_word = 1'b0; pix_cap = 1'b0;
        if (fs) begin n_ly = 10'h3FF; n_cap = 1'b0; end
        if (fe) n_cap = 1'b0;
        if (ls) begin
            n_x = 10'd0; n_pk = 3'd0; n_grp = 6'd0;
            if (n_in && !vbl) begin
                n_ly  = n_ly + 10'd1;
                n_cap = (n_ly >= Y0) && (n_ly < Y0 + LINES);
                n_row = n_ly - Y0;
            end else begin
                n_cap = 1'b0;
            end
        end
        if (!hbl) begin
            if (n_cap && n_x >= X0 && n_x < X0 + COLS) begin
                pix_cap = 1'b1;
                n_pk = n_pk + 3'd1;
                if (n_pk == 3'd0) begin
                    n_grp = n_grp + 6'd1;
                    if (n_grp == WORDS) last_word = 1'b1;
                end
            end
            n_x = n_x + 10'd1;
        end
    end

    // diagnostic overlay: pixel index in the window, row in the window; n_x has already counted this pixel
    reg  [127:0] dwl = 128'd0;
    wire [9:0]  ox = n_x - 10'd1 - X0;
    wire        ov_on = (OVERLAY != 0) && (n_row < 10'd64) && (ox < 10'd256);
    wire [15:0] ov_w = dwl[127 - 16 * n_row[5:3] -: 16];
    wire        ov_bit = ov_w[4'd15 - ox[7:4]];
    wire [15:0] ocol = !ov_on ? col :
                       (ox[3:0] == 4'd0 || ox[3:0] == 4'd15 || n_row[2:0] == 3'd0 || n_row[2:0] == 3'd7) ? 16'h0000 :
                       ov_bit ? 16'hFFFF : 16'h001F;

    wire        lb_we = fpix && pix_cap && n_pk == 3'd0;
    wire [6:0]  lb_wa = {half, grp};
    wire [127:0] lb_wd = {ocol, acc[127:16]};
    always @(posedge sys_clk) if (lb_we) lb[lb_wa] <= lb_wd;

    always @(posedge sys_clk) begin
        ack_s <= {ack_s[0], ack_tgl};
        if (fpix) begin
            hb_d <= hbl; vb_d <= vbl;
            if (fs) begin dwl <= dbgw; fwb <= fcnt; fcnt <= fcnt + 2'd1; waiting_last <= 1'b0; end
            if (pix_cap) acc <= {ocol, acc[127:16]};
            x <= n_x; pk <= n_pk; grp <= n_grp; cap_row <= n_row; ly <= n_ly;
            cap <= n_cap; in_frame <= n_in;
            if (last_word) begin
                if (ack_s[1] != ln_tgl) over_i <= over_i + 16'd1;
                ln_tgl <= ~ln_tgl;
                ln_row <= n_row; ln_wb <= fwb; ln_half <= half;
                half <= ~half;
                cap <= 1'b0;
                if (n_row == LINES - 1) waiting_last <= 1'b1;
            end
        end
        if (waiting_last && ack_s[1] == ln_tgl) begin
            waiting_last <= 1'b0;
            ftgl_i <= ~ftgl_i;
            latest_i <= {6'b100000, fwb};
            seq_i <= seq_i + 8'd1;
        end
        if (sys_rst) begin in_frame <= 1'b0; waiting_last <= 1'b0; cap <= 1'b0; end
    end

    // ------------------------------------------------------------------ ui_clk side
    localparam U_IDLE = 0, U_BASE = 1, U_RD = 2, U_W1 = 3, U_W2 = 4, U_PUT = 5, U_NEXT = 6;
    reg  [2:0]  us = U_IDLE;
    (* ASYNC_REG = "TRUE" *) reg [2:0] ln_s = 3'b000;
    reg         ack_r = 1'b0;
    reg  [127:0] lb_q = 128'd0;
    reg  [6:0]  rd_a = 7'd0;
    reg  [5:0]  ug = 6'd0;
    reg  [9:0]  u_row = 10'd0;
    reg  [1:0]  u_wb = 2'd0;
    reg         u_half = 1'b0;
    reg  [31:0] b_a = 0, b_b = 0, s_a = 0, s_b = 0;
    reg  [31:0] b_q = 32'h01D00000, s_q = 32'h00100000;
    reg  [26:0] buf_w = 27'd0;
    reg         e_en_i = 1'b0;
    reg  [2:0]  e_cmd_i = 3'b000;
    wire [26:0] base_w = b_q[27:1];
    wire [26:0] stride_w = s_q[27:1];
    wire [26:0] r27 = {17'd0, u_row};
    wire [26:0] row_w = (r27 << 8) + (r27 << 4) + (r27 << 3);       // COLS = 280 = 256 + 16 + 8

    assign e_wdf_mask = 16'h0000;
    assign e_en = e_en_i;
    assign e_cmd = e_cmd_i;
    assign e_wdf_wren = (e_cmd_i == 3'b000) ? e_en_i : 1'b0;
    assign ack_tgl = ack_r;

    always @(posedge ui_clk) begin
        ln_s <= {ln_s[1:0], ln_tgl};
        lb_q <= lb[rd_a];
        b_a <= fb_base;   b_b <= b_a;   if (b_a == b_b) b_q <= b_b;
        s_a <= fb_stride; s_b <= s_a;   if (s_a == s_b) s_q <= s_b;
        if (ui_rst) begin
            us <= U_IDLE; e_en_i <= 1'b0;
        end else begin
            case (us)
                U_IDLE: if (ln_s[2] != ln_s[1]) begin
                    u_row <= ln_row; u_wb <= ln_wb; u_half <= ln_half; ug <= 6'd0;
                    us <= U_BASE;
                end
                U_BASE: begin
                    case (u_wb)
                        2'd0: buf_w <= base_w;
                        2'd1: buf_w <= base_w + stride_w;
                        2'd2: buf_w <= base_w + (stride_w << 1);
                        default: buf_w <= base_w + (stride_w << 1) + stride_w;
                    endcase
                    us <= U_RD;
                end
                U_RD: begin rd_a <= {u_half, ug}; us <= U_W1; end
                U_W1: us <= U_W2;
                U_W2: begin
                    e_wdf_data <= lb_q;
                    e_addr <= buf_w + row_w + ({21'd0, ug} << 3);
                    e_cmd_i <= 3'b000;
                    e_en_i <= 1'b1;
                    us <= U_PUT;
                end
                U_PUT: if (e_rdy) begin e_en_i <= 1'b0; us <= U_NEXT; end
                U_NEXT: begin
                    if (ug == WORDS - 1) begin ack_r <= ln_s[2]; us <= U_IDLE; end
                    else begin ug <= ug + 6'd1; us <= U_RD; end
                end
                default: us <= U_IDLE;
            endcase
        end
    end
endmodule
