-- TRX Apple //e core (T89): the Enhanced Apple //e of NanoApple2 (cores/apple2/rtl/src; Szombathelyi, Voss, Edwards and
-- others, GPL v3, see rtl/NOTICE) behind the host's layer-2 socket. This entity is what
-- cores/apple2/layer2_apple2.v wraps; it has no C64 part in it.
--
-- Clock: the machine counts 14.318 MHz enables (a2_nco: 7 of 16 sys_clk of the host's NTSC mode) and runs on sys_clk. `cpu_clk` is
-- only the app's register bus (a2_regs); everything that crosses is a level (two flip-flops) or a toggle.
-- Memory: main RAM 64 KB, the system ROM, the character ROM, the Disk II boot ROM and the track buffers are inferred block RAM
-- (a2_ram) filled by the app through the DMA window (a2_win); the ROMs are not in the bitstream. The 64 KB of aux RAM is in the DDR2
-- behind a cache (a2_aux, T89 Q4) or, with AUX_DDR = 0, a block RAM too (the bench's reference).
-- Freeze: the machine's 14M enable stops (so does the accumulator that makes it), from any point: everything the machine has is in
-- registers that move only on that enable. A reset takes the hold away. The dummy raster for the host's scaler does not stop.
-- Picture: the machine's 1-bit video stream becomes a 16 colour pixel per 14M cycle (a2_pixc) and a frame buffer in the DDR2
-- (a2_pixw); the scaler gets a dummy VIC-II shaped raster (65 x 263 cycles of 8 dots, colour 0).
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity a2_core is
  generic (
    AUX_DDR    : integer := 0;
    MOUSE      : integer := 1;             -- the AppleMouse card (its 6805 is Verilog: GHDL cannot take it, the VHDL benches set 0)
    COLD_CLKS  : integer := 4194304;       -- machine cycles (14M) the power-on reset lasts: 0.29 s, the Nano's flash_clk(22)
    WARM_CLKS  : integer := 1024;
    PIX_X0     : integer := 16;            -- hcount of the first pixel of the picture (tb_a2_video / check_video.py: the text screen is predicted dot by dot)
    REPEAT_DELAY : integer := 534000;      -- keyboard auto repeat, machine cycles (1 MHz): to the first repeat, between repeats
    REPEAT_RATE  : integer := 91000;
    HD_HOLD_CLKS : integer := 1600000      -- sys_clk clocks the hard disk request is held for the case LED: 50 ms
  );
  port (
    sys_clk    : in  std_logic;
    sys_rst    : in  std_logic;
    cpu_clk    : in  std_logic;
    mclk       : in  std_logic;
    ui_clk     : in  std_logic;
    ui_rst     : in  std_logic;
    reset_req  : in  std_logic;                     -- the app's machine reset (sys_clk): a cold reset
    halt_req   : in  std_logic;                     -- the app's pause (sys_clk)
    halted     : out std_logic;
    running    : out std_logic;

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

    -- the host's general DDR2 port e_* (ui_clk): the frame writer
    e_addr     : out std_logic_vector(26 downto 0);
    e_cmd      : out std_logic_vector(2 downto 0);
    e_en       : out std_logic;
    e_rdy      : in  std_logic;
    e_wdf_data : out std_logic_vector(127 downto 0);
    e_wdf_mask : out std_logic_vector(15 downto 0);
    e_wdf_wren : out std_logic;
    -- the host's real-time DDR2 port c_* (ui_clk): the aux RAM
    c_addr     : out std_logic_vector(26 downto 0);
    c_cmd      : out std_logic_vector(2 downto 0);
    c_en       : out std_logic;
    c_rdy      : in  std_logic;
    c_wdf_data : out std_logic_vector(127 downto 0);
    c_wdf_mask : out std_logic_vector(15 downto 0);
    c_wdf_wren : out std_logic;
    c_rd_data  : in  std_logic_vector(127 downto 0);
    c_rd_valid : in  std_logic;

    -- T159 D-all: the case LED 1 source (sys_clk): a Disk II motor runs (D*_ACTIVE, with the drive's 1 s spin-down) or the hard disk card has a request open (held 50 ms); drv_hd = the hard disk part alone
    drv_led    : out std_logic;
    drv_hd     : out std_logic;

    -- the bench's view
    dbg_ce_cnt : out std_logic_vector(31 downto 0);
    dbg_sig    : out std_logic_vector(31 downto 0);
    -- the bench's probes: 0 enable, 1 PHI0, 2 HBL, 3 VBL, 4 PHI0 rising next, 5 PDL strobe, 6 the next enable toggles Q3, 7 speaker, 8 video, 9 COL80, 10 any key down, 11 keystrobe, 15:12 reserved, 23:16 game port
    dbg_probe  : out std_logic_vector(23 downto 0)
  );
end entity;

architecture rtl of a2_core is
  attribute ASYNC_REG : string;
  -- ---------------------------------------------------------------- the app's settings (cpu_clk), and in sys_clk
  signal ctrl_c, cfg_c, dctl_c, mdx_c, mdy_c, dcfg_c, hctl_c, hres_c : std_logic_vector(7 downto 0);
  signal cmd_c    : std_logic_vector(9 downto 0);
  signal bits1_c, bits2_c, hsz1_c, hsz2_c : std_logic_vector(15 downto 0);
  signal trkfl_c  : std_logic_vector(1 downto 0);
  signal sel_c    : std_logic_vector(3 downto 0);
  signal pal_c    : std_logic_vector(383 downto 0);
  signal abase_c, fbase_c, fstride_c : std_logic_vector(31 downto 0);
  signal cold_c, mbtn_c, caps_c : std_logic;
  signal caps_s1, caps_s2 : std_logic := '1';
  signal inj_c    : std_logic_vector(6 downto 0);
  signal ctrl_s1, ctrl_s2, cfg_s1, cfg_s2, dctl_s1, dctl_s2 : std_logic_vector(7 downto 0) := (others => '0');
  signal sel_s1, sel_s2 : std_logic_vector(3 downto 0) := (others => '0');
  signal pal_s1, pal_s2 : std_logic_vector(383 downto 0) := (others => '0');
  signal abase_s1, abase_s2 : std_logic_vector(31 downto 0) := (others => '0');
  signal tgl_s1, tgl_s2, tgl_s3 : std_logic_vector(10 downto 0) := (others => '0');
  signal dcfg_s1, dcfg_s2, hctl_s1, hctl_s2, hres_s1, hres_s2 : std_logic_vector(7 downto 0) := (others => '0');
  signal bits1_s1, bits1_s2, bits2_s1, bits2_s2, hsz1_s1, hsz1_s2, hsz2_s1, hsz2_s2 : std_logic_vector(15 downto 0) := (others => '0');
  signal trkfl_s1, trkfl_s2 : std_logic_vector(1 downto 0) := (others => '0');
  signal inj_s, inj_m : std_logic_vector(6 downto 0) := (others => '0');
  attribute ASYNC_REG of ctrl_s1, cfg_s1, caps_s1, sel_s1, dctl_s1, pal_s1, abase_s1, tgl_s1, tgl_s2, dcfg_s1, hctl_s1, hres_s1, bits1_s1, bits2_s1, hsz1_s1, hsz2_s1, trkfl_s1 : signal is "TRUE";
  signal chg_p, fl_p, ack_p : std_logic_vector(1 downto 0);
  signal inj_p, cold_req, hdd_ack_p, lat_clr : std_logic;
  -- ---------------------------------------------------------------- clock enable, reset, pause
  signal ce, run, stall   : std_logic;
  signal hold_req, hold, held, held_q : std_logic := '0';
  signal m_reset, rst_m   : std_logic;
  signal rst_q            : std_logic := '1';
  signal cold_active      : std_logic := '1';
  signal cold_cnt         : integer range 0 to 8388607 := 0;
  signal warm_cnt         : integer range 0 to 1048575 := 0;
  signal restore_q        : std_logic := '0';
  signal ce_cnt           : unsigned(31 downto 0) := (others => '0');
  signal sig              : std_logic_vector(31 downto 0) := x"FFFFFFFF";
  -- ---------------------------------------------------------------- the machine
  signal clk_2m, q3_rise, phase_zero, phase_zero_r, phase_zero_f : std_logic;
  signal cpu_addr, ram_addr : unsigned(15 downto 0);
  signal cpu_d     : unsigned(7 downto 0);
  signal ram_do    : unsigned(15 downto 0);
  signal aux       : std_logic;
  signal pd        : unsigned(7 downto 0);
  signal cpu_we_m, ram_we : std_logic;
  signal video, color_line, text_mode, hbl, vbl : std_logic;
  signal k_m       : unsigned(7 downto 0);
  signal keystrobe, akd : std_logic;
  signal an        : std_logic_vector(3 downto 0);
  signal gameport  : std_logic_vector(7 downto 0);
  signal pdl_strobe, stb : std_logic;
  signal io_select, device_select : std_logic_vector(7 downto 0);
  signal rom_addr  : unsigned(13 downto 0);
  signal rom_out   : unsigned(7 downto 0);
  signal vrom_addr : std_logic_vector(12 downto 0);
  signal vrom_out  : unsigned(7 downto 0);
  signal float_bus : unsigned(7 downto 0);
  signal cpu_ram_sel, cpu_ram_wr : std_logic;
  signal cpu_addr_ram, vid_addr : unsigned(15 downto 0);
  signal q3_rise_d : std_logic := '0';
  signal col80, altchar, hires_m, page2, mixed, store80, dhires : std_logic;
  signal speaker   : std_logic;
  signal flash_cnt : unsigned(22 downto 0) := (others => '0');
  signal irq_n_m   : std_logic;
  signal samp_en   : std_logic;
  signal a_main_we, a_aux_we : std_logic;
  signal a_main_addr : std_logic_vector(15 downto 0);
  signal a_main_din  : std_logic_vector(7 downto 0);
  signal ram_main_q, ram_aux_q : std_logic_vector(7 downto 0);
  -- ---------------------------------------------------------------- the Disk II
  signal disk_do   : unsigned(7 downto 0);
  signal d_ready, d_busy : std_logic_vector(1 downto 0);
  signal d1_act, d2_act : std_logic;
  signal trk1, trk2 : unsigned(7 downto 0);
  signal t1_addr, t2_addr : unsigned(12 downto 0);
  signal t1_di, t2_di, t1_do, t2_do : unsigned(7 downto 0);
  signal t1_we, t2_we, t1_busy, t2_busy : std_logic;
  signal boot_addr : unsigned(7 downto 0);
  signal boot_q : std_logic_vector(7 downto 0);
  signal tr_req, tr_save, tr_dirty, tr_valid : std_logic_vector(1 downto 0);
  type trk_t is array (0 to 1) of unsigned(7 downto 0);
  signal tr_trk    : trk_t;
  signal trk_ev_tgl : std_logic := '0';
  signal tr_req_d  : std_logic_vector(1 downto 0) := "00";
  signal bit_wr    : std_logic_vector(1 downto 0);
  signal bc1, bc2  : unsigned(15 downto 0);
  signal d_noise   : std_logic_vector(1 downto 0);
  signal d_mounted : std_logic_vector(1 downto 0);
  signal lat1, lat2 : unsigned(31 downto 0);
  signal reqs1, reqs2 : unsigned(15 downto 0);
  signal starved, cell_noise, cpu_step, cpu_wait_h : std_logic;
  signal starved_c : unsigned(15 downto 0) := (others => '0');
  signal noise_c   : unsigned(31 downto 0) := (others => '0');
  -- the slot 7 card
  signal hdd_do    : unsigned(7 downto 0);
  signal hrom_addr : unsigned(7 downto 0);
  signal hrom_q    : std_logic_vector(7 downto 0);
  signal hrom_u    : unsigned(7 downto 0);
  signal h_req, h_wr, h_unit, h_b1, hram_b1 : std_logic;
  signal h_blk, h_mem : std_logic_vector(15 downto 0);
  signal h_cmds    : unsigned(15 downto 0);
  signal h_req_d   : std_logic := '0';
  signal hd_hold   : integer range 0 to HD_HOLD_CLKS := 0;
  signal hd_led    : std_logic;
  signal hdd_ev_tgl : std_logic := '0';
  signal hd_t_s1, hd_t_s2 : std_logic_vector(2 downto 0) := (others => '0');
  signal ev_hdd    : std_logic;
  signal b_hrom_q  : std_logic_vector(7 downto 0);
  signal wb_hrom   : std_logic;
  -- ---------------------------------------------------------------- keyboard, game, picture
  signal k_code    : std_logic_vector(7 downto 0);
  signal open_apple, closed_apple, caps_l : std_logic;
  signal q_keys    : std_logic_vector(4 downto 0);
  signal pv, ls, fs : std_logic;
  signal rgb       : std_logic_vector(15 downto 0);
  signal lrow      : unsigned(7 downto 0);
  signal hq        : unsigned(10 downto 0);
  signal pw_quiet, frame_tgl : std_logic;
  signal fb_latest, fb_seq : std_logic_vector(7 downto 0);
  signal fb_over   : std_logic_vector(15 downto 0);
  signal frames    : unsigned(15 downto 0) := (others => '0');
  signal vb_d      : std_logic := '1';
  signal ft_c      : std_logic_vector(2 downto 0) := (others => '0');
  signal tt_c      : std_logic_vector(2 downto 0) := (others => '0');
  attribute ASYNC_REG of ft_c, tt_c : signal is "TRUE";
  signal ev_frame, ev_track : std_logic;
  signal pal_mono, pal_sel : std_logic_vector(1 downto 0);
  -- ---------------------------------------------------------------- the window
  signal t_start, t_we, t_rom, t_done : std_logic;
  signal t_sel     : std_logic_vector(3 downto 0);
  signal t_addr    : std_logic_vector(15 downto 0);
  signal t_wdata, t_rdata : std_logic_vector(7 downto 0);
  signal tq        : std_logic_vector(4 downto 0) := (others => '0');
  signal t_done_r  : std_logic := '0';
  signal b_main_q, b_rom_q, b_vrom_q, b_boot_q, b_t1_q, b_t2_q, b_aux_q : std_logic_vector(7 downto 0);
  signal wb_main, wb_rom, wb_vrom, wb_boot, wb_t1, wb_t2, wb_aux : std_logic;
  signal win_last  : std_logic_vector(7 downto 0);
  -- ---------------------------------------------------------------- registers
  signal sysv      : std_logic_vector(511 downto 0) := (others => '0');
  signal aux_miss, aux_stall : std_logic_vector(31 downto 0) := (others => '0');
  signal aux_wait  : std_logic := '0';
  signal status8   : std_logic_vector(7 downto 0);
  signal dph       : unsigned(1 downto 0) := (others => '0');
  signal dx        : unsigned(9 downto 0) := (others => '0');
  signal dyy       : unsigned(8 downto 0) := (others => '0');
  signal jd        : std_logic_vector(31 downto 0);
  signal rom_q, vrom_q, t1_q, t2_q : std_logic_vector(7 downto 0);
  signal mouse_do : unsigned(7 downto 0);
  signal mouse_oe, mouse_irq_n, mouse_p : std_logic;
  signal mstb, ce_f : std_logic := '0';
  signal mrom_a, mmcu_a : std_logic_vector(10 downto 0);
  signal mrom_q, mmcu_q, b_mrom_q, b_mmcu_q : std_logic_vector(7 downto 0);
  signal wb_mrom, wb_mmcu : std_logic;
  signal mdx_s1, mdx_s2, mdy_s1, mdy_s2 : std_logic_vector(7 downto 0) := (others => '0');
  signal mbtn_s1, mbtn_s2 : std_logic := '0';
  signal boot_u    : unsigned(7 downto 0);
  signal au_l, au_r : signed(23 downto 0);
  signal px_strobe : std_logic;
  signal aux_done  : std_logic;
  signal aux_w_start, is_aux_q : std_logic;
  -- the mouse card as a component: GHDL cannot analyse its Verilog 6805, and with MOUSE = 0 nothing needs the unit
  component applemouse is
    port (
      CLK_14M, CE_14M, CE_F, CE_2M, PHASE_ZERO, IO_SELECT, IO_STROBE, DEVICE_SELECT, RESET : in std_logic;
      A : in unsigned(15 downto 0); D_IN : in unsigned(7 downto 0); D_OUT : out unsigned(7 downto 0);
      RNW : in std_logic; OE : out std_logic; IRQ_N : out std_logic;
      STROBE : in std_logic; X : in signed(8 downto 0); Y : in signed(8 downto 0); BUTTON : in std_logic;
      CARD_ROM_A : out std_logic_vector(10 downto 0); CARD_ROM_D : in std_logic_vector(7 downto 0);
      MCU_ROM_A : out std_logic_vector(10 downto 0); MCU_ROM_D : in std_logic_vector(7 downto 0));
  end component;
begin
  -- ================================================================ the app's settings into sys_clk
  process (sys_clk)
  begin
    if rising_edge(sys_clk) then
      ctrl_s1 <= ctrl_c; ctrl_s2 <= ctrl_s1;
      cfg_s1 <= cfg_c; cfg_s2 <= cfg_s1;
      caps_s1 <= caps_c; caps_s2 <= caps_s1;
      sel_s1 <= sel_c; sel_s2 <= sel_s1;
      dctl_s1 <= dctl_c; dctl_s2 <= dctl_s1;
      pal_s1 <= pal_c; pal_s2 <= pal_s1;
      abase_s1 <= abase_c; abase_s2 <= abase_s1;
      tgl_s1 <= cold_c & cmd_c; tgl_s2 <= tgl_s1; tgl_s3 <= tgl_s2;
      dcfg_s1 <= dcfg_c; dcfg_s2 <= dcfg_s1; hctl_s1 <= hctl_c; hctl_s2 <= hctl_s1; hres_s1 <= hres_c; hres_s2 <= hres_s1;
      bits1_s1 <= bits1_c; bits1_s2 <= bits1_s1; bits2_s1 <= bits2_c; bits2_s2 <= bits2_s1;
      hsz1_s1 <= hsz1_c; hsz1_s2 <= hsz1_s1; hsz2_s1 <= hsz2_c; hsz2_s2 <= hsz2_s1; trkfl_s1 <= trkfl_c; trkfl_s2 <= trkfl_s1;
      inj_m <= inj_c; inj_s <= inj_m;                 -- two stages, as the toggle: the code that goes with a pulse is the one written with its toggle
      mdx_s1 <= mdx_c; mdx_s2 <= mdx_s1; mdy_s1 <= mdy_c; mdy_s2 <= mdy_s1; mbtn_s1 <= mbtn_c; mbtn_s2 <= mbtn_s1;
      ce_f <= ce;
      if mouse_p = '1' then mstb <= '1'; elsif ce = '1' then mstb <= '0'; end if;
    end if;
  end process;
  chg_p(0) <= tgl_s3(0) xor tgl_s2(0);
  chg_p(1) <= tgl_s3(1) xor tgl_s2(1);
  fl_p(0)  <= tgl_s3(2) xor tgl_s2(2);
  fl_p(1)  <= tgl_s3(3) xor tgl_s2(3);
  ack_p(0) <= tgl_s3(4) xor tgl_s2(4);
  ack_p(1) <= tgl_s3(5) xor tgl_s2(5);
  inj_p    <= tgl_s3(6) xor tgl_s2(6);
  cold_req <= tgl_s3(10) xor tgl_s2(10);
  hdd_ack_p <= tgl_s3(8) xor tgl_s2(8);
  lat_clr  <= tgl_s3(9) xor tgl_s2(9);
  mouse_p  <= tgl_s3(7) xor tgl_s2(7);
  pal_mono <= cfg_s2(1 downto 0);
  pal_sel  <= cfg_s2(3 downto 2);

  -- ================================================================ reset, pause, enable
  hold_req <= halt_req or ctrl_s2(1);
  m_reset  <= cold_active or '1' when warm_cnt /= 0 else cold_active;
  hold     <= hold_req and not m_reset;
  run      <= not hold and not stall;
  nco : entity work.a2_nco port map (clk => sys_clk, rst => sys_rst, run => run, en => ce);

  process (sys_clk)
  begin
    if rising_edge(sys_clk) then
      rst_m <= m_reset;
      restore_q <= restore;
      held_q <= hold;
      held <= held_q;
      if sys_rst = '1' or reset_req = '1' or cold_req = '1' then
        cold_active <= '1'; cold_cnt <= 0;
      elsif ce = '1' and cold_active = '1' then
        if cold_cnt >= COLD_CLKS then cold_active <= '0'; else cold_cnt <= cold_cnt + 1; end if;
      end if;
      if ce = '1' then
        q3_rise_d <= q3_rise;
        ce_cnt <= ce_cnt + 1;
        if restore_q = '1' or ctrl_s2(0) = '1' then
          warm_cnt <= WARM_CLKS;
        elsif warm_cnt /= 0 then
          warm_cnt <= warm_cnt - 1;
        end if;
        -- the machine's trace signature: what it did with each enable (a bench compares two machines by it)
        sig <= (sig(30 downto 0) & (sig(31) xor sig(21) xor sig(1) xor sig(0))) xor
               (std_logic_vector(cpu_addr) & std_logic_vector(cpu_d) & cpu_we_m & speaker & video & akd & phase_zero & "000");
        flash_cnt <= flash_cnt + 1;
      end if;
    end if;
  end process;
  halted  <= held and pw_quiet;
  running <= not rst_m;
  dbg_ce_cnt <= std_logic_vector(ce_cnt);
  dbg_sig <= sig;
  dbg_probe <= gameport & "0000" & keystrobe & akd & col80 & video & speaker & q3_rise & pdl_strobe & phase_zero_r & vbl & hbl & phase_zero & ce;

  -- ================================================================ the machine
  k_m <= unsigned(k_code);
  pd  <= disk_do when (io_select(6) or device_select(6)) = '1' else hdd_do when (io_select(7) or device_select(7)) = '1' else mouse_do when mouse_oe = '1' else float_bus;
  irq_n_m <= mouse_irq_n;
  machine : entity work.apple2
    port map (
      CLK_14M => sys_clk, CE_14M => ce, PALMODE => '0', ROMSWITCH => cfg_s2(5),
      CLK_2M => clk_2m, Q3_RISE => q3_rise, CPU_WAIT => cpu_wait_h, CPU_STEP => cpu_step,
      PHASE_ZERO => phase_zero, PHASE_ZERO_R => phase_zero_r, PHASE_ZERO_F => phase_zero_f,
      FLASH_CLK => flash_cnt(22), reset => rst_m,
      ADDR => cpu_addr, ram_addr => ram_addr, D => cpu_d, ram_do => ram_do, aux => aux,
      PD => pd, CPU_WE => cpu_we_m, IRQ_n => irq_n_m, NMI_n => '1', ram_we => ram_we,
      VIDEO => video, COLOR_LINE => color_line, TEXT_MODE => text_mode, HBL => hbl, VBL => vbl,
      K => k_m, KEYSTROBE => keystrobe, AKD => akd, AN => an, GAMEPORT => gameport,
      PDL_STROBE => pdl_strobe, STB => stb, IO_SELECT => io_select, DEVICE_SELECT => device_select,
      rom_addr => rom_addr, rom_out => rom_out, video_rom_addr => vrom_addr, video_rom_out => vrom_out,
      FLOAT_BUS => float_bus, CPU_RAM_SEL => cpu_ram_sel, CPU_ADDR_RAM => cpu_addr_ram, VIDEO_ADDR => vid_addr, CPU_RAM_WR => cpu_ram_wr,
      COL80_O => col80, ALTCHAR_O => altchar, HIRES_O => hires_m, PAGE2_O => page2, MIXED_O => mixed,
      STORE80_O => store80, DHIRES_O => dhires, HRAM_BANK1_O => hram_b1, speaker => speaker);

  -- ================================================================ memories (port A: the machine, port B: the window)
  -- main RAM and aux RAM take the address of the new phase one enable after the machine toggles Q3 / PHI0 (the SDRAM of NanoApple2 starts its cycle after the
  -- rising edge of CLK_2M; the video latch takes the data at the end of the scanner's phase, the CPU at the start of the next); the ROMs and the
  -- track buffers on every enable (their Gowin blocks were clocked by CLK_14M)
  samp_en <= ce and q3_rise_d;
  -- a cold reset writes the power-up byte ($03F4 = 0) at every sample: the monitor then goes to the disk boot routine (the Nano does it)
  a_main_addr <= x"03F4" when cold_active = '1' else std_logic_vector(ram_addr);
  a_main_din  <= x"00" when cold_active = '1' else std_logic_vector(cpu_d);
  a_main_we   <= cold_active or (ram_we and not aux);
  a_aux_we    <= ram_we and aux;
  ram_do      <= unsigned(ram_aux_q) & unsigned(ram_main_q);

  ram_main : entity work.a2_ram generic map (AW => 16, DW => 8)
    port map (clk => sys_clk, a_en => samp_en, a_we => a_main_we, a_addr => a_main_addr, a_din => a_main_din, a_dout => ram_main_q,
              b_we => wb_main, b_addr => t_addr, b_din => t_wdata, b_dout => b_main_q);
  sysrom : entity work.a2_ram generic map (AW => 14, DW => 8)
    port map (clk => sys_clk, a_en => ce, a_we => '0', a_addr => std_logic_vector(rom_addr), a_din => x"00", a_dout => rom_q,
              b_we => wb_rom, b_addr => t_addr(13 downto 0), b_din => t_wdata, b_dout => b_rom_q);
  rom_out <= unsigned(rom_q);
  chrrom : entity work.a2_ram generic map (AW => 13, DW => 8)
    port map (clk => sys_clk, a_en => ce, a_we => '0', a_addr => vrom_addr, a_din => x"00", a_dout => vrom_q,
              b_we => wb_vrom, b_addr => t_addr(12 downto 0), b_din => t_wdata, b_dout => b_vrom_q);
  vrom_out <= unsigned(vrom_q);
  bootrom : entity work.a2_ram generic map (AW => 8, DW => 8)
    port map (clk => sys_clk, a_en => ce, a_we => '0', a_addr => std_logic_vector(boot_addr), a_din => x"00", a_dout => boot_q,
              b_we => wb_boot, b_addr => t_addr(7 downto 0), b_din => t_wdata, b_dout => b_boot_q);

  trkbuf1 : entity work.a2_ram generic map (AW => 13, DW => 8)
    port map (clk => sys_clk, a_en => ce, a_we => t1_we, a_addr => std_logic_vector(t1_addr), a_din => std_logic_vector(t1_di), a_dout => t1_q,
              b_we => wb_t1, b_addr => t_addr(12 downto 0), b_din => t_wdata, b_dout => b_t1_q);
  trkbuf2 : entity work.a2_ram generic map (AW => 13, DW => 8)
    port map (clk => sys_clk, a_en => ce, a_we => t2_we, a_addr => std_logic_vector(t2_addr), a_din => std_logic_vector(t2_di), a_dout => t2_q,
              b_we => wb_t2, b_addr => t_addr(12 downto 0), b_din => t_wdata, b_dout => b_t2_q);

  g_aux_bram : if AUX_DDR = 0 generate
    ram_aux : entity work.a2_ram generic map (AW => 16, DW => 8)
      port map (clk => sys_clk, a_en => samp_en, a_we => a_aux_we, a_addr => std_logic_vector(ram_addr), a_din => std_logic_vector(cpu_d), a_dout => ram_aux_q,
                b_we => wb_aux, b_addr => t_addr, b_din => t_wdata, b_dout => b_aux_q);
    stall <= '0';
    aux_wait <= '0';
    aux_done <= '0';
    c_addr <= (others => '0'); c_cmd <= (others => '0'); c_en <= '0'; c_wdf_data <= (others => '0'); c_wdf_mask <= (others => '0'); c_wdf_wren <= '0';
  end generate;
  g_aux_ddr : if AUX_DDR /= 0 generate
    aux_u : entity work.a2_aux
      port map (clk => sys_clk, rst => rst_m, ce => ce, samp_en => samp_en, q3_rise => q3_rise,
                cpu_addr => std_logic_vector(cpu_addr_ram), vid_addr => std_logic_vector(vid_addr),
                cpu_phase => phase_zero, col80 => col80, aux_sel => aux, cpu_ram => cpu_ram_sel, cpu_we => cpu_we_m, cpu_ram_wr => cpu_ram_wr,
                wdata => std_logic_vector(cpu_d), dout => ram_aux_q, stall => stall,
                w_start => aux_w_start, w_we => t_we, w_addr => t_addr, w_wdata => t_wdata, w_done => aux_done, w_rdata => b_aux_q,
                aux_base => abase_s2, st_miss => aux_miss, st_stall => aux_stall, st_wait => aux_wait,
                ui_clk => ui_clk, ui_rst => ui_rst, c_addr => c_addr, c_cmd => c_cmd, c_en => c_en, c_rdy => c_rdy,
                c_wdf_data => c_wdf_data, c_wdf_mask => c_wdf_mask, c_wdf_wren => c_wdf_wren, c_rd_data => c_rd_data, c_rd_valid => c_rd_valid);
  end generate;

  -- ================================================================ the Disk II (slot 6)
  d_mounted <= dctl_s2(1 downto 0);
  disk : entity work.disk_ii
    port map (
      CLK_14M => sys_clk, CE_14M => ce, STEP => cpu_step,
      IO_SELECT => io_select(6), DEVICE_SELECT => device_select(6), RESET => rst_m,
      A => cpu_addr, CPU_WE => cpu_we_m, D_IN => cpu_d, D_OUT => disk_do,
      D1_ACTIVE => d1_act, D2_ACTIVE => d2_act, D1_WP => dctl_s2(2), D2_WP => dctl_s2(3), KEEP_OLD => dcfg_s2(0),
      MOUNTED => d_mounted, TRACK_READY => d_ready, TRACK_BUSY => d_busy, NOISE_TRACK => d_noise,
      BIT_COUNT1 => bc1, BIT_COUNT2 => bc2,
      TRACK1 => trk1, TRACK1_ADDR => t1_addr, TRACK1_DI => t1_di, TRACK1_DO => t1_do, TRACK1_WE => t1_we,
      TRACK2 => trk2, TRACK2_ADDR => t2_addr, TRACK2_DI => t2_di, TRACK2_DO => t2_do, TRACK2_WE => t2_we,
      BIT_WRITTEN => bit_wr, STARVED => starved, CELL_NOISE => cell_noise,
      ROM_ADDR => boot_addr, ROM_DOUT => boot_u);
  boot_u <= unsigned(boot_q);
  t1_do <= unsigned(t1_q);
  t2_do <= unsigned(t2_q);

  drv1 : entity work.a2_trk
    port map (clk => sys_clk, rst => rst_m, mounted => dctl_s2(0), change => chg_p(0), flush => fl_p(0), track => trk1, active => d1_act,
              wr => bit_wr(0), ack => ack_p(0), bits_in => unsigned(bits1_s2), flags_in => "0" & trkfl_s2(0),
              req => tr_req(0), req_save => tr_save(0), req_track => tr_trk(0), dirty => tr_dirty(0),
              valid => tr_valid(0), ready => d_ready(0), busy => d_busy(0), bit_count => bc1, noise => d_noise(0),
              lat_max => lat1, reqs => reqs1, lat_clear => lat_clr);
  drv2 : entity work.a2_trk
    port map (clk => sys_clk, rst => rst_m, mounted => dctl_s2(1), change => chg_p(1), flush => fl_p(1), track => trk2, active => d2_act,
              wr => bit_wr(1), ack => ack_p(1), bits_in => unsigned(bits2_s2), flags_in => "0" & trkfl_s2(1),
              req => tr_req(1), req_save => tr_save(1), req_track => tr_trk(1), dirty => tr_dirty(1),
              valid => tr_valid(1), ready => d_ready(1), busy => d_busy(1), bit_count => bc2, noise => d_noise(1),
              lat_max => lat2, reqs => reqs2, lat_clear => lat_clr);
  process (sys_clk)
  begin
    if rising_edge(sys_clk) then
      tr_req_d <= tr_req;
      if (tr_req(0) = '1' and tr_req_d(0) = '0') or (tr_req(1) = '1' and tr_req_d(1) = '0') then
        trk_ev_tgl <= not trk_ev_tgl;
      end if;
      -- the window's counters (T89.1 D6): the guest read the data latch while its drive had no track; cells played as noise while a track loaded
      if lat_clr = '1' then
        starved_c <= (others => '0'); noise_c <= (others => '0');
      else
        if starved = '1' and starved_c /= x"FFFF" then starved_c <= starved_c + 1; end if;
        if cell_noise = '1' and noise_c /= x"FFFFFFFF" then noise_c <= noise_c + 1; end if;
      end if;
    end if;
  end process;

  -- ================================================================ the ProDOS hard disk card (slot 7, T89.1 D4)
  hdd : entity work.a2_hdd
    port map (clk => sys_clk, rst => rst_m, step => cpu_step, dev_sel => device_select(7), io_sel => io_select(7),
              addr => cpu_addr, cpu_we => cpu_we_m, d_in => cpu_d, d_out => hdd_do, rom_addr => hrom_addr, rom_q => hrom_u,
              cpu_wait => cpu_wait_h, ctl => hctl_s2(3 downto 0), size1 => hsz1_s2, size2 => hsz2_s2, result => hres_s2, ack => hdd_ack_p,
              req => h_req, req_write => h_wr, req_unit => h_unit, req_block => h_blk, req_mem => h_mem, req_bank1 => h_b1, bank1 => hram_b1, cmds => h_cmds);
  hrom : entity work.a2_ram generic map (AW => 8, DW => 8)
    port map (clk => sys_clk, a_en => ce, a_we => '0', a_addr => std_logic_vector(hrom_addr), a_din => x"00", a_dout => hrom_q,
              b_we => wb_hrom, b_addr => t_addr(7 downto 0), b_din => t_wdata, b_dout => b_hrom_q);
  hrom_u <= unsigned(hrom_q);
  process (sys_clk)
  begin
    if rising_edge(sys_clk) then
      h_req_d <= h_req;
      if h_req = '1' and h_req_d = '0' then hdd_ev_tgl <= not hdd_ev_tgl; end if;
    end if;
  end process;

  -- ================================================================ the AppleMouse card (slot 5, T89 Q8)
  g_mouse : if MOUSE /= 0 generate
    mouse : applemouse
      port map (CLK_14M => sys_clk, CE_14M => ce, CE_F => ce_f, CE_2M => samp_en, PHASE_ZERO => phase_zero,
                IO_SELECT => io_select(5), IO_STROBE => '0', DEVICE_SELECT => device_select(5), RESET => rst_m,
                A => cpu_addr, D_IN => cpu_d, D_OUT => mouse_do, RNW => not cpu_we_m, OE => mouse_oe, IRQ_N => mouse_irq_n,
                STROBE => mstb, X => resize(signed(mdx_s2), 9), Y => -resize(signed(mdy_s2), 9), BUTTON => mbtn_s2,
                CARD_ROM_A => mrom_a, CARD_ROM_D => mrom_q, MCU_ROM_A => mmcu_a, MCU_ROM_D => mmcu_q);
    mouse_rom : entity work.a2_ram generic map (AW => 11, DW => 8)
      port map (clk => sys_clk, a_en => ce, a_we => '0', a_addr => mrom_a, a_din => x"00", a_dout => mrom_q,
                b_we => wb_mrom, b_addr => t_addr(10 downto 0), b_din => t_wdata, b_dout => b_mrom_q);
    mouse_mcu_rom : entity work.a2_ram generic map (AW => 11, DW => 8)
      port map (clk => sys_clk, a_en => ce, a_we => '0', a_addr => mmcu_a, a_din => x"00", a_dout => mmcu_q,
                b_we => wb_mmcu, b_addr => t_addr(10 downto 0), b_din => t_wdata, b_dout => b_mmcu_q);

  end generate;
  g_nomouse : if MOUSE = 0 generate
    mouse_do <= (others => '0'); mouse_oe <= '0'; mouse_irq_n <= '1'; b_mrom_q <= (others => '0'); b_mmcu_q <= (others => '0');
  end generate;

  -- ================================================================ keyboard, game port, sound
  keyb : entity work.a2_keys
    generic map (REPEAT_DELAY => REPEAT_DELAY, REPEAT_RATE => REPEAT_RATE)
    port map (clk => sys_clk, rst => rst_m, ce => ce, cyc => phase_zero_r, keys => keys, keystrobe => keystrobe,
              inj_we => inj_p, inj_data => inj_s, k => k_code, akd => akd, open_apple => open_apple,
              closed_apple => closed_apple, caps_in => caps_s2, caps => caps_l, queued => q_keys);
  game : entity work.a2_game
    port map (clk => sys_clk, rst => rst_m, ce => ce, cyc => phase_zero_r, strobe => pdl_strobe, joy1 => joy1, joy2 => joy2,
              swap => cfg_s2(6), open_apple => open_apple, closed_apple => closed_apple, gameport => gameport);
  snd : entity work.a2_audio
    port map (clk => sys_clk, spk => speaker, mute => held, mclk => mclk, audio_l => au_l, audio_r => au_r);
  audio_l <= std_logic_vector(au_l);
  audio_r <= std_logic_vector(au_r);

  -- ================================================================ picture
  pixc : entity work.a2_pixc
    generic map (X0 => PIX_X0)
    port map (clk => sys_clk, ce => ce, video => video, color_line => color_line, hbl => hbl, vbl => vbl, col80 => col80,
              mono => pal_mono, pal => pal_sel, custom => pal_s2, pv => pv, rgb => rgb, ls => ls, fs => fs, lrow => lrow, hq_o => hq);
  pixw : entity work.a2_pixw
    port map (sys_clk => sys_clk, sys_rst => rst_m, strobe => px_strobe, rgb565 => rgb, ls => ls, fs => fs, lrow => lrow,
              quiet => pw_quiet, frame_tgl => frame_tgl, latest => fb_latest, seq => fb_seq, overruns => fb_over,
              fb_base => fbase_c, fb_stride => fstride_c, scan => cfg_s2(4),
              ui_clk => ui_clk, ui_rst => ui_rst, e_addr => e_addr, e_cmd => e_cmd, e_en => e_en, e_rdy => e_rdy,
              e_wdf_data => e_wdf_data, e_wdf_mask => e_wdf_mask, e_wdf_wren => e_wdf_wren);
  px_strobe <= ce and pv;

  -- the raster the host's scaler locks to: 65 x 8 dots of 4 clocks, 263 lines, colour 0; not tied to the machine, never stops
  process (sys_clk)
  begin
    if rising_edge(sys_clk) then
      dph <= dph + 1;
      if dph = 3 then
        if dx = 519 then
          dx <= (others => '0');
          if dyy = 262 then dyy <= (others => '0'); else dyy <= dyy + 1; end if;
        else
          dx <= dx + 1;
        end if;
      end if;
      -- Apple frames
      if ce = '1' then
        vb_d <= vbl;
        if vb_d = '1' and vbl = '0' then frames <= frames + 1; end if;
      end if;
    end if;
  end process;
  pix_color <= "0000";
  pix_x <= std_logic_vector(dx);
  pix_y <= std_logic_vector(dyy);

  -- ================================================================ the app's window
  win : entity work.a2_win
    port map (clk => sys_clk, dma_req_tgl => dma_req_tgl, dma_addr => dma_addr, dma_we => dma_we, dma_wdata => dma_wdata,
              dma_rom => dma_rom, dma_done_tgl => dma_done_tgl, dma_rdata => dma_rdata, dbg_busy => dbg_busy,
              port_use => win_port_use, port_addr => win_port_addr, port_rom => win_port_rom, port_we => win_port_we,
              port_wdata => win_port_wdata, sel => sel_s2,
              t_start => t_start, t_we => t_we, t_rom => t_rom, t_sel => t_sel, t_addr => t_addr, t_wdata => t_wdata,
              t_done => t_done, t_rdata => t_rdata, last_rd => win_last);
  ram_rdata <= win_last;
  wb_main <= t_start and t_we and not t_rom;
  wb_rom  <= t_start and t_we and t_rom when t_sel = x"0" else '0';
  wb_vrom <= t_start and t_we and t_rom when t_sel = x"1" else '0';
  wb_boot <= t_start and t_we and t_rom when t_sel = x"2" else '0';
  wb_t1   <= t_start and t_we and t_rom when t_sel = x"3" else '0';
  wb_t2   <= t_start and t_we and t_rom when t_sel = x"4" else '0';
  wb_aux  <= t_start and t_we and t_rom when t_sel = x"5" else '0';
  wb_mrom <= t_start and t_we and t_rom when t_sel = x"6" else '0';
  wb_mmcu <= t_start and t_we and t_rom when t_sel = x"7" else '0';
  wb_hrom <= t_start and t_we and t_rom when t_sel = x"8" else '0';
  process (sys_clk)
  begin
    if rising_edge(sys_clk) then
      tq <= t_rom & t_sel;
      t_done_r <= t_start;
    end if;
  end process;
  aux_w_start <= t_start and t_rom when t_sel = x"5" else '0';
  t_done <= (t_done_r and not is_aux_q) or aux_done;
  is_aux_q <= '1' when tq = "10101" and AUX_DDR /= 0 else '0';       -- only the DDR2 engine answers for the aux array; the block RAM variant answers like the others
  with tq select t_rdata <=
    b_main_q when "00000" | "00001" | "00010" | "00011" | "00100" | "00101" | "00110" | "00111" | "01000" | "01001" | "01010" | "01011" | "01100" | "01101" | "01110" | "01111",
    b_rom_q  when "10000",
    b_vrom_q when "10001",
    b_boot_q when "10010",
    b_t1_q   when "10011",
    b_t2_q   when "10100",
    b_aux_q  when "10101",
    b_mrom_q when "10110",
    b_mmcu_q when "10111",
    b_hrom_q when "11000",
    x"00"    when others;

  -- ================================================================ the register block (cpu_clk)
  ft_tt : process (cpu_clk)
  begin
    if rising_edge(cpu_clk) then
      ft_c <= ft_c(1 downto 0) & frame_tgl;
      tt_c <= tt_c(1 downto 0) & trk_ev_tgl;
      hd_t_s2 <= hd_t_s2(1 downto 0) & hdd_ev_tgl;
    end if;
  end process;
  ev_frame <= ft_c(2) xor ft_c(1);
  ev_track <= tt_c(2) xor tt_c(1);
  ev_hdd   <= hd_t_s2(2) xor hd_t_s2(1);

  -- T159 D-all: the case LED 1. The hard disk request is open for the time the app takes to serve a block: held, so that it can be seen.
  process (sys_clk)
  begin
    if rising_edge(sys_clk) then
      if rst_m = '1' then hd_hold <= 0;
      elsif h_req = '1' then hd_hold <= HD_HOLD_CLKS;
      elsif hd_hold /= 0 then hd_hold <= hd_hold - 1;
      end if;
    end if;
  end process;
  hd_led  <= '1' when h_req = '1' or hd_hold /= 0 else '0';
  drv_hd  <= hd_led;
  drv_led <= d1_act or d2_act or hd_led;

  status8 <= "000" & aux_wait & caps_l & cold_active & held & rst_m;
  sysv(15 downto 0)    <= std_logic_vector(frames);
  sysv(31 downto 16)   <= std_logic_vector(cpu_addr);
  sysv(63 downto 32)   <= aux_miss;
  sysv(95 downto 64)   <= aux_stall;
  sysv(111 downto 96)  <= fb_over;
  sysv(127 downto 112) <= fb_latest & fb_seq;
  sysv(143 downto 128) <= x"00" & status8;
  sysv(159 downto 144) <= x"00" & "000" & d1_act & tr_valid(0) & tr_dirty(0) & tr_save(0) & tr_req(0);
  sysv(175 downto 160) <= x"00" & "000" & d2_act & tr_valid(1) & tr_dirty(1) & tr_save(1) & tr_req(1);
  sysv(223 downto 208) <= std_logic_vector(tr_trk(1)) & std_logic_vector(tr_trk(0));
  sysv(239 downto 224) <= std_logic_vector(trk2) & std_logic_vector(trk1);
  sysv(255 downto 240) <= x"00" & "0000" & h_b1 & h_unit & h_wr & h_req;
  sysv(271 downto 256) <= h_blk;
  sysv(287 downto 272) <= h_mem;
  sysv(319 downto 288) <= std_logic_vector(lat1);
  sysv(351 downto 320) <= std_logic_vector(lat2);
  sysv(367 downto 352) <= std_logic_vector(reqs1);
  sysv(383 downto 368) <= std_logic_vector(reqs2);
  sysv(399 downto 384) <= std_logic_vector(starved_c);
  sysv(431 downto 400) <= std_logic_vector(noise_c);
  sysv(447 downto 432) <= std_logic_vector(h_cmds);
  sysv(207 downto 192) <= "00000000000" & q_keys;
  regs : entity work.a2_regs
    generic map (CONTRACT => 2)
    port map (clk => cpu_clk, we => cw_we, re => cw_re, addr => cw_addr, wdata => cw_wdata, rdata => cw_rdata, ack => cw_ack, irq => cw_irq,
              build => build, ddr_cal => ddr_cal, px_we => px_we, px_re => px_re, px_rdata => px_rdata,
              ev_vsync => px_ev_vsync, ev_flip => px_ev_flip, ev_frame => ev_frame, ev_track => ev_track, ev_hdd => ev_hdd,
              ctrl => ctrl_c, cold_tgl => cold_c, cfg => cfg_c, caps => caps_c, aux_base => abase_c, fb_base => fbase_c, fb_stride => fstride_c,
              win_sel => sel_c, disk_ctl => dctl_c, disk_cfg => dcfg_c, bits1 => bits1_c, bits2 => bits2_c, trkfl => trkfl_c,
              hdd_ctl => hctl_c, hdd_sz1 => hsz1_c, hdd_sz2 => hsz2_c, hdd_res => hres_c, palette => pal_c, cmd_tgl => cmd_c, key_inj => inj_c,
              mouse_dx => mdx_c, mouse_dy => mdy_c, mouse_btn => mbtn_c, sysv => sysv);

  -- ================================================================ JTAG proof of life (trxdbg "trx 0 ..."), words:
  --  0 "APL2"  1 status (b0 in reset, b1 held, b2 cold reset, b3 caps)  2 frames  3 CPU address  4 enables counted (low 16)  5 (high 16)
  --  6 trace signature low  7 high  8 aux misses  9 aux stall clocks
  with dbg_addr(3 downto 0) select jd <=
    x"324C5041"                                         when x"0",
    x"000000" & status8                                 when x"1",
    x"0000" & std_logic_vector(frames)                  when x"2",
    x"0000" & std_logic_vector(cpu_addr)                when x"3",
    x"0000" & std_logic_vector(ce_cnt(15 downto 0))     when x"4",
    x"0000" & std_logic_vector(ce_cnt(31 downto 16))    when x"5",
    x"0000" & sig(15 downto 0)                          when x"6",
    x"0000" & sig(31 downto 16)                         when x"7",
    aux_miss                                            when x"8",
    aux_stall                                           when x"9",
    x"00000000"                                         when others;
  j_rdata <= jd;
  j_rvalid <= '1';
end architecture;
