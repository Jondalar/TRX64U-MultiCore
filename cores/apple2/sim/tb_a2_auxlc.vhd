-- T89 S11 and the aux RAM in the DDR2 (Q4): aux RAM through RAMRD/RAMWRT, ALTZP (zero page and stack), 80STORE + PAGE2 and the language
-- card behave as on a //e (asm/auxlc.s: our own 6502 code, no Apple ROM call), and the machine with its aux RAM in the DDR2 behind the cache
-- (a2_aux, stalling on a miss) is the SAME machine as the one with a block RAM: both run the program side by side and
--   - the results the program leaves in main RAM are the expected ones,
--   - aux RAM read back through the DMA window (the DDR2 engine's own job) holds the expected bytes,
--   - at the same enable count the two machines' trace signatures (every address and data the CPU put out, the video bit, the speaker) agree,
--   - the DDR2 variant did wait (misses and stall clocks are counted: the cache works and costs time).
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.textio.all;
use work.a2_tb_pkg.all;

entity tb_a2_auxlc is
  generic (
    DIR    : string := ".";
    SIGN   : integer := 400000;             -- enables to run before the signatures are compared
    C_LAT  : integer := 40
  );
end entity;

architecture sim of tb_a2_auxlc is
  type res_t is array (0 to 15) of integer;
  type chk_t is array (0 to 3) of integer;
  signal clkA, clkB : std_logic;
  signal pclkA, uclkA, mclkA, pclkB, uclkB, mclkB : std_logic;
  signal rstA, rstB : std_logic := '1';
  signal appA, appB : app_t := APP_IDLE;
  signal ainA, ainB : app_in_t;
  signal ceA, ceB, sigA, sigB : std_logic_vector(31 downto 0);
  signal resA, resB : res_t := (others => -1);
  signal chkA, chkB : chk_t := (others => -1);
  signal doneA, doneB : boolean := false;
  signal sigsA, sigsB : std_logic_vector(31 downto 0) := (others => 'X');
  signal gotA, gotB : boolean := false;
  signal missA, stallA, missB, stallB : integer := 0;
  signal cA, cB, wA, wB : integer;
  constant EXPECT : res_t := (16#11#, 16#22#, 16#11#, 16#33#, 16#44#, 16#55#, 16#77#, 16#66#, 16#88#, 16#AA#, 16#99#, 16#88#, 16#00#, 16#23#, -1, 16#A5#);

  procedure session (tag : string; signal pclk : in std_logic; signal app : inout app_t; signal ain : in app_in_t; signal rst : out std_logic;
                     signal res : out res_t; signal chk : out chk_t; signal done : out boolean; signal miss, stall : out integer) is
    variable d, k, t : integer;
    variable r : res_t := (others => -1);
    variable c : chk_t;
  begin
    wait for 300 ns;
    load_hex(DIR & "/auxlc.hex", 0, 16384, pclk, app, ain);
    load_hex(DIR & "/chr.hex", 1, 8192, pclk, app, ain);
    cw_wr(pclk, app, ain, 16#0D4#, 0);
    rst <= '0';
    report tag & ": loaded";
    -- the program says it is done in LOG+15
    for tries in 1 to 400 loop
      wait for 100 us;
      win_rd(app, ain, '0', 16#030F#, d);
      if tries mod 50 = 0 then report tag & ": poll " & integer'image(tries) & " LOG+15 = " & integer'image(d); end if;
      exit when d = 16#A5#;
    end loop;
    report tag & ": program done";
    for k in 0 to 15 loop
      win_rd(app, ain, '0', 16#0300# + k, d);
      r(k) := d;
    end loop;
    res <= r;
    report tag & ": results read";
    -- the aux RAM through the window (array 5): the DDR2 engine's own read job in the DDR2 variant
    cw_wr(pclk, app, ain, 16#0D4#, 5);
    report tag & ": window on aux";
    win_rd(app, ain, '1', 16#6000#, d); c(0) := d;
    win_rd(app, ain, '1', 16#0010#, d); c(1) := d;
    win_rd(app, ain, '1', 16#0400#, d); c(2) := d;
    win_rd(app, ain, '1', 16#01FF#, d); c(3) := d;
    chk <= c;
    report tag & ": aux read";
    cw_rd(pclk, app, ain, 16#0B0#, d); t := d;
    cw_rd(pclk, app, ain, 16#0B1#, d); t := t + d * 256;
    miss <= t;
    cw_rd(pclk, app, ain, 16#0B4#, d); t := d;
    cw_rd(pclk, app, ain, 16#0B5#, d); t := t + d * 256;
    cw_rd(pclk, app, ain, 16#0B6#, d); t := t + d * 65536;
    stall <= t;
    done <= true;
    wait;
  end procedure;
begin
  hA : entity work.a2_harness
    generic map (AUX_DDR => 0, COLD_CLKS => 20000)
    port map (sys_clk => clkA, cpu_clk => pclkA, ui_clk => uclkA, mclk => mclkA, sys_rst => rstA,
              dma_req_tgl => appA.dma_tgl, dma_addr => appA.dma_addr, dma_we => appA.dma_we, dma_wdata => appA.dma_wdata, dma_rom => appA.dma_rom,
              dma_done_tgl => ainA.dma_done, dma_rdata => ainA.dma_rdata,
              cw_we => appA.cw_we, cw_re => appA.cw_re, cw_addr => appA.cw_addr, cw_wdata => appA.cw_wdata, cw_rdata => ainA.cw_rdata, cw_ack => ainA.cw_ack,
              dbg_ce_cnt => ceA, dbg_sig => sigA, c_reads => cA, c_writes => wA);
  hB : entity work.a2_harness
    generic map (AUX_DDR => 1, COLD_CLKS => 20000, C_LAT => C_LAT)
    port map (sys_clk => clkB, cpu_clk => pclkB, ui_clk => uclkB, mclk => mclkB, sys_rst => rstB,
              dma_req_tgl => appB.dma_tgl, dma_addr => appB.dma_addr, dma_we => appB.dma_we, dma_wdata => appB.dma_wdata, dma_rom => appB.dma_rom,
              dma_done_tgl => ainB.dma_done, dma_rdata => ainB.dma_rdata,
              cw_we => appB.cw_we, cw_re => appB.cw_re, cw_addr => appB.cw_addr, cw_wdata => appB.cw_wdata, cw_rdata => ainB.cw_rdata, cw_ack => ainB.cw_ack,
              dbg_ce_cnt => ceB, dbg_sig => sigB, c_reads => cB, c_writes => wB);

  sa : process
  begin
    session("block RAM", pclkA, appA, ainA, rstA, resA, chkA, doneA, missA, stallA);
  end process;
  sb : process
  begin
    session("DDR2", pclkB, appB, ainB, rstB, resB, chkB, doneB, missB, stallB);
  end process;

  -- the trace signatures at the same enable count
  capA : process (clkA)
  begin
    if rising_edge(clkA) and not gotA and unsigned(ceA) = SIGN then sigsA <= sigA; gotA <= true; end if;
  end process;
  capB : process (clkB)
  begin
    if rising_edge(clkB) and not gotB and unsigned(ceB) = SIGN then sigsB <= sigB; gotB <= true; end if;
  end process;

  judge : process
    variable bad : integer := 0;
  begin
    wait until doneA and doneB and gotA and gotB for 150 ms;
    if not (doneA and doneB and gotA and gotB) then
      report "FAIL tb_a2_auxlc: a variant did not finish (block RAM done " & boolean'image(doneA) & ", DDR2 done " & boolean'image(doneB) & ")";
      std.env.stop;
    end if;
    wait for 1 us;
    for k in 0 to 15 loop
      if k /= 14 then
        if resA(k) /= EXPECT(k) then report "block RAM variant: LOG+" & integer'image(k) & " = " & integer'image(resA(k)) & ", wanted " & integer'image(EXPECT(k)); bad := bad + 1; end if;
        if resB(k) /= EXPECT(k) then report "DDR2 variant: LOG+" & integer'image(k) & " = " & integer'image(resB(k)) & ", wanted " & integer'image(EXPECT(k)); bad := bad + 1; end if;
      end if;
    end loop;
    if chkA(0) /= 16#23# or chkB(0) /= 16#23# then report "aux $6000: " & integer'image(chkA(0)) & " / " & integer'image(chkB(0)); bad := bad + 1; end if;
    if chkA(1) /= 16#44# or chkB(1) /= 16#44# then report "aux $0010: " & integer'image(chkA(1)) & " / " & integer'image(chkB(1)); bad := bad + 1; end if;
    if chkA(2) /= 16#66# or chkB(2) /= 16#66# then report "aux $0400: " & integer'image(chkA(2)) & " / " & integer'image(chkB(2)); bad := bad + 1; end if;
    if chkA(3) /= 16#F0# or chkB(3) /= 16#F0# then report "aux $01FF (the JSR's return address, high byte): " & integer'image(chkA(3)) & " / " & integer'image(chkB(3)); bad := bad + 1; end if;
    report "DDR2 variant: " & integer'image(cB) & " line reads, " & integer'image(wB) & " byte writes, stall " & integer'image(stallB) & " sys_clk, misses " & integer'image(missB);
    if cB = 0 or missB = 0 or stallB = 0 then report "the DDR2 variant never went to the DDR2"; bad := bad + 1; end if;
    report "trace signature at " & integer'image(SIGN) & " enables: block RAM " & to_hstring(sigsA) & ", DDR2 " & to_hstring(sigsB);
    if sigsA /= sigsB then report "the two machines are not the same machine"; bad := bad + 1; end if;
    if bad = 0 then report "PASS tb_a2_auxlc"; else report "FAIL tb_a2_auxlc " & integer'image(bad) & " differences"; end if;
    std.env.stop;
  end process;
end architecture;
