-- T89 benches: the app's side of the register bus and of the DMA window, the loading of an image from a hex file (one byte a line,
-- written by rom2hex.py / mkrom.py), a text screen dump.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.textio.all;

package a2_tb_pkg is
  -- what the app drives (a bench keeps one signal of this type and connects its fields to the harness)
  type app_t is record
    cw_we, cw_re : std_logic;                       -- the register bus (cpu_clk)
    cw_addr      : std_logic_vector(16 downto 0);
    cw_wdata     : std_logic_vector(7 downto 0);
    dma_tgl      : std_logic;                       -- the DMA window: a toggle per byte
    dma_addr     : std_logic_vector(15 downto 0);
    dma_we       : std_logic;
    dma_wdata    : std_logic_vector(7 downto 0);
    dma_rom      : std_logic;
  end record;
  constant APP_IDLE : app_t := ('0', '0', (others => '0'), (others => '0'), '0', (others => '0'), '0', (others => '0'), '0');
  -- what comes back
  type app_in_t is record
    cw_rdata  : std_logic_vector(7 downto 0);
    cw_ack    : std_logic;
    dma_done  : std_logic;
    dma_rdata : std_logic_vector(7 downto 0);
  end record;

  procedure cw_wr (signal clk : in std_logic; signal a : inout app_t; signal i : in app_in_t; ad : in integer; d : in integer);
  procedure cw_rd (signal clk : in std_logic; signal a : inout app_t; signal i : in app_in_t; ad : in integer; d : out integer);
  -- the DMA window; the array is chosen by register WIN_SEL (0D4) for rom = '1'
  procedure win_wr (signal a : inout app_t; signal i : in app_in_t; rom : in std_logic; ad : in integer; d : in integer);
  procedure win_rd (signal a : inout app_t; signal i : in app_in_t; rom : in std_logic; ad : in integer; d : out integer);
  -- an image from a hex file into array `sel` (rom = 1), or main RAM (sel < 0)
  procedure load_hex (fname : in string; sel : in integer; n : in integer; signal clk : in std_logic; signal a : inout app_t; signal i : in app_in_t);
  -- the app types a line
  procedure type_line (txt : in string; signal clk : in std_logic; signal a : inout app_t; signal i : in app_in_t);
  -- the printable character of a screen code (normal, inverse and flashing text all map to ASCII)
  function scr_char (c : integer) return character;
end package;

package body a2_tb_pkg is
  procedure cw_wr (signal clk : in std_logic; signal a : inout app_t; signal i : in app_in_t; ad : in integer; d : in integer) is
  begin
    wait until rising_edge(clk);
    a.cw_addr <= std_logic_vector(to_unsigned(ad, 17)); a.cw_wdata <= std_logic_vector(to_unsigned(d mod 256, 8)); a.cw_we <= '1';
    wait until rising_edge(clk);
    a.cw_we <= '0';
    wait until rising_edge(clk) and i.cw_ack = '1';
  end procedure;

  procedure cw_rd (signal clk : in std_logic; signal a : inout app_t; signal i : in app_in_t; ad : in integer; d : out integer) is
  begin
    wait until rising_edge(clk);
    a.cw_addr <= std_logic_vector(to_unsigned(ad, 17)); a.cw_re <= '1';
    wait until rising_edge(clk);
    a.cw_re <= '0';
    wait until rising_edge(clk) and i.cw_ack = '1';
    wait for 1 ns;
    d := to_integer(unsigned(i.cw_rdata));
  end procedure;

  procedure win_wr (signal a : inout app_t; signal i : in app_in_t; rom : in std_logic; ad : in integer; d : in integer) is
    variable was : std_logic;
  begin
    was := i.dma_done;
    a.dma_addr <= std_logic_vector(to_unsigned(ad, 16)); a.dma_wdata <= std_logic_vector(to_unsigned(d mod 256, 8)); a.dma_we <= '1'; a.dma_rom <= rom;
    wait for 20 ns;
    a.dma_tgl <= not a.dma_tgl;
    if i.dma_done = was then wait until i.dma_done /= was; end if;
  end procedure;

  procedure win_rd (signal a : inout app_t; signal i : in app_in_t; rom : in std_logic; ad : in integer; d : out integer) is
    variable was : std_logic;
  begin
    was := i.dma_done;
    a.dma_addr <= std_logic_vector(to_unsigned(ad, 16)); a.dma_we <= '0'; a.dma_rom <= rom;
    wait for 20 ns;
    a.dma_tgl <= not a.dma_tgl;
    if i.dma_done = was then wait until i.dma_done /= was; end if;
    wait for 1 ns;
    d := to_integer(unsigned(i.dma_rdata));
  end procedure;

  procedure load_hex (fname : in string; sel : in integer; n : in integer; signal clk : in std_logic; signal a : inout app_t; signal i : in app_in_t) is
    file f : text open read_mode is fname;
    variable l : line; variable v : std_logic_vector(7 downto 0); variable k : integer := 0;
  begin
    if sel >= 0 then cw_wr(clk, a, i, 16#0D4#, sel); end if;
    while not endfile(f) and k < n loop
      readline(f, l); hread(l, v);
      if sel >= 0 then win_wr(a, i, '1', k, to_integer(unsigned(v))); else win_wr(a, i, '0', k, to_integer(unsigned(v))); end if;
      k := k + 1;
    end loop;
  end procedure;

  -- one 7 bit code per write of register KEY_INJ (0x140), then CR
  procedure type_line (txt : in string; signal clk : in std_logic; signal a : inout app_t; signal i : in app_in_t) is
    variable q : integer;
  begin
    -- the queue holds 16 codes (register 141 says how many wait): a longer line is typed as the program takes the keys
    for k in txt'range loop
      loop
        cw_rd(clk, a, i, 16#141#, q);
        exit when q < 12;
        wait for 200 us;
      end loop;
      cw_wr(clk, a, i, 16#140#, character'pos(txt(k)));
    end loop;
    loop
      cw_rd(clk, a, i, 16#141#, q);
      exit when q < 12;
      wait for 200 us;
    end loop;
    cw_wr(clk, a, i, 16#140#, 13);
  end procedure;

  function scr_char (c : integer) return character is
    variable v : integer;
  begin
    v := c mod 128;
    if c < 128 then
      if v < 32 then v := v + 64; end if;
    end if;
    if v < 32 or v > 126 then v := 46; end if;
    return character'val(v);
  end function;
end package body;
