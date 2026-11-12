-- SPDX-License-Identifier: GPL-3.0-or-later
-- T88 S3/S4/S6/S7/S8/S10: the whole Atari core (a8_core: Watson's machine behind the host's layer-2 socket) in GHDL, loaded the way the
-- app loads it - through the app's window and the register block - with a test OS of our own (testos2.asm) and test images of our
-- own: no Atari ROM, no Atari software. It checks
--   * the boot: the OS runs, its vertical blank counter counts, the memory checks it wrote are right (base RAM, BASIC at $A000,
--     the extended banks through PORTB - in the DDR2 behind the cache -, the cartridge image and its bank register);
--   * the clock (S3): a PAL frame is 632,320 sys_clk, the line is 114 machine cycles, the frame 312 lines;
--   * the picture (S6): the frame the pixel writer put into the DDR2 model equals the frame the bench re-captured itself from the
--     machine's pixel outputs (hblank, vblank, pixel clock enable, RGB), and it is not a flat colour;
--   * the sound (S7): the POKEY tone the OS sets, counted from the 24 bit samples, is the programmed frequency;
--   * the keys, the console keys and the joystick (S8): the host's matrix and joystick bits arrive in the OS's variables.
-- Generics: OS_HEX, BASIC_HEX, CART_HEX (hex files, one byte per line), CART_MODE (cart_logic's mode), RAMSEL (RAM_SELECT),
-- FRAMES (frames to run), DDR_LAT (DDR2 read latency in ui_clk), DDR_BUSY (percent of clocks the DDR2 port is not ready).
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.textio.all;
use work.tb_a8_pkg.all;

entity tb_a8_machine is
  generic (
    OS_HEX    : string  := "build/testos2.hex";
    BASIC_HEX : string  := "build/basic.hex";
    CART_HEX  : string  := "";
    CART_MODE : integer := 0;
    RAMSEL    : integer := 1;
    FRAMES    : integer := 8;
    -- a byte written over one byte of a loaded image after the load, before the machine starts: a
    -- control for the benches that compare runs (the Freeze bench's negative control). Target as in
    -- load_rom: CTRL_SEL 0 = OS, 1 = BASIC, CTRL_ADDR the window address it writes.
    -- 1 = the machine runs testos2 and every check about its RAM contents applies; 0 = it runs a
    -- real Atari OS (cores/atari800/roms), so those checks are meaningless and the picture, the
    -- frame timing and the DDR2 window are what is checked (run.sh `os`).
    TESTOS    : integer := 1;
    CTRL_BYTE : integer := -1;
    CTRL_SEL  : integer := 1;
    CTRL_ADDR : integer := 16#4000#;
    DDR_LAT   : integer := 40;
    DDR_BUSY  : integer := 10;
    PPM       : string  := "");
end entity;

architecture sim of tb_a8_machine is
  constant TSYS : time := 31.718 ns;                -- 31.527778 MHz
  constant X0 : integer := 2;
  constant Y0 : integer := 18;
  constant COLS : integer := 384;
  constant LINES : integer := 288;
  signal sys_clk, cpu_clk, ui_clk, mclk : std_logic := '0';
  signal sys_rst, ui_rst : std_logic := '1';
  signal reset_req, halt_req : std_logic := '0';
  signal keys : std_logic_vector(63 downto 0) := (others => '0');
  signal joy1, joy2 : std_logic_vector(4 downto 0) := (others => '0');
  signal restore : std_logic := '0';
  signal dma_req_tgl, dma_we, dma_rom : std_logic := '0';
  signal dma_addr : std_logic_vector(15 downto 0) := (others => '0');
  signal dma_wdata : std_logic_vector(7 downto 0) := (others => '0');
  signal dma_done_tgl : std_logic;
  signal dma_rdata : std_logic_vector(7 downto 0);
  signal win_port_use, win_port_rom, win_port_we : std_logic;
  signal win_port_addr : std_logic_vector(15 downto 0);
  signal win_port_wdata : std_logic_vector(7 downto 0);
  signal ram_rdata : std_logic_vector(7 downto 0);
  signal pix_color : std_logic_vector(3 downto 0);
  signal pix_x : std_logic_vector(9 downto 0);
  signal pix_y : std_logic_vector(8 downto 0);
  signal audio_l, audio_r : std_logic_vector(23 downto 0);
  signal cw_we, cw_re : std_logic := '0';
  signal cw_addr : std_logic_vector(16 downto 0) := (others => '0');
  signal cw_wdata : std_logic_vector(7 downto 0) := (others => '0');
  signal cw_rdata : std_logic_vector(7 downto 0);
  signal cw_ack, cw_irq, px_we, px_re : std_logic;
  signal halted, running : std_logic;
  signal c_addr, e_addr : std_logic_vector(26 downto 0);
  signal c_cmd, e_cmd : std_logic_vector(2 downto 0);
  signal c_en, c_rdy, c_wdf_wren, c_rd_valid, e_en, e_rdy, e_wdf_wren : std_logic := '0';
  signal c_wdf_data, c_rd_data, e_wdf_data : std_logic_vector(127 downto 0) := (others => '0');
  signal c_wdf_mask, e_wdf_mask : std_logic_vector(15 downto 0);
  signal tap_ce, tap_cpu, tap_antic, tap_e179, tap_hblank, tap_vblank, tap_pixce, tap_reset_n : std_logic;
  signal tap_addr : std_logic_vector(15 downto 0);
  signal tap_rgb : std_logic_vector(23 downto 0);
  signal j_rdata : std_logic_vector(31 downto 0);
  signal j_rvalid : std_logic;
  signal done : boolean := false;
  signal fail : integer := 0;

  -- the host's side of the window: the winner of the port comes back as p_* in the same clock
  signal p_req, p_rom, p_we : std_logic;
  signal p_addr : std_logic_vector(15 downto 0);
  signal p_wdata : std_logic_vector(7 downto 0);

  -- the bench's own capture of the machine's pixels
  type frame_t is array (0 to LINES - 1, 0 to COLS - 1) of std_logic_vector(15 downto 0);
  type frames_t is array (0 to 3) of frame_t;
  shared variable ref : frames_t;
  signal ref_frames : integer := 0;
  signal ref_lines_in_frame : integer := 0;
  -- measurements
  signal vb_period : integer := 0;
  signal hb_period : integer := 0;
  signal lines_per_frame : integer := 0;
  signal px_width : integer := 0;
  signal zc_count : integer := 0;
  signal zc_t0, zc_t1 : time := 0 ns;
  signal aud_on : boolean := false;
begin
  sys_clk <= not sys_clk after TSYS / 2 when not done;
  cpu_clk <= not cpu_clk after 5 ns when not done;
  ui_clk  <= not ui_clk  after 5.1 ns when not done;
  mclk    <= not mclk    after 40.7 ns when not done;

  p_req <= win_port_use; p_rom <= win_port_rom; p_we <= win_port_we; p_addr <= win_port_addr; p_wdata <= win_port_wdata;

  dut : entity work.a8_core
    port map (
      sys_clk => sys_clk, sys_rst => sys_rst, cpu_clk => cpu_clk, mclk => mclk, ui_clk => ui_clk, ui_rst => ui_rst,
      reset_req => reset_req, halt_req => halt_req, halted => halted, running => running,
      keys => keys, joy1 => joy1, joy2 => joy2, restore => restore,
      dma_req_tgl => dma_req_tgl, dma_addr => dma_addr, dma_we => dma_we, dma_wdata => dma_wdata, dma_rom => dma_rom,
      dma_done_tgl => dma_done_tgl, dma_rdata => dma_rdata, dbg_busy => '0',
      win_port_use => win_port_use, win_port_addr => win_port_addr, win_port_rom => win_port_rom,
      win_port_we => win_port_we, win_port_wdata => win_port_wdata,
      port_req => p_req, port_addr => p_addr, port_rom => p_rom, port_we => p_we, port_wdata => p_wdata,
      ram_rdata => ram_rdata, pix_color => pix_color, pix_x => pix_x, pix_y => pix_y,
      audio_l => audio_l, audio_r => audio_r,
      cw_we => cw_we, cw_re => cw_re, cw_addr => cw_addr, cw_wdata => cw_wdata, cw_rdata => cw_rdata, cw_ack => cw_ack,
      cw_irq => cw_irq, px_we => px_we, px_re => px_re, px_rdata => x"00", px_ev_vsync => '0', px_ev_flip => '0',
      build => x"A8A8A8A8", ddr_cal => (others => '0'), dbg_re => '0', dbg_addr => (others => '0'),
      j_rdata => j_rdata, j_rvalid => j_rvalid,
      c_addr => c_addr, c_cmd => c_cmd, c_en => c_en, c_rdy => c_rdy, c_wdf_data => c_wdf_data, c_wdf_mask => c_wdf_mask,
      c_wdf_wren => c_wdf_wren, c_rd_data => c_rd_data, c_rd_valid => c_rd_valid,
      e_addr => e_addr, e_cmd => e_cmd, e_en => e_en, e_rdy => e_rdy, e_wdf_data => e_wdf_data, e_wdf_mask => e_wdf_mask,
      e_wdf_wren => e_wdf_wren,
      tap_ce => tap_ce, tap_cpu => tap_cpu, tap_antic => tap_antic, tap_addr => tap_addr, tap_e179 => tap_e179,
      tap_hblank => tap_hblank, tap_vblank => tap_vblank, tap_pixce => tap_pixce, tap_rgb => tap_rgb,
      tap_reset_n => tap_reset_n, tap_snoop => open, tap_wdata => open, tap_we => open,
      tap_audio => open, tap_halted => open);

  -- ---------------------------------------------------------------- the host's DDR2: port c_* (reads and writes), port e_* (writes)
  c_model : process (ui_clk)
    variable seed : integer := 12345;
    variable rd_cnt : integer := -1;
    variable rd_line : std_logic_vector(127 downto 0);
    variable rnd : integer;
    variable byte_addr : integer;
  begin
    if rising_edge(ui_clk) then
      c_rd_valid <= '0';
      if c_en = '1' and c_rdy = '1' then
        byte_addr := to_integer(unsigned(c_addr)) * 2 - 16#1000000#;
        assert c_cmd = "000" or c_cmd = "001" report "c_cmd" severity failure;
        if c_cmd = "000" then
          assert c_wdf_wren = '1' report "write command without data" severity failure;
          ddr.wr128(byte_addr, c_wdf_data, c_wdf_mask);
        else
          rd_line := ddr.rd128(byte_addr);
          rd_cnt := DDR_LAT;
        end if;
      end if;
      if rd_cnt = 0 then c_rd_data <= rd_line; c_rd_valid <= '1'; rd_cnt := -1;
      elsif rd_cnt > 0 then rd_cnt := rd_cnt - 1; end if;
      seed := (seed * 1103515245 + 12345) mod 2147483648;
      rnd := (seed / 65536) mod 100;
      if rnd < DDR_BUSY then c_rdy <= '0'; else c_rdy <= '1'; end if;
    end if;
  end process;

  e_model : process (ui_clk)
    variable seed : integer := 777;
    variable rnd : integer;
    variable byte_addr : integer;
  begin
    if rising_edge(ui_clk) then
      if e_en = '1' and e_rdy = '1' then
        byte_addr := to_integer(unsigned(e_addr)) * 2 - 16#1000000#;
        assert e_cmd = "000" and e_wdf_wren = '1' report "e_ port: only writes with data" severity failure;
        ddr.wr128(byte_addr, e_wdf_data, e_wdf_mask);
      end if;
      seed := (seed * 1103515245 + 12345) mod 2147483648;
      rnd := (seed / 65536) mod 100;
      if rnd < DDR_BUSY then e_rdy <= '0'; else e_rdy <= '1'; end if;
    end if;
  end process;

  -- ---------------------------------------------------------------- the bench's own capture of the picture and the clock
  monitor : process (sys_clk)
    variable hb_d, vb_d : std_logic := '1';
    variable in_frame : boolean := false;
    variable line_no : integer := -1;
    variable x : integer := 0;
    variable cap : boolean := false;
    variable cnt_vb, cnt_hb, nlines, all_lines : integer := 0;
    variable fno : integer := 0;
    variable rgb565 : std_logic_vector(15 downto 0);
    variable cur : integer := 0;
    variable pos : integer;
    variable px, last_px : std_logic_vector(15 downto 0) := (others => '0');
    variable vis : integer := 0;
  begin
    if rising_edge(sys_clk) then
      cnt_vb := cnt_vb + 1; cnt_hb := cnt_hb + 1;
      if tap_ce = '1' and tap_pixce = '1' and tap_reset_n = '1' then
        rgb565 := tap_rgb(23 downto 19) & tap_rgb(15 downto 10) & tap_rgb(7 downto 3);
        if hb_d = '1' and tap_hblank = '0' then          -- a line starts
          if in_frame then px_width <= vis; end if;
          vis := 0;
          x := 0;
          all_lines := all_lines + 1;
          if in_frame and tap_vblank = '0' then
            line_no := line_no + 1;
            nlines := nlines + 1;
          end if;
          hb_period <= cnt_hb; cnt_hb := 0;
        end if;
        if vb_d = '1' and tap_vblank = '0' then            -- a frame starts
          in_frame := true; line_no := -1; nlines := 0;
          lines_per_frame <= all_lines; all_lines := 0;
          cur := fno mod 4; fno := fno + 1;
          vb_period <= cnt_vb; cnt_vb := 0;
        end if;
        if vb_d = '0' and tap_vblank = '1' then            -- a frame ends
          ref_frames <= fno;
          in_frame := false;
        end if;
        if in_frame and tap_vblank = '0' then
          -- the window counts every pixel from the end of hblank; what hblank hides repeats the last visible pixel (a8_pixw)
          if tap_hblank = '1' then px := last_px; else px := rgb565; last_px := rgb565; vis := vis + 1; end if;
          if line_no - Y0 >= 0 and line_no - Y0 < LINES and x - X0 >= 0 and x - X0 < COLS then
            ref(cur)(line_no - Y0, x - X0) := px;
          end if;
          if x < 1023 then x := x + 1; end if;
        end if;
        hb_d := tap_hblank; vb_d := tap_vblank;
      end if;
    end if;
  end process;

  -- the sound: zero crossings of the 24 bit samples once the tone is on
  audio_mon : process (mclk)
    variable prev : signed(23 downto 0) := (others => '0');
    variable cur : signed(23 downto 0);
    variable was_on : boolean := false;
    variable n : integer := 0;
  begin
    if rising_edge(mclk) then
      cur := signed(audio_l);
      if aud_on and not was_on then n := 0; zc_count <= 0; end if;
      was_on := aud_on;
      if aud_on then
        if prev < 0 and cur >= 0 then
          if n = 0 then zc_t0 <= now; end if;
          n := n + 1;
          zc_count <= n;
          zc_t1 <= now;
        end if;
      end if;
      prev := cur;
    end if;
  end process;

  -- ---------------------------------------------------------------- the app
  stim : process
    variable n : natural;
    variable l : line;
    variable v8 : std_logic_vector(7 downto 0);

    procedure ck(cond : boolean; msg : string) is
    begin
      if cond then report "ok   " & msg; else report "FAIL " & msg severity error; fail <= fail + 1; end if;
    end procedure;

    procedure wsys(k : integer) is begin for i in 1 to k loop wait until rising_edge(sys_clk); end loop; end procedure;

    procedure cw_write(off : integer; d : std_logic_vector(7 downto 0)) is
    begin
      wait until rising_edge(cpu_clk);
      cw_addr <= std_logic_vector(to_unsigned(off, 17)); cw_wdata <= d; cw_we <= '1';
      wait until rising_edge(cpu_clk);
      cw_we <= '0';
    end procedure;

    procedure cw_read(off : integer; d : out std_logic_vector(7 downto 0)) is
    begin
      wait until rising_edge(cpu_clk);
      cw_addr <= std_logic_vector(to_unsigned(off, 17)); cw_re <= '1';
      wait until rising_edge(cpu_clk);
      cw_re <= '0';
      wait for 1 ns;
      d := cw_rdata;
    end procedure;

    procedure cw_rd(off : integer; v : out integer) is
      variable d : std_logic_vector(7 downto 0);
    begin
      cw_read(off, d);
      v := to_integer(unsigned(d));
    end procedure;

    procedure cw_rd16(off : integer; v : out integer) is
      variable a, b : integer;
    begin
      cw_rd(off, a); cw_rd(off + 1, b);
      v := a + 256 * b;
    end procedure;

    procedure win(rom : std_logic; a : integer; we : std_logic; d : std_logic_vector(7 downto 0); q : out std_logic_vector(7 downto 0)) is
      variable t : std_logic;
    begin
      wait until rising_edge(sys_clk);
      dma_addr <= std_logic_vector(to_unsigned(a, 16)); dma_we <= we; dma_wdata <= d; dma_rom <= rom;
      t := dma_done_tgl;
      dma_req_tgl <= not dma_req_tgl;
      wait until rising_edge(sys_clk) and dma_done_tgl /= t;
      q := dma_rdata;
    end procedure;

    procedure win_wr(rom : std_logic; a : integer; d : std_logic_vector(7 downto 0)) is
      variable q : std_logic_vector(7 downto 0);
    begin
      win(rom, a, '1', d, q);
    end procedure;

    procedure ram_rd(a : integer; v : out integer) is
      variable q : std_logic_vector(7 downto 0);
    begin
      win('0', a, '0', x"00", q);
      v := to_integer(unsigned(q));
    end procedure;

    -- check a RAM byte of the machine through the window against a value
    procedure ram_is(a : integer; e : integer; msg : string) is
      variable v : integer;
    begin
      ram_rd(a, v);
      ck(v = e, msg & ": " & integer'image(v) & " (expected " & integer'image(e) & ")");
    end procedure;

    -- a ROM file through the window into the aperture of array `sel`
    procedure load_rom(file_name : string; sel : integer; size : integer) is
      file f : text;
      variable l : line;
      variable k : integer := 0;
      variable st : file_open_status;
      variable a : integer;
    begin
      cw_write(16#0A6#, std_logic_vector(to_unsigned(sel, 8)));
      file_open(st, f, file_name, read_mode);
      assert st = open_ok report "cannot open " & file_name severity failure;
      while not endfile(f) and k < size loop
        readline(f, l);
        if l'length >= 2 then
          if k < 16#2000# then a := 16#4000# + k; else a := k; end if;
          win_wr('1', a, hex_byte(l.all(1 to 2)));
          k := k + 1;
        end if;
      end loop;
      file_close(f);
      report "loaded " & file_name & " " & integer'image(k) & " bytes into array " & integer'image(sel);
    end procedure;

    procedure wait_frames(target : integer) is
      variable f : integer;
    begin
      loop
        wait for 2 ms;
        cw_rd16(16#0AC#, f);
        exit when f >= target;
      end loop;
    end procedure;

    impure function hexb(file_name : string; idx : integer) return integer is
      file f : text;
      variable l : line;
      variable k : integer := 0;
      variable st : file_open_status;
      variable r : integer := -1;
    begin
      file_open(st, f, file_name, read_mode);
      while not endfile(f) loop
        readline(f, l);
        if l'length >= 2 then
          if k = idx then r := to_integer(unsigned(hex_byte(l.all(1 to 2)))); end if;
          k := k + 1;
        end if;
      end loop;
      file_close(f);
      return r;
    end function;

    variable f0, f1, errs, i, j, k, lo, hi, diff : integer;
    variable d : std_logic_vector(7 downto 0);
    variable exp_a0, exp_a1, exp_bf, exp_80 : integer;
    variable fb : integer;
    variable px : std_logic_vector(15 downto 0);
    variable bytes_lo, bytes_hi : std_logic_vector(7 downto 0);
    variable nd : integer;
    variable first : std_logic_vector(15 downto 0);
    variable freq : real;
    file fo : text;
    variable status : file_open_status;
    variable ll : line;
    variable rv, rv2, rv3 : integer;
  begin
    sys_rst <= '1'; ui_rst <= '1';
    wait for 200 ns;
    sys_rst <= '0'; ui_rst <= '0';
    wsys(10);
    -- the app holds the machine in reset while it loads the ROMs, sets the configuration and the cartridge
    cw_write(16#0A0#, x"01");
    cw_write(16#0A1#, std_logic_vector(to_unsigned(RAMSEL, 8)));
    cw_write(16#0A3#, std_logic_vector(to_unsigned(CART_MODE, 8)));
    d := x"00"; cw_read(16#000#, d);
    ck(d = x"41", "identity byte 0 is 'A'");
    cw_read(16#003#, d); ck(d = x"4C", "identity byte 3 is 'L'");
    load_rom(OS_HEX, 0, 16384);
    load_rom(BASIC_HEX, 1, 8192);
    if CTRL_BYTE >= 0 then
      cw_write(16#0A6#, std_logic_vector(to_unsigned(CTRL_SEL, 8)));
      win_wr('1', CTRL_ADDR, std_logic_vector(to_unsigned(CTRL_BYTE, 8)));
      win('1', CTRL_ADDR, '0', x"00", d);
      report "control: image " & integer'image(CTRL_SEL) & " byte " & integer'image(CTRL_ADDR) &
             " is now " & integer'image(to_integer(unsigned(d))) &
             " (wrote " & integer'image(CTRL_BYTE) & ")";
    end if;
    if CART_HEX /= "" then
      load_hex(CART_HEX, 16#400000#, 0, n);
      report "cartridge image: " & integer'image(n) & " bytes in the DDR2 window";
    end if;
    -- a few bytes of the loaded ROM read back (the window reads ROM too)
    wait for 3 ms;                                        -- the RAM sweep of the reset (65,536 clocks = 2.1 ms)
    cw_write(16#0A0#, x"00");                             -- release: the machine starts
    -- ============================================================ boot
    wait_frames(FRAMES);
    cw_rd16(16#0AC#, rv);
    report "frames run: " & integer'image(rv);
    ram_rd(16#80#, f0);
    report "the OS's vertical blank counter: " & integer'image(f0);
    if TESTOS = 1 then
    ck(f0 >= FRAMES - 3, "the OS's vertical blank counter counts: " & integer'image(f0));
    ram_is(16#600#, 1, "base RAM check of the OS");
    ram_is(16#0700#, 16#5A#, "base RAM byte through the window");
    if CART_HEX = "" then
      exp_a0 := hexb(BASIC_HEX, 0); exp_a1 := hexb(BASIC_HEX, 1); exp_bf := hexb(BASIC_HEX, 16#1FFF#); exp_80 := 0;
    elsif CART_MODE = 1 then
      exp_a0 := hexb(CART_HEX, 0); exp_a1 := hexb(CART_HEX, 1); exp_bf := hexb(CART_HEX, 16#1FFF#); exp_80 := 0;
    elsif CART_MODE = 33 then
      exp_a0 := hexb(CART_HEX, 16#2000#); exp_a1 := hexb(CART_HEX, 16#2001#); exp_bf := hexb(CART_HEX, 16#3FFF#); exp_80 := hexb(CART_HEX, 0);
    else
      exp_a0 := hexb(CART_HEX, 0); exp_a1 := hexb(CART_HEX, 1); exp_bf := hexb(CART_HEX, 16#1FFF#); exp_80 := 0;
    end if;
    ram_is(16#601#, exp_a0, "byte at $A000");
    ram_is(16#602#, exp_a1, "byte at $A001");
    ram_is(16#603#, exp_bf, "byte at $BFFF");
    ram_is(16#604#, exp_80, "byte at $8000");
    if RAMSEL > 0 then
      ram_is(16#605#, 0, "extended bank errors (low), 4 banks of 256 bytes written and read back");
      ram_is(16#606#, 0, "extended bank errors (high)");
      ram_is(16#607#, 16#77#, "the main bank at $4000 is untouched by the bank test");
      ram_is(16#608#, 16#00#, "first byte of bank 0");
      ram_is(16#609#, 16#11#, "first byte of bank 1");
      ram_is(16#60A#, 16#22#, "first byte of bank 2");
      ram_is(16#60B#, 16#33#, "first byte of bank 3");
    end if;
    if CART_MODE = 2 then
      ram_is(16#610#, hexb(CART_HEX, 3 * 16#2000#), "AtariMax bank 3 at $A000");
      ram_is(16#611#, hexb(CART_HEX, 15 * 16#2000#), "AtariMax bank 15 at $A000");
      ram_is(16#612#, hexb(CART_HEX, 16#1FFF#), "AtariMax bank 0, $BFFF");
      ram_is(16#613#, hexb(CART_HEX, 0), "AtariMax bank 0, $A000");
    end if;
    end if;                                               -- TESTOS = 1
    -- the DDR2 cache
    cw_rd16(16#0F0#, rv); cw_rd16(16#0F4#, rv2); cw_rd16(16#0F8#, rv3);
    report "cache: hits " & integer'image(rv) & " misses " & integer'image(rv2) & " writes " & integer'image(rv3);
    cw_rd16(16#0FC#, rv); report "cache: stalled clocks (low 16 bits) " & integer'image(rv);
    -- ============================================================ S3: the clock
    wait for 45 ms;
    ck(vb_period = 632320, "a PAL frame is 632,320 sys_clk: " & integer'image(vb_period));
    ck(lines_per_frame = 312, "the frame has 312 lines: " & integer'image(lines_per_frame));
    report "picture: " & integer'image(px_width) & " pixels a line between hblanks, window " & integer'image(COLS) & " x " & integer'image(LINES);
    ck(px_width > 300 and px_width <= X0 + COLS, "ANTIC's hblank leaves a visible part within the window: " & integer'image(px_width) & " of " & integer'image(COLS));
    ck(hb_period = 2026 or hb_period = 2027, "a line is 114 machine cycles = 2026.67 sys_clk: " & integer'image(hb_period));
    -- ============================================================ S6: the picture
    cw_rd(16#0C1#, f1);                                    -- FRAME_SEQ
    cw_rd(16#0C0#, fb);
    ck(fb >= 128, "a complete frame is announced: latest " & integer'image(fb) & " seq " & integer'image(f1));
    errs := 0; lo := 0;
    first := (others => '0');
    j := 0;
    -- the buffer of the newest frame; the bench's own capture of the same frame
    k := fb mod 4;
    for y in 0 to LINES - 1 loop
      for xx in 0 to COLS - 1 loop
        bytes_lo := ddr.rd8(16#D00000# + k * 16#100000# + y * COLS * 2 + xx * 2);
        bytes_hi := ddr.rd8(16#D00000# + k * 16#100000# + y * COLS * 2 + xx * 2 + 1);
        px := bytes_hi & bytes_lo;
        if y = 0 and xx = 0 then first := px; end if;
        if px /= first then j := j + 1; end if;
        if px /= ref(k)(y, xx) then errs := errs + 1; end if;
      end loop;
    end loop;
    ck(errs = 0, "the frame buffer equals the bench's own capture of the machine's pixels: " & integer'image(errs) & " of " & integer'image(LINES * COLS) & " differ");
    ck(j > 1000, "the picture is not a flat colour: " & integer'image(j) & " pixels differ from the corner");
    if PPM /= "" then
      file_open(status, fo, PPM, write_mode);
      write(ll, string'("P3")); writeline(fo, ll);
      write(ll, integer'image(COLS) & " " & integer'image(LINES)); writeline(fo, ll);
      write(ll, string'("255")); writeline(fo, ll);
      for y in 0 to LINES - 1 loop
        for xx in 0 to COLS - 1 loop
          bytes_lo := ddr.rd8(16#D00000# + k * 16#100000# + y * COLS * 2 + xx * 2);
          bytes_hi := ddr.rd8(16#D00000# + k * 16#100000# + y * COLS * 2 + xx * 2 + 1);
          px := bytes_hi & bytes_lo;
          write(ll, integer'image(to_integer(unsigned(px(15 downto 11))) * 255 / 31) & " " & integer'image(to_integer(unsigned(px(10 downto 5))) * 255 / 63) & " " &
                    integer'image(to_integer(unsigned(px(4 downto 0))) * 255 / 31) & " ");
        end loop;
        writeline(fo, ll);
      end loop;
      file_close(fo);
    end if;
    -- ============================================================ S7: the sound
    -- S7 and S8 read what testos2 programmed and wrote; with a real OS they say nothing.
    if TESTOS = 1 then
    aud_on <= true;
    wait for 60 ms;
    aud_on <= false;
    wait for 1 us;
    if zc_count > 2 then freq := real(zc_count - 1) / (real((zc_t1 - zc_t0) / 1 ps) * 1.0e-12); else freq := 0.0; end if;
    report "tone: " & integer'image(zc_count) & " periods, " & real'image(freq) & " Hz (programmed 989.6 Hz)";
    ck(freq > 0.99 * 989.6 and freq < 1.01 * 989.6, "the POKEY tone is the programmed frequency within 1 %");
    -- ============================================================ S8: keys, console keys, joystick
    keys(10) <= '1';                                       -- A
    wait for 50 ms;
    ram_is(16#82#, 16#3F#, "key A gives KBCODE $3F");
    keys(10) <= '0';                                       -- KBCODE is latched when a key goes down: release A,
    wait for 20 ms;
    keys(15) <= '1';                                       -- LSHIFT first,
    wait for 20 ms;
    keys(10) <= '1';                                       -- then A
    wait for 50 ms;
    ram_is(16#82#, 16#7F#, "Shift + A gives $7F");
    keys(10) <= '0'; keys(15) <= '0';
    wait for 20 ms;
    keys(58) <= '1';                                       -- CTRL
    wait for 20 ms;
    keys(10) <= '1';
    wait for 50 ms;
    ram_is(16#82#, 16#BF#, "Control + A gives $BF");
    keys(58) <= '0'; keys(10) <= '0';
    keys(4) <= '1';                                        -- F1 = START
    wait for 50 ms;
    ram_rd(16#83#, rv); ck((rv mod 8) = 6, "F1 pulls CONSOL START low: " & integer'image(rv));
    keys(4) <= '0'; keys(6) <= '1';                        -- F5 = OPTION
    wait for 50 ms;
    ram_rd(16#83#, rv); ck((rv mod 8) = 3, "F5 pulls CONSOL OPTION low: " & integer'image(rv));
    keys(6) <= '0';
    joy1 <= "10001";                                       -- fire + up on port 1
    wait for 50 ms;
    ram_rd(16#84#, rv); ck((rv mod 16) = 14, "joystick 1 up reads on PORTA low nibble: " & integer'image(rv));
    ram_rd(16#85#, rv); ck((rv mod 2) = 0, "joystick 1 fire reads on STRIG0: " & integer'image(rv));
    joy1 <= "00000";
    joy2 <= "00100";                                       -- left on port 2
    wait for 50 ms;
    ram_rd(16#84#, rv); ck((rv / 16) = 11, "joystick 2 left reads on PORTA high nibble: " & integer'image(rv));
    joy2 <= "00000";
    end if;                                               -- TESTOS = 1
    -- ============================================================ the end
    ck(ddr.errors = 0, "no access outside the modelled DDR2 pieces");
    wait for 1 us;      -- `fail` is a signal: without this the last FAIL is not in it yet and the
                        -- bench prints PASS over it (seen with TESTOS = 0, where no wait follows)
    if fail = 0 then report "PASS tb_a8_machine"; else report "FAIL tb_a8_machine: " & integer'image(fail) & " checks" severity error; end if;
    done <= true;
    wait;
  end process;
end architecture;
