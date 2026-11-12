-- T89 S6: the speaker. asm/spk.s toggles $C030 every 510 machine cycles; the core's audio stream (one 24 bit sample per 682 sys_clk, 47.99 kHz) is
-- captured and the frequency taken from its zero crossings: it has to be 1,000 Hz within 0.1 % (the program's 510 cycle half period gives
-- 1,000.47 Hz on the machine's 1.0205 MHz average cycle). While the machine is frozen the stream is exactly 0.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.a2_tb_pkg.all;

entity tb_a2_spk is
  generic (DIR : string := "."; LEN_MS : integer := 120);
end entity;

architecture sim of tb_a2_spk is
  signal sclk, pclk, uclk, mclk : std_logic;
  signal sys_rst : std_logic := '1';
  signal halt : std_logic := '0';
  signal app : app_t := APP_IDLE;
  signal ain : app_in_t;
  signal al, ar : std_logic_vector(23 downto 0);
begin
  h : entity work.a2_harness
    generic map (AUX_DDR => 0, COLD_CLKS => 20000)
    port map (sys_clk => sclk, cpu_clk => pclk, ui_clk => uclk, mclk => mclk, sys_rst => sys_rst, halt_req => halt,
              dma_req_tgl => app.dma_tgl, dma_addr => app.dma_addr, dma_we => app.dma_we, dma_wdata => app.dma_wdata, dma_rom => app.dma_rom,
              dma_done_tgl => ain.dma_done, dma_rdata => ain.dma_rdata,
              cw_we => app.cw_we, cw_re => app.cw_re, cw_addr => app.cw_addr, cw_wdata => app.cw_wdata, cw_rdata => ain.cw_rdata, cw_ack => ain.cw_ack,
              audio_l => al, audio_r => ar);

  stim : process
    type smp_t is array (0 to 8191) of integer;
    variable s : smp_t;
    variable n : integer := 0;
    variable cross : integer := 0;
    variable t_first, t_last, t : real;
    variable y0, y1 : integer;
    variable f, exp_f, fs : real;
    variable bad : integer := 0;
    variable nonzero : integer := 0;
    variable peak : integer := 0;
  begin
    wait for 300 ns;
    load_hex(DIR & "/spk.hex", 0, 16384, pclk, app, ain);
    load_hex(DIR & "/chr.hex", 1, 8192, pclk, app, ain);
    cw_wr(pclk, app, ain, 16#0D4#, 0);
    sys_rst <= '0';
    wait for 10 ms;
    -- 48 kHz: one sample per 682 sys_clk, taken in the middle of the window (the sample changes at the window's end)
    for k in 0 to LEN_MS * 48 loop
      wait for 682 * 30.556 ns;
      if n < 8192 then s(n) := to_integer(signed(al)); n := n + 1; end if;
    end loop;
    fs := 32727272.7 / 682.0;
    for k in 1 to n - 1 loop
      y0 := s(k - 1); y1 := s(k);
      if abs (y0) > peak then peak := abs (y0); end if;
      if y0 < 0 and y1 >= 0 then          -- a rising crossing, linearly interpolated
        t := real(k - 1) + real(-y0) / real(y1 - y0);
        if cross = 0 then t_first := t; end if;
        t_last := t;
        cross := cross + 1;
      end if;
    end loop;
    f := real(cross - 1) / ((t_last - t_first) / fs);
    exp_f := 14318181.8 / (2.0 * 510.0 * 912.0 / 65.0) / 1.0;
    exp_f := 14318181.8 / (2.0 * 510.0 * (912.0 / 65.0));
    report "samples " & integer'image(n) & ", peak " & integer'image(peak) & ", rising crossings " & integer'image(cross) &
           ", frequency " & real'image(f) & " Hz (expected from the cycle count " & real'image(exp_f) & ", nominal 1000)";
    if cross < 50 then report "too few crossings"; bad := bad + 1; end if;
    if abs (f - exp_f) / exp_f > 0.001 then report "frequency off the cycle count by more than 0.1 %"; bad := bad + 1; end if;
    if abs (f - 1000.0) / 1000.0 > 0.001 then report "frequency off 1 kHz by more than 0.1 %"; bad := bad + 1; end if;
    if peak < 1000000 then report "the tone is too quiet"; bad := bad + 1; end if;
    -- Freeze: the next samples are exactly 0
    halt <= '1';
    wait for 3 ms;
    for k in 0 to 40 loop
      wait for 682 * 30.556 ns;
      if to_integer(signed(al)) /= 0 or to_integer(signed(ar)) /= 0 then nonzero := nonzero + 1; end if;
    end loop;
    report "samples not 0 while frozen: " & integer'image(nonzero);
    if nonzero /= 0 then bad := bad + 1; end if;
    if bad = 0 then report "PASS tb_a2_spk"; else report "FAIL tb_a2_spk " & integer'image(bad) & " differences"; end if;
    std.env.stop;
  end process;
end architecture;
