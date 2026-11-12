-- SPDX-License-Identifier: GPL-3.0-or-later
-- T108 D1 / S1, S2, S4: the real XL OS and BASIC (cores/atari800/roms) in the whole Atari core, a cold start, and the
-- application's SIO responder (coreapp/cores/atari_sio.cc, compiled unchanged into the simulator by app_bridge.cc) answering
-- through the core's register window - the two halves that no bench had joined. The testbench plays the application's loop
-- (a8_service every 5 ms, run.cc) and the bridge accesses; it watches the SIO wire itself (command line, the OS's bytes, the
-- drive's bytes) and measures how long each command frame waits for its answer.
--
-- Generics: OS_HEX, BASIC_HEX, RAMSEL, RUN_MS (simulated milliseconds to run), ACCESS_US (what one bridge access costs the
-- application), TICK_US (what an idle pass through the application's loop costs, run.cc vTaskDelay(1)), SLOW_US (the
-- control of S4: the loop sleeps this long after every service call that did something), WATCH_FROM_MS / WATCH_TO_MS (a
-- window in which every edge on the SIO wire is logged and the OS's SIO variables are sampled every 20 us), MODE ("boot": the picture is the
-- program of the boot disk; "basic": the picture is the BASIC prompt), PPM (a picture file).
library ieee;
use ieee.std_logic_1164.all;

-- T108: the application's side as the simulator sees it: VHPIDIRECT into app_bridge.cc
package app_pkg is
  impure function app_init return integer;
  attribute foreign of app_init : function is "VHPIDIRECT app_init";
  impure function app_kick(t : integer) return integer;
  attribute foreign of app_kick : function is "VHPIDIRECT app_kick";
  impure function app_next(t : integer) return integer;
  attribute foreign of app_next : function is "VHPIDIRECT app_next";
  impure function app_reply(v : integer) return integer;
  attribute foreign of app_reply : function is "VHPIDIRECT app_reply";
  impure function app_stat(i : integer) return integer;
  attribute foreign of app_stat : function is "VHPIDIRECT app_stat";
end package;
package body app_pkg is
  impure function app_init return integer is begin assert false severity failure; return 0; end function;
  impure function app_kick(t : integer) return integer is begin assert false severity failure; return 0; end function;
  impure function app_next(t : integer) return integer is begin assert false severity failure; return 0; end function;
  impure function app_reply(v : integer) return integer is begin assert false severity failure; return 0; end function;
  impure function app_stat(i : integer) return integer is begin assert false severity failure; return 0; end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.textio.all;
use work.tb_a8_pkg.all;
use work.app_pkg.all;

entity tb_a8_disk is
  generic (
    OS_HEX    : string  := "build/osxl.hex";
    BASIC_HEX : string  := "build/basxl.hex";
    RAMSEL    : integer := 0;
    RUN_MS    : integer := 3000;
    ACCESS_US : integer := 2;
    TICK_US   : integer := 1000;
    SLOW_US   : integer := 0;
    MODE      : string  := "boot";
    DDR_LAT   : integer := 40;
    DDR_BUSY  : integer := 10;
    PC_FROM_US : integer := 0;
    PC_TO_US   : integer := 0;
    WATCH_FROM_MS : integer := 0;
    WATCH_TO_MS   : integer := 0;
    PPM       : string  := "");
end entity;

architecture sim of tb_a8_disk is
  constant TSYS : time := 31.718 ns;
  constant COLS : integer := 384;
  constant LINES : integer := 288;
  constant OS_FIELD : std_logic_vector(15 downto 0) := x"030E";
  signal sys_clk, cpu_clk, ui_clk, mclk : std_logic := '0';
  signal sys_rst, ui_rst : std_logic := '1';
  signal keys : std_logic_vector(63 downto 0) := (others => '0');
  signal joy1, joy2 : std_logic_vector(4 downto 0) := (others => '0');
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
  signal j_rdata : std_logic_vector(31 downto 0);
  signal j_rvalid : std_logic;
  signal done : boolean := false;
  signal fail : integer := 0;
  signal p_req, p_rom, p_we : std_logic;
  signal p_addr : std_logic_vector(15 downto 0);
  signal p_wdata : std_logic_vector(7 downto 0);

  -- the SIO wire, from the core's tap: the command line (PIA CB2), POKEY's serial out, the drive's line into POKEY
  signal tap_sio : std_logic_vector(2 downto 0);
  signal tap_addr : std_logic_vector(15 downto 0);
  signal w_cmd, w_txd, w_rxd : std_logic;

  -- what the wire monitor measured
  signal n_cmd_frames, n_answered, n_unanswered : integer := 0;
  signal worst_us, last_us : integer := 0;
  signal worst_gap_us : integer := 0;       -- the longest a command frame waited and still got an answer, from the line rising
begin
  sys_clk <= not sys_clk after TSYS / 2 when not done;
  cpu_clk <= not cpu_clk after 15 ns when not done;
  ui_clk  <= not ui_clk  after 15.3 ns when not done;
  mclk    <= not mclk    after 40.7 ns when not done;

  p_req <= win_port_use; p_rom <= win_port_rom; p_we <= win_port_we; p_addr <= win_port_addr; p_wdata <= win_port_wdata;

  dut : entity work.a8_core
    port map (
      sys_clk => sys_clk, sys_rst => sys_rst, cpu_clk => cpu_clk, mclk => mclk, ui_clk => ui_clk, ui_rst => ui_rst,
      reset_req => '0', halt_req => '0', halted => halted, running => running,
      keys => keys, joy1 => joy1, joy2 => joy2, restore => '0',
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
      tap_ce => open, tap_cpu => open, tap_antic => open, tap_addr => tap_addr, tap_e179 => open,
      tap_hblank => open, tap_vblank => open, tap_pixce => open, tap_rgb => open,
      tap_reset_n => open, tap_snoop => open, tap_wdata => open, tap_we => open,
      tap_audio => open, tap_halted => open, tap_sio => tap_sio);
  w_cmd <= tap_sio(0); w_txd <= tap_sio(1); w_rxd <= tap_sio(2);

  -- ---------------------------------------------------------------- the host's DDR2 (as in tb_a8_machine)
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

  -- where the machine is, every 25 ms of simulated time: the bus address (the OS's progress through its cold start)
  where_mon : process
  begin
    while not done loop
      wait for 25 ms;
      exit when done;
      report "where: bus address " & to_hstring(tap_addr) & " at " & integer'image(integer(now / 1 ms)) & " ms";
    end loop;
    wait;
  end process;

  -- the machine's bus address while it is in the ROM (the OS's code), inside the PC window: where the OS was when something happened
  pc_mon : process (tap_addr)
  begin
    if integer(now / 1 us) >= PC_FROM_US and integer(now / 1 us) < PC_TO_US and tap_addr(15 downto 13) = "111" then
      report "pc: " & integer'image(integer(now / 1 us)) & " us " & to_hstring(tap_addr);
    end if;
  end process;

  -- every edge of the three lines inside the watch window
  edge_mon : process
  begin
    wait until w_cmd'event or w_txd'event or w_rxd'event;
    if integer(now / 1 ms) >= WATCH_FROM_MS and integer(now / 1 ms) < WATCH_TO_MS then
      report "edge: " & integer'image(integer(now / 1 us)) & " us cmd " & std_logic'image(w_cmd) & " txd " & std_logic'image(w_txd) &
             " rxd " & std_logic'image(w_rxd);
    end if;
  end process;

  -- ---------------------------------------------------------------- the wire monitor
  -- A command frame is over when the command line rises; the answer starts with the falling edge of the drive's line (the
  -- start bit of the ACK). The time between the two is what the OS gets to see.
  wire_mon : process
    variable t_rise : time := 0 ns;
    variable waiting : boolean := false;
    variable bytes_in_frame : integer := 0;
    variable dt : integer;
  begin
    wait until w_cmd'event or w_rxd'event or w_txd'event;
    if w_txd'event and w_txd = '0' and w_cmd = '0' then
      bytes_in_frame := bytes_in_frame + 1;               -- a start bit or more during the command frame
    elsif w_cmd'event and w_cmd = '0' then
      if waiting then
        n_unanswered <= n_unanswered + 1;
        dt := integer((now - t_rise) / 1 us);
        report "wire: command frame at " & integer'image(integer(t_rise / 1 ms)) & " ms got NO answer; the OS began the next one " &
               integer'image(dt) & " us after the line rose";
      end if;
      waiting := false;
      bytes_in_frame := 0;
    elsif w_cmd'event and w_cmd = '1' and w_cmd'last_value = '0' then
      if bytes_in_frame > 0 then
        t_rise := now;
        waiting := true;
        n_cmd_frames <= n_cmd_frames + 1;
      end if;
    elsif w_rxd'event and w_rxd = '0' and waiting then
      dt := integer((now - t_rise) / 1 us);
      waiting := false;
      n_answered <= n_answered + 1;
      last_us <= dt;
      if dt > worst_us then worst_us <= dt; end if;
      report "wire: command frame at " & integer'image(integer(t_rise / 1 ms)) & " ms answered after " & integer'image(dt) & " us";
    end if;
  end process;

  -- the bytes on both lines, decoded at the OS's standard rate (19200 baud)
  dec_txd : process
    variable b : std_logic_vector(7 downto 0);
    variable n : integer := 0;
  begin
    wait until falling_edge(w_txd);
    wait for 78125 ns;
    for i in 0 to 7 loop b(i) := w_txd; wait for 52083 ns; end loop;
    n := n + 1;
    if n <= 400 then
      report "wire: OS -> drive " & to_hstring(b) & " at " & integer'image(integer(now / 1 us)) & " us, command line " & std_logic'image(w_cmd);
    end if;
  end process;

  dec_rxd : process
    variable b : std_logic_vector(7 downto 0);
    variable n : integer := 0;
  begin
    wait until falling_edge(w_rxd);
    wait for 78125 ns;
    for i in 0 to 7 loop b(i) := w_rxd; wait for 52083 ns; end loop;
    n := n + 1;
    if n <= 400 then
      report "wire: drive -> OS " & to_hstring(b) & " at " & integer'image(integer(now / 1 us)) & " us";
    end if;
  end process;

  -- ---------------------------------------------------------------- the application
  stim : process
    variable l : line;

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

    procedure cw_rd16(off : integer; v : out integer) is
      variable a, b : std_logic_vector(7 downto 0);
    begin
      cw_read(off, a); cw_read(off + 1, b);
      v := to_integer(unsigned(a)) + 256 * to_integer(unsigned(b));
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

    function us_now return integer is begin return integer(now / 1 us); end function;

    type probe_t is array (0 to 6) of integer;
    variable probe_last : probe_t := (others => -1);
    -- the OS's SIO variables: CSTAT $30, the byte received $023E, error flag $023F, DSTATS $0303, timeout flag $0317, command retries $029C, device retries $02BD
    procedure probe is
      type adr_t is array (0 to 6) of integer;
      constant adr : adr_t := (16#30#, 16#23E#, 16#23F#, 16#303#, 16#317#, 16#29C#, 16#2BD#);
      variable q : std_logic_vector(7 downto 0);
      variable v : integer;
      variable changed : boolean := false;
      variable l2 : line;
    begin
      for i in 0 to 6 loop
        win('0', adr(i), '0', x"00", q);
        v := to_integer(unsigned(q));
        if v /= probe_last(i) then changed := true; end if;
        probe_last(i) := v;
      end loop;
      if changed then
        write(l2, string'("os: ") & integer'image(integer(now / 1 us)) & " us  CSTAT " & to_hstring(to_unsigned(probe_last(0), 8)) & " rxbyte " & to_hstring(to_unsigned(probe_last(1), 8)) &
              " errflag " & to_hstring(to_unsigned(probe_last(2), 8)) & " DSTATS " & to_hstring(to_unsigned(probe_last(3), 8)) & " timeout " & to_hstring(to_unsigned(probe_last(4), 8)) &
              " cmd retries left " & to_hstring(to_unsigned(probe_last(5), 8)) & " dev retries left " & to_hstring(to_unsigned(probe_last(6), 8)));
        report l2.all;
      end if;
    end procedure;

    -- one service call of the application's loop: the engine asks, the core answers, simulated time passes
    procedure service(busy : out boolean) is
      variable r, kind, reg, dat, dummy : integer;
      variable d : std_logic_vector(7 downto 0);
    begin
      dummy := app_kick(us_now);
      loop
        r := app_next(us_now);
        kind := r / 16#10000000#;
        reg := (r / 16#10000#) mod 16#1000#;
        dat := r mod 16#10000#;
        case kind is
          when 1 =>
            cw_read(reg, d);
            wait for ACCESS_US * 1 us;
            dummy := app_reply(to_integer(unsigned(d)));
          when 2 =>
            cw_write(reg, std_logic_vector(to_unsigned(dat, 8)));
            wait for ACCESS_US * 1 us;
            dummy := app_reply(0);
          when 3 =>
            wait for dat * 1 us;
            dummy := app_reply(0);
          when others =>
            busy := dat /= 0;
            exit;
        end case;
      end loop;
    end procedure;

    variable d : std_logic_vector(7 downto 0);
    variable n, rv, fb, k, nlit : integer;
    variable bytes_lo, bytes_hi : std_logic_vector(7 downto 0);
    variable px, corner, centre : std_logic_vector(15 downto 0);
    variable busy : boolean;
    variable last_svc_us, spin_until_us, t_end_us : integer;
    variable ok_init : integer;
    variable counts : integer := 0;
    file fo : text;
    variable status : file_open_status;
    variable ll : line;
  begin
    sys_rst <= '1'; ui_rst <= '1';
    wait for 200 ns;
    sys_rst <= '0'; ui_rst <= '0';
    wsys(10);
    -- the application holds the machine in reset while it loads the ROMs and sets the configuration
    cw_write(16#0A0#, x"01");
    cw_write(16#0A1#, std_logic_vector(to_unsigned(RAMSEL, 8)));
    cw_write(16#0A3#, x"00");
    cw_read(16#000#, d);
    ck(d = x"41", "identity byte 0 is 'A'");
    load_rom(OS_HEX, 0, 16384);
    load_rom(BASIC_HEX, 1, 8192);
    ok_init := app_init;
    ck(ok_init = 0, "the application mounted what it was given");
    wait for 3 ms;                                        -- the RAM sweep of the reset
    cw_write(16#0A0#, x"00");                             -- release: the cold start
    report "cold start at " & integer'image(us_now) & " us";
    -- ============================================================ the application's loop (run.cc)
    last_svc_us := us_now - 5000;
    spin_until_us := 0;
    t_end_us := us_now + RUN_MS * 1000;
    while us_now < t_end_us loop
      busy := false;
      if us_now - last_svc_us >= 5000 then              -- desc->service_period_ms = 5
        last_svc_us := us_now;
        service(busy);
        if busy and SLOW_US > 0 then
          wait for SLOW_US * 1 us;                        -- the control: a loop that sleeps after it served something
        end if;
      end if;
      if busy then spin_until_us := us_now + 5000; end if;
      if us_now >= WATCH_FROM_MS * 1000 and us_now < WATCH_TO_MS * 1000 then
        probe;
        wait for 20 us;
      elsif spin_until_us - us_now > 0 then
        wait for 20 us;                                   -- taskYIELD: the pass is short
      else
        wait for TICK_US * 1 us;                          -- vTaskDelay(1)
      end if;
    end loop;
    report "application loop ended at " & integer'image(us_now / 1000) & " ms";
    -- ============================================================ the picture
    cw_rd16(16#0AC#, rv);
    report "frames run: " & integer'image(rv);
    cw_read(16#0C0#, d); fb := to_integer(unsigned(d));
    k := fb mod 4;
    nlit := 0;
    corner := ddr.rd8(16#D00000# + k * 16#100000# + 1) & ddr.rd8(16#D00000# + k * 16#100000#);
    centre := ddr.rd8(16#D00000# + k * 16#100000# + (LINES / 2) * COLS * 2 + (COLS / 2) * 2 + 1) &
              ddr.rd8(16#D00000# + k * 16#100000# + (LINES / 2) * COLS * 2 + (COLS / 2) * 2);
    for y in 0 to LINES - 1 loop
      for xx in 0 to COLS - 1 loop
        bytes_lo := ddr.rd8(16#D00000# + k * 16#100000# + y * COLS * 2 + xx * 2);
        bytes_hi := ddr.rd8(16#D00000# + k * 16#100000# + y * COLS * 2 + xx * 2 + 1);
        px := bytes_hi & bytes_lo;
        if px /= corner and px /= centre then nlit := nlit + 1; end if;
      end loop;
    end loop;
    report "picture: border pixel " & to_hstring(corner) & ", field pixel (centre) " & to_hstring(centre) & ", " & integer'image(nlit) &
           " pixels of a third colour (cursor, text)";
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
    report "sio: " & integer'image(n_cmd_frames) & " command frames on the wire, " & integer'image(n_answered) & " answered, " &
           integer'image(n_unanswered) & " not; the application saw " & integer'image(app_stat(0)) & " frames, " &
           integer'image(app_stat(1)) & " bad checksums, " & integer'image(app_stat(2)) & " unknown, " &
           integer'image(app_stat(3)) & " bridge timeouts";
    report "sio: worst command-frame answer time " & integer'image(worst_us) & " us";
    -- OS_FIELD is the colour the OS paints its text field with before anything else ($94 in this palette, measured), so
    -- "not the OS_FIELD" is a program that ran; the cursor alone is 64 pixels, the prompt's text is more.
    if MODE = "boot" then
      ck(centre /= OS_FIELD and centre /= x"0000" and nlit <= 100,
         "the picture is the boot disk's program (a flat colour that is not the OS's field, cursor only): " & to_hstring(centre) & ", " & integer'image(nlit) & " pixels of a third colour");
    else
      ck(centre = OS_FIELD and nlit > 150, "the picture is the BASIC prompt (the OS's field with text on it): " & to_hstring(centre) & ", " & integer'image(nlit) & " pixels of a third colour");
      ck(n_answered = 0, "no command frame was answered: the drive is not there (" & integer'image(n_cmd_frames) & " frames, " & integer'image(n_answered) & " answered)");
    end if;
    ck(ddr.errors = 0, "no access outside the modelled DDR2 pieces");
    wait for 1 us;
    if fail = 0 then report "PASS tb_a8_disk"; else report "FAIL tb_a8_disk: " & integer'image(fail) & " checks" severity error; end if;
    done <= true;
    wait;
  end process;
end architecture;
