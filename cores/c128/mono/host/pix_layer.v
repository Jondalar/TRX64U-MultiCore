// The pixel layer and the still capture (ARCH-lean-state-access.md §7.5,
// T12 §5): video client "v" of mig_arb.
//
// Pixel layer: a picture in DDR2, W x H source pixels (W <= 960, H <= 1080), in one of three formats (PX_CTRL b3:1; 3 is the VIC's capture, see Still):
// 1 = RGB565 (LVGL LV_COLOR_FORMAT_RGB565: one little-endian 16-bit word per pixel, R in bits 15:11, G 10:5, B 4:0),
// 2 = one 32-bit word per pixel (B 7:0, G 15:8, R 23:16, 31:24 ignored, no colour key; T136). It is shown as OW x OH output
// pixels (PX_OSIZE) with its top left at output pixel (X0, Y0) of the HDMI mode's active area (hact x vact at xact0 / yact0,
// hdmi_timing.v), over the C64 and overlay.v, under the freeze menu's text (C64 -> overlay.v -> this -> c64_text.v). The
// application chooses the size (Integer, Fit, Fill); there is no divider for it here. Every register on the pixel side changes
// only when ce is high (pixel repetition of the mode).
// Nearest sampling: a phase accumulator per axis gives output pixel x source pixel floor(x * W / OW) of source line
// floor(y * H / OH); W <= 2 OW and H <= 2 OH (a picture shown smaller than its source skips pixels and lines).
// Smoothing (parameter SMOOTH, PX_CTRL b6): bilinear weights of the two source pixels and the two source lines the position
// falls between (position x * W / OW, weight = its fraction in 1/1024 steps from an exact fraction recurrence, pix_wrec), per
// axis only while the source is not larger than the output on that axis (otherwise that axis stays nearest), the vertical
// part only while a source line is at most 128 words (RGB565: always; format 2: W <= 512). SMOOTH = 0 builds none of it.
// 565 channels are widened to 8 bits by repeating the top bits (R5 -> {R5, R5[4:2]}, G6 -> {G6, G6[5:4]}), so 0 and
// full scale stay 0 and 255. A pixel equal to the colour key (PX_KEY, format 1, when enabled) is transparent (smoothed: the
// nearest source pixel decides); the others are blended with the global alpha G:
//   out = bg + a (fg - bg) / 256,  a = G + G[7]
// per channel, so G = 255 gives the pixel and G = 0 the picture below,
// exactly. No per-pixel alpha. Outside the layer, and on a line whose fetch
// was late, the picture below passes unchanged (a late line is never shown in
// part: "underrun", counted).
// Scanout: one line memory of 512 x 128 bit (two RAMB36), written by the fetcher in ui_clk and read in the pixel clock through ONE
// port, with SMOOTH too (a second read port would double the block RAM). Nearest: two slots of 256 words, the line starting on
// screen is in the slot the accumulator says. Vertical smoothing: four slots of 128 words, source line k in slot k mod 4; the pair
// (m, m + 1) is shown and m + 2 fetched; the two lines are read word by word (8 pixels a word in format 1, 4 in format 2) when the
// pixel chain reaches the first pixel of a word, into two 128-bit holding registers, so the one port serves both.
// When source line m starts on screen the fetcher is asked for the next line (line 0 at the frame start, cy = 0, and with vertical
// smoothing line 1 at cy = 1): ceil(W / 8) MIG reads back to back (120 at W = 960 in format 1, 1.6 us of the 14.8 us an output line
// of 1080p has; in mig_arb the video client takes turns with the RISC-V/REU pair, and goes before them once a line fetch is 5.3 us
// old: v_urgent). A request that comes while the previous line is still being read stops that one (it is late anyway); the new
// one starts once the reads in flight have landed.
// Page flip: a write of PX_BACK (its byte 3) asks for a flip; at the next
// frame start the shown buffer (PX_FRONT's, which does not follow: write-only) becomes
// PX_BACK's and the flip-done cause is raised.
//
// Still (T194: the C128's copy; this is the 40-column picture's source): the VIC's pixels (pix/px/py) in the window 400 x 270 from raster X0S, Y0S, 4 bits each, 32 to a
// 128-bit word (pixel x of a row at nibble x mod 32 of word 13 y + x / 32: every row is padded with 16 zero pixels to 416, 13 words = 208 bytes, so every row starts
// on a word), 3,510 words = 56,160 bytes per frame, written to STILL_BASE + slot x 64 KB (STILL_BASE bits 17:0 are not used), three slots.
// 60 Hz System Modes (v60, T40: 65 x 263 lines): the window is 400 x 240 from line 31 through 262 into lines 0-7 of the next frame. The still has the same
// 240 rows but all of one VIC frame, so that it is complete at line 262: lines 0-7 of the frame (written first) are rows 232-239 (words 3,016-3,119), lines 31-262
// rows 0-231 (words 0-3,015); 3,120 words = 49,920 bytes. The mode is taken when the capture starts and kept for the frame.
// The pixels shift into a 128-bit register; the finished word goes into ONE hand-off word to ui_clk (toggle + ack), which the DDR2 side must have taken
// before the next word is finished (32 pixel events, about 4 us at 7.88 MHz).
// A frame goes into the slot after the last complete one (0, 1, 2, 0, ...); a frame that lost a word (the hand-off word was still waiting) is not reported and
// its slot is used again. The machine pause stops the VIC and with it the capture, so while paused STILL_STATUS names the slot of the last frame before the pause.
// Format 3 (PX_CTRL b3:1 = 3, SMOOTH = 1 only): the layer shows that capture. W = 400, H = 270 (240), PX_STRIDE = 208, the source is 4 bit indexed through the palette
// (pal_ram, the C64 palette the application writes; two read ports, so smoothing works on the looked-up colours). PX_FRONT / PX_BACK are not used: at every frame start the
// line address is the slot of the last complete capture (fol_slot, taken into clk with the capture's toggle), so a frame is never put together from two captures; the
// writer is always at least one frame away from the slot in display. The layer shows nothing until the first capture is complete.
//
// Registers (page 6 of trx_io, 0x101A6000; 32 bit little endian, the T21
// rule: writing byte 3 commits the four bytes, reading byte 0 takes bytes
// 1-3; read data one clk_sys after the address). Only the two status registers
// read; every other register is write-only and reads 0 (OPT-pix.md):
//   060 STILL_CTRL   W   b0 capture (default 1)
//   064 STILL_BASE   W   DDR2 byte address of slot 0 (default 0x05050000, T20 §5.1, bits 17:0 not used); slot n at + n x 0x10000
//   068 STILL_STATUS R   b0 valid (a frame since capture on), b1 that frame is 60 Hz: 400 x 240 (0: 400 x 270), b3:2 slot of the
//                        last complete frame, b7:4 0, b31:8 frames captured (wrapping)
//   080 PX_CTRL      W   b0 enable, b3:1 format (1 = RGB565; 2 = one 32-bit
//                        word per pixel, no colour key; 3 = the VIC's capture, see above; others: off),
//                        b5:4 are not read any more (the old scale S: the size is PX_OSIZE), b6 smoothing (bilinear;
//                        ignored when SMOOTH = 0; default 0x22)
//   084 PX_FRONT     W   DDR2 byte address of the shown buffer (16-byte aligned)
//   088 PX_BACK      W   show this buffer from the next frame start
//   090 PX_SIZE      W   {height 16, width 16} source pixels (default 540, 960)
//   094 PX_STRIDE    W   bytes from line to line (multiple of 16; default 1920)
//   098 PX_POS       W   {y0 16, x0 16} output pixel of the top left (default 0)
//   0A8 PX_OSIZE     W   {height 16, width 16} output pixels of the picture, each below 2048: source pixel
//                        floor(x * width_src / width) of source line floor(y * height_src / height); width_src <= 2 * width,
//                        height_src <= 2 * height (default 1080, 1920)
//   09C PX_GALPHA    W   b7:0 global alpha (default 255)
//   0A0 PX_KEY       W   b15:0 colour key (an RGB565 value), b16 key on: pixels
//                        equal to it are transparent (default 0: off)
//   0A4 PX_STATUS    R   b0 flip pending, b1 vertical blank, b15:8 lines not
//                        shown for underrun (wrapping), b31:16 frames (wrapping)
// Events for trx_io's IRQ_CAUSE (clk_sys pulses): ev_vsync at every frame
// start (cy = 0, yact0 lines before the first visible line), ev_flip when a
// flip was done.
module pix_layer #(
    parameter [9:0] X0S = 10'd96,       // still window, as vic_scaler's X0, Y0
    parameter [8:0] Y0S = 9'd16,
    parameter       SMOOTH = 1          // 1: bilinear smoothing (PX_CTRL b6), 0: nearest only
) (
    input  wire         clk_sys,
    input  wire         we,
    input  wire         re,
    input  wire [11:0]  addr,
    input  wire [7:0]   wdata,
    output reg  [7:0]   rdata = 8'd0,
    output reg          ev_vsync = 1'b0,
    output reg          ev_flip = 1'b0,

    input  wire         ui_clk,
    input  wire         ui_rst,
    output wire [26:0]  v_addr,
    output wire [2:0]   v_cmd,
    output wire         v_en,
    input  wire         v_rdy,
    output wire [127:0] v_wdf_data,
    output wire [15:0]  v_wdf_mask,
    output wire         v_wdf_wren,
    input  wire [127:0] v_rd_data,
    input  wire         v_rd_data_valid,
    output reg          v_urgent = 1'b0,

    input  wire         vclk,
    input  wire [3:0]   pix,
    input  wire [9:0]   px,
    input  wire [8:0]   py,
    input  wire         v60,            // 65 x 263 VIC (any clock; as vic_scaler's)

    input  wire         pal_clk,        // T194: the C64 palette (C64_PALETTE writes, the app's bus clock) for format 3
    input  wire         pal_we,
    input  wire [5:0]   pal_addr,
    input  wire [7:0]   pal_wdata,

    input  wire         clk,
    input  wire         ce,             // pixel enable (hdmi_timing.v); every clk register below changes only when it is high
    input  wire [11:0]  hact,           // the mode's active area and its origin in cx / cy
    input  wire [10:0]  vact,
    input  wire [11:0]  xact0,
    input  wire [10:0]  yact0,
    input  wire [23:0]  rgb_in,
    input  wire         de_in, hs_in, vs_in,
    input  wire [11:0]  cx_in,
    input  wire [10:0]  cy_in,
    output reg  [23:0]  rgb,
    output reg          de, hs, vs,
    output reg  [11:0]  cx,
    output reg  [10:0]  cy
);
    // ======================= registers (clk_sys) ===============================
    reg        r_still_en = 1'b1;
    reg [31:0] r_still_base = 32'h05050000;
    reg [6:0]  r_ctrl = 7'h22;
    reg [31:0] r_front = 32'd0, r_back = 32'd0;
    reg [15:0] r_w = 16'd960, r_h = 16'd540;
    reg [15:0] r_stride = 16'd1920;
    reg [15:0] r_x0 = 16'd0, r_y0 = 16'd0;
    reg [15:0] r_ow = 16'd1920, r_oh = 16'd1080;
    reg [7:0]  r_ga = 8'd255;
    reg [16:0] r_key = 17'd0;
    reg        cfg_tgl = 1'b0, front_tgl = 1'b0, back_tgl = 1'b0;
    reg [23:0] stg = 24'd0;
    reg [31:0] lat = 32'd0;

    // events from the other clocks
    wire       px_vs_tgl, px_flip_tgl, px_flip_seen, px_under_tgl, px_vblank, st_done_tgl;
    wire [1:0] st_done_slot;
    wire       st_seen;
    wire       st_done_60;
    (* ASYNC_REG = "TRUE" *) reg [2:0] s_vs = 3'd0, s_flip = 3'd0, s_under = 3'd0, s_st = 3'd0;
    (* ASYNC_REG = "TRUE" *) reg [1:0] s_vb = 2'd0, s_seen = 2'd0;
    always @(posedge clk_sys) begin
        s_vs <= {s_vs[1:0], px_vs_tgl};
        s_flip <= {s_flip[1:0], px_flip_tgl};
        s_under <= {s_under[1:0], px_under_tgl};
        s_st <= {s_st[1:0], st_done_tgl};
        s_vb <= {s_vb[0], px_vblank};
        s_seen <= {s_seen[0], px_flip_seen};
    end
    reg [15:0] n_frames = 16'd0;
    reg [7:0]  n_under = 8'd0;
    reg [23:0] n_still = 24'd0;
    reg        still_valid = 1'b0, still_60 = 1'b0;
    reg [1:0]  still_slot = 2'd0;
    wire       flip_pending = back_tgl != s_seen[1];

    wire [11:0] reg_a = {addr[11:2], 2'b00};
    reg  [31:0] cur;                    // the register at reg_a: only the two status registers read
    always @(*)
        case (reg_a)
        12'h068: cur = {n_still, 4'd0, still_slot, still_60, still_valid};
        12'h0A4: cur = {n_frames, n_under, 6'd0, s_vb[1], flip_pending};
        default: cur = 32'd0;
        endcase

    wire [31:0] wv = {wdata, stg};
    always @(posedge clk_sys) begin
        ev_vsync <= s_vs[2] != s_vs[1];
        ev_flip  <= s_flip[2] != s_flip[1];
        if (s_vs[2] != s_vs[1]) n_frames <= n_frames + 16'd1;
        if (s_under[2] != s_under[1]) n_under <= n_under + 8'd1;
        if (s_st[2] != s_st[1]) begin
            n_still <= n_still + 24'd1;
            still_valid <= 1'b1;
            still_slot  <= st_done_slot;
            still_60    <= st_done_60;
        end

        if (we)
            case (addr[1:0])
            2'd0: stg[7:0]   <= wdata;
            2'd1: stg[15:8]  <= wdata;
            2'd2: stg[23:16] <= wdata;
            default: begin
                case (reg_a)
                12'h060: r_still_en   <= wv[0];
                12'h064: r_still_base <= {wv[31:4], 4'd0};
                12'h080: r_ctrl       <= wv[6:0];
                12'h084: r_front      <= {wv[31:4], 4'd0};
                12'h088: r_back       <= {wv[31:4], 4'd0};
                12'h090: begin r_w <= wv[15:0]; r_h <= wv[31:16]; end
                12'h094: r_stride     <= {wv[15:4], 4'd0};
                12'h098: begin r_x0 <= wv[15:0]; r_y0 <= wv[31:16]; end
                12'h0A8: begin r_ow <= wv[15:0]; r_oh <= wv[31:16]; end
                12'h09C: r_ga         <= wv[7:0];
                12'h0A0: r_key        <= wv[16:0];
                default: ;
                endcase
                if (reg_a == 12'h084) front_tgl <= ~front_tgl;
                else if (reg_a == 12'h088) back_tgl <= ~back_tgl;
                else cfg_tgl <= ~cfg_tgl;
            end
            endcase
        if (re && addr[1:0] == 2'd0) lat <= cur;
        case (addr[1:0])
        2'd0: rdata <= cur[7:0];
        2'd1: rdata <= lat[15:8];
        2'd2: rdata <= lat[23:16];
        default: rdata <= lat[31:24];
        endcase
    end

    // ======================= pixel side (clk) ==================================
    // settings: copied whenever the app commits one (the copy is taken again
    // at the next commit, so a value read while it changed is replaced), used
    // from the next frame start
    (* ASYNC_REG = "TRUE" *) reg [2:0] p_cfg = 3'd0, p_front = 3'd0, p_back = 3'd0;
    reg [6:0]  c_ctrl = 7'h22;
    reg [15:0] c_w = 16'd960, c_h = 16'd540, c_stride = 16'd1920, c_x0 = 16'd0, c_y0 = 16'd0, c_ow = 16'd1920, c_oh = 16'd1080;
    reg [7:0]  c_ga = 8'd255;
    reg [16:0] c_key = 17'd0;
    reg [31:0] c_front = 32'd0, c_back = 32'd0;
    reg        flip_req = 1'b0, flip_tgl = 1'b0, flip_seen = 1'b0, vs_tgl = 1'b0, under_tgl = 1'b0;
    always @(posedge clk) if (ce) begin
        p_cfg   <= {p_cfg[1:0], cfg_tgl};
        p_front <= {p_front[1:0], front_tgl};
        p_back  <= {p_back[1:0], back_tgl};
    end
    // T194 format 3: the slot of the last complete capture and "a capture is complete", taken into clk on the capture's toggle (sd_slot has been stable
    // for two vclk when the toggle arrives); fol_slot is read once per frame, at the frame start
    (* ASYNC_REG = "TRUE" *) reg [2:0] p_sd = 3'd0;
    reg [1:0]  fol_slot = 2'd0;
    reg        fol_valid = 1'b0;
    reg [31:0] c_sbase = 32'h05050000;
    always @(posedge clk) if (ce) begin
        p_sd <= {p_sd[1:0], st_done_tgl};
        if (p_sd[2] != p_sd[1]) begin fol_slot <= st_done_slot; fol_valid <= st_seen; end
    end
    assign px_vs_tgl    = vs_tgl;
    assign px_flip_tgl  = flip_tgl;
    assign px_flip_seen = flip_seen;
    assign px_under_tgl = under_tgl;
    assign px_vblank    = cy_in < yact0;

    // per frame
    reg        f_en = 1'b0, par = 1'b0;
    reg [11:0] f_ow;                    // T107: output size and the accumulator steps of both axes (source, source - out, source - 2 out)
    reg [10:0] f_oh;
    reg signed [14:0] f_hd0, f_hd1, f_hd2, f_vd0, f_vd1, f_vd2;
    reg [9:0]  f_w;
    reg [10:0] f_hm1;                   // layer height - 1, registered: keeps the subtract off
                                        // the m compare that gates the next line's request
    reg [11:0] f_xs;                    // cx of the first layer pixel
    reg [10:0] f_ys;                    // cy of the first layer line
    reg [15:0] f_stride;
    reg [8:0]  f_ga;                    // 0..256
    reg        f_ken;                   // colour key on
    reg [15:0] f_key;
    reg        f_fmt2;                  // 32 bits per pixel
    wire       fmt2_new = c_ctrl[3:1] == 3'd2;
    wire       fmt3_new = SMOOTH != 0 && c_ctrl[3:1] == 3'd3;     // T194: the VIC's capture, 4 bit indexed, palette
    reg        f_fmt3 = 1'b0;
    // smoothing per axis: only where the source is not larger than the output; vertically also only where a line fits 128 words
    reg        f_hsm = 1'b0, f_vsm = 1'b0;
    reg [11:0] f_xs_pre;                // cx 9 clocks before the first pixel: the smoothing chain starts there with its prime pixel
    reg [11:0] f_qS = 12'd0, f_rS = 12'd0, f_qSv = 12'd0, f_rSv = 12'd0;   // 1024 * source / output on each axis: quotient, remainder
    wire [9:0]  f_wc = (c_w > 16'd960) ? 10'd960 : c_w[9:0];
    wire [10:0] f_hc = (c_h > 16'd1080) ? 11'd1080 : c_h[10:0];
    wire        hsm_new = SMOOTH != 0 && c_ctrl[6] && {6'd0, f_wc} <= c_ow;
    wire        vsm_new = SMOOTH != 0 && c_ctrl[6] && {5'd0, f_hc} <= c_oh && (!fmt2_new || f_wc <= 10'd512);
    wire       en_new = c_ctrl[0] && (c_ctrl[3:1] == 3'd1 || fmt2_new || (fmt3_new && fol_valid)) && c_w != 0 && c_h != 0 && c_ow != 0 && c_oh != 0 &&
                        c_ow < 16'd2048 && c_oh < 16'd2048 && c_x0 < {4'd0, hact} && c_y0 < {5'd0, vact};
    // the step of an axis from its accumulator: q = 0, 1 or 2 source steps for the next output step
    reg signed [14:0] hacc, vacc;
    wire signed [14:0] hn0 = hacc + f_hd0, hn1 = hacc + f_hd1, hn2 = hacc + f_hd2;
    wire [1:0]  hq = !hn2[14] ? 2'd2 : !hn1[14] ? 2'd1 : 2'd0;
    wire signed [14:0] hacc_n = (hq == 2'd2) ? hn2 : (hq == 2'd1) ? hn1 : hn0;
    wire signed [14:0] vn0 = vacc + f_vd0, vn1 = vacc + f_vd1, vn2 = vacc + f_vd2;
    wire [1:0]  vq_c = !vn2[14] ? 2'd2 : !vn1[14] ? 2'd1 : 2'd0;
    wire signed [14:0] vacc_n_c = (vq_c == 2'd2) ? vn2 : (vq_c == 2'd1) ? vn1 : vn0;
    // The line side uses these only at cx 16 / cx 24, 8 clocks after vacc and m last changed (cx 16), so
    // they are pipelined: stage 1 vq/vacc_n (one adder), stage 2 m_next and the request stride, stage 3 the
    // m_next compare. Each stage is one adder deep; the values equal the old combinational ones from clock 4 on.
    reg [1:0]          vq = 2'd0;
    reg signed [14:0]  vacc_n = 15'sd0;
    reg [10:0]         m_next = 11'd0;
    reg [31:0]         la_inc = 32'd0;
    reg                m_ok = 1'b0;

    // fetch requests (stable from their toggle to the next request)
    reg        rq_tgl = 1'b0;
    reg [7:0]  rq_n;                    // 128-bit words
    reg [1:0]  rq_slot;                 // the slot the line goes to: nearest 0 / 1, vertical smoothing the line number mod 4
    reg        rq_sm = 1'b0;            // vertical smoothing: four slots of 128 words, else two of 256
    reg [11:0] rq_tag;                  // {frame parity, line}
    reg [31:0] la;                      // address of the last line asked for; with rq_tgl it is the request's address

    // fetch done (ui_clk), into the line tags
    wire       fd_tgl;
    wire [1:0] fd_slot;
    wire [11:0] fd_tag;
    (* ASYNC_REG = "TRUE" *) reg [2:0] p_fd = 3'd0;
    reg [11:0] vtag [0:3];
    integer    ti;
    initial for (ti = 0; ti < 4; ti = ti + 1) vtag[ti] = 12'hFFF;

    // lines
    reg        row_on = 1'b0, first = 1'b0, line_ok = 1'b0, slot_cur = 1'b0, init_row = 1'b0;
    reg [10:0] m;
    reg [10:0] m1 = 11'd0;              // m + 1: the line below the one shown (vertical smoothing)
    reg        vsel = 1'b0;             // the line has a line below it to blend with
    reg [10:0] yl;                      // output lines still to come after this one
    wire        sl_new = init_row ? 1'b0 : ~slot_cur;               // the buffer half the line starting now is in
    wire        v_init = cx_in == 12'd16 && cy_in != 11'd0 && f_en && cy_in == f_ys;
    wire        v_adv  = cx_in == 12'd16 && cy_in != 11'd0 && !(f_en && cy_in == f_ys) && row_on && yl != 11'd0;
    wire [9:0]  qv;                     // vertical weight of the line shown, 1024 * fraction (smoothing)
    wire        sm_ok_t = vtag[m[1:0]]  == {par, m};
    wire        sm_ok_b = m == f_hm1 || vtag[m1[1:0]] == {par, m1};
    integer     fi;

    always @(posedge clk) if (ce) begin
        vq     <= vq_c;
        vacc_n <= vacc_n_c;
        m_next <= m + ((f_vsm || vq == 2'd2) ? 11'd2 : 11'd1);  // the next distinct source line (smoothing: the line below the pair)
        la_inc <= (vq == 2'd2 && !f_vsm) ? {15'd0, f_stride, 1'b0} : {16'd0, f_stride};
        m_ok   <= m_next <= {1'b0, f_hm1};
        m1     <= m + 11'd1;
        if (p_cfg[2] != p_cfg[1]) begin
            c_ctrl <= r_ctrl; c_w <= r_w; c_h <= r_h; c_stride <= r_stride;
            c_x0 <= r_x0; c_y0 <= r_y0; c_ow <= r_ow; c_oh <= r_oh; c_ga <= r_ga; c_key <= r_key; c_sbase <= r_still_base;
        end
        p_fd <= {p_fd[1:0], fd_tgl};
        if (p_fd[2] != p_fd[1]) vtag[fd_slot] <= fd_tag;

        // frame start, once the last pixels of the frame are through the
        // pipeline (they use f_key up to cx 3, f_ga up to cx 5)
        if (cy_in == 11'd0 && cx_in == 12'd8) begin
            vs_tgl <= ~vs_tgl;
            par    <= ~par;
            f_en   <= en_new;
            f_fmt2 <= fmt2_new;
            f_fmt3 <= fmt3_new;
            f_hsm  <= hsm_new;
            f_vsm  <= vsm_new;
            f_w    <= f_wc;
            f_hm1  <= f_hc - 11'd1;
            f_ow   <= c_ow[11:0];
            f_oh   <= c_oh[10:0];
            f_hd0  <= $signed({5'd0, f_wc});
            f_hd1  <= $signed({5'd0, f_wc}) - $signed({3'd0, c_ow[11:0]});
            f_hd2  <= $signed({5'd0, f_wc}) - $signed({2'd0, c_ow[11:0], 1'b0});
            f_vd0  <= $signed({4'd0, f_hc});
            f_vd1  <= $signed({4'd0, f_hc}) - $signed({4'd0, c_oh[10:0]});
            f_vd2  <= $signed({4'd0, f_hc}) - $signed({3'd0, c_oh[10:0], 1'b0});
            f_xs   <= xact0 + c_x0[11:0];
            f_ys   <= yact0 + c_y0[10:0];
            f_stride <= c_stride;
            f_ga   <= {1'b0, c_ga} + c_ga[7];
            f_ken  <= c_key[16];
            f_key  <= c_key[15:0];
            for (fi = 0; fi < 4; fi = fi + 1) vtag[fi] <= 12'hFFF;
            if (flip_req) begin
                c_front   <= c_back;
                flip_req  <= 1'b0;
                flip_tgl  <= ~flip_tgl;
                flip_seen <= p_back[2];
                la        <= c_back;
            end else
                la        <= c_front;
            if (fmt3_new)                // the page flip is the capture's: the slot of the last complete frame
                la        <= {c_sbase[31:18], 2'b00, 16'd0} | {14'd0, fol_slot, 16'd0};
        end
        // after the frame start: a request in its clock waits for the next one
        if (p_front[2] != p_front[1]) c_front <= r_front;
        if (p_back[2] != p_back[1]) begin c_back <= r_back; flip_req <= 1'b1; end
        if (cy_in == 11'd0 && cx_in == 12'd12 && f_en) begin
            rq_n <= f_fmt3 ? {3'd0, f_w[9:5]} + (f_w[4:0] != 5'd0) : f_fmt2 ? {f_w[9:2]} + (f_w[1:0] != 2'd0) : {1'b0, f_w[9:3]} + (f_w[2:0] != 3'd0);
            rq_slot <= 2'd0; rq_sm <= f_vsm; rq_tag <= {par, 11'd0}; rq_tgl <= ~rq_tgl;
        end
        // vertical smoothing needs lines 0 and 1 before the first row (a line after the frame start's, the fetches go one at a time)
        if (cy_in == 11'd1 && cx_in == 12'd12 && f_en && f_vsm && f_hm1 != 11'd0) begin
            la <= la + f_stride;
            rq_slot <= 2'd1; rq_sm <= 1'b1; rq_tag <= {par, 11'd1}; rq_tgl <= ~rq_tgl;
        end

        // per output line: the accumulator says whether it shows the next source line (and which: 1 or 2 on)
        if (cx_in == 12'd16) begin
            if (cy_in == 11'd0)
                row_on <= 1'b0;
            else if (f_en && cy_in == f_ys) begin
                row_on <= 1'b1; m <= 11'd0; vacc <= 15'sd0; first <= 1'b1; init_row <= 1'b1; yl <= f_oh - 11'd1;
            end else if (row_on) begin
                if (yl == 11'd0)
                    row_on <= 1'b0;
                else begin
                    yl <= yl - 11'd1; init_row <= 1'b0;
                    vacc <= vacc_n;
                    first <= vq != 2'd0;
                    m <= m + {9'd0, vq};
                end
            end
        end
        if (cx_in == 12'd24) begin
            vsel <= f_vsm && m != f_hm1;
            if (!row_on)
                line_ok <= 1'b0;
            else if (first) begin
                if (f_vsm) begin
                    // the pair (m, m + 1) must be in; line m + 2 is fetched into the slot of line m - 2
                    line_ok <= sm_ok_t && sm_ok_b;
                    if (!(sm_ok_t && sm_ok_b)) under_tgl <= ~under_tgl;
                    if (m_ok) begin
                        la <= la + la_inc;
                        rq_slot <= m_next[1:0]; rq_sm <= 1'b1; rq_tag <= {par, m_next}; rq_tgl <= ~rq_tgl;
                    end
                end else begin
                    line_ok  <= vtag[{1'b0, sl_new}] == {par, m};
                    slot_cur <= sl_new;
                    if (vtag[{1'b0, sl_new}] != {par, m}) under_tgl <= ~under_tgl;
                    if (m_ok) begin
                        la <= la + la_inc;
                        rq_slot <= {1'b0, ~sl_new}; rq_sm <= 1'b0; rq_tag <= {par, m_next}; rq_tgl <= ~rq_tgl;
                    end
                end
            end
        end
    end

    // Smoothing: the weights of both axes are exact fractions (pix_wrec) from 1024 * source / output, divided once per frame
    // (restoring division, 21 clocks per axis, long before the first line).
    reg        dv_run = 1'b0, dv_ax = 1'b0;
    reg [4:0]  dv_n = 5'd0;
    reg [20:0] dv_num = 21'd0;
    reg [11:0] dv_rem = 12'd0, dv_d = 12'd1, dv_q = 12'd0;
    wire [11:0] dv_sh = {dv_rem[10:0], dv_num[20]};
    wire [12:0] dv_df = {1'b0, dv_sh} - {1'b0, dv_d};
    wire [11:0] dv_nq = {dv_q[10:0], ~dv_df[12]};
    wire [11:0] dv_nr = dv_df[12] ? dv_sh : dv_df[11:0];
    always @(posedge clk) if (ce && SMOOTH != 0) begin
        f_xs_pre <= f_xs - 12'd9;
        if (cy_in == 11'd0 && cx_in == 12'd10) begin
            dv_run <= 1'b1; dv_ax <= 1'b0; dv_n <= 5'd0; dv_rem <= 12'd0; dv_q <= 12'd0;
            dv_num <= {1'b0, f_w, 10'd0}; dv_d <= f_ow;
        end else if (dv_run) begin
            dv_num <= {dv_num[19:0], 1'b0};
            dv_rem <= dv_nr; dv_q <= dv_nq;
            dv_n   <= dv_n + 5'd1;
            if (dv_n == 5'd20) begin
                if (!dv_ax) begin
                    f_qS <= dv_nq; f_rS <= dv_nr;
                    dv_ax <= 1'b1; dv_n <= 5'd0; dv_rem <= 12'd0; dv_q <= 12'd0;
                    dv_num <= {f_hm1 + 11'd1, 10'd0}; dv_d <= {1'b0, f_oh};
                end else begin
                    f_qSv <= dv_nq; f_rSv <= dv_nr;
                    dv_run <= 1'b0;
                end
            end
        end
    end

    // ---- the pixel chain ---------------------------------------------------------------------------------------------
    // Pixels, one stage per clock from the pixel at cx_in. Nearest: E0 counters, E1 line buffer, E2 its output register, E3
    // lane, E4 colour key (the picture below passes beside it at vrgb[1..5]). Smoothing runs 8 clocks longer in front
    // and starts 8 clocks earlier (its prime pixel 9). Both end in fg888 / on5, aligned with vrgb[5] / vctl[5]. Then E5 fg - bg, E6 product
    // (DSP48 M register), E7 DSP48 P register, E8 out. The difference and the product are registered on their own
    // (DSP48 A/B, M, P), so no path has more than one adder or one multiplier stage.
    (* ram_style = "block" *) reg [127:0] lb [0:511];
    wire [23:0] fg888;
    wire        on5;
    reg [23:0] vrgb [1:8];
    reg [25:0] vctl [1:8];              // {de, hs, vs, cx, cy}
    function [23:0] to888(input [31:0] f, input fmt2);
        to888 = fmt2 ? f[23:0] : {f[15:11], f[15:13], f[10:5], f[10:9], f[4:0], f[4:2]};   // RGB565 by bit replication
    endfunction

    generate if (SMOOTH == 0) begin : g_nn
        reg        h_on = 1'b0;
        reg [9:0]  pidx;
        reg [11:0] hcnt;                    // output pixels still to come after this one
        always @(posedge clk) if (ce)
            if (cx_in == f_xs) begin
                h_on <= line_ok; pidx <= 10'd0; hacc <= 15'sd0; hcnt <= f_ow - 12'd1;
            end else if (h_on) begin
                pidx <= pidx + {8'd0, hq};
                hacc <= hacc_n;
                if (hcnt == 12'd0) h_on <= 1'b0;
                else hcnt <= hcnt - 12'd1;
            end

        reg [127:0] lb_q, lb_r;
        reg [2:0]  lane2, lane3;
        reg        w2, w3, w4;              // format 2
        reg        on2, on3, on4, on5_r;
        reg [31:0] fg4, fg5;
        assign fg888 = to888(fg5, w4);
        assign on5   = on5_r;
        always @(posedge clk) if (ce) begin
            // E1
            lb_q  <= lb[f_fmt2 ? {slot_cur, pidx[9:2]} : {slot_cur, 1'b0, pidx[9:3]}];
            lane2 <= f_fmt2 ? {1'b0, pidx[1:0]} : pidx[2:0];
            w2    <= f_fmt2;
            on2   <= h_on && vctl[1][25];
            // E2
            lb_r  <= lb_q;
            lane3 <= lane2;
            w3    <= w2;
            on3   <= on2;
            // E3
            fg4 <= w3 ? lb_r[32*lane3[1:0] +: 32] : {16'd0, lb_r[16*lane3 +: 16]};
            w4  <= w3;
            on4 <= on3;
            // E4
            fg5 <= fg4;
            on5_r <= on4 && !(f_ken && !w4 && fg4[15:0] == f_key);
        end
    end else begin : g_sm
        // ---- E0: counters. The pixel before the first one (the prime) reads source pixel 0; pixel k then reads source
        // pixel s + 1, s = floor(k * W / OW), so that the value of pixel s is the one the chain saw one step ago
        // (A = the previous B when the read pixel moved on). Without horizontal smoothing pixel k reads s itself.
        reg        h_pre = 1'b0, h_on = 1'b0;
        reg [9:0]  ridx;
        reg [11:0] hcnt;
        reg        stp0;
        wire [9:0] qh;
        pix_wrec wh (.clk(clk), .ce(ce), .init(cx_in == f_xs_pre), .adv(h_on),
                     .qS(f_qS), .rS(f_rS), .D({1'b0, f_ow[10:0]}), .q(qh), .wrap());
        // The line memory has ONE read port (two would double the block RAM). A pixel moves at most one source pixel a clock
        // (two without horizontal smoothing), so a 128-bit word of the top line and one of the bottom line are needed at most every
        // 2nd clock: they are read when the chain reaches the first pixel of a new word, the top one in the clock of the pixel,
        // the bottom one in the next, and held in wt / wb. wt changes one clock before wb, so the top pixel is picked a clock
        // before the bottom one and delayed.
        wire [7:0] wsel = f_fmt3 ? {3'd0, ridx[9:5]} : f_vsm ? {1'b0, f_fmt2 ? ridx[8:2] : ridx[9:3]} : (f_fmt2 ? ridx[9:2] : {1'b0, ridx[9:3]});
        reg  [7:0] wprev;
        wire       rdt = (h_pre | h_on) & (h_pre | (wsel != wprev));
        always @(posedge clk) if (ce) begin
            if (h_pre | h_on) wprev <= wsel;
            if (cx_in == f_xs_pre) begin
                h_pre <= line_ok; h_on <= 1'b0; ridx <= 10'd0; hacc <= 15'sd0; hcnt <= f_ow - 12'd1; stp0 <= 1'b0;
            end else if (h_pre) begin
                h_pre <= 1'b0; h_on <= 1'b1; ridx <= f_hsm ? 10'd1 : 10'd0; stp0 <= f_hsm;
            end else if (h_on) begin
                ridx <= ridx + {8'd0, hq};
                hacc <= hacc_n;
                stp0 <= hq != 2'd0;
                if (hcnt == 12'd0) h_on <= 1'b0;
                else hcnt <= hcnt - 12'd1;
            end
        end
        wire [8:0] adr_t = f_vsm ? {m[1:0], wsel[6:0]} : {slot_cur, wsel};
        reg        rdt1 = 1'b0, rdb1 = 1'b0, rdb2 = 1'b0;
        reg [6:0]  wd1;
        wire [8:0] adr_b = {m1[1:0], wd1};
        reg [127:0] lbq, wt, wb;
        reg [4:0]  lane1, lane2, lane3;
        reg [31:0] tsel, td, bsel;
        // flags of a pixel along the chain: {load, show, step, last}; q = its horizontal weight; kk = colour key of the top pixel
        reg [3:0]  fl [1:8];
        reg [9:0]  qq [1:10];
        reg        kk [3:8];
        reg [23:0] ct5, bs5, ct6;
        reg        on9, on10, on11, k9, k10, k11, on12_r;
        reg [23:0] fg_r;
        integer    gi;
        assign fg888 = fg_r;
        assign on5   = on12_r;
        function [31:0] pick(input [127:0] w, input [4:0] lane, input fmt2, input fmt3);
            pick = fmt3 ? {28'd0, w[4*lane +: 4]} : fmt2 ? w[32*lane[1:0] +: 32] : {16'd0, w[16*lane[2:0] +: 16]};
        endfunction
        // T194 format 3: the 16-entry palette (the app's C64_PALETTE), one read port for the top pixel, one for the bottom one
        wire [23:0] pal_t, pal_b;
        pal_ram palt (.clk_w(pal_clk), .we(pal_we), .addr(pal_addr), .wdata(pal_wdata), .idx(td[3:0]),   .rgb(pal_t));
        pal_ram palb (.clk_w(pal_clk), .we(pal_we), .addr(pal_addr), .wdata(pal_wdata), .idx(bsel[3:0]), .rgb(pal_b));
        always @(posedge clk) if (ce) begin
            // edge 1: the read of the top word of a pixel that starts a new one
            if (rdb1 | rdt) lbq <= lb[rdb1 ? adr_b : adr_t];
            rdt1  <= rdt;
            rdb1  <= rdt & f_vsm;
            wd1   <= wsel[6:0];
            lane1 <= f_fmt3 ? ridx[4:0] : f_fmt2 ? {3'd0, ridx[1:0]} : {2'd0, ridx[2:0]};
            fl[1] <= {h_pre | h_on, h_on, stp0, f_hsm && ridx == f_w};
            qq[1] <= qh;
            // edge 2: lbq is the top word (read in the clock before): held; the bottom one is being read
            if (rdt1) wt <= lbq;
            rdb2  <= rdb1;
            lane2 <= lane1;
            // edge 3: lbq is the bottom word: held; the top pixel is picked from wt
            if (rdb2) wb <= lbq;
            lane3 <= lane2;
            tsel  <= pick(wt, lane2, f_fmt2, f_fmt3);
            kk[3] <= f_ken && !f_fmt2 && !f_fmt3 && pick(wt, lane2, 1'b0, 1'b0) == {16'd0, f_key};
            // edge 4: the bottom pixel is picked from wb, the top one waits a clock
            td    <= tsel;
            bsel  <= pick(wb, lane3, f_fmt2, f_fmt3);
            for (gi = 2; gi <= 8; gi = gi + 1) fl[gi] <= fl[gi-1];
            for (gi = 2; gi <= 10; gi = gi + 1) qq[gi] <= qq[gi-1];
            for (gi = 4; gi <= 8; gi = gi + 1) kk[gi] <= kk[gi-1];
            // edge 5: to 888; the bottom line is the top one where there is no line below
            ct5   <= f_fmt3 ? pal_t : to888(td, f_fmt2);
            bs5   <= vsel ? (f_fmt3 ? pal_b : to888(bsel, f_fmt2)) : (f_fmt3 ? pal_t : to888(td, f_fmt2));
        end
        // ---- edges 6-8 per channel: vertical blend V = T + (B - T) * wv, kept with 6 fractional bits (weight (2 qv + 1) / 2048)
        // edge 9: A / B of the horizontal pair; edges 10-12: H = A + (B - A) * wh, rounded
        reg  [10:0] vwt;
        always @(posedge clk) if (ce) vwt <= {qv, 1'b1};
        genvar gc;
        for (gc = 0; gc < 3; gc = gc + 1) begin : ch
            reg signed [8:0]  d6;
            reg [7:0]         t6, t7;
            reg signed [20:0] pr7;
            reg [13:0]        v8;
            reg [13:0]        va = 14'd0, pb = 14'd0, sa9 = 14'd0, sb9 = 14'd0, sa10 = 14'd0, sa11 = 14'd0;
            reg signed [14:0] dv10;
            reg signed [26:0] ph11;
            reg [7:0]         o12;
            wire [13:0]       va_n = fl[8][1] ? pb : va;
            wire signed [31:0] tot = $signed({7'd0, sa11, 11'd0}) + ph11 + 32'sd65536;
            always @(posedge clk) if (ce) begin
                d6  <= $signed({1'b0, bs5[8*gc +: 8]}) - $signed({1'b0, ct5[8*gc +: 8]});
                t6  <= ct5[8*gc +: 8];
                pr7 <= d6 * $signed({1'b0, vwt});
                t7  <= t6;
                v8  <= ({3'd0, t7, 11'd0} + {{1{pr7[20]}}, pr7}) >> 5;
                // edge 9
                if (fl[8][3]) begin
                    va <= va_n; pb <= v8;
                end
                sa9 <= f_hsm ? va_n : v8;
                sb9 <= (f_hsm && fl[8][0]) ? va_n : v8;
                // edge 10
                dv10 <= $signed({1'b0, sb9}) - $signed({1'b0, sa9});
                sa10 <= sa9;
                // edge 11
                ph11 <= dv10 * $signed({1'b0, qq[10], 1'b1});
                sa11 <= sa10;
                // edge 12
                o12 <= tot[24:17];
            end
            always @(*) fg_r[8*gc +: 8] = o12;
        end
        // the A / B colour key and the flags
        reg ka = 1'b0, kpb = 1'b0;
        wire ka_n = fl[8][1] ? kpb : ka;
        always @(posedge clk) if (ce) begin
            // edge 9
            if (fl[8][3]) begin ka <= ka_n; kpb <= kk[8]; end
            on9 <= fl[8][2]; k9 <= f_hsm ? ka_n : kk[8];
            // edges 10, 11
            on10 <= on9; k10 <= k9;
            on11 <= on10; k11 <= k10;
            // edge 12: the de gate is the one of the pixel at this stage (vctl[4] becomes vctl[5])
            on12_r <= on11 && !k11 && vctl[4][25];
        end
    end endgenerate

    reg        on6, on7, on8_;
    reg signed [8:0]  d6 [0:2];
    reg signed [18:0] m7 [0:2], p8 [0:2];
    integer i;
    always @(posedge clk) if (ce) begin
        vrgb[1] <= rgb_in; vctl[1] <= {de_in, hs_in, vs_in, cx_in, cy_in};
        for (i = 2; i <= 8; i = i + 1) begin vrgb[i] <= vrgb[i-1]; vctl[i] <= vctl[i-1]; end
        // E5
        for (i = 0; i < 3; i = i + 1)
            d6[i] <= $signed({1'b0, fg888[8*i +: 8]}) - $signed({1'b0, vrgb[5][8*i +: 8]});
        on6 <= on5;
        // E6, E7
        for (i = 0; i < 3; i = i + 1) begin
            m7[i] <= d6[i] * $signed({1'b0, f_ga});
            p8[i] <= m7[i];
        end
        on7 <= on6;
        on8_ <= on7;
        // E8
        for (i = 0; i < 3; i = i + 1)
            rgb[8*i +: 8] <= on8_ ? vrgb[8][8*i +: 8] + p8[i][15:8] : vrgb[8][8*i +: 8];
        {de, hs, vs, cx, cy} <= vctl[8];
    end

    generate if (SMOOTH != 0) begin : g_wv
        pix_wrec wv (.clk(clk), .ce(ce), .init(v_init), .adv(v_adv),
                     .qS(f_qSv), .rS(f_rSv), .D({1'b0, f_oh}), .q(qv), .wrap());
    end else begin : g_nwv
        assign qv = 10'd0;
    end endgenerate

    // ======================= DDR2 side (ui_clk) ================================
    (* ASYNC_REG = "TRUE" *) reg [2:0] u_rq = 3'd0;
    (* ASYNC_REG = "TRUE" *) reg [1:0] u_st = 2'd0;
    reg        nxt = 1'b0, run = 1'b0;
    reg [31:0] n_addr;                  // the request, taken on its toggle
    reg [7:0]  n_n;
    reg [1:0]  n_slot;
    reg        n_sm;
    reg [11:0] n_tag;
    reg [26:0] rd_a;                    // MIG address of the next read
    reg [7:0]  n_left;                  // reads still to issue
    reg [7:0]  rcv_left;                // words still to land for this line
    reg [4:0]  out = 5'd0;              // reads in flight
    reg [7:0]  wa;                      // word of the line in its slot
    reg [1:0]  cur_slot;
    reg        cur_sm;
    reg [9:0]  age;                     // ui_clk since the line's fetch began
    reg [11:0] cur_tag;
    reg        done_tgl = 1'b0;
    reg [1:0]  done_slot = 2'd0;
    reg [11:0] done_tag = 12'hFFF;
    assign fd_tgl = done_tgl; assign fd_slot = done_slot; assign fd_tag = done_tag;

    // the still word from vclk: one entry, full while its toggle differs from the ack
    reg          st_tgl = 1'b0, st_ack = 1'b0;
    reg  [127:0] st_d;
    reg  [26:4]  st_a;
    wire         st_pend = u_st[1] != st_ack;

    wire issue = run && !nxt && n_left != 8'd0;
    assign v_en       = st_pend || issue;
    assign v_cmd      = st_pend ? 3'b000 : 3'b001;
    assign v_addr     = st_pend ? {1'b0, st_a, 3'b000} : rd_a;
    assign v_wdf_data = st_d;
    assign v_wdf_mask = 16'h0000;
    assign v_wdf_wren = st_pend;
    wire rd_go = v_rdy && !st_pend;

    always @(posedge ui_clk) begin
        u_rq  <= {u_rq[1:0], rq_tgl};
        u_st  <= {u_st[0], st_tgl};
        if (u_rq[2] != u_rq[1]) begin
            nxt <= 1'b1; run <= 1'b0;
            n_addr <= la; n_n <= rq_n; n_slot <= rq_slot; n_sm <= rq_sm; n_tag <= rq_tag;
        end

        out <= out + rd_go - v_rd_data_valid;
        if (v_rdy && st_pend) st_ack <= ~st_ack;
        if (rd_go) begin
            rd_a   <= rd_a + 27'd8;
            n_left <= n_left - 8'd1;
        end
        if (v_rd_data_valid) begin
            // four slots of 128 words: a late line's reads landing past its slot are dropped, not written into the next line
            if (!(cur_sm && wa[7])) lb[cur_sm ? {cur_slot, wa[6:0]} : {cur_slot[0], wa[7:0]}] <= v_rd_data;
            wa     <= wa + 8'd1;
            if (run) begin
                rcv_left <= rcv_left - 8'd1;
                if (rcv_left == 8'd1) begin
                    run <= 1'b0;
                    done_tag <= cur_tag; done_slot <= cur_slot; done_tgl <= ~done_tgl;
                end
            end
        end
        // the next line once the last one's reads have landed
        if (nxt && u_rq[2] == u_rq[1] && out == 5'd0 && !v_rd_data_valid) begin
            nxt      <= 1'b0;
            run      <= n_n != 8'd0;
            rd_a     <= {1'b0, n_addr[26:4], 3'b000};
            n_left   <= n_n;
            rcv_left <= n_n;
            wa       <= 8'd0;
            cur_slot <= n_slot;
            cur_sm   <= n_sm;
            cur_tag  <= n_tag;
        end
        // a line not in after 400 clocks (5.3 us) goes before the RISC-V and the REU in mig_arb
        if (!run) age <= 10'd0;
        else if (age != 10'h3FF) age <= age + 10'd1;
        v_urgent <= run && age >= 10'd400;
        if (ui_rst) begin
            nxt <= 1'b0; run <= 1'b0; out <= 5'd0;
        end
    end

    // ======================= still capture (vclk) ==============================
    (* ASYNC_REG = "TRUE" *) reg [2:0] v_cfg = 3'd0;
    (* ASYNC_REG = "TRUE" *) reg [1:0] v_ack = 2'd0;
    reg        sv_en = 1'b1;
    reg [31:0] sv_base = 32'h05050000;   // bits 17:0 are not used: the three slots are 64 KB apart from a 256 KB boundary
    reg [3:0]  pix_d;
    reg [9:0]  px_d = 10'd0;
    reg [8:0]  py_d = 9'd0;
    reg        cap = 1'b0, torn = 1'b0;
    reg [1:0]  slot = 2'd0, last_slot = 2'd2;   // T194: three slots, the writer takes (last + 1) mod 3
    reg [4:0]  pad = 5'd0;              // T194: zero pixels still to shift in after the row's last pixel (rows are 416 pixels = 13 words)
    reg        sv_seen = 1'b0;          // a frame is complete (format 3 shows only then)
    wire [1:0] slot_nx = last_slot == 2'd2 ? 2'd0 : last_slot + 2'd1;
    reg [31:0] base;
    reg [127:0] sh;                     // the window's pixels, 32 to a word: pixel k at nibble k mod 32
    reg [4:0]  k = 5'd0;
    reg [11:0] widx = 12'd0;           // T194: word of the frame; a row is 13 words, 50 Hz 3,510 words, 60 Hz 3,120
    reg        sd_tgl = 1'b0, sd_60 = 1'b0;
    reg [1:0]  sd_slot = 2'd0;
    assign st_done_tgl = sd_tgl; assign st_done_slot = sd_slot; assign st_done_60 = sd_60; assign st_seen = sv_seen;
    (* ASYNC_REG = "TRUE" *) reg [1:0] v60_vs = 2'b00;
    reg        m60 = 1'b0;                  // the mode of the frame being captured
    wire       v60_v = v60_vs[1];
    wire       f60   = cap ? m60 : v60_v;
    wire       st_free = st_tgl == v_ack[1];
    wire       emit = px != px_d;       // the pixel at px_d is final
    wire       in_x = px_d >= X0S && px_d < X0S + 10'd400;
    wire       padding = pad != 5'd0;
    wire       last_px = px_d == X0S + 10'd399;
    wire       in_y = f60 ? (py_d < 9'd8 || py_d >= 9'd31)
                            : (py_d >= Y0S && py_d < Y0S + 9'd270);
    wire       start = px_d == X0S && py_d == (v60_v ? 9'd0 : Y0S);
    wire       cap_n = start ? sv_en : cap;
    wire [4:0] kk = start ? 5'd0 : k;
    wire [127:0] sh_n = {padding ? 4'd0 : pix_d, sh[127:4]};  // the word, when pix_d is its 32nd pixel
    always @(posedge vclk) begin
        v_cfg  <= {v_cfg[1:0], cfg_tgl};
        v_ack  <= {v_ack[0], st_ack};
        if (v_cfg[2] != v_cfg[1]) begin sv_en <= r_still_en; sv_base <= r_still_base; end
        pix_d <= pix; px_d <= px; py_d <= py;
        v60_vs <= {v60_vs[0], v60};

        if (emit && ((in_x && (in_y || start)) || padding)) begin
            if (start && !padding) begin
                cap  <= sv_en; torn <= 1'b0; slot <= slot_nx;
                base <= {sv_base[31:18], 2'b00, 16'd0} + {14'd0, slot_nx, 16'd0};
                widx <= v60_v ? 12'd3016 : 12'd0; k <= 5'd0;
                m60  <= v60_v;
            end
            if (cap_n) begin
                k <= kk + 5'd1;
                sh <= sh_n;
                if (padding) pad <= pad - 5'd1;
                else if (last_px) pad <= 5'd16;
                if (kk == 5'd31) begin
                    widx <= (f60 && widx == 12'd3119) ? 12'd0 : widx + 12'd1;
                    if (st_free) begin
                        st_d   <= sh_n;
                        st_a   <= base[26:4] + {11'd0, widx};
                        st_tgl <= ~st_tgl;
                    end else
                        torn <= 1'b1;
                    if (widx == (f60 ? 12'd3015 : 12'd3509)) begin
                        cap <= 1'b0;
                        if (!torn && st_free) begin
                            last_slot <= slot;
                            sd_slot   <= slot;
                            sd_60     <= f60;
                            sd_tgl    <= ~sd_tgl;
                            sv_seen   <= 1'b1;
                        end
                    end
                end
            end
        end
    end
endmodule

// The fraction of an axis's source position, exactly, one step at a time: at output step k the position is k * S / D, its fraction
// (k * S mod D) / D, and q = floor(1024 * (k * S mod D) / D) is the weight in 1/1024 steps. With qS, rS = quotient and remainder of
// 1024 * S / D, each step adds qS plus the carry of the remainder (r + rS >= D) to q, and a q of 1024 or more wraps by 1024: that
// wrap is the step to the next source sample (equal to the accumulator's). The remainder runs one step ahead of q so that the
// carry is a register: q is one adder, r one adder and a mux. Needs S <= D (qS <= 1024) and rS < D.
module pix_wrec (
    input  wire        clk,
    input  wire        ce,
    input  wire        init,            // step 0: q = 0
    input  wire        adv,             // one step on
    input  wire [11:0] qS,
    input  wire [11:0] rS,
    input  wire [11:0] D,
    output reg  [9:0]  q = 10'd0,
    output wire        wrap             // the next step is on the next source sample
);
    reg [11:0] r = 12'd0;               // the remainder after the step now in progress
    reg        cy = 1'b0;               // carry of the step now in progress
    reg [11:0] nD = 12'd0;              // D - rS
    wire [11:0] qsum = {2'b00, q} + qS + {11'd0, cy};
    assign wrap = qsum[11] | qsum[10];
    wire [12:0] d13 = {1'b0, r} - {1'b0, nD};
    wire [12:0] s13 = {1'b0, r} + {1'b0, rS};
    always @(posedge clk) if (ce) begin
        nD <= D - rS;
        if (init) begin
            q <= 10'd0; r <= rS; cy <= 1'b0;
        end else if (adv) begin
            q  <= qsum[9:0];
            r  <= d13[12] ? s13[11:0] : d13[11:0];
            cy <= ~d13[12];
        end
    end
endmodule
