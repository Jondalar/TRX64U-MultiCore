// T255: DDT's Mandelbrot demo as a LARGE core: one flat design of the XC7A50T, nothing but his engines, a frame buffer and the HDMI output. Written for this project
// (TREX), GPL v3. This is not a copy of the host's top.v: there is no RISC-V, no DDR2, no network, no SD card, no USB, no keyboard scan, no application. What the board
// needs from the FPGA to show a picture, and nothing else:
//   - the 50 MHz of the Ethernet PHY's oscillator (RMII_REFCLK, a clock pin only);
//   - the HDMI synthesizer (CDCEL): its reference from mmcm_sys -> mmcm_ref (the host's chain, 31.53 -> 37.89 MHz, PAL), its registers over I2C (cdce_mode, both buses until
//     done, as the host's top.v does, then released), the GTP reference (HDMI_REFCLK) and the transmitters (ddt_hdmi);
//   - the 100 MHz of the engines (mmcm_cpu);
//   - port 2's joystick contacts and RESTORE, and the two enables of the CIA-side buffers (without them the contacts never reach the FPGA, host top.v).
// What this top leaves alone stays unconfigured: the pins that are not ports float (BITSTREAM.CONFIG.UNUSEDPIN PULLNONE, mono/pins.xdc).
// The way back to the host is the power cycle (legacy.h: nothing returns).
`timescale 1ns / 1ps
(* use_dsp = "no" *)
module top #(
    parameter integer MANDEL_CORES = 20          // DSP48E1: 6 per engine; the device has 120
) (
    input  wire       RMII_REFCLK,               // 50 MHz
    input  wire       HDMI_REFCLK_P, HDMI_REFCLK_N,
    output wire [2:0] HDMI_DATA_P, HDMI_DATA_N,
    output wire       HDMI_CLK_P, HDMI_CLK_N,
    output wire       HDMI_CLK_SYNTH,            // reference into the HDMI synthesizer
    inout  wire       I2C18_SCL, I2C18_SDA,
    inout  wire       HDMI_SCL, HDMI_SDA,
    output wire       HDMI_DDC_EN,
    input  wire       HDMI_HPD,
    input  wire [4:0] KBJOY_2_JOY,               // port 2: up, down, left, right, fire; low = pressed
    input  wire       KBJOY_RESTORE,             // low = pressed
    output wire       CIA_BUFFER_EN, CIA_PWR_EN  // C64-side port buffers and their supply
);
    assign CIA_BUFFER_EN = 1'b1;
    assign CIA_PWR_EN    = 1'b1;

    // ---- 50 MHz and power-on reset
    wire clk50_ibuf, clk50;
    IBUF  ib50 (.I(RMII_REFCLK), .O(clk50_ibuf));
    BUFG  bg50 (.I(clk50_ibuf), .O(clk50));
    reg [15:0] por = 16'd0;
    wire por_rst = ~por[15];
    always @(posedge clk50) if (!por[15]) por <= por + 16'd1;

    // ---- the synthesizer's reference, as the host makes it for the PAL rows: dot4x 31.5278 MHz = 50 x 56.75 / 3 / 30, then 125/104 of it = 37.894 MHz
    wire fb1, sys_mmcm, sys_free, sys_locked;
    MMCME2_BASE #(
        .CLKIN1_PERIOD(20.0), .DIVCLK_DIVIDE(3), .CLKFBOUT_MULT_F(56.75),   // VCO 945.833 MHz
        .CLKOUT0_DIVIDE_F(30.0)                                            // 31.5278 MHz
    ) mmcm_sys (
        .CLKIN1(clk50_ibuf), .CLKFBIN(fb1), .CLKFBOUT(fb1), .CLKOUT0(sys_mmcm),
        .LOCKED(sys_locked), .RST(por_rst), .PWRDWN(1'b0),
        .CLKOUT0B(), .CLKOUT1(), .CLKOUT1B(), .CLKOUT2(), .CLKOUT2B(), .CLKOUT3(),
        .CLKOUT3B(), .CLKOUT4(), .CLKOUT5(), .CLKOUT6(), .CLKFBOUTB()
    );
    BUFG bg_sysfree (.I(sys_mmcm), .O(sys_free));
    wire fb2, ref_mmcm, ref_clk, ref_locked;
    MMCME2_BASE #(
        .CLKIN1_PERIOD(31.718), .DIVCLK_DIVIDE(1), .CLKFBOUT_MULT_F(31.25), // VCO 985.24 MHz
        .CLKOUT0_DIVIDE_F(26.0)                                            // 37.894 MHz
    ) mmcm_ref (
        .CLKIN1(sys_free), .CLKFBIN(fb2), .CLKFBOUT(fb2), .CLKOUT0(ref_mmcm),
        .LOCKED(ref_locked), .RST(~sys_locked), .PWRDWN(1'b0),
        .CLKOUT0B(), .CLKOUT1(), .CLKOUT1B(), .CLKOUT2(), .CLKOUT2B(), .CLKOUT3(),
        .CLKOUT3B(), .CLKOUT4(), .CLKOUT5(), .CLKFBOUTB()
    );
    BUFG bg_ref (.I(ref_mmcm), .O(ref_clk));
    ODDR #(.DDR_CLK_EDGE("SAME_EDGE")) fwd_hdmi (.Q(HDMI_CLK_SYNTH), .C(ref_clk), .CE(1'b1),
        .D1(1'b1), .D2(1'b0), .R(1'b0), .S(1'b0));
    wire clocks_ok = sys_locked & ref_locked;

    // ---- the engines' clock: 100 MHz = 50 x 20 / 10
    wire fb3, cpu_mmcm, cpu_clk, cpu_locked;
    MMCME2_BASE #(
        .CLKIN1_PERIOD(20.0), .DIVCLK_DIVIDE(1), .CLKFBOUT_MULT_F(20.0),   // VCO 1000 MHz
        .CLKOUT0_DIVIDE_F(10.0)                                            // 100 MHz
    ) mmcm_cpu (
        .CLKIN1(clk50_ibuf), .CLKFBIN(fb3), .CLKFBOUT(fb3), .CLKOUT0(cpu_mmcm),
        .LOCKED(cpu_locked), .RST(por_rst), .PWRDWN(1'b0),
        .CLKOUT0B(), .CLKOUT1(), .CLKOUT1B(), .CLKOUT2(), .CLKOUT2B(), .CLKOUT3(),
        .CLKOUT3B(), .CLKOUT4(), .CLKOUT5(), .CLKFBOUTB()
    );
    BUFG bg_cpu (.I(cpu_mmcm), .O(cpu_clk));
    wire cpu_rst;
    ddt_rst_sync rs_cpu (.clk(cpu_clk), .arst(~cpu_locked), .rst(cpu_rst));

    // ---- the HDMI synthesizer's I2C: the sequencer drives the 1.8 V bus and the HDMI bus until done (the CDCEL sits on one of them, by board revision: host top.v), then both are released
    wire scl_low, sda_low, i2c_done;
    IOBUF b_scl18 (.IO(I2C18_SCL), .I(1'b0), .T(~(scl_low & ~i2c_done)), .O());
    IOBUF b_sda18 (.IO(I2C18_SDA), .I(1'b0), .T(~(sda_low & ~i2c_done)), .O());
    IOBUF b_sclhd (.IO(HDMI_SCL),  .I(1'b0), .T(~(scl_low & ~i2c_done)), .O());
    IOBUF b_sdahd (.IO(HDMI_SDA),  .I(1'b0), .T(~(sda_low & ~i2c_done)), .O());

    // give the synthesizer PLL 50 ms to settle before the transmitters start (the host's `settle`)
    reg [21:0] settle = 22'd0;
    wire gt_rst = ~settle[21];
    always @(posedge clk50)
        if (!i2c_done) settle <= 22'd0;
        else if (!settle[21]) settle <= settle + 22'd1;

    // ---- the picture
    wire        pix_clk, pix_rst, gt_ready;
    wire [11:0] xact0, t_cx, cx;
    wire [10:0] yact0, t_cy, cy;
    wire        t_de, t_hs, t_vs, de, hs, vs;
    wire [23:0] rgb;
    ddt_hdmi hd (
        .clk50(clk50), .cdce_rst(por_rst | ~clocks_ok), .gt_rst(gt_rst),
        .refclk_p(HDMI_REFCLK_P), .refclk_n(HDMI_REFCLK_N),
        .data_p(HDMI_DATA_P), .data_n(HDMI_DATA_N), .clk_p(HDMI_CLK_P), .clk_n(HDMI_CLK_N),
        .scl_low(scl_low), .sda_low(sda_low), .i2c_done(i2c_done), .gt_ready(gt_ready),
        .pix_clk(pix_clk), .pix_rst(pix_rst), .xact0(xact0), .yact0(yact0),
        .t_cx(t_cx), .t_cy(t_cy), .t_de(t_de), .t_hs(t_hs), .t_vs(t_vs),
        .rgb(rgb), .de(de), .hs(hs), .vs(vs), .cx(cx), .cy(cy)
    );

    // DDC (the monitor's EDID bus) is switched through once the synthesizer sequence is over and the monitor's hot-plug line is up; the host's application does the same, later
    (* ASYNC_REG = "TRUE" *) reg [1:0] hpd_s = 2'b00;
    always @(posedge clk50) hpd_s <= {hpd_s[0], HDMI_HPD};
    assign HDMI_DDC_EN = i2c_done & hpd_s[1];

    // ---- port 2 and RESTORE into the engines' clock (two flip-flops; the contacts are active low, the pull-ups are in pins.xdc)
    (* ASYNC_REG = "TRUE" *) reg [5:0] jin1 = 6'h3f, jin2 = 6'h3f;
    always @(posedge cpu_clk) begin
        jin1 <= {KBJOY_RESTORE, KBJOY_2_JOY};
        jin2 <= jin1;
    end

    ddt_mandel_hd #(.MANDEL_CORES(MANDEL_CORES)) demo (
        .cpu_clk(cpu_clk), .cpu_rst(cpu_rst), .joy_n(jin2[4:0]), .rst_key_n(jin2[5]),
        .render_busy(), .max_iters_o(),
        .pix_clk(pix_clk), .pix_rst(pix_rst), .xact0(xact0), .yact0(yact0),
        .t_cx(t_cx), .t_cy(t_cy), .t_de(t_de), .t_hs(t_hs), .t_vs(t_vs),
        .rgb(rgb), .de(de), .hs(hs), .vs(vs), .cx(cx), .cy(cy)
    );
endmodule
