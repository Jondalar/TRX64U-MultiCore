-- T89 Q4 ("its latency to be handled and measured"): what the aux RAM in the DDR2 costs in machine time. Programs that use aux RAM - the 80 column
-- screen (the scanner reads aux on every machine cycle), zero page and stack in aux, both - run in the DDR2 variant for a while; the counters of the
-- cache (misses, sys_clk clocks the machine was held) are read at the start and the end, and the lost fraction of machine time is reported.
-- The DDR2 model answers a read after C_LAT ui_clk (13.3 ns each); the bench reports the numbers, it fails if the machine is held more than the
-- limit (a quarter of its time: a cache that does not work would hold it for most of it).
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.a2_tb_pkg.all;

entity tb_a2_stall is
  generic (DIR : string := "."; PROG : string := "text80.hex"; C_LAT : integer := 40; RUN_MS : integer := 40; LIMIT_PCT : integer := 25);
end entity;

architecture sim of tb_a2_stall is
  signal sclk, pclk, uclk, mclk : std_logic;
  signal sys_rst : std_logic := '1';
  signal app : app_t := APP_IDLE;
  signal ain : app_in_t;
  signal ce_cnt : std_logic_vector(31 downto 0);
begin
  h : entity work.a2_harness
    generic map (AUX_DDR => 1, COLD_CLKS => 20000, C_LAT => C_LAT)
    port map (sys_clk => sclk, cpu_clk => pclk, ui_clk => uclk, mclk => mclk, sys_rst => sys_rst,
              dma_req_tgl => app.dma_tgl, dma_addr => app.dma_addr, dma_we => app.dma_we, dma_wdata => app.dma_wdata, dma_rom => app.dma_rom,
              dma_done_tgl => ain.dma_done, dma_rdata => ain.dma_rdata,
              cw_we => app.cw_we, cw_re => app.cw_re, cw_addr => app.cw_addr, cw_wdata => app.cw_wdata, cw_rdata => ain.cw_rdata, cw_ack => ain.cw_ack,
              dbg_ce_cnt => ce_cnt);

  stim : process
    variable m0, m1, s0, s1, t0, t1, d, k : integer;
    procedure get32 (a : in integer; v : out integer) is
      variable d : integer; variable r : integer := 0;
    begin
      for i in 0 to 3 loop cw_rd(pclk, app, ain, a + i, d); r := r + d * (256 ** i); end loop;
      v := r;
    end procedure;
    variable clks, lost_pct, per_s : integer;
  begin
    wait for 300 ns;
    load_hex(DIR & "/" & PROG, 0, 16384, pclk, app, ain);
    load_hex(DIR & "/chr.hex", 1, 8192, pclk, app, ain);
    cw_wr(pclk, app, ain, 16#0D4#, 0);
    sys_rst <= '0';
    wait for 12 ms;                                    -- past the power-on reset and the program's setup
    get32(16#0B0#, m0); get32(16#0B4#, s0); t0 := to_integer(unsigned(ce_cnt));
    wait for RUN_MS * 1 ms;
    get32(16#0B0#, m1); get32(16#0B4#, s1); t1 := to_integer(unsigned(ce_cnt));
    clks := RUN_MS * 32727;                            -- sys_clk cycles of the interval (32.727 MHz)
    report PROG & " (C_LAT " & integer'image(C_LAT) & "): " & integer'image(m1 - m0) & " line fetches, " & integer'image(s1 - s0) & " sys_clk held of about " &
           integer'image(clks) & " (" & integer'image(((s1 - s0) * 1000) / clks) & " per mille), " & integer'image(t1 - t0) & " enables ran (an undisturbed machine would run " &
           integer'image(clks * 7 / 16) & ")";
    if ((s1 - s0) * 100) / clks > LIMIT_PCT then report "FAIL tb_a2_stall: held more than " & integer'image(LIMIT_PCT) & " %"; else report "PASS tb_a2_stall"; end if;
    std.env.stop;
  end process;
end architecture;
