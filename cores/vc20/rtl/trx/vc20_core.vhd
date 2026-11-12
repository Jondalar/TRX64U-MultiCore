-- TRX VC-20 core (T83): the VIC-20 machine of VIC20Nano (cores/vc20/rtl/src, MikeJ/WoS/Gideon
-- Zweijtzer/Mist, see NOTICE) behind the host's layer-2 socket.
-- This entity is what cores/vc20/layer2_vc20.v wraps; it has no C64 part in it.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vc20_core is
  generic (ID_TRX2 : integer := 0; MUT_PIXW : integer := 0);   -- MUT_PIXW: the bench's mutation (vc20_pixw), 0 in every build
  port (
    sys_clk    : in  std_logic;                     -- the machine (the host's C64 clock, 31.5278 MHz PAL)
    sys_rst    : in  std_logic;
    cpu_clk    : in  std_logic;                     -- the app's bus (100 MHz)
    mclk       : in  std_logic;                     -- audio
    reset_req  : in  std_logic;                     -- the app's machine reset (sys_clk)
    halt_req   : in  std_logic;                     -- the app's pause (sys_clk)
    halted     : out std_logic;
    running    : out std_logic;                     -- out of reset (case LED 1)
    cpu_tap    : out std_logic_vector(25 downto 0);  -- the benches' view of the 6502 bus (vic20.vhd o_tap); the layer leaves it open
    pix_tap    : out std_logic_vector(6 downto 0);   -- the benches' view of the 6561's picture: ce_pix, hblank, vblank, colour; the layer leaves it open
    fb_tap     : out std_logic_vector(31 downto 0);  -- the benches' view of the frame writer: overruns, seq, latest; the layer leaves it open

    keys       : in  std_logic_vector(63 downto 0);
    joy1       : in  std_logic_vector(4 downto 0);
    joy2       : in  std_logic_vector(4 downto 0);
    restore    : in  std_logic;

    iec_atn_i  : in  std_logic;                     -- the bus levels, 1 = high
    iec_clk_i  : in  std_logic;
    iec_data_i : in  std_logic;
    iec_atn_o  : out std_logic;                     -- 0 = the machine pulls the line low
    iec_clk_o  : out std_logic;
    iec_data_o : out std_logic;

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
    -- the host's JTAG window at 0x101000 (trxdbg "trx 0 ..."): word index in dbg_addr, read data
    -- with valid; the C64 module's pages are not here, these are this core's own debug words
    dbg_re     : in  std_logic := '0';
    dbg_addr   : in  std_logic_vector(11 downto 0) := (others => '0');
    j_rdata    : out std_logic_vector(31 downto 0);
    j_rvalid   : out std_logic;

    -- the host's general DDR2 port e_* (ui_clk): the frame writer (T119)
    ui_clk     : in  std_logic := '0';
    ui_rst     : in  std_logic := '0';
    e_addr     : out std_logic_vector(26 downto 0);
    e_cmd      : out std_logic_vector(2 downto 0);
    e_en       : out std_logic;
    e_rdy      : in  std_logic := '1';
    e_wdf_data : out std_logic_vector(127 downto 0);
    e_wdf_mask : out std_logic_vector(15 downto 0);
    e_wdf_wren : out std_logic
  );
end entity;

architecture rtl of vc20_core is
  signal ctrl, ctrl_s1, ctrl_s2 : std_logic_vector(7 downto 0) := (others => '0');
  signal ext, ext_s1, ext_s2    : std_logic_vector(4 downto 0) := (others => '0');
  signal ro, ro_s1, ro_s2       : std_logic_vector(4 downto 0) := (others => '0');
  attribute ASYNC_REG : string;
  attribute ASYNC_REG of ctrl_s1, ctrl_s2, ext_s1, ext_s2, ro_s1, ro_s2 : signal is "TRUE";

  signal rst_m, rst_q, hold_req, held : std_logic := '0';
  signal vic_rst_q : std_logic := '1';
  signal en8                    : std_logic;
  signal ce_pix, hblank, vblank : std_logic;
  signal colour                 : std_logic_vector(3 downto 0);
  signal vcnt                   : std_logic_vector(8 downto 0);
  signal vic_audio              : std_logic_vector(5 downto 0);
  signal caddr                  : std_logic_vector(15 downto 0);
  signal frame                  : std_logic;
  signal frames                 : unsigned(15 downto 0) := (others => '0');
  signal kb_out, kb_in          : std_logic_vector(7 downto 0);
  signal joy_n                  : std_logic_vector(3 downto 0);
  signal fire_n                 : std_logic;
  signal conf_wr                : std_logic;
  signal conf_ai                : std_logic_vector(15 downto 0);
  signal conf_di, conf_do       : std_logic_vector(7 downto 0);
  signal status                 : std_logic_vector(7 downto 0);
  signal atn_o, clk_o, data_o   : std_logic;
  signal ram_ext_i, ram_ro_i    : std_logic_vector(4 downto 0);
  signal al, ar                 : signed(23 downto 0);
  signal req_q                  : std_logic := '0';
  signal rd_hold                : std_logic_vector(7 downto 0) := (others => '0');
  signal ce_cnt, ln_cnt, ad_cnt : unsigned(15 downto 0) := (others => '0');
  signal hb_q, hb_d             : std_logic := '1';
  signal caddr_q                : std_logic_vector(15 downto 0) := (others => '0');
  signal seen_run               : std_logic := '0';
  signal jv                     : std_logic := '1';
  signal jd                     : std_logic_vector(31 downto 0);
  signal fb_base, fb_stride     : std_logic_vector(31 downto 0);
  signal fb_latest, fb_seq      : std_logic_vector(7 downto 0);
  signal fb_over                : std_logic_vector(15 downto 0);
  signal frame_tgl              : std_logic;
  signal ft_s                   : std_logic_vector(2 downto 0) := (others => '0');
  signal ev_frame               : std_logic;
  attribute ASYNC_REG of ft_s   : signal is "TRUE";
begin
  -- JTAG-readable proof of life, in sys_clk. Words (trxdbg trx 0 / 4 / 8 ...):
  --  0 "VC20"   1 status: b0 rst_q (the machine is in reset), b1 halted, b2 reset_req (the host: JTAG bit 0,
  --  the app's C64_RESET, a reset key), b3 ctrl reset hold, b4 sys_rst, b5 halt_req, b6 ctrl halt,
  --  b7 left reset at least once since the partial loaded   2 frames   3 CPU address (now)
  --  4 pixel enables counted   5 lines counted   6 CPU address changes counted
  process (sys_clk)
  begin
    if rising_edge(sys_clk) then
      if ce_pix = '1' then
        ce_cnt <= ce_cnt + 1;
        hb_q <= hblank; hb_d <= hb_q;
        if hb_q = '0' and hb_d = '1' then ln_cnt <= ln_cnt + 1; end if;
      end if;
      caddr_q <= caddr;
      if caddr_q /= caddr then ad_cnt <= ad_cnt + 1; end if;
      if rst_q = '0' then seen_run <= '1'; end if;
    end if;
  end process;
  -- combinational on the JTAG address, which stays put until the result is collected (a registered
  -- word came one read late on the board)
  with dbg_addr(3 downto 0) select jd <=
    x"56433230"                                                                              when x"0",
    x"000000" & seen_run & ctrl_s2(1) & halt_req & sys_rst & ctrl_s2(0) & reset_req & held & rst_q when x"1",
    x"0000" & std_logic_vector(frames)                                                       when x"2",
    x"0000" & caddr                                                                          when x"3",
    x"0000" & std_logic_vector(ce_cnt)                                                       when x"4",
    x"0000" & std_logic_vector(ln_cnt)                                                       when x"5",
    x"0000" & std_logic_vector(ad_cnt)                                                       when x"6",
    x"00000000"                                                                              when others;
  j_rdata <= jd;
  j_rvalid <= jv;

  -- the app's settings (cpu_clk) into sys_clk
  process (sys_clk)
  begin
    if rising_edge(sys_clk) then
      ctrl_s1 <= ctrl; ctrl_s2 <= ctrl_s1;
      ext_s1  <= ext;  ext_s2  <= ext_s1;
      ro_s1   <= ro;   ro_s2   <= ro_s1;
    end if;
  end process;

  rst_m <= sys_rst or reset_req or ctrl_s2(0);
  process (sys_clk)
  begin
    if rising_edge(sys_clk) then rst_q <= rst_m; vic_rst_q <= sys_rst; end if;
  end process;
  -- The pause (the app's C64_STOP, the host's halts, the register's ctrl bit 1) stops the 6502 and the VIAs at a bus
  -- cycle boundary (vic20.vhd) and mutes the sound. The clock enables, and with them the 6561 (picture, raster counters,
  -- pix_x/pix_y), keep running: the host's scaler holds its lock, and a resume continues where the machine stopped.
  hold_req <= halt_req or ctrl_s2(1);
  process (sys_clk)
  begin
    if rising_edge(sys_clk) then
      if frame = '1' then frames <= frames + 1; end if;
    end if;
  end process;
  halted <= held;
  pix_tap <= ce_pix & hblank & vblank & colour;
  fb_tap  <= fb_over & fb_seq & fb_latest;
  running <= not rst_q;
  status <= "000000" & held & rst_q;

  nco : entity work.vc20_nco port map (clk => sys_clk, rst => sys_rst, run => '1', en => en8);

  joy_n  <= not (joy1(3 downto 0) or joy2(3 downto 0));      -- 0 up, 1 down, 2 left, 3 right, low active
  fire_n <= not (joy1(4) or joy2(4));

  ram_ext_i <= ext_s2;
  ram_ro_i  <= ro_s2;

  machine : entity work.vic20
    port map (
      i_sysclk     => sys_clk,
      i_sysclk_en  => en8,
      i_reset      => rst_q,
      i_vic_reset  => vic_rst_q,
      i_hold       => hold_req,
      o_held       => held,
      o_tap        => cpu_tap,
      o_p2h        => open,
      o_caddr      => caddr,
      atn_o        => atn_o,
      clk_o        => clk_o,
      clk_i        => iec_clk_i,
      data_o       => data_o,
      data_i       => iec_data_i,
      i_joy        => joy_n,
      i_fire       => fire_n,
      i_potx       => x"FF",
      i_poty       => x"FF",
      i_ram_ext_ro => ram_ro_i,
      i_ram_ext    => ram_ext_i,
      i_extmem_en  => '0',
      o_extmem_sel => open,
      o_extmem_r_wn => open,
      o_extmem_addr => open,
      i_extmem_data => x"FF",
      o_extmem_data => open,
      o_io2_sel    => open,
      o_io3_sel    => open,
      o_blk123_sel => open,
      o_blk5_sel   => open,
      o_ram123_sel => open,
      o_ce_pix     => ce_pix,
      o_video_r    => open,
      o_video_g    => open,
      o_video_b    => open,
      o_hsync      => open,
      o_vsync      => open,
      o_hblank     => hblank,
      o_vblank     => vblank,
      i_center     => "00",
      i_pal        => '1',
      i_wide       => '0',
      keyboard_matrix_out => kb_out,
      keyboard_matrix_in  => kb_in,
      tape_play    => open,
      o_audio      => vic_audio,
      cass_write   => open,
      cass_read    => '1',
      cass_motor   => open,
      cass_sw      => '1',
      conf_clk     => sys_clk,
      conf_wr      => conf_wr,
      conf_ai      => conf_ai,
      conf_di      => conf_di,
      conf_do      => conf_do,
      i_restore    => restore,
      o_colour     => colour,
      o_vcnt       => vcnt,
      user_port_cb1_in  => '1',
      user_port_cb1_out => open,
      user_port_cb2_in  => '1',
      user_port_cb2_out => open,
      user_port_in      => x"FF",
      user_port_out     => open
    );

  iec_atn_o  <= atn_o;
  iec_clk_o  <= clk_o;
  iec_data_o <= data_o;

  keys_i : entity work.vc20_keys port map (keys => keys, col_out => kb_out, row_in => kb_in);

  video : entity work.vc20_video
    port map (clk => sys_clk, ena => ce_pix, hblank => hblank, vcnt => vcnt,
              pix_color => pix_color, pix_x => pix_x, pix_y => pix_y, frame => frame);

  -- the picture: RGB565 into the frame buffer of the host's pixel layer (T119)
  pixw : entity work.vc20_pixw
    generic map (MUT_PIXW => MUT_PIXW)
    port map (sys_clk => sys_clk, sys_rst => sys_rst, ce_pix => ce_pix, colour => colour, hblank => hblank, vblank => vblank,
              frame_tgl => frame_tgl, latest => fb_latest, seq => fb_seq, overruns => fb_over,
              fb_base => fb_base, fb_stride => fb_stride, ui_clk => ui_clk, ui_rst => ui_rst,
              e_addr => e_addr, e_cmd => e_cmd, e_en => e_en, e_rdy => e_rdy, e_wdf_data => e_wdf_data,
              e_wdf_mask => e_wdf_mask, e_wdf_wren => e_wdf_wren);
  -- the frame-done toggle into cpu_clk for the interrupt cause
  process (cpu_clk)
  begin
    if rising_edge(cpu_clk) then ft_s <= ft_s(1 downto 0) & frame_tgl; end if;
  end process;
  ev_frame <= ft_s(2) xor ft_s(1);

  audio : entity work.vc20_audio
    port map (clk => sys_clk, a_in => vic_audio, mute => held, mclk => mclk, audio_l => al, audio_r => ar);
  audio_l <= std_logic_vector(al);
  audio_r <= std_logic_vector(ar);

  win : entity work.vc20_win
    port map (clk => sys_clk,
              dma_req_tgl => dma_req_tgl, dma_addr => dma_addr, dma_we => dma_we, dma_wdata => dma_wdata,
              dma_rom => dma_rom, dma_done_tgl => dma_done_tgl, dma_rdata => dma_rdata,
              dbg_busy => dbg_busy,
              port_use => win_port_use, port_addr => win_port_addr, port_rom => win_port_rom,
              port_we => win_port_we, port_wdata => win_port_wdata, port_rdata => conf_do,
              p_req => port_req, p_addr => port_addr, p_rom => port_rom, p_we => port_we, p_wdata => port_wdata,
              conf_wr => conf_wr, conf_ai => conf_ai, conf_di => conf_di);
  -- the read data is valid in the clock after the request and stays until the next request's
  -- (c64_mem.v holds it the same way; the host samples it a clock or two later)
  process (sys_clk)
  begin
    if rising_edge(sys_clk) then
      req_q <= port_req;
      if req_q = '1' then rd_hold <= conf_do; end if;
    end if;
  end process;
  ram_rdata <= conf_do when req_q = '1' else rd_hold;

  regs : entity work.vc20_regs
    generic map (ID_TRX2 => ID_TRX2)
    port map (clk => cpu_clk, we => cw_we, re => cw_re, addr => cw_addr, wdata => cw_wdata,
              rdata => cw_rdata, ack => cw_ack, irq => cw_irq, build => build, ddr_cal => ddr_cal,
              px_we => px_we, px_re => px_re, px_rdata => px_rdata,
              ev_vsync => px_ev_vsync, ev_flip => px_ev_flip, ev_frame => ev_frame,
              ctrl => ctrl, ram_ext => ext, ram_ro => ro, fb_base => fb_base, fb_stride => fb_stride,
              status => status, frames => std_logic_vector(frames), cpuaddr => caddr,
              fb_latest => fb_latest, fb_seq => fb_seq, fb_over => fb_over);
end architecture;
