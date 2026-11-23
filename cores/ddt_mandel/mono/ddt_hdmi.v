// T255: the HDMI side of the Mandelbrot large core. Written for this project (TREX), GPL v3. The chain of the host's hdmi_out.v (T164 WP8) without what a
// frame-buffer picture does not need: no mode request (the mode is fixed: row 2 of ddt_hdmi_modes.vh, 1080p50 from the PAL reference, the row every frame-buffer core
// starts in), no VIC scaler, no menu overlay, no pixel layer, no freeze text, no audio source. The files it instantiates are this core's own copies (mono/host/).
//
// Clocks: the GTPs run on usrclk = pix_clk / 2; mmcm_pix (x14 / 7 from usrclk) makes pix_clk = the TMDS clock (148.87 MHz). The mode's gear is 1: a pixel lasts one
// pix_clk and `ce` of hdmi_timing is 1 in every clock, so the picture source after the timing needs no enable. The source takes the timing's counters (t_*) and
// gives the picture back delayed by any number of clocks; it delays the counters with it (cx, cy, de, hs, vs travel with the pixel), which is what the encoder wants.
//
// No DSP anywhere in here (the 20 engines of the demo own all 120).
`timescale 1ns / 1ps
(* use_dsp = "no" *)
module ddt_hdmi (
    input  wire        clk50,
    input  wire        cdce_rst,         // clk50: the synthesizer sequence starts again (por | clocks not locked)
    input  wire        gt_rst,           // clk50: GTP reset (the synthesizer settles for 50 ms first)
    // pins
    input  wire        refclk_p, refclk_n,
    output wire [2:0]  data_p, data_n,
    output wire        clk_p, clk_n,
    // the synthesizer's I2C (the top drives the buses)
    output wire        scl_low, sda_low, i2c_done,
    output wire        gt_ready,
    // the picture source, pix_clk domain
    output wire        pix_clk,
    output wire        pix_rst,
    output wire [11:0] xact0,
    output wire [10:0] yact0,
    output wire [11:0] t_cx,             // the timing's counters: the pixel the source takes next
    output wire [10:0] t_cy,
    output wire        t_de, t_hs, t_vs,
    input  wire [23:0] rgb,              // the source's pixel with its counters, as many clocks after t_* as the source needs
    input  wire        de, hs, vs,
    input  wire [11:0] cx,
    input  wire [10:0] cy
);
`include "ddt_hdmi_modes.vh"
    localparam [3:0] MODE = 4'd2;        // 1080p50, hdmi_modes.vh row 2

    // ---- clocks: usrclk from the GTPs, pix_clk = 2 x usrclk from mmcm_pix
    wire        usrclk;
    wire        pfb, pfb_bufg, pix_mmcm, pix_locked;
    MMCME2_BASE #(
        .CLKIN1_PERIOD(13.45), .DIVCLK_DIVIDE(1), .CLKFBOUT_MULT_F(14.0),   // VCO 645-1042 MHz
        .CLKOUT0_DIVIDE_F(7.0)                                             // = the TMDS clock (2 x usrclk)
    ) mmcm_pix (
        .CLKIN1(usrclk), .CLKFBIN(pfb_bufg), .CLKFBOUT(pfb), .CLKOUT0(pix_mmcm),
        .LOCKED(pix_locked), .RST(~gt_ready), .PWRDWN(1'b0),
        .CLKOUT0B(), .CLKOUT1(), .CLKOUT1B(), .CLKOUT2(), .CLKOUT2B(), .CLKOUT3(),
        .CLKOUT3B(), .CLKOUT4(), .CLKOUT5(), .CLKOUT6(), .CLKFBOUTB()
    );
    BUFG bg_pfb (.I(pfb), .O(pfb_bufg));            // feedback through a BUFG: in phase with usrclk
    BUFG bg_pix (.I(pix_mmcm), .O(pix_clk));
    ddt_rst_sync rs_pix (.clk(pix_clk), .arst(~pix_locked), .rst(pix_rst));

    // ---- the synthesizer: the PLL registers of the mode
    cdce_mode i2c (.clk(clk50), .rst(cdce_rst), .scl_low(scl_low), .sda_low(sda_low), .done(i2c_done), .mode(MODE), .lock(1'b0));

    // ---- the timing
    wire        pce, running;
    wire [23:0] t_rgb;
    wire [11:0] m_hact;
    wire [10:0] m_vact;
    wire  [3:0] m_gear;
    wire  [6:0] m_vic;
    wire [31:0] m_rate;
    wire [12:0] e_sx, e_fwm1;
    wire [10:0] e_sy, e_ey;
    hdmi_timing mtim (
        .clk(pix_clk), .rst(pix_rst), .mode(MODE), .align(1'b0), .valid(), .aligned(),
        .ce(pce), .rgb(t_rgb), .de(t_de), .hs(t_hs), .vs(t_vs), .cx(t_cx), .cy(t_cy), .running(running),
        .hact(m_hact), .vact(m_vact), .xact0(xact0), .yact0(yact0), .gear(m_gear), .vic(m_vic), .rate(m_rate),
        .sx_e(e_sx), .fw_m1(e_fwm1), .sy_e(e_sy), .ey_e(e_ey), .ph_e(), .mode_q()
    );
    wire [1:0]  m_aspect = hm_aspect(MODE);

    // ---- the encoder: the same instance as the host's (hdmi_out.v), audio samples zero
    wire [9:0] tmds_0, tmds_1, tmds_2;
    hdmi #(
        .DVI_OUTPUT(1'b0), .IT_CONTENT(1'b1), .VIDEO_ID_CODE(0), .VIDEO_RATE(165_000_000), .RUNTIME(1), .MAX_PACKETS(3),
        .AUDIO_RATE(48_000), .AUDIO_BIT_WIDTH(24),
        .BIT_WIDTH(12), .BIT_HEIGHT(11),
        .VENDOR_NAME(64'h5452583634000000),                        // "TRX64"
        .PRODUCT_DESCRIPTION(128'h43363420556C74696D61746500000000), // "C64 Ultimate"
        .SOURCE_DEVICE_INFORMATION(8'h08)
    ) hdmi_enc (
        .clk_pixel_x5(1'b0), .clk_pixel(pix_clk), .ce(pce | pix_rst), .reset(pix_rst),
        .rgb(rgb), .hsync(hs), .vsync(vs), .cx(cx), .cy(cy), .de(de),
        .vic_code(m_vic), .video_rate(m_rate), .aspect(m_aspect),
        .sx_e(e_sx), .fw_m1(e_fwm1), .sy_e(e_sy), .ey_e(e_ey),
        .audio_sample_word_0(24'd0), .audio_sample_word_1(24'd0),
        .audio_sample_en(),
        .tmds_0(tmds_0), .tmds_1(tmds_1), .tmds_2(tmds_2)
    );

    // ---- the bit repeater and the GTPs
    wire [19:0] w0, w1, w2, wc;
    tmds_rep rep (
        .clk(pix_clk), .usrclk(usrclk), .rst(pix_rst), .ce(pce), .gear(m_gear),
        .sym0(tmds_0), .sym1(tmds_1), .sym2(tmds_2), .w0(w0), .w1(w1), .w2(w2), .wc(wc)
    );
    hdmi_gtp gtp (
        .sysclk(clk50), .reset(gt_rst),
        .refclk_p(refclk_p), .refclk_n(refclk_n),
        // Lane 2 (TMDS 0, blue) is wired with P and N swapped on the board.
        .tx0(w2), .tx1(w1), .tx2(~w0),
        .tx3(wc),
        .usrclk(usrclk), .ready(gt_ready),
        .txp({clk_p, data_p[0], data_p[1], data_p[2]}),
        .txn({clk_n, data_n[0], data_n[1], data_n[2]})
    );
endmodule

// reset synchroniser: asserted at once, released on three clocks (as the host's rst_sync)
module ddt_rst_sync (
    input  wire clk,
    input  wire arst,
    output wire rst
);
    (* ASYNC_REG = "TRUE" *) reg [2:0] s = 3'b111;
    always @(posedge clk or posedge arst)
        if (arst) s <= 3'b111;
        else      s <= {s[1:0], 1'b0};
    assign rst = s[2];
endmodule
