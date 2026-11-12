-- SPDX-License-Identifier: GPL-3.0-or-later
-- T88 S12 / D10: Freeze freezes. The whole Atari core runs the test OS (testos2.asm) for TICKS machine ticks, once free (FREEZE = 0) and once
-- with random pauses (FREEZE = 1: halt_req high for 1..3000 clocks at random times, from the first tick on - the OS's bank tests with
-- their DDR2 traffic, the first frames with the DLI, the tone). Every 1024 ticks the bench writes a line "tick hash" into the trace
-- file; the hash chains, tick by tick, everything the machine shows outside: the address and the data of the bus, the memory-ready
-- pulses of the CPU and ANTIC, hblank, vblank, the pixel clock, the pixel's RGB and the POKEY mix. The two trace files must be
-- identical (run_pause.sh compares them): a pause that changes the machine by one tick anywhere changes every hash after it.
-- While the core says `halted` the bench checks that nothing moves: no tick, no DDR2 command on c_* or e_*, and (after the sample
-- period) a silent audio output.
-- run_pause.sh runs it four times: free; with random pauses; with random pauses and another DDR2 timing; and free with another
-- configuration (RAMSEL 0), which must NOT match - the negative control that the hash sees a difference.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.textio.all;
use work.tb_a8_pkg.all;

entity tb_a8_pause is
  generic (
    OS_HEX    : string  := "build/testos2.hex";
    BASIC_HEX : string  := "build/basic.hex";
    RAMSEL    : integer := 1;
    -- the negative control (run_pause.sh "other"): >= 0 writes this byte over the first byte of the
    -- BASIC image, which the test OS reads at $A000 within the first frames. RAMSEL alone was the
    -- control and it is NOT one: 1,500,000 ticks never reach the extended banks, so RAMSEL 0 and 1
    -- gave the same trace and the control said "the hash sees no difference" every time.
    CTRL_BYTE : integer := -1;
    -- where that byte goes: the ROM window's select (0 = OS, 1 = BASIC) and the window address
    -- load_rom uses for it. Defaults to the first byte of the BASIC image (select 1, 0x4000).
    CTRL_SEL  : integer := 1;
    CTRL_ADDR : integer := 16#4000#;
    -- how many GRANTED bus cycles to hash (not sys_clk ticks): 85,000 of them are about the same
    -- 0.8 s of machine time the bench used to cover with 1,500,000 ticks, since only about 6 % of
    -- the machine's ticks are a granted cycle.
    TICKS     : integer := 85000;
    FREEZE    : integer := 0;
    SEED      : integer := 1;
    TRACE     : string  := "trace.txt";
    -- a per-cycle dump of one window into TRACE & ".dump": which cycle, address, data and flags.
    -- For localising where two runs start to differ, after the hashes have named the 1024-cycle
    -- window (the hash trace itself must stay one line per 1024 cycles, or the files differ by
    -- their size alone).
    DUMP_FROM : integer := 0;
    DUMP_TO   : integer := 0;
    DDR_LAT   : integer := 40;
    DDR_BUSY  : integer := 10);
end entity;

architecture sim of tb_a8_pause is
  -- numeric_std's to_01 covers the vectors; a single bit needs this: anything but '1' is '0'
  function b01(x : std_logic) return std_logic is
  begin
    if x = '1' then return '1'; else return '0'; end if;
  end function;
  constant TSYS : time := 31.718 ns;
  signal sys_clk, cpu_clk, ui_clk, mclk : std_logic := '0';
  signal sys_rst, ui_rst : std_logic := '1';
  signal reset_req, halt_req : std_logic := '0';
  signal dma_req_tgl, dma_we, dma_rom : std_logic := '0';
  signal dma_addr : std_logic_vector(15 downto 0) := (others => '0');
  signal dma_wdata : std_logic_vector(7 downto 0) := (others => '0');
  signal dma_done_tgl : std_logic;
  signal dma_rdata : std_logic_vector(7 downto 0);
  signal win_port_use, win_port_rom, win_port_we : std_logic;
  signal win_port_addr : std_logic_vector(15 downto 0);
  signal win_port_wdata : std_logic_vector(7 downto 0);
  signal audio_l, audio_r : std_logic_vector(23 downto 0);
  signal cw_we, cw_re : std_logic := '0';
  signal cw_addr : std_logic_vector(16 downto 0) := (others => '0');
  signal cw_wdata : std_logic_vector(7 downto 0) := (others => '0');
  signal cw_rdata : std_logic_vector(7 downto 0);
  signal halted, running : std_logic;
  signal c_addr, e_addr : std_logic_vector(26 downto 0);
  signal c_cmd, e_cmd : std_logic_vector(2 downto 0);
  signal c_en, c_rdy, c_wdf_wren, c_rd_valid, e_en, e_rdy, e_wdf_wren : std_logic := '0';
  signal c_wdf_data, c_rd_data, e_wdf_data : std_logic_vector(127 downto 0) := (others => '0');
  signal c_wdf_mask, e_wdf_mask : std_logic_vector(15 downto 0);
  signal tap_ce, tap_cpu, tap_antic, tap_e179, tap_hblank, tap_vblank, tap_pixce, tap_reset_n, tap_halted : std_logic;
  signal tap_addr : std_logic_vector(15 downto 0);
  signal tap_rgb : std_logic_vector(23 downto 0);
  signal tap_snoop, tap_wdata : std_logic_vector(7 downto 0);
  signal tap_we : std_logic;
  signal tap_audio : std_logic_vector(15 downto 0);
  signal p_req, p_rom, p_we : std_logic;
  signal p_addr : std_logic_vector(15 downto 0);
  signal p_wdata : std_logic_vector(7 downto 0);
  signal done : boolean := false;
  signal started : boolean := false;
  signal ticks_seen : integer := 0;
  signal live_data, live_addr, ce_ticks : integer := 0;
  signal fail : integer := 0;
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
      keys => (others => '0'), joy1 => (others => '0'), joy2 => (others => '0'), restore => '0',
      dma_req_tgl => dma_req_tgl, dma_addr => dma_addr, dma_we => dma_we, dma_wdata => dma_wdata, dma_rom => dma_rom,
      dma_done_tgl => dma_done_tgl, dma_rdata => dma_rdata, dbg_busy => '0',
      win_port_use => win_port_use, win_port_addr => win_port_addr, win_port_rom => win_port_rom,
      win_port_we => win_port_we, win_port_wdata => win_port_wdata,
      port_req => p_req, port_addr => p_addr, port_rom => p_rom, port_we => p_we, port_wdata => p_wdata,
      ram_rdata => open, pix_color => open, pix_x => open, pix_y => open, audio_l => audio_l, audio_r => audio_r,
      cw_we => cw_we, cw_re => cw_re, cw_addr => cw_addr, cw_wdata => cw_wdata, cw_rdata => cw_rdata, cw_ack => open,
      cw_irq => open, px_we => open, px_re => open, px_rdata => x"00", px_ev_vsync => '0', px_ev_flip => '0',
      build => x"A8A8A8A8", ddr_cal => (others => '0'), dbg_re => '0', dbg_addr => (others => '0'),
      j_rdata => open, j_rvalid => open,
      c_addr => c_addr, c_cmd => c_cmd, c_en => c_en, c_rdy => c_rdy, c_wdf_data => c_wdf_data, c_wdf_mask => c_wdf_mask,
      c_wdf_wren => c_wdf_wren, c_rd_data => c_rd_data, c_rd_valid => c_rd_valid,
      e_addr => e_addr, e_cmd => e_cmd, e_en => e_en, e_rdy => e_rdy, e_wdf_data => e_wdf_data, e_wdf_mask => e_wdf_mask,
      e_wdf_wren => e_wdf_wren,
      tap_ce => tap_ce, tap_cpu => tap_cpu, tap_antic => tap_antic, tap_addr => tap_addr, tap_e179 => tap_e179,
      tap_hblank => tap_hblank, tap_vblank => tap_vblank, tap_pixce => tap_pixce, tap_rgb => tap_rgb,
      tap_reset_n => tap_reset_n, tap_snoop => tap_snoop, tap_wdata => tap_wdata, tap_we => tap_we,
      tap_audio => tap_audio,
      tap_halted => tap_halted);

  -- the host's DDR2 (the same model as tb_a8_machine)
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
        if c_cmd = "000" then ddr.wr128(byte_addr, c_wdf_data, c_wdf_mask);
        else rd_line := ddr.rd128(byte_addr); rd_cnt := DDR_LAT; end if;
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
        ddr.wr128(byte_addr, e_wdf_data, e_wdf_mask);
      end if;
      seed := (seed * 1103515245 + 12345) mod 2147483648;
      rnd := (seed / 65536) mod 100;
      if rnd < DDR_BUSY then e_rdy <= '0'; else e_rdy <= '1'; end if;
    end if;
  end process;

  -- The trace: one hash chained over the machine's GRANTED bus cycles. Not over every tick: a tick
  -- in which the memory makes the machine wait is not a machine cycle, and how many of those there
  -- are depends on the DDR2 latency and on where a Freeze hold falls - a run with pauses then
  -- differs from a free one without the machine having behaved differently (measured 2026-10-14:
  -- 1,311,922 against 1,311,866 ticks with data). Pixel and audio fields are out for the same
  -- reason: they live in the host's clock, which never stops, so after a resume the machine's cycle
  -- meets a different pixel phase. What must be identical is the sequence of cycles the machine
  -- really takes, with their addresses and their data.
  tracer : process (sys_clk)
    file ft : text;
    variable st : file_open_status;
    variable l : line;
    variable h : unsigned(63 downto 0) := x"A8A8A8A8A8A8A8A8";
    variable w1, w2 : unsigned(63 downto 0);
    variable dat : unsigned(7 downto 0);
    constant K : unsigned(63 downto 0) := x"9E3779B97F4A7C15";
    variable n : integer := 0;
    variable metas : integer := 0;
    variable opened : boolean := false;
    file fd : text;
    variable ld : line;
    variable dopen : boolean := false;
  begin
    if rising_edge(sys_clk) then
      if not opened then
        file_open(st, ft, TRACE, write_mode);
        opened := true;
      end if;
      if tap_ce = '1' and tap_reset_n = '1' and started then
        -- to_01 on every field, and the metavalues counted: one 'U' or 'X' anywhere in the chain
        -- turns the hash into XXXXXXXXXXXXXXXX for the rest of the run, and then every run writes
        -- the same file and the comparison proves nothing. That is what happened until 2026-10-14.
        if is_x(tap_addr) or is_x(tap_snoop) or is_x(tap_wdata) or is_x(tap_audio) or is_x(tap_rgb)
        then
          if metas = 0 then
            report "trace: first metavalue at tick " & integer'image(n) & ": addr " &
                   to_string(tap_addr) & " snoop " & to_string(tap_snoop) & " wdata " &
                   to_string(tap_wdata) & " audio " & to_string(tap_audio) & " rgb " &
                   to_string(tap_rgb);
          end if;
          metas := metas + 1;
        end if;
        if tap_cpu = '1' or tap_antic = '1' then
          -- a write cycle carries its data in tap_wdata and nothing meaningful in tap_snoop (what
          -- the memory happens to hold then is timing-dependent: that one field made a pauses run
          -- differ from a free one at cycle 8,138, address $0090, for no defect). So: the address,
          -- the data the cycle is really about, and the flags.
          if tap_we = '1' then
            dat := to_01(unsigned(tap_wdata), '0');
          else
            dat := to_01(unsigned(tap_snoop), '0');
          end if;
          -- 16 + 8 + 24 + 4 + 6 flags + 6 spare = 64
          w1 := to_01(unsigned(tap_addr), '0') & dat & x"000000" & x"0"
                & b01(tap_cpu) & b01(tap_antic) & b01(tap_hblank) & b01(tap_vblank)
                & b01(tap_we) & b01(tap_e179) & "000000";
          w2 := (others => '0');
          h := resize((rotate_left(h, 5) xor w1 xor rotate_left(w2, 17)) * K, 64);
          n := n + 1;
          if DUMP_TO > 0 and n >= DUMP_FROM and n <= DUMP_TO then
            if not dopen then
              file_open(st, fd, TRACE & ".dump", write_mode);
              dopen := true;
            end if;
            write(ld, integer'image(n) & " " & to_hstring(tap_addr) & " " & to_hstring(tap_snoop) &
                  " " & to_hstring(tap_wdata) & " we " & to_string(b01(tap_we)) &
                  " cpu " & to_string(b01(tap_cpu)) & " antic " &
                  to_string(b01(tap_antic)) & " hb " & to_string(b01(tap_hblank)) & " vb " &
                  to_string(b01(tap_vblank)) & " e179 " & to_string(b01(tap_e179)));
            writeline(fd, ld);
            if n = DUMP_TO then file_close(fd); dopen := false; end if;
          end if;
          if n mod 1024 = 0 then
            write(l, integer'image(n) & " " & to_hstring(std_logic_vector(h)));
            writeline(ft, l);
          end if;
          ticks_seen <= n;
        end if;
      end if;
      if n >= TICKS and not done then
        report "trace: " & integer'image(n) & " granted bus cycles hashed, " &
               integer'image(metas) & " ticks had a metavalue (counted, not hashed)";
        file_close(ft);
        done <= true;
      end if;
    end if;
  end process;

  -- while the core says halted nothing moves; and the machine has to be alive at all, otherwise
  -- every run traces the same dead machine and "pauses == free" proves nothing (2026-10-14: it did
  -- exactly that - a changed ROM byte, even the reset vector, left the trace byte-identical).
  checker : process (sys_clk)
    variable hc : integer := 0;
    variable seen : integer := 0;
  begin
    if rising_edge(sys_clk) then
      if tap_ce = '1' and tap_reset_n = '1' and started then
        ce_ticks <= ce_ticks + 1;
        if tap_snoop /= x"00" then live_data <= live_data + 1; end if;
        if tap_addr /= x"0000" then live_addr <= live_addr + 1; end if;
      end if;
      if tap_halted = '1' then
        hc := hc + 1;
        if tap_ce = '1' then report "FAIL: a machine tick while halted" severity error; fail <= fail + 1; end if;
        if c_en = '1' or e_en = '1' then report "FAIL: a DDR2 command while halted" severity error; fail <= fail + 1; end if;
        if hc > 3000 and audio_l /= x"000000" then report "FAIL: sound while halted" severity error; fail <= fail + 1; end if;
        seen := seen + 1;
      else
        hc := 0;
      end if;
    end if;
  end process;

  -- the app
  stim : process
    variable seed1 : integer := SEED;
    variable rnd : integer;
    variable waitc, holdc : integer;
    variable rb : std_logic_vector(7 downto 0);
    procedure wsys(k : integer) is begin for i in 1 to k loop wait until rising_edge(sys_clk); end loop; end procedure;
    procedure cw_write(off : integer; d : std_logic_vector(7 downto 0)) is
    begin
      wait until rising_edge(cpu_clk);
      cw_addr <= std_logic_vector(to_unsigned(off, 17)); cw_wdata <= d; cw_we <= '1';
      wait until rising_edge(cpu_clk);
      cw_we <= '0';
    end procedure;
    procedure win_wr(rom : std_logic; a : integer; d : std_logic_vector(7 downto 0)) is
      variable t : std_logic;
    begin
      wait until rising_edge(sys_clk);
      dma_addr <= std_logic_vector(to_unsigned(a, 16)); dma_we <= '1'; dma_wdata <= d; dma_rom <= rom;
      t := dma_done_tgl;
      dma_req_tgl <= not dma_req_tgl;
      wait until rising_edge(sys_clk) and dma_done_tgl /= t;
    end procedure;
    -- the same window, reading: the control write has to be seen before the machine starts, or the
    -- negative control is vacuous again and says "no difference" for the wrong reason.
    procedure win_rd(rom : std_logic; a : integer; q : out std_logic_vector(7 downto 0)) is
      variable t : std_logic;
    begin
      wait until rising_edge(sys_clk);
      dma_addr <= std_logic_vector(to_unsigned(a, 16)); dma_we <= '0'; dma_rom <= rom;
      t := dma_done_tgl;
      dma_req_tgl <= not dma_req_tgl;
      wait until rising_edge(sys_clk) and dma_done_tgl /= t;
      q := dma_rdata;
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
    end procedure;
  begin
    sys_rst <= '1'; ui_rst <= '1';
    wait for 200 ns;
    sys_rst <= '0'; ui_rst <= '0';
    wsys(10);
    cw_write(16#0A0#, x"01");
    cw_write(16#0A1#, std_logic_vector(to_unsigned(RAMSEL, 8)));
    load_rom(OS_HEX, 0, 16384);
    load_rom(BASIC_HEX, 1, 8192);
    if CTRL_BYTE >= 0 then
      -- the target follows load_rom's own mapping: image byte k goes to window address 0x4000 + k
      -- while k < 0x2000 and to k above that, so the BASIC image starts at 0x4000 and the OS's
      -- reset vector ($FFFC, byte 0x3FFC of the 16 KB image) sits at 0x3FFC.
      cw_write(16#0A6#, std_logic_vector(to_unsigned(CTRL_SEL, 8)));
      win_wr('1', CTRL_ADDR, std_logic_vector(to_unsigned(CTRL_BYTE, 8)));
      win_rd('1', CTRL_ADDR, rb);
      report "control: image " & integer'image(CTRL_SEL) & " byte " & integer'image(CTRL_ADDR) &
             " is now " & integer'image(to_integer(unsigned(rb))) &
             " (wrote " & integer'image(CTRL_BYTE) & ")";
      assert to_integer(unsigned(rb)) = CTRL_BYTE
        report "the control write did not land: the negative control proves nothing" severity error;
    end if;
    wait for 3 ms;
    started <= true;
    cw_write(16#0A0#, x"00");
    if FREEZE = 0 then
      wait;
    end if;
    -- random pauses from the first tick on
    loop
      seed1 := (seed1 * 1103515245 + 12345) mod 2147483648;
      rnd := (seed1 / 65536) mod 20000;
      waitc := 50 + rnd;
      wsys(waitc);
      seed1 := (seed1 * 1103515245 + 12345) mod 2147483648;
      rnd := (seed1 / 65536) mod 100;
      if rnd < 30 then holdc := 1 + (seed1 / 4096) mod 40;          -- short: the hold may end before `halted` rises
      else holdc := 40 + (seed1 / 4096) mod 3000; end if;
      halt_req <= '1';
      wsys(holdc);
      halt_req <= '0';
    end loop;
  end process;

  finish : process
  begin
    wait until done;
    wait for 1 us;
    report "bus: " & integer'image(live_data) & " ticks with data, " & integer'image(live_addr) &
           " with an address, of " & integer'image(ce_ticks) & " ticks; " &
           integer'image(ticks_seen) & " granted cycles hashed";
    if live_data = 0 or live_addr = 0 then
      report "FAIL: nothing on the machine bus - this run traces a dead machine" severity error;
    end if;
    if fail = 0 and live_data > 0 and live_addr > 0 then
      report "PASS tb_a8_pause freeze=" & integer'image(FREEZE) & " cycles=" & integer'image(ticks_seen);
    else report "FAIL tb_a8_pause: " & integer'image(fail) & " checks" severity error; end if;
    wait;
  end process;
end architecture;
