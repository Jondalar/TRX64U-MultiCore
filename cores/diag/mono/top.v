// T175 / T201: the Port Diagnostics core's copy of the host copy's top.v (mono/host/top.v; mono/top.v.diff): KBJOY_1_JOY / KBJOY_2_JOY are open-drain pads (diag_pads.v) the probe engine of layer 2 can pull low, layer2 has the two extra ports kbjoy_i / kbjoy_low. T201: the A/V port (AVID_*) is driven from layer 2's three group words (av_ga/gb/gc). Everything else is the host's.
// c64_basic: the icepi-zero-c64 C64 core (6502n, vicii-kawari, reDIP SID and
// CIA) on the C64 Ultimate, with HDMI out and nothing else.
//
// c64_top.v and everything below it is vendored in vendor/icepi; c64_core.v
// wraps it. This file gives it what the icepi board gave it:
//   - clock: dot4x, 31.53 MHz (PAL); the app's System Mode switches it live to
//     NTSC, PAL-60 or NTSC-50 (T40, mode_ctl.v).
//   - memory: block RAM (c64_mem, inside c64_core) in place of the SDRAM port.
// and replaces kawari's 576p output with 1080p50 (1080p60 in the 60 Hz modes):
//   - vic_scaler takes the VIC's native pixels, scales them 4x (4.5x
//     vertically at 60 Hz) and puts out 1080p with 2640 x 1125 (2200 x 1125)
//     clocks per frame (CEA blanking), one frame per VIC frame.
//   - The pixel clock, 148.87 MHz = 6875/1456 of dot4x (148.06 MHz = 61875/13676
//     of NTSC's), comes from the board's CDCEL824 (55/14, or 990/263) fed with
//     37.89 MHz = 125/104 of dot4x from a second MMCM; it is also the GTP
//     reference (1.489 Gb/s per lane, two pixels per 20-bit word), so picture
//     and VIC stay locked.
// Keyboard: the C64U's own, scanned over I2C (c64u_keyboard); joysticks and
// RESTORE straight from their pins. Tied off: tape. The expansion port and the
// SID sockets: docs/specs/T16-cartridge-port.md §13. The serial bus
// joins the C64, the app's drive A and the board's IEC port (iec_bus.v).
//
// Next to the C64: Gideon's RISC-V (rvlite, 100 MHz) with the stubs the U64-II
// "ultimate" app needs to boot (ultimate_sys.vhd, docs/ultimate-app-hw-spec.md
// step A). It sees the bottom 64 MB of the DDR2; its boot ROM takes the app
// over JTAG, or from flash when the bitstream in flash is this one (T15); its
// console UART goes to the UART pins and into a ring that
// tools/trxdbg.py reads (`console`). The app's menu overlay (overlay.v) is
// drawn over the scaled picture, the pixel layer (pix_layer.v) over that, and
// the freeze menu's C64 text (c64_text.v) on top.
//
// LEDs: board LED off. The case LEDs show what the app
// selects (case_leds.v); until it does, case LED 1 on = C64 out of reset,
// case LED 2 on = transmitters running and the picture locked to the VIC.
// The keyboard board's bytes and the keyboard chain's 71st LED (power lamp, floppy / hard disk activity from drive_leds_n[0]/[1], SHIFT LOCK) are bling_io.v; the case LED strip and keyboard lighting are out (OPT-bling).
module top #(
    // capability bits 29:28 (itu.h CAPAB_FPGA_TYPE): 2 = XC7A50T (C64 Ultimate),
    // 3 = XC7A100T (Ultimate 64 Elite II). The app and the boot ROM take the
    // flash layout from it; build.tcl sets it from the part (T53).
    parameter [1:0] FPGA_TYPE = 2'd2
) (
    input  wire       RMII_REFCLK,          // 50 MHz from the Ethernet PHY
    input  wire       HDMI_REFCLK_P, HDMI_REFCLK_N,
    output wire [2:0] HDMI_DATA_P, HDMI_DATA_N,
    output wire       HDMI_CLK_P, HDMI_CLK_N,
    output wire       HDMI_CLK_SYNTH,       // reference into the HDMI synthesizer
    output wire       CLOCK_C64_SYNTH,      // reference into the C64 synthesizer
    inout  wire       I2C18_SCL, I2C18_SDA,
    inout  wire       HDMI_SCL, HDMI_SDA,
    inout  wire       I2C33_SCL, I2C33_SDA,
    output wire       HDMI_DDC_EN,
    input  wire       HDMI_HPD,
    output wire       LED_BOARDn, LED_CASE1n, LED_CASE2n,
    output wire       LED_CASE3n,           // third case LED line: held off (active high)
    // expansion port and SID sockets (T16 §3.1, §13.1)
    inout  wire [15:0] SLOT_ADDR,           // A0-A11 behind the address transceiver, A12-A15 direct
    inout  wire [7:0] SLOT_DATA,
    output wire       SLOT_ADDRDIR, SLOT_ADDROEn, SLOT_DATADIR, SLOT_DATAOEn,
    output wire       SLOT_ROMLn, SLOT_ROMHn, SLOT_IO1n, SLOT_IO2n,
    output wire       SLOT_PHI2, SLOT_DOTCLK, SLOT_BA,
    inout  wire       SLOT_RWn,
    input  wire       SLOT_GAMEn, SLOT_EXROMn, SLOT_DMAn, SLOT_IRQn, SLOT_NMIn,
    input  wire       SLOT_RST_I,               // 1 = the port's RESET is low
    output wire       SLOT_RST_O,               // 1 = pull the port's RESET low
    input  wire       CIA_RSTn,                 // not driven (§13.6)
    output wire       SLOT_SID1n, SLOT_SID2n, SLOT_SID_ADDR, SLOT_PWR,
    inout  wire [4:0] KBJOY_1_JOY, KBJOY_2_JOY, // up, down, left, right, fire; low = pressed. T175: open-drain pads (diag_pads.v), released = the input they always were
    input  wire       KBJOY_RESTORE,            // low = pressed
    input  wire       KBJOY_INT,                // keyboard controller interrupt (1.8 V)
    input  wire       BLING_KEYB,               // serial line from the keyboard (Bling) board
    output wire       CIA_BUFFER_EN, CIA_PWR_EN, // C64-side port buffers and their supply
    output wire       AUDIO_MCLK, AUDIO_BCLK, AUDIO_LRCLK, AUDIO_SDI,  // I2S to the codec
    output wire       SPEAKER_DATA,         // I2S data to the speaker amplifier (same BCLK/LRCK)
    input  wire       AUDIO_SDO,            // codec ADC data (the SID sockets' audio), passed to the core (T103)
    // pins only for the cores (T103): the cassette port, the POT comparators of the joystick ports, the SID sockets' selects
    inout  wire       C2N_MOTOR, C2N_SENSE, C2N_READ, C2N_WRITE,    // direction of MOTOR unknown: released unless the core enables it; SENSE open drain; READ and WRITE released unless enabled
    input  wire [3:0] PADDLE_CMP,           // 1.8 V
    output wire       PADDLE_DIS, PADDLE_ADJ,            // high-Z unless the core enables them
    output wire       UART_TXD,             // RISC-V console, 115200 8N1
    input  wire       UART_RXD,
    // Ethernet PHY (KSZ8081, RMII; its 50 MHz reference is RMII_REFCLK)
    input  wire       RMII_CRS_DV,
    input  wire [1:0] RMII_RX_DATA,
    output wire       RMII_TX_EN,
    output wire [1:0] RMII_TX_DATA,
    output wire       MDIO_CLK,
    output wire       ETH_LED_YELn,         // T62 G19: low = on (Ethernet carrier seen in the last 2.7 s)
    inout  wire       MDIO_DATA,
    // ESP32 (control module) UART, for the app's WiFi/power/rocker link
    output wire       WIFI_TXD, WIFI_RTS,
    output wire       WIFI_BOOT, WIFI_DIS,  // T152 S8: ESP32 GPIO0 / EN, high-Z until esp_ctl is armed
    output wire       SD_SSn, SD_CLK, SD_MOSI,  // SD card, SPI mode
    input  wire       SD_MISO, SD_CARDDETn,
    output wire       FLASH_CSn, FLASH_MOSI,    // configuration flash; SCK is CCLK
    input  wire       FLASH_MISO,
    input  wire       WIFI_RXD, WIFI_CTS,
    input  wire [2:0] UI_BUTTON,            // T195: the board's three buttons (left, middle, right), low = pressed, pull-ups in pins.xdc
    input  wire       ULPI_CLOCK, ULPI_NXT, ULPI_DIR,   // USB PHY (usb_phy.v)
    output wire       ULPI_STP, ULPI_RESET, ULPI_REFCLK,
    inout  wire [7:0] ULPI_DATA,
    output wire       HUB_RESETn, HUB_CLOCK,
    // serial bus (IEC) port: _O = 1 pulls the line low, _I = 1 while it is low
    input  wire       IEC_ATN_I, IEC_CLOCK_I, IEC_DATA_I, IEC_RESET_I,   // IEC_RESET_I: T62 G19, 1 while the bus RESET line is low
    output wire       IEC_ATN_O, IEC_CLOCK_O, IEC_DATA_O, IEC_RESET_O, IEC_SRQ_O,
    // DDR2 (constrained in pins.xdc: Gideon's HSTL standards, bank 35)
    inout  wire [15:0] ddr2_dq,
    inout  wire [1:0]  ddr2_dqs_n, ddr2_dqs_p,
    output wire [12:0] ddr2_addr,
    output wire        SDRAM_A13,
    output wire [2:0]  ddr2_ba,
    output wire        ddr2_ras_n, ddr2_cas_n, ddr2_we_n,
    output wire [0:0]  ddr2_ck_p, ddr2_ck_n, ddr2_cke, ddr2_cs_n, ddr2_odt,
    output wire [1:0]  ddr2_dm,
    output wire        BLING_LED,           // keyboard LEDs, WS2812 chain
    output wire        LED_DATA, LED_CLOCK, // case LED strip, WS2812 or APA102
    // T201 (the diag core's A/V test picture): the A/V port's THS8136 DAC, three 8 bit groups from layer 2 (diag_avout.v); balls in avid_pins.xdc
    output wire [9:2]  AVID_R, AVID_G, AVID_B,
    output wire        AVID_CLK, AVID_SYNCn, AVID_FB
);
    // T79 effective decouple (sys_clk): the JTAG bit (dc_jtag, declared below) or the app's request
    // (ultimate_sys 0x10060620 bit 0, cpu_clk, 2-FF synchronised). Every user below uses this one.
    wire         app_decouple, app_dc_sys, core_decouple;
    wire         app_port_release, app_prel_sys;   // T135 D6 expansion port lock: the application's release bit (icap region +0x21), in sys_clk
    // What the host has, in the capability word: the ITU word the bootloader and the application read (ultimate_sys,
    // unchanged by T103) and, as a constant, the contract input host_caps, which the core ORs with its own bits.
`ifdef NO_NET
    // T164 console flat builds (NO_NET): no Ethernet in the design. CAPAB_ETH_RMII (bit 24) is off, so the application starts no
    // RMII driver (rmii_interface.cc tests the bit); the WiFi UART stays
    localparam [31:0] HOST_CAPS = {2'b00, FPGA_TYPE, 28'h4E40202};
    localparam        NET_ON    = 0;
`else
    localparam [31:0] HOST_CAPS = {2'b00, FPGA_TYPE, 28'h5E40202};
    localparam        NET_ON    = 1;
`endif

    // The expansion port (T16 §13): c64_core's slot_port.v drives it; idle
    // (transceivers off with DIR towards the port, strobes high, PHI2/DOTCLK
    // low, BA high, A12-A15 and R/W released) unless the app's
    // C64_BUS_EXTERNAL selects it. SID sockets deselected, port power on as in
    // Gideon's design.
    wire [15:0] slot_a;
    wire  [7:0] slot_d_o;
    wire        slot_ahi_t, slot_rw, slot_d_t, slot_rst_o;
    wire  [3:0] bus_int_cpu, bus_ext_cpu;
    wire        slot_wr_en;
    wire [31:0] slot_status, slot_cnt0, slot_cnt1, slot_last;
    assign SLOT_ADDR[11:0]  = slot_a[11:0];
    assign SLOT_ADDR[15:12] = slot_ahi_t ? 4'bzzzz : slot_a[15:12];
    assign SLOT_RWn         = slot_ahi_t ? 1'bz : slot_rw;
    assign SLOT_DATA        = slot_d_t ? 8'bzzzzzzzz : slot_d_o;
    assign SLOT_RST_O    = slot_rst_o;
    // SLOT_SID1n/2n/_ADDR: the core's (l2_sid_sock*, idle 1/1/0), below
    assign SLOT_PWR      = 1'b1;
    // Gideon's design holds both high (JTAG SAMPLE, 2026-09-30); without them
    // the joystick and keyboard lines never reach the FPGA.
    assign CIA_BUFFER_EN = 1'b1;
    assign CIA_PWR_EN    = 1'b1;
    assign SDRAM_A13    = 1'b0;             // 1 Gbit part: A13 unused

    // ---- 50 MHz and power-on reset -------------------------------------------
    wire clk50_ibuf, clk50;
    IBUF  ib50 (.I(RMII_REFCLK), .O(clk50_ibuf));
    BUFG  bg50 (.I(clk50_ibuf), .O(clk50));
    reg [15:0] por = 0;
    wire por_rst = ~por[15];
    always @(posedge clk50) if (!por[15]) por <= por + 1;

    // ---- sys = dot4x, sid2x = 2 x dot4x -------------------------------------------
    // PAL from power-on: 50 x 56.75 / 3 / 30 = 31.5278 MHz, with an integer
    // divider, so that a second output can run at exactly twice that,
    // edge-aligned: the T25.1 SID engine's clock (T25.1). The other
    // System Modes (T40) reprogram it through its DRP (mode_ctl.v). While that
    // runs, sys and sid2x are stopped by their BUFGCEs (the MMCM's outputs are
    // not clean until it has locked); their enable is taken over on an ungated
    // copy of the output (sys_free).
    wire fb1, sys_mmcm, sys_clk, sys_locked, sid2x_mmcm, sid2x_clk, sys_free;
    wire        mmcm_drst, mmcm_den, mmcm_dwe, mmcm_drdy;
    wire  [6:0] mmcm_daddr;
    wire [15:0] mmcm_di, mmcm_do;
    MMCME2_ADV #(
        .CLKIN1_PERIOD(20.0), .DIVCLK_DIVIDE(3), .CLKFBOUT_MULT_F(56.75),   // VCO 945.833 MHz
        .CLKOUT0_DIVIDE_F(30.0),                                           // 31.5278 MHz
        .CLKOUT1_DIVIDE(15)                                                // 63.0556 MHz
    ) mmcm_sys (
        .CLKIN1(clk50_ibuf), .CLKIN2(1'b0), .CLKINSEL(1'b1), .CLKFBIN(fb1), .CLKFBOUT(fb1),
        .CLKOUT0(sys_mmcm), .LOCKED(sys_locked), .RST(por_rst | mmcm_drst), .PWRDWN(1'b0),
        .CLKOUT0B(), .CLKOUT1(sid2x_mmcm), .CLKOUT1B(), .CLKOUT2(), .CLKOUT2B(), .CLKOUT3(),
        .CLKOUT3B(), .CLKOUT4(), .CLKOUT5(), .CLKOUT6(), .CLKFBOUTB(),
        .DCLK(clk50), .DADDR(mmcm_daddr), .DI(mmcm_di), .DO(mmcm_do), .DEN(mmcm_den),
        .DWE(mmcm_dwe), .DRDY(mmcm_drdy),
        .PSCLK(1'b0), .PSEN(1'b0), .PSINCDEC(1'b0), .PSDONE(),
        .CLKINSTOPPED(), .CLKFBSTOPPED()
    );
    wire mc_sys_en;                     // mode_ctl (clk50)
    BUFG bg_sysfree (.I(sys_mmcm), .O(sys_free));
    (* ASYNC_REG = "TRUE" *) reg [2:0] sys_en_s = 3'b111;
    always @(posedge sys_free) sys_en_s <= {sys_en_s[1:0], mc_sys_en};
    BUFGCE bg_sys (.I(sys_mmcm), .CE(sys_en_s[2]), .O(sys_clk));
    BUFGCE bg_sid2x (.I(sid2x_mmcm), .CE(sys_en_s[2]), .O(sid2x_clk));
    // the C64's own copy, stopped by the machine pause (c64_core.v)
    wire c64_gclk, c64_ce;
    BUFGCE bg_c64 (.I(sys_mmcm), .CE(c64_ce & sys_en_s[2]), .O(c64_gclk));

    // synthesizer reference: 125/104 of dot4x; the HDMI synthesizer makes x4.
    // Fed from the ungated copy: during a mode switch it must lock again
    // while sys itself is still stopped (mode_ctl.v waits for both locks).
    wire fb2, ref_mmcm, ref_clk, ref_locked;
    MMCME2_BASE #(
        .CLKIN1_PERIOD(31.718), .DIVCLK_DIVIDE(1), .CLKFBOUT_MULT_F(31.25), // VCO 985.24 MHz
        .CLKOUT0_DIVIDE_F(26.0)                                            // 37.894 MHz
    ) mmcm_ref (
        .CLKIN1(sys_free), .CLKFBIN(fb2), .CLKFBOUT(fb2), .CLKOUT0(ref_mmcm),
        .LOCKED(ref_locked), .RST(~sys_locked), .PWRDWN(1'b0),
        .CLKOUT0B(), .CLKOUT1(), .CLKOUT1B(), .CLKOUT2(), .CLKOUT2B(), .CLKOUT3(),
        .CLKOUT3B(), .CLKOUT4(), .CLKOUT5(), .CLKOUT6(), .CLKFBOUTB()
    );
    BUFG bg_ref (.I(ref_mmcm), .O(ref_clk));
    ODDR #(.DDR_CLK_EDGE("SAME_EDGE")) fwd_hdmi (.Q(HDMI_CLK_SYNTH), .C(ref_clk), .CE(1'b1),
        .D1(1'b1), .D2(1'b0), .R(1'b0), .S(1'b0));
    ODDR #(.DDR_CLK_EDGE("SAME_EDGE")) fwd_c64 (.Q(CLOCK_C64_SYNTH), .C(ref_clk), .CE(1'b1),
        .D1(1'b1), .D2(1'b0), .R(1'b0), .S(1'b0));

    wire clocks_ok = sys_locked & ref_locked;
    wire sys_rst, mc_reconf;
    // a mode switch's lock loss (mc_reconf) is no reset of the sys domain
    rst_sync rs_sys (.clk(sys_clk), .arst(~clocks_ok & ~mc_reconf), .rst(sys_rst));

    // ---- DDR2 ------------------------------------------------------------------
    // 200 MHz for ddr2_lean's PLL and IDELAYCTRL, the audio MCLK (1000 / 81.375 =
    // 12.2887 MHz, 256 x 48.003 kHz) and the RISC-V's 100 MHz: they share one
    // MMCM (the 50T has five; mmcm_sys, mmcm_ref, mmcm_pix and this one are ours,
    // the fifth was the MIG's and is free since OPT-ddr2 §9).
    wire fb3, clk200_mmcm, clk200, clk200_locked, mclk_mmcm, mclk, cpu_mmcm, cpu_clk;
    MMCME2_BASE #(
        .CLKIN1_PERIOD(20.0), .DIVCLK_DIVIDE(1), .CLKFBOUT_MULT_F(20.0),   // VCO 1000 MHz
        .CLKOUT0_DIVIDE_F(81.375),                                        // 12.2887 MHz
        .CLKOUT1_DIVIDE(5),                                               // 200 MHz
        .CLKOUT2_DIVIDE(10)                                               // 100 MHz
    ) mmcm_ddr (
        .CLKIN1(clk50_ibuf), .CLKFBIN(fb3), .CLKFBOUT(fb3), .CLKOUT0(mclk_mmcm),
        .LOCKED(clk200_locked), .RST(por_rst), .PWRDWN(1'b0),
        .CLKOUT0B(), .CLKOUT1(clk200_mmcm), .CLKOUT1B(), .CLKOUT2(cpu_mmcm), .CLKOUT2B(), .CLKOUT3(),
        .CLKOUT3B(), .CLKOUT4(), .CLKOUT5(), .CLKOUT6(), .CLKFBOUTB()
    );
    BUFG bg200 (.I(clk200_mmcm), .O(clk200));
    BUFG bgmclk (.I(mclk_mmcm), .O(mclk));
    BUFG bgcpu (.I(cpu_mmcm), .O(cpu_clk));
    rst_sync rs_mclk (.clk(mclk), .arst(~clk200_locked), .rst(mclk_rst));

    wire         ui_clk, ui_rst, ddr_calib;
    wire [26:0]  app_addr;
    wire [2:0]   app_cmd;
    wire         app_en, app_rdy, app_wdf_wren, app_wdf_rdy, app_rd_data_valid;
    wire [127:0] app_wdf_data, app_rd_data;
    wire [15:0]  app_wdf_mask;
    // cart_fill's commands for mig_arb (c_)
    wire [26:0]  c_addr;
    wire [2:0]   c_cmd;
    wire         c_en, c_rdy, c_wdf_wren, c_wdf_rdy, c_rd_valid;
    wire [127:0] c_wdf_data, c_rd_data;
    wire [15:0]  c_wdf_mask;
    // the video client's (pix_layer, v_); its read data comes on c_rd_data
    wire [26:0]  v_addr;
    wire [2:0]   v_cmd;
    wire         v_en, v_rdy, v_wdf_wren, v_rd_valid, v_urgent;
    wire [127:0] v_wdf_data;
    wire [15:0]  v_wdf_mask;
    // the engine's (trx_engine, e_; frame ring and mover); read data on c_rd_data
    wire [26:0]  e_addr;
    wire [2:0]   e_cmd;
    wire         e_en, e_rdy, e_wdf_wren, e_rd_valid;
    wire [127:0] e_wdf_data;
    wire [15:0]  e_wdf_mask;
    // cart_fill reads a cartridge RAM byte before writing it, for the log
    wire         cart_rbw, ob_v;
    wire [14:0]  ob_addr;
    wire [7:0]   ob_old;
    // cartridge: ROM image (from the app: DDR2 0x3C00000, the REU block 0x1000000 or the
    // built-in slot 0xF00000, trx_io ROM_SEL, cart_fill) and RAM (0xEF0000) in
    // DDR2; cart_fill serves the core's cache with them
    wire         cart_req_tgl, cart_req_we, cart_req_cached, cart_ack_tgl;
    wire [23:0]  cart_req_addr;
    wire [7:0]   cart_req_data, cart_fill_be;
    wire [5:0]   cart_req_slot;
    wire [8:0]   cart_fill_addr;
    wire [63:0]  cart_fill_data;
    // our DDR2 controller (ddr2_lean + the calibration sequence cal_seq, ddr2_ctrl.v):
    // MIG's app_* interface, ui_clk 75 MHz from the PLL inside ddr2_lean. ddr_calib rises
    // when the calibration ended ok and never when it found no window; ddr_cal is the
    // result (ddr2_ctrl.v: CAL0 and CAL1, done / ok are bits of CAL0) for trx_io page 0
    // and the JTAG registers 0x100050-0x100052.
    wire [63:0]  ddr_cal;
    ddr2_ctrl ddr (
        .ddr2_dq(ddr2_dq), .ddr2_dqs_n(ddr2_dqs_n), .ddr2_dqs_p(ddr2_dqs_p),
        .ddr2_addr(ddr2_addr), .ddr2_ba(ddr2_ba), .ddr2_ras_n(ddr2_ras_n),
        .ddr2_cas_n(ddr2_cas_n), .ddr2_we_n(ddr2_we_n), .ddr2_ck_p(ddr2_ck_p),
        .ddr2_ck_n(ddr2_ck_n), .ddr2_cke(ddr2_cke), .ddr2_cs_n(ddr2_cs_n),
        .ddr2_dm(ddr2_dm), .ddr2_odt(ddr2_odt),
        .sys_clk_i(clk200), .sys_rst(~clk200_locked),
        .app_addr(app_addr), .app_cmd(app_cmd), .app_en(app_en),
        .app_wdf_data(app_wdf_data), .app_wdf_mask(app_wdf_mask),
        .app_wdf_wren(app_wdf_wren),
        .app_rd_data(app_rd_data), .app_rd_data_valid(app_rd_data_valid),
        .app_rdy(app_rdy), .app_wdf_rdy(app_wdf_rdy),
        .ui_clk(ui_clk), .ui_clk_sync_rst(ui_rst), .init_calib_complete(ddr_calib),
        .cal_done(), .cal_ok(), .cal_res(ddr_cal)
    );

    // DDR2 map: the RISC-V app's 64 MB at the bottom (its own addresses), with the
    // REU inside it at 16 MB (word 0x0800000, Gideon's REU_MEMORY_BASE), all of its 16 MB (T60),
    // which is also where the app puts a cartridge's ROM image (cart_fill ROM_SEL 1: the
    // expansion-port block, REU or cartridge, never both). mig_arb serves cart_fill first,
    // then drive A, then in turn the RISC-V (cpu_mem_bridge) and one port
    // shared by the REU (first) and the JTAG debug window (ddr2_port).
    localparam [25:0] REU_BASE = 26'h0800000;
    reg         ddr_req = 1'b0;
    reg  [25:0] ddr_addr;
    reg         ddr_we;
    reg  [31:0] ddr_wdata;
    reg  [3:0]  ddr_be;
    reg         ddr_for_reu;
    // trx_sampler (in c64_core, T25.6): the app's byte window (trx_io page 8) and its DDR2 reads
    wire        smp_en;                             // C64_SAMPLER_ENABLE
    wire        smp_a_tgl, smp_a_we, smp_a_rs_tgl, smp_f_tgl, smp_f_done_tgl;
    wire  [7:0] smp_a_addr, smp_a_wdata, smp_a_rdata;
    wire [26:0] smp_f_addr;
    reg  [31:0] smp_f_data = 32'd0;
    reg         ddr_for_smp = 1'b0;     // the sampler's read (T25.6): after the REU, before the JTAG window
    reg         smp_pending = 1'b0, smp_done_r = 1'b0;
    (* ASYNC_REG = "TRUE" *) reg [2:0] smp_rq_s = 3'b000;
    wire        smp_rq_ev = smp_rq_s[2] != smp_rq_s[1];
    assign      smp_f_done_tgl = smp_done_r;
    reg         ddr_wide;               // JTAG 32-bit access
    wire        ddr_ack;
    wire [31:0] ddr_rdata;
    wire [15:0] ddr_rdata16 = ddr_addr[0] ? ddr_rdata[31:16] : ddr_rdata[15:0];

    wire [23:0] reu_cmd_addr;
    wire        reu_cmd_we, reu_cmd_valid;
    wire [15:0] reu_wdata;
    wire [1:0]  reu_wdata_we;
    reg  [15:0] reu_rdata;
    reg         reu_rdata_valid;
    wire        reu_cmd_ready = !ddr_req;       // taken the clk it is offered
    wire [25:0] reu_map_addr;
    wire [3:0]  reu_map_be;
    reu_ddr_map #(.BASE(REU_BASE)) reu_map (.cmd_addr(reu_cmd_addr), .wdata_we(reu_wdata_we),
        .ddr_addr(reu_map_addr), .ddr_be(reu_map_be));
    reg         jtag_ddr_pending = 1'b0;
    reg         jtag_ddr_we;
    reg         jtag_ddr_wide;
    reg  [25:0] jtag_ddr_addr;
    reg  [31:0] jtag_ddr_wdata;
    reg         jtag_ddr_done;
    reg  [31:0] jtag_ddr_rdata;

    always @(posedge sys_clk) begin
        reu_rdata_valid <= 1'b0;
        jtag_ddr_done   <= 1'b0;
        smp_rq_s <= {smp_rq_s[1:0], smp_f_tgl};
        if (smp_rq_ev) smp_pending <= 1'b1;
        if (!ddr_req) begin
            // 16-bit clients put their word into the half addr[0] picks
            if (reu_cmd_valid) begin
                ddr_req <= 1'b1; ddr_for_reu <= 1'b1; ddr_for_smp <= 1'b0; ddr_wide <= 1'b0;
                ddr_addr  <= reu_map_addr;
                ddr_we    <= reu_cmd_we;
                ddr_wdata <= {reu_wdata, reu_wdata};
                ddr_be    <= reu_map_be;
            end else if (smp_pending) begin
                // the sampler's 32-bit read: byte address -> 16-bit word address
                ddr_req <= 1'b1; ddr_for_reu <= 1'b0; ddr_for_smp <= 1'b1; ddr_wide <= 1'b1;
                ddr_addr  <= smp_f_addr[26:1];
                ddr_we    <= 1'b0;
                ddr_be    <= 4'b1111;
                smp_pending <= 1'b0;
            end else if (jtag_ddr_pending) begin
                ddr_req <= 1'b1; ddr_for_reu <= 1'b0; ddr_for_smp <= 1'b0; ddr_wide <= jtag_ddr_wide;
                ddr_addr  <= jtag_ddr_addr;
                ddr_we    <= jtag_ddr_we;
                ddr_wdata <= jtag_ddr_wide ? jtag_ddr_wdata : {2{jtag_ddr_wdata[15:0]}};
                ddr_be    <= jtag_ddr_wide ? 4'b1111 : jtag_ddr_addr[0] ? 4'b1100 : 4'b0011;
            end
        end else if (ddr_ack) begin
            ddr_req <= 1'b0;
            if (ddr_for_reu) begin
                if (!ddr_we) begin reu_rdata <= ddr_rdata16; reu_rdata_valid <= 1'b1; end
            end else if (ddr_for_smp) begin
                smp_f_data  <= ddr_rdata;
                smp_done_r  <= ~smp_done_r;
            end else begin
                jtag_ddr_done  <= 1'b1;
                jtag_ddr_rdata <= ddr_wide ? ddr_rdata : {16'd0, ddr_rdata16};
            end
        end
        // T79.1 decoupler: the sampler's handshake starts again at 0, as the new core does after its reset
        if (core_decouple) begin
            smp_rq_s <= 3'b000; smp_pending <= 1'b0; smp_done_r <= 1'b0;
        end
    end

    wire        port_tgl, port_done_tgl;
    wire [31:0] port_done_rdata;
    ddr2_port ddr_port (
        .clk(sys_clk), .req(ddr_req), .addr(ddr_addr), .we(ddr_we), .wdata(ddr_wdata),
        .be(ddr_be), .ack(ddr_ack), .rdata(ddr_rdata),
        .req_tgl(port_tgl), .done_tgl(port_done_tgl), .done_rdata(port_done_rdata)
    );

    // ---- the RISC-V (Ultimate app) ----------------------------------------------
    wire        cpu_rst;
    reg  [4:0]  machine_ctl = 5'b00000;     // JTAG register 9 (below), sys_clk
    rst_sync rs_cpu (.clk(cpu_clk), .arst(~clk200_locked | machine_ctl[2]), .rst(cpu_rst));
    (* ASYNC_REG = "TRUE" *) reg [1:0] calib_cpu = 2'b00;
    always @(posedge cpu_clk) calib_cpu <= {calib_cpu[0], ddr_calib};

    wire        m_request, m_rwn, m_rack;
    wire [7:0]  m_tag, m_rack_tag, m_dack_tag;
    wire [25:0] m_address;
    wire [3:0]  m_byte_en;
    wire [31:0] m_wdata, m_rdata;
    wire        con_valid;
    wire [7:0]  con_data;
    wire [31:0] con_rdata, con_count;       // con_ring, after the debug port
    wire        mdio_in, mdio_out, eth_rst;
    // ports of ultimate_sys: Ethernet byte streams, C64 control
    wire [7:0] erx_data, mac_tx_data, st_tx_data;
    wire       erx_sof, erx_eof, erx_valid;
    wire       mac_tx_eof, mac_tx_valid, st_tx_eof, st_tx_valid, st_req;
    wire       mac_tx_ready;
    wire       app_mdc;
    reg [31:0] mdio_ovr = 32'd0;            // sys_clk; static levels for the pins
    // the core's packet port (T103): the UDP video/audio streams are the core's now, behind pktport_host
    wire        pp_req, pp_valid, pp_eof, pp_grant, pp_ready;
    wire [7:0]  pp_data;
    // the app's I2C master (T14): register port from ultimate_sys, bus side below
    wire        ai2c_we, ai2c_re, ai2c_own, ai2c_scan, kb_pause, kb_parked;
    wire        mc_i2c_hold, ai2c_parked, mc_kb_pause;   // T40 mode switch (mode_ctl)
    wire        mc_halt;
    wire  [1:0] vmode;                      // System Mode in use (mode_ctl, clk50)
    wire        v60 = (vmode == 2'd1) || (vmode == 2'd2);   // 65 x 263: NTSC, PAL-60
    wire  [7:0] video_fmt;                  // C64_VIDEOFORMAT (ultimate_sys, cpu_clk)
    wire  [7:0] turbo_en, turbo_prefer;     // T29: TURBOREGS_EN, SPEED_PREFER (ultimate_sys, cpu_clk)
    wire        turbo_upd;                  // toggles with SPEED_UPDATE
    wire [31:0] turbo_stat, turbo_w1, turbo_w2;
    wire        turbo_ran_clr, turbo_on;
    wire [31:0] mc_status, mc_status2;
    wire [3:0]  ai2c_addr;
    wire        wifi_boot_u, wifi_enable_u, esp_we, esp_re, esp_boot_o, esp_dis_o, esp_oe;   // T152 S8
    wire [3:0]  esp_addr;
    wire [7:0]  esp_wdata, esp_rdata;
    wire [7:0]  ai2c_wdata, ai2c_rdata;
    wire [2:0]  ai2c_scl_lo, ai2c_sda_lo;
    wire        app_c64_reset, app_c64_pause;
    // T38: the app's keyboard/joystick state (cpu_clk), merged in app_input below
    wire [63:0] app_keys;
    wire  [4:0] app_joy1, app_joy2;
    wire        app_reset, app_restore, app_freeze;
    wire        core_reset_req;             // the C64's chips in reset (T39)
    wire        c64_halted;                 // machine pause (c64_core.v), sys_clk
    wire        speaker_en;
    wire  [3:0] speaker_vol;            // U2PIO_SPEAKER_EN volume, unused (the mixer sets it)
    wire  [7:0] mix_l, mix_r, mix_spk;
    wire  [7:0] master_vol;             // T160, cpu_clk, quasi static
    wire  [7:0] tone_sel;               // T189, cpu_clk, quasi static: 0 Flat, 1 Bass boost, 2 Treble boost
    // the app's UltiSID registers, filter curve writes, envelope levels (T25.1)
    wire [15:0] sid_base, sid_mask;
    wire  [2:0] sid_split;
    wire  [1:0] sid_waves, sid_res;
    wire  [3:0] sid_digi;
    wire [49:0] sid_rs8;                 // trx_io page 0 0x040: the reSID8 filter control (T25.5)
    wire        sid_resid8;              // the engine is the 8-SID reDIP (CAPS3 b0)
    wire        sidcv_we;
    wire [11:0] sidcv_addr;
    wire  [7:0] sidcv_wdata, sid_lv_data;
    wire  [4:0] sid_lv_addr;
    wire [31:0] sid_gains, sid_gains2;
    wire [7:0]  app_cart_type;
    wire        app_reu_en;
    wire [2:0]  app_reu_size;
    wire        app_cart_kill_tgl, app_cart_force_tgl, cart_active;
    wire        app_ultimax;
    wire        dma_req_tgl, dma_we, dma_bus, dma_rom;
    wire        dbg_stop;                   // the debugger's stop (trx_dbg.v, T12 §14.8): unused by ultimate_sys since G06 (the app window's D page is a bus cycle stopped or running)
    wire [15:0] dma_addr;
    wire [7:0]  dma_wdata;
    wire       dma_done_tgl;
    wire [7:0] dma_rdata_q;
    // SD card detect into the CPU clock; the flash clock leaves through the
    // configuration clock pin (the first three edges after startup are lost,
    // the app's flash probe begins with a dummy byte)
    // the key matrix for the menu's scan (bits change slowly, as for the C64)
    (* ASYNC_REG = "TRUE" *) reg [63:0] keys_cpu_s1, keys_cpu_s2;
    (* ASYNC_REG = "TRUE" *) reg [1:0] sd_detectn_s = 2'b11;
    always @(posedge cpu_clk) sd_detectn_s <= {sd_detectn_s[0], SD_CARDDETn};
    wire flash_sck;
    STARTUPE2 #(.PROG_USR("FALSE")) startup (
        .CFGCLK(), .CFGMCLK(), .EOS(), .PREQ(),
        .CLK(1'b0), .GSR(1'b0), .GTS(1'b0), .KEYCLEARB(1'b1), .PACK(1'b0),
        .USRCCLKO(flash_sck), .USRCCLKTS(1'b0), .USRDONEO(1'b1), .USRDONETS(1'b1));
    // the build stamp (pins.xdc USR_ACCESS TIMESTAMP) for the boot ROM's
    // identity check (trx_io BUILD, T15 §4.2); static once configured
    wire [31:0] usr_access;
    USR_ACCESSE2 usr (.CFGCLK(), .DATA(usr_access), .DATAVALID());

    wire        ovl_we;
    wire        pal_we;                 // T59: C64_PALETTE writes (ultimate_sys, cpu_clk)
    wire  [5:0] pal_addr;
    wire  [7:0] pal_wdata;
    wire [14:0] ovl_addr;
    wire [7:0]  ovl_wdata, ovl_rdata;
    wire        ovl_own;                // the menu has the keyboard (cpu_clk)
    (* ASYNC_REG = "TRUE" *) reg [1:0] ovl_own_s = 2'b00;
    always @(posedge sys_clk) ovl_own_s <= {ovl_own_s[0], ovl_own};
    // LEDs and the keyboard board's bytes (bling_io.v), case LED selector
    wire        bl_we, bl_irq, caseled_set, busy_any, busy_usb;
    wire [11:0] bl_addr;
    wire [7:0]  bl_wdata, bl_rdata, caseled_sel;
    // our IO block 0x101A0000 and the EEPROM window (trx_io.v); the cartridge's
    // flash responder and GMod2 EEPROM port into cart_slot
    wire        cw_we, cw_re, cw_ack, cw_irq, cart_ee_we;
    // its pages in the C64's clock (trx_sys.v, below): byte handshake, the
    // machine's stop and capture events for its interrupt causes
    wire        trx_rq_tgl, trx_rq_we, trx_rs_tgl, trx_j_rvalid;
    wire [15:0] trx_rq_addr;
    wire  [7:0] trx_rq_wdata, trx_rs_rdata;
    wire [31:0] trx_j_rdata;
    wire        cap_tgl, eng_tgl, gap_tgl;
    reg         stop_tgl = 1'b0;
    wire [16:0] cw_addr;
    // T79.1 (run 721: the app CPU's address -> decode -> trx_io register path crossed the partition
    // in 10 levels, WNS -0.218 ns): one register stage on the request side of the cw window, as the
    // contract's register bus will have at the edge. The window answers with ack, so the extra
    // clock of latency is harmless.
    wire        cw_we_u, cw_re_u;
    wire [16:0] cw_addr_u;
    wire [7:0]  cw_wdata_u;
    reg         cw_we_r = 1'b0, cw_re_r = 1'b0;
    reg  [16:0] cw_addr_r = 17'd0;
    reg  [7:0]  cw_wdata_r = 8'd0;
    always @(posedge cpu_clk) begin
        cw_we_r <= cw_we_u; cw_re_r <= cw_re_u; cw_addr_r <= cw_addr_u; cw_wdata_r <= cw_wdata_u;
    end
    assign cw_we = cw_we_r;
    assign cw_re = cw_re_r;
    assign cw_addr = cw_addr_r;
    assign cw_wdata = cw_wdata_r;
    wire [7:0]  cw_wdata, cw_rdata, cart_ee_wdata, cart_ee_rdata;
    wire [10:0] cart_ee_addr;
    wire        cart_fl_enable, cart_evt_tgl, cart_fl_ack_tgl, cart_inval_tgl, cart_ee_tgl;
    wire [15:0] cart_fl_magic1, cart_fl_magic2, cart_fl_mask;
    wire [23:0] cart_fl_id;
    wire [343:0] cart_desc;                     // the cartridge's descriptor (trx_io page 4)
    wire [33:0] cart_evt_word;
    wire        px_we, px_re, px_ev_vsync, px_ev_flip;  // trx_io page 6: pix_layer, c64_text
    wire [7:0]  px_rdata, pxl_rdata, tx_rdata;
    assign px_rdata = pxl_rdata | tx_rdata;
    wire        ulpi_clk, ulpi_nxt, ulpi_dir, ulpi_stp, ulpi_data_t, ulpi_reset_req, hub_reset_n;
    wire [7:0]  ulpi_data_i, ulpi_data_o;
    wire [31:0] usb_khz;
    wire [15:0] usb_dir_edges;
    reg  [1:0]  usb_ctl = 2'b00;            // JTAG register 0x10 (sys_clk)
    usb_phy usb (
        .clk50(clk50), .pll_rst(por_rst), .cpu_clk(cpu_clk), .cpu_rst(cpu_rst),
        .ULPI_CLOCK(ULPI_CLOCK), .ULPI_NXT(ULPI_NXT), .ULPI_DIR(ULPI_DIR), .ULPI_STP(ULPI_STP),
        .ULPI_DATA(ULPI_DATA), .ULPI_RESET(ULPI_RESET), .ULPI_REFCLK(ULPI_REFCLK),
        .HUB_RESETn(HUB_RESETn), .HUB_CLOCK(HUB_CLOCK),
        .ulpi_clk(ulpi_clk), .ulpi_nxt(ulpi_nxt), .ulpi_dir(ulpi_dir), .ulpi_data_i(ulpi_data_i),
        .ulpi_stp(ulpi_stp), .ulpi_data_o(ulpi_data_o), .ulpi_data_t(ulpi_data_t),
        .ulpi_reset_req(ulpi_reset_req), .hub_reset_n(hub_reset_n),
        .ctl(usb_ctl), .khz(usb_khz), .dir_edges(usb_dir_edges)
    );
    // serial bus (iec_bus below), '0' = line low
    wire c64_atn_o, c64_clk_o, c64_data_o, c64_atn_i, c64_clk_i, c64_data_i;
    wire drv_clk_o, drv_data_o, drv_atn_i, drv_clk_i, drv_data_i;
    wire [2:0] drive_leds_n;                // drive A: activity, motor, dirty (cpu_clk)
    wire [255:0] drive_dbg;                 // drive A status for the debug map (cpu_clk)
    reg          iec_port_off = 1'b0;       // JTAG register 0x24 bit 0 (sys_clk)
    reg          dc_jtag = 1'b0;            // JTAG register 0x29 bit 0 (sys_clk): the core is cut off for a swap (T79.1)
    dfx_sync dcs_app  (.clk(sys_clk), .i(app_decouple), .o(app_dc_sys));
    assign core_decouple = dc_jtag | app_dc_sys;
    // T135 D6: the expansion port stays off whatever layer 2 drives, from power-on (and every IPROG back to the host: the register starts at 1) until the host application
    // (icap region +0x21) or JTAG (0x10002A bit 0) releases it. Its own bit: a swap (core_decouple) and the lock do not depend on each other.
    reg          plock_jtag_rel = 1'b0;     // JTAG register 0x2A bit 0 (sys_clk)
    dfx_sync dpl_app  (.clk(sys_clk), .i(app_port_release), .o(app_prel_sys));
    wire         port_lock = ~(plock_jtag_rel | app_prel_sys);
    wire         port_off  = core_decouple | port_lock;
    wire         dc_cpu, dc_mclk, dc_ui;    // core_decouple in cpu_clk, mclk, ui_clk (dfx_sync below)
    // drive A status (drive_unit.vhd), sampled; a word may tear while it changes
    (* ASYNC_REG = "TRUE" *) reg [255:0] drive_dbg_s1, drive_dbg_s2;
    always @(posedge sys_clk) begin drive_dbg_s1 <= drive_dbg; drive_dbg_s2 <= drive_dbg_s1; end
    // the serial bus for 0x24: {board pins in (ATN, CLK, DATA; 1 = low), out,
    //   C64 pulls, drive pulls (CLK, DATA); '0' = pulling}
    (* ASYNC_REG = "TRUE" *) reg [10:0] iec_s1, iec_s2;
    always @(posedge sys_clk) begin
        iec_s1 <= {IEC_ATN_I, IEC_CLOCK_I, IEC_DATA_I, IEC_ATN_O, IEC_CLOCK_O, IEC_DATA_O,
                   c64_atn_o, c64_clk_o, c64_data_o, drv_clk_o, drv_data_o};
        iec_s2 <= iec_s1;
    end
    // drive A stops with the machine pause (its ticks, drive_unit.vhd)
    (* ASYNC_REG = "TRUE" *) reg [1:0] drv_halt_s = 2'b00;
    always @(posedge cpu_clk) drv_halt_s <= {drv_halt_s[0], c64_halted};
    // drive A's debug (drv_dbg.v, T21): its halt is the machine pause's unless
    // the debugger holds or steps the drive
    wire        drv_dbg_halt, drv_trace_valid, drv_stop_ev, drv_done_ev;
    wire [31:0] drv_trace, drv_dr_rdata;
    // GAME/EXROM pins and the port RESET sense for the app (CART_DETECT,
    // C64_CLOCK_DETECT b4), synchronized to its clock
    (* ASYNC_REG = "TRUE" *) reg [2:0] slot_pins_s1 = 3'b011, slot_pins_cpu = 3'b011;
    always @(posedge cpu_clk) begin
        slot_pins_s1  <= {SLOT_RST_I, SLOT_EXROMn, SLOT_GAMEn};
        slot_pins_cpu <= slot_pins_s1;
    end

    wire        drv_tgl, drv_done_tgl, drv_we;  // drive A's DDR2 port (cpu_clk)
    wire [25:0] drv_addr;
    wire [31:0] drv_wdata;
    wire [3:0]  drv_be;
    wire [127:0] drv_done_rdata;
    wire [127:0] rv_tap;                    // the RISC-V's state (ultimate_sys, cpu_clk)
    // T62: RESTORE level for U64_RESTORE_REG (G04), the C64's write to $D7FF as toggle + held byte (G17,
    // c64_core d7ff_*; the byte is stable long before ultimate_sys has seen the toggle), the scan lines bit (G09),
    // the IEC RESET line into the cpu clock (G19; ignored while the board port is switched off)
    wire        scanlines;
    wire        d7ff_ev;
    wire  [7:0] d7ff_data;
    reg         d7ff_tgl = 1'b0;
    reg   [7:0] d7ff_hold = 8'd0;
    always @(posedge sys_clk)
        if (d7ff_ev) begin d7ff_tgl <= ~d7ff_tgl; d7ff_hold <= d7ff_data; end
    (* ASYNC_REG = "TRUE" *) reg restore_cpu_s1, restore_cpu_s2;
    (* ASYNC_REG = "TRUE" *) reg [1:0] iec_rst_s1 = 2'b00, iec_rst_s2 = 2'b00;
    wire iec_rst_n_cpu = ~(iec_rst_s2[1] & ~iec_rst_s2[0]);
    // ETH_LED_YELn: on while a carrier start (RMII_CRS_DV) was seen in the last 2^27 clk50 cycles
    reg        eth_crs_d = 1'b0;
    reg [27:0] eth_age = {28{1'b1}};
    always @(posedge clk50) begin
        eth_crs_d <= RMII_CRS_DV;
        if (RMII_CRS_DV && !eth_crs_d) eth_age <= 28'd0;
        else if (!eth_age[27]) eth_age <= eth_age + 1'b1;
    end
    assign ETH_LED_YELn = eth_age[27];
    wire        drv_t16, drv_t4, drv_t1k, drv_c64_stop, drv_c64_rst_n;
    wire  [2:0] ui_btn;                 // T195: UI_BUTTON debounced, high = pressed (ui_buttons.v, cpu_clk)
    wire        drv_io_rd, drv_io_wr, drv_io_ack, drv_io_nc, gcr_io_rd, gcr_io_wr, gcr_io_ack, gcr_io_nc;
    wire [23:0] drv_io_addr, gcr_io_addr;
    wire  [7:0] drv_io_wdata, drv_io_rdata, gcr_io_wdata, gcr_io_rdata;
    wire  [7:0] snp_tag, snp_rack_tag, snp_dack_tag;
    wire        snp_req, snp_rwn, snp_rack;
    wire [25:0] snp_addr;
    wire  [3:0] snp_be;
    wire [31:0] snp_wdata, snp_rdata;
    // T79 ICAP loader (static, outside pb_l2): the RISC-V streams layer 2's partial bitstream through
    // ultimate_sys 0x10060610. icap_data holds the file's 32-bit word (first byte in 31:24); ICAPE2
    // wants every byte bit-reversed (UG470, the bitstream file is MSB first, the port is not).
    wire        icap_we;
    wire [31:0] icap_data;
    wire [31:0] icap_i;
    genvar      icap_g;
    generate for (icap_g = 0; icap_g < 4; icap_g = icap_g + 1) begin : icap_swap
        assign icap_i[8*icap_g +: 8] = {icap_data[8*icap_g], icap_data[8*icap_g+1], icap_data[8*icap_g+2],
                                        icap_data[8*icap_g+3], icap_data[8*icap_g+4], icap_data[8*icap_g+5],
                                        icap_data[8*icap_g+6], icap_data[8*icap_g+7]};
    end endgenerate
    ICAPE2 #(.ICAP_WIDTH("X32")) icap_i0 (.CLK(cpu_clk), .CSIB(~icap_we), .RDWRB(1'b0), .I(icap_i), .O());
    wire [31:0] dc_data;                // T133 debug: selected data cache counter (cpu_clk)
    reg  [15:0] dc_ctl = 16'h0007;      // T133 control, JTAG 0x100060 (sys_clk); default: cache, snoop and ext walk ON, check mode off
    wire dc_ext_tgl;                    // T133: writes into the RISC-V's cacheable window from outside its bus (ui_clk)
    ultimate_sys #(.g_capabilities(HOST_CAPS), .g_net(NET_ON)) ult (
        .clock(cpu_clk), .reset(cpu_rst), .dc_ext_tgl(dc_ext_tgl), .dc_ctl(dc_ctl), .dc_data(dc_data), .dc_line(dc_line), .dc_line_adr(dc_line_adr),
        .mem_request(m_request), .mem_tag(m_tag), .mem_read_writen(m_rwn),
        .mem_address(m_address), .mem_byte_en(m_byte_en), .mem_wdata(m_wdata),
        .mem_rdata(m_rdata), .mem_rack(m_rack), .mem_rack_tag(m_rack_tag),
        .mem_dack_tag(m_dack_tag),
        .ddr_ready(calib_cpu[1]),
        .uart_txd(UART_TXD), .uart_rxd(UART_RXD),
        .con_valid(con_valid), .con_data(con_data),
        .eth_clock(clk50), .eth_reset(eth_rst),
        .eth_tx_data(mac_tx_data), .eth_tx_eof(mac_tx_eof), .eth_tx_valid(mac_tx_valid),
        .eth_tx_ready(mac_tx_ready),
        .eth_rx_data(erx_data), .eth_rx_sof(erx_sof), .eth_rx_eof(erx_eof), .eth_rx_valid(erx_valid),
        .mdc(app_mdc), .mdio_i(mdio_in), .mdio_o(mdio_out),
        .wifi_txd(WIFI_TXD), .wifi_rxd(WIFI_RXD), .wifi_rts(WIFI_RTS), .wifi_cts(WIFI_CTS),
        .sd_ssn(SD_SSn), .sd_clk(SD_CLK), .sd_mosi(SD_MOSI), .sd_miso(SD_MISO),
        .sd_detectn(sd_detectn_s[1]),
        .keys(keys_cpu_s2), .joys(joys_cpu_s2),
        .restore_pin(restore_cpu_s2), .scanlines(scanlines), .c64_dbg_tgl(d7ff_tgl), .c64_dbg_data(d7ff_hold),
        .app_keys(app_keys), .app_reset(app_reset), .app_restore(app_restore), .app_freeze(app_freeze),
        .app_joy1(app_joy1), .app_joy2(app_joy2), .c64_reset_sense(core_reset_req), .core_decouple(dc_cpu), .ui_btn(ui_btn),
        .hdmi_hpd(HDMI_HPD), .hdmi_ddc_en(HDMI_DDC_EN),
        .flash_csn(FLASH_CSn), .flash_sck(flash_sck), .flash_mosi(FLASH_MOSI),
        .flash_miso(FLASH_MISO),
        .c64_reset_req(app_c64_reset), .c64_pause_req(app_c64_pause), .c64_ultimax_req(app_ultimax),
        .slot_pins(slot_pins_cpu), .c64_bus_int(bus_int_cpu), .c64_bus_ext(bus_ext_cpu),
        .video_fmt(video_fmt),
        .turbo_en(turbo_en), .turbo_prefer(turbo_prefer), .turbo_upd_tgl(turbo_upd),
        .speaker_en(speaker_en), .speaker_vol(speaker_vol),
        .mix_l(mix_l), .mix_r(mix_r), .mix_spk(mix_spk), .master_vol(master_vol), .tone_sel(tone_sel),
        .sid_base(sid_base), .sid_mask(sid_mask), .sid_split(sid_split), .sid_waves(sid_waves),
        .sid_res(sid_res), .sid_digi(sid_digi), .sidcv_we(sidcv_we), .sidcv_addr(sidcv_addr),
        .sidcv_wdata(sidcv_wdata), .sid_lv_addr(sid_lv_addr), .sid_lv_data(sid_lv_data),
        .sid_gains(sid_gains), .sid_gains2(sid_gains2), .smp_en(smp_en),
        .cart_type(app_cart_type), .cart_kill_tgl(app_cart_kill_tgl),
        .reu_enable(app_reu_en), .reu_size(app_reu_size),
        .cart_force_tgl(app_cart_force_tgl), .cart_active(cart_active),
        .dma_req_tgl(dma_req_tgl), .dma_addr(dma_addr), .dma_we(dma_we), .dma_wdata(dma_wdata),
        .dma_bus(dma_bus), .dma_rom(dma_rom),
        .dma_done_tgl(dma_done_tgl), .dma_rdata(dma_rdata_q), .dbg_stop(dbg_stop),
        .i2c_we(ai2c_we), .i2c_re(ai2c_re), .i2c_addr(ai2c_addr), .i2c_wdata(ai2c_wdata),
        .i2c_rdata(ai2c_rdata),
        .wifi_boot(wifi_boot_u), .wifi_enable(wifi_enable_u),
        .esp_we(esp_we), .esp_re(esp_re), .esp_addr(esp_addr), .esp_wdata(esp_wdata), .esp_rdata(esp_rdata),
        .ovl_we(ovl_we), .ovl_addr(ovl_addr), .ovl_wdata(ovl_wdata), .ovl_rdata(ovl_rdata),
        .pal_we(pal_we), .pal_addr(pal_addr), .pal_wdata(pal_wdata),
        .bl_we(bl_we), .bl_addr(bl_addr), .bl_wdata(bl_wdata), .bl_rdata(bl_rdata), .bl_irq(bl_irq),
        .cw_we(cw_we_u), .cw_re(cw_re_u), .cw_addr(cw_addr_u), .cw_wdata(cw_wdata_u), .cw_rdata(cw_rdata),
        .cw_ack(cw_ack),
        .cw_irq(cw_irq),
        .caseled_sel(caseled_sel), .caseled_set(caseled_set), .busy_any(busy_any), .busy_usb(busy_usb),
        .ulpi_clock(ulpi_clk), .ulpi_nxt(ulpi_nxt), .ulpi_dir(ulpi_dir), .ulpi_stp(ulpi_stp),
        .ulpi_data_i(ulpi_data_i), .ulpi_data_o(ulpi_data_o), .ulpi_data_t(ulpi_data_t),
        .ulpi_reset_req(ulpi_reset_req), .hub_reset_n(hub_reset_n),
        .rv_tap(rv_tap),
        .tick_16MHz_o(drv_t16), .tick_4MHz_o(drv_t4), .tick_1kHz_o(drv_t1k),
        .c64_stop_o(drv_c64_stop), .c64_reset_n_o(drv_c64_rst_n),
        .drv_io_read(drv_io_rd), .drv_io_write(drv_io_wr), .drv_io_addr(drv_io_addr), .drv_io_wdata(drv_io_wdata),
        .drv_io_rdata(drv_io_rdata), .drv_io_ack(drv_io_ack), .drv_io_nc(drv_io_nc),
        .gcr_io_read(gcr_io_rd), .gcr_io_write(gcr_io_wr), .gcr_io_addr(gcr_io_addr), .gcr_io_wdata(gcr_io_wdata),
        .gcr_io_rdata(gcr_io_rdata), .gcr_io_ack(gcr_io_ack), .gcr_io_nc(gcr_io_nc),
        .icap_we(icap_we), .icap_data(icap_data), .app_decouple(app_decouple), .app_port_release(app_port_release),
        .snoop_tag(snp_tag), .snoop_request(snp_req), .snoop_rwn(snp_rwn), .snoop_addr(snp_addr),
        .snoop_be(snp_be), .snoop_wdata(snp_wdata), .snoop_rdata(snp_rdata), .snoop_rack(snp_rack),
        .snoop_rack_tag(snp_rack_tag), .snoop_dack_tag(snp_dack_tag)
    );

    // T79: the 1541 drive and the GCR codec (drive_l2.vhd), layer 2: ultimate_sys
    // hands over their I/O regions, its ticks and the system bus for the snoop

    // ---- serial bus: the C64, the app's drive A, the board's IEC port -------------
    assign IEC_RESET_O = 1'b0;
    assign IEC_SRQ_O   = 1'b0;
    // MDIO: open drain, released = pulled high. Debug register 7 can take
    // MDC/MDIO over from the app (tools/trxdbg.py mdio).
    IOBUF b_mdio (.IO(MDIO_DATA), .I(1'b0), .T(mdio_ovr[31] ? mdio_ovr[1] : mdio_out), .O(mdio_in));
    assign MDIO_CLK = mdio_ovr[31] ? mdio_ovr[0] : app_mdc;
    rst_sync rs_eth (.clk(clk50), .arst(cpu_rst), .rst(eth_rst));

    // ---- Ethernet: RMII transceiver, shared by the MAC and the core's packet port ----
    wire [7:0] x_data;
    wire       x_eof, x_valid, x_ready;
`ifdef NO_NET
    // T164 console flat builds: no transceiver, no arbiter, no packet-port FIFO. The pins stay (RMII_REFCLK is the clock root of rmii_clock and of
    // the host's PLL; the constraints and clock groups of pins.xdc keep their objects) and are driven idle; the core's packet port never gets a grant
    // and drops what it writes.
    wire own_mac = 1'b0, own_st = 1'b0;
    assign x_ready      = 1'b0;
    assign RMII_TX_EN   = 1'b0;
    assign RMII_TX_DATA = 2'b00;
    assign erx_data = 8'd0;
    assign erx_sof = 1'b0;
    assign erx_eof = 1'b0;
    assign erx_valid = 1'b0;
    assign mac_tx_ready = 1'b0;
    assign st_req = 1'b0;
    assign st_tx_data = 8'd0;
    assign st_tx_valid = 1'b0;
    assign st_tx_eof = 1'b0;
    assign pp_grant = 1'b0;
    assign pp_ready = 1'b1;
`else
    rmii_transceiver rmii (
        .clock(clk50), .reset(eth_rst),
        .rmii_crs_dv(RMII_CRS_DV), .rmii_rxd(RMII_RX_DATA),
        .rmii_tx_en(RMII_TX_EN), .rmii_txd(RMII_TX_DATA),
        .eth_rx_data(erx_data), .eth_rx_sof(erx_sof), .eth_rx_eof(erx_eof), .eth_rx_valid(erx_valid),
        .eth_tx_data(x_data), .eth_tx_eof(x_eof), .eth_tx_valid(x_valid), .eth_tx_ready(x_ready),
        .ten_meg_mode(1'b0)
    );
    // whole frames in turn: whoever has one waiting gets the transmitter when it
    // is free, the MAC first, and keeps it until its last byte is taken
    reg own_mac = 1'b0, own_st = 1'b0;
    always @(posedge clk50)
        if (eth_rst) begin
            own_mac <= 1'b0; own_st <= 1'b0;
        end else if (!own_mac && !own_st) begin
            if (mac_tx_valid) own_mac <= 1'b1;
            else if (st_req)  own_st  <= 1'b1;
        end else if (own_mac && mac_tx_valid && mac_tx_eof && x_ready)
            own_mac <= 1'b0;
        else if (own_st && st_tx_valid && st_tx_eof && x_ready)
            own_st <= 1'b0;
    assign x_data  = own_st ? st_tx_data : mac_tx_data;
    assign x_eof   = own_st ? st_tx_eof  : mac_tx_eof;
    assign x_valid = own_st ? st_tx_valid : own_mac & mac_tx_valid;
    assign mac_tx_ready = own_mac & x_ready;
`endif

    // counters for the debug port (clk50; read across clocks, for a look only)
    reg [15:0] cnt_rx = 0, cnt_rx_ok = 0, cnt_tx = 0, cnt_crs = 0;
    reg        crs_d;
    always @(posedge clk50) begin
        crs_d <= RMII_CRS_DV;
        if (RMII_CRS_DV && !crs_d) cnt_crs <= cnt_crs + 1'b1;
        if (erx_valid && erx_eof) begin
            cnt_rx <= cnt_rx + 1'b1;
            if (erx_data[0]) cnt_rx_ok <= cnt_rx_ok + 1'b1;
        end
        if (own_mac && mac_tx_valid && mac_tx_eof && x_ready) cnt_tx <= cnt_tx + 1'b1;
    end


    wire        cpu_tgl, cpu_done_tgl, cpu_we;
    wire [25:0] cpu_addr;
    wire [31:0] cpu_wdata, cpu_done_rdata;
    wire [127:0] cpu_done_rline, dc_line;      // T133: client 0's whole 128-bit word (ui_clk), and the bridge's copy for the caches (cpu_clk)
    wire [21:0]  dc_line_adr;
    wire [3:0]  cpu_be;
    cpu_mem_bridge bridge (
        .clk(cpu_clk), .rst(cpu_rst),
        .request(m_request), .tag(m_tag), .read_writen(m_rwn), .address(m_address),
        .byte_en(m_byte_en), .wdata(m_wdata), .rdata(m_rdata), .rack(m_rack),
        .rack_tag(m_rack_tag), .dack_tag(m_dack_tag),
        .req_tgl(cpu_tgl), .a_addr(cpu_addr), .a_we(cpu_we), .a_wdata(cpu_wdata), .a_be(cpu_be),
        .done_tgl(cpu_done_tgl), .done_rdata(cpu_done_rdata),
        .done_rline(cpu_done_rline), .line(dc_line), .line_adr(dc_line_adr)
    );

    // client 0: RISC-V, client 1: REU / JTAG, client 2: drive A; cart_fill
    // before them
    mig_arb arb (
        .ext_wr_tgl(dc_ext_tgl),
        .ui_clk(ui_clk), .ui_rst(ui_rst), .calib_done(ddr_calib),
        .req_tgl({drv_tgl, port_tgl, cpu_tgl}), .addr({drv_addr, ddr_addr, cpu_addr}),
        .we({drv_we, ddr_we, cpu_we}), .wdata({drv_wdata, ddr_wdata, cpu_wdata}),
        .be({drv_be, ddr_be, cpu_be}),
        .done_tgl({drv_done_tgl, port_done_tgl, cpu_done_tgl}), .rdata({port_done_rdata, cpu_done_rdata}), .rline0(cpu_done_rline),
        .rdata_line(drv_done_rdata),
        .c_addr(c_addr), .c_cmd(c_cmd), .c_en(c_en), .c_rdy(c_rdy), .c_wdf_data(c_wdf_data),
        .c_wdf_mask(c_wdf_mask), .c_wdf_wren(c_wdf_wren), .c_wdf_rdy(c_wdf_rdy),
        .c_rd_data(c_rd_data), .c_rd_data_valid(c_rd_valid),
        .v_addr(v_addr), .v_cmd(v_cmd), .v_en(v_en), .v_rdy(v_rdy), .v_wdf_data(v_wdf_data),
        .v_wdf_mask(v_wdf_mask), .v_wdf_wren(v_wdf_wren), .v_wdf_rdy(), .v_rd_data_valid(v_rd_valid),
        .v_urgent(v_urgent),
        .e_addr(e_addr), .e_cmd(e_cmd), .e_en(e_en), .e_rdy(e_rdy), .e_wdf_data(e_wdf_data),
        .e_wdf_mask(e_wdf_mask), .e_wdf_wren(e_wdf_wren), .e_rd_data_valid(e_rd_valid),
        .app_addr(app_addr), .app_cmd(app_cmd), .app_en(app_en), .app_rdy(app_rdy),
        .app_wdf_data(app_wdf_data), .app_wdf_mask(app_wdf_mask), .app_wdf_wren(app_wdf_wren),
        .app_wdf_rdy(app_wdf_rdy), .app_rd_data(app_rd_data), .app_rd_data_valid(app_rd_data_valid)
    );

    // the lens unit's look at the cartridge's DDR2 bytes (dma_win, T12 §11 Q2, T21 §3.4.1)
    wire        pkd_tgl, pkd_ack_tgl;
    wire [23:0] pkd_addr;
    wire  [7:0] pkd_data;
    wire [1:0] cart_rom_sel;                    // trx_io ROM_SEL (T60)

    // our IO block (ARCH §10): ID and interrupt, the cartridge page (flash
    // responder configuration, pending operation, ACK, cache invalidation,
    // write counter), the GMod2 EEPROM window. The responder configuration
    // goes into the C64 clock as it is: the app writes it before the cartridge
    // starts and leaves it alone while it runs.

    // ---- program both synthesizers -------------------------------------------
    wire scl_low, sda_low, i2c_done;
    wire mc_cdce_v60;
    // (the sequencer is cdce_mode in hdmi_out.v: the PLL registers of the HDMI mode, T164 WP8)
    // The synthesizer master drives the 1.8 V and the HDMI bus until i2c_done (the
    // two PLLs change between those two with the board revision: u64ii_init.cc,
    // hw_version 0x15). After i2c_done the keyboard scan has the 1.8 V bus only:
    // the keyboard expander 0x21 is there on every revision (u64ii_init.cc writes
    // it on I2C_CHANNEL_1V8 with no condition). The HDMI bus (the monitor's DDC)
    // and the 3.3 V bus are released, as before the scan ran (T253 D1).
    wire kb_scl_low, kb_sda_low, sda18_in, sdahd_in, sda33_in;
    wire bus_scl_low = i2c_done ? kb_scl_low : scl_low;
    wire bus_sda_low = i2c_done ? kb_sda_low : sda_low;
    // JTAG register 10 can take the buses over for exploring them by hand
    // (tools/trxdbg.py i2c): bit 31 on, bits 2:0 pull SCL low, bits 5:3 pull
    // SDA low, per bus {3.3 V, HDMI, 1.8 V}. Reading it gives the line levels.
    reg  [31:0] i2c_ovr = 32'd0;            // sys_clk; static levels, no sync needed
    wire        ovr = i2c_ovr[31];
    // The app's master has them while it owns them (app_i2c.v: claimed with
    // scan_enable 0 once the keyboard scan stands between two transactions).
    wire [2:0]  scl_lo = ovr ? i2c_ovr[2:0] : (ai2c_own & i2c_done) ? ai2c_scl_lo :
                         {1'b0, ~i2c_done & scl_low, bus_scl_low};
    wire [2:0]  sda_lo = ovr ? i2c_ovr[5:3] : (ai2c_own & i2c_done) ? ai2c_sda_lo :
                         {1'b0, ~i2c_done & sda_low, bus_sda_low};
    wire scl18_in, sclhd_in, scl33_in;
    IOBUF b_scl18 (.IO(I2C18_SCL), .I(1'b0), .T(~scl_lo[0]), .O(scl18_in));
    IOBUF b_sda18 (.IO(I2C18_SDA), .I(1'b0), .T(~sda_lo[0]), .O(sda18_in));
    IOBUF b_sclhd (.IO(HDMI_SCL),  .I(1'b0), .T(~scl_lo[1]), .O(sclhd_in));
    IOBUF b_sdahd (.IO(HDMI_SDA),  .I(1'b0), .T(~sda_lo[1]), .O(sdahd_in));
    IOBUF b_scl33 (.IO(I2C33_SCL), .I(1'b0), .T(~scl_lo[2]), .O(scl33_in));
    IOBUF b_sda33 (.IO(I2C33_SDA), .I(1'b0), .T(~sda_lo[2]), .O(sda33_in));
    wire kb_sda_in = sda18_in;                  // the expander answers on the 1.8 V bus only (T253 D1)
    app_i2c ai2c (
        .clk(cpu_clk), .rst(cpu_rst), .we(ai2c_we), .re(ai2c_re), .addr(ai2c_addr),
        .wdata(ai2c_wdata), .rdata(ai2c_rdata), .scan_en(ai2c_scan),
        .i2c_done(i2c_done), .kb_pause(kb_pause), .kb_parked(kb_parked), .own(ai2c_own),
        .scl_lo(ai2c_scl_lo), .sda_lo(ai2c_sda_lo),
        .scl_in({scl33_in, sclhd_in, scl18_in}), .sda_in({sda33_in, sdahd_in, sda18_in}),
        .hold(mc_i2c_hold), .parked(ai2c_parked)
    );

    // System Mode (T40): the app's C64_VIDEOFORMAT picks PAL, NTSC, PAL-60 or
    // NTSC-50; mode_ctl switches the C64 clock (mmcm_sys DRP), the
    // synthesizers and with them the HDMI frame, live
    mode_ctl mctl (
        .clk(clk50), .rst(por_rst), .fmt(video_fmt),
        .halt_req(mc_halt), .halted(c64_halted), .in_reset(core_reset_req),
        .i2c_hold(mc_i2c_hold), .i2c_parked(ai2c_parked), .kb_pause(mc_kb_pause), .kb_parked(kb_parked),
        .i2c_done(i2c_done), .cdce_v60(mc_cdce_v60), .sys_en(mc_sys_en), .reconf(mc_reconf),
        .mmcm_rst(mmcm_drst), .daddr(mmcm_daddr), .di(mmcm_di), .dout(mmcm_do), .den(mmcm_den),
        .dwe(mmcm_dwe), .drdy(mmcm_drdy), .sys_locked(sys_locked), .ref_locked(ref_locked),
        .vmode(vmode), .status(mc_status), .status2(mc_status2)
    );

    // give the synthesizer PLL 50 ms to settle before the transmitters start
    reg [21:0] settle = 0;
    wire gt_rst = ~settle[21];
    always @(posedge clk50)
        if (!i2c_done) settle <= 0;
        else if (!settle[21]) settle <= settle + 1;

    // ---- keyboard, joysticks, RESTORE ------------------------------------------
    wire [63:0] kb_keys;                // I2C expander behind P7 (an original C64 keyboard)
    wire [63:0] bl_keys;                // the C64U keyboard on J8 (bling_keyboard, below)
    wire  [7:0] bl_last, bl_other;
    wire        bl_restore;
    wire [15:0] kb_status;
    wire  [9:0] x_text;                 // from vic_scaler (below)
    c64u_keyboard kb (.clk(clk50), .rst(~i2c_done), .sda_in(kb_sda_in),
                      .scl_low(kb_scl_low), .sda_low(kb_sda_low), .keys(kb_keys),
                      .status(kb_status), .pause(kb_pause | mc_kb_pause), .parked(kb_parked));
    // Into the C64 clock. The key bits change slowly (one column every 0.7 ms),
    // so bits of one scan arriving a clock apart do no harm.
    (* ASYNC_REG = "TRUE" *) reg [63:0] keys_s1, keys_s2;
    // line levels for register 10: bit 6 KBJOY_INT, 5:3 SDA, 2:0 SCL {3.3 V, HDMI, 1.8 V};
    // bit 13 the app owns the buses, bit 12 its scan_enable
    (* ASYNC_REG = "TRUE" *) reg [11:0] i2c_lv_s1, i2c_lv_s;
    always @(posedge sys_clk) begin
        i2c_lv_s1 <= {5'd0, KBJOY_INT, sda33_in, sdahd_in, sda18_in, scl33_in, sclhd_in, scl18_in};
        i2c_lv_s  <= i2c_lv_s1;
    end
    // T175: the joystick pins are open-drain pads; the probe engine of layer 2 (kbjoy_low) is the only one that can pull one low
    wire [4:0] kbj1_i, kbj2_i;
    wire [9:0] l2_kbjoy_low;
    wire [7:0] av_ga, av_gb, av_gc;          // T201: layer 2's A/V group words
    wire       av_syn_n, av_fb;              // T259: layer 2's AVID_SYNCn and AVID_FB
    diag_pads kbjoy_pads (.KBJOY_1_JOY(KBJOY_1_JOY), .KBJOY_2_JOY(KBJOY_2_JOY), .low(l2_kbjoy_low), .decouple(core_decouple), .j1_i(kbj1_i), .j2_i(kbj2_i));
    (* ASYNC_REG = "TRUE" *) reg [10:0] pins_s1, pins_s2;
    always @(posedge sys_clk) begin
        keys_s1 <= kb_keys | bl_keys;  keys_s2 <= keys_s1;
        pins_s1 <= ~{KBJOY_RESTORE, kbj2_i, kbj1_i};
        pins_s2 <= pins_s1;
    end
    // the app's own scans (KEYB_ROW, KEYB_JOY, ultimate_sys.vhd) in the CPU's clock
    (* ASYNC_REG = "TRUE" *) reg [9:0] joys_cpu_s1, joys_cpu_s2;
    always @(posedge cpu_clk) begin
        restore_cpu_s1 <= pins_s2[10] | bl_restore;
        restore_cpu_s2 <= restore_cpu_s1;
        iec_rst_s1 <= {IEC_RESET_I, iec_port_off};
        iec_rst_s2 <= iec_rst_s1;
    end
    always @(posedge cpu_clk) begin
        keys_cpu_s1 <= keys_s2; keys_cpu_s2 <= keys_cpu_s1;
        joys_cpu_s1 <= pins_s2[9:0]; joys_cpu_s2 <= joys_cpu_s1;
    end

    // ---- the C64 ---------------------------------------------------------------
    wire        audio_sample_en;            // mclk, once per I2S frame (48 kHz)
    wire signed [23:0] audio_l, audio_r;    // mclk
    // Silent while the app has the C64 stopped (its freeze menu) or the machine
    // is paused. The app's mixer gains for the UltiSIDs are applied in the SID's
    // mixer (T25.3), which also decays to 0 on
    // its own when the C64 clock stops; this cut stays for the app's C64_STOP.
    reg         c64_mute = 1'b0;            // sys_clk, set below
    (* ASYNC_REG = "TRUE" *) reg [1:0] pause_mclk = 2'b00;
    always @(posedge mclk) pause_mclk <= {pause_mclk[0], c64_mute};
    // T160: the master volume of every core, sat24(audio * master_vol / 128), before HDMI and I2S (trx_gain.v)
    wire signed [23:0] gain_l, gain_r;
    // T189: the tone preset (Flat / Bass boost / Treble boost) in front of it, 25 bit out: the gain's sat stays the only clip point (trx_tone.v)
    wire signed [24:0] tone_l, tone_r;
    trx_tone mtone (.clk(mclk), .frame_en(audio_sample_en), .sel(tone_sel), .in_l(audio_l), .in_r(audio_r), .out_l(tone_l), .out_r(tone_r));
    trx_gain mgain (.clk(mclk), .vol(master_vol), .in_l(tone_l), .in_r(tone_r), .out_l(gain_l), .out_r(gain_r));
    reg signed [23:0] snd_l, snd_r;
    always @(posedge mclk) begin
        snd_l <= pause_mclk[1] ? 24'sd0 : gain_l;
        snd_r <= pause_mclk[1] ? 24'sd0 : gain_r;
    end
    wire        mclk_rst;
    wire [3:0] pix_color;
    wire [9:0] pix_x;
    wire [8:0] pix_y;
    wire [4:0] c64_leds;
    // ---- debug over JTAG (tools/trxdbg.py) -------------------------------------
    // 0x000000-0x00FFFF C64 RAM (one byte per access); 0x100000 + n: registers
    //   0 'TRX1', 1 x_text, 2 keyboard status {J8: other codes, last byte;
    //     P7 expander: nacks, rows}, 3/4 keys low/high,
    //   5 {DDR2 calibrated, synthesizers done, RESTORE, joystick 2, joystick 1},
    //   6 RISC-V console: bytes written so far,
    //   7 MDIO by hand: write bit 31 = take MDC/MDIO from the app, bit 0 MDC,
    //     bit 1 MDIO (1 = released); read bit 2 = MDIO pin,
    //   8 DDR2 window (16 MB units, bits 2:0);
    //   9 machine control: bit 0 C64 CPU reset (held while set), bit 1 C64 CPU
    //     pause, bit 2 RISC-V reset (held while set), bit 3 machine pause (the
    //     whole C64 and drive A, c64_core.v), bit 4 the app's C64_STOP pauses
    //     only the CPU (as before T12 B), whatever GATE_CTRL says (the app's
    //     stop is a machine pause only with GATE_CTRL b0, trx_sys.v);
    //     read bit 8 = the machine is paused;
    //   10 I2C by hand (tools/i2cx.py), 11 BLING_KEYB changes recorded;
    //   12 Ethernet frames received {CRC ok, all}, 13 sent {video frames,
    //   MAC frames}, 14 {app DMA bytes, PHY carrier edges}, 15 {0};
    //   0x10 USB: read ULPI_CLOCK in kHz, write bit 0 inverts ULPI_RESET, bit 1
    //   lets the app's ULPI reset reach the PHY; 0x11 {DIR edges, ULPI_RESET,
    //   HUB_RESETn, those two bits} (usb_phy.v);
    //   0x20-0x23 drive A (drive_unit.vhd): 0x20 last GCR fetch address,
    //   0x21 {GCR fetches, their answers}, 0x22 the drive CPU's last bus cycle
    //   {0, ATN, DATA, CLK, SYNC, SO, IRQ, read, data, address}, 0x23 {SYNC
    //   edges (12 bits), byte-ready edges (12 bits), last GCR byte};
    //   0x24 serial bus {pins in ATN/CLK/DATA (1 = low), pins out (1 = pull),
    //   C64 out, drive CLK/DATA out ('0' = pull), port off}; write bit 0 = 1
    //   disconnects the board's IEC port (iec_bus.v);
    //   0x25 drive A {half track (7), step phase (2), motor, mode, write_prot_n,
    //   inserted, power, reset, byte ready | last write to VIA 2 port B $1C00 |
    //   highest half track since power-on (7), 0}; 0x26/0x27 the parameter
    //   RAM's second word for half tracks 0 and 34 as the app wrote it
    //   {length - 1 | bit time << 16}; 0x28 {GCR fetches in the last 100 ms, drive
    //   CPU cycles (16 bits, wrapping)};
    //   0x30 System Mode (T40, mode_ctl.v status), 0x31 its status2 (DRP
    //   check value, last switch in 20.48 us units), 0x32 the VIC as measured
    //   {lines per frame (9), dot4x clocks per line (12)}, 0x33 dot4x clocks
    //   per frame (sys_clk counts between two line-0 starts); 0x34 HDMI
    //   {gt_ready, pix_locked, scaler locked, 0, output frames in the last
    //   second (12), pixel clocks per frame / 256 (16)};
    //   (registers answer only at 0x100000-0x1000FF, groups 0-3 of bits 7:4;
    //   the rest of 0x100000-0x100FFF reads 0)
    // 0x101000-0x1013FF: our IO block's page JTAG_PAGE (trx_sys.v, 32-bit
    //   words at byte offset 4 x bits 9:0; only its pages in the C64 clock:
    //   0 from 0x100 (and ID at 0), 1, 5); 0x101400 JTAG_PAGE; the rest of
    //   0x101000-0x1FFFFF reads 0
    // 0x200000-0x200FFF: RISC-V console ring (4 bytes per read, see con_ring.v)
    // 0x300000-0x3003FF: BLING_KEYB edge recorder (above)
    // 0x400000-0x7FFFFF: DDR2, 32-bit words, window + word address bits 21:0
    // 0x800000-0xFFFFFF: DDR2, 16-bit words, window + address bits 22:0
    wire [23:0] dbg_addr;
    wire [31:0] dbg_wdata;
    wire        dbg_re, dbg_we;
    wire  [7:0] ram_rdata;
    reg  [31:0] dbg_reg;
    (* ASYNC_REG = "TRUE" *) reg [31:0] dc_data_s1, dc_data_s2;   // T133: cpu_clk -> sys_clk
    wire [255:0] rv_hold;                   // rv_dbg.v, below
    (* ASYNC_REG = "TRUE" *) reg [255:0] rv_hold_s1, rv_hold_s2;   // still between snapshots
    reg         dbg_is_reg, dbg_is_ddr, dbg_is_con, dbg_is_edge, dbg_is_trx, dbg_is_ram, dbg_rvalid_fast;
    wire        dbg_trx_sel = dbg_addr[23:12] == 12'h101;

    // BLING_KEYB edge recorder, for working out its protocol: every change of
    // the line is stored as {new level, sys_clk count bits 30:0}, 1024 of them,
    // read at 0x300000 + n; register 11 counts the changes so far.
    (* ASYNC_REG = "TRUE" *) reg [2:0] bk_s = 3'b111;
    reg  [30:0] bk_time = 31'd0;
    reg  [31:0] bk_count = 32'd0;
    reg  [31:0] bk_mem [0:1023];
    reg  [31:0] bk_rdata;
    always @(posedge sys_clk) begin
        bk_s    <= {bk_s[1:0], BLING_KEYB};
        bk_time <= bk_time + 31'd1;
        if (bk_s[2] != bk_s[1]) begin
            if (bk_count < 32'd1024) bk_mem[bk_count[9:0]] <= {bk_s[1], bk_time};
            bk_count <= bk_count + 32'd1;
        end
        bk_rdata <= bk_mem[dbg_addr[9:0]];
    end
    wire        bl_rx_tgl, bl_locked, bl_lock_off;
    (* ASYNC_REG = "TRUE" *) reg [1:0] bl_lock_off_s = 2'b00;
    always @(posedge sys_clk) bl_lock_off_s <= {bl_lock_off_s[0], bl_lock_off};
    bling_keyboard bkb (.clk(sys_clk), .v60(v60), .rx(bk_s[1]), .keys(bl_keys), .restore(bl_restore),
                        .last(bl_last), .other(bl_other),
                        .rx_tgl(bl_rx_tgl), .locked(bl_locked), .lock_off(bl_lock_off_s[1]));
    (* ASYNC_REG = "TRUE" *) reg [2:0] cled_s1, cled_s2;   // declared here: bling_io's lamp reads cled_s2[2]
    bling_io bio (.clk(cpu_clk), .rst(cpu_rst), .we(bl_we), .addr(bl_addr), .wdata(bl_wdata),
                  .rdata(bl_rdata), .irq(bl_irq),
                  .rx_tgl(bl_rx_tgl), .rx_byte(bl_last), .locked(bl_locked), .lock_off(bl_lock_off),
                  .drive_act(cled_s2[2]), .hd_act(!drive_leds_n[1]), .lamp_en(caseled_set && caseled_sel[3:0] == 4'd4),
                  .bling_led(BLING_LED));
    // T152 S8: the ESP32's GPIO0 (BOOT) and EN (DIS); z until the app arms esp_ctl at 0x10060910
    esp_ctl esp (.clk(cpu_clk), .rst(cpu_rst), .we(esp_we), .re(esp_re), .addr(esp_addr), .wdata(esp_wdata), .rdata(esp_rdata),
                 .boot_i(wifi_boot_u), .enable_i(wifi_enable_u), .boot_o(esp_boot_o), .dis_o(esp_dis_o), .oe(esp_oe));
    // T195: the board's buttons; the debounce counts ultimate_sys's 1 ms tick
    ui_buttons ubtn (.clk(cpu_clk), .tick_1ms(drv_t1k), .pin_n(UI_BUTTON), .btn(ui_btn));
    assign WIFI_BOOT = esp_oe ? esp_boot_o : 1'bz;
    assign WIFI_DIS  = esp_oe ? esp_dis_o  : 1'bz;
    assign LED_DATA  = 1'b0;            // case LED strip: out (OPT-bling), idle level
    assign LED_CLOCK = 1'b0;
    wire [31:0] hdmi_meas_s;              // T40: HDMI as measured (below, LEDs section)
    // T40: mode_ctl's status (clk50, static between switches) and the VIC's
    // frame as it runs: raster lines, dot4x clocks per line and per frame
    reg  [31:0] mc_status_s, mc_status2_s;
    reg  [8:0]  vm_lines = 9'd0, vm_py_d = 9'd0;
    reg  [11:0] vm_line_clk = 12'd0, vm_lc = 12'd0;
    reg  [19:0] vm_frame_clk = 20'd0, vm_fc = 20'd0;
    reg  [9:0]  vm_px_d = 10'd0;
    always @(posedge sys_clk) begin
        mc_status_s  <= mc_status;
        mc_status2_s <= mc_status2;
        vm_py_d <= pix_y;
        vm_px_d <= pix_x;
        vm_lc <= vm_lc + 12'd1;
        vm_fc <= vm_fc + 20'd1;
        if (pix_x == 10'd0 && vm_px_d != 10'd0) begin vm_line_clk <= vm_lc; vm_lc <= 12'd1; end
        if (pix_y == 9'd0 && vm_py_d != 9'd0) begin
            vm_lines <= vm_py_d + 9'd1; vm_frame_clk <= vm_fc; vm_fc <= 20'd1;
        end
    end
    reg  [2:0]  ddr_window = 3'd0;
    wire        dbg_ddr_sel = dbg_addr[23] | dbg_addr[22];
    (* ASYNC_REG = "TRUE" *) reg [1:0] calib_s;
    always @(posedge sys_clk) begin
        calib_s <= {calib_s[0], ddr_calib};
        dbg_rvalid_fast <= dbg_re && !dbg_ddr_sel && !dbg_trx_sel;
        if (dbg_re || dbg_we) begin
            dbg_is_reg <= dbg_addr[23:12] == 12'h100;
            dbg_is_trx <= dbg_trx_sel;
            dbg_is_ram <= dbg_addr[23:16] == 8'h00;
            dbg_is_con <= dbg_addr[23:20] == 4'h2;
            dbg_is_edge <= dbg_addr[23:20] == 4'h3;
            dbg_is_ddr <= dbg_ddr_sel;
        end
        if (dbg_we && dbg_addr == 24'h100008) ddr_window <= dbg_wdata[2:0];
        if (dbg_we && dbg_addr == 24'h100009) machine_ctl <= dbg_wdata[4:0];
        if (dbg_we && dbg_addr == 24'h10000A) i2c_ovr <= dbg_wdata;
        if (dbg_we && dbg_addr == 24'h100007) mdio_ovr <= dbg_wdata;
        if (dbg_we && dbg_addr == 24'h100010) usb_ctl <= dbg_wdata[1:0];
        // DDR2 accesses from the debug port, queued for the DDR2 arbiter
        if ((dbg_re || dbg_we) && dbg_ddr_sel) begin
            jtag_ddr_pending <= 1'b1;
            jtag_ddr_wide    <= !dbg_addr[23];
            jtag_ddr_addr    <= dbg_addr[23] ? {ddr_window, dbg_addr[22:0]}
                                             : {ddr_window, dbg_addr[21:0], 1'b0};
            jtag_ddr_we      <= dbg_we;
            jtag_ddr_wdata   <= dbg_wdata;
        end else if (ddr_req && !ddr_for_reu)
            jtag_ddr_pending <= 1'b0;
        case (dbg_addr[3:0])
            4'd0: dbg_reg <= 32'h54525831;
            4'd1: dbg_reg <= {22'd0, x_text};
            4'd2: dbg_reg <= {bl_other, bl_last, kb_status};
            4'd3: dbg_reg <= keys_s2[31:0];
            4'd4: dbg_reg <= keys_s2[63:32];
            4'd5: dbg_reg <= {19'd0, calib_s[1], i2c_done, pins_s2};
            4'd6: dbg_reg <= con_count;
            4'd8: dbg_reg <= {29'd0, ddr_window};
            4'd7: dbg_reg <= {mdio_ovr[31], 28'd0, mdio_in, mdio_ovr[1:0]};  // MDIO by hand
            4'd9: dbg_reg <= {23'd0, c64_halted, 3'd0, machine_ctl};
            4'd10: dbg_reg <= {i2c_ovr[31], 17'd0, ai2c_own, ai2c_scan, i2c_lv_s[11:0]};
            4'd11: dbg_reg <= bk_count;
            4'd12: dbg_reg <= {cnt_rx_ok, cnt_rx};         // Ethernet frames received: CRC ok, all
            4'd13: dbg_reg <= {16'd0, cnt_tx};             // sent: MAC frames and the core's packet frames (the stream frame count is the core's)
            4'd14: dbg_reg <= {16'd0, cnt_crs};            // PHY carrier edges
            4'd15: dbg_reg <= 32'd0;
            default: dbg_reg <= 32'd0;
        endcase
        if (dbg_addr[7:4] == 4'h1)
            dbg_reg <= dbg_addr[0] ? {usb_dir_edges, 12'd0, ULPI_RESET, HUB_RESETn, usb_ctl} : usb_khz;
        if (dbg_addr[7:4] == 4'h2)
            dbg_reg <= dbg_addr[3:0] == 4'd4 ? {20'd0, iec_s2, iec_port_off} :
                       dbg_addr[3:0] == 4'd9 ? {30'd0, core_decouple, dc_jtag} :
                       dbg_addr[3:0] == 4'd10 ? {29'd0, port_lock, app_prel_sys, plock_jtag_rel} :
                       dbg_addr[3:0] < 4'd4 ? drive_dbg_s2[dbg_addr[1:0] * 32 +: 32] :
                       dbg_addr[3:0] < 4'd9 ? drive_dbg_s2[(dbg_addr[3:0] - 4'd1) * 32 +: 32] : 32'd0;
        if (dbg_addr[7:4] == 4'h3)
            case (dbg_addr[2:0])
                3'd0: dbg_reg <= mc_status_s;
                3'd1: dbg_reg <= mc_status2_s;
                3'd2: dbg_reg <= {11'd0, vm_lines, vm_line_clk};
                3'd3: dbg_reg <= {12'd0, vm_frame_clk};
                3'd4: dbg_reg <= hdmi_meas_s;
                default: dbg_reg <= 32'd0;
            endcase
        if (dbg_addr[7:4] == 4'h4) dbg_reg <= rv_hold_s2[dbg_addr[2:0] * 32 +: 32];
        // T133 data cache debug: 0x100060 control (r/w), 0x100061 the counter selected by ctl[15:12]
        if (dbg_addr[7:4] == 4'h6) dbg_reg <= dbg_addr[0] ? dc_data_s2 : {16'd0, dc_ctl};
        // DDR2 calibration (ddr2_ctrl.v): the id, then the result words; they are final
        // once CAL0 bit 29 (done) is set, no synchronizer in front (static from then on)
        if (dbg_addr[7:4] == 4'h5)
            case (dbg_addr[3:0])
                4'd0: dbg_reg <= 32'hDD2C0001;
                4'd1: dbg_reg <= ddr_cal[31:0];
                4'd2: dbg_reg <= ddr_cal[63:32];
                default: dbg_reg <= 32'd0;
            endcase
        if (dbg_addr[11:8] != 4'h0 || dbg_addr[7:4] > 4'h6) dbg_reg <= 32'd0;   // not ours
        if (dbg_we && dbg_addr == 24'h100024) iec_port_off <= dbg_wdata[0];
        if (dbg_we && dbg_addr == 24'h100029) dc_jtag <= dbg_wdata[0];
        if (dbg_we && dbg_addr == 24'h10002A) plock_jtag_rel <= dbg_wdata[0];
        if (dbg_we && dbg_addr == 24'h100060) dc_ctl <= {dbg_wdata[15:5], dc_ctl[4] ^ dbg_wdata[4], dbg_wdata[3:0]};
        dc_data_s1 <= dc_data; dc_data_s2 <= dc_data_s1;
    end
    // RISC-V tap (rv_dbg.v): a write to 0x100040 takes a snapshot, to 0x100041
    // clears the longest-stall record; the words are read at 0x100040-0x100047
    reg         rv_snap_tgl = 1'b0, rv_clr_tgl = 1'b0;
    always @(posedge sys_clk) begin
        rv_hold_s1 <= rv_hold; rv_hold_s2 <= rv_hold_s1;
        if (dbg_we && dbg_addr == 24'h100040) rv_snap_tgl <= ~rv_snap_tgl;
        if (dbg_we && dbg_addr == 24'h100041) rv_clr_tgl  <= ~rv_clr_tgl;
    end
    rv_dbg rvd (.clk(cpu_clk), .rst(cpu_rst), .tap(rv_tap), .snap_req(rv_snap_tgl),
                .clr_req(rv_clr_tgl), .hold(rv_hold));
`ifdef NO_JTAG_DBG
    // T79 (Alex, 2026-10-11: "behind a flag"): no JTAG debug master (TRX_FEATURES without jtag);
    // every debug register, the console read-out and the JTAG DDR2 port fall away with it
    assign dbg_addr = 24'd0;
    assign dbg_wdata = 32'd0;
    assign dbg_re = 1'b0;
    assign dbg_we = 1'b0;
`else
    jtag_dbg dbg (
        .clk(sys_clk), .addr(dbg_addr), .wdata(dbg_wdata), .re(dbg_re), .we(dbg_we),
        .rdata(dbg_is_ddr ? jtag_ddr_rdata : dbg_is_reg ? dbg_reg :
               dbg_is_con ? con_rdata : dbg_is_edge ? bk_rdata : dbg_is_trx ? trx_j_rdata :
               dbg_is_ram ? {24'd0, ram_rdata} : 32'd0),
        .rvalid(dbg_rvalid_fast || (dbg_is_ddr && jtag_ddr_done) || (dbg_is_trx && trx_j_rvalid))
    );
`endif

    // the RISC-V console, read through the debug port
    con_ring con (
        .wclk(cpu_clk), .we(con_valid), .wdata(con_data),
        .rclk(sys_clk), .raddr(dbg_addr[11:2]), .rdata(con_rdata), .count(con_count)
    );

    // ---- the core's packet port (T103, benched in sim/pktport): the UDP video/audio senders and their mux are the
    // core's, on cpu_clk; here only the crossing FIFO into the RMII arbiter above
    wire        l2_pp_req, l2_pp_valid, l2_pp_eof;
    wire  [7:0] l2_pp_data;
    assign pp_req   = dc_cpu ? 1'b0 : l2_pp_req;
    assign pp_valid = dc_cpu ? 1'b0 : l2_pp_valid;
    assign pp_eof   = dc_cpu ? 1'b0 : l2_pp_eof;
    assign pp_data  = dc_cpu ? 8'd0 : l2_pp_data;
`ifndef NO_NET
    pktport_host pkh (
        .wclk(cpu_clk), .wrst(cpu_rst), .dc(dc_cpu),
        .pp_req(pp_req), .pp_grant(pp_grant), .pp_data(pp_data), .pp_valid(pp_valid), .pp_eof(pp_eof), .pp_ready(pp_ready),
        .rclk(clk50), .rrst(eth_rst),
        .st_req(st_req), .st_data(st_tx_data), .st_valid(st_tx_valid), .st_eof(st_tx_eof),
        .pop(own_st & x_ready)
    );
`endif

    // ---- the app's C64 control (step C), into the C64 clock ------------------------
    // (CART_TYPE is static around the reset and the force that take it)
    (* ASYNC_REG = "TRUE" *) reg [5:0] app_s1 = 6'b000000, app_s2 = 6'b000000;
    (* ASYNC_REG = "TRUE" *) reg [7:0] cart_type_s1 = 8'h00, cart_type_s2 = 8'h00;
    (* ASYNC_REG = "TRUE" *) reg [3:0] reu_s1 = 4'h0, reu_s2 = 4'h0;    // {REU_SIZE[2:0], REU_ENABLE} (T60)
    always @(posedge sys_clk) begin
        app_s1 <= {app_ultimax, 3'b000, app_c64_pause, app_c64_reset};
        app_s2 <= app_s1;
        c64_mute <= c64_halted | app_s2[1];
        cart_type_s1 <= app_cart_type;
        cart_type_s2 <= cart_type_s1;
        reu_s1 <= {app_reu_size, app_reu_en};
        reu_s2 <= reu_s1;
    end
    // The app's C64_STOP pauses the whole machine (T12 B) only with GATE_CTRL
    // b0 (trx_sys.v) and JTAG register 9 bit 4 clear; otherwise it pauses
    // only the CPU, as before T12 B (the reset value, until our firmware
    // draws the freeze menu: T12 P2).
    wire        gate_frz;
    wire        app_pause = gate_frz && !machine_ctl[4];
    wire        dbg_own;                    // the debugger owns run control (trx_dbg.v OWNER)
    wire        dbg_hold, dbg_rel;          // (dbg_stop is declared with the DMA window's wires)
    wire        app_stop = app_s2[1] & !dbg_own;
    wire        c64_halt_req = machine_ctl[3] | (app_stop & app_pause);
    // T40: a System Mode switch halts the machine too, unseen by the DMA window
    (* ASYNC_REG = "TRUE" *) reg [1:0] mc_halt_s = 2'b00;
    (* ASYNC_REG = "TRUE" *) reg [1:0] vmode_s1 = 2'd0, vmode_sys = 2'd0;
    always @(posedge sys_clk) begin
        mc_halt_s <= {mc_halt_s[0], mc_halt};
        vmode_s1  <= vmode;
        vmode_sys <= vmode_s1;
    end
    // the C64 keyboard and joysticks: the physical ones (keys_s2, pins_s2,
    // bl_restore) with the app's (T38); the physical ones stay on their own
    wire [63:0] c64_keys;
    wire  [4:0] c64_joy1, c64_joy2;
    wire        c64_restore, c64_app_reset, c64_freeze;
    app_input ain (
        .clk(sys_clk), .ovl_own(ovl_own_s[1]), .phys_keys(keys_s2),
        .phys_joy1(pins_s2[4:0]), .phys_joy2(pins_s2[9:5]), .phys_restore(pins_s2[10] | bl_restore),
        .app_keys(app_keys), .app_joy1(app_joy1), .app_joy2(app_joy2),
        .app_restore(app_restore), .app_reset(app_reset), .app_freeze(app_freeze),
        .keys(c64_keys), .joy1(c64_joy1), .joy2(c64_joy2), .restore(c64_restore),
        .reset(c64_app_reset), .freeze(c64_freeze)
    );
    wire        c64_reset_req = machine_ctl[0] | app_s2[0] | c64_app_reset;
    // DMA window and lens unit (dma_win.v): a byte goes to the C64 RAM's second port whenever
    // the debug port does not use it in that clock, or with the C64 stopped becomes a C64
    // bus cycle; a byte of the app's ROM apertures (dma_rom) goes to the ROM block through
    // the same port
    wire        win_port_use, win_port_rom, win_port_we;
    wire [15:0] win_port_addr;
    wire  [7:0] win_port_wdata;
    wire        dma_cyc_req, dma_cyc_we, dma_cyc_done;
    wire [15:0] dma_cyc_addr;
    wire  [7:0] dma_cyc_wdata, dma_cyc_rdata;
    wire        core_ultimax;
    wire  [1:0] dma_cyc_force;
    wire        lk_go;
    wire [18:0] lk_req;
    wire [31:0] lk_res;
    wire [11:0] sb_addr;
    wire [31:0] sb_rdata;
    wire        sb_frozen;
    wire  [9:0] cram_addr, win_cram_addr, tx_cram_addr;
    wire        cram_we, win_cram_use;
    wire  [3:0] cram_wdata, cram_rdata;
    wire [15:0] tx_view;
    wire        tx_req, tx_rom;
    wire [15:0] tx_paddr;
    wire [15:0] pk_addr;
    wire  [1:0] pk_cls;
    wire        pk_hit, pk_mem, pk_ext;
    wire [23:0] pk_maddr;
    wire  [7:0] pk_data;
    wire  [7:0] cap_every;
    wire [15:0] cap_gen, cap_nosnap;
    assign core_reset_req = c64_reset_req;
    // The RAM's second port: the window, then JTAG, then the text layer's reads
    // (c64_text.v), then APPLY, in the clocks the ones before do not use. The
    // colour RAM's second port: the window's aperture in the clocks it uses it,
    // the text layer's in the others.
    wire        tx_go      = tx_req && !win_port_use && !(dbg_re || dbg_we);
    // the engine's APPLY (trx_engine.v, T20 §17.3) last: a byte in a clock none of them uses
    wire        ap_req, ap_gnt;
    wire [15:0] ap_addr;
    wire  [7:0] ap_wdata;
    assign      ap_gnt     = ap_req && !win_port_use && !tx_go && !(dbg_re || dbg_we);
    wire        port_req   = win_port_use || tx_go || ap_gnt || dbg_re || dbg_we;   // c64_mem's second port wanted (T29)
    wire [15:0] port_addr  = win_port_use ? win_port_addr : tx_go ? tx_paddr : ap_gnt ? ap_addr : dbg_addr[15:0];
    wire        port_we    = win_port_use ? win_port_we : ap_gnt || (dbg_we && dbg_addr[23:16] == 8'h00);
    wire [7:0]  port_wdata = win_port_use ? win_port_wdata : ap_gnt ? ap_wdata : dbg_wdata[7:0];
    wire        port_rom   = win_port_use ? win_port_rom : tx_go && tx_rom;
    assign cram_addr = win_cram_use ? win_cram_addr : tx_cram_addr;

    // the engine's side of c64_core and trx_sys (trx_engine below)
    wire        log_on, seq_busy, mk, mk_snap, mk_cap, mk_skip, mk_intg, reu_busy, phi2_tick;
    // debug (trx_dbg.v in c64_core, drv_dbg.v): page 3 / page 0 words from trx_sys
    wire        dr_we, dr_re, dbg_ev, dbg_log_put;
    wire [10:0] dr_addr;
    wire [31:0] dr_wdata, dbg_dr_rdata;
    wire [63:0] dbg_log_rec;
    wire        tr_ib;                      // the trace former's feed (T21 §4.11.1)
    wire [15:0] tr_pc;
    wire  [7:0] tr_op, tr_b1, tr_b2;
    wire [39:0] tr_regs;
    wire  [9:0] mk_x;
    wire  [8:0] mk_line;
    wire        p2_we, p2_re, seq_sb_req, seq_sb_gnt;
    wire  [9:0] p2_addr;
    wire [31:0] p2_wdata, p2_rdata;
    wire [11:0] seq_sb_addr;
    wire        wa_we, wc_we, wk_we;
    wire [15:0] wa_addr;
    wire  [7:0] wa_new, wa_old, wk_new;
    wire  [9:0] wc_addr;
    wire  [3:0] wc_new, wc_old;
    wire [14:0] wk_addr;
    // restore (trx_sys page 0/1 -> c64_core): staged writes, loads, stop position
    wire        sb_we, sb_ldreq, pos_set, pos_arm_in, pos_release, pos_arm, pos_hold;
    wire [11:0] sb_waddr;
    wire [31:0] sb_wdata;
    wire  [4:0] sb_ldmask, sb_ldarm;
    wire  [5:0] pos_cyc;
    wire [19:0] resume_phase, rp_pend;
    wire        rp_set;
    // the app's C64_BUS_INTERNAL/EXTERNAL into the C64 clock (quasi-static: the
    // port takes them only in reset or while paused)
    (* ASYNC_REG = "TRUE" *) reg [7:0] bus_m_s1 = 8'h0F, bus_m_s2 = 8'h0F;
    always @(posedge sys_clk) begin
        bus_m_s1 <= {bus_ext_cpu, bus_int_cpu};
        bus_m_s2 <= bus_m_s1;
    end
    // machine pause: JTAG bit 3, or the app's C64_STOP as above


    // the engine (trx_engine.v, page 2): frame markers, the chip block of every
    // capture, the write log; its DDR2 side on mig_arb's engine client

    // our IO block's pages in this clock (trx_sys.v): gate and capture
    // control, state bus, lens unit, text layer view; from the app through trx_io, from
    // JTAG through the JTAG page
    reg         c64_halted_q = 1'b0;
    always @(posedge sys_clk) begin
        c64_halted_q <= c64_halted;
        if ((c64_halted && !c64_halted_q) || dbg_ev) stop_tgl <= ~stop_tgl;
    end

    // the SID samples for HDMI: held in the mclk domain, a toggle per sample; taken over in the pixel clock by hdmi_out.v
    reg [47:0] snd_hold;
    reg        snd_tgl = 1'b0;
    always @(posedge mclk)
        if (audio_sample_en) begin
            snd_hold <= {snd_l, snd_r};
            snd_tgl  <= ~snd_tgl;
        end

    // ---- HDMI: hdmi_out.v (T164 WP8, G1) holds the mode request, the synthesizer sequencer, mmcm_pix, the timing generator, the VIC path
    // (vic_scaler on the timing's counters), the menu overlay, the pixel layer, the freeze text, the encoder, the TMDS bit repeater and the
    // GTPs. c64_type 0: no DFX core ships on the VIC path (T164 Q4), so the request is not held to the System Mode's family.
    wire        gt_ready, video_locked_s;
    hdmi_out #(.SMOOTH(1)) hout (
        .clk50(clk50), .cpu_clk(cpu_clk), .sys_clk(sys_clk),
        .cdce_rst(por_rst | ~clocks_ok), .gt_rst(gt_rst), .c64_type(1'b0), .fam_v60(mc_cdce_v60), .core_decouple(core_decouple),
        .refclk_p(HDMI_REFCLK_P), .refclk_n(HDMI_REFCLK_N),
        .data_p(HDMI_DATA_P), .data_n(HDMI_DATA_N), .clk_p(HDMI_CLK_P), .clk_n(HDMI_CLK_N),
        .scl_low(scl_low), .sda_low(sda_low), .i2c_done(i2c_done),
        .ovl_we(ovl_we), .ovl_addr(ovl_addr), .ovl_wdata(ovl_wdata), .ovl_rdata(ovl_rdata), .ovl_own(ovl_own),
        .px_we(px_we), .px_re(px_re), .px_addr(cw_addr[11:0]), .px_wdata(cw_wdata), .pxl_rdata(pxl_rdata),
        .px_ev_vsync(px_ev_vsync), .px_ev_flip(px_ev_flip),
        .ui_clk(ui_clk), .ui_rst(ui_rst || !ddr_calib), .v_addr(v_addr), .v_cmd(v_cmd), .v_en(v_en),
        .v_rdy(v_rdy), .v_wdf_data(v_wdf_data), .v_wdf_mask(v_wdf_mask), .v_wdf_wren(v_wdf_wren),
        .v_rd_data(c_rd_data), .v_rd_data_valid(v_rd_valid), .v_urgent(v_urgent),
        .tx_rdata(tx_rdata), .c64_halted(c64_halted), .tx_view(tx_view),
        .tx_req(tx_req), .tx_paddr(tx_paddr), .tx_rom(tx_rom), .tx_go(tx_go), .ram_rdata(ram_rdata),
        .tx_cram_addr(tx_cram_addr), .cram_grant(!win_cram_use), .cram_rdata(cram_rdata),
        .pix_color(pix_color), .pix_x(pix_x), .pix_y(pix_y), .v60(v60), .scanlines(scanlines),
        .pal_we(pal_we), .pal_addr(pal_addr), .pal_wdata(pal_wdata), .x_text(x_text),
        .snd_hold(snd_hold), .snd_tgl(snd_tgl),
        .gt_ready(gt_ready), .video_locked_s(video_locked_s), .hdmi_meas(hdmi_meas_s)
    );

    // ---- analog audio: the same SID samples to the codec over I2S -------------
    wire i2s_bclk, i2s_lrck, i2s_data, i2s_spk;
    i2s_out i2s (
        .mclk(mclk), .frame_en(audio_sample_en), .in_l(snd_l), .in_r(snd_r),
        .spk_en(speaker_en), .spk_gain(mix_spk),
        .bclk(i2s_bclk), .lrck(i2s_lrck), .sdata(i2s_data), .spk(i2s_spk)
    );

    ODDR #(.DDR_CLK_EDGE("SAME_EDGE")) fwd_mclk (.Q(AUDIO_MCLK), .C(mclk), .CE(1'b1),
        .D1(1'b1), .D2(1'b0), .R(1'b0), .S(1'b0));
    assign AUDIO_BCLK  = i2s_bclk;
    assign AUDIO_LRCLK = i2s_lrck;
    assign AUDIO_SDI   = i2s_data;
    assign SPEAKER_DATA = i2s_spk;

    // ---- LEDs ------------------------------------------------------------------
    reg [25:0] blink = 0;
    always @(posedge clk50) blink <= blink + 1;
    assign LED_BOARDn = 1'b0;               // off: active high despite the name (green, on the FPGA module)
    // case LEDs: drive A = the app's 1541 (power always on, activity = its LED),
    // no drive B, no IRQ source yet
    always @(posedge cpu_clk) begin
        cled_s1 <= {!drive_leds_n[0], gt_ready & video_locked_s, c64_leds[0]};
        cled_s2 <= cled_s1;
    end
    wire led1n, led2n;
    case_leds cled (.clk(cpu_clk), .sel(caseled_sel), .sel_set(caseled_set),
                    .def1(cled_s2[0]), .def2(cled_s2[1]),
                    .a_pwr(1'b1), .b_pwr(1'b0), .a_act(cled_s2[2]), .b_act(1'b0),
                    .usb_act(busy_usb), .any_act(busy_any | cled_s2[2]), .irq_line(1'b0),
                    .led1n(led1n), .led2n(led2n));
    // The case LED pins are active high despite their names (Gideon's XDC):
    // boundary scan of his design shows H19/H17 high with the selector On and
    // low with Off (1541ultimate-tools pinmap notes, 2026-09-30).
    assign LED_CASE1n = !led1n;
    assign LED_CASE2n = !led2n;
    assign LED_CASE3n = 1'b0;

    // ---- T79.1 decoupler (docs/specs/T79.1-host-core-contract.md section 7), the host side of the l2 edge ----
    // core_decouple (JTAG 0x100029) is 1 while the core is being swapped: l2's outputs are undefined then,
    // so every one that reaches the host or a pin is held at its idle here (all transparent at 0).
    dfx_sync dcs_cpu  (.clk(cpu_clk), .i(core_decouple), .o(dc_cpu));
    dfx_sync dcs_mclk (.clk(mclk),    .i(core_decouple), .o(dc_mclk));
    dfx_sync dcs_ui   (.clk(ui_clk),  .i(core_decouple), .o(dc_ui));
    // (the scaler locks to the new core's VIC frame after a swap: hdmi_out.v takes core_decouple)

    // l2's outputs as it drives them
    wire l2_IEC_ATN_O;
    wire l2_IEC_CLOCK_O;
    wire l2_IEC_DATA_O;
    wire l2_SLOT_DOTCLK;
    wire l2_SLOT_PHI2;
    wire l2_slot_rst_o;
    wire l2_SLOT_ADDRDIR;
    wire l2_SLOT_ADDROEn;
    wire l2_SLOT_BA;
    wire l2_SLOT_DATADIR;
    wire l2_SLOT_DATAOEn;
    wire l2_SLOT_IO1n;
    wire l2_SLOT_IO2n;
    wire l2_SLOT_ROMHn;
    wire l2_SLOT_ROMLn;
    wire [15:0] l2_slot_a;
    wire l2_slot_ahi_t;
    wire [7:0] l2_slot_d_o;
    wire l2_slot_d_t;
    wire l2_slot_rw;
    wire l2_ap_req;
    wire [4:0] l2_c64_leds;
    wire l2_d7ff_ev;
    wire l2_dbg_ev;
    wire l2_dbg_own;
    wire l2_dbg_stop;
    wire l2_gate_frz;
    wire l2_win_port_use;
    wire l2_win_cram_use;
    wire l2_reu_cmd_valid;
    wire [15:0] l2_tx_view;
    wire [7:0] l2_ram_rdata;
    wire [7:0] l2_sid_lv_data;
    wire [31:0] l2_trx_j_rdata;
    wire l2_c64_ce;
    wire l2_c64_halted;
    wire l2_trx_j_rvalid;
    wire signed  [23:0] l2_audio_l;
    wire signed  [23:0] l2_audio_r;
    wire l2_c_en;
    wire l2_c_wdf_wren;
    wire l2_e_en;
    wire l2_e_wdf_wren;
    wire [3:0] l2_cram_rdata;
    wire [7:0] l2_cw_rdata;
    wire l2_cw_ack;
    wire l2_cw_irq;
    wire [7:0] l2_dma_rdata_q;
    wire l2_drv_io_ack;
    wire l2_drv_io_nc;
    wire [7:0] l2_drv_io_rdata;
    wire l2_gcr_io_ack;
    wire l2_gcr_io_nc;
    wire [7:0] l2_gcr_io_rdata;
    wire l2_px_re;
    wire l2_px_we;
    wire [2:0] l2_drive_leds_n;
    wire l2_cart_active, l2_dma_done_tgl, l2_drv_tgl, l2_drv_we, l2_drv_done_tgl;
    wire [25:0] l2_drv_addr; wire [31:0] l2_drv_wdata; wire [3:0] l2_drv_be;
    assign IEC_ATN_O = core_decouple ? 1'b0 : l2_IEC_ATN_O;
    assign IEC_CLOCK_O = core_decouple ? 1'b0 : l2_IEC_CLOCK_O;
    assign IEC_DATA_O = core_decouple ? 1'b0 : l2_IEC_DATA_O;
    // T135 D6a: under the lock PHI2 / DOTCLK run at the C64 rate from sys_clk (portlock_clk), RESET low; the decouple forces both low
    portlock_clk plclk (.clk(sys_clk), .lock(port_lock), .decouple(core_decouple), .l2_phi2(l2_SLOT_PHI2), .l2_dotclk(l2_SLOT_DOTCLK),
                        .l2_rst_o(l2_slot_rst_o), .phi2(SLOT_PHI2), .dotclk(SLOT_DOTCLK), .rst_o(slot_rst_o), .gen_phi2(), .gen_dotclk());
    assign SLOT_ADDRDIR = port_off ? 1'b1 : l2_SLOT_ADDRDIR;
    assign SLOT_ADDROEn = port_off ? 1'b1 : l2_SLOT_ADDROEn;
    assign SLOT_BA = port_off ? 1'b1 : l2_SLOT_BA;
    assign SLOT_DATADIR = port_off ? 1'b1 : l2_SLOT_DATADIR;
    assign SLOT_DATAOEn = port_off ? 1'b1 : l2_SLOT_DATAOEn;
    assign SLOT_IO1n = port_off ? 1'b1 : l2_SLOT_IO1n;
    assign SLOT_IO2n = port_off ? 1'b1 : l2_SLOT_IO2n;
    assign SLOT_ROMHn = port_off ? 1'b1 : l2_SLOT_ROMHn;
    assign SLOT_ROMLn = port_off ? 1'b1 : l2_SLOT_ROMLn;
    assign slot_a = port_off ? {16{1'b1}} : l2_slot_a;
    assign slot_ahi_t = port_off ? 1'b1 : l2_slot_ahi_t;
    assign slot_d_o = port_off ? {8{1'b1}} : l2_slot_d_o;
    assign slot_d_t = port_off ? 1'b1 : l2_slot_d_t;
    assign slot_rw = port_off ? 1'b1 : l2_slot_rw;
    assign ap_req = core_decouple ? 1'b0 : l2_ap_req;
    assign c64_leds = l2_c64_leds;
    assign d7ff_ev = core_decouple ? 1'b0 : l2_d7ff_ev;
    assign dbg_ev = core_decouple ? 1'b0 : l2_dbg_ev;
    assign dbg_own = core_decouple ? 1'b0 : l2_dbg_own;
    assign dbg_stop = core_decouple ? 1'b0 : l2_dbg_stop;
    assign gate_frz = core_decouple ? 1'b0 : l2_gate_frz;
    assign win_port_use = core_decouple ? 1'b0 : l2_win_port_use;
    assign win_cram_use = core_decouple ? 1'b0 : l2_win_cram_use;
    assign reu_cmd_valid = core_decouple ? 1'b0 : l2_reu_cmd_valid;
    assign tx_view = l2_tx_view;
    assign ram_rdata = l2_ram_rdata;
    assign sid_lv_data = l2_sid_lv_data;
    assign trx_j_rdata = l2_trx_j_rdata;
    assign c64_ce = core_decouple ? 1'b1 : l2_c64_ce;
    assign c64_halted = core_decouple ? 1'b1 : l2_c64_halted;
    assign trx_j_rvalid = core_decouple ? 1'b1 : l2_trx_j_rvalid;
    assign audio_l = l2_audio_l;
    assign audio_r = l2_audio_r;
    assign c_en = dc_ui ? 1'b0 : l2_c_en;
    assign c_wdf_wren = dc_ui ? 1'b0 : l2_c_wdf_wren;
    assign e_en = dc_ui ? 1'b0 : l2_e_en;
    assign e_wdf_wren = dc_ui ? 1'b0 : l2_e_wdf_wren;
    assign cram_rdata = l2_cram_rdata;
    assign cw_rdata = l2_cw_rdata;
    assign cw_ack = dc_cpu ? 1'b0 : l2_cw_ack;
    assign cw_irq = dc_cpu ? 1'b0 : l2_cw_irq;
    assign dma_rdata_q = l2_dma_rdata_q;
    assign drv_io_ack = dc_cpu ? 1'b0 : l2_drv_io_ack;
    assign drv_io_nc = dc_cpu ? 1'b0 : l2_drv_io_nc;
    assign drv_io_rdata = l2_drv_io_rdata;
    assign gcr_io_ack = dc_cpu ? 1'b0 : l2_gcr_io_ack;
    assign gcr_io_nc = dc_cpu ? 1'b0 : l2_gcr_io_nc;
    assign gcr_io_rdata = l2_gcr_io_rdata;
    assign px_re = dc_cpu ? 1'b0 : l2_px_re;
    assign px_we = dc_cpu ? 1'b0 : l2_px_we;
    assign drive_leds_n = l2_drive_leds_n;
    // Data buses go through ungated: each is read only with its valid, ack or enable, and those are gated
    // (audio: the mixer mutes on c64_halted, which reads 1 while decoupled). Saves the muxes at the edge.
    // cartridge-active holds its last value (a swap is no cartridge removal for the app)
    reg ca_hold = 1'b0;
    always @(posedge cpu_clk) if (!dc_cpu) ca_hold <= l2_cart_active;
    assign cart_active = dc_cpu ? ca_hold : l2_cart_active;
    // toggles host -> core: the core sees 0 while decoupled and from release on the host's toggles relative to that
    wire dma_req_tgl_c, app_cart_force_tgl_c, app_cart_kill_tgl_c, turbo_upd_c, stop_tgl_c;
    dfx_h2c_tgl t_dma   (.clk(cpu_clk), .dc(dc_cpu), .i(dma_req_tgl),         .o(dma_req_tgl_c));
    dfx_h2c_tgl t_force (.clk(cpu_clk), .dc(dc_cpu), .i(app_cart_force_tgl),  .o(app_cart_force_tgl_c));
    dfx_h2c_tgl t_kill  (.clk(cpu_clk), .dc(dc_cpu), .i(app_cart_kill_tgl),   .o(app_cart_kill_tgl_c));
    dfx_h2c_tgl t_turbo (.clk(cpu_clk), .dc(dc_cpu), .i(turbo_upd),           .o(turbo_upd_c));
    dfx_h2c_tgl t_stop  (.clk(sys_clk), .dc(core_decouple), .i(stop_tgl),     .o(stop_tgl_c));
    // toggle core -> host: the DMA window's done
    dfx_c2h_tgl t_ddone (.clk(cpu_clk), .dc(dc_cpu), .i(l2_dma_done_tgl),     .o(dma_done_tgl));
    // the drive's DDR2 client (mig_arb client 2)
    dfx_drv_port drvp (.clk(cpu_clk), .dc(dc_cpu),
        .c_req_tgl(l2_drv_tgl), .c_done_tgl(l2_drv_done_tgl), .a_req_tgl(drv_tgl), .a_done_tgl(drv_done_tgl));
    assign drv_addr = l2_drv_addr; assign drv_we = l2_drv_we; assign drv_wdata = l2_drv_wdata; assign drv_be = l2_drv_be;



    // The constant goes into the partition through flops (DONT_TOUCH), not as a tie: a constant on a partition input would
    // sit on the global GND/VCC net, which is what the T87 boundary rule (tools/vivado_lib.tcl) keeps out of the carve-out.
    (* DONT_TOUCH = "TRUE" *) reg [31:0] host_caps_q = HOST_CAPS;
    always @(posedge cpu_clk) host_caps_q <= HOST_CAPS;

    // ---- pins for the cores (T103): the host owns the pads, the core the logic. Everything is asynchronous to the
    // core's clocks and is synchronised there. While a core is swapped (core_decouple) every pad is released: SENSE,
    // READ, WRITE, DIS and ADJ high-Z, the SID sockets deselected.
    wire l2_c2n_motor_o, l2_c2n_motor_en, c2n_motor_i;
    IOBUF b_c2n_motor (.IO(C2N_MOTOR), .I(l2_c2n_motor_o), .T(core_decouple | ~l2_c2n_motor_en), .O(c2n_motor_i));
    wire l2_c2n_sense_o, l2_c2n_read_o, l2_c2n_read_en, l2_c2n_write_o, l2_c2n_write_en;
    wire l2_paddle_dis_o, l2_paddle_dis_en, l2_paddle_adj_o, l2_paddle_adj_en;
    wire l2_sid_sock1_n, l2_sid_sock2_n, l2_sid_sock_addr;
    wire c2n_sense_i, c2n_read_i, c2n_write_i;
    IOBUF b_c2n_sense (.IO(C2N_SENSE), .I(1'b0),          .T(core_decouple | ~l2_c2n_sense_o), .O(c2n_sense_i));   // open drain
    IOBUF b_c2n_read  (.IO(C2N_READ),  .I(l2_c2n_read_o),  .T(core_decouple | ~l2_c2n_read_en),  .O(c2n_read_i));
    IOBUF b_c2n_write (.IO(C2N_WRITE), .I(l2_c2n_write_o), .T(core_decouple | ~l2_c2n_write_en), .O(c2n_write_i));
    OBUFT b_pad_dis   (.O(PADDLE_DIS), .I(l2_paddle_dis_o), .T(core_decouple | ~l2_paddle_dis_en));
    OBUFT b_pad_adj   (.O(PADDLE_ADJ), .I(l2_paddle_adj_o), .T(core_decouple | ~l2_paddle_adj_en));
    assign SLOT_SID1n    = core_decouple ? 1'b1 : l2_sid_sock1_n;
    assign SLOT_SID2n    = core_decouple ? 1'b1 : l2_sid_sock2_n;
    assign SLOT_SID_ADDR = core_decouple ? 1'b0 : l2_sid_sock_addr;

    // T79 trial B: layer 2 (layer2.v), the reconfigurable partition
    layer2 l2 (   // parameters: layer2.v carries top.v's values (an out-of-context partition takes none)
        .IEC_ATN_I(IEC_ATN_I),
        .IEC_ATN_O(l2_IEC_ATN_O),
        .IEC_CLOCK_I(IEC_CLOCK_I),
        .IEC_CLOCK_O(l2_IEC_CLOCK_O),
        .IEC_DATA_I(IEC_DATA_I),
        .IEC_DATA_O(l2_IEC_DATA_O),
        .SLOT_ADDRDIR(l2_SLOT_ADDRDIR),
        .SLOT_ADDROEn(l2_SLOT_ADDROEn),
        .SLOT_BA(l2_SLOT_BA),
        .SLOT_DATA(SLOT_DATA),
        .SLOT_DATADIR(l2_SLOT_DATADIR),
        .SLOT_DATAOEn(l2_SLOT_DATAOEn),
        .SLOT_DMAn(SLOT_DMAn),
        .SLOT_DOTCLK(l2_SLOT_DOTCLK),
        .SLOT_EXROMn(SLOT_EXROMn),
        .SLOT_GAMEn(SLOT_GAMEn),
        .SLOT_IO1n(l2_SLOT_IO1n),
        .SLOT_IO2n(l2_SLOT_IO2n),
        .SLOT_IRQn(SLOT_IRQn),
        .SLOT_NMIn(SLOT_NMIn),
        .SLOT_PHI2(l2_SLOT_PHI2),
        .SLOT_ROMHn(l2_SLOT_ROMHn),
        .SLOT_ROMLn(l2_SLOT_ROMLn),
        .SLOT_RST_I(SLOT_RST_I),
        .ap_addr(ap_addr),
        .ap_gnt(ap_gnt),
        .ap_req(l2_ap_req),
        .ap_wdata(ap_wdata),
        .app_cart_force_tgl(app_cart_force_tgl_c),
        .app_cart_kill_tgl(app_cart_kill_tgl_c),
        .app_pause(app_pause),
        .app_s2(app_s2),
        .app_stop(app_stop),
        .audio_l(l2_audio_l),
        .audio_r(l2_audio_r),
        .audio_sample_en(audio_sample_en),
        .adc_sdo(AUDIO_SDO),
        .adc_bclk(i2s_bclk),
        .adc_lrck(i2s_lrck),
        .host_caps(host_caps_q),
        .pp_data(l2_pp_data),
        .pp_eof(l2_pp_eof),
        .pp_grant(pp_grant),
        .pp_ready(pp_ready),
        .pp_req(l2_pp_req),
        .pp_valid(l2_pp_valid),
        .c2n_motor_i(c2n_motor_i),
        .c2n_motor_o(l2_c2n_motor_o),
        .c2n_motor_en(l2_c2n_motor_en),
        .c2n_sense_i(c2n_sense_i),
        .c2n_sense_o(l2_c2n_sense_o),
        .c2n_read_i(c2n_read_i),
        .c2n_read_o(l2_c2n_read_o),
        .c2n_read_en(l2_c2n_read_en),
        .c2n_write_i(c2n_write_i),
        .c2n_write_o(l2_c2n_write_o),
        .c2n_write_en(l2_c2n_write_en),
        .paddle_cmp_i(PADDLE_CMP),
        .kbjoy_i({kbj2_i, kbj1_i}),
        .kbjoy_low(l2_kbjoy_low),
        .av_ga(av_ga), .av_gb(av_gb), .av_gc(av_gc), .av_syn_n(av_syn_n), .av_fb(av_fb),
        .paddle_dis_o(l2_paddle_dis_o),
        .paddle_dis_en(l2_paddle_dis_en),
        .paddle_adj_o(l2_paddle_adj_o),
        .paddle_adj_en(l2_paddle_adj_en),
        .sid_sock1_n(l2_sid_sock1_n),
        .sid_sock2_n(l2_sid_sock2_n),
        .sid_sock_addr(l2_sid_sock_addr),
        .bus_m_s2(bus_m_s2),
        .c64_atn_o(c64_atn_o),
        .c64_ce(l2_c64_ce),
        .c64_clk_o(c64_clk_o),
        .c64_data_o(c64_data_o),
        .c64_freeze(c64_freeze),
        .c64_gclk(c64_gclk),
        .c64_halt_req(c64_halt_req),
        .c64_halted(l2_c64_halted),
        .c64_joy1(c64_joy1),
        .c64_joy2(c64_joy2),
        .c64_keys(c64_keys),
        .c64_leds(l2_c64_leds),
        .c64_restore(c64_restore),
        .c_addr(c_addr),
        .c_cmd(c_cmd),
        .c_en(l2_c_en),
        .c_rd_data(c_rd_data),
        .c_rd_valid(c_rd_valid & ~dc_ui),
        .c_rdy(c_rdy),
        .c_wdf_data(c_wdf_data),
        .c_wdf_mask(c_wdf_mask),
        .c_wdf_rdy(c_wdf_rdy),
        .c_wdf_wren(l2_c_wdf_wren),
        .cart_active(l2_cart_active),
        .cart_type_s2(cart_type_s2),
        .core_reset_req(core_reset_req),
        .cpu_clk(cpu_clk),
        .cpu_rst(cpu_rst | dc_cpu),
        .cram_addr(cram_addr),
        .cram_rdata(l2_cram_rdata),
        .cw_ack(l2_cw_ack),
        .cw_addr(cw_addr),
        .cw_irq(l2_cw_irq),
        .cw_rdata(l2_cw_rdata),
        .cw_re(cw_re),
        .cw_wdata(cw_wdata),
        .cw_we(cw_we),
        .d7ff_data(d7ff_data),
        .d7ff_ev(l2_d7ff_ev),
        .dbg_addr(dbg_addr),
        .dbg_ev(l2_dbg_ev),
        .dbg_own(l2_dbg_own),
        .dbg_re(dbg_re),
        .dbg_stop(l2_dbg_stop),
        .dbg_trx_sel(dbg_trx_sel),
        .dbg_wdata(dbg_wdata),
        .dbg_we(dbg_we),
        .ddr_cal(ddr_cal),
        .ddr_calib(ddr_calib & ~dc_ui),
        .dma_addr(dma_addr),
        .dma_bus(dma_bus),
        .dma_done_tgl(l2_dma_done_tgl),
        .dma_rdata_q(l2_dma_rdata_q),
        .dma_req_tgl(dma_req_tgl_c),
        .dma_rom(dma_rom),
        .dma_wdata(dma_wdata),
        .dma_we(dma_we),
        .drive_dbg(drive_dbg),
        .drive_leds_n(l2_drive_leds_n),
        .drv_addr(l2_drv_addr),
        .drv_be(l2_drv_be),
        .drv_c64_rst_n(drv_c64_rst_n),
        .drv_c64_stop(drv_c64_stop),
        .drv_clk_o(drv_clk_o),
        .drv_data_o(drv_data_o),
        .drv_done_rdata(drv_done_rdata),
        .drv_done_tgl(l2_drv_done_tgl),
        .drv_halt_s(drv_halt_s),
        .drv_io_ack(l2_drv_io_ack),
        .drv_io_addr(drv_io_addr),
        .drv_io_nc(l2_drv_io_nc),
        .drv_io_rd(drv_io_rd),
        .drv_io_rdata(l2_drv_io_rdata),
        .drv_io_wdata(drv_io_wdata),
        .drv_io_wr(drv_io_wr),
        .drv_t16(drv_t16),
        .drv_t1k(drv_t1k),
        .drv_t4(drv_t4),
        .drv_tgl(l2_drv_tgl),
        .drv_wdata(l2_drv_wdata),
        .drv_we(l2_drv_we),
        .e_addr(e_addr),
        .e_cmd(e_cmd),
        .e_en(l2_e_en),
        .e_rd_valid(e_rd_valid & ~dc_ui),
        .e_rdy(e_rdy),
        .e_wdf_data(e_wdf_data),
        .e_wdf_mask(e_wdf_mask),
        .e_wdf_wren(l2_e_wdf_wren),
        .gate_frz(l2_gate_frz),
        .gcr_io_ack(l2_gcr_io_ack),
        .gcr_io_addr(gcr_io_addr),
        .gcr_io_nc(l2_gcr_io_nc),
        .gcr_io_rd(gcr_io_rd),
        .gcr_io_rdata(l2_gcr_io_rdata),
        .gcr_io_wdata(gcr_io_wdata),
        .gcr_io_wr(gcr_io_wr),
        .iec_port_off(iec_port_off),
        .iec_rst_n_cpu(iec_rst_n_cpu),
        .machine_ctl(machine_ctl),
        .mc_halt_s(mc_halt_s),
        .mclk(mclk),
        .mclk_rst(mclk_rst | dc_mclk),
        .pix_color(pix_color),
        .pix_x(pix_x),
        .pix_y(pix_y),
        .port_addr(port_addr),
        .port_req(port_req),
        .port_rom(port_rom),
        .port_wdata(port_wdata),
        .port_we(port_we),
        .px_ev_flip(px_ev_flip & ~dc_cpu),
        .px_ev_vsync(px_ev_vsync & ~dc_cpu),
        .px_rdata(px_rdata),
        .px_re(l2_px_re),
        .px_we(l2_px_we),
        .ram_rdata(l2_ram_rdata),
        .reu_cmd_addr(reu_cmd_addr),
        .reu_cmd_ready(reu_cmd_ready),
        .reu_cmd_valid(l2_reu_cmd_valid),
        .reu_cmd_we(reu_cmd_we),
        .reu_rdata(reu_rdata),
        .reu_rdata_valid(reu_rdata_valid & ~core_decouple),
        .reu_s2(reu_s2),
        .reu_wdata(reu_wdata),
        .reu_wdata_we(reu_wdata_we),
        .sid2x_clk(sid2x_clk),
        .sid_base(sid_base),
        .sid_digi(sid_digi),
        .sid_gains(sid_gains),
        .sid_gains2(sid_gains2),
        .sid_lv_addr(sid_lv_addr),
        .sid_lv_data(l2_sid_lv_data),
        .sid_mask(sid_mask),
        .sid_res(sid_res),
        .sid_split(sid_split),
        .sid_waves(sid_waves),
        .sidcv_addr(sidcv_addr),
        .sidcv_wdata(sidcv_wdata),
        .sidcv_we(sidcv_we),
        .slot_a(l2_slot_a),
        .slot_ahi_t(l2_slot_ahi_t),
        .slot_d_o(l2_slot_d_o),
        .slot_d_t(l2_slot_d_t),
        .slot_rst_o(l2_slot_rst_o),
        .slot_rw(l2_slot_rw),
        .smp_en(smp_en),
        .smp_f_addr(smp_f_addr),
        .smp_f_data(smp_f_data),
        .smp_f_done_tgl(smp_f_done_tgl),
        .smp_f_tgl(smp_f_tgl),
        .snp_addr(snp_addr),
        .snp_be(snp_be),
        .snp_dack_tag(snp_dack_tag),
        .snp_rack(snp_rack),
        .snp_rack_tag(snp_rack_tag),
        .snp_rdata(snp_rdata),
        .snp_req(snp_req),
        .snp_rwn(snp_rwn),
        .snp_tag(snp_tag),
        .snp_wdata(snp_wdata),
        .stop_tgl(stop_tgl_c),
        .sys_clk(sys_clk),
        .sys_rst(sys_rst | core_decouple),
        .trx_j_rdata(l2_trx_j_rdata),
        .trx_j_rvalid(l2_trx_j_rvalid),
        .turbo_en(turbo_en),
        .turbo_prefer(turbo_prefer),
        .turbo_upd(turbo_upd_c),
        .tx_view(l2_tx_view),
        .ui_clk(ui_clk),
        .ui_rst(ui_rst | dc_ui),
        .usr_access(usr_access),
        .vmode_sys(vmode_sys),
        .win_cram_addr(win_cram_addr),
        .win_cram_use(l2_win_cram_use),
        .win_port_addr(win_port_addr),
        .win_port_rom(win_port_rom),
        .win_port_use(l2_win_port_use),
        .win_port_wdata(win_port_wdata),
        .win_port_we(win_port_we)
    );

    // T201: the A/V port. Which ball is in which group: the boundary scan's three groups of eight (as cores/c128/mono/c128_avpins.v; the words are MSB first in the order listed).
    // Which word (Y, C, CVBS) goes to which group and in which bit order is layer 2's register setting (diag_avout.v). The DAC takes the words on CLK, the inverted sys_clk.
    assign {AVID_G[9], AVID_R[9], AVID_G[7], AVID_G[6], AVID_G[4], AVID_G[3], AVID_B[9], AVID_B[6]} = av_ga;
    assign {AVID_R[4], AVID_R[3], AVID_B[8], AVID_B[7], AVID_B[5], AVID_B[4], AVID_B[3], AVID_B[2]} = av_gb;
    assign {AVID_R[8], AVID_R[7], AVID_R[6], AVID_R[5], AVID_R[2], AVID_G[8], AVID_G[5], AVID_G[2]} = av_gc;
    assign AVID_SYNCn = av_syn_n;       // T259: a knob of diag_avout.v (reset: high, the sync is in the Y and CV words)
    assign AVID_FB    = av_fb;          // T259: a knob (reset: low)
    ODDR #(.DDR_CLK_EDGE("SAME_EDGE")) fwd_avid (.Q(AVID_CLK), .C(sys_clk), .CE(1'b1),
        .D1(1'b0), .D2(1'b1), .R(1'b0), .S(1'b0));

endmodule

// Asynchronous assert, synchronous release.
module rst_sync (
    input  wire clk,
    input  wire arst,
    output wire rst
);
    (* ASYNC_REG = "TRUE" *) reg [2:0] rst_sync = 3'b111;
    always @(posedge clk or posedge arst)
        if (arst) rst_sync <= 3'b111;
        else      rst_sync <= {rst_sync[1:0], 1'b0};
    assign rst = rst_sync[2];
endmodule
