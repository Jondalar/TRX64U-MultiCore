// hdmi_test: first own bitstream for the C64 Ultimate.
//
// 1280x720 at 60 Hz on HDMI: colour bars, a white frame and a bouncing
// square. TMDS goes out through the four GTP transmitters (lanes 0-2 carry
// TMDS channels 2-0, lane 3 the TMDS clock), 742.5 Mb/s each, from a 148.5 MHz
// reference that the board's CDCEL824 makes out of 27 MHz we feed it.
//
// LEDs: board LED blinks (design alive), case LED 1 on = synthesizer
// programmed, case LED 2 on = transmitters out of reset.
// The expansion port and SID socket transceivers stay disabled.
module top (
    input  wire       RMII_REFCLK,          // 50 MHz from the Ethernet PHY
    input  wire       HDMI_REFCLK_P, HDMI_REFCLK_N,
    output wire [2:0] HDMI_DATA_P, HDMI_DATA_N,
    output wire       HDMI_CLK_P, HDMI_CLK_N,
    output wire       HDMI_CLK_SYNTH,       // reference into the HDMI synthesizer
    output wire       CLOCK_C64_SYNTH,      // reference into the C64 synthesizer
    inout  wire       I2C18_SCL, I2C18_SDA,
    inout  wire       HDMI_SCL, HDMI_SDA,
    output wire       HDMI_DDC_EN,
    input  wire       HDMI_HPD,
    output wire       LED_BOARDn, LED_CASE1n, LED_CASE2n,
    output wire       SLOT_ADDROEn, SLOT_DATAOEn, SLOT_ROMLn, SLOT_ROMHn
);
    assign SLOT_ADDROEn = 1'b1;
    assign SLOT_DATAOEn = 1'b1;
    assign SLOT_ROMLn   = 1'b1;
    assign SLOT_ROMHn   = 1'b1;
    assign HDMI_DDC_EN  = 1'b0;

    // ---- 50 MHz system clock and power-on reset --------------------------
    wire clk50_ibuf, clk50;
    IBUF  ib50 (.I(RMII_REFCLK), .O(clk50_ibuf));
    BUFG  bg50 (.I(clk50_ibuf), .O(clk50));
    reg [15:0] por = 0;
    wire sys_rst = ~por[15];
    always @(posedge clk50) if (!por[15]) por <= por + 1;

    // ---- 27 MHz for both synthesizers ------------------------------------
    wire fb, clk27_mmcm, clk27, mmcm_locked;
    MMCME2_BASE #(
        .CLKIN1_PERIOD(20.0), .DIVCLK_DIVIDE(2), .CLKFBOUT_MULT_F(40.5),   // VCO 1012.5 MHz
        .CLKOUT0_DIVIDE_F(37.5)                                           // 27.0 MHz
    ) mmcm (
        .CLKIN1(clk50_ibuf), .CLKFBIN(fb), .CLKFBOUT(fb), .CLKOUT0(clk27_mmcm),
        .LOCKED(mmcm_locked), .RST(sys_rst), .PWRDWN(1'b0),
        .CLKOUT0B(), .CLKOUT1(), .CLKOUT1B(), .CLKOUT2(), .CLKOUT2B(), .CLKOUT3(),
        .CLKOUT3B(), .CLKOUT4(), .CLKOUT5(), .CLKOUT6(), .CLKFBOUTB()
    );
    BUFG bg27 (.I(clk27_mmcm), .O(clk27));
    ODDR #(.DDR_CLK_EDGE("SAME_EDGE")) fwd_hdmi (.Q(HDMI_CLK_SYNTH), .C(clk27), .CE(1'b1),
        .D1(1'b1), .D2(1'b0), .R(1'b0), .S(1'b0));
    ODDR #(.DDR_CLK_EDGE("SAME_EDGE")) fwd_c64 (.Q(CLOCK_C64_SYNTH), .C(clk27), .CE(1'b1),
        .D1(1'b1), .D2(1'b0), .R(1'b0), .S(1'b0));

    // ---- program both synthesizers ---------------------------------------
    wire scl_low, sda_low, i2c_done;
    cdce_i2c i2c (.clk(clk50), .rst(sys_rst | ~mmcm_locked), .scl_low(scl_low),
                  .sda_low(sda_low), .done(i2c_done));
    IOBUF b_scl18 (.IO(I2C18_SCL), .I(1'b0), .T(~scl_low), .O());
    IOBUF b_sda18 (.IO(I2C18_SDA), .I(1'b0), .T(~sda_low), .O());
    IOBUF b_sclhd (.IO(HDMI_SCL),  .I(1'b0), .T(~scl_low), .O());
    IOBUF b_sdahd (.IO(HDMI_SDA),  .I(1'b0), .T(~sda_low), .O());

    // give the synthesizer PLL 50 ms to settle before the transmitters start
    reg [21:0] settle = 0;
    wire gt_rst = ~settle[21];
    always @(posedge clk50)
        if (!i2c_done) settle <= 0;
        else if (!settle[21]) settle <= settle + 1;

    // ---- transmitters ----------------------------------------------------
    wire        usrclk, gt_ready;
    wire [19:0] w_red, w_green, w_blue;
    wire [19:0] w_clk = 20'b00000_11111_00000_11111;
    hdmi_gtp gtp (
        .sysclk(clk50), .reset(gt_rst),
        .refclk_p(HDMI_REFCLK_P), .refclk_n(HDMI_REFCLK_N),
        // Lane 2 (TMDS 0, blue) is wired with P and N swapped on the board:
        // sent as is, 0xFF blue arrived as 0x01 (a white square showed yellow).
        .tx0(w_red), .tx1(w_green), .tx2(~w_blue), .tx3(w_clk),
        .usrclk(usrclk), .ready(gt_ready),
        .txp({HDMI_CLK_P, HDMI_DATA_P[0], HDMI_DATA_P[1], HDMI_DATA_P[2]}),
        .txn({HDMI_CLK_N, HDMI_DATA_N[0], HDMI_DATA_N[1], HDMI_DATA_N[2]})
    );

    // ---- picture ---------------------------------------------------------
    (* ASYNC_REG = "TRUE" *) reg [2:0] vrst_sync = 3'b111;
    always @(posedge usrclk) vrst_sync <= {vrst_sync[1:0], ~gt_ready};
    wire vrst = vrst_sync[2];
    wire [23:0] p0, p1;
    wire de, hs, vs;
    video_720p video (.clk(usrclk), .rst(vrst), .rgb0(p0), .rgb1(p1), .de(de), .hs(hs), .vs(vs));
    tmds_pair t_blue  (.clk(usrclk), .rst(vrst), .d0(p0[7:0]),   .d1(p1[7:0]),
                       .c0({vs, hs}), .c1({vs, hs}), .de0(de), .de1(de), .q(w_blue));
    tmds_pair t_green (.clk(usrclk), .rst(vrst), .d0(p0[15:8]),  .d1(p1[15:8]),
                       .c0(2'b00), .c1(2'b00), .de0(de), .de1(de), .q(w_green));
    tmds_pair t_red   (.clk(usrclk), .rst(vrst), .d0(p0[23:16]), .d1(p1[23:16]),
                       .c0(2'b00), .c1(2'b00), .de0(de), .de1(de), .q(w_red));

    // ---- LEDs ------------------------------------------------------------
    reg [25:0] blink = 0;
    always @(posedge clk50) blink <= blink + 1;
    assign LED_BOARDn = blink[25];
    assign LED_CASE1n = ~i2c_done;
    assign LED_CASE2n = ~gt_ready;
endmodule
