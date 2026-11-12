// T143 D6: the Neo Geo picture into a frame buffer in the DDR2 for the host's pixel layer (pix_layer.v): RGB565, 320 x 224 source pixels (the Neo Geo's visible
// area: the machine's own blanking is the window, X0 = Y0 = 0), 640 bytes a row, four buffers (the layer shows one, the application flips on the frame-done
// report, the core writes the next buffer in the ring). Written for this project (TREX), no upstream code; the circuit of nes_pixw.v (T95) with the Neo
// Geo's window and a 16 bit wide line buffer (one pixel written per dot on the machine's clock, eight read per DDR2 word on ui_clk: a 1024 x 16 block RAM
// instead of four for 128 bit words).
//
// Everything on the clk side moves only on `dotst`, one strobe per Neo Geo pixel (6 MHz: neo_core's ce_pix), so a stalled machine (no strobe) stops it too.
// A completed line goes to the ui_clk side by a toggle; that side reads it from the line buffer and offers eight-pixel words to neo_mem's e_ arbiter
// (pw_en / pw_rdy: one 128 bit write each).
`timescale 1ns / 1ps
module neo_pixw #(
    parameter integer X0 = 0,
    parameter integer Y0 = 0,
    parameter integer COLS = 320,
    parameter integer LINES = 224
) (
    input  wire         sys_clk,                      // the machine clock (48 MHz)
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
    output reg  [9:0]   seen_cols = 10'd0,            // the widest line (pixels between blanks) seen in this frame - bring-up
    output reg  [9:0]   seen_lines = 10'd0,           // the number of lines with pixels in the last frame
    // quasi static, from the app (cpu_clk)
    input  wire [31:0]  fb_base,                      // DDR2 byte address of buffer 0
    input  wire [31:0]  fb_stride,                    // bytes between the buffers
    // ui_clk side
    input  wire         ui_clk,
    input  wire         ui_rst,
    output reg          pw_en = 1'b0,
    output reg  [26:0]  pw_addr = 27'd0,              // 16 bit word address
    output reg  [127:0] pw_data = 128'd0,
    input  wire         pw_rdy
);
    localparam integer WORDS = COLS / 8;
    // ------------------------------------------------------------------ sys_clk side
    reg         hb_d = 1'b1, vb_d = 1'b1;
    reg         in_frame = 1'b0;
    reg [9:0]   x = 10'd0;                            // dots since hblank ended
    reg [9:0]   ly = 10'h3FF;                         // lines since vblank ended
    reg         cap = 1'b0;                           // this line is in the window
    reg [9:0]   cap_row = 10'd0;
    reg [8:0]   col = 9'd0;                           // pixels of this line stored
    reg [1:0]   fcnt = 2'd0;                          // buffer of the frame that starts next
    reg [1:0]   fwb = 2'd0;                           // buffer of this frame
    reg         half = 1'b0;
    (* ram_style = "block" *) reg [15:0] lb [0:1023];
    reg         ln_tgl = 1'b0;
    reg [9:0]   ln_row = 10'd0;
    reg [1:0]   ln_wb = 2'd0;
    reg         ln_half = 1'b0;
    reg         waiting_last = 1'b0;
    wire        ack_tgl;
    (* ASYNC_REG = "TRUE" *) reg [1:0] ack_s = 2'b00;
    assign quiet = (ack_s[1] == ln_tgl);
    reg [9:0]   fr_lines = 10'd0;

    // the next state of one dot, combinational
    reg         ls, fs, fe, in_n, cap_n;
    reg [9:0]   x_n, ly_n, row_n;
    reg         pix_cap, last_pix;
    always @* begin
        ls = hb_d & ~hblank;
        fs = vb_d & ~vblank;
        fe = ~vb_d & vblank;
        in_n = (in_frame | fs) & ~fe;
        x_n = x; ly_n = ly; cap_n = cap; row_n = cap_row;
        last_pix = 1'b0;
        pix_cap = 1'b0;
        if (fs) begin ly_n = 10'h3FF; cap_n = 1'b0; end
        if (fe) cap_n = 1'b0;
        if (ls) begin
            x_n = 10'd0;
            if (in_n && !vblank) begin
                ly_n = ly_n + 10'd1;
                cap_n = (ly_n >= Y0) && (ly_n < Y0 + LINES);
                row_n = ly_n - Y0;
            end else cap_n = 1'b0;
        end
        if (!hblank) begin
            if (cap_n && x_n >= X0 && x_n < X0 + COLS) begin
                pix_cap = 1'b1;
                if (x_n == X0 + COLS - 1) last_pix = 1'b1;
            end
            x_n = x_n + 10'd1;
        end
    end

    always @(posedge sys_clk) begin
        ack_s <= {ack_s[0], ack_tgl};
        if (dotst) begin
            hb_d <= hblank; vb_d <= vblank;
            if (fs) begin fwb <= fcnt; fcnt <= fcnt + 2'd1; waiting_last <= 1'b0; seen_lines <= fr_lines; fr_lines <= 10'd0; end
            if (ls) col <= 9'd0;
            if (pix_cap) begin
                lb[{half, (ls ? 9'd0 : col)}] <= rgb565;
                col <= (ls ? 9'd0 : col) + 9'd1;
            end
            if (x_n > seen_cols && !hblank) seen_cols <= x_n;
            if (ls && x > 10'd0 && x > seen_cols) seen_cols <= x;
            x <= x_n; ly <= ly_n; cap <= cap_n; cap_row <= row_n; in_frame <= in_n;
            if (last_pix) begin                             // the line is complete: hand it to the ui side
                if (ack_s[1] != ln_tgl) overruns <= overruns + 16'd1;
                ln_tgl <= ~ln_tgl;
                ln_row <= row_n; ln_wb <= fwb; ln_half <= half;
                half <= ~half;
                cap <= 1'b0;
                fr_lines <= fr_lines + 10'd1;
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
    reg [15:0]  lb_q = 16'd0;
    reg [9:0]   rd_a = 10'd0;
    reg [3:0]   us = 4'd0;
    reg [5:0]   ug = 6'd0;
    reg [3:0]   ui = 4'd0;
    reg [9:0]   u_row = 10'd0;
    reg [1:0]   u_wb = 2'd0;
    reg         u_half = 1'b0;
    // the registers cross from cpu_clk: a value counts when two samples in a row agree
    reg [31:0]  b_a = 32'd0, b_b = 32'd0, b_q = 32'h01D00000;
    reg [31:0]  s_a = 32'd0, s_b = 32'd0, s_q = 32'h00100000;
    reg [26:0]  buf_w = 27'd0;
    localparam U_IDLE = 4'd0, U_BASE = 4'd1, U_RD = 4'd2, U_PUT = 4'd5, U_NEXT = 4'd6, U_ASM = 4'd7;
    // 16-bit units: a row is 320, a word 8
    wire [26:0] row_w = ({17'd0, u_row} << 8) + ({17'd0, u_row} << 6);
    wire [26:0] base_w = b_q[27:1];
    wire [26:0] stride_w = s_q[27:1];

    // the pixel slot of the word the block RAM answers for step ui (2..9 -> 0..7); a 3 bit value on purpose: inside `16 * (...)` the subtraction would be 32 bits wide and
    // ui = 8, 9 would give a negative offset, an out of range part select that writes nothing (pixels 6 and 7 of every word lost)
    wire [2:0] slot = ui[2:0] - 3'd2;
    always @(posedge ui_clk) begin
        ln_s <= {ln_s[1:0], ln_tgl};
        lb_q <= lb[rd_a];
        b_a <= fb_base;   b_b <= b_a;   if (b_a == b_b) b_q <= b_b;
        s_a <= fb_stride; s_b <= s_a;   if (s_a == s_b) s_q <= s_b;
        if (ui_rst) begin
            us <= U_IDLE; pw_en <= 1'b0;
        end else case (us)
        U_IDLE: if (ln_s[2] != ln_s[1]) begin
            u_row <= ln_row; u_wb <= ln_wb; u_half <= ln_half; ug <= 6'd0;
            us <= U_BASE;
        end
        U_BASE: begin                                  // buffer base in 16-bit units
            buf_w <= base_w + (u_wb == 2'd0 ? 27'd0 : u_wb == 2'd1 ? stride_w : u_wb == 2'd2 ? {stride_w[25:0], 1'b0} : {stride_w[25:0], 1'b0} + stride_w);
            ui <= 4'd0;
            us <= U_RD;
        end
        // eight pixels of word ug: the address leaves, the block RAM answers two clocks later (its read register, then lb_q)
        U_RD: begin
            rd_a <= {u_half, ug[5:0] , 3'd0} + {6'd0, ui[2:0]};
            if (ui >= 4'd2) pw_data[{slot, 4'd0} +: 16] <= lb_q;
            if (ui == 4'd9) begin
                pw_addr <= buf_w + row_w + {18'd0, ug, 3'd0};
                pw_en <= 1'b1;
                us <= U_PUT;
            end
            ui <= ui + 4'd1;
        end
        U_PUT: if (pw_rdy) begin pw_en <= 1'b0; us <= U_NEXT; end
        U_NEXT: begin
            if (ug == WORDS - 1) begin
                ack_r <= ln_s[2]; us <= U_IDLE;
            end else begin ug <= ug + 6'd1; ui <= 4'd0; us <= U_RD; end
        end
        default: us <= U_IDLE;
        endcase
    end
endmodule
