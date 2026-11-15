// SPDX-License-Identifier: GPL-3.0-or-later
// The C64 text layer (T12 §4, in hardware after all): the freeze menu the app
// writes into the C64's memory, drawn on the HDMI picture as the VIC would
// draw it, on top of everything (vic_scaler -> overlay.v -> pix_layer -> this).
// The pixel layer below holds the dimmed still the app drew once at the
// freeze; this layer puts the text on it, so a key press shows in the next
// frame without the app drawing anything.
//
// Sources (C64 clock, cclk): the menu's screen and charset through the C64
// RAM's second port (c64_mem.v; the char ROM through its ROM block), the
// colours through the colour RAM's second port, both read in clocks the app's
// window, JTAG and APPLY leave free (top.v), so no machine cycle is used. (The
// window's colour RAM aperture, dma_win.v, uses that port too, ahead of this layer.) The
// VIC registers are not the VIC's: the app writes the view it wants into
// trx_sys.v's TX_VIEW (page 5, 0x5028; reset value = the freeze menu's: text
// mode, screen $0400, charset $0800, background 0, bank 0), which comes in as
// vic_view. The machine's own VIC registers stay what the program left.
//
// Shown while TX_CTRL.show, the machine is paused (halted: the text never stays over a running
// machine, whatever ended the freeze) and the mode is standard text (TX_VIEW's ECM, BMM,
// MCM 0), checked at every output line. The app sets show when its menu is up
// (end of C64::freeze()) and clears it before the program's bytes go back
// (start of C64::unfreeze()), T12 §11 Q3; the hardware does not guess. Drawn: 40 x 25 cells of 8 x 8 from the
// screen at bank + ($D018 & $F0) << 6 and the charset at bank + ($D018 & $0E)
// << 10 (the char ROM at bank-relative $1000-$1FFF in banks 0 and 2), bank =
// 3 - (CIA 2 PRA | ~DDRA)[1:0]; foreground the colour RAM nibble, background
// ($D021) transparent, or $D021 with TX_CTRL.opaque. Scroll, 38 columns/24
// rows, sprites, multicolour, ECM and bitmap are not drawn (the menu uses
// none). Colours: the C64 palette (pal_ram.v, as the scaler).
// Geometry (U16 D6): the mode's and scale's (hm_geom, from vic_scaler.v): the text area's first pixel at (XS, YS) = (xact0 + xt0,
// yact0 + yt0), hn / hd C64 pixels per output pixel (the picture's horizontal step) and tvn / tvd C64 lines per output line (the step
// the 50 Hz window has at that scale), each <= 1: one accumulator per axis, as the scaler's. 1080p at Fit (and Integer): 4 x 4, 1280 x
// 800 from 320 / 140 of the active area, as before U16 and as the app's pixel layer draws the C64 window there (trx_video.cc). Pixel
// repetition: every register of the pixel side changes only when ce is high (hdmi_timing.v).
//
// Fetch: when the first output line of C64 line l of the text area starts, the
// pixel side asks for line l + 1 (line 0 at the frame start) into the other
// half of a two-line buffer (2 x 40 cells of {colour, glyph byte}, LUT RAM
// written in cclk, read in clk). Per cell the fetcher reads the screen byte,
// then the glyph byte, and the colour nibble beside them: at least 4 clocks,
// some 165 for a line (5.2 us), of the at least one output line (25.7 us at 800 x 600, the shortest of
// U16 D2) it has. A line whose fetch is not in when it starts is not drawn (counted, TX_STATUS).
//
// Registers (trx_io page 6, 0x101A6000; clk_sys; 32 bit, the T21 rule: writing
// byte 3 commits, reading byte 0 takes bytes 1-3):
//   0C0 TX_CTRL   RW  b0 show, b1 opaque background ($D021); reads b31:24 = 2
//                     (the layer is there, its VIC view is TX_VIEW and no freeze session
//                     exists: 1 = the T12 layer with the session responder, older
//                     bitstreams read 0)
//   0C4 TX_STATUS R   b0 shown now (show, machine paused, text mode),
//                     b15:8 lines not drawn for a late fetch (wrapping)
// rdata is 0 outside these two (top.v ORs it with pix_layer's).
module c64_text (
    input  wire        clk_sys,
    input  wire        we,
    input  wire        re,
    input  wire [11:0] addr,
    input  wire [7:0]  wdata,
    output reg  [7:0]  rdata = 8'd0,

    input  wire        pal_clk,         // C64_PALETTE writes (T59), the app's bus clock
    input  wire        pal_we,
    input  wire [5:0]  pal_addr,
    input  wire [7:0]  pal_wdata,
    input  wire        cclk,
    input  wire        halted,          // c64_core: the machine is paused; the text is not drawn on a running machine
    input  wire [15:0] vic_view,        // trx_sys.v TX_VIEW
    output reg         port_req = 1'b0, // the C64 RAM's second port: wanted in this clock
    output wire [15:0] port_addr,
    output wire        port_rom,
    input  wire        port_grant,      // top.v: it is ours in this clock
    input  wire [7:0]  port_rdata,      // one clock after the grant
    output wire [9:0]  cram_addr,
    input  wire        cram_grant,      // top.v: the colour RAM port is ours in this clock (dma_win.v does not use it)
    input  wire [3:0]  cram_rdata,      // one clock after the grant

    input  wire        clk,
    input  wire        ce,              // pixel enable (hdmi_timing.v)
    input  wire [11:0] xact0,           // where the active area starts in cx / cy
    input  wire [10:0] yact0,
    input  wire [11:0] xt0,             // the text area's first pixel / line, relative to the active origin (hm_geom)
    input  wire [10:0] yt0,
    input  wire [5:0]  hn, hd,          // C64 pixels per output pixel: hn / hd (hm_geom)
    input  wire [5:0]  tvn, tvd,        // C64 lines per output line: tvn / tvd (hm_geom)
    input  wire [23:0] rgb_in,
    input  wire        de_in, hs_in, vs_in,
    input  wire [11:0] cx_in,
    input  wire [10:0] cy_in,
    output reg  [23:0] rgb,
    output reg         de, hs, vs,
    output reg  [11:0] cx,
    output reg  [10:0] cy
);
    // ======================= registers (clk_sys) ===============================
    reg  [1:0] r_ctrl = 2'b00;
    reg  [7:0] stg0 = 8'd0;
    reg [23:0] lat = 24'd0;
    reg  [7:0] n_late = 8'd0;
    reg        ready_c = 1'b0;          // cclk: everything but TX_CTRL.show says show
    (* ASYNC_REG = "TRUE" *) reg [1:0] s_rdy = 2'b00;
    (* ASYNC_REG = "TRUE" *) reg [2:0] s_late = 3'b000;
    wire       late_tgl;
    wire       sel = addr[11:3] == 9'h018;     // 0C0-0C7
    wire [31:0] cur = addr[2] ? {16'd0, n_late, 7'd0, r_ctrl[0] & s_rdy[1]}
                              : {8'h02, 22'd0, r_ctrl};
    always @(posedge clk_sys) begin
        s_rdy  <= {s_rdy[0], ready_c};
        s_late <= {s_late[1:0], late_tgl};
        if (s_late[2] != s_late[1]) n_late <= n_late + 8'd1;
        if (we && sel && addr[2:0] == 3'd0) stg0 <= wdata;
        if (we && sel && addr[2:0] == 3'd3) r_ctrl <= stg0[1:0];
        if (re && sel && addr[1:0] == 2'd0) lat <= cur[31:8];
        if (!sel)                     rdata <= 8'd0;
        else case (addr[1:0])
        2'd0:    rdata <= cur[7:0];
        2'd1:    rdata <= lat[7:0];
        2'd2:    rdata <= lat[15:8];
        default: rdata <= lat[23:16];
        endcase
    end

    // ======================= fetcher (cclk) ====================================
    wire       v_ecm = vic_view[15], v_bmm = vic_view[14], v_mcm = vic_view[13];
    wire [6:0] v_d018 = vic_view[12:6];         // $D018 bits 7:1
    wire [1:0] v_bank = ~vic_view[1:0];         // 3 - pins

    (* ASYNC_REG = "TRUE" *) reg [2:0] c_rq = 3'b000;
    wire       rq_tgl;
    wire [7:0] rq_line;
    reg  [7:0] f_line;
    reg  [9:0] f_ptr;                   // screen / colour RAM offset of the cell
    reg  [5:0] f_col;
    reg  [1:0] f_ph = 2'd0;             // 0 screen byte, 1 its data, 2 glyph byte, 3 its data
    reg        f_run = 1'b0;
    reg        got = 1'b0, cgot = 1'b0, have_col = 1'b0;
    reg  [7:0] code;
    reg  [3:0] colr;
    reg        done_tgl = 1'b0;
    reg  [7:0] done_line = 8'd0;
    wire       chr_rom = !v_bank[0] && v_d018[2:1] == 2'b01;   // char base $1000/$1800 in bank 0, 2
    wire [15:0] scr_a = {v_bank, v_d018[6:3], f_ptr};
    wire [15:0] chr_a = chr_rom ? {4'd0, v_d018[0], code, f_line[2:0]}
                                : {v_bank, v_d018[2:0], code, f_line[2:0]};
    assign port_addr = f_ph[1] ? chr_a : scr_a;
    assign port_rom  = f_ph[1] && chr_rom;
    assign cram_addr = f_ptr;

    (* ram_style = "distributed" *) reg [11:0] lb [0:127];

    always @(posedge cclk) begin
        ready_c <= halted && !v_ecm && !v_bmm && !v_mcm;  // a register before the synchronizers
        c_rq <= {c_rq[1:0], rq_tgl};
        got  <= port_req && port_grant;
        cgot <= f_run && !have_col && cram_grant && !cgot;
        if (cgot) begin colr <= cram_rdata; have_col <= 1'b1; end
        case (f_ph)
        2'd0: if (port_req && port_grant) begin port_req <= 1'b0; f_ph <= 2'd1; end
        2'd1: if (got) begin code <= port_rdata; port_req <= 1'b1; f_ph <= 2'd2; end
        2'd2: if (port_req && port_grant) begin port_req <= 1'b0; f_ph <= 2'd3; end
        default:
            if (got || !port_req) begin
                if (got) code <= port_rdata;    // the glyph byte, kept until written
                if (have_col || cgot) begin
                    lb[{f_line[0], f_col}] <= {cgot ? cram_rdata : colr, got ? port_rdata : code};
                    have_col <= 1'b0;
                    f_ptr <= f_ptr + 10'd1;
                    f_col <= f_col + 6'd1;
                    f_ph  <= 2'd0;
                    if (f_col == 6'd39) begin
                        f_run <= 1'b0;
                        done_line <= f_line;
                        done_tgl  <= ~done_tgl;
                    end else
                        port_req <= 1'b1;
                end
            end
        endcase
        // a request: the line rq_line (stable from its toggle on), from its first cell
        if (c_rq[2] != c_rq[1]) begin
            f_line   <= rq_line;
            f_ptr    <= {rq_line[7:3], 5'd0} + {2'd0, rq_line[7:3], 3'd0};   // 40 x row
            f_col    <= 6'd0;
            f_ph     <= 2'd0;
            f_run    <= 1'b1;
            port_req <= 1'b1;
            have_col <= 1'b0;
            cgot     <= 1'b0;
        end
    end

    // ======================= pixel side (clk) ==================================
    (* ASYNC_REG = "TRUE" *) reg [1:0] p_en = 2'b00, p_opq = 2'b00, p_rdy = 2'b00;
    (* ASYNC_REG = "TRUE" *) reg [3:0] p_bg0 = 4'd0, p_bg1 = 4'd0;
    (* ASYNC_REG = "TRUE" *) reg [2:0] p_done = 3'b000;
    reg        o_rq_tgl = 1'b0, o_late_tgl = 1'b0;
    reg  [7:0] o_rq_line = 8'd0;
    assign rq_tgl = o_rq_tgl; assign rq_line = o_rq_line; assign late_tgl = o_late_tgl;
    reg  [8:0] tag0 = 9'h1FF, tag1 = 9'h1FF;  // {invalid, line} of the buffer halves
    reg        act = 1'b0, first = 1'b0, line_ok = 1'b0, line_on = 1'b0;
    reg  [7:0] l;                        // C64 line of the text area
    reg  [5:0] tacc;                     // vertical accumulator: tvn per output line, a C64 line at tvd
    wire [6:0] tsum = {1'b0, tacc} + {1'b0, tvn};
    reg  [3:0] f_bg;
    reg        f_opq;
    wire       want = p_en[1] && p_rdy[1];
    // the text area's origin in this mode and scale (static while a frame runs)
    reg  [11:0] XS = 12'd0;
    reg  [10:0] YS = 11'd0;
    always @(posedge clk) if (ce) begin
        XS <= xact0 + xt0;
        YS <= yact0 + yt0;
    end
    always @(posedge clk) if (ce) begin
        p_en  <= {p_en[0], r_ctrl[0]};
        p_opq <= {p_opq[0], r_ctrl[1]};
        p_rdy <= {p_rdy[0], ready_c};
        p_bg0 <= vic_view[5:2]; p_bg1 <= p_bg0;
        p_done <= {p_done[1:0], done_tgl};
        if (p_done[2] != p_done[1]) begin
            if (done_line[0]) tag1 <= {1'b0, done_line}; else tag0 <= {1'b0, done_line};
        end
        if (cy_in == 11'd0 && cx_in == 12'd8) begin
            f_bg <= p_bg1; f_opq <= p_opq[1];
            tag0 <= 9'h1FF; tag1 <= 9'h1FF;
        end
        if (cy_in == 11'd0 && cx_in == 12'd12 && want) begin
            o_rq_line <= 8'd0; o_rq_tgl <= ~o_rq_tgl;
        end
        if (cx_in == 12'd16) begin
            first <= 1'b0;
            if (cy_in == 11'd0)
                act <= 1'b0;
            else if (cy_in == YS) begin
                act <= 1'b1; l <= 8'd0; tacc <= 6'd0; first <= 1'b1;
            end else if (act) begin
                if (tsum >= {1'b0, tvd}) begin
                    tacc <= tsum[5:0] - tvd;
                    if (l == 8'd199) act <= 1'b0;
                    else begin l <= l + 8'd1; first <= 1'b1; end
                end else
                    tacc <= tsum[5:0];
            end
        end
        if (cx_in == 12'd24) begin
            if (first) begin
                line_ok <= (l[0] ? tag1 : tag0) == {1'b0, l};
                if (want && (l[0] ? tag1 : tag0) != {1'b0, l}) o_late_tgl <= ~o_late_tgl;
                if (want && l != 8'd199) begin o_rq_line <= l + 8'd1; o_rq_tgl <= ~o_rq_tgl; end
            end
        end
        if (cx_in == 12'd32)
            line_on <= act && line_ok && want;
    end

    // pixels: E0 counters (for the pixel one clock behind cx_in), E1 line
    // buffer, E2 bit and colour, E3 palette, E4 out
    reg        h_on = 1'b0;
    reg  [5:0] hcol;
    reg  [2:0] hbit;
    reg  [5:0] hacc;
    wire [6:0] hsum = {1'b0, hacc} + {1'b0, hn};
    always @(posedge clk) if (ce)
        if (cx_in == XS) begin
            h_on <= line_on; hcol <= 6'd0; hbit <= 3'd0; hacc <= 6'd0;
        end else if (h_on) begin
            hacc <= hsum[5:0];
            if (hsum >= {1'b0, hd}) begin
                hacc <= hsum[5:0] - hd;
                hbit <= hbit + 3'd1;
                if (hbit == 3'd7) begin
                    hcol <= hcol + 6'd1;
                    if (hcol == 6'd39) h_on <= 1'b0;
                end
            end
        end

    (* shreg_extract = "no" *) reg [23:0] vrgb [1:4];   // flip-flops, not LUTs (SRL)
    (* shreg_extract = "no" *) reg [25:0] vctl [1:4];   // {de, hs, vs, cx, cy}
    reg [11:0] q1;
    reg [2:0]  b1;
    reg        on1, on2, on3;
    reg [3:0]  idx2;
    reg [23:0] c3;
    // the C64 palette (T59), as vic_scaler.v's: its own copy, the same writes
    wire [23:0] pal_rgb;
    pal_ram pal (.clk_w(pal_clk), .we(pal_we), .addr(pal_addr), .wdata(pal_wdata), .idx(idx2), .rgb(pal_rgb));
    integer i;
    always @(posedge clk) if (ce) begin
        vrgb[1] <= rgb_in; vctl[1] <= {de_in, hs_in, vs_in, cx_in, cy_in};
        for (i = 2; i <= 4; i = i + 1) begin vrgb[i] <= vrgb[i-1]; vctl[i] <= vctl[i-1]; end
        // E1
        q1  <= lb[{l[0], hcol}];
        b1  <= hbit;
        on1 <= h_on;
        // E2
        idx2 <= q1[3'd7 - b1] ? q1[11:8] : f_bg;
        on2  <= on1 && (q1[3'd7 - b1] || f_opq);
        // E3
        c3  <= pal_rgb;
        on3 <= on2;
        // E4
        rgb <= on3 ? c3 : vrgb[4];
        {de, hs, vs, cx, cy} <= vctl[4];
    end
endmodule
