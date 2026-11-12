-- T89 S7: the keyboard. The C64U's matrix (keys, 1 = down; index = column * 8 + row) is pressed key by key, plain and with SHIFT, and the
-- ASCII byte the //e program reads at $C000 (bit 7 set), $C010 (bit 7 = any key down) and $C061 / $C062 (Open / Closed Apple) is compared with
-- the table written here from the symbols on the C64 keycaps. CTRL, CAPS LOCK, the app's typing and auto repeat (with shortened delays: the
-- counting is what is tested, the defaults are checked by the assertion at the bottom) too. asm/keys.s logs what it gets.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.a2_tb_pkg.all;

entity tb_a2_keys is
  generic (DIR : string := ".");
end entity;

architecture sim of tb_a2_keys is
  type int3_t is array (0 to 2) of integer;
  constant TYPED : int3_t := (character'pos('h'), character'pos('i'), 13);      -- what the app typed through register 140
  signal sclk, pclk, uclk, mclk : std_logic;
  signal sys_rst : std_logic := '1';
  signal app : app_t := APP_IDLE;
  signal ain : app_in_t;
  signal keys : std_logic_vector(63 downto 0) := (others => '0');
  signal ce_cnt : std_logic_vector(31 downto 0);
  type tab_t is array (0 to 63) of integer;
  -- by the symbol on the keycap: 0 = no code. Unshifted, then with SHIFT. SHIFT+LEFT-ARROW is the underscore (CAPS LOCK is the app's register 0A2)
  constant PLAIN : tab_t := (
    16#7F#, 16#0D#, 16#15#, 0, 0, 0, 0, 16#0A#,
    character'pos('3'), character'pos('W'), character'pos('A'), character'pos('4'), character'pos('Z'), character'pos('S'), character'pos('E'), 0,
    character'pos('5'), character'pos('R'), character'pos('D'), character'pos('6'), character'pos('C'), character'pos('F'), character'pos('T'), character'pos('X'),
    character'pos('7'), character'pos('Y'), character'pos('G'), character'pos('8'), character'pos('B'), character'pos('H'), character'pos('U'), character'pos('V'),
    character'pos('9'), character'pos('I'), character'pos('J'), character'pos('0'), character'pos('M'), character'pos('K'), character'pos('O'), character'pos('N'),
    character'pos('+'), character'pos('P'), character'pos('L'), character'pos('-'), character'pos('.'), character'pos(':'), character'pos('@'), character'pos(','),
    character'pos('\'), character'pos('*'), character'pos(';'), 0, 0, character'pos('='), character'pos('^'), character'pos('/'),
    character'pos('1'), character'pos('_'), 0, character'pos('2'), character'pos(' '), 0, character'pos('Q'), 16#1B#);
  constant SHIFTED : tab_t := (
    16#7F#, 16#0D#, 16#08#, 0, 0, 0, 0, 16#0B#,
    character'pos('#'), character'pos('W'), character'pos('A'), character'pos('$'), character'pos('Z'), character'pos('S'), character'pos('E'), 0,
    character'pos('%'), character'pos('R'), character'pos('D'), character'pos('&'), character'pos('C'), character'pos('F'), character'pos('T'), character'pos('X'),
    character'pos('''), character'pos('Y'), character'pos('G'), character'pos('('), character'pos('B'), character'pos('H'), character'pos('U'), character'pos('V'),
    character'pos(')'), character'pos('I'), character'pos('J'), character'pos('0'), character'pos('M'), character'pos('K'), character'pos('O'), character'pos('N'),
    character'pos('{'), character'pos('P'), character'pos('L'), character'pos('}'), character'pos('>'), character'pos('['), character'pos('`'), character'pos('<'),
    character'pos('|'), character'pos('*'), character'pos(']'), 0, 0, character'pos('='), character'pos('~'), character'pos('?'),
    character'pos('!'), character'pos('_'), 0, character'pos('"'), character'pos(' '), 0, character'pos('Q'), 16#1B#);
  constant LSHIFT : integer := 1 * 8 + 7;
  constant RSHIFT : integer := 6 * 8 + 4;
  constant CTRLK  : integer := 7 * 8 + 2;
  constant CBM    : integer := 7 * 8 + 5;     -- Open Apple
  constant HOME   : integer := 6 * 8 + 3;     -- Closed Apple
  constant LARROW : integer := 7 * 8 + 1;
begin
  h : entity work.a2_harness
    generic map (AUX_DDR => 0, COLD_CLKS => 20000, REPEAT_DELAY => 3000, REPEAT_RATE => 700)
    port map (sys_clk => sclk, cpu_clk => pclk, ui_clk => uclk, mclk => mclk, sys_rst => sys_rst, keys => keys,
              dma_req_tgl => app.dma_tgl, dma_addr => app.dma_addr, dma_we => app.dma_we, dma_wdata => app.dma_wdata, dma_rom => app.dma_rom,
              dma_done_tgl => ain.dma_done, dma_rdata => ain.dma_rdata,
              cw_we => app.cw_we, cw_re => app.cw_re, cw_addr => app.cw_addr, cw_wdata => app.cw_wdata, cw_rdata => ain.cw_rdata, cw_ack => ain.cw_ack,
              dbg_ce_cnt => ce_cnt);

  stim : process
    variable n : integer := 0;                      -- entries logged so far
    variable bad : integer := 0;
    variable tested : integer := 0;
    variable d, e0, e1, e2, e3 : integer;
    variable t0, t1, t2, t3 : integer;

    -- wait for entry n to appear (its key byte has bit 7) and return the four bytes
    procedure get_entry (idx : in integer; ok : out boolean; k, akd, oa, ca : out integer) is
      variable v : integer;
    begin
      ok := false;
      for tries in 1 to 200 loop
        wait for 20 us;
        win_rd(app, ain, '0', 16#0300# + 4 * idx, v);
        if v >= 128 then ok := true; exit; end if;
      end loop;
      k := v;
      wait for 100 us;                                -- the program stores the other three bytes after the key byte
      win_rd(app, ain, '0', 16#0301# + 4 * idx, akd);
      win_rd(app, ain, '0', 16#0302# + 4 * idx, oa);
      win_rd(app, ain, '0', 16#0303# + 4 * idx, ca);
    end procedure;

    procedure let_go is
    begin
      keys <= (others => '0');
      wait for 300 us;                               -- the program is back at its loop, the keyboard sees "no key"
    end procedure;

    procedure expect_key (name : in string; want : in integer; held : in std_logic_vector(63 downto 0); want_oa, want_ca : in integer) is
      variable ok : boolean;
      variable k, a, o, c : integer;
    begin
      keys <= held;
      get_entry(n, ok, k, a, o, c);
      tested := tested + 1;
      if not ok then
        report name & ": no key arrived" severity warning; bad := bad + 1;
      else
        if k /= 128 + want then report name & ": code " & integer'image(k - 128) & ", wanted " & integer'image(want); bad := bad + 1; end if;
        if a < 128 then report name & ": any-key-down not set while the key is held (byte " & integer'image(a) & ")"; bad := bad + 1; end if;
        if (o >= 128) /= (want_oa = 1) then report name & ": Open Apple byte " & integer'image(o) & ", wanted " & integer'image(want_oa); bad := bad + 1; end if;
        if (c >= 128) /= (want_ca = 1) then report name & ": Closed Apple " & integer'image(c); bad := bad + 1; end if;
        n := n + 1;
      end if;
      let_go;
    end procedure;

    variable h1 : std_logic_vector(63 downto 0);
    variable rv : integer;
    variable s, r, tt : integer;
    variable ok : boolean;
    variable k, a, o, c : integer;
  begin
    wait for 300 ns;
    load_hex(DIR & "/keys.hex", 0, 16384, pclk, app, ain);
    load_hex(DIR & "/chr.hex", 1, 8192, pclk, app, ain);
    cw_wr(pclk, app, ain, 16#0D4#, 0);
    sys_rst <= '0';
    wait for 2 ms;
    -- every key, plain and shifted; the keys with no code are pressed too and must not produce one (checked by the count at the end)
    for idx in 0 to 63 loop
      if idx /= LSHIFT and idx /= RSHIFT and idx /= CTRLK and idx /= CBM and idx /= HOME then
        if PLAIN(idx) /= 0 then
          h1 := (others => '0'); h1(idx) := '1';
          expect_key("key " & integer'image(idx / 8) & "," & integer'image(idx mod 8) & " plain", PLAIN(idx), h1, 0, 0);
        end if;
        if SHIFTED(idx) /= 0 then
          h1 := (others => '0'); h1(idx) := '1'; h1(LSHIFT) := '1';
          expect_key("key " & integer'image(idx / 8) & "," & integer'image(idx mod 8) & " shifted", SHIFTED(idx), h1, 0, 0);
        end if;
      end if;
    end loop;
    -- the right SHIFT works as the left one
    h1 := (others => '0'); h1(7 * 8 + 3) := '1'; h1(RSHIFT) := '1';
    expect_key("RSHIFT+2", character'pos('"'), h1, 0, 0);
    -- CTRL clears bits 6:5: CTRL-A is $01, CTRL-I (TAB) $09, CTRL-@ $00, CTRL-1 stays '1'
    h1 := (others => '0'); h1(1 * 8 + 2) := '1'; h1(CTRLK) := '1';
    expect_key("CTRL-A", 1, h1, 0, 0);
    h1 := (others => '0'); h1(4 * 8 + 1) := '1'; h1(CTRLK) := '1';
    expect_key("CTRL-I (TAB)", 9, h1, 0, 0);
    h1 := (others => '0'); h1(5 * 8 + 6) := '1'; h1(CTRLK) := '1';
    expect_key("CTRL-@", 0, h1, 0, 0);
    h1 := (others => '0'); h1(7 * 8 + 0) := '1'; h1(CTRLK) := '1';
    expect_key("CTRL-1", character'pos('1'), h1, 0, 0);
    -- Open Apple (C=) and Closed Apple (HOME) read at $C061 / $C062 while a key is down
    h1 := (others => '0'); h1(1 * 8 + 2) := '1'; h1(CBM) := '1';
    expect_key("C=+A", character'pos('A'), h1, 1, 0);
    h1 := (others => '0'); h1(1 * 8 + 2) := '1'; h1(HOME) := '1';
    expect_key("HOME+A", character'pos('A'), h1, 0, 1);
    -- CAPS LOCK is register 0A2 (the app's, from the C64U's SHIFT LOCK): 1 at power-up, 0 gives lower case, 1 again capitals; STATUS b3 follows
    cw_rd(pclk, app, ain, 16#0A2#, rv);
    if rv /= 1 then report "FAIL tb_a2_keys CAPS reads " & integer'image(rv) & " at power-up, expected 1"; bad := bad + 1; end if;
    cw_wr(pclk, app, ain, 16#0A2#, 0);
    wait for 400 us;
    cw_rd(pclk, app, ain, 16#0A4#, rv);
    if (rv / 8) mod 2 /= 0 then report "FAIL tb_a2_keys STATUS b3 still shows CAPS LOCK after 0A2 = 0"; bad := bad + 1; end if;
    h1 := (others => '0'); h1(1 * 8 + 2) := '1';
    expect_key("a (caps lock off)", character'pos('a'), h1, 0, 0);
    h1 := (others => '0'); h1(1 * 8 + 2) := '1'; h1(LSHIFT) := '1';
    expect_key("SHIFT+a (caps lock off)", character'pos('A'), h1, 0, 0);
    cw_wr(pclk, app, ain, 16#0A2#, 1);
    wait for 400 us;
    h1 := (others => '0'); h1(1 * 8 + 2) := '1';
    expect_key("A (caps lock on again)", character'pos('A'), h1, 0, 0);
    -- keys with no code of their own: F1 F3 F5 F7, SHIFT, CTRL, C=, HOME alone produce nothing
    for q in 0 to 8 loop
      h1 := (others => '0');
      case q is
        when 0 => h1(4) := '1';
        when 1 => h1(5) := '1';
        when 2 => h1(6) := '1';
        when 3 => h1(3) := '1';
        when 4 => h1(LSHIFT) := '1';
        when 5 => h1(RSHIFT) := '1';
        when 6 => h1(CTRLK) := '1';
        when 7 => h1(CBM) := '1';
        when others => h1(HOME) := '1';
      end case;
      keys <= h1; wait for 400 us;
      win_rd(app, ain, '0', 16#0300# + 4 * n, d);
      if d >= 128 then report "a key without a code produced " & integer'image(d - 128); bad := bad + 1; n := n + 1; end if;
      let_go;
    end loop;
    -- the app types
    for q in -2 to -1 loop
      win_rd(app, ain, '0', 16#0300# + 4 * (n + q), d);
      report "log entry " & integer'image(n + q) & " before the typing: " & integer'image(d);
    end loop;
    cw_wr(pclk, app, ain, 16#140#, character'pos('h'));
    cw_wr(pclk, app, ain, 16#140#, character'pos('i'));
    cw_wr(pclk, app, ain, 16#140#, 13);
    for q in 0 to 2 loop
      get_entry(n, ok, k, a, o, c);
      report "typed " & integer'image(q) & ": log entry " & integer'image(n) & " = " & integer'image(k - 128) & " ok " & boolean'image(ok);
      if not ok or k - 128 /= TYPED(q) then
        report "typing: code " & integer'image(k - 128) & " at " & integer'image(q); bad := bad + 1;
      end if;
      n := n + 1;
    end loop;
    -- auto repeat: a key held after REPEAT_DELAY machine cycles repeats every REPEAT_RATE cycles; the counting is in the machine's time
    h1 := (others => '0'); h1(7 * 8 + 6) := '1';
    keys <= h1;
    get_entry(n, ok, k, a, o, c); n := n + 1;                  -- the press
    t0 := to_integer(unsigned(ce_cnt));
    get_entry(n, ok, k, a, o, c); n := n + 1;                  -- the first repeat
    t1 := to_integer(unsigned(ce_cnt));
    get_entry(n, ok, k, a, o, c); n := n + 1;                  -- the second
    t2 := to_integer(unsigned(ce_cnt));
    get_entry(n, ok, k, a, o, c); n := n + 1;                  -- the third
    t3 := to_integer(unsigned(ce_cnt));
    let_go;
    if (t1 - t0) < 3000 * 14 - 1500 or (t1 - t0) > 3000 * 14 + 1500 then report "repeat delay wrong"; bad := bad + 1; end if;
    report "repeat: press to first repeat " & integer'image(t1 - t0) & " enables, then " & integer'image(t2 - t1) & ", " & integer'image(t3 - t2) & " (the generics: 3000 and 700 cycles of 14 enables)";
    -- the counting is in the machine's time: the polling granularity of this bench is a few thousand enables, so the check is a window
    if (t2 - t1) < 700 * 14 - 1500 or (t2 - t1) > 700 * 14 + 1500 then report "repeat rate wrong"; bad := bad + 1; end if;
    if k /= 128 + character'pos('Q') then report "repeat code " & integer'image(k); bad := bad + 1; end if;
    report "keys tested: " & integer'image(tested) & ", logged: " & integer'image(n);
    if bad = 0 then report "PASS tb_a2_keys"; else report "FAIL tb_a2_keys " & integer'image(bad) & " differences"; end if;
    std.env.stop;
  end process;
end architecture;
