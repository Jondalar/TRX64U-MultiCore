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
--   0B0 TAPE_PIN RW   b0 MOTOR pin driven, b1 WRITE pin driven, b4 the datasette's READ and SENSE pins are the VIC-20's inputs (reset 0 = every pin released), as PET_TAPE_PIN
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
    tape_pin : out std_logic_vector(7 downto 0);
    status   : in  std_logic_vector(7 downto 0);
    frames   : in  std_logic_vector(15 downto 0);
    cpuaddr  : in  std_logic_vector(15 downto 0);
    fb_latest : in  std_logic_vector(7 downto 0);
    fb_seq   : in  std_logic_vector(7 downto 0);
    fb_over  : in  std_logic_vector(15 downto 0)
  );
end entity;

architecture rtl of vc20_regs is
  -- >>> generated from cores/vc20/app/gateware.toml (T264 S2): do not edit, `tools/toml_a_gen.py gen` rewrites it
  constant R_IDENT_ID : integer := 16#000#;
  constant R_IDENT_CAPS : integer := 16#004#;
  constant R_IDENT_CAPS2 : integer := 16#005#;
  constant R_IDENT_BUILD : integer := 16#008#;
  constant R_IRQ_IRQ_EN : integer := 16#010#;
  constant R_IRQ_IRQ_CAUSE : integer := 16#014#;
  constant R_DDR_CAL_DDR_CAL0 : integer := 16#020#;
  constant R_DDR_CAL_DDR_CAL1 : integer := 16#024#;
  constant R_MACHINE_CTRL : integer := 16#0A0#;
  constant R_MACHINE_RAM_EXT : integer := 16#0A1#;
  constant R_MACHINE_RAM_RO : integer := 16#0A2#;
  constant R_MACHINE_STATUS : integer := 16#0A4#;
  constant R_MACHINE_FRAMES : integer := 16#0A8#;
  constant R_MACHINE_CPU_ADDR : integer := 16#0AC#;
  constant R_TAPE_TAPE_PIN : integer := 16#0B0#;
  constant R_VIDEO_FB_LATEST : integer := 16#0C0#;
  constant R_VIDEO_FRAME_SEQ : integer := 16#0C1#;
  constant R_VIDEO_FB_OVERRUNS : integer := 16#0C2#;
  constant R_VIDEO_FB_BASE : integer := 16#0C4#;
  constant R_VIDEO_FB_STRIDE : integer := 16#0C8#;
  -- <<< generated
  signal ctrl_r   : std_logic_vector(7 downto 0) := x"00";
  signal ext_r    : std_logic_vector(4 downto 0) := (others => '0');
  signal tpin_r   : std_logic_vector(7 downto 0) := (others => '0');
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
  tape_pin <= tpin_r;
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
        case to_integer(unsigned(off)) is
          when R_IRQ_IRQ_EN => irq_en <= wdata;
          when R_IRQ_IRQ_CAUSE => cause <= (cause and not wdata) or ev;
          when R_MACHINE_CTRL => ctrl_r <= wdata;
          when R_MACHINE_RAM_EXT => ext_r <= wdata(4 downto 0);
          when R_TAPE_TAPE_PIN => tpin_r <= wdata;
          when R_MACHINE_RAM_RO => ro_r <= wdata(4 downto 0);
          when R_VIDEO_FB_BASE => base_r(7 downto 0) <= wdata;
          when R_VIDEO_FB_BASE + 1 => base_r(15 downto 8) <= wdata;
          when R_VIDEO_FB_BASE + 2 => base_r(23 downto 16) <= wdata;
          when R_VIDEO_FB_BASE + 3 => base_r(31 downto 24) <= wdata;
          when R_VIDEO_FB_STRIDE => stride_r(7 downto 0) <= wdata;
          when R_VIDEO_FB_STRIDE + 1 => stride_r(15 downto 8) <= wdata;
          when R_VIDEO_FB_STRIDE + 2 => stride_r(23 downto 16) <= wdata;
          when R_VIDEO_FB_STRIDE + 3 => stride_r(31 downto 24) <= wdata;
          when others => null;
        end case;
      end if;

      if ID_TRX2 /= 0 then id := x"32585254"; else id := x"30324356"; end if;   -- bytes "TRX2" / "VC20", byte 0 first
      q <= x"00";
      if win = '0' and page = x"0" then
        case to_integer(unsigned(off)) is
          when R_IDENT_ID => q <= id(7 downto 0);
          when R_IDENT_ID + 1 => q <= id(15 downto 8);
          when R_IDENT_ID + 2 => q <= id(23 downto 16);
          when R_IDENT_ID + 3 => q <= id(31 downto 24);
          when R_IDENT_CAPS => q <= x"C0";
          when R_IDENT_CAPS2 => q <= x"02";
          when R_IDENT_BUILD => q <= build(7 downto 0);
          when R_IDENT_BUILD + 1 => q <= build(15 downto 8);
          when R_IDENT_BUILD + 2 => q <= build(23 downto 16);
          when R_IDENT_BUILD + 3 => q <= build(31 downto 24);
          when R_IRQ_IRQ_EN => q <= irq_en;
          when R_IRQ_IRQ_CAUSE => q <= cause;
          when R_DDR_CAL_DDR_CAL0 => q <= ddr_cal(7 downto 0);
          when R_DDR_CAL_DDR_CAL0 + 1 => q <= ddr_cal(15 downto 8);
          when R_DDR_CAL_DDR_CAL0 + 2 => q <= ddr_cal(23 downto 16);
          when R_DDR_CAL_DDR_CAL0 + 3 => q <= ddr_cal(31 downto 24);
          when R_DDR_CAL_DDR_CAL1 => q <= ddr_cal(39 downto 32);
          when R_DDR_CAL_DDR_CAL1 + 1 => q <= ddr_cal(47 downto 40);
          when R_DDR_CAL_DDR_CAL1 + 2 => q <= ddr_cal(55 downto 48);
          when R_DDR_CAL_DDR_CAL1 + 3 => q <= ddr_cal(63 downto 56);
          when R_MACHINE_CTRL => q <= ctrl_r;
          when R_MACHINE_RAM_EXT => q <= "000" & ext_r;
          when R_TAPE_TAPE_PIN => q <= tpin_r;
          when R_MACHINE_RAM_RO => q <= "000" & ro_r;
          when R_MACHINE_STATUS => q <= s_q;
          when R_MACHINE_FRAMES => q <= f_q(7 downto 0);
          when R_MACHINE_FRAMES + 1 => q <= f_q(15 downto 8);
          when R_MACHINE_CPU_ADDR => q <= c_q(7 downto 0);
          when R_MACHINE_CPU_ADDR + 1 => q <= c_q(15 downto 8);
          when R_VIDEO_FB_LATEST => q <= l_q;
          when R_VIDEO_FRAME_SEQ => q <= n_q;
          when R_VIDEO_FB_OVERRUNS => q <= o_q(7 downto 0);
          when R_VIDEO_FB_OVERRUNS + 1 => q <= o_q(15 downto 8);
          when R_VIDEO_FB_BASE => q <= base_r(7 downto 0);
          when R_VIDEO_FB_BASE + 1 => q <= base_r(15 downto 8);
          when R_VIDEO_FB_BASE + 2 => q <= base_r(23 downto 16);
          when R_VIDEO_FB_BASE + 3 => q <= base_r(31 downto 24);
          when R_VIDEO_FB_STRIDE => q <= stride_r(7 downto 0);
          when R_VIDEO_FB_STRIDE + 1 => q <= stride_r(15 downto 8);
          when R_VIDEO_FB_STRIDE + 2 => q <= stride_r(23 downto 16);
          when R_VIDEO_FB_STRIDE + 3 => q <= stride_r(31 downto 24);
          when others => null;
        end case;
      end if;
    end if;
  end process;
end architecture;
