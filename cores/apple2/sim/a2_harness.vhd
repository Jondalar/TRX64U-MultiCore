-- T89 benches: the Apple //e core with its clocks and a model of the host's two DDR2 ports around it, so that a bench only drives
-- what the app and the C64U drive (registers, DMA window, keys, joysticks, the pause) and reads what comes out.
--   clocks     sys_clk 32.7273 MHz (the host's NTSC clock, 7/16 of it is the Apple's 14.318 MHz), cpu_clk 100 MHz, ui_clk 75 MHz (the MIG's, host/top.v),
--              mclk 12.288 MHz
--   e port     the frame writer's: every write lands in `emem` (the four frame buffers, 4 MB from FB_BASE); e_rdy is 1 except for
--              the clocks a throttle pattern takes it away (E_BUSY of 8: one in eight clocks)
--   c port     the aux RAM's: 64 KB at AUX_BASE in `cmem`; a read comes back C_LAT ui_clk (13.3 ns) later (c_rd_valid for one clock)
--   readback   fb_rd / aux_rd are the memories seen from the bench (combinational)
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity a2_harness is
  generic (
    AUX_DDR    : integer := 1;
    MOUSE      : integer := 0;
    COLD_CLKS  : integer := 20000;
    WARM_CLKS  : integer := 1024;
    REPEAT_DELAY : integer := 534000;
    REPEAT_RATE  : integer := 91000;
    C_LAT      : integer := 40;           -- ui_clk cycles from the read command to the data: 0.53 us, inside the 0.4-0.7 us the aux RAM has to live with
    E_BUSY     : integer := 8;
    FB_BASE_W  : integer := 16#E80000#;       -- 16-bit word address of 0x01D00000
    AUX_BASE_W : integer := 16#880000#        -- 0x01100000
  );
  port (
    sys_clk, cpu_clk, ui_clk, mclk : out std_logic;
    sys_rst    : in  std_logic;
    halt_req   : in  std_logic := '0';
    reset_req  : in  std_logic := '0';
    halted, running : out std_logic;
    keys       : in  std_logic_vector(63 downto 0) := (others => '0');
    joy1, joy2 : in  std_logic_vector(4 downto 0) := (others => '0');
    restore    : in  std_logic := '0';
    -- the app: DMA window and register bus
    dma_req_tgl : in std_logic := '0';
    dma_addr   : in  std_logic_vector(15 downto 0) := (others => '0');
    dma_we     : in  std_logic := '0';
    dma_wdata  : in  std_logic_vector(7 downto 0) := (others => '0');
    dma_rom    : in  std_logic := '0';
    dma_done_tgl : out std_logic;
    dma_rdata  : out std_logic_vector(7 downto 0);
    dbg_busy   : in  std_logic := '0';
    cw_we, cw_re : in std_logic := '0';
    cw_addr    : in  std_logic_vector(16 downto 0) := (others => '0');
    cw_wdata   : in  std_logic_vector(7 downto 0) := (others => '0');
    cw_rdata   : out std_logic_vector(7 downto 0);
    cw_ack, cw_irq : out std_logic;
    px_rdata   : in  std_logic_vector(7 downto 0) := (others => '0');
    px_we, px_re : out std_logic;
    ev_vsync, ev_flip : in std_logic := '0';
    -- outputs
    audio_l, audio_r : out std_logic_vector(23 downto 0);
    pix_x      : out std_logic_vector(9 downto 0);
    pix_y      : out std_logic_vector(8 downto 0);
    dbg_ce_cnt, dbg_sig : out std_logic_vector(31 downto 0);
    probe      : out std_logic_vector(23 downto 0);
    dbg_addr   : in  std_logic_vector(11 downto 0) := (others => '0');
    j_rdata    : out std_logic_vector(31 downto 0);
    ram_rdata  : out std_logic_vector(7 downto 0);
    drv_led, drv_hd : out std_logic;                        -- T159 D-all: the case LED 1 source and its hard disk part
    -- the DDR2 memories, read by the bench
    fb_rd_addr : in  integer := 0;                          -- 128-bit word index from FB_BASE
    fb_rd_data : out std_logic_vector(127 downto 0);
    aux_rd_addr : in integer := 0;
    aux_rd_data : out std_logic_vector(127 downto 0);
    e_writes   : out integer;
    c_reads, c_writes : out integer
  );
end entity;

architecture sim of a2_harness is
  signal sclk : std_logic := '0';
  signal pclk : std_logic := '0';
  signal uclk : std_logic := '0';
  signal mck  : std_logic := '0';
  signal ui_rst : std_logic := '1';
  signal e_addr, c_addr : std_logic_vector(26 downto 0);
  signal e_cmd, c_cmd : std_logic_vector(2 downto 0);
  signal e_en, c_en : std_logic;
  signal e_rdy, c_rdy : std_logic := '1';
  signal e_wdf_data, c_wdf_data : std_logic_vector(127 downto 0);
  signal e_wdf_mask, c_wdf_mask : std_logic_vector(15 downto 0);
  signal e_wdf_wren, c_wdf_wren : std_logic;
  signal c_rd_data : std_logic_vector(127 downto 0) := (others => '0');
  signal c_rd_valid : std_logic := '0';
  signal a_halted, a_running : std_logic;
  signal ewr, crd, cwr : integer := 0;

  constant E_WORDS : integer := 262144;
  constant C_WORDS : integer := 4096;
  type mem_e is array (0 to E_WORDS - 1) of std_logic_vector(127 downto 0);
  type mem_c is array (0 to C_WORDS - 1) of std_logic_vector(127 downto 0);
  shared variable emem : mem_e := (others => (others => '0'));
  shared variable cmem : mem_c := (others => (others => '0'));
  signal ecnt : unsigned(2 downto 0) := (others => '0');
begin
  sclk <= not sclk after 15.278 ns;          -- 32.7273 MHz
  pclk <= not pclk after 5 ns;
  uclk <= not uclk after 6.667 ns;
  mck  <= not mck after 40.69 ns;
  sys_clk <= sclk; cpu_clk <= pclk; ui_clk <= uclk; mclk <= mck;
  ui_rst <= '1', '0' after 100 ns;
  halted <= a_halted; running <= a_running;

  core : entity work.a2_core
    generic map (AUX_DDR => AUX_DDR, MOUSE => MOUSE, COLD_CLKS => COLD_CLKS, WARM_CLKS => WARM_CLKS, REPEAT_DELAY => REPEAT_DELAY, REPEAT_RATE => REPEAT_RATE)
    port map (
      sys_clk => sclk, sys_rst => sys_rst, cpu_clk => pclk, mclk => mck, ui_clk => uclk, ui_rst => ui_rst,
      reset_req => reset_req, halt_req => halt_req, halted => a_halted, running => a_running,
      keys => keys, joy1 => joy1, joy2 => joy2, restore => restore,
      dma_req_tgl => dma_req_tgl, dma_addr => dma_addr, dma_we => dma_we, dma_wdata => dma_wdata, dma_rom => dma_rom,
      dma_done_tgl => dma_done_tgl, dma_rdata => dma_rdata, dbg_busy => dbg_busy, port_req => '0', ram_rdata => ram_rdata,
      pix_x => pix_x, pix_y => pix_y, audio_l => audio_l, audio_r => audio_r,
      cw_we => cw_we, cw_re => cw_re, cw_addr => cw_addr, cw_wdata => cw_wdata, cw_rdata => cw_rdata, cw_ack => cw_ack, cw_irq => cw_irq,
      px_we => px_we, px_re => px_re, px_rdata => px_rdata, px_ev_vsync => ev_vsync, px_ev_flip => ev_flip,
      build => x"00000000", ddr_cal => (others => '0'), dbg_re => '1', dbg_addr => dbg_addr, j_rdata => j_rdata,
      e_addr => e_addr, e_cmd => e_cmd, e_en => e_en, e_rdy => e_rdy, e_wdf_data => e_wdf_data, e_wdf_mask => e_wdf_mask, e_wdf_wren => e_wdf_wren,
      c_addr => c_addr, c_cmd => c_cmd, c_en => c_en, c_rdy => c_rdy, c_wdf_data => c_wdf_data, c_wdf_mask => c_wdf_mask,
      c_wdf_wren => c_wdf_wren, c_rd_data => c_rd_data, c_rd_valid => c_rd_valid,
      drv_led => drv_led, drv_hd => drv_hd,
      dbg_ce_cnt => dbg_ce_cnt, dbg_sig => dbg_sig, dbg_probe => probe);

  -- ------------------------------------------------------------ the e port: the frame buffers
  e_throttle : process (uclk)
  begin
    if rising_edge(uclk) then
      ecnt <= ecnt + 1;
      if E_BUSY > 0 and ecnt = to_unsigned(E_BUSY - 1, 3) then e_rdy <= '0'; else e_rdy <= '1'; end if;
    end if;
  end process;
  e_mem : process (uclk)
    variable w : integer;
    variable d : std_logic_vector(127 downto 0);
  begin
    if rising_edge(uclk) then
      if e_en = '1' and e_rdy = '1' and e_cmd = "000" then
        w := (to_integer(unsigned(e_addr)) - FB_BASE_W) / 8;
        if w >= 0 and w < E_WORDS then
          emem(w) := e_wdf_data;
          ewr <= ewr + 1;
        end if;
      end if;
    end if;
  end process;
  e_writes <= ewr;
  fb_rd_data <= emem(fb_rd_addr) when fb_rd_addr >= 0 and fb_rd_addr < E_WORDS else (others => '0');

  -- ------------------------------------------------------------ the c port: the aux RAM
  c_mem : process (uclk)
    variable w : integer;
    variable d : std_logic_vector(127 downto 0);
    type pend_t is array (0 to 63) of integer;
    variable due : integer := -1;
    variable due_w : integer := 0;
  begin
    if rising_edge(uclk) then
      c_rd_valid <= '0';
      c_rdy <= '1';
      if due >= 0 then
        if due = 0 then
          c_rd_data <= cmem(due_w); c_rd_valid <= '1';
        end if;
        due := due - 1;
      end if;
      if c_en = '1' and c_rdy = '1' then
        w := (to_integer(unsigned(c_addr)) - AUX_BASE_W) / 8;
        if w >= 0 and w < C_WORDS then
          if c_cmd = "000" then
            d := cmem(w);
            for i in 0 to 15 loop
              if c_wdf_mask(i) = '0' then d(8 * i + 7 downto 8 * i) := c_wdf_data(8 * i + 7 downto 8 * i); end if;
            end loop;
            cmem(w) := d;
            cwr <= cwr + 1;
          elsif c_cmd = "001" then
            due := C_LAT; due_w := w;
            crd <= crd + 1;
          end if;
        end if;
      end if;
    end if;
  end process;
  c_reads <= crd; c_writes <= cwr;
  aux_rd_data <= cmem(aux_rd_addr) when aux_rd_addr >= 0 and aux_rd_addr < C_WORDS else (others => '0');
end architecture;
