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
  -- >>> generated from cores/apple2/app/gateware.toml (T264 S2): do not edit, `tools/toml_a_gen.py gen` rewrites it
  constant R_IDENT_ID : integer := 16#000#;
  constant R_IDENT_CAPS : integer := 16#004#;
  constant R_IDENT_CAPS2 : integer := 16#005#;
  constant R_IDENT_CONTRACT : integer := 16#006#;
  constant R_IDENT_RSVD7 : integer := 16#007#;
  constant R_IDENT_BUILD : integer := 16#008#;
  constant R_A2_IRQ_IRQ_EN : integer := 16#010#;
  constant R_A2_IRQ_IRQ_CAUSE : integer := 16#014#;
  constant R_A2_DDR_CAL_DDR_CAL0 : integer := 16#020#;
  constant R_A2_DDR_CAL_DDR_CAL1 : integer := 16#024#;
  constant R_A2_MACHINE_CTRL : integer := 16#0A0#;
  constant R_A2_MACHINE_CFG : integer := 16#0A1#;
  constant R_A2_MACHINE_CAPS : integer := 16#0A2#;
  constant R_A2_MACHINE_STATUS : integer := 16#0A4#;
  constant R_A2_MACHINE_FRAMES : integer := 16#0A8#;
  constant R_A2_MACHINE_CPUADDR : integer := 16#0AC#;
  constant R_A2_MACHINE_AUX_MISS : integer := 16#0B0#;
  constant R_A2_MACHINE_AUX_STALL : integer := 16#0B4#;
  constant R_A2_VIDEO_FB_LATEST : integer := 16#0C0#;
  constant R_A2_VIDEO_FRAME_SEQ : integer := 16#0C1#;
  constant R_A2_VIDEO_FB_OVER : integer := 16#0C2#;
  constant R_A2_VIDEO_FB_BASE : integer := 16#0C4#;
  constant R_A2_VIDEO_FB_STRIDE : integer := 16#0C8#;
  constant R_A2_MEM_AUX_BASE : integer := 16#0D0#;
  constant R_A2_MEM_WIN_SEL : integer := 16#0D4#;
  constant R_A2_FLOPPY_STATUS : integer := 16#0E0#;
  constant R_A2_FLOPPY_REQ_TRACK : integer := 16#0E1#;
  constant R_A2_FLOPPY_HEAD : integer := 16#0E2#;
  constant R_A2_FLOPPY_ACK : integer := 16#0E3#;
  constant R_A2_FLOPPY_BITS : integer := 16#0E4#;
  constant R_A2_FLOPPY_FLAGS : integer := 16#0E6#;
  constant R_A2_FLOPPY_DISK_CTL : integer := 16#0F0#;
  constant R_A2_FLOPPY_DISK_CMD : integer := 16#0F1#;
  constant R_A2_FLOPPY_DISK_CFG : integer := 16#0F2#;
  constant R_A2_FLOPPY_DISK_CLR : integer := 16#0F3#;
  constant R_A2_PALETTE_COLOR : integer := 16#100#;
  constant R_A2_KEYS_KEY_INJ : integer := 16#140#;
  constant R_A2_KEYS_KEY_QUEUED : integer := 16#141#;
  constant R_A2_MOUSE_DX : integer := 16#150#;
  constant R_A2_MOUSE_DY : integer := 16#151#;
  constant R_A2_MOUSE_BTN : integer := 16#152#;
  constant R_A2_MOUSE_GO : integer := 16#153#;
  constant R_A2_STATS_LAT_MAX : integer := 16#160#;
  constant R_A2_STATS_REQS : integer := 16#168#;
  constant R_A2_STATS_STARVED : integer := 16#16C#;
  constant R_A2_STATS_NOISE_CELLS : integer := 16#170#;
  constant R_A2_HDD_STATUS : integer := 16#180#;
  constant R_A2_HDD_BLOCK : integer := 16#182#;
  constant R_A2_HDD_MEM : integer := 16#184#;
  constant R_A2_HDD_RESULT : integer := 16#186#;
  constant R_A2_HDD_ACK : integer := 16#187#;
  constant R_A2_HDD_CTL : integer := 16#188#;
  constant R_A2_HDD_SIZE : integer := 16#18A#;
  constant R_A2_HDD_CMDS : integer := 16#18E#;
  -- <<< generated
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
    variable off_i    : integer;
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
        off_i := to_integer(unsigned(off));
        case off_i is
          when R_A2_IRQ_IRQ_EN => irq_en <= wdata;
          when R_A2_IRQ_IRQ_CAUSE => cause <= (cause and not wdata) or ev;
          when R_A2_MACHINE_CTRL => ctrl_r <= "000000" & wdata(1 downto 0);
                          if wdata(2) = '1' then cold_r <= not cold_r; end if;
          when R_A2_MACHINE_CFG => cfg_r <= wdata;
          when R_A2_MACHINE_CAPS => caps_r <= wdata(0);
          when R_A2_VIDEO_FB_BASE => fbase_r(7 downto 0) <= wdata;
          when R_A2_VIDEO_FB_BASE + 1 => fbase_r(15 downto 8) <= wdata;
          when R_A2_VIDEO_FB_BASE + 2 => fbase_r(23 downto 16) <= wdata;
          when R_A2_VIDEO_FB_BASE + 3 => fbase_r(31 downto 24) <= wdata;
          when R_A2_VIDEO_FB_STRIDE => fstr_r(7 downto 0) <= wdata;
          when R_A2_VIDEO_FB_STRIDE + 1 => fstr_r(15 downto 8) <= wdata;
          when R_A2_VIDEO_FB_STRIDE + 2 => fstr_r(23 downto 16) <= wdata;
          when R_A2_VIDEO_FB_STRIDE + 3 => fstr_r(31 downto 24) <= wdata;
          when R_A2_MEM_AUX_BASE => abase_r(7 downto 0) <= wdata;
          when R_A2_MEM_AUX_BASE + 1 => abase_r(15 downto 8) <= wdata;
          when R_A2_MEM_AUX_BASE + 2 => abase_r(23 downto 16) <= wdata;
          when R_A2_MEM_AUX_BASE + 3 => abase_r(31 downto 24) <= wdata;
          when R_A2_MEM_WIN_SEL => sel_r <= wdata(3 downto 0);
          when R_A2_FLOPPY_ACK => tgl_r(4) <= not tgl_r(4);
          when R_A2_FLOPPY_BITS => bits1_r(7 downto 0) <= wdata;
          when R_A2_FLOPPY_BITS + 1 => bits1_r(15 downto 8) <= wdata;
          when R_A2_FLOPPY_FLAGS => fl_r(0) <= wdata(0);
          when R_A2_FLOPPY_ACK + 8 => tgl_r(5) <= not tgl_r(5);
          when R_A2_FLOPPY_BITS + 8 => bits2_r(7 downto 0) <= wdata;
          when R_A2_FLOPPY_BITS + 9 => bits2_r(15 downto 8) <= wdata;
          when R_A2_FLOPPY_FLAGS + 8 => fl_r(1) <= wdata(0);
          when R_A2_FLOPPY_DISK_CFG => dcfg_r <= wdata;
          when R_A2_FLOPPY_DISK_CLR => tgl_r(9) <= not tgl_r(9);
          when R_A2_HDD_RESULT => hres_r <= wdata;
          when R_A2_HDD_ACK => tgl_r(8) <= not tgl_r(8);
          when R_A2_HDD_CTL => hctl_r <= wdata;
          when R_A2_HDD_SIZE => hsz1_r(7 downto 0) <= wdata;
          when R_A2_HDD_SIZE + 1 => hsz1_r(15 downto 8) <= wdata;
          when R_A2_HDD_SIZE + 2 => hsz2_r(7 downto 0) <= wdata;
          when R_A2_HDD_SIZE + 3 => hsz2_r(15 downto 8) <= wdata;
          when R_A2_FLOPPY_DISK_CTL => dctl_r <= wdata;
          when R_A2_FLOPPY_DISK_CMD =>
            for i in 0 to 3 loop
              if wdata(i) = '1' then tgl_r(i) <= not tgl_r(i); end if;
            end loop;
          when R_A2_KEYS_KEY_INJ => inj_r <= wdata(6 downto 0); tgl_r(6) <= not tgl_r(6);
          when R_A2_MOUSE_DX => mdx_r <= wdata;
          when R_A2_MOUSE_DY => mdy_r <= wdata;
          when R_A2_MOUSE_BTN => mbtn_r <= wdata(0);
          when R_A2_MOUSE_GO => tgl_r(7) <= not tgl_r(7);
          when others =>
            if off_i >= R_A2_PALETTE_COLOR and off_i < R_A2_PALETTE_COLOR + 48 then pal_r(off_i - R_A2_PALETTE_COLOR) <= wdata; end if;
        end case;
      end if;

      q <= x"00";
      if win = '0' and page = x"0" then
        off_i := to_integer(unsigned(off));
        case off_i is
          when R_IDENT_ID => q <= x"41";   -- 'A'
          when R_IDENT_ID + 1 => q <= x"50";   -- 'P'
          when R_IDENT_ID + 2 => q <= x"4C";   -- 'L'
          when R_IDENT_ID + 3 => q <= x"32";   -- '2'
          when R_IDENT_CAPS => q <= x"C0";
          when R_IDENT_CAPS2 => q <= x"06";
          when R_IDENT_CONTRACT => q <= std_logic_vector(to_unsigned(CONTRACT, 8));
          when R_IDENT_RSVD7 => q <= x"0F";
          when R_IDENT_BUILD => q <= build(7 downto 0);
          when R_IDENT_BUILD + 1 => q <= build(15 downto 8);
          when R_IDENT_BUILD + 2 => q <= build(23 downto 16);
          when R_IDENT_BUILD + 3 => q <= build(31 downto 24);
          when R_A2_IRQ_IRQ_EN => q <= irq_en;
          when R_A2_IRQ_IRQ_CAUSE => q <= cause;
          when R_A2_DDR_CAL_DDR_CAL0 to R_A2_DDR_CAL_DDR_CAL1 + 3 => q <= b4(ddr_cal, off_i - R_A2_DDR_CAL_DDR_CAL0);
          when R_A2_MACHINE_CTRL => q <= ctrl_r;
          when R_A2_MACHINE_CFG => q <= cfg_r;
          when R_A2_MACHINE_CAPS => q <= "0000000" & caps_r;
          when R_A2_MACHINE_STATUS => q <= sv_q(8 * 16 + 7 downto 8 * 16);
          when R_A2_MACHINE_FRAMES => q <= sv_q(7 downto 0);
          when R_A2_MACHINE_FRAMES + 1 => q <= sv_q(15 downto 8);
          when R_A2_MACHINE_CPUADDR => q <= sv_q(16 + 7 downto 16);
          when R_A2_MACHINE_CPUADDR + 1 => q <= sv_q(16 + 15 downto 16 + 8);
          when R_A2_MACHINE_AUX_MISS to R_A2_MACHINE_AUX_MISS + 3 => q <= b4(sv_q(63 downto 32), off_i - R_A2_MACHINE_AUX_MISS);
          when R_A2_MACHINE_AUX_STALL to R_A2_MACHINE_AUX_STALL + 3 => q <= b4(sv_q(95 downto 64), off_i - R_A2_MACHINE_AUX_STALL);
          when R_A2_VIDEO_FB_LATEST => q <= sv_q(7 * 16 + 15 downto 7 * 16 + 8);
          when R_A2_VIDEO_FRAME_SEQ => q <= sv_q(7 * 16 + 7 downto 7 * 16);
          when R_A2_VIDEO_FB_OVER => q <= sv_q(6 * 16 + 7 downto 6 * 16);
          when R_A2_VIDEO_FB_OVER + 1 => q <= sv_q(6 * 16 + 15 downto 6 * 16 + 8);
          when R_A2_VIDEO_FB_BASE to R_A2_VIDEO_FB_BASE + 3 => q <= b4(fbase_r, off_i - R_A2_VIDEO_FB_BASE);
          when R_A2_VIDEO_FB_STRIDE to R_A2_VIDEO_FB_STRIDE + 3 => q <= b4(fstr_r, off_i - R_A2_VIDEO_FB_STRIDE);
          when R_A2_MEM_AUX_BASE to R_A2_MEM_AUX_BASE + 3 => q <= b4(abase_r, off_i - R_A2_MEM_AUX_BASE);
          when R_A2_MEM_WIN_SEL => q <= "0000" & sel_r;
          when R_A2_FLOPPY_STATUS => q <= sv_q(9 * 16 + 7 downto 9 * 16);
          when R_A2_FLOPPY_REQ_TRACK => q <= sv_q(13 * 16 + 7 downto 13 * 16);
          when R_A2_FLOPPY_HEAD => q <= sv_q(14 * 16 + 7 downto 14 * 16);
          when R_A2_FLOPPY_BITS => q <= bits1_r(7 downto 0);
          when R_A2_FLOPPY_BITS + 1 => q <= bits1_r(15 downto 8);
          when R_A2_FLOPPY_FLAGS => q <= "0000000" & fl_r(0);
          when R_A2_FLOPPY_STATUS + 8 => q <= sv_q(10 * 16 + 7 downto 10 * 16);
          when R_A2_FLOPPY_REQ_TRACK + 8 => q <= sv_q(13 * 16 + 15 downto 13 * 16 + 8);
          when R_A2_FLOPPY_HEAD + 8 => q <= sv_q(14 * 16 + 15 downto 14 * 16 + 8);
          when R_A2_FLOPPY_BITS + 8 => q <= bits2_r(7 downto 0);
          when R_A2_FLOPPY_BITS + 9 => q <= bits2_r(15 downto 8);
          when R_A2_FLOPPY_FLAGS + 8 => q <= "0000000" & fl_r(1);
          when R_A2_FLOPPY_DISK_CTL => q <= dctl_r;
          when R_A2_FLOPPY_DISK_CFG => q <= dcfg_r;
          when R_A2_STATS_LAT_MAX to R_A2_STATS_LAT_MAX + 3 => q <= b4(sv_q(18 * 16 + 31 downto 18 * 16), off_i - R_A2_STATS_LAT_MAX);
          when R_A2_STATS_LAT_MAX + 4 to R_A2_STATS_LAT_MAX + 7 => q <= b4(sv_q(20 * 16 + 31 downto 20 * 16), off_i - (R_A2_STATS_LAT_MAX + 4));
          when R_A2_STATS_REQS => q <= sv_q(22 * 16 + 7 downto 22 * 16);
          when R_A2_STATS_REQS + 1 => q <= sv_q(22 * 16 + 15 downto 22 * 16 + 8);
          when R_A2_STATS_REQS + 2 => q <= sv_q(23 * 16 + 7 downto 23 * 16);
          when R_A2_STATS_REQS + 3 => q <= sv_q(23 * 16 + 15 downto 23 * 16 + 8);
          when R_A2_STATS_STARVED => q <= sv_q(24 * 16 + 7 downto 24 * 16);
          when R_A2_STATS_STARVED + 1 => q <= sv_q(24 * 16 + 15 downto 24 * 16 + 8);
          when R_A2_STATS_NOISE_CELLS to R_A2_STATS_NOISE_CELLS + 3 => q <= b4(sv_q(25 * 16 + 31 downto 25 * 16), off_i - R_A2_STATS_NOISE_CELLS);
          when R_A2_HDD_STATUS => q <= sv_q(15 * 16 + 7 downto 15 * 16);
          when R_A2_HDD_BLOCK => q <= sv_q(16 * 16 + 7 downto 16 * 16);
          when R_A2_HDD_BLOCK + 1 => q <= sv_q(16 * 16 + 15 downto 16 * 16 + 8);
          when R_A2_HDD_MEM => q <= sv_q(17 * 16 + 7 downto 17 * 16);
          when R_A2_HDD_MEM + 1 => q <= sv_q(17 * 16 + 15 downto 17 * 16 + 8);
          when R_A2_HDD_RESULT => q <= hres_r;
          when R_A2_HDD_CTL => q <= hctl_r;
          when R_A2_HDD_SIZE => q <= hsz1_r(7 downto 0);
          when R_A2_HDD_SIZE + 1 => q <= hsz1_r(15 downto 8);
          when R_A2_HDD_SIZE + 2 => q <= hsz2_r(7 downto 0);
          when R_A2_HDD_SIZE + 3 => q <= hsz2_r(15 downto 8);
          when R_A2_HDD_CMDS => q <= sv_q(27 * 16 + 7 downto 27 * 16);
          when R_A2_HDD_CMDS + 1 => q <= sv_q(27 * 16 + 15 downto 27 * 16 + 8);
          when R_A2_KEYS_KEY_QUEUED => q <= sv_q(12 * 16 + 7 downto 12 * 16);
          when R_A2_MOUSE_DX => q <= mdx_r;
          when R_A2_MOUSE_DY => q <= mdy_r;
          when R_A2_MOUSE_BTN => q <= "0000000" & mbtn_r;
          when others =>
            if off_i >= R_A2_PALETTE_COLOR and off_i < R_A2_PALETTE_COLOR + 48 then q <= pal_r(off_i - R_A2_PALETTE_COLOR); end if;
        end case;
      end if;
    end if;
  end process;
end architecture;
