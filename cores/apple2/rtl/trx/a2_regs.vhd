-- TRX Apple //e core (T89 D11): the register block the app sees at its core window (cw_*, cpu_clk, the bytes at 0x101A0000; the
-- same window trx_io.v answers for the C64 core), laid out like nes_regs.v / vc20_regs.vhd. Written for this project (TREX).
--   000-003 ID        "APL2"
--   004 CAPS 0xC0     b6 page 6 (pixel layer), b7 RGB565 pixel layer: the host's, forwarded below
--   005 CAPS2 0x02    b1 BUILD is there
--   006 CONTRACT      the contract version the core was written for (contract/VERSION)
--   007 CAPS3 0x0F    b0 pixel layer video, b1 mono audio, b2 two joysticks, b3 wants the host's NTSC clock (sys_clk 32.7273 MHz)
--   008-00B BUILD     the bitstream's build stamp (USR_ACCESS)
--   010 IRQ_EN, 014 IRQ_CAUSE (W1C)   b7 page flip done (px_ev_flip), b6 video frame start (px_ev_vsync), b5 an Apple frame is complete,
--                                     b3 a drive wants a track (either drive)
--   020-027 DDR_CAL   the host's DDR2 calibration, as for the C64 core
--   005 CAPS2 0x06   b1 BUILD is there, b2 the bit level Disk II engine and the slot 7 card (T89.1): an application written for the byte engine must refuse
--   0A0 CTRL     RW   b0 warm reset hold (CTRL-RESET), b1 halt (Freeze), b2 write 1: cold reset (the power-up byte is written, as the Nano does)
--   0A1 CFG      RW   b1:0 monitor (0 colour 1 B&W 2 green 3 amber), b3:2 palette (0 original 1 IIgs 2 AppleWin 3 custom 100-12F),
--                     b4 scanlines, b5 character set (the ROMSWITCH pin: 0 first 4 KB of the character ROM file, 1 second), b6 swap the joystick ports
--   0A2 CAPS     RW   b0 CAPS LOCK as the app knows it (the C64U's SHIFT LOCK, bling_io 0x10100802 b1); 1 at power-up, so letters are capitals until the app says otherwise
--   0A4 STATUS   R    b0 machine in reset, b1 held (Freeze), b2 cold reset running, b3 CAPS LOCK, b4 aux RAM waits for the DDR2 now
--   0A8-0A9 FRAMES    R   Apple frame counter (low 16 bits)
--   0AC-0AD CPUADDR   R   the CPU's address bus (a sample)
--   0B0-0B3 AUX_MISS, 0B4-0B7 AUX_STALL R   line fetches of the aux RAM cache, sys_clk clocks the machine was held for the DDR2
--   0C0 FB_LATEST R   b7 valid, b1:0 the buffer of the newest complete frame;  0C1 FRAME_SEQ R;  0C2-0C3 FB_OVERRUNS R
--   0C4-0C7 FB_BASE RW   DDR2 byte address of frame buffer 0 (default 0x01D00000);  0C8-0CB FB_STRIDE RW (default 0x00100000)
--   0D0-0D3 AUX_BASE RW  DDR2 byte address of the aux RAM image (64 KB, default 0x01100000, bits 3:0 ignored)
--   0D4 WIN_SEL  RW   b3:0 the array the DMA window sees with dma_rom = 1 (a2_win.vhd)
--   0E0.. drive 1, 0E8.. drive 2:  +0 STATUS R (b0 request open, b1 it is a write-back, b2 dirty, b3 buffer valid, b4 motor on),
--                     +1 REQ_TRACK R (the QUARTER track to put in / take out, 0..139: the stepper's count, whole track N is 4N + 2), +2 HEAD R (the head's quarter track),
--                     +3 ACK W (any value: the request is done),
--                     +4 BITS_L, +5 BITS_H RW (bits of the track the app put in, written before ACK), +6 FLAGS RW (b0: the track is unformatted, random flux)
--   0F0 DISK_CTL RW   b0 drive 1 has an image, b1 drive 2 has one, b2 write protect 1, b3 write protect 2
--   0F1 DISK_CMD W    b0 image changed 1, b1 image changed 2 (the buffer is invalid), b2 flush 1, b3 flush 2 (write a dirty track back now)
--   0F2 DISK_CFG RW   b0 while a track loads keep playing the old one (default: random flux)
--   0F3 DISK_CLR W    any value: clear the counters below
--   160-163 LAT_MAX1, 164-167 LAT_MAX2  R  the longest a track request stayed open, sys_clk clocks (32.7 MHz)
--   168-169 REQS1, 16A-16B REQS2, 16C-16D STARVED  R  requests opened; reads of the data latch while the mounted drive's track was not in; 170-173 NOISE_CELLS R
--   180 HDD_STATUS R  b0 a block is wanted (the CPU waits), b1 it is a write (the app takes 512 bytes out of RAM), b2 unit 2, b3 language card bank 1 was selected when the command was issued (the CPU sees $D000-$DFFF at the RAM $C000-$CFFF)
--   182-183 HDD_BLOCK R, 184-185 HDD_MEM R (the buffer in main RAM), 186 HDD_RESULT W (0 ok; 1 error; the CPU gets it as the command's result), 187 HDD_ACK W
--   188 HDD_CTL RW b0 unit 1 mounted, b1 unit 2, b2 protected 1, b3 protected 2;  18A-18B HDD_SIZE1 RW, 18C-18D HDD_SIZE2 RW (blocks);  18E-18F HDD_CMDS R
--   100-12F PALETTE RW  the custom palette, entry n (0..15) = R, G, B at 100 + 3 n
--   140 KEY_INJ W     the app types: a 7 bit code into the keyboard queue;  141 KEY_QUEUED R   codes waiting
--   150 MOUSE_DX W (signed), 151 MOUSE_DY W (signed), 152 MOUSE_BTN RW (b0), 153 MOUSE_GO W (any: deliver the movement to the card)
--   page 6 (0x6000-0x6FFF)   forwarded to the host's pixel layer (px_we, px_re, px_rdata)
--   everything else reads 0 and is acknowledged; the EEPROM window (addr[16]) is not here.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity a2_regs is
  generic (
    CONTRACT : integer := 2
  );
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
    ev_frame : in  std_logic;
    ev_track : in  std_logic;
    ev_hdd   : in  std_logic;
    -- settings, to the core (cpu_clk: the core synchronises them)
    ctrl     : out std_logic_vector(7 downto 0);
    cold_tgl : out std_logic;
    cfg      : out std_logic_vector(7 downto 0);
    caps     : out std_logic;
    aux_base : out std_logic_vector(31 downto 0);
    fb_base  : out std_logic_vector(31 downto 0);
    fb_stride : out std_logic_vector(31 downto 0);
    win_sel  : out std_logic_vector(3 downto 0);
    disk_ctl : out std_logic_vector(7 downto 0);
    disk_cfg : out std_logic_vector(7 downto 0);
    bits1    : out std_logic_vector(15 downto 0);
    bits2    : out std_logic_vector(15 downto 0);
    trkfl    : out std_logic_vector(1 downto 0);
    hdd_ctl  : out std_logic_vector(7 downto 0);
    hdd_sz1  : out std_logic_vector(15 downto 0);
    hdd_sz2  : out std_logic_vector(15 downto 0);
    hdd_res  : out std_logic_vector(7 downto 0);
    palette  : out std_logic_vector(383 downto 0);
    cmd_tgl  : out std_logic_vector(9 downto 0);     -- a toggle per bit of DISK_CMD (0-3) and of ACK1 (4), ACK2 (5), KEY_INJ (6), MOUSE_GO (7), HDD_ACK (8), DISK_CLR (9)
    key_inj  : out std_logic_vector(6 downto 0);
    mouse_dx : out std_logic_vector(7 downto 0);
    mouse_dy : out std_logic_vector(7 downto 0);
    mouse_btn : out std_logic;
    -- the core's state, sys_clk values: 16 fields of 16 bits, each taken when two samples in a row agree
    sysv     : in  std_logic_vector(511 downto 0)
  );
end entity;

architecture rtl of a2_regs is
  signal ctrl_r  : std_logic_vector(7 downto 0) := x"00";
  signal cold_r  : std_logic := '0';
  signal cfg_r   : std_logic_vector(7 downto 0) := x"00";
  signal caps_r  : std_logic := '1';
  signal abase_r : std_logic_vector(31 downto 0) := x"01100000";
  signal fbase_r : std_logic_vector(31 downto 0) := x"01D00000";
  signal fstr_r  : std_logic_vector(31 downto 0) := x"00100000";
  signal sel_r   : std_logic_vector(3 downto 0) := x"0";
  signal dctl_r  : std_logic_vector(7 downto 0) := x"00";
  signal tgl_r   : std_logic_vector(9 downto 0) := (others => '0');
  signal dcfg_r  : std_logic_vector(7 downto 0) := x"00";
  signal bits1_r, bits2_r : std_logic_vector(15 downto 0) := (others => '0');
  signal fl_r    : std_logic_vector(1 downto 0) := "00";
  signal hctl_r  : std_logic_vector(7 downto 0) := x"00";
  signal hsz1_r, hsz2_r : std_logic_vector(15 downto 0) := (others => '0');
  signal hres_r  : std_logic_vector(7 downto 0) := x"00";
  signal inj_r   : std_logic_vector(6 downto 0) := (others => '0');
  signal mdx_r, mdy_r : std_logic_vector(7 downto 0) := (others => '0');
  signal mbtn_r  : std_logic := '0';
  type pal_t is array (0 to 47) of std_logic_vector(7 downto 0);
  signal pal_r   : pal_t := (others => (others => '0'));
  signal irq_en  : std_logic_vector(7 downto 0) := (others => '0');
  signal cause   : std_logic_vector(7 downto 0) := (others => '0');
  signal q       : std_logic_vector(7 downto 0) := (others => '0');
  signal q_p6    : std_logic := '0';
  signal ack_r   : std_logic := '0';
  signal win     : std_logic;
  signal page    : std_logic_vector(3 downto 0);
  signal off     : std_logic_vector(11 downto 0);
  signal ev      : std_logic_vector(7 downto 0);
  signal sv_a, sv_b, sv_q : std_logic_vector(511 downto 0) := (others => '0');
begin
  win  <= addr(16);
  page <= addr(15 downto 12);
  off  <= addr(11 downto 0);
  ev   <= ev_flip & ev_vsync & ev_frame & ev_hdd & ev_track & "000";
  px_we <= we and not win when page = x"6" else '0';
  px_re <= re and not win when page = x"6" else '0';
  ctrl     <= ctrl_r;
  cold_tgl <= cold_r;
  cfg      <= cfg_r;
  caps     <= caps_r;
  aux_base <= abase_r;
  fb_base  <= fbase_r;
  fb_stride <= fstr_r;
  win_sel  <= sel_r;
  disk_ctl <= dctl_r;
  disk_cfg <= dcfg_r;
  bits1    <= bits1_r;
  bits2    <= bits2_r;
  trkfl    <= fl_r;
  hdd_ctl  <= hctl_r;
  hdd_sz1  <= hsz1_r;
  hdd_sz2  <= hsz2_r;
  hdd_res  <= hres_r;
  cmd_tgl  <= tgl_r;
  key_inj  <= inj_r;
  mouse_dx <= mdx_r;
  mouse_dy <= mdy_r;
  mouse_btn <= mbtn_r;
  ack   <= ack_r;
  irq   <= '1' when (irq_en and cause) /= x"00" else '0';
  rdata <= px_rdata when q_p6 = '1' else q;

  g_pal : for i in 0 to 47 generate
    palette(8 * i + 7 downto 8 * i) <= pal_r(i);
  end generate;

  process (clk)
    variable o    : integer;
    variable f    : integer;
    variable fv   : std_logic_vector(15 downto 0);
    -- byte i of a vector, whatever range the vector (a slice) has
    function b4(v : std_logic_vector; i : integer) return std_logic_vector is
      variable w : std_logic_vector(v'length - 1 downto 0);
    begin
      w := v;
      return w(8 * i + 7 downto 8 * i);
    end function;
  begin
    if rising_edge(clk) then
      -- the sys_clk fields: accepted when two samples in a row agree
      sv_a <= sysv; sv_b <= sv_a;
      for i in 0 to 31 loop
        if sv_a(16 * i + 15 downto 16 * i) = sv_b(16 * i + 15 downto 16 * i) then
          sv_q(16 * i + 15 downto 16 * i) <= sv_b(16 * i + 15 downto 16 * i);
        end if;
      end loop;

      cause <= cause or ev;
      ack_r <= '0';
      if re = '1' or we = '1' then ack_r <= '1'; end if;
      q_p6 <= '0';
      if re = '1' and win = '0' and page = x"6" then q_p6 <= '1'; end if;

      if we = '1' and win = '0' and page = x"0" then
        o := to_integer(unsigned(off));
        case o is
          when 16#010# => irq_en <= wdata;
          when 16#014# => cause <= (cause and not wdata) or ev;
          when 16#0A0# => ctrl_r <= "000000" & wdata(1 downto 0);
                          if wdata(2) = '1' then cold_r <= not cold_r; end if;
          when 16#0A1# => cfg_r <= wdata;
          when 16#0A2# => caps_r <= wdata(0);
          when 16#0C4# => fbase_r(7 downto 0) <= wdata;
          when 16#0C5# => fbase_r(15 downto 8) <= wdata;
          when 16#0C6# => fbase_r(23 downto 16) <= wdata;
          when 16#0C7# => fbase_r(31 downto 24) <= wdata;
          when 16#0C8# => fstr_r(7 downto 0) <= wdata;
          when 16#0C9# => fstr_r(15 downto 8) <= wdata;
          when 16#0CA# => fstr_r(23 downto 16) <= wdata;
          when 16#0CB# => fstr_r(31 downto 24) <= wdata;
          when 16#0D0# => abase_r(7 downto 0) <= wdata;
          when 16#0D1# => abase_r(15 downto 8) <= wdata;
          when 16#0D2# => abase_r(23 downto 16) <= wdata;
          when 16#0D3# => abase_r(31 downto 24) <= wdata;
          when 16#0D4# => sel_r <= wdata(3 downto 0);
          when 16#0E3# => tgl_r(4) <= not tgl_r(4);
          when 16#0E4# => bits1_r(7 downto 0) <= wdata;
          when 16#0E5# => bits1_r(15 downto 8) <= wdata;
          when 16#0E6# => fl_r(0) <= wdata(0);
          when 16#0EB# => tgl_r(5) <= not tgl_r(5);
          when 16#0EC# => bits2_r(7 downto 0) <= wdata;
          when 16#0ED# => bits2_r(15 downto 8) <= wdata;
          when 16#0EE# => fl_r(1) <= wdata(0);
          when 16#0F2# => dcfg_r <= wdata;
          when 16#0F3# => tgl_r(9) <= not tgl_r(9);
          when 16#186# => hres_r <= wdata;
          when 16#187# => tgl_r(8) <= not tgl_r(8);
          when 16#188# => hctl_r <= wdata;
          when 16#18A# => hsz1_r(7 downto 0) <= wdata;
          when 16#18B# => hsz1_r(15 downto 8) <= wdata;
          when 16#18C# => hsz2_r(7 downto 0) <= wdata;
          when 16#18D# => hsz2_r(15 downto 8) <= wdata;
          when 16#0F0# => dctl_r <= wdata;
          when 16#0F1# =>
            for i in 0 to 3 loop
              if wdata(i) = '1' then tgl_r(i) <= not tgl_r(i); end if;
            end loop;
          when 16#140# => inj_r <= wdata(6 downto 0); tgl_r(6) <= not tgl_r(6);
          when 16#150# => mdx_r <= wdata;
          when 16#151# => mdy_r <= wdata;
          when 16#152# => mbtn_r <= wdata(0);
          when 16#153# => tgl_r(7) <= not tgl_r(7);
          when others =>
            if o >= 16#100# and o < 16#130# then pal_r(o - 16#100#) <= wdata; end if;
        end case;
      end if;

      q <= x"00";
      if win = '0' and page = x"0" then
        o := to_integer(unsigned(off));
        case o is
          when 16#000# => q <= x"41";   -- 'A'
          when 16#001# => q <= x"50";   -- 'P'
          when 16#002# => q <= x"4C";   -- 'L'
          when 16#003# => q <= x"32";   -- '2'
          when 16#004# => q <= x"C0";
          when 16#005# => q <= x"06";
          when 16#006# => q <= std_logic_vector(to_unsigned(CONTRACT, 8));
          when 16#007# => q <= x"0F";
          when 16#008# => q <= build(7 downto 0);
          when 16#009# => q <= build(15 downto 8);
          when 16#00A# => q <= build(23 downto 16);
          when 16#00B# => q <= build(31 downto 24);
          when 16#010# => q <= irq_en;
          when 16#014# => q <= cause;
          when 16#020# to 16#027# => q <= b4(ddr_cal, o - 16#020#);
          when 16#0A0# => q <= ctrl_r;
          when 16#0A1# => q <= cfg_r;
          when 16#0A2# => q <= "0000000" & caps_r;
          when 16#0A4# => q <= sv_q(8 * 16 + 7 downto 8 * 16);
          when 16#0A8# => q <= sv_q(7 downto 0);
          when 16#0A9# => q <= sv_q(15 downto 8);
          when 16#0AC# => q <= sv_q(16 + 7 downto 16);
          when 16#0AD# => q <= sv_q(16 + 15 downto 16 + 8);
          when 16#0B0# to 16#0B3# => q <= b4(sv_q(63 downto 32), o - 16#0B0#);
          when 16#0B4# to 16#0B7# => q <= b4(sv_q(95 downto 64), o - 16#0B4#);
          when 16#0C0# => q <= sv_q(7 * 16 + 15 downto 7 * 16 + 8);
          when 16#0C1# => q <= sv_q(7 * 16 + 7 downto 7 * 16);
          when 16#0C2# => q <= sv_q(6 * 16 + 7 downto 6 * 16);
          when 16#0C3# => q <= sv_q(6 * 16 + 15 downto 6 * 16 + 8);
          when 16#0C4# to 16#0C7# => q <= b4(fbase_r, o - 16#0C4#);
          when 16#0C8# to 16#0CB# => q <= b4(fstr_r, o - 16#0C8#);
          when 16#0D0# to 16#0D3# => q <= b4(abase_r, o - 16#0D0#);
          when 16#0D4# => q <= "0000" & sel_r;
          when 16#0E0# => q <= sv_q(9 * 16 + 7 downto 9 * 16);
          when 16#0E1# => q <= sv_q(13 * 16 + 7 downto 13 * 16);
          when 16#0E2# => q <= sv_q(14 * 16 + 7 downto 14 * 16);
          when 16#0E4# => q <= bits1_r(7 downto 0);
          when 16#0E5# => q <= bits1_r(15 downto 8);
          when 16#0E6# => q <= "0000000" & fl_r(0);
          when 16#0E8# => q <= sv_q(10 * 16 + 7 downto 10 * 16);
          when 16#0E9# => q <= sv_q(13 * 16 + 15 downto 13 * 16 + 8);
          when 16#0EA# => q <= sv_q(14 * 16 + 15 downto 14 * 16 + 8);
          when 16#0EC# => q <= bits2_r(7 downto 0);
          when 16#0ED# => q <= bits2_r(15 downto 8);
          when 16#0EE# => q <= "0000000" & fl_r(1);
          when 16#0F0# => q <= dctl_r;
          when 16#0F2# => q <= dcfg_r;
          when 16#160# to 16#163# => q <= b4(sv_q(18 * 16 + 31 downto 18 * 16), o - 16#160#);
          when 16#164# to 16#167# => q <= b4(sv_q(20 * 16 + 31 downto 20 * 16), o - 16#164#);
          when 16#168# => q <= sv_q(22 * 16 + 7 downto 22 * 16);
          when 16#169# => q <= sv_q(22 * 16 + 15 downto 22 * 16 + 8);
          when 16#16A# => q <= sv_q(23 * 16 + 7 downto 23 * 16);
          when 16#16B# => q <= sv_q(23 * 16 + 15 downto 23 * 16 + 8);
          when 16#16C# => q <= sv_q(24 * 16 + 7 downto 24 * 16);
          when 16#16D# => q <= sv_q(24 * 16 + 15 downto 24 * 16 + 8);
          when 16#170# to 16#173# => q <= b4(sv_q(25 * 16 + 31 downto 25 * 16), o - 16#170#);
          when 16#180# => q <= sv_q(15 * 16 + 7 downto 15 * 16);
          when 16#182# => q <= sv_q(16 * 16 + 7 downto 16 * 16);
          when 16#183# => q <= sv_q(16 * 16 + 15 downto 16 * 16 + 8);
          when 16#184# => q <= sv_q(17 * 16 + 7 downto 17 * 16);
          when 16#185# => q <= sv_q(17 * 16 + 15 downto 17 * 16 + 8);
          when 16#186# => q <= hres_r;
          when 16#188# => q <= hctl_r;
          when 16#18A# => q <= hsz1_r(7 downto 0);
          when 16#18B# => q <= hsz1_r(15 downto 8);
          when 16#18C# => q <= hsz2_r(7 downto 0);
          when 16#18D# => q <= hsz2_r(15 downto 8);
          when 16#18E# => q <= sv_q(27 * 16 + 7 downto 27 * 16);
          when 16#18F# => q <= sv_q(27 * 16 + 15 downto 27 * 16 + 8);
          when 16#141# => q <= sv_q(12 * 16 + 7 downto 12 * 16);
          when 16#150# => q <= mdx_r;
          when 16#151# => q <= mdy_r;
          when 16#152# => q <= "0000000" & mbtn_r;
          when others =>
            if o >= 16#100# and o < 16#130# then q <= pal_r(o - 16#100#); end if;
        end case;
      end if;
    end if;
  end process;
end architecture;
