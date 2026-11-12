-- SPDX-License-Identifier: GPL-3.0-or-later
-- T88: the Atari 800 XL machine - Watson's atari800core with everything the wrapper atari800core_simple_sdram does
-- around it (joysticks into PIA port A and the GTIA triggers, paddles through pot_from_signed, the SIO wiring) but
-- with our memory interface: the machine's three memory buses (RAM_*, SDRAM_*, the 64 KB base RAM and everything
-- else) come out as ports and a8_mem answers them. The wrapper itself is not used (spec 5.7).
-- XL/XE mode only (Q4), PAL/NTSC by `pal`, one POKEY (Q8), no VBXE (Q7), no PBI, no freezer cartridge.
-- Every clocked process runs on CE28 (gen/, D3): with CE28 low the whole machine stands still.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity a8_machine is
  port (
    CE28    : in  std_logic;
    CLK     : in  std_logic;
    RESET_N : in  std_logic;

    -- configuration (quasi static, sys_clk)
    pal            : in  std_logic;
    ram_select     : in  std_logic_vector(2 downto 0);
    cart1_select   : in  std_logic_vector(7 downto 0);
    cart2_select   : in  std_logic_vector(7 downto 0);
    xex_loader     : in  std_logic;
    throttle       : in  std_logic_vector(5 downto 0);
    reset_rnmi     : in  std_logic;                       -- the RESET key: ANTIC's reset NMI (warm start)

    -- input
    joy1_n         : in  std_logic_vector(4 downto 0);    -- FRLDU, 0 = pressed
    joy2_n         : in  std_logic_vector(4 downto 0);
    paddle0        : in  signed(7 downto 0);
    paddle1        : in  signed(7 downto 0);
    paddle2        : in  signed(7 downto 0);
    paddle3        : in  signed(7 downto 0);
    kb_scan        : out std_logic_vector(5 downto 0);
    kb_resp        : in  std_logic_vector(1 downto 0);
    consol_option  : in  std_logic;                       -- 1 = pressed (GTIA reads CONSOL as not CONSOL_IN)
    consol_select  : in  std_logic;
    consol_start   : in  std_logic;

    -- SIO (the register bridge of sio_handler is inside)
    sio_addr       : in  std_logic_vector(4 downto 0);
    sio_wdata      : in  std_logic_vector(7 downto 0);
    sio_en         : in  std_logic;
    sio_wr         : in  std_logic;
    sio_rdata      : out std_logic_vector(15 downto 0);

    -- picture and sound
    video_r        : out std_logic_vector(7 downto 0);
    video_g        : out std_logic_vector(7 downto 0);
    video_b        : out std_logic_vector(7 downto 0);
    video_vs       : out std_logic;
    video_hs       : out std_logic;
    hblank         : out std_logic;
    vblank         : out std_logic;
    pixce          : out std_logic;                       -- one pixel (7.09 MHz PAL) per pulse
    audio_l        : out std_logic_vector(15 downto 0);
    audio_r        : out std_logic_vector(15 downto 0);

    -- memory
    ram_addr       : out std_logic_vector(18 downto 0);
    ram_req        : out std_logic;
    ram_we         : out std_logic;
    ram_wdata      : out std_logic_vector(7 downto 0);
    ram_q          : in  std_logic_vector(7 downto 0);
    ram_done       : in  std_logic;
    sd_addr        : out std_logic_vector(24 downto 0);
    sd_req         : out std_logic;
    sd_we          : out std_logic;
    sd_q           : in  std_logic_vector(7 downto 0);
    sd_done        : in  std_logic;

    -- views for the registers and the benches
    bus_addr       : out std_logic_vector(15 downto 0);
    snoop          : out std_logic_vector(7 downto 0);   -- the byte the machine's bus reads (PBI_SNOOP_DATA)
    cpu_ready      : out std_logic;                       -- MEMORY_READY_CPU
    antic_ready    : out std_logic;                       -- MEMORY_READY_ANTIC
    enable_179     : out std_logic;                       -- ENABLE_179_EARLY: one pulse per machine cycle
    tap_sio        : out std_logic_vector(2 downto 0);    -- the SIO wire for the benches: 2 drive -> POKEY, 1 POKEY -> drive, 0 command line
    rdy_out        : out std_logic;
    nmi_n_out      : out std_logic;
    irq_n_out      : out std_logic);
end entity;

architecture trx of a8_machine is
  signal ca2_in, ca2_out, ca2_dir, cb2_in, cb2_out, cb2_dir : std_logic;
  signal porta_in, porta_out, porta_dir, portb_in, portb_out, portb_dir : std_logic_vector(7 downto 0);
  signal trig : std_logic_vector(3 downto 0);
  signal lightpen : std_logic;
  signal write_data : std_logic_vector(31 downto 0);
  signal sd_do : std_logic_vector(31 downto 0);
  signal ram_do16 : std_logic_vector(15 downto 0);
  signal pot_reset : std_logic;
  signal pot_in : std_logic_vector(7 downto 0);
  signal sio_rxd, sio_txd, sio_clkout, sio_command : std_logic;
  signal e179 : std_logic;
  signal snoop_i : std_logic_vector(31 downto 0);
begin
  snoop <= snoop_i(7 downto 0);
  -- PIA: SIO proceed/interrupt idle, the motor and command lines as outputs (the SIO bridge sees the command line)
  ca2_in <= ca2_out when ca2_dir = '1' else '1';
  cb2_in <= cb2_out when cb2_dir = '1' else '1';
  sio_command <= cb2_out;

  porta_in <= (not porta_dir or porta_out) and (joy2_n(3) & joy2_n(2) & joy2_n(1) & joy2_n(0) & joy1_n(3) & joy1_n(2) & joy1_n(1) & joy1_n(0));
  portb_in <= portb_out;                                  -- XL/XE: port B is the memory control register
  lightpen <= joy2_n(4) and joy1_n(4);
  trig     <= "11" & joy2_n(4) & joy1_n(4);

  -- four paddles (the XL has two ports), reverse as in the wrapper
  pots : for i in 0 to 3 generate
    signal pos : signed(7 downto 0);
  begin
    pos <= paddle0 when i = 0 else paddle1 when i = 1 else paddle2 when i = 2 else paddle3;
    pot : entity work.pot_from_signed
      generic map (cycle_length => 16, reverse => 1)
      port map (CE28 => CE28, CLK => CLK, RESET_N => RESET_N, ENABLED => '1', POT_RESET => pot_reset,
                POS => pos, POT_HIGH => pot_in(i));
  end generate;
  pot_in(7 downto 4) <= (others => '0');

  ram_do16 <= x"00" & ram_q;
  sd_do    <= x"000000" & sd_q;
  ram_wdata <= write_data(7 downto 0);

  machine : entity work.atari800core
    generic map (cycle_length => 16, video_bits => 8, palette => 1, low_memory => 0, sdram_start_bank => 4)
    port map (
      CE28 => CE28, CLK => CLK, RESET_N => RESET_N,
      VIDEO_VS => video_vs, VIDEO_HS => video_hs, VIDEO_CS => open,
      VIDEO_B => video_b, VIDEO_G => video_g, VIDEO_R => video_r,
      VIDEO_BLANK => open, VIDEO_PIXCE => pixce, VIDEO_BURST => open, VIDEO_START_OF_FIELD => open,
      VIDEO_ODD_LINE => open, HBLANK => hblank, VBLANK => vblank,
      interlace_field => open, interlace => open, interlace_enable => '0',
      POKEYMAX_CONFIG => (others => '0'), SIO_AUDIO => x"00",
      AUDIO_L => audio_l, AUDIO_R => audio_r,
      CA1_IN => '1', CB1_IN => '1', CA2_IN => ca2_in, CA2_OUT => ca2_out, CA2_DIR_OUT => ca2_dir,
      CB2_IN => cb2_in, CB2_OUT => cb2_out, CB2_DIR_OUT => cb2_dir,
      PORTA_IN => porta_in, PORTA_OUT => porta_out, PORTA_DIR_OUT => porta_dir,
      PORTB_IN => portb_in, PORTB_OUT => portb_out, PORTB_DIR_OUT => portb_dir,
      KEYBOARD_RESPONSE => kb_resp, KEYBOARD_SCAN => kb_scan,
      POT_IN => pot_in, POT_RESET => pot_reset,
      ENABLE_179_EARLY => e179,
      PBI_ADDR => bus_addr, PBI_WRITE_ENABLE => open, PBI_SNOOP_DATA => snoop_i, PBI_SNOOP_READY => open,
      PBI_WRITE_DATA => write_data,
      PBI_WIDTH_8bit_ACCESS => open, PBI_WIDTH_16bit_ACCESS => open, PBI_WIDTH_32bit_ACCESS => open,
      PBI_ROM_DO => x"FF", PBI_REQUEST => open, PBI_TAKEOVER => '0', PBI_RELEASE => '0',
      PBI_REQUEST_COMPLETE => '0', PBI_DISABLE => '1',
      CART_RD5 => '0', PBI_MPD_N => '1', PBI_IRQ_N => '1',
      SIO_RXD => sio_rxd, SIO_TXD => sio_txd, SIO_CLOCKIN_IN => '1', SIO_CLOCKIN_OUT => open,
      SIO_CLOCKIN_OE => open, SIO_CLOCKOUT => sio_clkout,
      CONSOL_OPTION => consol_option, CONSOL_SELECT => consol_select, CONSOL_START => consol_start,
      GTIA_TRIG => trig,
      ANTIC_LIGHTPEN => lightpen, ANTIC_REFRESH => open, ANTIC_TURBO => open, ANTIC_RNMI_N => not reset_rnmi,
      SDRAM_REQUEST => sd_req, SDRAM_REQUEST_COMPLETE => sd_done, SDRAM_READ_ENABLE => open,
      SDRAM_WRITE_ENABLE => sd_we, SDRAM_ADDR => sd_addr, SDRAM_DO => sd_do,
      RAM_ADDR => ram_addr, RAM_DO => ram_do16, RAM_REQUEST => ram_req, RAM_REQUEST_COMPLETE => ram_done,
      RAM_WRITE_ENABLE => ram_we,
      ROM_ADDR => open, ROM_DO => x"FF", ROM_REQUEST => open, ROM_REQUEST_COMPLETE => '1', ROM_WRITE_ENABLE => open,
      DMA_FETCH => '0', DMA_READ_ENABLE => '0', DMA_32BIT_WRITE_ENABLE => '0', DMA_16BIT_WRITE_ENABLE => '0',
      DMA_8BIT_WRITE_ENABLE => '0', DMA_ADDR => (others => '0'), DMA_WRITE_DATA => (others => '0'),
      MEMORY_READY_DMA => open,
      RAM_SELECT => ram_select, CART_EMULATION_SELECT => cart1_select, CART2_EMULATION_SELECT => cart2_select,
      EMU_FLASH_REQUEST => open, EMU_FLASH_SLAVE => open,
      PAL => pal, CLIP_SIDES => '0', GTIA_XCOLOR => '0', ROM_IN_RAM => '1',
      THROTTLE_COUNT_6502 => throttle, HALT => '0', freezer_enable => '0', freezer_activate => '0',
      ATARI800MODE => '0', ATARI800MODE_16K => '0', PBI_ROM_MODE => '0', XEX_LOADER_MODE => xex_loader,
      RTC => (others => '0'), CLK_CONF => "000",
      VBXE_SWITCH => '0', VBXE_REG_BASE => '0', VBXE_NTSC_FIX => '0', VBXE_TURBO => '0',
      VBXE_PALETTE_RGB => "000", VBXE_PALETTE_INDEX => x"00", VBXE_PALETTE_COLOR => "0000000",
      freezer_state_out => open, state_reg_out => open,
      memory_ready_antic_out => antic_ready, memory_ready_cpu_out => cpu_ready, shared_enable_out => open,
      nmi_n_out => nmi_n_out, irq_n_out => irq_n_out, rdy_out => rdy_out, AN_out => open);

  enable_179 <= e179;
  tap_sio <= sio_rxd & sio_txd & sio_command;

  -- the SIO bridge: Watson's sio_handler between POKEY's serial pins and the register window
  sio : entity work.sio_handler
    port map (CE28 => CE28, CLK => CLK, ADDR => sio_addr, CPU_DATA_IN => sio_wdata, EN => sio_en, WR_EN => sio_wr,
              RESET_N => RESET_N, POKEY_ENABLE => e179, SIO_DATA_IN => sio_rxd, SIO_COMMAND => sio_command,
              SIO_DATA_OUT => sio_txd, SIO_CLK_OUT => sio_clkout, DATA_OUT => sio_rdata);
end architecture;
