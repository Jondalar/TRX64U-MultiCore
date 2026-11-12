-- TRX Apple //e core (T89 D4/D8/D11): the app's DMA window (a toggle per byte, ultimate_sys.vhd). The window asks the host
-- for the port (port_*), as c264_win / nes_win do it, but starts its access from its own registers, so no path runs
-- core -> static host -> core in one clock (pipeline 874, T103). Written for this project (TREX), no upstream code.
-- The address space (16 bit window address, `dma_rom`, and the SELECT register on top):
--   dma_rom = 0   the 64 KB of main RAM, at the Apple address
--   dma_rom = 1   by SELECT: 0 system ROM (16 KB, $C000-$FFFF), 1 character ROM (8 KB), 2 Disk II boot ROM (256 B),
--                 3 track buffer of drive 1 (8,192 B), 4 track buffer of drive 2, 5 aux RAM (64 KB), 6 mouse card ROM (2 KB),
--                 7 mouse MCU ROM (2 KB); anything else reads 0 and ignores a write
-- The access (t_start for one clock, then t_done with t_rdata) goes to the core, which owns the arrays and the aux RAM's DDR2 engine.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity a2_win is
  port (
    clk          : in  std_logic;
    dma_req_tgl  : in  std_logic;
    dma_addr     : in  std_logic_vector(15 downto 0);
    dma_we       : in  std_logic;
    dma_wdata    : in  std_logic_vector(7 downto 0);
    dma_rom      : in  std_logic;
    dma_done_tgl : out std_logic;
    dma_rdata    : out std_logic_vector(7 downto 0);
    dbg_busy     : in  std_logic;
    port_use     : out std_logic;
    port_addr    : out std_logic_vector(15 downto 0);
    port_rom     : out std_logic;
    port_we      : out std_logic;
    port_wdata   : out std_logic_vector(7 downto 0);
    sel          : in  std_logic_vector(3 downto 0);
    -- the access
    t_start      : out std_logic;
    t_we         : out std_logic;
    t_rom        : out std_logic;
    t_sel        : out std_logic_vector(3 downto 0);
    t_addr       : out std_logic_vector(15 downto 0);
    t_wdata      : out std_logic_vector(7 downto 0);
    t_done       : in  std_logic;
    t_rdata      : in  std_logic_vector(7 downto 0);
    last_rd      : out std_logic_vector(7 downto 0)
  );
end entity;

architecture rtl of a2_win is
  signal dreq_s  : std_logic_vector(2 downto 0) := (others => '0');
  signal done    : std_logic := '0';
  signal rd      : std_logic_vector(7 downto 0) := (others => '0');
  signal p_ram   : std_logic := '0';
  signal own     : std_logic := '0';
  signal pr_addr : std_logic_vector(15 downto 0) := (others => '0');
  signal pr_rom  : std_logic := '0';
  signal pr_we   : std_logic := '0';
  signal pr_wd   : std_logic_vector(7 downto 0) := (others => '0');
  signal use_i   : std_logic;
  signal s_req   : std_logic := '0';
  signal s_sel   : std_logic_vector(3 downto 0) := (others => '0');
  attribute ASYNC_REG : string;
  attribute ASYNC_REG of dreq_s : signal is "TRUE";
begin
  use_i <= p_ram and not dbg_busy;
  port_use   <= use_i;
  port_addr  <= pr_addr;
  port_rom   <= pr_rom;
  port_we    <= pr_we;
  port_wdata <= pr_wd;
  dma_done_tgl <= done;
  dma_rdata    <= rd;
  last_rd      <= rd;

  t_start <= s_req;
  t_we    <= pr_we;
  t_rom   <= pr_rom;
  t_sel   <= s_sel;
  t_addr  <= pr_addr;
  t_wdata <= pr_wd;

  process (clk)
  begin
    if rising_edge(clk) then
      dreq_s <= dreq_s(1 downto 0) & dma_req_tgl;
      s_req <= use_i;
      if use_i = '1' then s_sel <= sel; end if;
      if dreq_s(2) /= dreq_s(1) then
        p_ram   <= '1';
        pr_addr <= dma_addr;
        pr_rom  <= dma_rom;
        pr_we   <= dma_we;
        pr_wd   <= dma_wdata;
      end if;
      if use_i = '1' then p_ram <= '0'; own <= '1'; end if;
      if t_done = '1' and own = '1' then
        own <= '0';
        done <= not done;
        if pr_we = '1' then rd <= x"00"; else rd <= t_rdata; end if;
      end if;
    end if;
  end process;
end architecture;
