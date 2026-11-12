-- SPDX-License-Identifier: GPL-3.0-or-later
-- T88: the app's DMA window (a toggle per byte, ultimate_sys.vhd) and the host's second memory port, as vc20_win / c264_win do
-- it: the window asks the host for the port (port_*); the host merges it with the JTAG and gives the winner back as p_req / p_*
-- in the same clock; the read data comes back from the memory one clock after the request. The port's address space:
--   p_rom = 0   the 64 KB base RAM, byte address = the machine's (a PRG or .xex load, machine:readmem)
--   p_rom = 1   the ROM aperture: dma_addr 0x2000-0x3FFF and 0x4000-0x5FFF are the two halves of ONE 16 KB aperture, offset =
--               dma_addr[13:0] (the app sees ROMS_BASE .. ROMS_BASE + 0x3FFF contiguous); rom_sel names the array behind it:
--               0 the XL OS (16 KB), 1 BASIC (8 KB, offsets 0-0x1FFF), 2 the PBI/XEX loader ROM (8 KB, offsets 0-0x1FFF).
--               The host's other apertures (char, colour RAM) are not here: they read 0 and write nothing.
-- RAM above 64 KB and the cartridge image are in the DDR2 window, which the app writes itself.
-- Written for this project (TREX), no upstream code; the same protocol as c264_win.v.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity a8_win is
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
    rom_sel      : in  std_logic_vector(1 downto 0);   -- sys_clk, quasi static (the app sets it before the bytes)
    port_use     : out std_logic;
    port_addr    : out std_logic_vector(15 downto 0);
    port_rom     : out std_logic;
    port_we      : out std_logic;
    port_wdata   : out std_logic_vector(7 downto 0);
    port_rdata   : in  std_logic_vector(7 downto 0);   -- the memory's w_do
    p_req        : in  std_logic;
    p_addr       : in  std_logic_vector(15 downto 0);
    p_rom        : in  std_logic;
    p_we         : in  std_logic;
    p_wdata      : in  std_logic_vector(7 downto 0);
    w_wr         : out std_logic;
    w_hit        : out std_logic;
    w_rom        : out std_logic;
    w_sel        : out std_logic_vector(1 downto 0);
    w_ai         : out std_logic_vector(15 downto 0);
    w_di         : out std_logic_vector(7 downto 0));
end entity;

architecture rtl of a8_win is
  signal dreq_s   : std_logic_vector(2 downto 0) := "000";
  attribute ASYNC_REG : string;
  attribute ASYNC_REG of dreq_s : signal is "TRUE";
  signal done     : std_logic := '0';
  signal rd       : std_logic_vector(7 downto 0) := (others => '0');
  signal p_ram    : std_logic := '0';
  signal ram_rd   : std_logic := '0';
  signal pr_addr  : std_logic_vector(15 downto 0) := (others => '0');
  signal pr_rom, pr_we : std_logic := '0';
  signal pr_wd    : std_logic_vector(7 downto 0) := (others => '0');
  signal use_i    : std_logic;
begin
  use_i      <= p_ram and not dbg_busy;
  port_use   <= use_i;
  port_addr  <= pr_addr;
  port_rom   <= pr_rom;
  port_we    <= pr_we;
  port_wdata <= pr_wd;
  dma_done_tgl <= done;
  dma_rdata  <= rd;

  process (clk)
  begin
    if rising_edge(clk) then
      dreq_s <= dreq_s(1 downto 0) & dma_req_tgl;
      ram_rd <= use_i;
      if dreq_s(2) /= dreq_s(1) then
        p_ram <= '1'; pr_addr <= dma_addr; pr_rom <= dma_rom; pr_we <= dma_we; pr_wd <= dma_wdata;
      end if;
      if use_i = '1' then p_ram <= '0'; end if;
      if ram_rd = '1' then
        done <= not done;
        if pr_we = '1' then rd <= x"00"; else rd <= port_rdata; end if;
      end if;
    end if;
  end process;

  -- the memory side: one address decode for the whole port
  w_rom <= p_rom;
  w_sel <= rom_sel;
  w_di  <= p_wdata;
  w_wr  <= p_req and p_we;
  process (p_rom, p_addr, rom_sel)
  begin
    w_hit <= '1';
    w_ai  <= p_addr;
    if p_rom = '1' then
      w_ai  <= "00" & p_addr(13 downto 0);
      if p_addr(15) = '0' and (p_addr(14) xor p_addr(13)) = '1' and rom_sel /= "11"
         and (rom_sel = "00" or p_addr(13) = '0') then
        w_hit <= '1';
      else
        w_hit <= '0';
      end if;
    end if;
  end process;
end architecture;
