-- T89 S8: the game port. asm/pdl.s runs the monitor's PREAD loop (11 cycles an iteration) on the four paddles and logs them with the buttons.
-- A digital stick gives 0 (left / up), 127 (centred), 255 (right / down); the 558 one-shot timers keep $C064-$C067 bit 7 high for 11 machine
-- cycles per unit of value from the strobe at $C070 - measured here directly on the game port bits (external names), and again through the
-- loop's count. Fire is PB0 / PB1 (with Open / Closed Apple), the app can swap the ports.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.a2_tb_pkg.all;

entity tb_a2_pdl is
  generic (DIR : string := ".");
end entity;

architecture sim of tb_a2_pdl is
  signal sclk, pclk, uclk, mclk : std_logic;
  signal sys_rst : std_logic := '1';
  signal app : app_t := APP_IDLE;
  signal ain : app_in_t;
  signal j1, j2 : std_logic_vector(4 downto 0) := (others => '0');
  signal probe : std_logic_vector(23 downto 0);
  alias a_ce  is probe(0);
  alias a_pzr is probe(4);
  alias a_gp : std_logic_vector(7 downto 0) is probe(23 downto 16);
  alias a_str is probe(5);
  type cyc_t is array (0 to 3) of integer;
  signal dur : cyc_t := (others => -1);          -- cycles the timer of paddle n was high, -1: not measured
  signal rose : std_logic_vector(3 downto 0) := (others => '0');
begin
  h : entity work.a2_harness
    generic map (AUX_DDR => 0, COLD_CLKS => 20000)
    port map (sys_clk => sclk, cpu_clk => pclk, ui_clk => uclk, mclk => mclk, sys_rst => sys_rst, joy1 => j1, joy2 => j2, probe => probe,
              dma_req_tgl => app.dma_tgl, dma_addr => app.dma_addr, dma_we => app.dma_we, dma_wdata => app.dma_wdata, dma_rom => app.dma_rom,
              dma_done_tgl => ain.dma_done, dma_rdata => ain.dma_rdata,
              cw_we => app.cw_we, cw_re => app.cw_re, cw_addr => app.cw_addr, cw_wdata => app.cw_wdata, cw_rdata => ain.cw_rdata, cw_ack => ain.cw_ack);

  -- the timers, counted in machine cycles from the strobe (PHASE_ZERO_R is one per cycle)
  meas : process (sclk)
    variable cnt : cyc_t := (others => 0);
    variable run : std_logic_vector(3 downto 0) := (others => '0');
    variable str_d : std_logic := '0';
    variable seen : std_logic_vector(3 downto 0) := (others => '0');
  begin
    if rising_edge(sclk) and a_ce = '1' then
      if a_str = '1' and str_d = '0' then
        for i in 0 to 3 loop cnt(i) := 0; run(i) := '1'; seen(i) := '0'; end loop;
      end if;
      str_d := a_str;
      for i in 0 to 3 loop
        if run(i) = '1' then
          if a_pzr = '1' then cnt(i) := cnt(i) + 1; end if;
          if a_gp(4 + i) = '1' then seen(i) := '1'; end if;
          if a_gp(4 + i) = '0' and cnt(i) > 3 then
            run(i) := '0';
            if seen(i) = '1' then dur(i) <= cnt(i); else dur(i) <= 0; end if;      -- 0: the timer never ran
          end if;
        end if;
      end loop;
    end if;
  end process;

  stim : process
    variable bad : integer := 0;
    variable v : cyc_t;
    variable seq0, seq, d, b0, b1 : integer;
    procedure round (name : in string; e0, e1, e2, e3 : in integer; tol : in integer) is
    begin
      win_rd(app, ain, '0', 16#0308#, seq0);
      for tries in 1 to 200 loop
        wait for 1 ms;
        win_rd(app, ain, '0', 16#0308#, seq);
        exit when ((seq - seq0 + 256) mod 256) >= 2;
      end loop;
      for i in 0 to 3 loop win_rd(app, ain, '0', 16#0300# + i, v(i)); end loop;
      report name & ": PDL " & integer'image(v(0)) & " " & integer'image(v(1)) & " " & integer'image(v(2)) & " " & integer'image(v(3)) &
             "  (timers, cycles: " & integer'image(dur(0)) & " " & integer'image(dur(1)) & " " & integer'image(dur(2)) & " " & integer'image(dur(3)) & ")";
      if abs (v(0) - e0) > tol or abs (v(1) - e1) > tol or abs (v(2) - e2) > tol or abs (v(3) - e3) > tol then
        report name & ": paddle values off" severity warning; bad := bad + 1;
      end if;
    end procedure;
    procedure timers (name : in string; e0, e1, e2, e3 : in integer) is
    begin
      -- 11 cycles a unit, to within the sampling of the bench (two cycles)
      for i in 0 to 3 loop
        case i is
          when 0 => d := e0; when 1 => d := e1; when 2 => d := e2; when others => d := e3;
        end case;
        if d = 0 then
          if dur(i) /= 0 then report name & ": timer " & integer'image(i) & " ran " & integer'image(dur(i)); bad := bad + 1; end if;
        elsif abs (dur(i) - 11 * d) > 2 then
          report name & ": timer " & integer'image(i) & " ran " & integer'image(dur(i)) & " cycles, wanted " & integer'image(11 * d); bad := bad + 1;
        end if;
      end loop;
    end procedure;
  begin
    wait for 300 ns;
    load_hex(DIR & "/pdl.hex", 0, 16384, pclk, app, ain);
    load_hex(DIR & "/chr.hex", 1, 8192, pclk, app, ain);
    cw_wr(pclk, app, ain, 16#0D4#, 0);
    sys_rst <= '0';
    -- centred: the timers run 127 units
    wait for 5 ms;
    round("centred", 127, 127, 127, 127, 3);
    timers("centred", 127, 127, 127, 127);
    -- stick 1 left + up, stick 2 right + down
    j1 <= "00101"; j2 <= "01010";
    round("j1 left/up, j2 right/down", 0, 0, 255, 255, 3);
    wait for 1 ms;
    round("j1 left/up, j2 right/down (again)", 0, 0, 255, 255, 3);
    timers("extremes", 0, 0, 255, 255);
    -- the fire buttons: PB0 = fire 1, PB1 = fire 2
    j1 <= "10000"; j2 <= "00000";
    wait for 20 ms;
    win_rd(app, ain, '0', 16#0304#, b0); win_rd(app, ain, '0', 16#0305#, b1);
    report "fire 1: PB0 " & integer'image(b0) & ", PB1 " & integer'image(b1);
    if b0 < 128 or b1 >= 128 then report "fire 1 wrong"; bad := bad + 1; end if;
    j1 <= "00000"; j2 <= "10000";
    wait for 20 ms;
    win_rd(app, ain, '0', 16#0304#, b0); win_rd(app, ain, '0', 16#0305#, b1);
    report "fire 2: PB0 " & integer'image(b0) & ", PB1 " & integer'image(b1);
    if b0 >= 128 or b1 < 128 then report "fire 2 wrong"; bad := bad + 1; end if;
    -- the app swaps the ports (CFG bit 6): stick 1 left/up now moves paddles 2 and 3
    j1 <= "00101"; j2 <= "00000";
    cw_wr(pclk, app, ain, 16#0A1#, 64);
    wait for 2 ms;
    round("swapped: j1 left/up", 127, 127, 0, 0, 3);
    if bad = 0 then report "PASS tb_a2_pdl"; else report "FAIL tb_a2_pdl " & integer'image(bad) & " differences"; end if;
    std.env.stop;
  end process;
end architecture;
