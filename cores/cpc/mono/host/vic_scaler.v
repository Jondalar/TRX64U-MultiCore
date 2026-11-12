// VIC-II picture to 1920x1080 at 4x in both directions, frame-locked to the VIC.
//
// Input: the VIC's native pixel stream (colour index, raster x, raster line) in
// its dot4x clock. Every clock writes the current colour at {line, x} into a
// ring of 64 raster lines (32K x 4 bits of block RAM); a raster x holds its
// last written value.
// Output: 1080p with 2640 x 1125 clocks per frame (CEA 1080p50 blanking), so the
// pixel clock is 6875/1456 of dot4x (148.87 MHz at PAL, 50.12 Hz, one output
// frame per VIC frame), one pixel per clock with the counters hdmi.v takes. The
// counters keep the active area at cx 768-2687 for everything downstream, so
// cx runs 0-695 and then jumps to 744. A window of
// 400 x 270 VIC pixels (X0.., Y0..) becomes 1600 x 1080, centred with black
// bars left and right.
// 60 Hz System Modes (T40, v60: NTSC and PAL-60, 65 x 263): 2200 x 1125 clocks
// per frame (CEA 1080p60 blanking), pixel clock 61875/13676 of dot4x; cx runs
// 0-255 and then jumps to 744. A window of 400 x 240 VIC pixels (X0,
// Y0_60..) at 4x horizontally and 4.5x vertically (VIC lines take 5, 4, 5, 4
// output lines). The window runs from line 31 through 262 into 0-7 of the
// next frame (20 border lines above and below the text area): raster lines
// below Y0_60 count as 263 more. Lock as for 50 Hz, at line Y0_60 + 4.
// While blank is set (the machine pause stops the VIC) the window is black:
// the ring holds only the last 64 lines. c64_core resumes the VIC after whole
// frames, so the lock still holds afterwards.
// Lock: output line 0 (cy 45) starts when the VIC reaches raster line ALIGN. With
// ALIGN = Y0 + 4, a VIC line is complete before its first output line and read
// for the last time long before the ring comes round to it again (the output
// window takes 270 VIC lines in 1080/1125 of a frame, the VIC 270/312).
//
// T164 WP8 (EXT = 1, the host's hdmi_out.v): no counters of its own. The picture rides on hdmi_timing.v's counters (t_cx / t_cy / t_de / t_hs /
// t_vs, active area from xact0 / yact0, 1920 x 1080 rows only; `en` is the row being one of them). The frame lock becomes the `align`
// pulse to hdmi_timing (once per lock, cleared by rst / relock / !en): the timing restarts at the origin of its active area on the VIC's
// line ALIGN, as `frame_seen && !locked` did here. The VIC line of the window follows from the line counter (ln / acc advance in the first
// clock of every new t_cy, three clocks of hold after an align because t_cy jumps). Output: rgb and the counters three clocks later.
// EXT = 0 (default) is the module as it was, for the large cores' tops until they take hdmi_out.v (WP18).
module vic_scaler #(
    parameter       EXT   = 1'b0,
    parameter [9:0] X0    = 10'd96,
    parameter [8:0] Y0    = 9'd16,
    parameter [8:0] ALIGN = 9'd20,
    parameter [8:0] Y0_60 = 9'd31,
    parameter       EDGE  = 1'b0     // 1: white 1-pixel outline around the 1920x1080
) (
    input  wire        vclk,
    input  wire [3:0]  pix,
    input  wire [9:0]  px,
    input  wire [8:0]  py,

    input  wire        clk,
    input  wire        rst,             // sync to clk
    input  wire        pal_clk,         // C64_PALETTE writes (T59), the app's bus clock
    input  wire        pal_we,
    input  wire [5:0]  pal_addr,
    input  wire [7:0]  pal_wdata,
    output reg  [23:0] rgb,
    output reg         de, hs, vs,
    output reg  [11:0] cx,              // counters that go with rgb (for hdmi.v)
    output reg  [10:0] cy,
    output reg         locked,
    input  wire        blank,           // C64 picture black (any clock)
    output reg  [9:0]  x_text,          // calibration: raster x of the first background pixel on line 200 (vclk)
    input  wire        v60,             // 65 x 263 VIC (any clock; changes only around a reset of clk's side)
    input  wire        scan,            // T62 G09: darken the last output line of every VIC line (any clock, static)
    input  wire        relock,          // T79.1: lock to the VIC's frame again at its next frame (clk, one clock); the
                                        // output timing runs on meanwhile, so the screen keeps its signal
    // EXT = 1 only (leave unconnected otherwise)
    input  wire        en,              // static: the row is a 1920 x 1080 one (hm_locked)
    input  wire        t_de, t_hs, t_vs,
    input  wire [11:0] t_cx,
    input  wire [10:0] t_cy,
    input  wire [11:0] xact0,
    input  wire [10:0] yact0,
    output wire        align            // pulse to hdmi_timing: restart the frame at its active origin
);
    (* ASYNC_REG = "TRUE" *) reg [1:0] scan_s = 2'b00;
    always @(posedge clk) scan_s <= {scan_s[0], scan};
    (* ASYNC_REG = "TRUE" *) reg [1:0] v60_vs = 2'b00, v60_s = 2'b00;
    always @(posedge vclk) v60_vs <= {v60_vs[0], v60};
    always @(posedge clk)  v60_s  <= {v60_s[0], v60};
    wire v60_v = v60_vs[1];
    wire w60   = v60_s[1];
    // ---- write side (vclk) ---------------------------------------------------
    (* ram_style = "block" *) reg [3:0] ring [0:32767];
    always @(posedge vclk)
        ring[{py[5:0], px[8:0]}] <= pix;

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

    reg frame_tgl = 1'b0;
    always @(posedge vclk)
        if (py == (v60_v ? Y0_60 + 9'd4 : ALIGN) && px == 10'd0) frame_tgl <= ~frame_tgl;

    // ---- frame lock (clk) ----------------------------------------------------
    (* ASYNC_REG = "TRUE" *) reg [2:0] frame_sync = 3'b000;
    reg frame_seen;
    always @(posedge clk) begin
        frame_sync <= {frame_sync[1:0], frame_tgl};
        frame_seen <= frame_sync[2] ^ frame_sync[1];
    end

    (* ASYNC_REG = "TRUE" *) reg [1:0] blank_s = 2'b00;
    always @(posedge clk) blank_s <= {blank_s[0], blank};

    // ---- 1080p timing, one pixel per clock, counters as hdmi.v takes them ------
    // cx 0..2687 with a jump to 744 after the last clock of the blanking that
    // precedes it (H_JUMP): 50 Hz 0-695 then 744-2687 (2640 clocks: front porch
    // 0-527, sync 528-571, back porch 572-695 and 744-767); 60 Hz 0-255 then
    // 744-2687 (2200 clocks: front porch 0-87, sync 88-131, back porch 132-255
    // and 744-767); active 768-2687 in both. cy 0..1124: front porch 0-3, sync
    // 4-8, back porch 9-44, active 45-1124. Sync pulses positive.
    localparam H_TOT = 2688, H_ACT0 = 768, H_TO = 744;
    localparam H_JUMP = 695, H_SYNC0 = 528, H_SYNC1 = 572;
    localparam H_JUMP_60 = 255, H_SYNC0_60 = 88, H_SYNC1_60 = 132;
    localparam V_TOT = 1125, V_ACT0 = 45,  V_SYNC0 = 4,   V_SYNC1 = 9;
    localparam X_WIN0 = 160, X_WIN1 = 1760;        // 1600 of the 1920 pixels
    reg [11:0] h;
    reg [10:0] v;
    // VIC line of the window for the current output line: ln, with acc the
    // phase of 4 (50 Hz: +1 per line, next VIC line at 4) or 4.5 (60 Hz: +2,
    // next at 9) output lines per VIC line
    reg [8:0] ln;
    reg [3:0] acc;
    wire [3:0] acc_n = acc + (w60 ? 4'd2 : 4'd1);
    wire [3:0] acc_t = w60 ? 4'd9 : 4'd4;
    generate if (!EXT) begin : own_counters
        always @(posedge clk) begin
            if (rst) begin
                h <= 0; v <= 0; locked <= 1'b0; ln <= 9'd0; acc <= 4'd0;
            end else if (relock) begin
                locked <= 1'b0;
            end else if (frame_seen && !locked) begin
                h <= H_ACT0; v <= V_ACT0; locked <= 1'b1;   // output line 0 starts now
                ln <= 9'd0; acc <= 4'd0;
            end else if (h == H_TOT - 1) begin
                h <= 0;
                v <= (v == V_TOT - 1) ? 11'd0 : v + 11'd1;
                if (v == V_ACT0 - 1) begin
                    ln <= 9'd0; acc <= 4'd0;
                end else if (v >= V_ACT0) begin
                    if (acc_n >= acc_t) begin acc <= acc_n - acc_t; ln <= ln + 9'd1; end
                    else acc <= acc_n;
                end
            end else if (h == (w60 ? H_JUMP_60 : H_JUMP))
                h <= H_TO;
            else
                h <= h + 12'd1;
        end

    assign align = 1'b0;
    end else begin : ext_counters
        // h / v are the timing's counters; the line of the window follows t_cy
        always @* begin h = t_cx; v = t_cy; end
        reg [10:0] cy_q = 11'd0;
        reg [1:0]  hold = 2'd0;
        assign align = frame_seen && !locked && en;
        always @(posedge clk) begin
            cy_q <= t_cy;
            if (rst || !en) begin
                locked <= 1'b0; ln <= 9'd0; acc <= 4'd0; hold <= 2'd0;
            end else if (relock) begin
                locked <= 1'b0;
            end else if (align) begin
                locked <= 1'b1; ln <= 9'd0; acc <= 4'd0; hold <= 2'd3;
            end else if (hold != 2'd0) begin
                hold <= hold - 2'd1; ln <= 9'd0; acc <= 4'd0;
            end else if (t_cy != cy_q) begin             // the first clock of a line: cy_q is the one that has just ended
                if (cy_q == yact0 - 11'd1) begin
                    ln <= 9'd0; acc <= 4'd0;
                end else if (cy_q >= yact0) begin
                    if (acc_n >= acc_t) begin acc <= acc_n - acc_t; ln <= ln + 9'd1; end
                    else acc <= acc_n;
                end
            end
        end
    end endgenerate

    // ---- pipeline: address, block RAM, palette -----------------------------------
    wire        s0_act = EXT ? t_de : (h >= H_ACT0) && (v >= V_ACT0);
    wire [11:0] x      = EXT ? h - xact0 : h - H_ACT0;   // 0..1919 while active
    wire [10:0] y      = EXT ? v - yact0 : v - V_ACT0;   // 0..1079 while active
    wire        s0_win = s0_act && (x >= X_WIN0) && (x < X_WIN1) && !blank_s[1];
    wire        s0_hs  = EXT ? t_hs : w60 ? (h >= H_SYNC0_60) && (h < H_SYNC1_60) : (h >= H_SYNC0) && (h < H_SYNC1);
    wire        s0_vs  = EXT ? t_vs : (v >= V_SYNC0) && (v < V_SYNC1);
    wire [11:0] xw     = x - X_WIN0;
    wire [9:0]  sx     = X0 + {1'b0, xw[10:2]};       // four pixels per VIC pixel
    wire [8:0]  sy_v   = (w60 ? Y0_60 : Y0) + ln;    // 4 or 4.5 output lines per VIC line
    wire [8:0]  sy     = (w60 && sy_v >= 9'd263) ? sy_v - 9'd263 : sy_v;

    reg [14:0] s1_addr;
    reg [2:0]  s1_ctl, s2_ctl;                      // {de, hs, vs}
    reg [11:0] s1_h, s2_h;
    reg [10:0] s1_v, s2_v;
    reg        s1_win, s2_win;
    reg        s1_edge, s2_edge;
    // T62 G09: the output line after which ln advances is the last of its VIC line
    wire        s0_last = acc_n >= acc_t;
    reg         s1_last, s2_last;
    wire        s0_edge = EDGE && s0_act && (x == 0 || x == 1919 || y == 0 || y == 1079);
    reg [3:0]  s2_col;
    // the C64 palette (T59): pal_ram.v, Colodore until the app writes C64_PALETTE
    wire [23:0] pal_rgb;
    pal_ram pal (.clk_w(pal_clk), .we(pal_we), .addr(pal_addr), .wdata(pal_wdata), .idx(s2_col), .rgb(pal_rgb));
    always @(posedge clk) begin
        s1_addr <= {sy[5:0], sx[8:0]};
        s1_ctl  <= {s0_act, s0_hs, s0_vs};
        s1_h    <= h;
        s1_v    <= v;
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
