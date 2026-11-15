// T164 WP8 (G1, the host, every core), then T222: this core's copy of the HDMI side from the picture sources to the pins, with the HDMI rows
// of TRX64-Ultimate's U16 (d48734d8). One module holds the mode request, the scale and the status (mode_req), the synthesizer sequencer
// (cdce_mode), mmcm_pix, the timing generator (hdmi_timing), the VIC path (vic_scaler on the timing's counters, every row, at the scale of 0x4F01),
// the menu overlay, the pixel layer, the freeze text, the encoder fork (hdmi_enc), the TMDS bit repeater (tmds_rep), the GTPs, the HDMI side of the
// audio crossing and the measure register. A core delivers a picture and nothing else: the VIC raster (pix_*), and the DDR2 frame buffer through
// pix_layer.
//
// Clocks: the GTPs run on usrclk = pix_clk / 2; mmcm_pix (x14 / 7 from usrclk) makes pix_clk = the TMDS clock of the mode (122.7 .. 149.1 MHz). A
// pixel lasts `gear` pix_clk (hdmi_timing.v), `pce` is high in the last pix_clk of a pixel; every register of the picture chain after the
// timing changes only on that edge, the encoder and tmds_rep run on it as well.
//
// Differences against the host's hdmi_out.v (T222): the core is a C64-type one without exception (mode_req holds every request to the System Mode's
// family and to the seven locked rows of U16 D2), the VIC path is in the chain in every row (no black picture row, no picture mux), there is no core
// swap (no relock), `vmode` gives the encoder the clock rate of the System Mode.
module hdmi_out #(
    parameter SMOOTH = 1                 // pix_layer: bilinear smoothing
) (
    input  wire        clk50,
    input  wire        cpu_clk,          // the application's bus clock (overlay, pixel layer, mode request, palette)
    input  wire        sys_clk,          // the C64 system clock (VIC pixel side, freeze text, measure register)
    input  wire        cdce_rst,         // clk50: por_rst | ~clocks_ok; cdce_mode also restarts with every accepted mode request
    input  wire        gt_rst,           // clk50: GTP reset (the synthesizer settles for 50 ms first)
    input  wire        fam_v60,          // clk50: the System Mode's family (mode_ctl cdce_v60)
    input  wire [1:0]  vmode,            // clk50: the System Mode in use (mode_ctl vmode): 0 PAL, 1 NTSC, 2 PAL-60, 3 NTSC-50

    // pins
    input  wire        refclk_p, refclk_n,
    output wire [2:0]  data_p, data_n,
    output wire        clk_p, clk_n,

    // the synthesizer's I2C (the top muxes the buses)
    output wire        scl_low, sda_low, i2c_done,

    // the application's overlay window (0x10140000) with the mode request / status read in
    input  wire        ovl_we,
    input  wire [14:0] ovl_addr,
    input  wire [7:0]  ovl_wdata,
    output wire [7:0]  ovl_rdata,
    output wire        ovl_own,

    // the pixel layer: registers, DDR2 client, still capture events
    input  wire        px_we, px_re,
    input  wire [11:0] px_addr,
    input  wire [7:0]  px_wdata,
    output wire [7:0]  pxl_rdata,
    output wire        px_ev_vsync, px_ev_flip,
    input  wire        ui_clk, ui_rst,
    output wire [26:0] v_addr,
    output wire [2:0]  v_cmd,
    output wire        v_en,
    input  wire        v_rdy,
    output wire [127:0] v_wdf_data,
    output wire [15:0] v_wdf_mask,
    output wire        v_wdf_wren,
    input  wire [127:0] v_rd_data,
    input  wire        v_rd_data_valid,
    output wire        v_urgent,

    // the freeze text
    output wire [7:0]  tx_rdata,
    input  wire        c64_halted,
    input  wire [15:0] tx_view,
    output wire        tx_req,
    output wire [15:0] tx_paddr,
    output wire        tx_rom,
    input  wire        tx_go,
    input  wire [7:0]  ram_rdata,
    output wire [9:0]  tx_cram_addr,
    input  wire        cram_grant,
    input  wire [3:0]  cram_rdata,

    // the VIC raster and the palette
    input  wire [3:0]  pix_color,
    input  wire [9:0]  pix_x,
    input  wire [8:0]  pix_y,
    input  wire        v60,
    input  wire        scanlines,
    input  wire        pal_we,
    input  wire [5:0]  pal_addr,
    input  wire [7:0]  pal_wdata,
    output wire [9:0]  x_text,

    // audio: the samples held in the mclk domain, a toggle per sample
    input  wire [47:0] snd_hold,
    input  wire        snd_tgl,

    // status
    output wire        gt_ready,
    output wire        video_locked_s,   // clk50: the picture is up (and locked to the VIC where the VIC path is in use)
    output reg  [31:0] hdmi_meas         // sys_clk: {gt_ready, pix_locked, locked, 0, frames in the last second (12), pixel clocks per frame / 256 (16)}
);
`include "scpu_hdmi_modes.vh"

    // ---- clocks: usrclk from the GTPs, pix_clk = 2 x usrclk from mmcm_pix
    wire        usrclk;
    wire        pfb, pfb_bufg, pix_mmcm, pix_locked, pix_clk, pix_rst;
    MMCME2_BASE #(
        .CLKIN1_PERIOD(13.45), .DIVCLK_DIVIDE(1), .CLKFBOUT_MULT_F(14.0),   // VCO 645-1042 MHz for the TMDS clocks of all rows (92-149 MHz)
        .CLKOUT0_DIVIDE_F(7.0)                                             // = the TMDS clock (2 x usrclk)
    ) mmcm_pix (
        .CLKIN1(usrclk), .CLKFBIN(pfb_bufg), .CLKFBOUT(pfb), .CLKOUT0(pix_mmcm),
        .LOCKED(pix_locked), .RST(~gt_ready), .PWRDWN(1'b0),
        .CLKOUT0B(), .CLKOUT1(), .CLKOUT1B(), .CLKOUT2(), .CLKOUT2B(), .CLKOUT3(),
        .CLKOUT3B(), .CLKOUT4(), .CLKOUT5(), .CLKOUT6(), .CLKFBOUTB()
    );
    BUFG bg_pfb (.I(pfb), .O(pfb_bufg));            // feedback through a BUFG: in phase with usrclk
    BUFG bg_pix (.I(pix_mmcm), .O(pix_clk));
    rst_sync rs_pix (.clk(pix_clk), .arst(~pix_locked), .rst(pix_rst));

    // ---- the mode: request and status (cpu_clk), the synthesizer (clk50), the timing (pix_clk)
    wire [3:0]  vm_mode, mode_pix, m_mode;
    wire [1:0]  scale_pix;
    wire        smooth_pix;
    wire        vm_rst, running, fstart;
    wire [7:0]  mr_rdata;
    wire        mr_rhit;
    mode_req mreq (
        .clk_sys(cpu_clk), .we(ovl_we), .addr(ovl_addr), .wdata(ovl_wdata), .rdata(mr_rdata), .rhit(mr_rhit),
        .clk50(clk50), .fam_v60(fam_v60), .vm_mode(vm_mode), .vm_rst(vm_rst),
        .clk_pix(pix_clk), .mode_pix(mode_pix), .fstart(fstart), .scale_pix(scale_pix), .smooth_pix(smooth_pix),
        .gt_ready(gt_ready), .pix_locked(pix_locked), .running(running), .mode_q(m_mode)
    );
    cdce_mode i2c (.clk(clk50), .rst(cdce_rst | vm_rst), .scl_low(scl_low), .sda_low(sda_low), .done(i2c_done), .mode(vm_mode));

    wire        align, pce, t_de, t_hs, t_vs;
    wire [11:0] t_cx, m_hact, m_xact0, m_htot_m1;
    wire [10:0] t_cy, m_vact, m_yact0;
    wire  [3:0] m_gear;
    wire  [6:0] m_vic;
    hdmi_timing mtim (
        .clk(pix_clk), .rst(pix_rst), .mode(mode_pix), .align(align), .valid(), .aligned(),
        .ce(pce), .rgb(), .de(t_de), .hs(t_hs), .vs(t_vs), .cx(t_cx), .cy(t_cy), .running(running),
        .hact(m_hact), .vact(m_vact), .xact0(m_xact0), .yact0(m_yact0), .gear(m_gear), .htot_m1(m_htot_m1), .vtot_m1(),
        .vic(m_vic), .rate(), .sx_e(), .fw_m1(), .sy_e(), .ey_e(), .ph_e(), .mode_q(m_mode)
    );
    assign fstart = pce && t_cx == 12'd0 && t_cy == 11'd0;
    wire [1:0]  m_aspect = hm_aspect(m_mode);

    // ---- the VIC picture on the timing's counters, at the scale in use (the same geometry feeds the freeze text)
    wire [23:0] s_rgb;
    wire        s_de, s_hs, s_vs, sc_locked;
    wire [11:0] s_cx;
    wire [10:0] s_cy;
    wire [81:0] geom;                              // hm_geom(m_mode, scale_pix), registered in vic_scaler
    vic_scaler scaler (
        .vclk(sys_clk), .pix(pix_color), .px(pix_x), .py(pix_y),
        .clk(pix_clk), .rst(pix_rst), .ce(pce),
        .pal_clk(cpu_clk), .pal_we(pal_we), .pal_addr(pal_addr), .pal_wdata(pal_wdata),
        .mode(m_mode), .scale(scale_pix), .smooth(smooth_pix), .t_de(t_de), .t_hs(t_hs), .t_vs(t_vs), .t_cx(t_cx), .t_cy(t_cy),
        .xact0(m_xact0), .yact0(m_yact0), .hact(m_hact), .vact(m_vact), .htot_m1(m_htot_m1), .geom(geom), .align(align),
        .rgb(s_rgb), .de(s_de), .hs(s_hs), .vs(s_vs), .cx(s_cx), .cy(s_cy), .locked(sc_locked),
        .blank(c64_halted), .x_text(x_text), .scan(scanlines)
    );

    // ---- the app's menu (0x10140000) on top of the picture; the mode status is read over the overlay's data
    wire [7:0]  o_rdata;
    assign ovl_rdata = mr_rhit ? mr_rdata : o_rdata;
    wire [23:0] o_rgb;
    wire        o_de, o_hs, o_vs;
    wire [11:0] o_cx;
    wire [10:0] o_cy;
    overlay ovl (
        .clk_sys(cpu_clk), .we(ovl_we), .addr(ovl_addr), .wdata(ovl_wdata), .rdata(o_rdata),
        .clk(pix_clk), .ce(pce), .hact(m_hact), .vact(m_vact), .xact0(m_xact0), .yact0(m_yact0),
        .rgb_in(s_rgb), .de_in(s_de), .hs_in(s_hs), .vs_in(s_vs),
        .cx_in(s_cx), .cy_in(s_cy),
        .rgb(o_rgb), .de(o_de), .hs(o_hs), .vs(o_vs), .cx(o_cx), .cy(o_cy),
        .own_keyboard(ovl_own)
    );

    // ---- the pixel layer (DDR2) on top of that, and the still capture
    wire [23:0] p_rgb;
    wire        p_de, p_hs, p_vs;
    wire [11:0] p_cx;
    wire [10:0] p_cy;
    pix_layer #(.SMOOTH(SMOOTH)) pxl (
        .clk_sys(cpu_clk), .we(px_we), .re(px_re), .addr(px_addr), .wdata(px_wdata),
        .rdata(pxl_rdata), .ev_vsync(px_ev_vsync), .ev_flip(px_ev_flip),
        .ui_clk(ui_clk), .ui_rst(ui_rst), .v_addr(v_addr), .v_cmd(v_cmd), .v_en(v_en),
        .v_rdy(v_rdy), .v_wdf_data(v_wdf_data), .v_wdf_mask(v_wdf_mask), .v_wdf_wren(v_wdf_wren),
        .v_rd_data(v_rd_data), .v_rd_data_valid(v_rd_data_valid), .v_urgent(v_urgent),
        .vclk(sys_clk), .pix(pix_color), .px(pix_x), .py(pix_y), .v60(v60),
        .clk(pix_clk), .ce(pce), .hact(m_hact), .vact(m_vact), .xact0(m_xact0), .yact0(m_yact0),
        .rgb_in(o_rgb), .de_in(o_de), .hs_in(o_hs), .vs_in(o_vs), .cx_in(o_cx), .cy_in(o_cy),
        .rgb(p_rgb), .de(p_de), .hs(p_hs), .vs(p_vs), .cx(p_cx), .cy(p_cy)
    );

    // ---- the freeze menu's C64 text on top: the still the app drew on the pixel layer stays below it; its area and steps are the scale's (hm_geom)
    wire [23:0] rgb;
    wire        de, hs, vs;
    wire [11:0] cx;
    wire [10:0] cy;
    c64_text txt (
        .clk_sys(cpu_clk), .we(px_we), .re(px_re), .addr(px_addr), .wdata(px_wdata),
        .rdata(tx_rdata),
        .cclk(sys_clk), .halted(c64_halted), .vic_view(tx_view),
        .pal_clk(cpu_clk), .pal_we(pal_we), .pal_addr(pal_addr), .pal_wdata(pal_wdata),
        .port_req(tx_req), .port_addr(tx_paddr), .port_rom(tx_rom), .port_grant(tx_go),
        .port_rdata(ram_rdata), .cram_addr(tx_cram_addr), .cram_grant(cram_grant),
        .cram_rdata(cram_rdata),
        .clk(pix_clk), .ce(pce), .xact0(m_xact0), .yact0(m_yact0), .xt0(geom[22:11]), .yt0(geom[10:0]),
        .hn(geom[81:76]), .hd(geom[75:70]), .tvn(geom[57:52]), .tvd(geom[51:46]),
        .rgb_in(p_rgb), .de_in(p_de), .hs_in(p_hs), .vs_in(p_vs), .cx_in(p_cx), .cy_in(p_cy),
        .rgb(rgb), .de(de), .hs(hs), .vs(vs), .cx(cx), .cy(cy)
    );

    // ---- audio: the samples held in the mclk domain, taken over here (the encoder sends them at its own 48 kHz; a pair is repeated or
    // skipped now and then, the two clocks being 0.005 % apart)
    (* ASYNC_REG = "TRUE" *) reg [2:0] snd_tgl_s = 3'b000;
    reg [47:0] hdmi_snd;
    always @(posedge pix_clk) begin
        snd_tgl_s <= {snd_tgl_s[1:0], snd_tgl};
        if (snd_tgl_s[2] != snd_tgl_s[1]) hdmi_snd <= snd_hold;
    end
    wire hdmi_sample_en;

    // ---- the encoder: the chain's outputs are registers that change on the edge ending a `ce` clock; the frame constants are the mode's, in
    // pixels. ce_enc also runs while the timing is reset (ce is low then) so that the encoder's reset reaches its registers.
    // the pixel clock rate for the encoder's 48 kHz audio tick and N / CTS: the row's (hm_rate) in the family's first System Mode (PAL, NTSC),
    // hm_rate_alt in its second (NTSC-50, PAL-60; T40). The VIC code and the frame are the timing's.
    (* ASYNC_REG = "TRUE" *) reg [1:0] vm_p1 = 2'd0, vm_p = 2'd0;
    reg  [31:0] hdmi_rate = 32'd148869143;
    always @(posedge pix_clk) begin
        vm_p1 <= vmode; vm_p <= vm_p1;
        hdmi_rate <= (vm_p == 2'd2 || vm_p == 2'd3) ? hm_rate_alt(m_mode) : hm_rate(m_mode);
    end
    wire [9:0] tmds_0, tmds_1, tmds_2;
    wire [11:0] e_sx = m_xact0;
    wire [11:0] e_fwm1 = m_xact0 + m_hact - 12'd1;
    wire [10:0] e_sy = m_yact0;
    wire [10:0] e_ey = m_yact0 + m_vact;
    trx_hdmi #(
        .DVI_OUTPUT(1'b0), .IT_CONTENT(1'b1), .VIDEO_ID_CODE(0), .VIDEO_RATE(149_138_494), .RUNTIME(1), .MAX_PACKETS(3),
        .AUDIO_RATE(48_000), .AUDIO_BIT_WIDTH(24),
        .BIT_WIDTH(12), .BIT_HEIGHT(11),
        .VENDOR_NAME(64'h5452583634000000),                        // "TRX64"
        .PRODUCT_DESCRIPTION(128'h43363420556C74696D61746500000000), // "C64 Ultimate"
        .SOURCE_DEVICE_INFORMATION(8'h08)
    ) hdmi_enc (
        .clk_pixel_x5(1'b0), .clk_pixel(pix_clk), .ce(pce | pix_rst), .reset(pix_rst),
        .rgb(rgb), .hsync(hs), .vsync(vs), .cx(cx), .cy(cy), .de(de),
        .vic_code(m_vic), .video_rate(hdmi_rate), .aspect(m_aspect),
        .sx_e(e_sx), .fw_m1(e_fwm1), .sy_e(e_sy), .ey_e(e_ey),
        .audio_sample_word_0(hdmi_snd[47:24]), .audio_sample_word_1(hdmi_snd[23:0]),
        .audio_sample_en(hdmi_sample_en),
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

    // ---- T40: the HDMI output as measured: frames per second (clk50 window) and pixel clocks per frame, from the timing's vsync
    wire video_locked = running && sc_locked;
    reg        hm_vs_q = 1'b0, hm_tgl = 1'b0;
    reg [23:0] hm_pc = 24'd0, hm_pcf = 24'd0;
    always @(posedge pix_clk) begin
        hm_vs_q <= t_vs;
        hm_pc   <= hm_pc + 24'd1;
        if (t_vs && !hm_vs_q) begin hm_pcf <= hm_pc; hm_pc <= 24'd1; hm_tgl <= ~hm_tgl; end
    end
    (* ASYNC_REG = "TRUE" *) reg [2:0] hm_tgl_s = 3'b000;
    (* ASYNC_REG = "TRUE" *) reg [2:0] hm_lk1 = 3'b000, hm_lk = 3'b000;
    reg [25:0] hm_sec = 26'd0;
    reg [11:0] hm_fc = 12'd0, hm_fps = 12'd0;
    always @(posedge clk50) begin
        hm_tgl_s <= {hm_tgl_s[1:0], hm_tgl};
        hm_lk1 <= {gt_ready, pix_locked, video_locked}; hm_lk <= hm_lk1;
        hm_sec <= (hm_sec == 26'd49_999_999) ? 26'd0 : hm_sec + 26'd1;
        if (hm_sec == 26'd49_999_999) begin hm_fps <= hm_fc; hm_fc <= 12'd0; end
        else if (hm_tgl_s[2] != hm_tgl_s[1]) hm_fc <= hm_fc + 12'd1;
    end
    always @(posedge sys_clk) hdmi_meas <= {hm_lk, 1'b0, hm_fps, hm_pcf[23:8]};

    (* ASYNC_REG = "TRUE" *) reg [1:0] vlock_sync = 2'b00;
    always @(posedge clk50) vlock_sync <= {vlock_sync[0], video_locked};  // pix_clk -> clk50
    assign video_locked_s = vlock_sync[1];
endmodule
