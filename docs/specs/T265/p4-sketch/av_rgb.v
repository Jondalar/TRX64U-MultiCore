// T265 P4 SKETCH (cost estimate only, not product code): the host's RGB out on the A/V port.
// The pixel layer's buffer (the host menu or a frame-buffer core) at 15 kHz, in sys_clk (dot4x), black outside the window,
// csync on SYNCn, FB high in RGB mode. Levels: palette x 11/16 (the Sony-RGB preset, T259 5). Names map, MSB first.
module av_rgb #(parameter BLEND = 1, parameter FMT2 = 0) (
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
    // the fetcher (request in clk, done from ui_clk; the line RAM's write port in ui_clk)
    output reg          a_rq_tgl,
    output reg  [26:4]  a_rq_addr,
    output reg  [15:0]  a_rq_stride,
    output reg  [7:0]   a_rq_n,
    output reg          a_rq_two,         // two lines (S = 2)
    output reg          a_rq_pair,        // slots {2p, 2p+1}
    input  wire         a_done_tgl,
    input  wire         ui_clk,
    input  wire         a_we,
    input  wire [8:0]   a_wa,
    input  wire [127:0] a_wd,
    // sys_clk
    input  wire         clk,
    output reg  [7:0]   r_o, g_o, b_o,
    output reg          syn_n,
    output reg          fb
);
    // ---- crossings
    (* ASYNC_REG = "TRUE" *) reg [3:0] mo1 = 0, mo2 = 0;
    (* ASYNC_REG = "TRUE" *) reg [2:0] s_cfg = 0, s_fr = 0, s_bk = 0, s_dn = 0;
    always @(posedge clk) begin
        mo1 <= {r_std, r_mode}; mo2 <= mo1;
        s_cfg <= {s_cfg[1:0], cfg_tgl}; s_fr <= {s_fr[1:0], front_tgl}; s_bk <= {s_bk[1:0], back_tgl}; s_dn <= {s_dn[1:0], a_done_tgl};
    end
    reg [6:0]  c_ctrl = 0;
    reg [9:0]  c_w = 0;
    reg [10:0] c_h = 0;
    reg [15:0] c_stride = 0;
    reg [26:4] c_show = 0;
    always @(posedge clk) begin
        if (s_cfg[2] != s_cfg[1]) begin c_ctrl <= r_ctrl; c_w <= (r_w > 16'd960) ? 10'd960 : r_w[9:0]; c_h <= (r_h > 16'd1080) ? 11'd1080 : r_h[10:0]; c_stride <= r_stride; end
        if (s_fr[2] != s_fr[1]) c_show <= r_front[26:4];
        if (s_bk[2] != s_bk[1]) c_show <= r_back[26:4];
    end
    wire on  = mo2[1:0] == 2'd1;
    // std 0: the core's raster as measured (lines per frame of pix_y)
    reg [8:0] py_d = 0, py_max = 9'd311, py_run = 0;
    always @(posedge clk) begin
        py_d <= pix_y;
        if (pix_y == 9'd0 && py_d != 9'd0) begin py_max <= py_run; py_run <= 9'd0; end
        else if (pix_y > py_run) py_run <= pix_y;
    end
    wire s60 = (mo2[3:2] == 2'd2) || (mo2[3:2] == 2'd0 && py_max < 9'd288);

    // ---- the raster: hc in sys_clk (4 per dot), ly lines
    wire [11:0] HT4 = s60 ? 12'd2080 : 12'd2016;     // 65 / 63 cycles x 8 dots x 4
    wire [8:0]  LT  = s60 ? 9'd263 : 9'd312;
    wire [11:0] HS4 = s60 ? 12'd152 : 12'd148;       // hsync 38 / 37 dots
    localparam [11:0] HA0 = 12'd364, NW = 12'd1600;  // the window: 91 dots after the sync's edge, 400 dots
    wire [8:0]  VA0 = s60 ? 9'd18 : 9'd22;           // first active line (lines 0-2 broad pulses)
    wire [8:0]  ACT = s60 ? 9'd240 : 9'd288;
    reg  [11:0] hc = 0;
    reg  [8:0]  ly = 0;
    always @(posedge clk) begin
        if (hc >= HT4 - 12'd1) begin
            hc <= 12'd0;
            ly <= (ly >= LT - 9'd1) ? 9'd0 : ly + 9'd1;
        end else hc <= hc + 12'd1;
    end
    wire vs_l = ly < 9'd3;
    wire cs_w = vs_l ? (hc < HT4 - HS4) : (hc < HS4);
    wire [8:0] lv = ly - VA0;                            // output line in the window
    wire act_l = ly >= VA0 && lv < ACT;

    // ---- per frame: S, shown lines, the vertical placement
    reg        f_en = 0, f_two = 0, f_fmt2 = 0;
    reg [9:0]  f_w = 0;
    reg [7:0]  f_n = 0;
    reg [8:0]  f_top = 0, f_rows = 0;                    // black lines above, lines shown
    reg [10:0] f_src0 = 0;                               // first source line
    reg [15:0] f_stride = 0;
    reg [26:4] f_show = 0;
    wire       two_n = c_h > {2'b00, ACT};
    wire [10:0] hs_n = two_n ? {1'b0, c_h[10:1]} : c_h;
    wire        crop = hs_n > {2'b00, ACT};
    wire [10:0] over = hs_n - {2'b00, ACT};
    always @(posedge clk) if (hc == 12'd0 && ly == 9'd3) begin
        f_en   <= on && c_ctrl[0] && (c_ctrl[3:1] == 3'd1 || (FMT2 != 0 && c_ctrl[3:1] == 3'd2)) && c_w != 0 && c_h != 0;
        f_fmt2 <= FMT2 != 0 && c_ctrl[3:1] == 3'd2;
        f_two  <= two_n;
        f_w    <= c_w;
        f_n    <= (FMT2 != 0 && c_ctrl[3:1] == 3'd2) ? {c_w[9:2]} + (c_w[1:0] != 0) : {1'b0, c_w[9:3]} + (c_w[2:0] != 0);
        f_rows <= crop ? ACT : hs_n[8:0];
        f_top  <= crop ? 9'd0 : (ACT - hs_n[8:0]) >> 1;
        f_src0 <= crop ? (two_n ? {over[10:1], 1'b0} : {1'b0, over[10:1]}) : 11'd0;
        f_stride <= c_stride;
        f_show <= c_show;
    end

    // ---- line requests: at hc 0 of line L the source line(s) of output line L + 1 into the other pair
    wire [8:0]  ln  = lv + 9'd1;                         // the next output line
    wire        ln_in = (ln >= f_top) && (ln < f_top + f_rows);
    // the source line address: f_show + f_src0 * stride, stepped by repeated addition in the vertical blank, then + S * stride per request
    reg [26:4]  la = 0;
    reg [10:0]  kk = 0;
    always @(posedge clk) begin
        if (hc == 12'd1 && ly == 9'd3) begin la <= f_show; kk <= f_src0; end
        else if (kk != 11'd0) begin la <= la + {7'd0, f_stride[15:4]}; kk <= kk - 11'd1; end
        if (hc == 12'd8 && f_en && (ly + 9'd1 >= VA0) && ln_in) begin
            a_rq_addr <= la;
            a_rq_stride <= f_stride;
            a_rq_n <= f_n; a_rq_two <= f_two; a_rq_pair <= ln[0];
            a_rq_tgl <= ~a_rq_tgl;
            la <= la + (f_two ? {6'd0, f_stride[15:4], 1'b0} : {7'd0, f_stride[15:4]});
        end
    end
    reg ok_pair0 = 0, ok_pair1 = 0;
    always @(posedge clk) begin
        if (hc == 12'd8) begin if (ln[0]) ok_pair1 <= 1'b0; else ok_pair0 <= 1'b0; end
        if (s_dn[2] != s_dn[1]) begin if (a_rq_pair) ok_pair1 <= 1'b1; else ok_pair0 <= 1'b1; end
    end

    // ---- the line RAM: 4 slots of 128 x 128 bit (write ui_clk, read clk)
    (* ram_style = "block" *) reg [127:0] lb [0:511];
    always @(posedge ui_clk) if (a_we) lb[a_wa] <= a_wd;

    // ---- the window: horizontal accumulator, word reads (top, then bottom), lane, 888, blend, 11/16
    reg        h_on = 0, l_ok = 0;
    reg [9:0]  pidx = 0;
    reg [11:0] hacc = 0, hcnt = 0;
    wire       row_on = act_l && f_en && (lv >= f_top) && (lv < f_top + f_rows);
    always @(posedge clk) begin
        if (hc == HA0 - 12'd1) begin
            h_on <= row_on; l_ok <= lv[0] ? ok_pair1 : ok_pair0; pidx <= 0; hacc <= 0; hcnt <= NW - 12'd1;
        end else if (h_on) begin
            if (hacc + {2'b00, f_w} >= NW) begin hacc <= hacc + {2'b00, f_w} - NW; pidx <= pidx + 10'd1; end
            else hacc <= hacc + {2'b00, f_w};
            if (hcnt == 0) h_on <= 1'b0; else hcnt <= hcnt - 12'd1;
        end
    end
    wire [6:0] wsel = f_fmt2 ? pidx[8:2] : pidx[9:3];
    reg  [6:0] wprev = 7'h7F;
    reg        rdt1 = 0, rdb1 = 0;
    reg  [8:0] adr;
    reg  [127:0] lbq, wt, wb;
    reg  [2:0] lane1, lane2;
    reg  [2:0] on_p = 0;
    wire       newword = h_on && (wsel != wprev || hc == HA0);
    always @(posedge clk) begin
        if (h_on) wprev <= wsel;
        rdt1 <= newword;
        rdb1 <= rdt1 && f_two && BLEND != 0;
        adr  <= rdt1 ? {lv[0], 1'b1, wsel} : {lv[0], 1'b0, wsel};
        lbq  <= lb[newword ? {lv[0], 1'b0, wsel} : adr];
        if (rdt1) wt <= lbq;
        if (rdb1) wb <= lbq;
        lane1 <= f_fmt2 ? {1'b0, pidx[1:0]} : pidx[2:0];
        lane2 <= lane1;
        on_p  <= {on_p[1:0], h_on && l_ok};
    end
    function [23:0] to888(input [127:0] w, input [2:0] lane, input fmt2);
        reg [31:0] f;
        begin
            f = fmt2 ? w[32*lane[1:0] +: 32] : {16'd0, w[16*lane +: 16]};
            to888 = fmt2 ? f[23:0] : {f[15:11], f[15:13], f[10:5], f[10:9], f[4:0], f[4:2]};
        end
    endfunction
    reg [23:0] t3, b3;
    reg [8:0]  sr, sg, sb;
    reg [7:0]  ar, ag, ab;
    reg [3:0]  on_q = 0;
    always @(posedge clk) begin
        t3 <= to888(wt, lane2, f_fmt2);
        b3 <= (f_two && BLEND != 0) ? to888(wb, lane2, f_fmt2) : to888(wt, lane2, f_fmt2);
        sr <= {1'b0, t3[23:16]} + {1'b0, b3[23:16]};
        sg <= {1'b0, t3[15:8]}  + {1'b0, b3[15:8]};
        sb <= {1'b0, t3[7:0]}   + {1'b0, b3[7:0]};
        ar <= sr[8:1]; ag <= sg[8:1]; ab <= sb[8:1];
        on_q <= {on_q[2:0], on_p[2]};
    end
    // x 11/16 = (8x + 2x + x) / 16, no DSP
    function [7:0] lvl(input [7:0] x);
        reg [11:0] s;
        begin s = {1'b0, x, 3'd0} + {3'd0, x, 1'b0} + {4'd0, x}; lvl = s[11:4]; end
    endfunction
    // sync and FB delayed with the words (a few clocks; constant)
    reg [7:0] cs_d = 0;
    always @(posedge clk) cs_d <= {cs_d[6:0], cs_w};
    always @(posedge clk) begin
        r_o   <= (on && on_q[1]) ? lvl(ar) : 8'd0;
        g_o   <= (on && on_q[1]) ? lvl(ag) : 8'd0;
        b_o   <= (on && on_q[1]) ? lvl(ab) : 8'd0;
        syn_n <= on ? ~cs_d[7] : 1'b1;
        fb    <= on;
    end
endmodule
