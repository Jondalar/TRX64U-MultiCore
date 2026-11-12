-- SPDX-License-Identifier: GPL-3.0-or-later
-- T88 D10: the register block the app sees at its core window (cw_*, cpu_clk, the bytes at 0x101A0000; the same window trx_io.v
-- answers for the C64 core), laid out like c264_regs.v. Written for this project (TREX), no upstream code.
--   000-003 ID        "A8XL" (a build with ID_TRX2 = 1 answers "TRX2")
--   004 CAPS 0xC0     b6 page 6 (pixel layer), b7 RGB565 pixel layer: the host's, forwarded below
--   005 CAPS2 0x02    b1 BUILD is there
--   008-00B BUILD     the bitstream's build stamp (USR_ACCESS)
--   010 IRQ_EN, 014 IRQ_CAUSE (W1C)   b7 page flip done (px_ev_flip), b6 video frame start (px_ev_vsync), b5 an Atari frame is complete
--   020-027 DDR_CAL   the host's DDR2 calibration, as for the C64 core
--   0A0 CTRL     RW   b0 reset hold (cold reset), b1 halt (Freeze), b2 keep the RAM through a reset, b3 flush the cache (level)
--   0A1 RAM_SEL  RW   b2:0 RAM_SELECT of the core: 0 64 KB, 1 128 KB, 2 320 KB Compy Shop, 3 320 KB Rambo, 4 576 KB Compy Shop,
--                     5 576 KB Rambo, 6 1,088 KB Rambo (7, 4 MB Axlon, needs the whole 4 MB window); taken at the core's next reset
--   0A2 FLAGS    RW   b0 swap the joystick ports, b1 PAL (1) / NTSC (0) machine (taken at the next reset), b2 XEX loader mode
--   0A3 CART1    RW   emulated cartridge type (the cartridge image is in the DDR2 window at +0x400000); 0 = none
--   0A4 CART2    RW   the second (stacked) cartridge type; 0 = none
--   0A5 THROTTLE RW   b5:0 THROTTLE_COUNT_6502 (1 = the machine's speed, 2 = 2x, 4 = 4x ...)
--   0A6 ROM_SEL  RW   b1:0 the array behind the app's 16 KB ROM aperture: 0 XL OS, 1 BASIC, 2 PBI/XEX loader ROM
--   0A8 STATUS   R    b0 machine in reset, b1 held (Freeze), b2 PAL, b3 a memory access is on its way to the DDR2
--   0AC-0AD FRAMES    R   frame counter of the machine (low 16 bits)
--   0B0-0B1 CPUADDR   R   the bus address of the machine (a sample, read twice to be sure)
--   0C0 FB_LATEST R   b7 valid, b1:0 the buffer of the newest complete frame
--   0C1 FRAME_SEQ R   8 bit counter that changes with every complete frame
--   0C2-0C3 FB_OVERRUNS R   lines that could not be handed to the DDR2 side in time
--   0C4-0C7 FB_BASE   RW  DDR2 byte address of frame buffer 0 (default 0x01D00000: window base 0x01000000 + 0xD00000)
--   0C8-0CB FB_STRIDE RW  bytes between the four frame buffers (default 0x00100000)
--   0D0 SIO_ADDR RW   b4:0 register of the SIO handler (0 transmit, 1 tx status, 2 receive, 3 rx status, 4 divisor, 5 errors)
--   0D1 SIO_WDATA RW  data for a write
--   0D2 SIO_GO   W    b0 read the register SIO_ADDR, b1 write SIO_WDATA into it; the result is in SIO_RDATA when SIO_STATUS b0 is 0
--   0D3 SIO_STATUS R  b0 an operation is under way (it needs machine ticks: it does not finish while the machine is held)
--   0D4-0D5 SIO_RDATA R   15 bit result of the last read
--   0E0-0E3 PADDLE0-3 RW  signed paddle positions (default 0x80), the app's stand-in for the C64U paddle path (T47)
--   0F0-0FF   R   DDR2 cache counters: hits, misses, writes, stalled clocks (32 bit each, low byte first)
--   page 6 (0x6000-0x6FFF)   forwarded to the host's pixel layer (px_we, px_re, px_rdata)
--   everything else reads 0 and is acknowledged; the EEPROM window (addr[16]) is not here.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity a8_regs is
  generic (ID_TRX2 : integer := 0);
  port (
    clk        : in  std_logic;                       -- cpu_clk
    we         : in  std_logic;
    re         : in  std_logic;
    addr       : in  std_logic_vector(16 downto 0);
    wdata      : in  std_logic_vector(7 downto 0);
    rdata      : out std_logic_vector(7 downto 0);
    ack        : out std_logic;
    irq        : out std_logic;
    build      : in  std_logic_vector(31 downto 0);
    ddr_cal    : in  std_logic_vector(63 downto 0);
    px_we      : out std_logic;
    px_re      : out std_logic;
    px_rdata   : in  std_logic_vector(7 downto 0);
    ev_vsync   : in  std_logic;
    ev_flip    : in  std_logic;
    ev_frame   : in  std_logic;
    -- the core's side (the values cross from and to sys_clk through the caller's synchronisers)
    ctrl       : out std_logic_vector(7 downto 0);
    ram_sel    : out std_logic_vector(2 downto 0);
    flags      : out std_logic_vector(7 downto 0);
    cart1      : out std_logic_vector(7 downto 0);
    cart2      : out std_logic_vector(7 downto 0);
    throttle   : out std_logic_vector(5 downto 0);
    rom_sel    : out std_logic_vector(1 downto 0);
    fb_base    : out std_logic_vector(31 downto 0);
    fb_stride  : out std_logic_vector(31 downto 0);
    paddle0    : out std_logic_vector(7 downto 0);
    paddle1    : out std_logic_vector(7 downto 0);
    paddle2    : out std_logic_vector(7 downto 0);
    paddle3    : out std_logic_vector(7 downto 0);
    sio_addr   : out std_logic_vector(4 downto 0);
    sio_wdata  : out std_logic_vector(7 downto 0);
    sio_req_tgl : out std_logic;
    sio_wr     : out std_logic;
    sio_done_tgl : in  std_logic;
    sio_rdata  : in  std_logic_vector(14 downto 0);
    status     : in  std_logic_vector(7 downto 0);
    frames     : in  std_logic_vector(15 downto 0);
    cpuaddr    : in  std_logic_vector(15 downto 0);
    fb_latest  : in  std_logic_vector(7 downto 0);
    fb_seq     : in  std_logic_vector(7 downto 0);
    fb_over    : in  std_logic_vector(15 downto 0);
    st_hits    : in  std_logic_vector(31 downto 0);
    st_miss    : in  std_logic_vector(31 downto 0);
    st_wr      : in  std_logic_vector(31 downto 0);
    st_stall   : in  std_logic_vector(31 downto 0));
end entity;

architecture rtl of a8_regs is
  signal ctrl_r    : std_logic_vector(7 downto 0) := x"00";
  signal ramsel_r  : std_logic_vector(2 downto 0) := "000";
  signal flags_r   : std_logic_vector(7 downto 0) := x"02";     -- PAL
  signal cart1_r, cart2_r : std_logic_vector(7 downto 0) := x"00";
  signal thr_r     : std_logic_vector(5 downto 0) := "000001";
  signal romsel_r  : std_logic_vector(1 downto 0) := "00";
  signal base_r    : std_logic_vector(31 downto 0) := x"01D00000";
  signal stride_r  : std_logic_vector(31 downto 0) := x"00100000";
  signal pad0, pad1, pad2, pad3 : std_logic_vector(7 downto 0) := x"80";
  signal sioa_r    : std_logic_vector(4 downto 0) := (others => '0');
  signal siod_r    : std_logic_vector(7 downto 0) := (others => '0');
  signal sreq_tgl  : std_logic := '0';
  signal swr_r     : std_logic := '0';
  signal sdone_s   : std_logic_vector(2 downto 0) := "000";
  attribute ASYNC_REG : string;
  attribute ASYNC_REG of sdone_s : signal is "TRUE";
  signal sio_busy  : std_logic;
  signal irq_en    : std_logic_vector(7 downto 0) := (others => '0');
  signal cause     : std_logic_vector(7 downto 0) := (others => '0');
  signal q         : std_logic_vector(7 downto 0) := (others => '0');
  signal q_p6      : std_logic := '0';
  signal ack_r     : std_logic := '0';
  -- the sys_clk values, accepted when two samples in a row agree
  type v32_t is array (0 to 3) of std_logic_vector(31 downto 0);
  signal st_a, st_b, st_q : v32_t := (others => (others => '0'));
  signal st_in            : v32_t;
  signal f_a, f_b, f_q    : std_logic_vector(15 downto 0) := (others => '0');
  signal c_a, c_b, c_q    : std_logic_vector(15 downto 0) := (others => '0');
  signal s_a, s_b, s_q    : std_logic_vector(7 downto 0) := (others => '0');
  signal l_a, l_b, l_q    : std_logic_vector(7 downto 0) := (others => '0');
  signal n_a, n_b, n_q    : std_logic_vector(7 downto 0) := (others => '0');
  signal o_a, o_b, o_q    : std_logic_vector(15 downto 0) := (others => '0');
  signal sr_a, sr_b, sr_q : std_logic_vector(14 downto 0) := (others => '0');
  signal win       : std_logic;
  signal page      : std_logic_vector(3 downto 0);
  signal off       : std_logic_vector(11 downto 0);
  signal ev        : std_logic_vector(7 downto 0);
  signal id        : std_logic_vector(31 downto 0);
begin
  win  <= addr(16);
  page <= addr(15 downto 12);
  off  <= addr(11 downto 0);
  ev   <= ev_flip & ev_vsync & ev_frame & "00000";
  px_we <= we and not win when page = "0110" else '0';
  px_re <= re and not win when page = "0110" else '0';
  ctrl <= ctrl_r; ram_sel <= ramsel_r; flags <= flags_r; cart1 <= cart1_r; cart2 <= cart2_r;
  throttle <= thr_r; rom_sel <= romsel_r; fb_base <= base_r; fb_stride <= stride_r;
  paddle0 <= pad0; paddle1 <= pad1; paddle2 <= pad2; paddle3 <= pad3;
  sio_addr <= sioa_r; sio_wdata <= siod_r; sio_req_tgl <= sreq_tgl; sio_wr <= swr_r;
  sio_busy <= sreq_tgl xor sdone_s(1);
  ack  <= ack_r;
  irq  <= '1' when (irq_en and cause) /= x"00" else '0';
  rdata <= px_rdata when q_p6 = '1' else q;
  id <= x"32585254" when ID_TRX2 /= 0 else x"4C583841";           -- bytes "TRX2" / "A8XL", byte 0 first
  st_in <= (st_hits, st_miss, st_wr, st_stall);

  process (clk)
    variable o : integer;
  begin
    if rising_edge(clk) then
      for i in 0 to 3 loop
        st_a(i) <= st_in(i); st_b(i) <= st_a(i); if st_a(i) = st_b(i) then st_q(i) <= st_b(i); end if;
      end loop;
      f_a <= frames;    f_b <= f_a;    if f_a = f_b then f_q <= f_b; end if;
      c_a <= cpuaddr;   c_b <= c_a;    if c_a = c_b then c_q <= c_b; end if;
      s_a <= status;    s_b <= s_a;    if s_a = s_b then s_q <= s_b; end if;
      l_a <= fb_latest; l_b <= l_a;    if l_a = l_b then l_q <= l_b; end if;
      n_a <= fb_seq;    n_b <= n_a;    if n_a = n_b then n_q <= n_b; end if;
      o_a <= fb_over;   o_b <= o_a;    if o_a = o_b then o_q <= o_b; end if;
      sr_a <= sio_rdata; sr_b <= sr_a; if sr_a = sr_b then sr_q <= sr_b; end if;
      sdone_s <= sdone_s(1 downto 0) & sio_done_tgl;
      cause <= cause or ev;
      ack_r <= '0';
      if re = '1' or we = '1' then ack_r <= '1'; end if;
      q_p6 <= '0';
      if re = '1' and win = '0' and page = "0110" then q_p6 <= '1'; end if;

      if we = '1' and win = '0' and page = "0000" then
        case off is
          when x"010" => irq_en <= wdata;
          when x"014" => cause <= (cause and not wdata) or ev;
          when x"0A0" => ctrl_r <= wdata;
          when x"0A1" => ramsel_r <= wdata(2 downto 0);
          when x"0A2" => flags_r <= wdata;
          when x"0A3" => cart1_r <= wdata;
          when x"0A4" => cart2_r <= wdata;
          when x"0A5" => thr_r <= wdata(5 downto 0);
          when x"0A6" => romsel_r <= wdata(1 downto 0);
          when x"0C4" => base_r(7 downto 0) <= wdata;
          when x"0C5" => base_r(15 downto 8) <= wdata;
          when x"0C6" => base_r(23 downto 16) <= wdata;
          when x"0C7" => base_r(31 downto 24) <= wdata;
          when x"0C8" => stride_r(7 downto 0) <= wdata;
          when x"0C9" => stride_r(15 downto 8) <= wdata;
          when x"0CA" => stride_r(23 downto 16) <= wdata;
          when x"0CB" => stride_r(31 downto 24) <= wdata;
          when x"0D0" => sioa_r <= wdata(4 downto 0);
          when x"0D1" => siod_r <= wdata;
          when x"0D2" =>
            if wdata(1 downto 0) /= "00" and sio_busy = '0' then
              swr_r <= wdata(1); sreq_tgl <= not sreq_tgl;
            end if;
          when x"0E0" => pad0 <= wdata;
          when x"0E1" => pad1 <= wdata;
          when x"0E2" => pad2 <= wdata;
          when x"0E3" => pad3 <= wdata;
          when others => null;
        end case;
      end if;

      q <= x"00";
      if win = '0' and page = "0000" then
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
          when x"0A1" => q <= "00000" & ramsel_r;
          when x"0A2" => q <= flags_r;
          when x"0A3" => q <= cart1_r;
          when x"0A4" => q <= cart2_r;
          when x"0A5" => q <= "00" & thr_r;
          when x"0A6" => q <= "000000" & romsel_r;
          when x"0A8" => q <= s_q;
          when x"0AC" => q <= f_q(7 downto 0);
          when x"0AD" => q <= f_q(15 downto 8);
          when x"0B0" => q <= c_q(7 downto 0);
          when x"0B1" => q <= c_q(15 downto 8);
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
          when x"0D0" => q <= "000" & sioa_r;
          when x"0D1" => q <= siod_r;
          when x"0D3" => q <= "0000000" & sio_busy;
          when x"0D4" => q <= sr_q(7 downto 0);
          when x"0D5" => q <= '0' & sr_q(14 downto 8);
          when x"0E0" => q <= pad0;
          when x"0E1" => q <= pad1;
          when x"0E2" => q <= pad2;
          when x"0E3" => q <= pad3;
          when others =>
            if off(11 downto 4) = x"0F" then
              o := to_integer(unsigned(off(3 downto 2)));
              case off(1 downto 0) is
                when "00" => q <= st_q(o)(7 downto 0);
                when "01" => q <= st_q(o)(15 downto 8);
                when "10" => q <= st_q(o)(23 downto 16);
                when others => q <= st_q(o)(31 downto 24);
              end case;
            end if;
        end case;
      end if;
    end if;
  end process;
end architecture;
