-- T89 S16: Freeze stops the machine exactly. Two machines run asm/busy.s side by side; the second is paused for 64 ms at an arbitrary sys_clk
-- (halt_req, the C64_STOP / JTAG pause of the contract) and released. While it is held its enable count does not move, the sound is exactly 0,
-- the frame writer does not complete a frame (the last picture stays), `halted` is answered, and the dummy raster of the host's scaler keeps
-- running. A reset takes the hold away (the machine runs through its power-on reset and stops again). At the same enable count the two machines'
-- trace signatures agree: they ended bit-identical (the pause of the VC-20 core: tb_vc20_pause).
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.a2_tb_pkg.all;

entity tb_a2_pause is
  generic (
    DIR    : string := ".";
    SIGN   : integer := 3000000;            -- enables to run before the signatures are compared
    PAUSE_AT : integer := 777;              -- us of machine time, an arbitrary moment
    HOLD_MS : integer := 64
  );
end entity;

architecture sim of tb_a2_pause is
  signal clkA, clkB, pclkA, pclkB, uclkA, uclkB, mclkA, mclkB : std_logic;
  signal rstA, rstB : std_logic := '1';
  signal haltB, resetB : std_logic := '0';
  signal appA, appB : app_t := APP_IDLE;
  signal ainA, ainB : app_in_t;
  signal ceA, ceB, sigA, sigB : std_logic_vector(31 downto 0);
  signal alB, arB : std_logic_vector(23 downto 0);
  signal pxB : std_logic_vector(9 downto 0);
  signal pyB : std_logic_vector(8 downto 0);
  signal haltedB, runningB : std_logic;
  signal sigsA, sigsB : std_logic_vector(31 downto 0) := (others => 'X');
  signal gotA, gotB : boolean := false;
  signal bad : integer := 0;
begin
  hA : entity work.a2_harness
    generic map (AUX_DDR => 1, COLD_CLKS => 20000)
    port map (sys_clk => clkA, cpu_clk => pclkA, ui_clk => uclkA, mclk => mclkA, sys_rst => rstA,
              dma_req_tgl => appA.dma_tgl, dma_addr => appA.dma_addr, dma_we => appA.dma_we, dma_wdata => appA.dma_wdata, dma_rom => appA.dma_rom,
              dma_done_tgl => ainA.dma_done, dma_rdata => ainA.dma_rdata,
              cw_we => appA.cw_we, cw_re => appA.cw_re, cw_addr => appA.cw_addr, cw_wdata => appA.cw_wdata, cw_rdata => ainA.cw_rdata, cw_ack => ainA.cw_ack,
              dbg_ce_cnt => ceA, dbg_sig => sigA);
  hB : entity work.a2_harness
    generic map (AUX_DDR => 1, COLD_CLKS => 20000)
    port map (sys_clk => clkB, cpu_clk => pclkB, ui_clk => uclkB, mclk => mclkB, sys_rst => rstB, halt_req => haltB, reset_req => resetB,
              halted => haltedB, running => runningB,
              dma_req_tgl => appB.dma_tgl, dma_addr => appB.dma_addr, dma_we => appB.dma_we, dma_wdata => appB.dma_wdata, dma_rom => appB.dma_rom,
              dma_done_tgl => ainB.dma_done, dma_rdata => ainB.dma_rdata,
              cw_we => appB.cw_we, cw_re => appB.cw_re, cw_addr => appB.cw_addr, cw_wdata => appB.cw_wdata, cw_rdata => ainB.cw_rdata, cw_ack => ainB.cw_ack,
              audio_l => alB, audio_r => arB, pix_x => pxB, pix_y => pyB, dbg_ce_cnt => ceB, dbg_sig => sigB);

  loadA : process
  begin
    wait for 300 ns;
    load_hex(DIR & "/busy.hex", 0, 16384, pclkA, appA, ainA);
    load_hex(DIR & "/chr.hex", 1, 8192, pclkA, appA, ainA);
    cw_wr(pclkA, appA, ainA, 16#0D4#, 0);
    rstA <= '0';
    wait;
  end process;

  capA : process (clkA)
  begin
    if rising_edge(clkA) and not gotA and unsigned(ceA) = SIGN then sigsA <= sigA; gotA <= true; end if;
  end process;
  capB : process (clkB)
  begin
    if rising_edge(clkB) and not gotB and unsigned(ceB) = SIGN then sigsB <= sigB; gotB <= true; end if;
  end process;

  ctlB : process
    variable c0, c1, d, seq0, seq1, nz : integer;
    variable x0, x1 : std_logic_vector(9 downto 0);
    variable b : integer := 0;
    procedure fail (s : in string) is begin report s; b := b + 1; end procedure;
  begin
    wait for 300 ns;
    load_hex(DIR & "/busy.hex", 0, 16384, pclkB, appB, ainB);
    load_hex(DIR & "/chr.hex", 1, 8192, pclkB, appB, ainB);
    cw_wr(pclkB, appB, ainB, 16#0D4#, 0);
    rstB <= '0';
    -- the power-on reset takes 20,000 enables (1.4 ms); then the program runs; pause PAUSE_AT us later, at an arbitrary sys_clk
    wait for 1.5 ms + PAUSE_AT * 1 us + 13 ns;
    cw_rd(pclkB, appB, ainB, 16#0C1#, seq0);
    haltB <= '1';
    wait for 100 us;                                    -- the enables stop within a few sys_clk, the frame writer finishes the line it had
    if haltedB /= '1' then fail("halted is not answered"); end if;
    c0 := to_integer(unsigned(ceB));
    x0 := pxB;
    wait for HOLD_MS * 1 ms - 100 us;
    c1 := to_integer(unsigned(ceB));
    cw_rd(pclkB, appB, ainB, 16#0C1#, seq1);
    report "held " & integer'image(HOLD_MS) & " ms: enables " & integer'image(c0) & " -> " & integer'image(c1) & ", frame sequence " & integer'image(seq0) & " -> " & integer'image(seq1);
    if c1 /= c0 then fail("the enable count moved while held"); end if;
    if seq1 > seq0 + 1 then fail("frames kept coming while held"); end if;
    nz := 0;
    for k in 0 to 30 loop
      wait for 682 * 30.556 ns;
      if to_integer(signed(alB)) /= 0 or to_integer(signed(arB)) /= 0 then nz := nz + 1; end if;
    end loop;
    if nz /= 0 then fail("sound not 0 while held: " & integer'image(nz) & " samples"); end if;
    x1 := pxB;
    if x1 = x0 then fail("the dummy raster stopped"); end if;
    haltB <= '0';
    wait until gotA and gotB for 3 sec;
    report "trace signature at " & integer'image(SIGN) & " enables: undisturbed " & to_hstring(sigsA) & ", paused " & to_hstring(sigsB);
    if not (gotA and gotB) then fail("the signature enable count was not reached"); end if;
    if sigsA /= sigsB then fail("paused machine differs from the undisturbed one"); end if;
    -- a reset while held: the machine runs through its power-on reset (20,000 enables) and is held again afterwards
    haltB <= '1';
    wait for 100 us;
    c0 := to_integer(unsigned(ceB));
    resetB <= '1'; wait for 100 ns; resetB <= '0';
    wait for 4 ms;
    d := to_integer(unsigned(ceB)) - c0;
    report "a reset while held: " & integer'image(d) & " enables ran";
    if d < 19000 or d > 21500 then fail("a reset while held did not run the reset (" & integer'image(d) & " enables)"); end if;
    c0 := to_integer(unsigned(ceB));
    wait for 2 ms;
    if to_integer(unsigned(ceB)) /= c0 then fail("the machine ran on after its reset though held"); end if;
    haltB <= '0';
    if b = 0 then report "PASS tb_a2_pause"; else report "FAIL tb_a2_pause " & integer'image(b) & " differences"; end if;
    std.env.stop;
  end process;
end architecture;
