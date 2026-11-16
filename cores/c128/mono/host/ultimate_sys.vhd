--------------------------------------------------------------------------------
-- ultimate_sys: Gideon's RISC-V (rvlite) with what the U64-II "ultimate" app
-- needs to get through its boot (docs/ultimate-app-hw-spec.md, step A).
--
--   - rvlite_wrapper, ITU (timers, IRQs, capabilities, console UART) and
--     u2p_io from vendor/gideon, unchanged.
--   - The memory bus leaves this entity flattened (Verilog cannot take VHDL
--     records); top.v bridges it onto the DDR2 controller (cpu_mem_bridge.v).
--   - Everything else the app touches is a stub (spec 4.7, 4.9, 6.1): register
--     pages that read back what was written, with the constants the boot polls
--     on. Every IO access is acked; what is not mapped reads 0 (spec 3b).
--   - Every byte written to the console UART also comes out on con_valid /
--     con_data, so the boot log can be read over JTAG.
--   - Step B: Gideon's RMII MAC (ethernet_interface) at 0x10060800, its DMA
--     next to the CPU on one memory bus (priority arbiter, the MAC first);
--     MDIO bit-banged through u2p_io. The byte streams go out to top.v, which
--     puts the RMII transceiver next to them (the UDP streams are the core's, T103).
--   - Step C: the C64 registers the app drives (spec 6.2) on our core: reset,
--     pause, the cartridge registers (CART_TYPE, kill/force, ACTIVE; the
--     cartridge itself is cart_slot in the C64 core),
--     and the DMA window 0x10050000 through a handshake with top.v (sys_clk).
--     While C64_STOP is set (and not C64_DMA_MEMONLY, 0x10180003) a byte is
--     one C64 bus cycle through the PLA (ROMs, I/O, colour RAM, Ultimax from
--     C64_MODE); otherwise it goes to the C64 RAM directly. $D000-$DFFF
--     is always a bus cycle unless C64_DMA_MEMONLY (G06): with the C64 running, c64_core
--     holds the 6510 at a read cycle for the byte (cycle steal), with the machine stopped
--     (C64_STOP, or a debugger stop, trx_dbg.v, T12 §14.8) the cycle is the window's own;
--     reads of $D400-$D7FF are $FF (no SID) unless MEMONLY.
--   - The ROM apertures (U64_ROMS_BASE 0x10188000: BASIC +0x0000, KERNAL
--     +0x2000; U64_CHARROM_BASE 0x1018C000) go the same handshake way with
--     dma_rom set, into the C64's ROM block (c64_mem.v through dma_win.v),
--     byte for byte as the app writes them (linear), readable too. The
--     bitstream holds no ROM: the app writes what it loads from /roms
--     (C64::init_system_roms) while the C64 is held in reset, which it is
--     from the FPGA's (and the RISC-V's) reset on, as cart_slot_registers
--     with g_cartreset_init = '1' does it, until the app releases C64_MODE.
--   - The WiFi DMA UART (uart_dma) at 0x10060900 on the ESP32's UART: the
--     app's link to the control module (identify, power, the rocker's
--     Menu/Reset/Freeze events). Its boot/enable/route outputs go nowhere:
--     the ESP32 runs the board's power and is never reset or rerouted.
--   - LEDs and the keyboard board's bytes, 0x10100600-0x101009FF, are in
--     bling_io.v (top.v; the app's LED registers read 0, OPT-bling); U64_CASELED_SELECT and the ITU busy bits go out
--     for case_leds.v.
--   - The app's I2C master (0x10100700) is a byte register port to top.v
--     (app_i2c.v), which shares the buses with our own I2C logic.
--   - USB: Gideon's usb_host_nano at 0x10080000 in the PHY's 60 MHz clock, its
--     DMA on the memory bus after the WiFi UART's; ULPI pins in usb_phy.v.
--   - Our IO block 0x101A0000-0x101AFFFF (trx_io.v in top.v; ID, interrupt,
--     the cartridge's flash responder and write counter) and Gideon's GMod2
--     EEPROM window at 0x1004C000 (CAPAB_EEPROM), one port (cw_*) with addr
--     b16 = the EEPROM window; its interrupt is ITU high interrupt 2 (drive
--     B's in ultimate_logic_32; we have no drive B).
--   - The Ultimate Audio sampler (T25.6, CAPAB_SAMPLER = bit 21; docs/specs/T25.6-sampler.md): its trx_sampler.v is in
--     c64_core (the C64's $DF20-$DFFF, the Sampler L/R mixer inputs); here only what the app drives: the byte
--     window SAMPLER_BASE 0x10048000-0x100480FF (region r_cw, trx_io page 8), C64_SAMPLER_ENABLE (cart register
--     +0E) and the mixer bytes 8..11 (sid_gains2).
--   - The UCI (T28) is not here any more (T103): it is the core's, behind the cw_* window; the old
--     window 0x10044000-0x10045FFF acks with 0. The ITU word g_capabilities is unchanged.
--   - Drive A (drive_unit: Gideon's c1541_drive) at 0x10020000, its memory
--     on a DDR2 port of its own (drv_mem_*, mig_arb client 2 in top.v); its
--     serial bus pins go to iec_bus.v in top.v. Drive B and the IEC processor
--     (0x10028000) are not there.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.mem_bus_pkg.all;
use work.io_bus_pkg.all;

entity ultimate_sys is
generic (
    -- spec 5: ULTIMATE64, type 2 (XC7A50T), CARTRIDGE, HARDWARE_IEC, DRIVE_1541_1,
    -- ETH_RMII, USB_HOST2, EEPROM (GMod2, trx_io.v), COMMAND_INTF (the UCI)
    g_capabilities  : std_logic_vector(31 downto 0) := X"25C40222";
    -- T164 console flat builds (NO_NET in top.v): 0 leaves out the Ethernet interface; its I/O region (0x060800..) still acks with zeros
    -- so a read never hangs the RISC-V, its DMA port and interrupts are idle. The WiFi UART stays (it carries the ESP32 button events)
    g_net           : integer := 1 );
port (
    clock           : in  std_logic;            -- 100 MHz: the app's timers assume it
    reset           : in  std_logic;

    mem_request     : out std_logic;
    mem_tag         : out std_logic_vector(7 downto 0);
    mem_read_writen : out std_logic;
    mem_address     : out std_logic_vector(25 downto 0);
    mem_byte_en     : out std_logic_vector(3 downto 0);
    mem_wdata       : out std_logic_vector(31 downto 0);
    mem_rdata       : in  std_logic_vector(31 downto 0);
    mem_rack        : in  std_logic;
    mem_rack_tag    : in  std_logic_vector(7 downto 0);
    mem_dack_tag    : in  std_logic_vector(7 downto 0);

    ddr_ready       : in  std_logic;            -- DDR2 controller calibrated (synchronized)

    uart_txd        : out std_logic;
    uart_rxd        : in  std_logic;

    con_valid       : out std_logic;            -- a byte went to the console UART
    con_data        : out std_logic_vector(7 downto 0);

    -- Ethernet: frames as byte streams in the 50 MHz RMII clock (the
    -- transceiver is in top.v), MDIO bit-banged
    eth_clock       : in  std_logic;
    eth_reset       : in  std_logic;            -- sync to eth_clock
    eth_tx_data     : out std_logic_vector(7 downto 0);
    eth_tx_eof      : out std_logic;
    eth_tx_valid    : out std_logic;
    eth_tx_ready    : in  std_logic;
    eth_rx_data     : in  std_logic_vector(7 downto 0);
    eth_rx_sof      : in  std_logic;
    eth_rx_eof      : in  std_logic;
    eth_rx_valid    : in  std_logic;
    mdc             : out std_logic;
    mdio_i          : in  std_logic;
    mdio_o          : out std_logic;            -- 0 = pull low, 1 = release

    -- ESP32 UART (5 Mbit/s, RTS/CTS active low)
    wifi_txd        : out std_logic;
    wifi_rxd        : in  std_logic;
    wifi_rts        : out std_logic;
    wifi_cts        : in  std_logic;

    -- SD card (SPI mode) and the configuration flash (its SCK is CCLK, through
    -- STARTUPE2 in top.v); writes pass i_flash_gate (flash_gate.vhd)
    sd_ssn          : out std_logic;
    sd_clk          : out std_logic;
    sd_mosi         : out std_logic;
    sd_miso         : in  std_logic;
    sd_detectn      : in  std_logic;            -- synchronized
    flash_csn       : out std_logic;
    flash_sck       : out std_logic;
    flash_mosi      : out std_logic;
    flash_miso      : in  std_logic;

    -- C64 keyboard matrix, bit col*8 + row = key down (synchronized), for the
    -- menu's own scan through KEYB_COL/KEYB_ROW; the physical joysticks (port 2 in
    -- 9:5, port 1 in 4:0; up, down, left, right, fire; 1 = pressed, synchronized)
    -- for KEYB_JOY
    keys            : in  std_logic_vector(63 downto 0);
    joys            : in  std_logic_vector(9 downto 0);
    -- T62 G04: RESTORE pressed (1), in this clock; U64_RESTORE_REG (0x10100402) reads it
    restore_pin     : in  std_logic := '0';
    -- T62 G09: C64_SCANLINES (0x10180000) b0, this clock (vic_scaler synchronises it)
    scanlines       : out std_logic;
    -- T62 G17: a C64 write to $D7FF, from the C64 clock: toggle, and the byte (held until the next write)
    c64_dbg_tgl     : in  std_logic := '0';
    c64_dbg_data    : in  std_logic_vector(7 downto 0) := X"00";

    -- T38: what the app puts on the C64's keyboard and joysticks (1 = pressed),
    -- latched from MATRIX_KEYB 0x10100300 (bytes 0-7 bit col*8 + row, byte 8
    -- reset, 9 RESTORE, 10 freeze) and C64_JOY1/2_SWOUT 0x10180030/31 (b4:0
    -- up, down, left, right, fire, written low = pressed); top.v merges them
    -- with the physical ones (app_input.v)
    app_keys        : out std_logic_vector(63 downto 0);
    app_reset       : out std_logic;
    app_restore     : out std_logic;
    app_freeze      : out std_logic;
    app_joy1        : out std_logic_vector(4 downto 0);
    app_joy2        : out std_logic_vector(4 downto 0);
    -- T39: the C64 is in reset (level, any clock): ITU irq_in(7) on its rising
    -- edge, C64_CLOCK_DETECT bit 4
    c64_reset_sense : in  std_logic;

    -- HDMI: hot plug detect (pin, not synchronized) and the DDC enable the app
    -- sets around its EDID read (U64_HDMI_REG)
    hdmi_hpd        : in  std_logic;
    hdmi_ddc_en     : out std_logic;

    -- C64 (spec 6.2), levels in this clock; top.v takes them into the C64's
    c64_reset_req   : out std_logic;
    c64_pause_req   : out std_logic;
    c64_ultimax_req : out std_logic;            -- C64_MODE b1: GAME low, EXROM high
    -- expansion port (T16 §13.2), synchronized to clock: b0 GAME pin, b1 EXROM pin
    -- (1 = high), b2 the port's RESET is sensed low
    slot_pins       : in  std_logic_vector(2 downto 0) := "011";
    -- C64_BUS_INTERNAL / C64_BUS_EXTERNAL (0x1018002B/2C): b0 IO1, b1 IO2, b2 ROM,
    -- b3 IRQ; which resources the real port serves (T16 §13.2)
    c64_bus_int     : out std_logic_vector(3 downto 0);
    c64_bus_ext     : out std_logic_vector(3 downto 0);
    -- C64_VIDEOFORMAT (0x10180001, T40): the app's mode_bits | RGB; reset PAL
    video_fmt       : out std_logic_vector(7 downto 0);
    -- T29 turbo (0x10180002 TURBOREGS_EN, 0x1018002D SPEED_PREFER, 0x1018002E
    -- SPEED_UPDATE: the toggle follows every write of it); Gideon's offsets
    turbo_en        : out std_logic_vector(7 downto 0);
    turbo_prefer    : out std_logic_vector(7 downto 0);
    turbo_upd_tgl   : out std_logic;
    cart_type       : out std_logic_vector(7 downto 0);  -- CART_TYPE: b4:0 logic, b7:5 variant
    reu_enable      : out std_logic;                     -- C64_REU_ENABLE (+08 b0, T60); reset 0
    reu_size        : out std_logic_vector(2 downto 0);  -- C64_REU_SIZE (+09 b2:0): 0 = 128 KB ... 7 = 16 MB; reset 7
    cart_kill_tgl   : out std_logic;            -- toggles with KILL (+06 b0)
    cart_force_tgl  : out std_logic;            -- toggles with the update strobe (+06 b1)
    cart_active     : in  std_logic;            -- from the C64 clock
    -- DMA into the C64 RAM, one byte per request toggle, answered by done
    dma_req_tgl     : out std_logic;
    dma_addr        : out std_logic_vector(15 downto 0);
    dma_we          : out std_logic;
    dma_wdata       : out std_logic_vector(7 downto 0);
    dma_bus         : out std_logic;            -- as a C64 bus cycle (the C64 is stopped)
    dma_rom         : out std_logic;            -- into the ROM block: dma_addr = its byte offset
    dma_done_tgl    : in  std_logic;
    dma_rdata       : in  std_logic_vector(7 downto 0);   -- stable when done toggles
    dbg_stop        : in  std_logic := '0';     -- a debugger stop is in force (trx_dbg.v; unused since G06: the D page is a bus cycle whether stopped or not)

    -- the app's I2C master 0x10100700-0F (app_i2c.v in top.v); read data the
    -- clock after i2c_re
    i2c_we          : out std_logic;
    i2c_re          : out std_logic;
    i2c_addr        : out std_logic_vector(3 downto 0);
    i2c_wdata       : out std_logic_vector(7 downto 0);
    i2c_rdata       : in  std_logic_vector(7 downto 0);

    -- ESP32 boot control 0x10060910-1F (esp_ctl.v in top.v, T152 S8); read data the clock after esp_re.
    -- wifi_boot / wifi_enable are uart_dma's flow control bits 4 / 5 (the app's ModuleCtrl)
    wifi_boot       : out std_logic;
    wifi_enable     : out std_logic;
    esp_we          : out std_logic;
    -- T195: the board's buttons UI_BUTTON[2:0], debounced (ui_buttons.v in top.v), high = pressed: left, middle = Menu, right = Reset
    ui_btn          : in  std_logic_vector(2 downto 0) := "000";
    esp_re          : out std_logic;
    esp_addr        : out std_logic_vector(3 downto 0);
    esp_wdata       : out std_logic_vector(7 downto 0);
    esp_rdata       : in  std_logic_vector(7 downto 0) := X"00";

    -- overlay 0x10140000-0x10147FFF (overlay.v): registers, screen and colour
    -- RAM, HDMI palette; read data one clock after the address
    ovl_we          : out std_logic;
    ovl_addr        : out std_logic_vector(14 downto 0);
    ovl_wdata       : out std_logic_vector(7 downto 0);
    ovl_rdata       : in  std_logic_vector(7 downto 0);

    -- C64_PALETTE 0x10180800-0x10180FFF (T59, pal_ram.v in vic_scaler.v and
    -- c64_text.v): entry n's R, G, B at 4n+0..2 (pal_addr 5:0); write only,
    -- the YUV table at +0x400 is accepted and dropped (no analog video)
    pal_we          : out std_logic;
    pal_addr        : out std_logic_vector(5 downto 0);
    pal_wdata       : out std_logic_vector(7 downto 0);

    -- 0x10100600-0x101009FF (bling_io.v): read data one clock after the
    -- address; bl_irq is ITU high interrupt 4
    bl_we           : out std_logic;
    bl_addr         : out std_logic_vector(11 downto 0);
    bl_wdata        : out std_logic_vector(7 downto 0);
    bl_rdata        : in  std_logic_vector(7 downto 0);
    bl_irq          : in  std_logic;
    -- our IO block 0x101A0000-0x101AFFFF and the EEPROM window
    -- 0x1004C000-0x1004CFFF (cw_addr b16 = 1), trx_io.v: read data with
    -- cw_ack (one clock after the address, later for the pages in the C64's
    -- clock); cw_irq is ITU high interrupt 2
    cw_we           : out std_logic;
    cw_re           : out std_logic;
    cw_addr         : out std_logic_vector(16 downto 0);
    cw_wdata        : out std_logic_vector(7 downto 0);
    cw_rdata        : in  std_logic_vector(7 downto 0);
    cw_ack          : in  std_logic;
    cw_irq          : in  std_logic;
    -- T79.1 decoupler: 1 while the core is being swapped (top.v). The core's windows answer at once
    -- with ack and 0, no DMA byte is started, and the host-owned core state returns to power-up.
    core_decouple   : in  std_logic := '0';
    -- case LEDs: U64_CASELED_SELECT once the app has written it, ITU busy
    caseled_sel     : out std_logic_vector(7 downto 0);
    caseled_set     : out std_logic;
    busy_any        : out std_logic;            -- USB, SD or printer busy
    busy_usb        : out std_logic;
    -- built-in speaker as the app sets it (U2PIO_SPEAKER_EN: b0 on, b4:1 volume)
    speaker_en      : out std_logic;
    -- the app's mixers (U64_AUDIO_MIXER 0x10100500, U64_SPEAKER_MIXER
    -- 0x10100540, write only: per source a right and a left gain, 0x80 = 0 dB).
    -- mix_l/mix_r: UltiSID 1 or socket 1, whichever is louder (no longer used by
    -- top.v: the UltiSID gains go to the SID mixer through sid_gains).
    mix_l           : out std_logic_vector(7 downto 0);
    mix_r           : out std_logic_vector(7 downto 0);
    mix_spk         : out std_logic_vector(7 downto 0);
    speaker_vol     : out std_logic_vector(3 downto 0);
    -- T160 master volume (0x10100550, byte, write only): the gain on every core's
    -- audio in top.v (trx_gain.v), 0x80 = unity (reset value), 0 silent, 0xFF +6 dB.
    -- Not the mixer above: that one also drives the SID mixer and would count twice.
    master_vol      : out std_logic_vector(7 downto 0);
    -- T189 tone preset (0x10100551, byte, write only): 0 Flat (reset value), 1 Bass boost, 2 Treble boost,
    -- anything else Flat; the filter in front of the gain (trx_tone.v).
    tone_sel        : out std_logic_vector(7 downto 0);
    -- T25.1 SID engine (T25.1): the app's UltiSID registers in the
    -- core config page (C64_EMUSIDn_BASE/MASK +0x0A/+0x0B/+0x0E/+0x0F, WAVES
    -- +0x20/21, RES +0x22/23, DIGI +0x27/28, SPLIT +0x29), the filter curve
    -- RAM (0x10185000-0x10185FFF, byte writes) and the envelope levels
    -- (C64_VOICE_ADSR, +0x80..+0x9F, read through sid_lv_addr/data)
    sid_base        : out std_logic_vector(15 downto 0);
    sid_mask        : out std_logic_vector(15 downto 0);
    sid_split       : out std_logic_vector(2 downto 0);
    sid_waves       : out std_logic_vector(1 downto 0);
    sid_res         : out std_logic_vector(1 downto 0);
    sid_digi        : out std_logic_vector(3 downto 0);
    sidcv_we        : out std_logic;
    sidcv_addr      : out std_logic_vector(11 downto 0);
    sidcv_wdata     : out std_logic_vector(7 downto 0);
    sid_lv_addr     : out std_logic_vector(4 downto 0);
    sid_lv_data     : in  std_logic_vector(7 downto 0) := X"00";
    -- U64_AUDIO_MIXER bytes 0-3 (UltiSID 1 R/L, UltiSID 2 R/L) for T25.3's mixer
    sid_gains       : out std_logic_vector(31 downto 0);
    -- T25.6 sampler: U64_AUDIO_MIXER bytes 8-11 (Sampler L R/L, Sampler R R/L) and C64_SAMPLER_ENABLE
    -- (cart register +0E); the byte window 0x10048000-0x100480FF goes out with the cw_* port as trx_io page 8
    sid_gains2      : out std_logic_vector(31 downto 0);
    smp_en          : out std_logic;

    -- USB: ULPI in the PHY's clock (pins in usb_phy.v), PHY and hub resets
    ulpi_clock      : in  std_logic;
    ulpi_nxt        : in  std_logic;
    ulpi_dir        : in  std_logic;
    ulpi_stp        : out std_logic;
    ulpi_data_i     : in  std_logic_vector(7 downto 0);
    ulpi_data_o     : out std_logic_vector(7 downto 0);
    ulpi_data_t     : out std_logic;            -- 1 = drive the data lines
    ulpi_reset_req  : out std_logic;            -- U2PIO_ULPI_RESET b0, or reset
    hub_reset_n     : out std_logic;           -- U2PIO_HUB_RESET

    -- the RISC-V's own state for rv_dbg.v: PC of the execute stage's last
    -- instruction, the CPU's bus state (see the packing at the assignment)
    rv_tap          : out std_logic_vector(127 downto 0);
    -- T79: the 1541 drive and the GCR codec live in layer 2 (drive_l2.vhd):
    -- their I/O bus regions, the timing ticks, the C64 stop and reset, and
    -- the system bus for the drive's snoop go out flat
    tick_16MHz_o    : out std_logic;
    tick_4MHz_o     : out std_logic;
    tick_1kHz_o     : out std_logic;
    c64_stop_o      : out std_logic;
    c64_reset_n_o   : out std_logic;
    drv_io_read     : out std_logic;
    drv_io_write    : out std_logic;
    drv_io_addr     : out std_logic_vector(23 downto 0);
    drv_io_wdata    : out std_logic_vector(7 downto 0);
    drv_io_rdata    : in  std_logic_vector(7 downto 0);
    drv_io_ack      : in  std_logic;
    drv_io_nc       : in  std_logic;
    gcr_io_read     : out std_logic;
    gcr_io_write    : out std_logic;
    gcr_io_addr     : out std_logic_vector(23 downto 0);
    gcr_io_wdata    : out std_logic_vector(7 downto 0);
    gcr_io_rdata    : in  std_logic_vector(7 downto 0);
    gcr_io_ack      : in  std_logic;
    gcr_io_nc       : in  std_logic;
    -- T79 ICAP loader (0x10060610..), cpu_clk: icap_data is valid with the one-clock pulse icap_we
    -- (a 32-bit word of the bitstream file, first byte in bits 31:24, bits not swapped; top.v swaps)
    icap_we         : out std_logic;
    icap_data       : out std_logic_vector(31 downto 0);
    -- T79 app decouple request (0x10060620 bit 0, cpu_clk); top.v ORs it into core_decouple
    app_decouple    : out std_logic;
    app_port_release: out std_logic;      -- T135 D6: 1 = the host application releases the expansion port lock (icap region +0x21)
    snoop_tag       : out std_logic_vector(7 downto 0);
    snoop_request   : out std_logic;
    snoop_rwn       : out std_logic;
    snoop_addr      : out std_logic_vector(25 downto 0);
    snoop_be        : out std_logic_vector(3 downto 0);
    snoop_wdata     : out std_logic_vector(31 downto 0);
    snoop_rdata     : out std_logic_vector(31 downto 0);
    snoop_rack      : out std_logic;
    snoop_rack_tag  : out std_logic_vector(7 downto 0);
    snoop_dack_tag  : out std_logic_vector(7 downto 0);
    -- T133: toggles for every DDR2 write into the CPU's cacheable window that is not on the CPU's bus (top.v, mig_arb)
    dc_ext_tgl      : in  std_logic := '0';
    dc_ctl          : in  std_logic_vector(15 downto 0) := X"0007";   -- T133 debug control (top.v, JTAG 0x100060)
    dc_data         : out std_logic_vector(31 downto 0);
    -- T133: the 128-bit word of the CPU's last DDR2 read and its line address (top.v, cpu_mem_bridge): the caches fill whole lines from it
    dc_line         : in  std_logic_vector(127 downto 0) := (others => '0');
    dc_line_adr     : in  std_logic_vector(21 downto 0) := (others => '0') );
end entity;

architecture rtl of ultimate_sys is
    signal mem_req      : t_mem_req_32;     -- to cpu_mem_bridge (after the arbiter)
    signal mem_resp     : t_mem_resp_32;
    signal cpu_mem_req  : t_mem_req_32;
    signal cpu_mem_resp : t_mem_resp_32;
    signal eth_mem_req  : t_mem_req_32;
    signal eth_mem_resp : t_mem_resp_32;
    signal eth_req      : t_io_req;
    signal eth_resp     : t_io_resp;
    signal eth_irq_rx   : std_logic;
    signal eth_irq_tx   : std_logic;
    signal wifi_mem_req : t_mem_req_32;
    signal wifi_mem_resp: t_mem_resp_32;
    signal wifi_req     : t_io_req;
    signal wifi_resp    : t_io_resp;
    signal wifi_irq     : std_logic;
    signal usb_mem_req  : t_mem_req_32;
    signal usb_mem_resp : t_mem_resp_32;
    signal usb_req      : t_io_req;
    signal usb_resp     : t_io_resp;
    signal usb_irq      : std_logic;
    signal uci_req      : t_io_req;
    signal uci_resp     : t_io_resp;
    signal ulpi_rst_req : std_logic;
    signal ulpi_reset   : std_logic;            -- ulpi_rst_req in ulpi_clock
    signal sd_req       : t_io_req;
    signal sd_resp      : t_io_resp;
    signal gcr_req      : t_io_req;
    signal gcr_resp     : t_io_resp;
    signal flash_req    : t_io_req;
    signal flash_resp   : t_io_resp;
    signal gate_req     : t_io_req;             -- flash_gate -> i_flash
    signal gate_resp    : t_io_resp;
    signal flash_busy   : std_logic;
    signal flash_ssn    : std_logic;
    type t_gains is array (0 to 3) of unsigned(7 downto 0);   -- UltiSID 1 R/L, socket 1 R/L
    signal main_gain    : t_gains := (others => X"80");
    signal spk_gain     : t_gains := (others => X"00");
    signal master_vol_i : std_logic_vector(7 downto 0) := X"80";   -- T160, unity until the app writes it
    signal tone_sel_i   : std_logic_vector(7 downto 0) := X"00";   -- T189, Flat until the app writes it
    signal keyb_col     : std_logic_vector(7 downto 0) := X"FF";  -- KEYB_COL, low = column selected
    signal keyb_joy_sel : std_logic := '0';                       -- KEYB_JOY b0: the port the line reads (0 port 2, 1 port 1)
    signal wifi_event   : std_logic_vector(7 downto 0);
    signal wifi_event_valid : std_logic;
    signal btn_menu     : unsigned(7 downto 0) := (others => '0');   -- ms left to hold
    signal btn_freeze   : unsigned(7 downto 0) := (others => '0');
    signal esp_reset    : unsigned(7 downto 0) := (others => '0');
    signal buttons      : std_logic_vector(2 downto 0);
    signal keyb_row     : std_logic_vector(7 downto 0);
    signal irq_high     : std_logic_vector(7 downto 0);
    signal irq_in       : std_logic_vector(7 downto 2);
    signal io_req       : t_io_req;
    signal io_resp      : t_io_resp;
    signal irq          : std_logic;

    signal tick_16MHz   : std_logic;
    signal tick_4MHz    : std_logic;
    signal tick_1MHz    : std_logic;
    signal tick_1kHz    : std_logic;

    signal itu_req      : t_io_req;
    signal itu_resp     : t_io_resp;
    signal u2p_req      : t_io_req;
    signal u2p_resp     : t_io_resp;

    signal drv_req      : t_io_req;
    signal drv_resp     : t_io_resp;
    signal c64_reset_n  : std_logic;

    type t_region is (r_none, r_itu, r_iec, r_drive, r_page, r_dma, r_u2p, r_mig, r_ovl, r_eth, r_udp, r_wifi,
                      r_sd, r_flash, r_gcr, r_bl, r_i2c, r_esp, r_usb, r_cw, r_sidcv, r_uci, r_rom, r_pal, r_icap);
    signal region       : t_region;
    signal region_r     : t_region := r_none;
    -- T79.1 (contract 2b, decoupling): a request into the swappable core that gets no ack (a grey box,
    -- or a core being reconfigured) is answered by the host after 65,536 clocks with 0
    signal core_pend    : std_logic := '0';
    signal core_cnt     : unsigned(15 downto 0) := (others => '0');
    signal core_to      : std_logic := '0';
    -- the palette's power-up contents (Colodore, as pal_ram.v initialises it); rewritten when core_decouple rises
    type t_pal is array(0 to 63) of std_logic_vector(7 downto 0);
    constant pal_colodore : t_pal := (
        X"00", X"00", X"00", X"00", X"FF", X"FF", X"FF", X"00",
        X"81", X"33", X"38", X"00", X"75", X"CE", X"C8", X"00",
        X"8E", X"3C", X"97", X"00", X"56", X"AC", X"4D", X"00",
        X"2E", X"2C", X"9B", X"00", X"ED", X"F1", X"71", X"00",
        X"8E", X"50", X"29", X"00", X"55", X"38", X"00", X"00",
        X"C4", X"6C", X"71", X"00", X"4A", X"4A", X"4A", X"00",
        X"7B", X"7B", X"7B", X"00", X"A9", X"FF", X"9F", X"00",
        X"70", X"6D", X"EB", X"00", X"B2", X"B2", X"B2", X"00");
    -- T79 ICAP loader: byte assembler, enable, word counter
    signal icap_sh      : std_logic_vector(31 downto 0) := (others => '0');
    signal icap_n       : unsigned(1 downto 0) := (others => '0');
    signal icap_en      : std_logic := '0';
    signal icap_cnt     : unsigned(31 downto 0) := (others => '0');
    signal app_dc_req   : std_logic := '0';
    signal app_prel_req : std_logic := '0';                          -- port lock release (T135 D6); 0 after reset = locked
    signal dc_prev      : std_logic := '0';
    signal dc_busy      : std_logic := '0';                          -- 64 cycles after core_decouple rises (palette)
    signal pal_sw       : unsigned(5 downto 0) := (others => '0');   -- rewrites the palette
    signal cw_win       : std_logic;
    signal page         : unsigned(2 downto 0);  -- which 256-byte register page
    signal page_r       : unsigned(2 downto 0) := "000";
    signal offset_r     : unsigned(13 downto 0) := (others => '0');
    signal ack_r        : std_logic := '0';
    signal scan_i       : std_logic := '0';
    signal dbg_reg_i    : std_logic_vector(7 downto 0) := X"00";
    signal dbg_tgl_s    : std_logic_vector(2 downto 0) := "000";

    -- register pages: 0 cart regs 0x10040000, 1 MATRIX_KEYB 0x10100300,
    -- 2 board IO 0x10100400, 3 core config 0x10180000, 4 PLD 0x10181000,
    -- 5 debug 0x10181800
    type t_bytes is array(natural range <>) of std_logic_vector(7 downto 0);
    signal pages        : t_bytes(0 to 2047) := (others => X"00");
    -- T140 D29: one write port, read address = write address (page & offset), read registered: block RAM can hold it. Vivado put it in LUTRAM
    -- (RAMB18 1 -> 0, LUTRAM +256) in the D28 run when the block RAM got short.
    attribute ram_style : string;
    attribute ram_style of pages : signal is "block";
    signal page_rdata   : std_logic_vector(7 downto 0);

    signal page_data    : std_logic_vector(7 downto 0);
    signal fixed_data   : std_logic_vector(7 downto 0);

    -- C64 control (cart_slot_registers' semantics for what we have)
    signal c64_reset_i  : std_logic := '1';     -- held until the app has written the ROMs
    signal c64_ultimax  : std_logic := '0';
    signal c64_nmi      : std_logic := '0';
    signal c64_stop     : std_logic := '0';
    signal cart_type_i  : std_logic_vector(7 downto 0) := X"00";   -- b4:0 type, b7:5 variant
    signal reu_enable_i : std_logic := '0';
    signal reu_size_i   : std_logic_vector(2 downto 0) := "111";
    signal cart_kill_i  : std_logic := '0';
    signal cart_force_i : std_logic := '0';
    signal active_s     : std_logic_vector(1 downto 0) := "00";
    signal dma_memonly  : std_logic := '0';
    signal bus_int_i    : std_logic_vector(3 downto 0) := X"F";
    signal bus_ext_i    : std_logic_vector(3 downto 0) := X"0";
    signal video_fmt_i  : std_logic_vector(7 downto 0) := X"00";
    signal turbo_en_i     : std_logic_vector(7 downto 0) := X"00";
    signal turbo_prefer_i : std_logic_vector(7 downto 0) := X"80";
    signal turbo_upd_i    : std_logic := '0';
    signal stream_ena_i : std_logic_vector(7 downto 0) := X"00";
    -- UltiSID registers (T25.1); until the app writes them: UltiSID 1 at
    -- $D400, UltiSID 2 off (base bit 0 outside the mask), 6581, digis Medium
    signal sid_base_i   : std_logic_vector(15 downto 0) := X"0140";
    signal sid_mask_i   : std_logic_vector(15 downto 0) := X"FEFE";
    signal sid_split_i  : std_logic_vector(2 downto 0) := "000";
    signal sid_waves_i  : std_logic_vector(1 downto 0) := "00";
    signal sid_res_i    : std_logic_vector(1 downto 0) := "00";
    signal sid_digi_i   : std_logic_vector(3 downto 0) := "1010";
    signal sid_gains_i  : std_logic_vector(31 downto 0) := X"80808080";
    signal sid_gains2_i : std_logic_vector(31 downto 0) := X"80808080";
    signal smp_en_i     : std_logic := '0';
    signal hpd_s        : std_logic_vector(1 downto 0) := "00";
    signal hpd_waslow   : std_logic := '1';
    signal ddc_en_i     : std_logic := '0';
    -- DMA window
    signal dma_req_i    : std_logic := '0';
    signal dma_busy     : std_logic := '0';
    signal dma_stub     : std_logic := '0';
    signal dma_ack      : std_logic := '0';
    signal dma_rdata_r  : std_logic_vector(7 downto 0) := X"00";
    signal done_s       : std_logic_vector(2 downto 0) := "000";
    signal caseled_sel_i: std_logic_vector(7 downto 0) := X"00";
    signal caseled_set_i: std_logic := '0';
    signal busy_usb_i   : std_logic := '0';
    signal app_keys_i   : std_logic_vector(63 downto 0) := (others => '0');
    signal app_ctl_i    : std_logic_vector(2 downto 0) := "000";   -- freeze, RESTORE, reset
    signal app_joy_i    : std_logic_vector(9 downto 0) := (others => '0');   -- port 2, port 1
    signal rst_sense_s  : std_logic_vector(1 downto 0) := "00";
    signal rv_pc        : std_logic_vector(31 downto 0);
    signal dc_snoop_valid : std_logic;
    signal dc_snoop_addr  : std_logic_vector(25 downto 0);
    signal dc_stat      : std_logic_vector(23 downto 0);
    signal rv_io_busy   : std_logic;
    signal dma_wd       : unsigned(25 downto 0) := (others => '0');   -- clocks the open DMA byte has waited
    signal dma_tmo      : unsigned(7 downto 0) := (others => '0');    -- DMA bytes given up on (rv_dbg.v)
begin
    -- [31:0] PC, [32] CPU waits on an I/O access, [33] memory request pending,
    -- [34] it reads, [35] IRQ line, [43:36] tag, [69:44] memory address,
    -- [93:70] I/O address, [94] it writes, [127:104] data cache counters (T133)
    rv_tap(31 downto 0)   <= rv_pc;
    rv_tap(32)            <= rv_io_busy;
    rv_tap(33)            <= cpu_mem_req.request;
    rv_tap(34)            <= cpu_mem_req.read_writen;
    rv_tap(35)            <= irq;
    rv_tap(43 downto 36)  <= cpu_mem_req.tag;
    rv_tap(69 downto 44)  <= std_logic_vector(cpu_mem_req.address);
    rv_tap(93 downto 70)  <= std_logic_vector(io_req.address);
    rv_tap(94)            <= io_req.write;
    rv_tap(95)            <= '0';
    rv_tap(103 downto 96) <= std_logic_vector(dma_tmo);
    rv_tap(127 downto 104) <= dc_stat;      -- T133: data cache hits [11:0] (upper bits of 20), misses [23:12]
    -- T133: a write by the MAC, WiFi or USB DMA reaches the data cache's snoop; the CPU's own tags do not
    dc_snoop_valid <= mem_req.request when mem_req.read_writen = '0' and mem_req.tag /= X"20" and mem_req.tag /= X"21" else '0';
    dc_snoop_addr  <= std_logic_vector(mem_req.address);
    i_cpu: entity work.rvlite_wrapper
    port map (
        clock    => clock,
        reset    => reset,
        irq_i    => irq,
        io_req   => io_req,
        io_resp  => io_resp,
        io_busy  => rv_io_busy,
        mem_req  => cpu_mem_req,
        mem_resp => cpu_mem_resp,
        dbg_pc   => rv_pc,
        dc_snoop_valid => dc_snoop_valid,
        dc_snoop_addr  => dc_snoop_addr,
        dc_ext_tgl     => dc_ext_tgl,
        dc_stat        => dc_stat,
        dc_ctl         => dc_ctl,
        dc_data        => dc_data,
        dc_line        => dc_line,
        dc_line_adr    => dc_line_adr );

    -- the MAC's DMA first, then the WiFi UART's, USB, the CPU; all see every
    -- response and pick theirs by tag
    i_mem_arb: entity work.mem_bus_arbiter_pri_32
    generic map (
        g_registered => false,
        g_ports      => 4 )
    port map (
        clock    => clock,
        reset    => reset,
        reqs(0)  => eth_mem_req,
        reqs(1)  => wifi_mem_req,
        reqs(2)  => usb_mem_req,
        reqs(3)  => cpu_mem_req,
        resps(0) => eth_mem_resp,
        resps(1) => wifi_mem_resp,
        resps(2) => usb_mem_resp,
        resps(3) => cpu_mem_resp,
        req      => mem_req,
        resp     => mem_resp );

    -- tags and reset divisor as in ultimate_logic_32; the app sets 5 Mbit/s
    i_wifi: entity work.uart_dma
    generic map (
        g_rx_tag    => X"1F",
        g_tx_tag    => X"1E",
        g_events    => true,               -- the ESP32's button packets (below)
        g_divisor   => 100_000_000 / 115_200 )
    port map (
        clock       => clock,
        reset       => reset,
        io_req      => wifi_req,
        io_resp     => wifi_resp,
        irq         => wifi_irq,
        event       => wifi_event,
        event_valid => wifi_event_valid,
        mem_req     => wifi_mem_req,
        mem_resp    => wifi_mem_resp,
        boot        => wifi_boot,
        enable      => wifi_enable,
        route       => open,
        txd         => wifi_txd,
        rxd         => wifi_rxd,
        rts         => wifi_rts,
        cts         => wifi_cts );

    -- ITU high interrupts as in ultimate_logic_32: 3 WiFi; 4 Bling RX; ours:
    -- 2 (drive B there) our IO block (level, cause register in trx_io.v)
    irq_high <= (2 => cw_irq, 3 => wifi_irq, 4 => bl_irq, others => '0');

    g_eth: if g_net /= 0 generate
        i_eth: entity work.ethernet_interface
        generic map (
            g_mem_tag    => X"12" )
        port map (
            sys_clock    => clock,
            sys_reset    => reset,
            io_req       => eth_req,
            io_resp      => eth_resp,
            io_irq_tx    => eth_irq_tx,
            io_irq_rx    => eth_irq_rx,
            mem_req      => eth_mem_req,
            mem_resp     => eth_mem_resp,
            eth_clock    => eth_clock,
            eth_reset    => eth_reset,
            eth_tx_data  => eth_tx_data,
            eth_tx_sof   => open,
            eth_tx_eof   => eth_tx_eof,
            eth_tx_valid => eth_tx_valid,
            eth_tx_ready => eth_tx_ready,
            eth_rx_data  => eth_rx_data,
            eth_rx_sof   => eth_rx_sof,
            eth_rx_eof   => eth_rx_eof,
            eth_rx_valid => eth_rx_valid );
    end generate;
    g_noeth: if g_net = 0 generate
        eth_mem_req    <= c_mem_req_32_init;
        eth_irq_tx     <= '0';
        eth_irq_rx     <= '0';
        eth_tx_data    <= (others => '0');
        eth_tx_eof     <= '0';
        eth_tx_valid   <= '0';
        process(clock)
        begin
            if rising_edge(clock) then
                eth_resp     <= c_io_resp_init;
                eth_resp.ack <= eth_req.read or eth_req.write;
            end if;
        end process;
    end generate;

    -- ITU interrupt inputs as in ultimate_logic_32: 2 USB, 5 RMII RX, 6 RMII TX
    -- (4, the UCI, is the core's now: its interrupt is cw_irq, ITU high interrupt 2)
    irq_in <= (2 => usb_irq, 5 => eth_irq_rx, 6 => eth_irq_tx, 7 => rst_sense_s(1),
               others => '0');

    -- T103: the UCI left the host (it is the core's, behind the cw_* window). Its old I/O window
    -- 0x044000-0x045FFF acks with 0, so a read of it never hangs the RISC-V.
    process(clock)
    begin
        if rising_edge(clock) then
            uci_resp      <= c_io_resp_init;
            uci_resp.ack  <= uci_req.read or uci_req.write;
        end if;
    end process;

    -- USB as in ultimate_logic_32 (tags 0x14-0x17); the link sits in reset
    -- until the PHY clock runs and the app releases U2PIO_ULPI_RESET
    i_ulpi_reset: entity work.level_synchronizer
    generic map (
        g_reset_val => '1' )
    port map (
        clock       => ulpi_clock,
        input       => ulpi_rst_req,
        input_c     => ulpi_reset );

    i_usb: entity work.usb_host_nano
    generic map (
        g_big_endian => false,
        g_tag        => X"14",
        g_simulation => false )
    port map (
        clock        => ulpi_clock,
        reset        => ulpi_reset,
        ulpi_nxt     => ulpi_nxt,
        ulpi_dir     => ulpi_dir,
        ulpi_stp     => ulpi_stp,
        ulpi_data_i  => ulpi_data_i,
        ulpi_data_o  => ulpi_data_o,
        ulpi_data_t  => ulpi_data_t,
        debug_data   => open,
        debug_valid  => open,
        error_pulse  => open,
        sys_clock    => clock,
        sys_reset    => reset,
        sys_mem_req  => usb_mem_req,
        sys_mem_resp => usb_mem_resp,
        sys_io_req   => usb_req,
        sys_io_resp  => usb_resp,
        sys_irq      => usb_irq );
    ulpi_reset_req <= ulpi_rst_req;

    mem_request     <= mem_req.request;
    mem_tag         <= mem_req.tag;
    mem_read_writen <= mem_req.read_writen;
    mem_address     <= std_logic_vector(mem_req.address);
    mem_byte_en     <= mem_req.byte_en;
    mem_wdata       <= mem_req.data;
    mem_resp.data     <= mem_rdata;
    mem_resp.rack     <= mem_rack;
    mem_resp.rack_tag <= mem_rack_tag;
    mem_resp.dack_tag <= mem_dack_tag;

    -- 100 MHz * 4 / 25 = 16 MHz; /4, /16, /16000 for the ITU
    i_ticks: entity work.fractional_div
    generic map (
        g_numerator   => 4,
        g_denominator => 25 )
    port map (
        clock         => clock,
        tick          => tick_16MHz,
        tick_by_4     => tick_4MHz,
        tick_by_16    => tick_1MHz,
        one_16000     => tick_1kHz );

    -- generics as in ultimate_logic_32 (spec 4.1)
    i_itu: entity work.itu
    generic map (
        g_version       => X"25",
        g_capabilities  => g_capabilities,
        g_edge_init     => "10000101",
        g_edge_write    => false,
        g_baudrate      => 115_200 )
    port map (
        clock       => clock,
        reset       => reset,
        io_req      => itu_req,
        io_resp     => itu_resp,
        irq_out     => irq,
        irq_in      => irq_in,
        irq_high    => irq_high,
        tick_4MHz   => tick_4MHz,
        tick_1us    => tick_1MHz,
        tick_1ms    => tick_1kHz,
        buttons     => buttons,
        busy_led    => busy_any,
        misc_io     => open,
        irq_flags   => open,
        uart_txd    => uart_txd,
        uart_rxd    => uart_rxd,
        uart_rts    => open,
        uart_cts    => '1' );

    -- BOARDREV 0x17 (reads 0xB8: an Elite board, not the 0x15 prototype that
    -- swaps the PLL I2C channels); MDC/MDIO go to the Ethernet PHY.
    i_u2p: entity work.u2p_io
    port map (
        clock       => clock,
        reset       => reset,
        io_req      => u2p_req,
        io_resp     => u2p_resp,
        mdc         => mdc,
        mdio_i      => mdio_i,
        mdio_o      => mdio_o,
        i2c_scl_i   => '1',
        i2c_sda_i   => '1',
        i2c_scl_o   => open,
        i2c_sda_o   => open,
        iec_o       => open,
        board_rev   => "10111",
        eth_irq_i   => '1',
        hub_reset_n => hub_reset_n,
        speaker_en  => speaker_en,
        ulpi_reset  => ulpi_rst_req,
        speaker_vol => speaker_vol,
        buffer_en   => open );

    -- ---- SD card and flash: Gideon's SPI block, as in his ultimate_logic_32 --
    i_sd: entity work.spi_peripheral_io
    generic map (
        g_fixed_rate => false,
        g_init_rate  => 500,
        g_crc        => true )
    port map (
        clock       => clock,
        reset       => reset,
        io_req      => sd_req,
        io_resp     => sd_resp,
        busy        => open,
        SD_DETECTn  => sd_detectn,
        SD_WRPROTn  => '1',
        SPI_SSn     => sd_ssn,
        SPI_CLK     => sd_clk,
        SPI_MOSI    => sd_mosi,
        SPI_MISO    => sd_miso );

    i_flash: entity work.spi_peripheral_io
    generic map (
        g_fixed_rate => true,
        g_init_rate  => 1,                  -- 25 MHz SCK at 100 MHz
        g_crc        => false )
    port map (
        clock       => clock,
        reset       => reset,
        io_req      => gate_req,
        io_resp     => gate_resp,
        busy        => flash_busy,
        SPI_SSn     => flash_ssn,
        SPI_CLK     => flash_sck,
        SPI_MOSI    => flash_mosi,
        SPI_MISO    => flash_miso );

    -- T79: GCR encoder/decoder (drive_l2.vhd, layer 2) the app's disk image code uses (HARDWARE_ENCODING),
    -- at 0x060500 as in his ultimate_logic_32
    gcr_io_read  <= gcr_req.read;
    gcr_io_write <= gcr_req.write;
    gcr_io_addr  <= std_logic_vector(gcr_req.address);
    gcr_io_wdata <= gcr_req.data;
    gcr_resp.data <= gcr_io_rdata;
    gcr_resp.ack  <= gcr_io_ack;
    gcr_resp.nc   <= gcr_io_nc;

    -- T44: the write gate, the only master of i_flash, owns the CS pin.
    -- Out of M1 (Alex, 2026-10-04): g_low_limit 0 lets program/erase through
    -- at every address, as on Gideon's core (app updates, any flash layout incl.
    -- the 100T's). 4-byte opcodes stay blocked, and so do status-register writes except the
    -- one lock the host app sets before a large core (g_lock, T152 D9 / T164 S15).
    -- g_wren_pass true: WREN reaches the chip. docs/specs/T44-flash-write-gate.md
    i_flash_gate: entity work.flash_gate
    generic map (
        g_wren_pass => true,
        g_low_limit => 0,
        g_4byte     => false,
        g_lock      => true,
        g_cs_gap    => 10 )
    port map (
        clock       => clock,
        reset       => reset,
        cpu_req     => flash_req,
        cpu_resp    => flash_resp,
        spi_req     => gate_req,
        spi_resp    => gate_resp,
        spi_busy    => flash_busy,
        spi_ssn     => flash_ssn,
        flash_csn   => flash_csn );

    -- KEYB_ROW: a row line is low while a key on it sits in a selected column,
    -- as port B of CIA 1 reads with port A driving the columns
    process(keys, keyb_col)
        variable r : std_logic_vector(7 downto 0);
    begin
        r := X"FF";
        for c in 0 to 7 loop
            if keyb_col(c) = '0' then
                r := r and not keys(c*8 + 7 downto c*8);
            end if;
        end loop;
        keyb_row <= r;
    end process;

    -- The rocker reaches us from the ESP32 as one-byte packets on its UART
    -- (software/u64ctrl: EVENT_FREEZE 0x81, EVENT_MENU 0x82, EVENT_RESET 0x83);
    -- uart_dma hands them out here instead of to the app. Menu is held as
    -- ITU button 1 for 200 ms (the app looks for its rising edge), freeze as
    -- button 0 (bit 5; bit 7 is the right board button, the boot Lock), reset
    -- resets the C64 for 200 ms. T195: the board buttons (ui_btn) join the same
    -- paths: left and middle = Menu (button 1, as long as held), right =
    -- button 2 and the C64 reset for as long as it is held.
    process(clock)
    begin
        if rising_edge(clock) then
            if tick_1kHz = '1' then
                if btn_menu /= 0 then btn_menu <= btn_menu - 1; end if;
                if btn_freeze /= 0 then btn_freeze <= btn_freeze - 1; end if;
                if esp_reset /= 0 then esp_reset <= esp_reset - 1; end if;
            end if;
            if wifi_event_valid = '1' then
                case wifi_event is
                when X"81" => btn_freeze <= to_unsigned(200, 8);
                when X"82" => btn_menu   <= to_unsigned(200, 8);
                when X"83" => esp_reset  <= to_unsigned(200, 8);
                when others => null;
                end case;
            end if;
        end if;
    end process;
    buttons(0) <= '1' when btn_freeze /= 0 else '0';
    buttons(1) <= '1' when btn_menu /= 0 or ui_btn(0) = '1' or ui_btn(1) = '1' else '0';
    buttons(2) <= ui_btn(2);

    -- ---- decode (io_req.address = CPU address - 0x10000000) ----------------
    process(io_req)
        variable a : unsigned(23 downto 0);
    begin
        a := io_req.address;
        region <= r_none;
        page   <= "000";
        if a(23 downto 16) = X"00" then
            region <= r_itu;
        elsif a(23 downto 12) = X"028" then
            region <= r_iec;
        elsif a(23 downto 14) = "0000001000" then          -- 0x020000-0x023FFF
            region <= r_drive;
        elsif a(23 downto 8) = X"0400" then
            region <= r_page; page <= "000";
        elsif a(23 downto 12) = X"04C" or a(23 downto 16) = X"1A" or a(23 downto 8) = X"0480" then
            region <= r_cw;                                 -- GMod2 EEPROM, trx_io, the sampler's bytes (page 8)
        elsif a(23 downto 13) = "00000100010" then         -- 0x044000-0x045FFF
            region <= r_uci;
        elsif a(23 downto 16) = X"05" then
            region <= r_dma;
        elsif a(23 downto 9) = "000001100000000" then       -- 0x060000-0x0601FF
            region <= r_sd;
        elsif a(23 downto 8) = X"0602" then                 -- 0x060200-0x0602FF
            region <= r_flash;
        elsif a(23 downto 8) = X"0605" then                 -- 0x060500-0x0605FF
            region <= r_gcr;
        elsif a(23 downto 8) = X"0606" then                 -- 0x060600-0x0606FF: ICAP (T79 loader)
            region <= r_icap;
        elsif a(23 downto 6) = "000001100000100000" then    -- 0x060800-0x06083F
            region <= r_eth;
        elsif a(23 downto 4) = X"06090" then                -- 0x060900-0x06090F
            region <= r_wifi;
        elsif a(23 downto 4) = X"06091" then                -- 0x060910-0x06091F: ESP32 boot control (esp_ctl.v)
            region <= r_esp;
        elsif a(23 downto 12) = X"080" then                 -- 0x080000-0x080FFF
            region <= r_usb;
        elsif a(23 downto 8) = X"1000" then
            region <= r_u2p;
        elsif a(23 downto 8) = X"1001" then
            region <= r_mig;
        elsif a(23 downto 8) = X"1006" or a(23 downto 9) = "000100000000100" then   -- 0x100800-0x1009FF
            region <= r_bl;
        elsif a(23 downto 4) = X"10070" then
            region <= r_i2c;
        elsif a(23 downto 8) = X"1003" then
            region <= r_page; page <= "001";
        elsif a(23 downto 8) = X"1004" then
            region <= r_page; page <= "010";
        elsif a(23 downto 15) = "000101000" then    -- 0x140000-0x147FFF
            region <= r_ovl;
        elsif a(23 downto 8) = X"1800" then
            region <= r_page; page <= "011";
        elsif a(23 downto 12) = X"185" then                 -- 0x185000-0x185FFF
            region <= r_sidcv;
        elsif a(23 downto 8) = X"1900" then
            region <= r_udp;
        elsif a(23 downto 15) = "000110001" then    -- 0x188000-0x18FFFF: BASIC, KERNAL, CHAR ROM
            region <= r_rom;
        elsif a(23 downto 11) = "0001100000001" then       -- 0x180800-0x180FFF: C64_PALETTE
            region <= r_pal;
        elsif a(23 downto 8) = X"1810" then
            region <= r_page; page <= "100";
        elsif a(23 downto 8) = X"1818" then
            region <= r_page; page <= "101";
        end if;
    end process;

    process(io_req, region)
    begin
        itu_req <= io_req;
        u2p_req <= io_req;
        eth_req <= io_req;
        if region /= r_eth then
            eth_req.read <= '0'; eth_req.write <= '0';
        end if;
        sd_req <= io_req;
        if region /= r_sd then
            sd_req.read <= '0'; sd_req.write <= '0';
        end if;
        flash_req <= io_req;
        if region /= r_flash then
            flash_req.read <= '0'; flash_req.write <= '0';
        end if;
        gcr_req <= io_req;
        if region /= r_gcr then
            gcr_req.read <= '0'; gcr_req.write <= '0';
        end if;
        wifi_req <= io_req;
        if region /= r_wifi then
            wifi_req.read <= '0'; wifi_req.write <= '0';
        end if;
        usb_req <= io_req;
        if region /= r_usb then
            usb_req.read <= '0'; usb_req.write <= '0';
        end if;
        uci_req <= io_req;
        if region /= r_uci then
            uci_req.read <= '0'; uci_req.write <= '0';
        end if;
        drv_req <= io_req;
        if region /= r_drive then
            drv_req.read <= '0'; drv_req.write <= '0';
        end if;
        if region /= r_itu then
            itu_req.read <= '0'; itu_req.write <= '0';
        end if;
        if region /= r_u2p then
            u2p_req.read <= '0'; u2p_req.write <= '0';
        end if;
    end process;

    ovl_we    <= io_req.write when region = r_ovl else '0';
    i2c_we    <= io_req.write when region = r_i2c else '0';
    i2c_re    <= io_req.read when region = r_i2c else '0';
    i2c_addr  <= std_logic_vector(io_req.address(3 downto 0));
    i2c_wdata <= io_req.data;
    esp_we    <= io_req.write when region = r_esp else '0';
    esp_re    <= io_req.read when region = r_esp else '0';
    esp_addr  <= std_logic_vector(io_req.address(3 downto 0));
    esp_wdata <= io_req.data;
    ovl_addr  <= std_logic_vector(io_req.address(14 downto 0));
    ovl_wdata <= io_req.data;
    pal_we    <= '1' when dc_busy = '1' and pal_sw(1 downto 0) /= "11" else
                 io_req.write when region = r_pal and io_req.address(10 downto 6) = "00000" and dc_busy = '0' else '0';
    pal_addr  <= std_logic_vector(pal_sw) when dc_busy = '1' else std_logic_vector(io_req.address(5 downto 0));
    pal_wdata <= pal_colodore(to_integer(pal_sw)) when dc_busy = '1' else io_req.data;

    cw_we     <= io_req.write when region = r_cw else '0';
    cw_re     <= io_req.read when region = r_cw else '0';
    cw_win    <= '1' when io_req.address(23 downto 12) = X"04C" else '0';
    cw_addr   <= cw_win & std_logic_vector(io_req.address(15 downto 0));
    cw_wdata  <= io_req.data;

    bl_we     <= io_req.write when region = r_bl else '0';
    bl_addr   <= std_logic_vector(io_req.address(11 downto 0));
    bl_wdata  <= io_req.data;

    process(clock)
    begin
        if rising_edge(clock) then
            ack_r   <= '0';
            dma_ack <= '0';
            if io_req.read = '1' or io_req.write = '1' then
                region_r <= region;
                page_r   <= page;
                offset_r <= io_req.address(13 downto 0);
                -- Reads of $D400-$D7FF see no SID: the C64U's SID sockets are empty,
                -- and the app's socket detection would take our internal SID for one
                -- ("SID model has changed" at every boot). Writes still reach it.
                -- G06: $D000-$DFFF is a bus cycle whether the C64 is stopped, held by a debugger
                -- stop or running (the machine's CPU is held at a read cycle for the byte, c64_core.v)
                if core_decouple = '0' and region = r_dma and
                   not (io_req.read = '1' and dma_memonly = '0' and
                        io_req.address(15 downto 10) = "110101") then
                    -- one byte: hand it to top.v, ack when it is done
                    dma_stub  <= '0';
                    if io_req.address(15 downto 12) = X"D" then
                        dma_bus <= not dma_memonly;
                    -- T16 Q8: a write of $0000/$0001 is a bus cycle even in MEMONLY (the app writes
                    -- $0000-$03FF that way): the cycle reaches the 6510 port and the RAM below it
                    elsif io_req.write = '1' and io_req.address(15 downto 1) = 0 then
                        dma_bus <= c64_stop;
                    else
                        dma_bus <= c64_stop and not dma_memonly;
                    end if;
                    dma_rom   <= '0';
                    dma_addr  <= std_logic_vector(io_req.address(15 downto 0));
                    dma_we    <= io_req.write;
                    dma_wdata <= io_req.data;
                    dma_req_i <= not dma_req_i;
                    dma_busy  <= '1';
                elsif core_decouple = '0' and region = r_rom then
                    -- ROM block byte offset: char 0x0000 (0x18C000, 4 K, mirrored
                    -- up to 0x18EFFF), KERNAL 0x2000 (0x18A000), BASIC 0x4000 (0x188000);
                    -- 0x18F000-0x18F3FF (mirrored to 0x18FFFF) is the colour RAM, through its
                    -- second port: dma_addr 0x6000 + offset, no bus cycle
                    dma_stub  <= '0';
                    dma_bus   <= '0';
                    dma_rom   <= '1';
                    if io_req.address(14 downto 12) = "111" then
                        dma_addr <= "011000" & std_logic_vector(io_req.address(9 downto 0));
                    elsif io_req.address(14) = '1' then
                        dma_addr <= X"0" & std_logic_vector(io_req.address(11 downto 0));
                    elsif io_req.address(13) = '1' then
                        dma_addr <= "001" & std_logic_vector(io_req.address(12 downto 0));
                    else
                        dma_addr <= "010" & std_logic_vector(io_req.address(12 downto 0));
                    end if;
                    dma_we    <= io_req.write;
                    dma_wdata <= io_req.data;
                    dma_req_i <= not dma_req_i;
                    dma_busy  <= '1';
                else
                    dma_stub <= '1';
                    if region /= r_cw and region /= r_uci then
                        ack_r <= '1';           -- trx_io, the UCI ack themselves
                    end if;
                end if;
            end if;
            done_s <= done_s(1 downto 0) & dma_done_tgl;
            if dma_busy = '1' and done_s(2) /= done_s(1) then
                dma_busy    <= '0';
                dma_ack     <= '1';
                dma_rdata_r <= dma_rdata;
            end if;
            -- A byte the C64 side never answers (a bus cycle that never comes, say the
            -- 6510 will not stop) would hold the CPU on this access for ever: after
            -- 2^25 clocks (335 ms) it is acked with $FF and counted. (G06: a byte stolen
            -- from the running C64 waits for a REU transfer, 64 KB at most, to end.)
            if dma_busy = '0' then
                dma_wd <= (others => '0');
            else
                dma_wd <= dma_wd + 1;
                if dma_wd(25) = '1' and not (done_s(2) /= done_s(1)) then
                    dma_busy    <= '0';
                    dma_ack     <= '1';
                    dma_rdata_r <= X"FF";
                    dma_wd      <= (others => '0');
                    if dma_tmo /= X"FF" then
                        dma_tmo <= dma_tmo + 1;
                    end if;
                end if;
            end if;

            -- C64 control registers 0x10040000-06 (cart_slot_registers)
            if io_req.write = '1' and region = r_page and page = "000" then
                case to_integer(io_req.address(7 downto 0)) is
                when 0 =>
                    if io_req.data(2) = '1' then
                        c64_reset_i <= '1';
                    elsif io_req.data(3) = '1' then
                        c64_reset_i <= '0';
                    else
                        c64_ultimax <= io_req.data(1);
                        c64_nmi     <= io_req.data(4);
                    end if;
                when 1 =>
                    c64_stop <= io_req.data(0);
                when 5 =>
                    cart_type_i <= io_req.data;
                when 8 =>
                    reu_enable_i <= io_req.data(0);
                when 9 =>
                    reu_size_i <= io_req.data(2 downto 0);
                when 14 =>
                    smp_en_i <= io_req.data(0);
                when 6 =>
                    if io_req.data(0) = '1' then
                        cart_kill_i <= not cart_kill_i;
                    end if;
                    if io_req.data(1) = '1' then
                        cart_force_i <= not cart_force_i;
                    end if;
                when others =>
                    null;
                end case;
            end if;
            if io_req.write = '1' and region = r_page and page = "011" and
               io_req.address(7 downto 0) = X"03" then
                dma_memonly <= io_req.data(0);
            end if;
            -- T38: MATRIX_KEYB and the joystick outputs
            if io_req.write = '1' and region = r_page and page = "001" then
                case to_integer(io_req.address(7 downto 0)) is
                when 0 to 7 =>
                    for i in 0 to 7 loop
                        if to_integer(io_req.address(2 downto 0)) = i then
                            app_keys_i(i*8 + 7 downto i*8) <= io_req.data;
                        end if;
                    end loop;
                when 8  => app_ctl_i(0) <= io_req.data(0);
                when 9  => app_ctl_i(1) <= io_req.data(0);
                when 10 => app_ctl_i(2) <= io_req.data(0);
                when others => null;
                end case;
            end if;
            if io_req.write = '1' and region = r_page and page = "011" then
                if io_req.address(7 downto 0) = X"30" then
                    app_joy_i(4 downto 0) <= not io_req.data(4 downto 0);
                elsif io_req.address(7 downto 0) = X"31" then
                    app_joy_i(9 downto 5) <= not io_req.data(4 downto 0);
                end if;
            end if;
            -- T39 with T16: the core's reset or the cartridge port's RESET line
            rst_sense_s <= rst_sense_s(0) & (c64_reset_sense or slot_pins(2));
            if io_req.write = '1' and region = r_page and page = "011" and
               io_req.address(7 downto 0) = X"2B" then
                bus_int_i <= io_req.data(3 downto 0);
            end if;
            if io_req.write = '1' and region = r_page and page = "011" and
               io_req.address(7 downto 0) = X"2C" then
                bus_ext_i <= io_req.data(3 downto 0);
            end if;
            if io_req.write = '1' and region = r_page and page = "011" and
               io_req.address(7 downto 0) = X"01" then
                video_fmt_i <= io_req.data;
            end if;
            if io_req.write = '1' and region = r_page and page = "011" then
                case io_req.address(7 downto 0) is
                when X"00" => scan_i         <= io_req.data(0);
                when X"02" => turbo_en_i     <= io_req.data;
                when X"2D" => turbo_prefer_i <= io_req.data;
                when X"2E" => turbo_upd_i    <= not turbo_upd_i;
                when others => null;
                end case;
            end if;
            if io_req.write = '1' and region = r_page and page = "011" then
                case io_req.address(7 downto 0) is
                when X"0A" => sid_base_i(7 downto 0)  <= io_req.data;
                when X"0B" => sid_base_i(15 downto 8) <= io_req.data;
                when X"0E" => sid_mask_i(7 downto 0)  <= io_req.data;
                when X"0F" => sid_mask_i(15 downto 8) <= io_req.data;
                when X"20" => sid_waves_i(0) <= io_req.data(0);
                when X"21" => sid_waves_i(1) <= io_req.data(0);
                when X"22" => sid_res_i(0) <= io_req.data(0);
                when X"23" => sid_res_i(1) <= io_req.data(0);
                when X"27" => sid_digi_i(1 downto 0) <= io_req.data(1 downto 0);
                when X"28" => sid_digi_i(3 downto 2) <= io_req.data(1 downto 0);
                when X"29" => sid_split_i <= io_req.data(2 downto 0);
                when others => null;
                end case;
            end if;
            sidcv_we <= '0';
            if io_req.write = '1' and region = r_sidcv then
                sidcv_we    <= '1';
                sidcv_addr  <= std_logic_vector(io_req.address(11 downto 0));
                sidcv_wdata <= io_req.data;
            end if;
            if io_req.write = '1' and region = r_page and page = "010" and
               io_req.address(7 downto 0) = X"0F" then
                stream_ena_i <= io_req.data;
            end if;
            -- U64_HDMI_REG: 0x20 DDC on, 0x10 DDC off, 0x08 clears "HPD was low"
            hpd_s <= hpd_s(0) & hdmi_hpd;
            if hpd_s(1) = '0' then
                hpd_waslow <= '1';
            end if;
            if io_req.write = '1' and region = r_page and page = "010" and
               io_req.address(7 downto 0) = X"00" then
                if io_req.data(5) = '1' then
                    ddc_en_i <= '1';
                elsif io_req.data(4) = '1' then
                    ddc_en_i <= '0';
                end if;
                if io_req.data(3) = '1' and hpd_s(1) = '1' then
                    hpd_waslow <= '0';
                end if;
            end if;
            if io_req.write = '1' and region = r_page and page = "010" and
               io_req.address(7 downto 0) = X"0A" then
                keyb_col <= io_req.data;
            end if;
            if io_req.write = '1' and region = r_page and page = "010" and
               io_req.address(7 downto 0) = X"06" then
                keyb_joy_sel <= io_req.data(0);
            end if;
            if io_req.write = '1' and region = r_page and page = "010" and
               io_req.address(7 downto 0) = X"0E" then
                caseled_sel_i <= io_req.data;
                caseled_set_i <= '1';
            end if;
            -- ITU_USB_BUSY (ITU 0x24, the third block of its splitter)
            if io_req.write = '1' and region = r_itu and io_req.address(5 downto 0) = "100100" then
                busy_usb_i <= io_req.data(0);
            end if;
            active_s <= active_s(0) & cart_active;
            -- register pages: write, and read every clock (the read data is
            -- used one clock after the request, with the ack)
            if io_req.write = '1' and region = r_page then
                pages(to_integer(page & io_req.address(7 downto 0))) <= io_req.data;
            end if;
            -- T62 G17: U64_DEBUG_REGISTER (page 101 offset 0) is its own byte, not in the page RAM; the C64's write to $D7FF comes after the app's
            dbg_tgl_s <= dbg_tgl_s(1 downto 0) & c64_dbg_tgl;
            if io_req.write = '1' and region = r_page and page = "101" and io_req.address(7 downto 0) = X"00" then
                dbg_reg_i <= io_req.data;
            end if;
            if dbg_tgl_s(2) /= dbg_tgl_s(1) then
                dbg_reg_i <= c64_dbg_data;
            end if;
            page_rdata <= pages(to_integer(page & io_req.address(7 downto 0)));

            if io_req.write = '1' and io_req.address(23 downto 8) = X"1005" then
                case io_req.address(7 downto 0) is
                when X"00" => main_gain(0) <= unsigned(io_req.data);
                              sid_gains_i(7 downto 0) <= io_req.data;
                when X"02" => sid_gains_i(23 downto 16) <= io_req.data;
                when X"03" => sid_gains_i(31 downto 24) <= io_req.data;
                when X"01" => main_gain(1) <= unsigned(io_req.data);
                              sid_gains_i(15 downto 8) <= io_req.data;
                when X"08" => sid_gains2_i(7 downto 0) <= io_req.data;
                when X"09" => sid_gains2_i(15 downto 8) <= io_req.data;
                when X"0A" => sid_gains2_i(23 downto 16) <= io_req.data;
                when X"0B" => sid_gains2_i(31 downto 24) <= io_req.data;
                when X"04" => main_gain(2) <= unsigned(io_req.data);
                when X"05" => main_gain(3) <= unsigned(io_req.data);
                when X"40" => spk_gain(0)  <= unsigned(io_req.data);
                when X"41" => spk_gain(1)  <= unsigned(io_req.data);
                when X"44" => spk_gain(2)  <= unsigned(io_req.data);
                when X"45" => spk_gain(3)  <= unsigned(io_req.data);
                when X"50" => master_vol_i <= io_req.data;
                when X"51" => tone_sel_i <= io_req.data;
                when others => null;
                end case;
            end if;

            con_valid <= '0';
            if io_req.write = '1' and region = r_itu and
               io_req.address(5 downto 4) = "01" and io_req.address(1 downto 0) = "00" then
                con_valid <= '1';
                con_data  <= io_req.data;
            end if;

            if reset = '1' then
                ack_r <= '0';
                dma_busy <= '0';
                c64_reset_i <= '1';                 -- g_cartreset_init = '1'
                c64_stop <= '0';
                cart_type_i <= X"00";
                reu_enable_i <= '0';
                smp_en_i <= '0';
                reu_size_i <= "111";
                dma_memonly <= '0';
                bus_int_i <= X"F";
                bus_ext_i <= X"0";
                turbo_en_i <= X"00";
                turbo_prefer_i <= X"80";
                stream_ena_i <= X"00";
                app_keys_i <= (others => '0');
                app_ctl_i <= "000";
                app_joy_i <= (others => '0');
                caseled_set_i <= '0';
                busy_usb_i <= '0';
            end if;

            -- T79.1: a core swap is a cold boot. When core_decouple rises, the host-owned core state goes
            -- back to its power-up values once: the registers of page 0 (cart) and page 3 (core config), the
            -- palette (pal_sw above, 64 cycles), the SID filter-curve writes. Pages 1, 2, 4, 5 stay. The page
            -- RAM's read-back bytes keep their values until the app writes them (clearing them took a second
            -- write port and turned the RAM into LUTRAM, run 732). Once only, not held: the app writes page 0
            -- while decoupled (C64::hard_stop polls C64_STOP; ue2emu S41).
            dc_prev <= core_decouple;
            if dc_busy = '1' then
                pal_sw   <= pal_sw + 1;
                sidcv_we <= '0';
                if pal_sw = 63 then
                    dc_busy <= '0';
                end if;
            end if;
            if core_decouple = '1' and dc_prev = '0' then
                dc_busy  <= '1';
                pal_sw   <= (others => '0');
                c64_reset_i <= '1';
                c64_ultimax <= '0';
                c64_nmi <= '0';
                c64_stop <= '0';
                cart_type_i <= X"00";
                reu_enable_i <= '0';
                reu_size_i <= "111";
                smp_en_i <= '0';
                dma_memonly <= '0';
                bus_int_i <= X"F";
                bus_ext_i <= X"0";
                video_fmt_i <= X"00";
                scan_i <= '0';
                turbo_en_i <= X"00";
                turbo_prefer_i <= X"80";
                app_joy_i <= (others => '0');
                sid_base_i <= X"0140";
                sid_mask_i <= X"FEFE";
                sid_split_i <= "000";
                sid_waves_i <= "00";
                sid_res_i <= "00";
                sid_digi_i <= "1010";
            end if;
        end if;
    end process;

    -- constants the boot polls on (spec 4.7, 6.1, ue2emu c64.rs / u64io.rs)
    process(page_r, offset_r, page_rdata, sid_lv_data, c64_nmi, c64_reset_i, c64_ultimax, c64_stop,
            cart_type_i, active_s, stream_ena_i, keyb_row, rst_sense_s, slot_pins, dbg_reg_i, restore_pin)
        variable o : unsigned(7 downto 0);
    begin
        o := offset_r(7 downto 0);
        page_data <= page_rdata;
        case page_r is
        when "000" =>                       -- cart registers 0x10040000
            case o is
            when X"00" =>                   -- C64_MODE
                page_data <= "000" & c64_nmi & "0" & c64_reset_i & c64_ultimax & "0";
            when X"01" =>                   -- C64_STOP: STOPPED follows the request at
                                            -- once (the machine pause stops within a
                                            -- cycle; a DMA byte waits for it anyway)
                page_data <= "000000" & c64_stop & c64_stop;
            when X"03" =>                   -- C64_CLOCK_DETECT: PHI2 present, b4 reset sensed (core or port)
                page_data <= "000" & rst_sense_s(1) & X"1";
            when X"05" =>
                page_data <= cart_type_i;
            when X"06" =>                   -- cartridge active
                page_data <= "0000000" & active_s(1);
            when others => null;
            end case;
        when "010" =>                       -- U64 board IO 0x10100400
            case o is
            when X"00" =>                   -- HDMI: HPD was low, HPD now
                page_data <= "0000" & hpd_waslow & hpd_s(1) & "00";
            when X"02" => page_data <= "0000000" & restore_pin;   -- RESTORE: 1 = safe mode (T62 G04)
            when X"03" => page_data <= "000000" & slot_pins(1 downto 0); -- CART_DETECT: EXROM, GAME pins
            when X"06" =>                   -- KEYB_JOY: the selected port's line, low = pressed
                if keyb_joy_sel = '1' then
                    page_data <= "111" & not joys(4 downto 0);
                else
                    page_data <= "111" & not joys(9 downto 5);
                end if;
            when X"07" => page_data <= X"01";   -- BLACKBOARD
            when X"0B" => page_data <= keyb_row;    -- KEYB_ROW for the columns in KEYB_COL
            when X"0F" => page_data <= stream_ena_i;
            when others => null;
            end case;
        when "101" =>                       -- debug 0x10181800: U64_DEBUG_REGISTER
            if o = X"00" then
                page_data <= dbg_reg_i;
            end if;
        when "011" =>                       -- core config 0x10180000
            if o = X"10" then
                page_data <= X"01";         -- CORE_VERSION
            elsif o(7 downto 5) = "100" then
                page_data <= sid_lv_data;   -- SID voice levels (C64_VOICE_ADSR)
            end if;
        when others => null;
        end case;
    end process;

    -- T79 ICAP loader. Gideon's Spartan offsets (+00 FPGA type, +04 pulse, +08 write: W25Q_Flash::reboot)
    -- stay inert; the loader lives at +10..+1B. Bytes are ignored while the enable (+14 bit 0) is 0.
    process(clock)
    begin
        if rising_edge(clock) then
            icap_we <= '0';
            if reset = '1' then
                icap_en  <= '0';
                app_dc_req <= '0';
                app_prel_req <= '0';
                icap_n   <= (others => '0');
            elsif io_req.write = '1' and region = r_icap then
                if io_req.address(7 downto 0) = X"10" and icap_en = '1' then
                    icap_sh <= icap_sh(23 downto 0) & io_req.data;
                    icap_n  <= icap_n + 1;
                    if icap_n = "11" then
                        icap_we  <= '1';
                        icap_cnt <= icap_cnt + 1;
                    end if;
                elsif io_req.address(7 downto 0) = X"20" then
                    app_dc_req <= io_req.data(0);
                elsif io_req.address(7 downto 0) = X"21" then
                    app_prel_req <= io_req.data(0);
                elsif io_req.address(7 downto 0) = X"14" then
                    icap_en <= io_req.data(0);
                    icap_n  <= (others => '0');
                    if io_req.data(0) = '1' and icap_en = '0' then
                        icap_cnt <= (others => '0');
                    end if;
                end if;
            end if;
        end if;
    end process;
    icap_data <= icap_sh;
    app_decouple <= app_dc_req;
    app_port_release <= app_prel_req;

    process(region_r, offset_r, page_data, ovl_rdata, ddr_ready, dma_stub, dma_rdata_r, bl_rdata, i2c_rdata, esp_rdata, cw_rdata,
            icap_en, icap_cnt, app_dc_req, app_prel_req, core_decouple)
    begin
        fixed_data <= X"00";
        case region_r is
        when r_icap =>                      -- +00 ID 0x7A, +14 status, +18..+1B word count (little endian)
            case to_integer(offset_r(7 downto 0)) is
            when 16#00# => fixed_data <= X"7A";
            when 16#14# => fixed_data <= "0000000" & icap_en;
            when 16#20# => fixed_data <= "000000" & core_decouple & app_dc_req;
            when 16#21# => fixed_data <= "0000000" & app_prel_req;
            when 16#18# => fixed_data <= std_logic_vector(icap_cnt(7 downto 0));
            when 16#19# => fixed_data <= std_logic_vector(icap_cnt(15 downto 8));
            when 16#1A# => fixed_data <= std_logic_vector(icap_cnt(23 downto 16));
            when 16#1B# => fixed_data <= std_logic_vector(icap_cnt(31 downto 24));
            when others => null;
            end case;
        when r_iec =>                       -- IEC processor idle: version, both FIFOs empty
            case to_integer(offset_r(11 downto 0)) is
            when 0 => fixed_data <= X"25";
            when 1 | 2 => fixed_data <= X"01";
            when others => null;
            end case;
        when r_page =>
            fixed_data <= page_data;
        when r_rom =>
            fixed_data <= dma_rdata_r;
        when r_dma =>                       -- C64 RAM, or $D000-$DFFF: $D012 = $FF, keyboard
            if dma_stub = '1' then          -- idle, no SID
                fixed_data <= X"FF";
            else
                fixed_data <= dma_rdata_r;
            end if;
        when r_mig =>                       -- read by our boot ROM in place of the DDR2 PHY regs
            fixed_data <= "0000000" & ddr_ready;
        when r_ovl =>
            fixed_data <= ovl_rdata;
        when r_bl =>
            fixed_data <= bl_rdata;
        when r_cw =>
            fixed_data <= cw_rdata;
        when r_i2c =>
            fixed_data <= i2c_rdata;
        when r_esp =>
            fixed_data <= esp_rdata;
        when others => null;
        end case;
    end process;

    -- T79.1: the host's timeout on the core's windows (1541 drive, GCR codec, cw window, DMA window,
    -- and the ROM window, whose bytes go to the core as DMA requests: found by the ue2emu core dev mode).
    -- The PoC of 2026-10-12 swapped the core by partial reconfiguration while the app read a drive
    -- register; the ack never came and the app CPU waited for ever.
    process(clock)
    begin
        if rising_edge(clock) then
            core_to <= '0';
            if reset = '1' then
                core_pend <= '0';
            elsif io_req.read = '1' or io_req.write = '1' then
                core_cnt <= (others => '0');
                if region = r_drive or region = r_gcr or region = r_cw or region = r_dma or region = r_rom then
                    if core_decouple = '1' then
                        core_pend <= '0';
                        core_to   <= '1';       -- decoupled: ack with 0 at once
                    else
                        core_pend <= '1';
                    end if;
                else
                    core_pend <= '0';
                end if;
            elsif core_pend = '1' then
                if io_resp.ack = '1' then
                    core_pend <= '0';
                elsif core_cnt = X"FFFF" then
                    core_pend <= '0';
                    core_to   <= '1';
                else
                    core_cnt <= core_cnt + 1;
                end if;
            end if;
        end if;
    end process;

    process(region_r, itu_resp, u2p_resp, eth_resp, wifi_resp, sd_resp, flash_resp, gcr_resp, usb_resp, drv_resp, ack_r,
            uci_resp,
            dma_ack, cw_ack,
            fixed_data, core_to)
    begin
        io_resp <= c_io_resp_init;
        case region_r is
        when r_itu => io_resp <= itu_resp;
        when r_u2p => io_resp <= u2p_resp;
        when r_eth => io_resp <= eth_resp;
        when r_wifi => io_resp <= wifi_resp;
        when r_sd => io_resp <= sd_resp;
        when r_flash => io_resp <= flash_resp;
        when r_gcr => io_resp <= gcr_resp;
        when r_usb => io_resp <= usb_resp;
        when r_drive => io_resp <= drv_resp;
        when r_uci => io_resp <= uci_resp;
        when r_cw =>
            io_resp.ack  <= cw_ack;
            io_resp.data <= fixed_data;
        when others =>
            io_resp.ack  <= ack_r or dma_ack;
            io_resp.data <= fixed_data;
        end case;
        if core_to = '1' then
            io_resp.ack  <= '1';
            io_resp.data <= (others => '0');
        end if;
    end process;

    dma_req_tgl   <= dma_req_i;
    c64_reset_n   <= '0' when c64_reset_i = '1' or esp_reset /= 0 or ui_btn(2) = '1' else '1';
    c64_reset_req <= not c64_reset_n;
    hdmi_ddc_en   <= ddc_en_i;

    -- T79: drive A (drive_l2.vhd, layer 2), as in ultimate_logic_32 (registers, parameter RAM, 64 K image
    -- at 0x0EE0000, sounds at 0x0EC0000); it stops with the C64 when the app
    -- asks for that (stop_on_freeze)
    drv_io_read  <= drv_req.read;
    drv_io_write <= drv_req.write;
    drv_io_addr  <= std_logic_vector(drv_req.address);
    drv_io_wdata <= drv_req.data;
    drv_resp.data <= drv_io_rdata;
    drv_resp.ack  <= drv_io_ack;
    drv_resp.nc   <= drv_io_nc;
    tick_16MHz_o  <= tick_16MHz;
    tick_4MHz_o   <= tick_4MHz;
    tick_1kHz_o   <= tick_1kHz;
    c64_stop_o    <= c64_stop;
    c64_reset_n_o <= c64_reset_n;
    snoop_tag      <= mem_req.tag;
    snoop_request  <= mem_req.request;
    snoop_rwn      <= mem_req.read_writen;
    snoop_addr     <= std_logic_vector(mem_req.address);
    snoop_be       <= mem_req.byte_en;
    snoop_wdata    <= mem_req.data;
    snoop_rdata    <= mem_resp.data;
    snoop_rack     <= mem_resp.rack;
    snoop_rack_tag <= mem_resp.rack_tag;
    snoop_dack_tag <= mem_resp.dack_tag;
    c64_pause_req <= c64_stop;
    c64_ultimax_req <= c64_ultimax;
    app_keys      <= app_keys_i;
    app_reset     <= app_ctl_i(0);
    app_restore   <= app_ctl_i(1) or c64_nmi;   -- and C64_MODE b4 (NMI)
    app_freeze    <= app_ctl_i(2);
    scanlines     <= scan_i;
    app_joy1      <= app_joy_i(4 downto 0);
    app_joy2      <= app_joy_i(9 downto 5);
    c64_bus_int     <= bus_int_i;
    c64_bus_ext     <= bus_ext_i;
    video_fmt       <= video_fmt_i;
    turbo_en        <= turbo_en_i;
    turbo_prefer    <= turbo_prefer_i;
    turbo_upd_tgl   <= turbo_upd_i;
    cart_type      <= cart_type_i;
    reu_enable     <= reu_enable_i;
    reu_size       <= reu_size_i;
    cart_kill_tgl  <= cart_kill_i;
    cart_force_tgl <= cart_force_i;
    caseled_sel   <= caseled_sel_i;
    caseled_set   <= caseled_set_i;
    busy_usb      <= busy_usb_i;
    sid_base    <= sid_base_i;
    sid_mask    <= sid_mask_i;
    sid_split   <= sid_split_i;
    sid_waves   <= sid_waves_i;
    sid_res     <= sid_res_i;
    sid_digi    <= sid_digi_i;
    sid_lv_addr <= std_logic_vector(offset_r(4 downto 0));
    sid_gains   <= sid_gains_i;
    sid_gains2  <= sid_gains2_i;
    smp_en      <= smp_en_i;
    mix_r   <= std_logic_vector(main_gain(0)) when main_gain(0) > main_gain(2) else std_logic_vector(main_gain(2));
    mix_l   <= std_logic_vector(main_gain(1)) when main_gain(1) > main_gain(3) else std_logic_vector(main_gain(3));
    master_vol <= master_vol_i;
    tone_sel <= tone_sel_i;
    mix_spk <= std_logic_vector(spk_gain(0)) when spk_gain(0) >= spk_gain(1) and spk_gain(0) >= spk_gain(2) and spk_gain(0) >= spk_gain(3) else
               std_logic_vector(spk_gain(1)) when spk_gain(1) >= spk_gain(2) and spk_gain(1) >= spk_gain(3) else
               std_logic_vector(spk_gain(2)) when spk_gain(2) >= spk_gain(3) else
               std_logic_vector(spk_gain(3));
end architecture;
