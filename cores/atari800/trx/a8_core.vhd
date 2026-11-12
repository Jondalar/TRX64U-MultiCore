-- SPDX-License-Identifier: GPL-3.0-or-later
-- TRX Atari 800 XL core (T88): Mark Watson's Atari800 machine (cores/atari800/rtl, gen) behind the host's layer-2 socket.
-- This entity is what cores/atari800/layer2_atari800.v wraps; it has no C64 part in it.
--
-- Clock (D3): sys_clk is the host's C64 clock (31.527778 MHz PAL); the machine's clock is sys_clk x 9/10 exactly (a8_nco):
-- `ce` is that enable, minus the stall of a DDR2 miss (a8_mem). Every flip-flop of the machine takes `ce` (gen/, tools/atari800_ce28.py),
-- so the pause is one gate: no ce, nothing moves.
-- Memory (D4): the 64 KB RAM and the ROMs are block RAM written by the app through the window (a8_win, a8_mem); RAM above 64 KB
-- and the cartridge image are in the host's DDR2 behind a cache (a8_mem, the real-time port c_*).
-- Picture (D5): the machine's pixels (RGB565) are written into a DDR2 frame buffer for the host's pixel layer (a8_pixw, the
-- general port e_*); the host's VIC-II raster input gets a dummy raster of the same size (the scaler locks to it; it never stops).
-- Sound (D6): the POKEY mix, averaged to 48 kHz (a8_audio). Input (D7): a8_keys, joysticks, RESTORE = the RESET key.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity a8_core is
  generic (ID_TRX2 : integer := 0);
  port (
    sys_clk    : in  std_logic;                     -- the machine (the host's C64 clock)
    sys_rst    : in  std_logic;
    cpu_clk    : in  std_logic;                     -- the app's bus (100 MHz)
    mclk       : in  std_logic;                     -- audio
    ui_clk     : in  std_logic;                     -- the host's DDR2 user clock
    ui_rst     : in  std_logic;
    reset_req  : in  std_logic;                     -- the app's machine reset (sys_clk)
    halt_req   : in  std_logic;                     -- the app's pause (sys_clk)
    halted     : out std_logic;
    running    : out std_logic;                     -- out of reset (case LED 1)

    keys       : in  std_logic_vector(63 downto 0);
    joy1       : in  std_logic_vector(4 downto 0);
    joy2       : in  std_logic_vector(4 downto 0);
    restore    : in  std_logic;

    dma_req_tgl  : in  std_logic;
    dma_addr     : in  std_logic_vector(15 downto 0);
    dma_we       : in  std_logic;
    dma_wdata    : in  std_logic_vector(7 downto 0);
    dma_rom      : in  std_logic;
    dma_done_tgl : out std_logic;
    dma_rdata    : out std_logic_vector(7 downto 0);
    dbg_busy     : in  std_logic;
    win_port_use   : out std_logic;
    win_port_addr  : out std_logic_vector(15 downto 0);
    win_port_rom   : out std_logic;
    win_port_we    : out std_logic;
    win_port_wdata : out std_logic_vector(7 downto 0);
    port_req   : in  std_logic;
    port_addr  : in  std_logic_vector(15 downto 0);
    port_rom   : in  std_logic;
    port_we    : in  std_logic;
    port_wdata : in  std_logic_vector(7 downto 0);
    ram_rdata  : out std_logic_vector(7 downto 0);

    pix_color  : out std_logic_vector(3 downto 0);
    pix_x      : out std_logic_vector(9 downto 0);
    pix_y      : out std_logic_vector(8 downto 0);
    audio_l    : out std_logic_vector(23 downto 0);
    audio_r    : out std_logic_vector(23 downto 0);

    cw_we      : in  std_logic;
    cw_re      : in  std_logic;
    cw_addr    : in  std_logic_vector(16 downto 0);
    cw_wdata   : in  std_logic_vector(7 downto 0);
    cw_rdata   : out std_logic_vector(7 downto 0);
    cw_ack     : out std_logic;
    cw_irq     : out std_logic;
    px_we      : out std_logic;
    px_re      : out std_logic;
    px_rdata   : in  std_logic_vector(7 downto 0);
    px_ev_vsync : in std_logic;
    px_ev_flip : in  std_logic;
    build      : in  std_logic_vector(31 downto 0);
    ddr_cal    : in  std_logic_vector(63 downto 0);
    dbg_re     : in  std_logic := '0';
    dbg_addr   : in  std_logic_vector(11 downto 0) := (others => '0');
    j_rdata    : out std_logic_vector(31 downto 0);
    j_rvalid   : out std_logic;

    -- the host's real-time DDR2 port c_* (ui_clk): the memory above 64 KB and the cartridge
    c_addr     : out std_logic_vector(26 downto 0);
    c_cmd      : out std_logic_vector(2 downto 0);
    c_en       : out std_logic;
    c_rdy      : in  std_logic;
    c_wdf_data : out std_logic_vector(127 downto 0);
    c_wdf_mask : out std_logic_vector(15 downto 0);
    c_wdf_wren : out std_logic;
    c_rd_data  : in  std_logic_vector(127 downto 0);
    c_rd_valid : in  std_logic;
    -- the host's general DDR2 port e_* (ui_clk): the frame writer
    e_addr     : out std_logic_vector(26 downto 0);
    e_cmd      : out std_logic_vector(2 downto 0);
    e_en       : out std_logic;
    e_rdy      : in  std_logic;
    e_wdf_data : out std_logic_vector(127 downto 0);
    e_wdf_mask : out std_logic_vector(15 downto 0);
    e_wdf_wren : out std_logic;

    -- the benches' view of the machine (the layer leaves them open)
    tap_ce     : out std_logic;
    tap_cpu    : out std_logic;                     -- MEMORY_READY_CPU
    tap_antic  : out std_logic;                     -- MEMORY_READY_ANTIC
    tap_addr   : out std_logic_vector(15 downto 0);
    tap_e179   : out std_logic;
    tap_hblank : out std_logic;
    tap_vblank : out std_logic;
    tap_pixce  : out std_logic;
    tap_rgb    : out std_logic_vector(23 downto 0);
    tap_reset_n : out std_logic;
    tap_snoop  : out std_logic_vector(7 downto 0);
    tap_wdata  : out std_logic_vector(7 downto 0);
    tap_we     : out std_logic;                     -- the machine's write: tap_snoop means nothing then
    tap_audio  : out std_logic_vector(15 downto 0);
    tap_halted : out std_logic;
    tap_sio    : out std_logic_vector(2 downto 0));  -- the SIO wire: 2 drive -> POKEY, 1 POKEY -> drive, 0 command line (bench only)
end entity;

architecture rtl of a8_core is
  attribute ASYNC_REG : string;
  -- ---------------------------------------------------------------- the app's settings (cpu_clk) into sys_clk
  signal ctrl, ctrl_s1, ctrl_s2         : std_logic_vector(7 downto 0) := (others => '0');
  signal flags, flags_s1, flags_s2      : std_logic_vector(7 downto 0) := (others => '0');
  signal ramsel, ramsel_s1, ramsel_s2   : std_logic_vector(2 downto 0) := (others => '0');
  signal cart1, cart1_s1, cart1_s2      : std_logic_vector(7 downto 0) := (others => '0');
  signal cart2, cart2_s1, cart2_s2      : std_logic_vector(7 downto 0) := (others => '0');
  signal thr, thr_s1, thr_s2            : std_logic_vector(5 downto 0) := (others => '0');
  signal romsel, romsel_s1, romsel_s2   : std_logic_vector(1 downto 0) := (others => '0');
  signal pd0, pd1, pd2, pd3             : std_logic_vector(7 downto 0);
  signal pd_s1, pd_s2                   : std_logic_vector(31 downto 0) := (others => '0');
  attribute ASYNC_REG of ctrl_s1, ctrl_s2, flags_s1, flags_s2, ramsel_s1, ramsel_s2, cart1_s1, cart1_s2, cart2_s1, cart2_s2,
    thr_s1, thr_s2, romsel_s1, romsel_s2, pd_s1, pd_s2 : signal is "TRUE";
  signal fb_base, fb_stride             : std_logic_vector(31 downto 0);
  signal sio_a_c                        : std_logic_vector(4 downto 0);
  signal sio_d_c                        : std_logic_vector(7 downto 0);
  signal sio_req_c, sio_wr_c            : std_logic;

  -- ---------------------------------------------------------------- reset and pause
  signal rst_m, rst_q                   : std_logic := '0';
  signal reset_n                        : std_logic;
  signal c_opt, c_sel, c_sta            : std_logic;
  signal sreset                         : std_logic := '1';       -- the machine's reset
  signal rcnt                           : unsigned(10 downto 0) := (others => '0');
  signal swp                            : unsigned(16 downto 0) := to_unsigned(65536, 17);
  signal clr_en                         : std_logic;
  signal ramsel_l                       : std_logic_vector(2 downto 0) := "000";
  signal pal_l                          : std_logic := '1';
  signal hold_req, hold_eff, held_i     : std_logic;
  signal hcnt                           : unsigned(5 downto 0) := (others => '0');
  signal ce_nco, ce, mem_stall          : std_logic;
  signal mem_quiet, pw_quiet            : std_logic;
  signal flush                          : std_logic;

  -- ---------------------------------------------------------------- the machine
  signal joy1_n, joy2_n                 : std_logic_vector(4 downto 0);
  signal kb_scan                        : std_logic_vector(5 downto 0);
  signal kb_resp                        : std_logic_vector(1 downto 0);
  signal k_pressed                      : std_logic_vector(63 downto 0);
  signal k_shift, k_ctrl, k_brk, k_start, k_select, k_option : std_logic;
  signal sio_addr                       : std_logic_vector(4 downto 0);
  signal sio_wdata                      : std_logic_vector(7 downto 0);
  signal sio_en, sio_wr                 : std_logic;
  signal sio_rdata                      : std_logic_vector(15 downto 0);
  signal vr, vg, vb                     : std_logic_vector(7 downto 0);
  signal hblank, vblank, pixce          : std_logic;
  signal vsync                          : std_logic;                -- GTIA's vertical sync: the frame of the picture window
  signal aud_l, aud_r                   : std_logic_vector(15 downto 0);
  signal ram_addr                       : std_logic_vector(18 downto 0);
  signal ram_req, ram_we, ram_done      : std_logic;
  signal ram_wdata, ram_q               : std_logic_vector(7 downto 0);
  signal sd_addr                        : std_logic_vector(24 downto 0);
  signal sd_req, sd_we, sd_done         : std_logic;
  signal sd_q                           : std_logic_vector(7 downto 0);
  signal bus_addr                       : std_logic_vector(15 downto 0);
  signal cpu_rdy, antic_rdy, e179       : std_logic;
  signal snoop_b                        : std_logic_vector(7 downto 0);

  -- ---------------------------------------------------------------- window
  signal w_wr, w_hit, w_rom             : std_logic;
  signal w_sel                          : std_logic_vector(1 downto 0);
  signal w_ai                           : std_logic_vector(15 downto 0);
  signal w_di, w_do, conf_do            : std_logic_vector(7 downto 0);
  signal req_q                          : std_logic := '0';
  signal rd_hold                        : std_logic_vector(7 downto 0) := (others => '0');
  signal clr_a                          : std_logic_vector(15 downto 0);
  signal st_hits, st_miss, st_wr, st_stall : std_logic_vector(31 downto 0);

  -- ---------------------------------------------------------------- SIO register bridge
  type sb_t is (SB_IDLE, SB_PULSE, SB_CAP);
  signal sb                             : sb_t := SB_IDLE;
  signal sreq_s                         : std_logic_vector(2 downto 0) := "000";
  attribute ASYNC_REG of sreq_s : signal is "TRUE";
  signal sdone_tgl                      : std_logic := '0';
  signal srd_q                          : std_logic_vector(14 downto 0) := (others => '0');
  signal sa_l                           : std_logic_vector(4 downto 0) := (others => '0');
  signal sd_l                           : std_logic_vector(7 downto 0) := (others => '0');
  signal sw_l                           : std_logic := '0';

  -- ---------------------------------------------------------------- picture, sound, registers
  signal rgb565                         : std_logic_vector(15 downto 0);
  signal dotst                          : std_logic;
  signal frame_tgl                      : std_logic;
  signal fb_latest, fb_seq              : std_logic_vector(7 downto 0);
  signal fb_over                        : std_logic_vector(15 downto 0);
  signal ft_s                           : std_logic_vector(2 downto 0) := "000";
  attribute ASYNC_REG of ft_s : signal is "TRUE";
  signal ev_frame                       : std_logic;
  signal frames                         : unsigned(15 downto 0) := (others => '0');
  signal vb_s                           : std_logic := '1';
  signal status                         : std_logic_vector(7 downto 0);
  signal dph                            : unsigned(1 downto 0) := (others => '0');
  signal dx, dy                         : unsigned(8 downto 0) := (others => '0');
  signal al, ar                         : signed(23 downto 0);
  signal ce_cnt, ln_cnt, ad_cnt         : unsigned(15 downto 0) := (others => '0');
  signal hb_q, hb_d, seen_run           : std_logic := '1';
  signal caddr_q                        : std_logic_vector(15 downto 0) := (others => '0');
  signal jd                             : std_logic_vector(31 downto 0);
begin
  -- ================================================================ the app's settings into sys_clk
  process (sys_clk)
  begin
    if rising_edge(sys_clk) then
      ctrl_s1 <= ctrl;     ctrl_s2 <= ctrl_s1;
      flags_s1 <= flags;   flags_s2 <= flags_s1;
      ramsel_s1 <= ramsel; ramsel_s2 <= ramsel_s1;
      cart1_s1 <= cart1;   cart1_s2 <= cart1_s1;
      cart2_s1 <= cart2;   cart2_s2 <= cart2_s1;
      thr_s1 <= thr;       thr_s2 <= thr_s1;
      romsel_s1 <= romsel; romsel_s2 <= romsel_s1;
      pd_s1 <= pd3 & pd2 & pd1 & pd0; pd_s2 <= pd_s1;
    end if;
  end process;

  -- ================================================================ reset: the RAM is swept first (a cold start), then the machine
  -- runs its reset for a few thousand ticks; RAM_SELECT and PAL are taken while the reset is asserted
  rst_m <= sys_rst or reset_req or ctrl_s2(0);
  flush <= rst_m or ctrl_s2(3);
  clr_en <= '1' when swp(16) = '0' else '0';
  clr_a  <= std_logic_vector(swp(15 downto 0));
  process (sys_clk)
  begin
    if rising_edge(sys_clk) then
      rst_q <= rst_m;
      if rst_m = '1' then
        ramsel_l <= ramsel_s2; pal_l <= flags_s2(1);
        sreset <= '1'; rcnt <= (others => '0');
        if rst_q = '0' and ctrl_s2(2) = '0' then swp <= (others => '0'); end if;   -- the sweep starts when the reset does
      else
        if swp(16) = '0' then
          sreset <= '1';
        elsif ce = '1' then
          if rcnt(10) = '1' then sreset <= '0'; else rcnt <= rcnt + 1; sreset <= '1'; end if;
        end if;
      end if;
      if swp(16) = '0' and not (rst_m = '1' and rst_q = '0' and ctrl_s2(2) = '0') then swp <= swp + 1; end if;
    end if;
  end process;
  running <= not sreset;
  reset_n <= not sreset;
  c_opt <= k_option; c_sel <= k_select; c_sta <= k_start;   -- GTIA reads CONSOL as not CONSOL_IN: 1 = pressed

  -- ================================================================ pause (Freeze): the whole machine stops between two ticks
  hold_req <= halt_req or ctrl_s2(1);
  hold_eff <= hold_req and not sreset and not rst_q;                -- a reset takes the hold away (the menu's Reset works while frozen)
  nco : entity work.a8_nco port map (clk => sys_clk, hold => hold_eff, en => ce_nco);
  ce <= ce_nco and not mem_stall;
  -- held: the hold has been on for 32 clocks with the DDR2 engine and the picture writer idle
  process (sys_clk)
  begin
    if rising_edge(sys_clk) then
      if hold_eff = '1' and mem_quiet = '1' and pw_quiet = '1' then
        if hcnt /= 32 then hcnt <= hcnt + 1; end if;
      else
        hcnt <= (others => '0');
      end if;
    end if;
  end process;
  -- `hcnt` is only cleared on the clock AFTER the hold goes away, so `hcnt = 32` alone would still
  -- say "halted" in the very clock the machine takes its first tick again. The hold itself is in the
  -- term, so halted falls with it (found by sim/machine/run_pause.sh: "a machine tick while halted",
  -- once per release). The rising edge still needs its 32 quiet clocks.
  held_i <= '1' when hcnt = 32 and hold_eff = '1' else '0';
  halted <= held_i;

  -- ================================================================ input
  swap : process (joy1, joy2, flags_s2)
  begin
    if flags_s2(0) = '1' then joy1_n <= not joy2; joy2_n <= not joy1;
    else joy1_n <= not joy1; joy2_n <= not joy2; end if;
  end process;
  keyx : entity work.a8_keys
    port map (keys => keys, pressed => k_pressed, shift => k_shift, ctrl => k_ctrl, brk => k_brk,
              cstart => k_start, cselect => k_select, coption => k_option);
  -- POKEY scans a counter n and asks for the key with code ~n; response(0) low = that key is down; response(1) low = the
  -- shift / control / break line of the scan block (00 break, 10 shift, 11 control)
  process (kb_scan, k_pressed, k_shift, k_ctrl, k_brk)
  begin
    kb_resp <= "11";
    if k_pressed(to_integer(unsigned(not kb_scan))) = '1' then kb_resp(0) <= '0'; end if;
    if kb_scan(5 downto 4) = "00" and k_brk = '1' then kb_resp(1) <= '0'; end if;
    if kb_scan(5 downto 4) = "10" and k_shift = '1' then kb_resp(1) <= '0'; end if;
    if kb_scan(5 downto 4) = "11" and k_ctrl = '1' then kb_resp(1) <= '0'; end if;
  end process;

  -- ================================================================ the machine
  machine : entity work.a8_machine
    port map (
      CE28 => ce, CLK => sys_clk, RESET_N => reset_n,
      pal => pal_l, ram_select => ramsel_l, cart1_select => cart1_s2, cart2_select => cart2_s2,
      xex_loader => flags_s2(2), throttle => thr_s2, reset_rnmi => restore,
      joy1_n => joy1_n, joy2_n => joy2_n,
      paddle0 => signed(pd_s2(7 downto 0)), paddle1 => signed(pd_s2(15 downto 8)),
      paddle2 => signed(pd_s2(23 downto 16)), paddle3 => signed(pd_s2(31 downto 24)),
      kb_scan => kb_scan, kb_resp => kb_resp,
      consol_option => c_opt, consol_select => c_sel, consol_start => c_sta,
      sio_addr => sio_addr, sio_wdata => sio_wdata, sio_en => sio_en, sio_wr => sio_wr, sio_rdata => sio_rdata,
      video_r => vr, video_g => vg, video_b => vb, video_vs => vsync, video_hs => open,
      hblank => hblank, vblank => vblank, pixce => pixce, audio_l => aud_l, audio_r => aud_r,
      ram_addr => ram_addr, ram_req => ram_req, ram_we => ram_we, ram_wdata => ram_wdata, ram_q => ram_q, ram_done => ram_done,
      sd_addr => sd_addr, sd_req => sd_req, sd_we => sd_we, sd_q => sd_q, sd_done => sd_done,
      bus_addr => bus_addr, snoop => snoop_b, cpu_ready => cpu_rdy, antic_ready => antic_rdy, enable_179 => e179, tap_sio => tap_sio,
      rdy_out => open, nmi_n_out => open, irq_n_out => open);

  mem : entity work.a8_mem
    port map (
      clk => sys_clk, rst => flush, tick => ce,
      ram_addr => ram_addr, ram_req => ram_req, ram_we => ram_we, ram_wdata => ram_wdata, ram_q => ram_q, ram_done => ram_done,
      sd_addr => sd_addr, sd_req => sd_req, sd_we => sd_we, sd_q => sd_q, sd_done => sd_done,
      stall => mem_stall, quiet => mem_quiet,
      w_wr => w_wr, w_hit => w_hit, w_rom => w_rom, w_sel => w_sel, w_ai => w_ai, w_di => w_di, w_do => w_do,
      clr_en => clr_en, clr_a => clr_a,
      ui_clk => ui_clk, ui_rst => ui_rst,
      c_addr => c_addr, c_cmd => c_cmd, c_en => c_en, c_rdy => c_rdy, c_wdf_data => c_wdf_data, c_wdf_mask => c_wdf_mask,
      c_wdf_wren => c_wdf_wren, c_rd_data => c_rd_data, c_rd_valid => c_rd_valid,
      st_hits => st_hits, st_miss => st_miss, st_wr => st_wr, st_stall => st_stall);

  -- ================================================================ the app's window and the JTAG port
  conf_do <= w_do;
  win : entity work.a8_win
    port map (clk => sys_clk,
              dma_req_tgl => dma_req_tgl, dma_addr => dma_addr, dma_we => dma_we, dma_wdata => dma_wdata,
              dma_rom => dma_rom, dma_done_tgl => dma_done_tgl, dma_rdata => dma_rdata, dbg_busy => dbg_busy,
              rom_sel => romsel_s2,
              port_use => win_port_use, port_addr => win_port_addr, port_rom => win_port_rom, port_we => win_port_we,
              port_wdata => win_port_wdata, port_rdata => conf_do,
              p_req => port_req, p_addr => port_addr, p_rom => port_rom, p_we => port_we, p_wdata => port_wdata,
              w_wr => w_wr, w_hit => w_hit, w_rom => w_rom, w_sel => w_sel, w_ai => w_ai, w_di => w_di);
  -- the read data is valid in the clock after the request and stays until the next request's
  process (sys_clk)
  begin
    if rising_edge(sys_clk) then
      req_q <= port_req;
      if req_q = '1' then rd_hold <= conf_do; end if;
    end if;
  end process;
  ram_rdata <= conf_do when req_q = '1' else rd_hold;

  -- ================================================================ the SIO register bridge: one handler access per request,
  -- the enable held until a machine tick has taken it
  process (sys_clk)
  begin
    if rising_edge(sys_clk) then
      sreq_s <= sreq_s(1 downto 0) & sio_req_c;
      case sb is
        when SB_IDLE =>
          if sreq_s(2) /= sreq_s(1) then
            sa_l <= sio_a_c; sd_l <= sio_d_c; sw_l <= sio_wr_c; sb <= SB_PULSE;
          end if;
        when SB_PULSE =>
          if ce = '1' then sb <= SB_CAP; end if;
        when SB_CAP =>
          srd_q <= sio_rdata(14 downto 0); sdone_tgl <= not sdone_tgl; sb <= SB_IDLE;
      end case;
    end if;
  end process;
  sio_addr  <= sa_l;
  sio_wdata <= sd_l;
  sio_en    <= '1' when sb = SB_PULSE and sw_l = '0' else '0';
  sio_wr    <= '1' when sb = SB_PULSE and sw_l = '1' else '0';

  -- ================================================================ picture
  rgb565 <= vr(7 downto 3) & vg(7 downto 2) & vb(7 downto 3);
  dotst  <= ce and pixce;
  pixw : entity work.a8_pixw
    port map (sys_clk => sys_clk, sys_rst => sys_rst, dotst => dotst, rgb565 => rgb565, hblank => hblank, vblank => vsync,
              quiet => pw_quiet, frame_tgl => frame_tgl, latest => fb_latest, seq => fb_seq, overruns => fb_over,
              fb_base => fb_base, fb_stride => fb_stride, ui_clk => ui_clk, ui_rst => ui_rst,
              e_addr => e_addr, e_cmd => e_cmd, e_en => e_en, e_rdy => e_rdy, e_wdf_data => e_wdf_data,
              e_wdf_mask => e_wdf_mask, e_wdf_wren => e_wdf_wren);

  -- the raster the host's scaler locks to: 504 dots of 4 clocks, 312 lines, colour 0; not tied to the machine, never stops
  process (sys_clk)
  begin
    if rising_edge(sys_clk) then
      dph <= dph + 1;
      if dph = 3 then
        if dx = 503 then
          dx <= (others => '0');
          if dy = 311 then dy <= (others => '0'); else dy <= dy + 1; end if;
        else
          dx <= dx + 1;
        end if;
      end if;
    end if;
  end process;
  pix_color <= "0000";
  pix_x     <= '0' & std_logic_vector(dx);
  pix_y     <= std_logic_vector(dy);

  -- ================================================================ sound
  audio : entity work.a8_audio
    port map (clk => sys_clk, a_in => signed(aud_l), mute => held_i, mclk => mclk, audio_l => al, audio_r => ar);
  audio_l <= std_logic_vector(al);
  audio_r <= std_logic_vector(ar);

  -- ================================================================ registers
  process (sys_clk)
  begin
    if rising_edge(sys_clk) then
      if ce = '1' then
        vb_s <= vblank;
        if vb_s = '1' and vblank = '0' then frames <= frames + 1; end if;
        ce_cnt <= ce_cnt + 1;
        hb_q <= hblank; hb_d <= hb_q;
        if hb_q = '0' and hb_d = '1' then ln_cnt <= ln_cnt + 1; end if;
      end if;
      caddr_q <= bus_addr;
      if caddr_q /= bus_addr then ad_cnt <= ad_cnt + 1; end if;
      if sreset = '0' then seen_run <= '1'; end if;
    end if;
  end process;
  -- the frame-done toggle into cpu_clk for the interrupt cause
  process (cpu_clk)
  begin
    if rising_edge(cpu_clk) then ft_s <= ft_s(1 downto 0) & frame_tgl; end if;
  end process;
  ev_frame <= ft_s(2) xor ft_s(1);
  status <= "0000" & (not mem_quiet) & pal_l & held_i & (rst_q or sreset);

  regs : entity work.a8_regs
    generic map (ID_TRX2 => ID_TRX2)
    port map (clk => cpu_clk, we => cw_we, re => cw_re, addr => cw_addr, wdata => cw_wdata, rdata => cw_rdata,
              ack => cw_ack, irq => cw_irq, build => build, ddr_cal => ddr_cal,
              px_we => px_we, px_re => px_re, px_rdata => px_rdata,
              ev_vsync => px_ev_vsync, ev_flip => px_ev_flip, ev_frame => ev_frame,
              ctrl => ctrl, ram_sel => ramsel, flags => flags, cart1 => cart1, cart2 => cart2, throttle => thr,
              rom_sel => romsel, fb_base => fb_base, fb_stride => fb_stride,
              paddle0 => pd0, paddle1 => pd1, paddle2 => pd2, paddle3 => pd3,
              sio_addr => sio_a_c, sio_wdata => sio_d_c, sio_req_tgl => sio_req_c, sio_wr => sio_wr_c,
              sio_done_tgl => sdone_tgl, sio_rdata => srd_q,
              status => status, frames => std_logic_vector(frames), cpuaddr => bus_addr,
              fb_latest => fb_latest, fb_seq => fb_seq, fb_over => fb_over,
              st_hits => st_hits, st_miss => st_miss, st_wr => st_wr, st_stall => st_stall);

  -- ================================================================ JTAG proof of life (trxdbg "trx 0 ..."), words:
  --  0 "A8XL"   1 status: b0 in reset, b1 held, b2 reset_req, b3 ctrl reset hold, b4 sys_rst, b5 halt_req, b6 ctrl halt, b7 left
  --  reset once   2 frames   3 bus address now   4 ticks counted   5 lines counted   6 address changes counted
  with dbg_addr(3 downto 0) select jd <=
    x"4C583841"                                                                                         when x"0",
    x"000000" & seen_run & ctrl_s2(1) & halt_req & sys_rst & ctrl_s2(0) & reset_req & held_i & (rst_q or sreset) when x"1",
    x"0000" & std_logic_vector(frames)                                                                  when x"2",
    x"0000" & bus_addr                                                                                  when x"3",
    x"0000" & std_logic_vector(ce_cnt)                                                                  when x"4",
    x"0000" & std_logic_vector(ln_cnt)                                                                  when x"5",
    x"0000" & std_logic_vector(ad_cnt)                                                                  when x"6",
    x"00000000"                                                                                         when others;
  j_rdata  <= jd;
  j_rvalid <= '1';

  -- ================================================================ taps for the benches
  tap_ce <= ce; tap_cpu <= cpu_rdy; tap_antic <= antic_rdy; tap_addr <= bus_addr; tap_e179 <= e179;
  tap_hblank <= hblank; tap_vblank <= vsync; tap_pixce <= pixce; tap_rgb <= vr & vg & vb; tap_reset_n <= not sreset;
  tap_snoop <= snoop_b; tap_wdata <= ram_wdata; tap_we <= ram_we; tap_audio <= aud_l;
  tap_halted <= held_i;
end architecture;
