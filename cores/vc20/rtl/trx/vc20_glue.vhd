-- TRX VC-20 core: the glue around the VIC-20 machine (cores/vc20/rtl/src), written for this
-- project. Everything here is in sys_clk (the host's C64 clock, 31.5278 MHz PAL) unless noted.
--   vc20_nco    the machine's clock enables from sys_clk
--   vc20_video  the idle raster the host's scaler locks to, and the frame pulse (the picture is vc20_pixw, T119)
--   vc20_keys   the C64 keyboard matrix of the host into the VIC-20 matrix
--   vc20_audio  the VIC's 6 bit sum into 24 bit samples in mclk
--   vc20_win    the app's DMA window and the host's second memory port
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

----------------------------------------------------------------------------------------------
-- Clock enable: 8.867 MHz (twice the pixel clock) as a fraction of sys_clk.
--
-- A VIC-20 frame is 312 lines of 284 pixels (4.43 MHz PAL). The host's scaler expects a VIC-II
-- frame of 312 lines of 504 dots = 2016 sys_clk, which is 628,992 clocks. Dividing sys_clk by
-- 2016/284 = 504/71 makes a VIC-20 line exactly one VIC-II line and a VIC-20 frame exactly one
-- VIC-II frame, so the scaler's frame lock holds for good. The enable is 71/252 of sys_clk
-- (two enables per pixel); the pixel clock comes out at 31.5278 MHz * 71/504 = 4.4414 MHz, 0.17 %
-- above the real 4.43362 MHz (50.12 Hz frames instead of 50.03 Hz; the CPU, the sound and the
-- 1541's bus timing run 0.17 % fast).
----------------------------------------------------------------------------------------------
entity vc20_nco is
  port (
    clk : in  std_logic;
    rst : in  std_logic;
    run : in  std_logic;
    en  : out std_logic
  );
end entity;

architecture rtl of vc20_nco is
  signal acc : unsigned(8 downto 0) := (others => '0');
  signal q   : std_logic := '0';
begin
  process (clk)
    variable n : unsigned(8 downto 0);
  begin
    if rising_edge(clk) then
      q <= '0';
      if rst = '1' then
        acc <= (others => '0');
      elsif run = '1' then
        n := acc + 71;
        if n >= 252 then
          acc <= n - 252;
          q   <= '1';
        else
          acc <= n;
        end if;
      end if;
    end if;
  end process;
  en <= q;
end architecture;

----------------------------------------------------------------------------------------------
-- Video: the host's scaler input. The picture itself goes through vc20_pixw into the DDR2 frame buffer of the pixel layer
-- (T119); what is left here is the raster the host's vic_scaler locks to, as the C264 core leaves it (c264_core.v): 504 dots of
-- 4 clocks, 312 lines, colour 0, free running, not tied to the 6561 and never stopping. 504 x 4 x 312 = 628,992 sys_clk is also
-- the VIC-20 frame (vc20_nco), so the two stay in step. `frame` is one clock per VIC-20 frame at the first visible line
-- (the 6561's line 29), for the frame counter.
----------------------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vc20_video is
  port (
    clk      : in  std_logic;
    ena      : in  std_logic;               -- the machine's pixel enable
    hblank   : in  std_logic;
    vcnt     : in  std_logic_vector(8 downto 0);    -- the 6561's raster line
    pix_color : out std_logic_vector(3 downto 0);
    pix_x    : out std_logic_vector(9 downto 0);
    pix_y    : out std_logic_vector(8 downto 0);
    frame    : out std_logic                -- one clock per frame, at the first visible line
  );
end entity;

architecture rtl of vc20_video is
  signal hb_d    : std_logic := '1';
  signal fr      : std_logic := '0';
  signal dph     : unsigned(1 downto 0) := (others => '0');
  signal dx      : unsigned(8 downto 0) := (others => '0');
  signal dy      : unsigned(8 downto 0) := (others => '0');
begin
  process (clk)
  begin
    if rising_edge(clk) then
      fr <= '0';
      if ena = '1' then
        hb_d <= hblank;
        if hb_d = '1' and hblank = '0' and unsigned(vcnt) = 29 then fr <= '1'; end if;
      end if;
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
  pix_color <= x"0";
  pix_x <= '0' & std_logic_vector(dx);
  pix_y <= std_logic_vector(dy);
  frame <= fr;
end architecture;

----------------------------------------------------------------------------------------------
-- Keyboard. keys[col*8 + row] is the C64 matrix (CIA 1 port A = column, port B = row), 1 = down.
-- The VIC-20 drives scan lines on VIA 2 port B (0 = selected) and reads the answer on port A.
-- vic2c64(p * 8 + s) names the C64 key that sits at read line p (port A bit) and scan line s
-- (port B bit) of the VIC-20 keyboard. The two keyboards have the same keys, wired differently
-- (VIC-20 has no INST/DEL pair, DEL is DEL, and RESTORE is separate). Which line is which was
-- settled with a real KERNAL in the boot bench: a key at (scan 1, read 2) types the KERNAL's "R".
----------------------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vc20_keys is
  port (
    keys    : in  std_logic_vector(63 downto 0);
    col_out : in  std_logic_vector(7 downto 0);     -- keyboard_matrix_out: the scan lines (port B), 0 = selected
    row_in  : out std_logic_vector(7 downto 0)      -- keyboard_matrix_in: the read lines (port A), 0 = a selected line has a key
  );
end entity;

architecture rtl of vc20_keys is
  type map_t is array (0 to 63) of integer range 0 to 63;
  -- index = read line * 8 + scan line (VIC20Nano's key map calls them column and row); value = c64 column * 8 + c64 row
  constant vic2c64 : map_t := (
    -- col 0: 1 <- CTRL STOP SPACE C= Q 2
    56, 57, 58, 63, 60, 61, 62, 59,
    -- col 1: 3 W A LSHIFT Z S E 4
    8, 9, 10, 15, 12, 13, 14, 11,
    -- col 2: 5 R D X C F T 6
    16, 17, 18, 23, 20, 21, 22, 19,
    -- col 3: 7 Y G V B H U 8
    24, 25, 26, 31, 28, 29, 30, 27,
    -- col 4: 9 I J N M K O 0
    32, 33, 34, 39, 36, 37, 38, 35,
    -- col 5: + P L , . : @ -
    40, 41, 42, 47, 44, 45, 46, 43,
    -- col 6: pound * ; / RSHIFT = up-arrow HOME
    48, 49, 50, 55, 52, 53, 54, 51,
    -- col 7: DEL RETURN CRSR-right CRSR-down F1 F3 F5 F7
    0, 1, 2, 7, 4, 5, 6, 3);
begin
  process (keys, col_out)
    variable a : std_logic;
  begin
    for r in 0 to 7 loop
      a := '0';
      for sl in 0 to 7 loop
        if col_out(sl) = '0' and keys(vic2c64(r * 8 + sl)) = '1' then
          a := '1';
        end if;
      end loop;
      row_in(r) <= not a;
    end loop;
  end process;
end architecture;

----------------------------------------------------------------------------------------------
-- Audio: the VIC's 6 bit sum (0..60, changing at the pixel enable) averaged over 656 sys_clk
-- (48.06 kHz), centred and scaled to a signed 24 bit sample, handed to mclk by a toggle: the
-- sample is stable for 656 clocks, mclk reads it two clocks after the toggle.
----------------------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vc20_audio is
  port (
    clk     : in  std_logic;
    a_in    : in  std_logic_vector(5 downto 0);
    mute    : in  std_logic;                   -- the pause: the next samples are exactly 0
    mclk    : in  std_logic;
    audio_l : out signed(23 downto 0);
    audio_r : out signed(23 downto 0)
  );
end entity;

architecture rtl of vc20_audio is
  signal acc  : unsigned(15 downto 0) := (others => '0');
  signal cnt  : integer range 0 to 655 := 0;
  signal avg  : unsigned(15 downto 0) := (others => '0');
  signal tgl  : std_logic := '0';
  signal t_s  : std_logic_vector(2 downto 0) := (others => '0');
  signal smp  : signed(23 downto 0) := (others => '0');
  attribute ASYNC_REG : string;
  attribute ASYNC_REG of t_s : signal is "TRUE";
begin
  process (clk)
  begin
    if rising_edge(clk) then
      if cnt = 655 then
        cnt <= 0;
        if mute = '1' then
          avg <= to_unsigned(19680, 16);
        else
          avg <= acc + resize(unsigned(a_in), 16);
        end if;
        acc <= (others => '0');
        tgl <= not tgl;
      else
        cnt <= cnt + 1;
        acc <= acc + resize(unsigned(a_in), 16);
      end if;
    end if;
  end process;

  process (mclk)
    variable c : signed(23 downto 0);
  begin
    if rising_edge(mclk) then
      t_s <= t_s(1 downto 0) & tgl;
      if t_s(2) /= t_s(1) then
        -- centre 30 * 656 = 19680, scale by 256: full swing +-5 M of +-8.4 M
        c := shift_left(resize(signed(resize(avg, 17)) - to_signed(19680, 17), 24), 8);
        smp <= c;
      end if;
    end if;
  end process;
  audio_l <= smp;
  audio_r <= smp;
end architecture;

----------------------------------------------------------------------------------------------
-- The app's DMA window (a toggle per byte, ultimate_sys.vhd) and the host's second memory port.
-- The window asks the host for the port (win_port_use / win_port_*); the host merges it with the
-- JTAG and gives the winner back as port_req / port_* in the same clock, the read data comes back
-- on port_rdata in the next clock (as dma_win.v does it for the C64).
-- The port's address space: port_rom = 0: the 64 KB VIC-20 map (RAM, colour RAM $9400, the ROMs
-- at $8000 char, $C000 BASIC, $E000 KERNAL); port_rom = 1: the app's ROM block as for the C64
-- (offset: char $0000, KERNAL $2000, BASIC $4000, colour RAM $6000, the colour RAM aperture).
----------------------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vc20_win is
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
    port_rdata   : in  std_logic_vector(7 downto 0);
    -- the memory side: the host's port as it comes back
    p_req        : in  std_logic;
    p_addr       : in  std_logic_vector(15 downto 0);
    p_rom        : in  std_logic;
    p_we         : in  std_logic;
    p_wdata      : in  std_logic_vector(7 downto 0);
    conf_wr      : out std_logic;
    conf_ai      : out std_logic_vector(15 downto 0);
    conf_di      : out std_logic_vector(7 downto 0)
  );
end entity;

architecture rtl of vc20_win is
  signal dreq_s  : std_logic_vector(2 downto 0) := (others => '0');
  signal done    : std_logic := '0';
  signal rd      : std_logic_vector(7 downto 0) := (others => '0');
  signal p_ram   : std_logic := '0';
  signal ram_rd  : std_logic := '0';
  signal pr_addr : std_logic_vector(15 downto 0) := (others => '0');
  signal pr_rom  : std_logic := '0';
  signal pr_we   : std_logic := '0';
  signal pr_wd   : std_logic_vector(7 downto 0) := (others => '0');
  signal use_i   : std_logic;
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

  process (clk)
  begin
    if rising_edge(clk) then
      dreq_s <= dreq_s(1 downto 0) & dma_req_tgl;
      ram_rd <= use_i;
      if dreq_s(2) /= dreq_s(1) then
        p_ram   <= '1';
        pr_addr <= dma_addr;
        pr_rom  <= dma_rom;
        pr_we   <= dma_we;
        pr_wd   <= dma_wdata;
      end if;
      if use_i = '1' then
        p_ram <= '0';
      end if;
      if ram_rd = '1' then
        done <= not done;
        if pr_we = '1' then
          rd <= x"00";
        elsif pr_rom = '1' and pr_addr(15 downto 13) = "011" then
          rd <= x"F" & port_rdata(3 downto 0);        -- the colour RAM aperture reads $Fx
        else
          rd <= port_rdata;
        end if;
      end if;
    end if;
  end process;

  -- the memory side: one address decode for the whole port
  process (p_req, p_addr, p_rom, p_we, p_wdata)
    variable a : std_logic_vector(15 downto 0);
  begin
    a := p_addr;
    if p_rom = '1' then
      a := x"9000";                                   -- no array there
      case p_addr(15 downto 13) is
        when "000" => if p_addr(12) = '0' then a := x"8" & p_addr(11 downto 0); end if;
        when "001" => a := "111" & p_addr(12 downto 0);
        when "010" => a := "110" & p_addr(12 downto 0);
        when "011" => a := "100101" & p_addr(9 downto 0);       -- colour RAM $9400
        when others => null;
      end case;
    end if;
    conf_ai <= a;
    conf_di <= p_wdata;
    conf_wr <= p_req and p_we;
  end process;
end architecture;
