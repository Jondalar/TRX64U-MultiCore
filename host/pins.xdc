# Pins used by c64_basic, taken from host/c64u.xdc (Gideon, issue #615).
set_property PACKAGE_PIN B17 [get_ports RMII_REFCLK]
set_property IOSTANDARD LVCMOS18 [get_ports RMII_REFCLK]
create_clock -name rmii_clock -period 20 [get_ports RMII_REFCLK]

set_property PACKAGE_PIN F6 [get_ports HDMI_REFCLK_P]
# The modes run 148.06-149.14 MHz (PAL 148.87, NTSC-50 149.14 = 6875/1456 of dot4x, 60 Hz 148.06-148.24);
# the 6.585 ns period (151.85 MHz, the old 50 Hz clock) is tighter than any of them and stays
create_clock -name hdmi_refclk -period 6.585 [get_ports HDMI_REFCLK_P]

set_property PACKAGE_PIN D17 [get_ports HDMI_CLK_SYNTH]
set_property IOSTANDARD LVCMOS18 [get_ports HDMI_CLK_SYNTH]
set_property DRIVE 4 [get_ports HDMI_CLK_SYNTH]
set_property PACKAGE_PIN AB5 [get_ports CLOCK_C64_SYNTH]
set_property IOSTANDARD LVCMOS18 [get_ports CLOCK_C64_SYNTH]
set_property DRIVE 4 [get_ports CLOCK_C64_SYNTH]

set_property PACKAGE_PIN U7 [get_ports I2C18_SCL]
set_property PACKAGE_PIN W6 [get_ports I2C18_SDA]
set_property IOSTANDARD LVCMOS18 [get_ports {I2C18_SCL I2C18_SDA}]
set_property PACKAGE_PIN L20 [get_ports HDMI_SCL]
set_property PACKAGE_PIN K18 [get_ports HDMI_SDA]
set_property IOSTANDARD LVCMOS33 [get_ports {HDMI_SCL HDMI_SDA}]
set_property PACKAGE_PIN K16 [get_ports I2C33_SCL]
set_property PACKAGE_PIN H15 [get_ports I2C33_SDA]
set_property IOSTANDARD LVCMOS33 [get_ports {I2C33_SCL I2C33_SDA}]
set_property PULLUP true [get_ports {I2C18_SCL I2C18_SDA HDMI_SCL HDMI_SDA I2C33_SCL I2C33_SDA}]

set_property PACKAGE_PIN N19 [get_ports HDMI_DDC_EN]
set_property IOSTANDARD LVCMOS33 [get_ports HDMI_DDC_EN]
set_property PACKAGE_PIN H18 [get_ports HDMI_HPD]
set_property IOSTANDARD LVCMOS33 [get_ports HDMI_HPD]

set_property PACKAGE_PIN P16 [get_ports LED_BOARDn]
set_property PACKAGE_PIN H19 [get_ports LED_CASE1n]
set_property PACKAGE_PIN H17 [get_ports LED_CASE2n]
set_property PACKAGE_PIN G17 [get_ports LED_CASE3n]
set_property IOSTANDARD LVCMOS33 [get_ports {LED_BOARDn LED_CASE1n LED_CASE2n LED_CASE3n}]

# Expansion port and SID sockets (T16, docs/specs/T16-cartridge-port.md §3.1,
# §13.1): every ball is a port, so none sits at Vivado's default pull-down.
# Balls, standards and drive as in Gideon's XDC / bitstream (no pulls there
# except CIA_RSTn); the pulls below are ours, for the lines a cart or nothing
# may leave floating.
set_property PACKAGE_PIN Y7  [get_ports {SLOT_ADDR[0]}]
set_property PACKAGE_PIN AB7 [get_ports {SLOT_ADDR[1]}]
set_property PACKAGE_PIN V7  [get_ports {SLOT_ADDR[2]}]
set_property PACKAGE_PIN AA6 [get_ports {SLOT_ADDR[3]}]
set_property PACKAGE_PIN Y6  [get_ports {SLOT_ADDR[4]}]
set_property PACKAGE_PIN AA5 [get_ports {SLOT_ADDR[5]}]
set_property PACKAGE_PIN U6  [get_ports {SLOT_ADDR[6]}]
set_property PACKAGE_PIN W5  [get_ports {SLOT_ADDR[7]}]
set_property PACKAGE_PIN F16 [get_ports {SLOT_ADDR[8]}]
set_property PACKAGE_PIN F15 [get_ports {SLOT_ADDR[9]}]
set_property PACKAGE_PIN F14 [get_ports {SLOT_ADDR[10]}]
set_property PACKAGE_PIN F13 [get_ports {SLOT_ADDR[11]}]
set_property PACKAGE_PIN V19 [get_ports {SLOT_ADDR[12]}]
set_property PACKAGE_PIN U20 [get_ports {SLOT_ADDR[13]}]
set_property PACKAGE_PIN U17 [get_ports {SLOT_ADDR[14]}]
set_property PACKAGE_PIN T18 [get_ports {SLOT_ADDR[15]}]
set_property PACKAGE_PIN W4  [get_ports {SLOT_DATA[0]}]
set_property PACKAGE_PIN V5  [get_ports {SLOT_DATA[1]}]
set_property PACKAGE_PIN W2  [get_ports {SLOT_DATA[2]}]
set_property PACKAGE_PIN U5  [get_ports {SLOT_DATA[3]}]
set_property PACKAGE_PIN T5  [get_ports {SLOT_DATA[4]}]
set_property PACKAGE_PIN U3  [get_ports {SLOT_DATA[5]}]
set_property PACKAGE_PIN T4  [get_ports {SLOT_DATA[6]}]
set_property PACKAGE_PIN T3  [get_ports {SLOT_DATA[7]}]
set_property IOSTANDARD LVCMOS18 [get_ports {SLOT_ADDR[0] SLOT_ADDR[1] SLOT_ADDR[2] SLOT_ADDR[3] SLOT_ADDR[4] SLOT_ADDR[5] SLOT_ADDR[6] SLOT_ADDR[7] SLOT_ADDR[8] SLOT_ADDR[9] SLOT_ADDR[10] SLOT_ADDR[11] SLOT_DATA[*]}]
set_property DRIVE 4 [get_ports {SLOT_ADDR[0] SLOT_ADDR[1] SLOT_ADDR[2] SLOT_ADDR[3] SLOT_ADDR[4] SLOT_ADDR[5] SLOT_ADDR[6] SLOT_ADDR[7] SLOT_ADDR[8] SLOT_ADDR[9] SLOT_ADDR[10] SLOT_ADDR[11] SLOT_DATA[*]}]
set_property IOSTANDARD LVCMOS33 [get_ports {SLOT_ADDR[12] SLOT_ADDR[13] SLOT_ADDR[14] SLOT_ADDR[15]}]
set_property PULLTYPE PULLUP [get_ports {SLOT_ADDR[12] SLOT_ADDR[13] SLOT_ADDR[14] SLOT_ADDR[15]}]

set_property PACKAGE_PIN D15 [get_ports SLOT_ADDRDIR]
set_property PACKAGE_PIN D16 [get_ports SLOT_ADDROEn]
set_property PACKAGE_PIN R6  [get_ports SLOT_DATADIR]
set_property PACKAGE_PIN T6  [get_ports SLOT_DATAOEn]
set_property PACKAGE_PIN W9  [get_ports SLOT_ROMLn]
set_property PACKAGE_PIN Y8  [get_ports SLOT_ROMHn]
set_property PACKAGE_PIN AB8 [get_ports SLOT_IO1n]
set_property PACKAGE_PIN V8  [get_ports SLOT_IO2n]
set_property PACKAGE_PIN W7  [get_ports SLOT_PHI2]
set_property PACKAGE_PIN AA8 [get_ports SLOT_DOTCLK]
set_property PACKAGE_PIN Y9  [get_ports SLOT_BA]
set_property IOSTANDARD LVCMOS18 [get_ports {SLOT_ADDRDIR SLOT_ADDROEn SLOT_DATADIR SLOT_DATAOEn SLOT_ROMLn SLOT_ROMHn SLOT_IO1n SLOT_IO2n SLOT_PHI2 SLOT_DOTCLK SLOT_BA}]
set_property DRIVE 4 [get_ports {SLOT_ADDRDIR SLOT_ADDROEn SLOT_DATADIR SLOT_DATAOEn SLOT_ROMLn SLOT_ROMHn SLOT_IO1n SLOT_IO2n SLOT_PHI2 SLOT_DOTCLK SLOT_BA}]

set_property PACKAGE_PIN V18 [get_ports SLOT_RWn]
set_property PACKAGE_PIN V17 [get_ports SLOT_GAMEn]
set_property PACKAGE_PIN W17 [get_ports SLOT_EXROMn]
set_property PACKAGE_PIN W19 [get_ports SLOT_DMAn]
set_property PACKAGE_PIN U18 [get_ports SLOT_IRQn]
set_property PACKAGE_PIN R17 [get_ports SLOT_NMIn]
set_property PACKAGE_PIN P20 [get_ports SLOT_RST_I]
set_property PACKAGE_PIN R21 [get_ports CIA_RSTn]
set_property IOSTANDARD LVCMOS33 [get_ports {SLOT_RWn SLOT_GAMEn SLOT_EXROMn SLOT_DMAn SLOT_IRQn SLOT_NMIn SLOT_RST_I CIA_RSTn}]
set_property PULLTYPE PULLUP [get_ports {SLOT_RWn SLOT_GAMEn SLOT_EXROMn SLOT_DMAn SLOT_IRQn SLOT_NMIn CIA_RSTn}]
set_property PULLTYPE PULLDOWN [get_ports SLOT_RST_I]

set_property PACKAGE_PIN M18 [get_ports SLOT_RST_O]
set_property PACKAGE_PIN M20 [get_ports SLOT_SID1n]
set_property PACKAGE_PIN N20 [get_ports SLOT_SID2n]
set_property PACKAGE_PIN K17 [get_ports SLOT_SID_ADDR]
set_property PACKAGE_PIN L15 [get_ports SLOT_PWR]
set_property IOSTANDARD LVCMOS33 [get_ports {SLOT_RST_O SLOT_SID1n SLOT_SID2n SLOT_SID_ADDR SLOT_PWR}]
set_property DRIVE 4 [get_ports {SLOT_RST_O SLOT_SID1n SLOT_SID2n SLOT_SID_ADDR}]

# C64-side buffers and their supply (Gideon's XDC; held high as his design does)
set_property PACKAGE_PIN H13 [get_ports CIA_BUFFER_EN]
set_property PACKAGE_PIN G18 [get_ports CIA_PWR_EN]
set_property IOSTANDARD LVCMOS33 [get_ports {CIA_BUFFER_EN CIA_PWR_EN}]

# Joysticks and RESTORE (bank 14, 3.3 V); pull-ups as in Gideon's XDC.
set_property PACKAGE_PIN W21  [get_ports {KBJOY_2_JOY[0]}]
set_property PACKAGE_PIN U21  [get_ports {KBJOY_2_JOY[1]}]
set_property PACKAGE_PIN U22  [get_ports {KBJOY_2_JOY[2]}]
set_property PACKAGE_PIN T21  [get_ports {KBJOY_2_JOY[3]}]
set_property PACKAGE_PIN V22  [get_ports {KBJOY_2_JOY[4]}]
set_property PACKAGE_PIN AB22 [get_ports {KBJOY_1_JOY[0]}]
set_property PACKAGE_PIN Y22  [get_ports {KBJOY_1_JOY[1]}]
set_property PACKAGE_PIN Y21  [get_ports {KBJOY_1_JOY[2]}]
set_property PACKAGE_PIN W22  [get_ports {KBJOY_1_JOY[3]}]
set_property PACKAGE_PIN AA21 [get_ports {KBJOY_1_JOY[4]}]
set_property PACKAGE_PIN M16  [get_ports KBJOY_RESTORE]
set_property IOSTANDARD LVCMOS33 [get_ports {KBJOY_1_JOY[*] KBJOY_2_JOY[*] KBJOY_RESTORE}]
set_property PULLTYPE PULLUP [get_ports {KBJOY_1_JOY[*] KBJOY_2_JOY[*] KBJOY_RESTORE}]

# I2S to the NAU88C22 (bank 34, 1.8 V)
set_property PACKAGE_PIN AB2 [get_ports AUDIO_MCLK]
set_property PACKAGE_PIN AA3 [get_ports AUDIO_BCLK]
set_property PACKAGE_PIN AA4 [get_ports AUDIO_LRCLK]
set_property PACKAGE_PIN Y3  [get_ports AUDIO_SDI]
set_property IOSTANDARD LVCMOS18 [get_ports {AUDIO_MCLK AUDIO_BCLK AUDIO_LRCLK AUDIO_SDI}]
set_property DRIVE 4 [get_ports {AUDIO_MCLK AUDIO_BCLK AUDIO_LRCLK AUDIO_SDI}]

# RISC-V console UART (3.3 V)
set_property PACKAGE_PIN N15 [get_ports UART_TXD]
set_property PACKAGE_PIN P15 [get_ports UART_RXD]
set_property IOSTANDARD LVCMOS33 [get_ports {UART_TXD UART_RXD}]
set_property PULLTYPE PULLUP [get_ports UART_RXD]

# The DDR2 pins (and A13, F4, held low) are ddr2_pins.xdc: Gideon's HSTL standards, driven by
# ddr2_lean; the clocks of its PLL (clk300 for the SERDES, ui_clk) are generated from clk200
# (mmcm_ddr CLKOUT1), which the clock group below already names with its generated clocks.

set_property CFGBVS VCCO [current_design]
set_property CONFIG_VOLTAGE 3.3 [current_design]
set_property BITSTREAM.GENERAL.COMPRESS TRUE [current_design]
# Configuration from the SPI flash as Gideon's C64U bitstream (host/c64u.xdc):
# x1, CCLK 33 MHz. The host's identity goes into the bitstream's AXSS register; the
# boot ROM starts the flash app only when the bitstream in flash carries the
# identity of the running one (T15 §4.2; trx_io BUILD).
#
# T84 D5b (Alex, 2026-10-14): that identity is the HOST KEY, the first 8 hex digits of the key the
# build cache uses to decide "same host" (ci/lib.sh host_key, exported as TRX_HOST_KEY8 by
# ci/bitstream_layers.sh). It is derived from the host's own sources, so the same sources always
# give the same number - a cache hit that copies an older image keeps it, which a TIMESTAMP would
# not. USR_ACCESSE2 (top.v) hands it to the design and flash_axss() (bootrom.c) reads it out of the
# flash image, so a .cor can name the host it was routed against and the boot app can refuse a
# partial that belongs to another one. The key is applied by trx_axss (tools/vivado_lib.tcl) right
# before every full write_bitstream, NOT here: an XDC is restricted Tcl, an "if" in it is a CRITICAL
# WARNING ([Designutils 20-1307]) and the skipped block left run 26 with no AXSS word at all. The
# TIMESTAMP below is what a hand build without TRX_HOST_KEY8 keeps.
set_property CONFIG_MODE SPIx1 [current_design]
set_property BITSTREAM.CONFIG.CONFIGRATE 16 [current_design]
set_property BITSTREAM.CONFIG.USR_ACCESS TIMESTAMP [current_design]

# The clock domains only meet in synchronizers and in the scaler's dual-clock
# line ring, so time none of the paths between them. mmcm_sys's two outputs
# (dot4x and the SID engine's 2 x dot4x) are one domain, timed together.
create_clock -name jtag_tck -period 50 [get_pins -hier -filter {NAME =~ dbg/bscan/TCK}]
set_clock_groups -asynchronous \
  -group [get_clocks jtag_tck] \
  -group [get_clocks rmii_clock] \
  -group [get_clocks -of_objects [get_pins {mmcm_sys/CLKOUT0 mmcm_sys/CLKOUT1}]] \
  -group [get_clocks -of_objects [get_pins mmcm_ref/CLKOUT0]] \
  -group [get_clocks -include_generated_clocks -of_objects [get_pins mmcm_ddr/CLKOUT1]] \
  -group [get_clocks -of_objects [get_pins mmcm_ddr/CLKOUT0]] \
  -group [get_clocks -of_objects [get_pins mmcm_ddr/CLKOUT2]] \
  -group [get_clocks -include_generated_clocks hdmi_refclk] \
  -group [get_clocks -include_generated_clocks -of_objects [get_pins -hier -filter {NAME =~ *gtp/*gtpe2_i/TXOUTCLK}]]

# T40: mmcm_sys runs at up to 32.765 MHz (PAL-60, through its DRP) while its
# static setting, which is all Vivado sees, is PAL's 31.528 MHz: time the
# sys domain for the fastest mode (30.52 ns, sid2x 15.26 ns).
set sysclk [get_clocks -of_objects [get_pins mmcm_sys/CLKOUT0]]
set sid2xclk [get_clocks -of_objects [get_pins mmcm_sys/CLKOUT1]]
set_clock_uncertainty -setup 1.2 -from $sysclk -to $sysclk
set_clock_uncertainty -setup 0.6 -from $sid2xclk -to $sid2xclk
set_clock_uncertainty -setup 0.6 -from $sysclk -to $sid2xclk
set_clock_uncertainty -setup 0.6 -from $sid2xclk -to $sysclk

# Ethernet PHY (KSZ8081, bank 16, 1.8 V). RMII timing as in Gideon's XDC
# (host/c64u.xdc): the PHY's clock-to-out 7..13 ns, its setup 4 ns.
set_property PACKAGE_PIN A14 [get_ports RMII_CRS_DV]
set_property PACKAGE_PIN C13 [get_ports {RMII_RX_DATA[0]}]
set_property PACKAGE_PIN A13 [get_ports {RMII_RX_DATA[1]}]
set_property PACKAGE_PIN B15 [get_ports RMII_TX_EN]
set_property PACKAGE_PIN A16 [get_ports {RMII_TX_DATA[0]}]
set_property PACKAGE_PIN B16 [get_ports {RMII_TX_DATA[1]}]
set_property PACKAGE_PIN B13 [get_ports MDIO_CLK]
set_property PACKAGE_PIN D14 [get_ports MDIO_DATA]
set_property IOSTANDARD LVCMOS18 [get_ports {RMII_CRS_DV RMII_RX_DATA[*] RMII_TX_EN RMII_TX_DATA[*] MDIO_CLK MDIO_DATA}]
set_property PULLTYPE PULLUP [get_ports MDIO_DATA]
set_input_delay -clock rmii_clock -max 13.000 [get_ports {RMII_RX_DATA[*] RMII_CRS_DV}]
set_input_delay -clock rmii_clock -min 7.000 [get_ports {RMII_RX_DATA[*] RMII_CRS_DV}]
set_output_delay -clock rmii_clock -max 4.000 [get_ports {RMII_TX_DATA[*] RMII_TX_EN}]
set_output_delay -clock rmii_clock -min -2.000 [get_ports {RMII_TX_DATA[*] RMII_TX_EN}]

# keyboard controller interrupt (Gideon's XDC: V9, 1.8 V)
set_property PACKAGE_PIN V9 [get_ports KBJOY_INT]
set_property IOSTANDARD LVCMOS18 [get_ports KBJOY_INT]

# serial line from the keyboard (Bling) board (Gideon's XDC: J16, pull-up)
set_property PACKAGE_PIN J16 [get_ports BLING_KEYB]
set_property IOSTANDARD LVCMOS33 [get_ports BLING_KEYB]
set_property PULLTYPE PULLUP [get_ports BLING_KEYB]

# built-in speaker amplifier: I2S data on the codec's BCLK/LRCK (Gideon's XDC: AB6, 1.8 V)
set_property PACKAGE_PIN AB6 [get_ports SPEAKER_DATA]
set_property IOSTANDARD LVCMOS18 [get_ports SPEAKER_DATA]
set_property DRIVE 4 [get_ports SPEAKER_DATA]

# ESP32 UART (WiFi / control module), 3.3 V. WIFI_BOOT (L13, ESP32 GPIO0) and
# WIFI_DIS (M13, ESP32 EN) are driven by esp_ctl.v only after the app arms it; until then high-Z.
set_property PACKAGE_PIN P14 [get_ports WIFI_TXD]
set_property PACKAGE_PIN N13 [get_ports WIFI_RXD]
set_property PACKAGE_PIN N14 [get_ports WIFI_RTS]
set_property PACKAGE_PIN L14 [get_ports WIFI_CTS]
set_property IOSTANDARD LVCMOS33 [get_ports {WIFI_TXD WIFI_RXD WIFI_RTS WIFI_CTS}]
set_property PULLTYPE PULLUP [get_ports {WIFI_RXD WIFI_CTS}]
# T195: the board's three buttons, pulled up: a press is low (c64u.xdc:206 LVCMOS33, 452-454 pins, 515-517 PULLUP)
set_property PACKAGE_PIN P17 [get_ports {UI_BUTTON[0]}]
set_property PACKAGE_PIN R18 [get_ports {UI_BUTTON[1]}]
set_property PACKAGE_PIN N18 [get_ports {UI_BUTTON[2]}]
set_property IOSTANDARD LVCMOS33 [get_ports {UI_BUTTON[*]}]
set_property PULLTYPE PULLUP [get_ports {UI_BUTTON[*]}]
set_property PACKAGE_PIN L13 [get_ports WIFI_BOOT]
set_property PACKAGE_PIN M13 [get_ports WIFI_DIS]
set_property IOSTANDARD LVCMOS33 [get_ports {WIFI_BOOT WIFI_DIS}]

# SD card (SPI mode) and the configuration flash (Gideon's XDC); the flash
# SCK is the CCLK pin, driven through STARTUPE2
set_property PACKAGE_PIN AA18 [get_ports SD_SSn]
set_property PACKAGE_PIN AB18 [get_ports SD_CLK]
set_property PACKAGE_PIN AA19 [get_ports SD_MOSI]
set_property PACKAGE_PIN T20  [get_ports SD_MISO]
set_property PACKAGE_PIN L16  [get_ports SD_CARDDETn]
set_property IOSTANDARD LVCMOS33 [get_ports {SD_SSn SD_CLK SD_MOSI SD_MISO SD_CARDDETn}]
set_property PULLTYPE PULLUP [get_ports {SD_MISO SD_CARDDETn}]
set_property PACKAGE_PIN T19 [get_ports FLASH_CSn]
set_property PACKAGE_PIN P22 [get_ports FLASH_MOSI]
set_property PACKAGE_PIN R22 [get_ports FLASH_MISO]
set_property IOSTANDARD LVCMOS33 [get_ports {FLASH_CSn FLASH_MOSI FLASH_MISO}]

# SHIFT LOCK LED on the keyboard board (WS2812 chain, bling_io.v) and the case LED strip
# header (Gideon's XDC); the strip pins are held low
set_property PACKAGE_PIN R16 [get_ports BLING_LED]
set_property PACKAGE_PIN M15 [get_ports LED_DATA]
set_property PACKAGE_PIN R14 [get_ports LED_CLOCK]
set_property IOSTANDARD LVCMOS33 [get_ports {BLING_LED LED_DATA LED_CLOCK}]

# Serial bus (IEC) port (Gideon's XDC, 3.3 V): _O = 1 pulls the line low,
# _I = 1 while it is low (iec_bus.v)
set_property PACKAGE_PIN J14 [get_ports IEC_ATN_I]
set_property PACKAGE_PIN G15 [get_ports IEC_CLOCK_I]
set_property PACKAGE_PIN J15 [get_ports IEC_DATA_I]
set_property PACKAGE_PIN G16 [get_ports IEC_ATN_O]
set_property PACKAGE_PIN L19 [get_ports IEC_CLOCK_O]
set_property PACKAGE_PIN L18 [get_ports IEC_DATA_O]
set_property PACKAGE_PIN H14 [get_ports IEC_RESET_O]
set_property PACKAGE_PIN J17 [get_ports IEC_SRQ_O]
# T62 G19: the bus RESET line in (1 while low; idle 0 in the boundary scan of Gideon's firmware) and the Ethernet LED (low = on)
set_property PACKAGE_PIN K13 [get_ports IEC_RESET_I]
set_property PACKAGE_PIN G13 [get_ports ETH_LED_YELn]
set_property IOSTANDARD LVCMOS33 [get_ports {IEC_RESET_I ETH_LED_YELn}]
set_property IOSTANDARD LVCMOS33 [get_ports {IEC_ATN_I IEC_CLOCK_I IEC_DATA_I IEC_ATN_O IEC_CLOCK_O IEC_DATA_O IEC_RESET_O IEC_SRQ_O}]

# T103: pins only for the cores. Package pins, standards and pull-ups from Gideon's XDC (c64u_gideon_issue615.xdc /
# c64u.xdc:141,213-217,317,408,418-421,470-474,482,519-520); direction and open-drain as his U2 top handles the
# cassette pins (CAS_SENSE pulled low or released, READ/WRITE tri-state). What PADDLE_DIS and PADDLE_ADJ drive is a
# hypothesis (T47 §4.6), so they are released (high-Z) unless a core enables them.
set_property PACKAGE_PIN AB21 [get_ports C2N_SENSE]
set_property PACKAGE_PIN Y18  [get_ports C2N_READ]
set_property PACKAGE_PIN Y19  [get_ports C2N_WRITE]
set_property PACKAGE_PIN H20  [get_ports C2N_MOTOR]
set_property IOSTANDARD LVCMOS33 [get_ports {C2N_SENSE C2N_READ C2N_WRITE C2N_MOTOR}]
set_property PULLTYPE PULLUP [get_ports {C2N_READ C2N_WRITE}]
set_property PACKAGE_PIN E14 [get_ports {PADDLE_CMP[0]}]
set_property PACKAGE_PIN L6  [get_ports {PADDLE_CMP[1]}]
set_property PACKAGE_PIN N5  [get_ports {PADDLE_CMP[2]}]
set_property PACKAGE_PIN L4  [get_ports {PADDLE_CMP[3]}]
set_property IOSTANDARD LVCMOS18 [get_ports {PADDLE_CMP[*]}]
set_property PACKAGE_PIN N22  [get_ports PADDLE_DIS]
set_property PACKAGE_PIN AA20 [get_ports PADDLE_ADJ]
set_property IOSTANDARD LVCMOS33 [get_ports {PADDLE_DIS PADDLE_ADJ}]
set_property PACKAGE_PIN AB3 [get_ports AUDIO_SDO]
set_property IOSTANDARD LVCMOS18 [get_ports AUDIO_SDO]

# T265 P4: the A/V port's DAC (THS8136) pins, RGB out of the host. Balls, standard and drive as host/c64u.xdc (Gideon, issue #615) and
# cores/c128/mono/avid_pins.xdc; the words are IOB flops in sys_clk, AVID_CLK the inverted sys_clk (top.v).
set_property IOSTANDARD LVCMOS18 [get_ports AVID_*]
set_property DRIVE 4 [get_ports AVID_*]
set_property PACKAGE_PIN C19 [get_ports {AVID_B[9]}]
set_property PACKAGE_PIN D20 [get_ports {AVID_B[8]}]
set_property PACKAGE_PIN D19 [get_ports {AVID_B[7]}]
set_property PACKAGE_PIN E19 [get_ports {AVID_B[6]}]
set_property PACKAGE_PIN C18 [get_ports {AVID_B[5]}]
set_property PACKAGE_PIN F19 [get_ports {AVID_B[4]}]
set_property PACKAGE_PIN C17 [get_ports {AVID_B[3]}]
set_property PACKAGE_PIN F20 [get_ports {AVID_B[2]}]
set_property PACKAGE_PIN C20 [get_ports {AVID_G[9]}]
set_property PACKAGE_PIN D22 [get_ports {AVID_G[8]}]
set_property PACKAGE_PIN D21 [get_ports {AVID_G[7]}]
set_property PACKAGE_PIN E22 [get_ports {AVID_G[6]}]
set_property PACKAGE_PIN E21 [get_ports {AVID_G[5]}]
set_property PACKAGE_PIN F21 [get_ports {AVID_G[4]}]
set_property PACKAGE_PIN G22 [get_ports {AVID_G[3]}]
set_property PACKAGE_PIN G21 [get_ports {AVID_G[2]}]
set_property PACKAGE_PIN C22 [get_ports AVID_CLK]
set_property PACKAGE_PIN B22 [get_ports {AVID_R[9]}]
set_property PACKAGE_PIN B21 [get_ports {AVID_R[8]}]
set_property PACKAGE_PIN A21 [get_ports {AVID_R[7]}]
set_property PACKAGE_PIN B20 [get_ports {AVID_R[6]}]
set_property PACKAGE_PIN A20 [get_ports {AVID_R[5]}]
set_property PACKAGE_PIN A19 [get_ports {AVID_R[4]}]
set_property PACKAGE_PIN B18 [get_ports {AVID_R[3]}]
set_property PACKAGE_PIN A18 [get_ports {AVID_R[2]}]
set_property PACKAGE_PIN E18 [get_ports AVID_FB]
set_property PACKAGE_PIN F18 [get_ports AVID_SYNCn]
