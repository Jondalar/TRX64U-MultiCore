// SPDX-License-Identifier: GPL-3.0-or-later
// U16: the VIC-II picture in the HDMI mode hdmi_timing.v runs, at the scale the application chose (Integer, Fit, Fill), frame-locked
// to the VIC (U16 D1: the HDMI frame is exactly one VIC frame; cdce_mode.v sets the synthesizer so).
//
// Input: the VIC's native pixel stream (colour index, raster x, raster line) in its dot4x clock. Every clock writes the current colour
// at {row, x} into a ring of 64 lines (32K x 4 bits of block RAM); a raster x holds its last written value. The row is the raster
// line counted from the window's first line (row = line - Y0, modulo the frame's 312 / 263 lines), so the ring's slot sequence
// breaks only between the frame's last row and the window's first line, outside the window: every window row stays in its slot for
// 64 lines (50 Hz rows 256-269: 56, as the frame's 312 lines leave them), where the raster line as slot gave the 60 Hz rows 256-262
// (lines 287-293 counted on) only 7 before the next frame's lines 0-6 took their slots. The read takes the window row, vln.
//
// Output: no counters of its own. The picture rides on hdmi_timing.v's counters (t_cx / t_cy / t_de / t_hs / t_vs, the active area
// from xact0 / yact0): every register of the pixel side changes only when ce is high (one pixel lasts `gear` clocks), so rgb and the
// counters leave three pixels (three ce) after they came in.
//
// The window: 400 x 270 VIC pixels from raster (X0, Y0) at 50 Hz; 400 x 240 from (X0, Y0_60) at 60 Hz (T40, 65 x 263: the window runs
// from line 31 through 262 into 0-7 of the next frame). X0, Y0 and Y0_60 are the ones
// tools/hdmi_modes.py computes the geometry and the align lines with. hm_geom(mode, scale) (hdmi_modes.vh) gives the steps, hn / hd VIC
// pixels per output pixel and vn / vd VIC lines per output line (each <= 1), and the window's first output pixel / line (xw0, yw0)
// relative to the active origin. One accumulator per axis: the horizontal one is held at 0 for every pixel left of the window (and in
// the blanking), the vertical one for every line above it; inside, each output pixel (line) adds hn (vn) and moves on one VIC pixel
// (line) whenever the sum reaches hd (vd), so output pixel x shows VIC pixel floor((x - xw0) hn / hd). Each saturates at the window's
// end (400 pixels, 270 / 240 lines): the window ends there. The family (50 / 60 Hz) is the mode's (mode >= 6).
//
// Lock: the VIC side toggles when the VIC reaches raster line hm_align(mode, scale) (one per row and scale, mode and scale synced into
// vclk) and hands the line over with the toggle. When the toggle arrives with the line this side wants, `align` goes high and stays high
// until a ce takes it: hdmi_timing.v then restarts the frame at the active origin (the pixel after the next one is cx = xact0, cy =
// yact0), and `locked` is set. On the ce after the taking one both accumulators are forced to 0, as the next pixel is the active
// origin. A different align line (a scale change, U16 D3) drops `locked` and the frame is aligned again at the next VIC frame: the
// window is black until then. tools/hdmi_modes.py check proves for every row and scale that a VIC line is complete at least 3 lines
// before its first output pixel and read for the last time at least 3 lines before the ring comes round to it again.
//
// While blank is set (the machine pause stops the VIC) the window is black: the ring holds only the last 64 lines. c64_core resumes
// the VIC after whole frames, so the lock still holds afterwards.
module vic_scaler #(
    parameter       EDGE  = 1'b0     // 1: white 1-pixel outline around the active area
) (
    input  wire        vclk,
    input  wire [3:0]  pix,
    input  wire [9:0]  px,
    input  wire [8:0]  py,

    input  wire        clk,
    input  wire        rst,             // sync to clk
    input  wire        ce,              // pixel enable (hdmi_timing.v)
    input  wire        pal_clk,         // C64_PALETTE writes (T59), the app's bus clock
    input  wire        pal_we,
    input  wire [5:0]  pal_addr,
    input  wire [7:0]  pal_wdata,
    input  wire [3:0]  mode,            // clk: the row hdmi_timing runs (its mode_q; static while it runs)
    input  wire [1:0]  scale,           // clk: the scale in use (mode_req.v scale_pix; changes at a frame start)
    input  wire        t_de, t_hs, t_vs,
    input  wire [11:0] t_cx,
    input  wire [10:0] t_cy,
    input  wire [11:0] xact0,
    input  wire [10:0] yact0,
    input  wire [11:0] hact,
    input  wire [10:0] vact,
    input  wire [11:0] htot_m1,
    output reg  [81:0] geom = 82'd0,    // clk: hm_geom(mode, scale), registered; also for c64_text.v
    output reg         align = 1'b0,    // to hdmi_timing: restart the frame at the active origin (held until a ce)
    output reg  [23:0] rgb,
    output reg         de, hs, vs,
    output reg  [11:0] cx,              // the counters that go with rgb
    output reg  [10:0] cy,
    output reg         locked = 1'b0,
    input  wire        blank,           // C64 picture black (any clock)
    output reg  [9:0]  x_text,          // calibration: raster x of the first background pixel on line 200 (vclk)
    input  wire        scan             // T62 G09: darken the last output line of every VIC line (any clock, static)
);
`include "hdmi_modes.vh"
    localparam [9:0] X0    = 10'd96;      // tools/hdmi_modes.py X0
    localparam [8:0] Y0    = 9'd16;       // tools/hdmi_modes.py Y0[0]
    localparam [8:0] Y0_60 = 9'd31;       // tools/hdmi_modes.py Y0[1]
    (* ASYNC_REG = "TRUE" *) reg [5:0] ms_v1 = 6'd0, ms_v2 = 6'd0;     // {mode, scale} in vclk

    (* ASYNC_REG = "TRUE" *) reg [1:0] scan_s = 2'b00;
    always @(posedge clk) scan_s <= {scan_s[0], scan};

    // ---- write side (vclk) ---------------------------------------------------
    (* ram_style = "block" *) reg [3:0] ring [0:32767];
    wire       v60_v = ms_v2[5:2] >= 4'd6;            // the mode's family, in vclk (below)
    wire [8:0] y0_v  = v60_v ? Y0_60 : Y0;
    wire [8:0] wrow  = (py >= y0_v) ? py - y0_v : py + (v60_v ? 9'd263 : 9'd312) - y0_v;
    always @(posedge vclk)
        ring[{wrow[5:0], px[8:0]}] <= pix;

    // Calibration: where the left border (light blue, 14) turns into the
    // background (blue, 6) on raster line 200 (an empty text row: on the cursor
    // row the light blue cursor block would move it), i.e. the first text-area pixel.
    reg [3:0] pix_d;
    reg       x_found;
    always @(posedge vclk) begin
        pix_d <= pix;
        if (py != 9'd200) x_found <= 1'b0;
        else if (!x_found && pix_d == 4'd14 && pix == 4'd6) begin
            x_text  <= px;
            x_found <= 1'b1;
        end
    end

    // the align line of the mode and scale, in vclk; the toggle carries it (stable from the toggle to the next, one VIC frame)
    reg  [8:0] al_v = 9'd0;
    reg        at_al = 1'b0;
    reg        frame_tgl = 1'b0;
    reg  [8:0] frame_line = 9'd0;
    always @(posedge vclk) begin
        ms_v1 <= {mode, scale}; ms_v2 <= ms_v1;
        al_v  <= hm_align(ms_v2[5:2], ms_v2[1:0]);
        at_al <= py == al_v && px == 10'd0;
        if (py == al_v && px == 10'd0 && !at_al) begin frame_tgl <= ~frame_tgl; frame_line <= al_v; end
    end

    // ---- frame lock (clk) ----------------------------------------------------
    (* ASYNC_REG = "TRUE" *) reg [2:0] frame_sync = 3'b000;
    reg frame_seen = 1'b0;
    always @(posedge clk) begin
        frame_sync <= {frame_sync[1:0], frame_tgl};
        frame_seen <= frame_sync[2] ^ frame_sync[1];
    end

    (* ASYNC_REG = "TRUE" *) reg [1:0] blank_s = 2'b00;
    always @(posedge clk) blank_s <= {blank_s[0], blank};

    // the geometry and the align line of the mode and scale (both static while a frame runs)
    reg  [8:0] al_want = 9'd0, al_pend = 9'd0, al_have = 9'd0;
    reg        post = 1'b0;             // the ce after the one that took the align: the next pixel is the active origin
    always @(posedge clk) begin
        geom    <= hm_geom(mode, scale);
        al_want <= hm_align(mode, scale);
        if (rst) begin
            locked <= 1'b0; align <= 1'b0; post <= 1'b0;
        end else begin
            if (locked && al_want != al_have) locked <= 1'b0;
            if (!locked && !align && frame_seen && frame_line == al_want) begin align <= 1'b1; al_pend <= frame_line; end
            if (align && ce) begin align <= 1'b0; locked <= 1'b1; al_have <= al_pend; end
            if (ce) post <= align;
        end
    end
    wire [5:0]  hn  = geom[81:76], hd = geom[75:70], vn = geom[69:64], vd = geom[63:58];
    wire [11:0] xw0 = geom[45:34];
    wire [10:0] yw0 = geom[33:23];
    wire        w60 = mode >= 4'd6;
    wire [8:0]  wln = w60 ? 9'd240 : 9'd270;

    // ---- the window's position, per axis (state for the pixel / line now on t_cx / t_cy) -------------------
    wire [11:0] x     = t_cx - xact0;                // 0 .. hact - 1 while t_de
    wire [10:0] y     = t_cy - yact0;                // 0 .. vact - 1 on an active line
    wire        y_act = t_cy >= yact0;
    reg  [8:0]  hpx = 9'd0, vln = 9'd0;             // VIC pixel / line of the window (400 / wln: past its end)
    reg  [5:0]  hacc = 6'd0, vacc = 6'd0;
    wire [6:0]  hsum = {1'b0, hacc} + {1'b0, hn};
    wire [6:0]  vsum = {1'b0, vacc} + {1'b0, vn};
    wire        h_in = t_de && x >= xw0;
    wire        v_in = y_act && y >= yw0;
    always @(posedge clk) if (ce) begin
        if (post || !h_in) begin
            hpx <= 9'd0; hacc <= 6'd0;
        end else if (hpx != 9'd400) begin
            if (hsum >= {1'b0, hd}) begin hacc <= hsum[5:0] - hd; hpx <= hpx + 9'd1; end
            else hacc <= hsum[5:0];
        end
        if (post) begin
            vln <= 9'd0; vacc <= 6'd0;
        end else if (t_cx == htot_m1) begin             // the last pixel of the line: the state for the next line
            if (!v_in) begin
                vln <= 9'd0; vacc <= 6'd0;
            end else if (vln != wln) begin
                if (vsum >= {1'b0, vd}) begin vacc <= vsum[5:0] - vd; vln <= vln + 9'd1; end
                else vacc <= vsum[5:0];
            end
        end
    end

    // ---- pipeline: address, block RAM, palette -----------------------------------
    wire        s0_win = h_in && v_in && hpx != 9'd400 && vln != wln && locked && !blank_s[1];
    wire [9:0]  sx     = X0 + {1'b0, hpx};
    // T62 G09: the output line after which vln moves on is the last of its VIC line
    wire        s0_last = vsum >= {1'b0, vd};
    wire        s0_edge = EDGE && t_de && (x == 12'd0 || x == hact - 12'd1 || y == 11'd0 || y == vact - 11'd1);

    reg [14:0] s1_addr;
    reg [2:0]  s1_ctl, s2_ctl;                      // {de, hs, vs}
    reg [11:0] s1_h, s2_h;
    reg [10:0] s1_v, s2_v;
    reg        s1_win, s2_win;
    reg        s1_edge, s2_edge;
    reg        s1_last, s2_last;
    reg [3:0]  s2_col;
    // the C64 palette (T59): pal_ram.v, Colodore until the app writes C64_PALETTE
    wire [23:0] pal_rgb;
    pal_ram pal (.clk_w(pal_clk), .we(pal_we), .addr(pal_addr), .wdata(pal_wdata), .idx(s2_col), .rgb(pal_rgb));
    always @(posedge clk) if (ce) begin
        s1_addr <= {vln[5:0], sx[8:0]};
        s1_ctl  <= {t_de, t_hs, t_vs};
        s1_h    <= t_cx;
        s1_v    <= t_cy;
        s1_win  <= s0_win;
        s1_edge <= s0_edge;
        s1_last <= s0_last;
        s2_last <= s1_last;
        s2_col  <= ring[s1_addr];
        s2_ctl  <= s1_ctl;
        s2_h    <= s1_h;
        s2_v    <= s1_v;
        s2_win  <= s1_win;
        s2_edge <= s1_edge;
        rgb     <= s2_edge ? 24'hFFFFFF
                 : s2_win  ? ((scan_s[1] && s2_last) ? {2'b00, pal_rgb[23:18], 2'b00, pal_rgb[15:10], 2'b00, pal_rgb[7:2]} : pal_rgb)
                 : 24'h000000;
        {de, hs, vs} <= s2_ctl;
        cx      <= s2_h;
        cy      <= s2_v;
    end

endmodule
