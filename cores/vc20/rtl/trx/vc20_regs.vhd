-- TRX VC-20 core: the register block the app sees at its core window (cw_*, cpu_clk, the bytes at
-- 0x101A0000; the same window trx_io.v answers for the C64 core). Written for this project.
--   000-003 ID        "VC20" (a build with the generic ID_TRX2 = 1 answers "TRX2")
--   004 CAPS 0xC0     b6 page 6 (pixel layer), b7 RGB565 pixel layer: the host's, forwarded below
--   005 CAPS2 0x02    b1 BUILD is there
--   008-00B BUILD     the bitstream's build stamp (USR_ACCESS)
--   010 IRQ_EN, 014 IRQ_CAUSE (W1C)   b7 page flip done (px_ev_flip), b6 video frame start (px_ev_vsync), b5 a VIC-20 frame is complete in the
--                     frame buffer (vc20_pixw)
--   020-027 DDR_CAL   the host's DDR2 calibration, as for the C64 core
--   0A0 CTRL     RW   b0 reset hold, b1 halt (b2 had the identity palette of the old scaler path; it does nothing since T119)
--   0A1 RAM_EXT  RW   b0 3K at $0400, b1 8K at $2000, b2 8K at $4000, b3 8K at $6000, b4 8K at $A000
--   0A2 RAM_RO   RW   the same bits: the block is read only (b4: a cartridge image)
--   0A4 STATUS   R    b0 machine in reset, b1 halted
--   0A8-0A9 FRAMES    R   frame counter (low 16 bits)
--   0AC-0AD CPUADDR   R   the CPU's address bus (a sample, read twice to be sure)
--   0C0 FB_LATEST R   b7 valid, b1:0 the buffer of the newest complete frame
--   0C1 FRAME_SEQ R   8 bit counter that changes with every complete frame
--   0C2-0C3 FB_OVERRUNS R   lines that could not be handed to the DDR2 side in time
--   0C4-0C7 FB_BASE   RW  DDR2 byte address of frame buffer 0 (default 0x01D00000: the REU block of the host, as the C264 core's)
--   0C8-0CB FB_STRIDE RW  bytes between the four frame buffers (default 0x00100000)
--   page 6 (0x6000-0x6FFF)   forwarded to the host's pixel layer (px_we, px_re, px_rdata)
--   everything else reads 0 and is acknowledged; the EEPROM window (addr[16]) is not here.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vc20_regs is
  generic (ID_TRX2 : integer := 0);
  port (
    clk      : in  std_logic;                       -- cpu_clk
    we       : in  std_logic;
    re       : in  std_logic;
    addr     : in  std_logic_vector(16 downto 0);
    wdata    : in  std_logic_vector(7 downto 0);
    rdata    : out std_logic_vector(7 downto 0);
    ack      : out std_logic;
    irq      : out std_logic;
    build    : in  std_logic_vector(31 downto 0);
    ddr_cal  : in  std_logic_vector(63 downto 0);
    px_we    : out std_logic;
    px_re    : out std_logic;
    px_rdata : in  std_logic_vector(7 downto 0);
    ev_vsync : in  std_logic;
    ev_flip  : in  std_logic;
    ev_frame : in  std_logic;                       -- a VIC-20 frame is complete in the frame buffer (synchronised by the caller)
    -- the core's side (the values cross from and to sys_clk through the caller's synchronisers)
    ctrl     : out std_logic_vector(7 downto 0);
    ram_ext  : out std_logic_vector(4 downto 0);
    ram_ro   : out std_logic_vector(4 downto 0);
    fb_base  : out std_logic_vector(31 downto 0);
    fb_stride : out std_logic_vector(31 downto 0);
    status   : in  std_logic_vector(7 downto 0);
    frames   : in  std_logic_vector(15 downto 0);
    cpuaddr  : in  std_logic_vector(15 downto 0);
    fb_latest : in  std_logic_vector(7 downto 0);
    fb_seq   : in  std_logic_vector(7 downto 0);
    fb_over  : in  std_logic_vector(15 downto 0)
  );
end entity;

architecture rtl of vc20_regs is
  signal ctrl_r   : std_logic_vector(7 downto 0) := x"00";
  signal ext_r    : std_logic_vector(4 downto 0) := (others => '0');
  signal ro_r     : std_logic_vector(4 downto 0) := (others => '0');
  signal base_r   : std_logic_vector(31 downto 0) := x"01D00000";
  signal stride_r : std_logic_vector(31 downto 0) := x"00100000";
  signal irq_en   : std_logic_vector(7 downto 0) := (others => '0');
  signal cause    : std_logic_vector(7 downto 0) := (others => '0');
  signal q        : std_logic_vector(7 downto 0) := (others => '0');
  signal q_p6     : std_logic := '0';
  signal ack_r    : std_logic := '0';
  signal win      : std_logic;
  signal page     : std_logic_vector(3 downto 0);
  signal off      : std_logic_vector(11 downto 0);
  signal ev       : std_logic_vector(7 downto 0);
  -- the sys_clk values, accepted when two samples in a row agree
  signal f_a, f_b, f_q : std_logic_vector(15 downto 0) := (others => '0');
  signal c_a, c_b, c_q : std_logic_vector(15 downto 0) := (others => '0');
  signal s_a, s_b, s_q : std_logic_vector(7 downto 0) := (others => '0');
  signal l_a, l_b, l_q : std_logic_vector(7 downto 0) := (others => '0');
  signal n_a, n_b, n_q : std_logic_vector(7 downto 0) := (others => '0');
  signal o_a, o_b, o_q : std_logic_vector(15 downto 0) := (others => '0');
begin
  win  <= addr(16);
  page <= addr(15 downto 12);
  off  <= addr(11 downto 0);
  ev   <= ev_flip & ev_vsync & ev_frame & "00000";
  px_we <= we and not win when page = x"6" else '0';
  px_re <= re and not win when page = x"6" else '0';
  ctrl    <= ctrl_r;
  ram_ext <= ext_r;
  ram_ro  <= ro_r;
  fb_base <= base_r;
  fb_stride <= stride_r;
  ack   <= ack_r;
  irq   <= '1' when (irq_en and cause) /= x"00" else '0';
  rdata <= px_rdata when q_p6 = '1' else q;

  process (clk)
    variable id : std_logic_vector(31 downto 0);
  begin
    if rising_edge(clk) then
      f_a <= frames; f_b <= f_a; if f_a = f_b then f_q <= f_b; end if;
      c_a <= cpuaddr; c_b <= c_a; if c_a = c_b then c_q <= c_b; end if;
      s_a <= status; s_b <= s_a; if s_a = s_b then s_q <= s_b; end if;
      l_a <= fb_latest; l_b <= l_a; if l_a = l_b then l_q <= l_b; end if;
      n_a <= fb_seq; n_b <= n_a; if n_a = n_b then n_q <= n_b; end if;
      o_a <= fb_over; o_b <= o_a; if o_a = o_b then o_q <= o_b; end if;
      cause <= cause or ev;
      ack_r <= '0';
      if (re = '1' or we = '1') then ack_r <= '1'; end if;
      q_p6 <= '0';
      if re = '1' and win = '0' and page = x"6" then q_p6 <= '1'; end if;

      if we = '1' and win = '0' and page = x"0" then
        case off is
          when x"010" => irq_en <= wdata;
          when x"014" => cause <= (cause and not wdata) or ev;
          when x"0A0" => ctrl_r <= wdata;
          when x"0A1" => ext_r <= wdata(4 downto 0);
          when x"0A2" => ro_r <= wdata(4 downto 0);
          when x"0C4" => base_r(7 downto 0) <= wdata;
          when x"0C5" => base_r(15 downto 8) <= wdata;
          when x"0C6" => base_r(23 downto 16) <= wdata;
          when x"0C7" => base_r(31 downto 24) <= wdata;
          when x"0C8" => stride_r(7 downto 0) <= wdata;
          when x"0C9" => stride_r(15 downto 8) <= wdata;
          when x"0CA" => stride_r(23 downto 16) <= wdata;
          when x"0CB" => stride_r(31 downto 24) <= wdata;
          when others => null;
        end case;
      end if;

      if ID_TRX2 /= 0 then id := x"32585254"; else id := x"30324356"; end if;   -- bytes "TRX2" / "VC20", byte 0 first
      q <= x"00";
      if win = '0' and page = x"0" then
        case off is
          when x"000" => q <= id(7 downto 0);
          when x"001" => q <= id(15 downto 8);
          when x"002" => q <= id(23 downto 16);
          when x"003" => q <= id(31 downto 24);
          when x"004" => q <= x"C0";
          when x"005" => q <= x"02";
          when x"008" => q <= build(7 downto 0);
          when x"009" => q <= build(15 downto 8);
          when x"00A" => q <= build(23 downto 16);
          when x"00B" => q <= build(31 downto 24);
          when x"010" => q <= irq_en;
          when x"014" => q <= cause;
          when x"020" => q <= ddr_cal(7 downto 0);
          when x"021" => q <= ddr_cal(15 downto 8);
          when x"022" => q <= ddr_cal(23 downto 16);
          when x"023" => q <= ddr_cal(31 downto 24);
          when x"024" => q <= ddr_cal(39 downto 32);
          when x"025" => q <= ddr_cal(47 downto 40);
          when x"026" => q <= ddr_cal(55 downto 48);
          when x"027" => q <= ddr_cal(63 downto 56);
          when x"0A0" => q <= ctrl_r;
          when x"0A1" => q <= "000" & ext_r;
          when x"0A2" => q <= "000" & ro_r;
          when x"0A4" => q <= s_q;
          when x"0A8" => q <= f_q(7 downto 0);
          when x"0A9" => q <= f_q(15 downto 8);
          when x"0AC" => q <= c_q(7 downto 0);
          when x"0AD" => q <= c_q(15 downto 8);
          when x"0C0" => q <= l_q;
          when x"0C1" => q <= n_q;
          when x"0C2" => q <= o_q(7 downto 0);
          when x"0C3" => q <= o_q(15 downto 8);
          when x"0C4" => q <= base_r(7 downto 0);
          when x"0C5" => q <= base_r(15 downto 8);
          when x"0C6" => q <= base_r(23 downto 16);
          when x"0C7" => q <= base_r(31 downto 24);
          when x"0C8" => q <= stride_r(7 downto 0);
          when x"0C9" => q <= stride_r(15 downto 8);
          when x"0CA" => q <= stride_r(23 downto 16);
          when x"0CB" => q <= stride_r(31 downto 24);
          when others => null;
        end case;
      end if;
    end if;
  end process;
end architecture;
