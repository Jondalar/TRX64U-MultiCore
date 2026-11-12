// T140 D1 (first made for the C128 core): the core's analogue picture - S-Video (Y and C) and composite from the VIC-IIe's native 15 kHz picture, no scaler.
//
// Input: the same stream the HDMI scaler takes (vic_scaler): palette index, raster x (dots), raster line, in sys_clk (dot4x, the
// machine's clock, 31.5-32.8 MHz by System Mode). The timing is the VIC's own: the line has HT dots (63 or 65 cycles x 8), the
// frame LT lines; the picture is the same 400 x 270/240 window as the HDMI scaler cuts (X0 = 96). Sync, blanking and burst are
// made here from the raster position (hx, ly), which follows the VIC while it runs (a dot step loads the position) and runs on
// by itself while the VIC is paused, so the picture does not lose its sync in the freeze menu.
//
//   sx  = dots since the sync's leading edge: raster x XS (5 at 63 cycles, 4 at 65), so the lines are 63/65 cycles apart as the VIC's
//   hsync     sx < W (37 dots = 4.7 us at 63 cycles, 38 = 4.65 us at 65)
//   active    raster x 96..495 (400 dots), lines Y0..Y0+ACT-1 (16/272 at 312 lines, 31/240 at 263 lines, wrapping over the frame end)
//   vsync     three serrated lines (the sync is low for the whole line but the last W dots): vpos VS..VS+2 (280 / 244)
//
// The encoder is c64scpu_yc.v: MiSTer's yc_out.sv numbers (vendor/mister, GPL v2+; palette index -> Y, C with burst, phase accumulator
// PHASE_INC = fsc / f_clk x 2^26, PAL switching) in fewer LUTs (T140 room): the colour matrix runs in DSP48s, the levels are added
// there: the sync tip (code 0), the blanking level (BLK), the white level and the chroma-over-luma sum for the composite output,
// so the DAC words carry the whole signal and the DAC's own sync input stays unused (SYNCn high). Codes are 8 bit, full scale
// = 1 V into the port's resistor network (not measured, T140 S1):
//   Y   sync 0, blank = BLK (77 PAL = 0.3 V, 73 NTSC = 40/140), white BLK + 175 (11/16 of the 8 bit luma)
//   C   128 + chroma; burst 2 x 39 codes (0.3 V peak to peak), 128 outside burst and picture and while the sync is low
//   CV  clamp(Y + C - 128)
//
// vmode (System Mode, mode_ctl.v): 0 PAL 63 x 312 (31.5278 MHz), 1 NTSC 65 x 263 (32.7257), 2 PAL-60 65 x 263 (32.7652),
// 3 NTSC-50 63 x 312 (31.5848). PAL encoding in 0 and 2, NTSC encoding in 1 and 3.
module c64scpu_avout #(parameter PAL_VINV = 1) (
    input  wire        clk,             // sys_clk
    input  wire [1:0]  vmode,           // sys_clk domain
    input  wire [3:0]  pix,             // VIC pixel (colour index), raster position px / py, all clk
    input  wire [9:0]  px,
    input  wire [8:0]  py,
    input  wire        pal_clk,         // the app's palette writes (C64_PALETTE), as the scaler's
    input  wire        pal_we,
    input  wire [5:0]  pal_addr,
    input  wire [7:0]  pal_wdata,
    output wire [7:0]  y_o,         // DAC codes, registered in c64scpu_yc (I/O flops)
    output wire [7:0]  c_o,
    output wire [7:0]  cv_o
);
    wire v60 = (vmode == 2'd1) || (vmode == 2'd2);
    wire pal = (vmode == 2'd0) || (vmode == 2'd2);

    wire [9:0] HT = v60 ? 10'd520 : 10'd504;
    wire [8:0] LT = v60 ? 9'd263  : 9'd312;
    wire [9:0] XS = v60 ? 10'd4   : 10'd5;
    wire [9:0] W  = v60 ? 10'd38  : 10'd37;
    wire [8:0] Y0 = v60 ? 9'd31   : 9'd16;
    wire [8:0] ACT = v60 ? 9'd240 : 9'd272;
    wire [8:0] VS = v60 ? 9'd244  : 9'd280;

    // ---- the raster position: loaded at every dot of the VIC, free-running in between ------------------------------------
    reg  [9:0] pxd = 10'd0;
    reg  [3:0] pix_r = 4'd0;
    reg  [9:0] hx = 10'd0;
    reg  [8:0] ly = 9'd0;
    reg  [1:0] dv = 2'd0;
    always @(posedge clk) begin
        pxd   <= px;
        pix_r <= pix;
        if (px != pxd) begin
            hx <= px; ly <= py; dv <= 2'd1;
        end else begin
            dv <= dv + 2'd1;
            if (dv == 2'd3) begin
                if (hx >= HT - 10'd1) begin
                    hx <= 10'd0;
                    ly <= (ly >= LT - 9'd1) ? 9'd0 : ly + 9'd1;
                end else hx <= hx + 10'd1;
            end
        end
    end

    wire [9:0] sx   = (hx >= XS) ? hx - XS : hx + HT - XS;
    wire [8:0] vpos = (ly >= Y0) ? ly - Y0 : ly + LT - Y0;
    reg        vs_cur = 1'b0;                       // the sync period that began at the last XS belongs to a vsync line
    always @(posedge clk) if (hx == XS - 10'd1) vs_cur <= (vpos >= VS) && (vpos < VS + 9'd3);

    wire hs_w = (sx < W);
`ifdef AVOUT_MUT_NOSYNC
    wire cs_w = 1'b0;
`else
    wire cs_w = vs_cur ? (sx < HT - W) : hs_w;
`endif
    wire de_w = (hx >= 10'd96) && (hx < 10'd496) && (vpos < ACT);

    // ---- palette and encoder -------------------------------------------------------------------------------------------
    // c64scpu_yc reads the palette by the colour index (sidx / rgb_w) and runs the colour matrix in DSP48s.
    wire [3:0]  sidx;
    wire [23:0] rgb_w;
    pal_ram pal_i (.clk_w(pal_clk), .we(pal_we), .addr(pal_addr), .wdata(pal_wdata), .idx(sidx), .rgb(rgb_w));

    reg [3:0] idx_r = 4'd0;
    reg       hs_r = 1'b0, cs_r = 1'b0, de_r = 1'b0;
    always @(posedge clk) begin
        idx_r <= pix_r;
        hs_r <= hs_w; cs_r <= cs_w; de_r <= de_w;
    end

    // PHASE_INC = fsc / f_clk x 2^26 for the clock each System Mode runs (mode_ctl.v) and its colour standard. 26 bits resolve
    // f_clk / 2^26 = 0.49 Hz; the constants are the old 40 bit ones rounded, the subcarrier is off by at most 0.27 Hz (0.08 ppm).
    reg [25:0] inc = 26'h900035;
    always @(posedge clk)
`ifdef AVOUT_MUT_BADINC
        inc <= 26'h700162;
`else
        case (vmode)
        2'd0:    inc <= 26'h900035;         // PAL     4.43361875 MHz / 31.527778 MHz
        2'd1:    inc <= 26'h700162;         // NTSC    3.579545   MHz / 32.725694 MHz
        2'd2:    inc <= 26'h8a9009;         // PAL-60  4.43361875 MHz / 32.765152 MHz
        default: inc <= 26'h740d18;         // NTSC-50 3.579545   MHz / 31.584821 MHz
        endcase
`endif
    // the burst window in samples after the sync's trailing edge: {start, end}; PAL 0.9 us / 10 cycles, NTSC 0.6 us / 9 cycles
`ifdef AVOUT_MUT_NOBURST
    wire [16:0] burst = {7'd127, 10'd0};
`else
    wire [16:0] burst = pal ? {7'd28, 10'd99} : {7'd20, 10'd102};
`endif
`ifdef AVOUT_MUT_NOPAL
    wire pal_en = 1'b0;
`else
    wire pal_en = pal;
`endif

    c64scpu_yc #(.WIDTH(26), .VINV(PAL_VINV)) enc (
        .clk(clk), .inc(inc), .pal_en(pal_en), .burst(burst), .blk(pal ? 8'd77 : 8'd73),
        .hsync(hs_r), .csync(cs_r), .de(de_r), .idx(idx_r), .sidx(sidx), .rgb_s(rgb_w),
        .y_o(y_o), .c_o(c_o), .cv_o(cv_o)
    );
endmodule
