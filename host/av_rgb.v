// T265 P4: the host's RGB out on the A/V port. The pixel layer's shown buffer (the host menu, or the picture of a frame-buffer core) at
// 15 kHz, in sys_clk (dot4x), black outside the window; csync on SYNCn, FB high while RGB is on; the words are the 565 pixel widened by bit
// repetition, x 11/16 (shifts and adds, no DSP). The names map, MSB first: AVID_R[9:2] = R, AVID_G[9:2] = G, AVID_B[9:2] = B (top.v).
//
// Raster: its own, 4 sys_clk per dot, 504 x 312 (50 Hz) or 520 x 263 (60 Hz), hsync 37 / 38 dots, three broad vsync lines (the sync
// timing of cores/c128/mono/c128_avout.v). The window is 1600 sys_clk (400 dots) wide, 91 dots after the sync's edge, 288 (50 Hz) or
// 240 (60 Hz) lines from line 22 / 18; the source picture (W <= 960 pixels) is spread over the window by a phase accumulator (nearest),
// one output sample per sys_clk. Standard: r_std 1 = 50 Hz, 2 = 60 Hz, 0 and 3 = the core's raster as measured (pix_y: lines per frame).
// Vertical: a source taller than 1.25 x the window's lines (the menu at 540 / 576 / 480 / 600, the Apple II at 384) goes out with two lines
// averaged into one; any other height 1:1, centred, black bars above and below or cropped.
//
// Source lines come from the DDR2 through pix_layer's fetcher (the a_* ports, ui_clk): at hc 8 of line L the line(s) of output line L+1
// are asked for into line RAM A (the first line) and B (the second line of a pair), pair L+1 mod 2; the output reads A and B at the same
// address every clock, so no word is held. Only format 1 (RGB565) is shown; another format leaves the port black.
module av_rgb (
    // the app's register (cpu_clk): mode 0 off / 1 RGB, std 0 follow the core / 1 50 Hz / 2 60 Hz
    input  wire         cpu_clk,
    input  wire [1:0]   r_mode,
    input  wire [1:0]   r_std,
    // the pixel layer's registers (cpu_clk) and their commit toggles
    input  wire [6:0]   r_ctrl,
    input  wire [15:0]  r_w, r_h, r_stride,
    input  wire [31:0]  r_front, r_back,
    input  wire         cfg_tgl, front_tgl, back_tgl,
    // the core's raster (sys_clk), for std 0
    input  wire [8:0]   pix_y,
    // the fetcher: the request (toggle, data stable until the next one), its completion and the line RAMs' write port (ui_clk)
    output reg          a_rq_tgl = 1'b0,
    output reg  [26:4]  a_rq_addr = 23'd0,
    output reg  [15:0]  a_rq_stride = 16'd0,
    output reg  [7:0]   a_rq_n = 8'd0,
    output reg          a_rq_two = 1'b0,
    output reg          a_rq_pair = 1'b0,
    input  wire         a_done_tgl,
    input  wire         ui_clk,
    input  wire         a_we_a, a_we_b,
    input  wire [7:0]   a_wa,
    input  wire [127:0] a_wd,
    // sys_clk
    input  wire         clk,
    (* IOB = "TRUE" *) output reg [7:0] r_o = 8'd0,
    (* IOB = "TRUE" *) output reg [7:0] g_o = 8'd0,
    (* IOB = "TRUE" *) output reg [7:0] b_o = 8'd0,
    (* IOB = "TRUE" *) output reg       syn_n = 1'b1,
    (* IOB = "TRUE" *) output reg       fb = 1'b0
);
    // ---- crossings into sys_clk
    (* ASYNC_REG = "TRUE" *) reg [3:0] mo1 = 4'd0, mo2 = 4'd0;
    (* ASYNC_REG = "TRUE" *) reg [2:0] s_cfg = 3'd0, s_fr = 3'd0, s_bk = 3'd0, s_dn = 3'd0;
    always @(posedge clk) begin
        mo1 <= {r_std, r_mode}; mo2 <= mo1;
        s_cfg <= {s_cfg[1:0], cfg_tgl}; s_fr <= {s_fr[1:0], front_tgl}; s_bk <= {s_bk[1:0], back_tgl}; s_dn <= {s_dn[1:0], a_done_tgl};
    end
    reg [6:0]  c_ctrl = 7'd0;
    reg [9:0]  c_w = 10'd0;
    reg [10:0] c_h = 11'd0;
    reg [15:0] c_stride = 16'd0;
    reg [26:4] c_show = 23'd0;
    always @(posedge clk) begin
        if (s_cfg[2] != s_cfg[1]) begin
            c_ctrl <= r_ctrl; c_w <= (r_w > 16'd960) ? 10'd960 : r_w[9:0]; c_h <= (r_h > 16'd1080) ? 11'd1080 : r_h[10:0]; c_stride <= r_stride;
        end
        if (s_fr[2] != s_fr[1]) c_show <= r_front[26:4];
        if (s_bk[2] != s_bk[1]) c_show <= r_back[26:4];
    end
    wire on = mo2[1:0] == 2'd1;

    // std 0: the core's raster as measured, the highest line number of a frame of pix_y
    reg [8:0] py_d = 9'd0, py_max = 9'd311, py_run = 9'd0;
    always @(posedge clk) begin
        py_d <= pix_y;
        if (pix_y == 9'd0 && py_d != 9'd0) begin py_max <= py_run; py_run <= 9'd0; end
        else if (pix_y > py_run) py_run <= pix_y;
    end
    wire s60 = (mo2[3:2] == 2'd2) || ((mo2[3:2] == 2'd0 || mo2[3:2] == 2'd3) && py_max < 9'd288);

    // ---- the raster: hc in sys_clk, 4 per dot; ly the line
    wire [11:0] HT4 = s60 ? 12'd2080 : 12'd2016;     // 65 / 63 cycles x 8 dots x 4
    wire [8:0]  LT  = s60 ? 9'd263 : 9'd312;
    wire [11:0] HS4 = s60 ? 12'd152 : 12'd148;       // hsync 38 / 37 dots
    localparam [11:0] HA0 = 12'd364, NW = 12'd1600;  // the window: 91 dots after the sync's edge, 400 dots
    wire [8:0]  VA0 = s60 ? 9'd18 : 9'd22;           // first window line (lines 0-2: broad pulses)
    wire [8:0]  ACT = s60 ? 9'd240 : 9'd288;
    reg  [11:0] hc = 12'd0;
    reg  [8:0]  ly = 9'd0;
    always @(posedge clk) begin
        if (hc >= HT4 - 12'd1) begin
            hc <= 12'd0;
            ly <= (ly >= LT - 9'd1) ? 9'd0 : ly + 9'd1;
        end else hc <= hc + 12'd1;
    end
    wire vs_l = ly < 9'd3;
    wire cs_w = vs_l ? (hc < HT4 - HS4) : (hc < HS4);   // csync active (low on the pin)
    wire [8:0] lv = ly - VA0;                            // the window line (wraps above the line before the first)
    wire act_l = ly >= VA0 && lv < ACT;

    // ---- per frame (at line 3): what is shown and where
    reg        f_en = 1'b0, f_two = 1'b0;
    reg [9:0]  f_w = 10'd0;
    reg [7:0]  f_n = 8'd0;
    reg [8:0]  f_top = 9'd0, f_rows = 9'd0;              // black lines above, lines shown
    reg [10:0] f_src0 = 11'd0;                           // first source line
    reg [15:0] f_stride = 16'd0;
    reg [26:4] f_show = 23'd0;
    wire [10:0] act_x = {2'b00, ACT};
    wire        two_n = c_h > act_x + {2'b00, ACT[8:2]};        // taller than 1.25 x the window: two lines into one
    wire [10:0] hs_n  = two_n ? {1'b0, c_h[10:1]} : c_h;
    wire        crop  = hs_n > act_x;
    wire [10:0] over  = hs_n - act_x;
    always @(posedge clk) if (hc == 12'd0 && ly == 9'd3) begin
        f_en   <= on && c_ctrl[0] && c_ctrl[3:1] == 3'd1 && c_w != 10'd0 && c_h != 11'd0;
        f_two  <= two_n;
        f_w    <= c_w;
        f_n    <= {1'b0, c_w[9:3]} + {7'd0, c_w[2:0] != 3'd0};
        f_rows <= crop ? ACT : hs_n[8:0];
        f_top  <= crop ? 9'd0 : (ACT - hs_n[8:0]) >> 1;
        f_src0 <= crop ? (two_n ? {over[10:1], 1'b0} : {1'b0, over[10:1]}) : 11'd0;
        f_stride <= c_stride;
        f_show <= c_show;
    end

    // ---- line requests: at hc 8 of line L the source line(s) of window line L + 1, into pair (L + 1) mod 2
    wire [8:0]  ln    = lv + 9'd1;
    wire        ln_in = (ln >= f_top) && (ln < f_top + f_rows);
    reg [26:4]  la = 23'd0;                              // the first source line of the next request, in 16 byte units
    reg [10:0]  kk = 11'd0;
    wire [26:4] st16 = {7'd0, f_stride[15:4]};
    always @(posedge clk) begin
        if (hc == 12'd1 && ly == 9'd3) begin la <= f_show; kk <= f_src0; end
        else if (kk != 11'd0) begin la <= la + st16; kk <= kk - 11'd1; end
        if (hc == 12'd8 && f_en && (ly + 9'd1 >= VA0) && ln_in) begin
            a_rq_addr <= la; a_rq_stride <= f_stride; a_rq_n <= f_n; a_rq_two <= f_two; a_rq_pair <= ln[0];
            a_rq_tgl <= ~a_rq_tgl;
            la <= la + (f_two ? {6'd0, f_stride[15:4], 1'b0} : st16);
        end
    end
    reg ok0 = 1'b0, ok1 = 1'b0;                          // pair 0 / 1 holds the line of the window line that reads it
    always @(posedge clk) begin
        if (hc == 12'd8) begin if (ln[0]) ok1 <= 1'b0; else ok0 <= 1'b0; end
        if (s_dn[2] != s_dn[1]) begin if (a_rq_pair) ok1 <= 1'b1; else ok0 <= 1'b1; end
    end

    // ---- the line RAMs: pair x 128 words x 128 bit, write ui_clk, read clk every clock (A: the first line, B: the second line of a pair)
    (* ram_style = "block" *) reg [127:0] lba [0:255];
    (* ram_style = "block" *) reg [127:0] lbb [0:255];
    always @(posedge ui_clk) begin
        if (a_we_a) lba[a_wa] <= a_wd;
        if (a_we_b) lbb[a_wa] <= a_wd;
    end

    // ---- the window: the phase accumulator gives the source pixel of each sys_clk
    reg        h_on = 1'b0, l_ok = 1'b0, row_pair = 1'b0;
    reg [9:0]  pidx = 10'd0;
    reg [11:0] hacc = 12'd0, hcnt = 12'd0;
    wire       row_on = act_l && f_en && (lv >= f_top) && (lv < f_top + f_rows);
    wire [11:0] hsum = hacc + {2'b00, f_w};
    always @(posedge clk) begin
        if (hc == HA0 - 12'd1) begin
            h_on <= row_on; l_ok <= lv[0] ? ok1 : ok0; row_pair <= lv[0]; pidx <= 10'd0; hacc <= 12'd0; hcnt <= NW - 12'd1;
        end else if (h_on) begin
            if (hsum >= NW) begin hacc <= hsum - NW; pidx <= pidx + 10'd1; end
            else hacc <= hsum;
            if (hcnt == 12'd0) h_on <= 1'b0; else hcnt <= hcnt - 12'd1;
        end
    end

    // stage 1: the RAMs' output registers (qa / qb) and the lane; stage 2: widen, add; stage 3: average, x 11/16, the pin registers
    reg [127:0] qa, qb;
    reg [2:0]   lane1 = 3'd0;
    reg         two1 = 1'b0;
    reg [1:0]   on_p = 2'd0;
    always @(posedge clk) begin
        qa    <= lba[{row_pair, pidx[9:3]}];
        qb    <= lbb[{row_pair, pidx[9:3]}];
        lane1 <= pidx[2:0];
        two1  <= f_two;
        on_p  <= {on_p[0], h_on && l_ok};
    end
    wire [15:0] pa = qa[16*lane1 +: 16];
    wire [15:0] pb = two1 ? qb[16*lane1 +: 16] : pa;
    wire [23:0] wa8 = {pa[15:11], pa[15:13], pa[10:5], pa[10:9], pa[4:0], pa[4:2]};
    wire [23:0] wb8 = {pb[15:11], pb[15:13], pb[10:5], pb[10:9], pb[4:0], pb[4:2]};
    reg [8:0] sr = 9'd0, sg = 9'd0, sb = 9'd0;
    always @(posedge clk) begin
        sr <= {1'b0, wa8[23:16]} + {1'b0, wb8[23:16]};
        sg <= {1'b0, wa8[15:8]}  + {1'b0, wb8[15:8]};
        sb <= {1'b0, wa8[7:0]}   + {1'b0, wb8[7:0]};
    end
    // x 11/16 = (8x + 2x + x) / 16 of the average
    function [7:0] lvl(input [8:0] sum);
        reg [7:0] x;
        reg [11:0] t;
        begin x = sum[8:1]; t = {1'b0, x, 3'd0} + {3'd0, x, 1'b0} + {4'd0, x}; lvl = t[11:4]; end
    endfunction
    reg [1:0] cs_d = 2'd0;
    always @(posedge clk) begin
        cs_d  <= {cs_d[0], cs_w};
        r_o   <= (on && on_p[1]) ? lvl(sr) : 8'd0;
        g_o   <= (on && on_p[1]) ? lvl(sg) : 8'd0;
        b_o   <= (on && on_p[1]) ? lvl(sb) : 8'd0;
        syn_n <= on ? ~cs_d[1] : 1'b1;
        fb    <= on;
    end
endmodule
