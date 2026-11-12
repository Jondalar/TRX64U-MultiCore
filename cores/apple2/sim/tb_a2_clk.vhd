-- T89 S3: the machine runs on exact Apple clocks. The 14M enable is 7 of every 16 sys_clk (any 16 in a row), a PHI0 period is 14 enables
-- (the 65th cycle of a line 16), a line is 912 enables (65 cycles, the last one long), a frame 262 lines = 238,944 enables = 59.92 Hz.
-- No program: the timing generator counts whatever the CPU does.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.a2_tb_pkg.all;

entity tb_a2_clk is
  generic (FRAMES : integer := 2);
end entity;

architecture sim of tb_a2_clk is
  signal sclk, pclk, uclk, mclk : std_logic;
  signal sys_rst : std_logic := '1';
  signal running : std_logic;
  signal probe : std_logic_vector(23 downto 0);
  alias a_ce  is probe(0);
  alias a_pz  is probe(1);
  alias a_hbl is probe(2);
  alias a_vbl is probe(3);
  signal done : boolean := false;
begin
  h : entity work.a2_harness
    generic map (AUX_DDR => 0, COLD_CLKS => 2000)
    port map (sys_clk => sclk, cpu_clk => pclk, ui_clk => uclk, mclk => mclk, sys_rst => sys_rst, running => running, probe => probe);

  process
  begin
    wait for 1 us; sys_rst <= '0';
    wait;
  end process;

  meas : process (sclk)
    variable win : std_logic_vector(15 downto 0) := (others => '0');
    variable warm : integer := 0;
    variable ces : integer;
    variable bad_win : integer := 0;
    variable tick : integer := 0;                 -- enables since the measurement started
    variable pz_d, hbl_d, vbl_d : std_logic := '0';
    variable last_pz, last_hbl, last_vbl : integer := -1;
    variable n_pz14, n_pz16, n_pzx, n_line, bad_line, n_frame, bad_frame : integer := 0;
    variable per_line16 : integer := 0;
    variable line_pz16 : integer := 0;
    variable bad_l16 : integer := 0;
    variable p : integer;
    variable sys_clks : integer := 0;
    variable ce_total : integer := 0;
  begin
    if rising_edge(sclk) and running = '1' and not done then
      warm := warm + 1;
      win := win(14 downto 0) & a_ce;
      if warm > 4000 then
        sys_clks := sys_clks + 1;
        if a_ce = '1' then ce_total := ce_total + 1; end if;
        ces := 0;
        for i in 0 to 15 loop if win(i) = '1' then ces := ces + 1; end if; end loop;
        if ces /= 7 then bad_win := bad_win + 1; end if;
        if a_ce = '1' then
          tick := tick + 1;
          -- PHI0 period (rising edge to rising edge), in enables
          if a_pz = '1' and pz_d = '0' then
            if last_pz >= 0 and tick > 40 then
              p := tick - last_pz;
              if p = 14 then n_pz14 := n_pz14 + 1;
              elsif p = 16 then n_pz16 := n_pz16 + 1; line_pz16 := line_pz16 + 1;
              else n_pzx := n_pzx + 1; report "PHI0 period of " & integer'image(p) & " enables at enable " & integer'image(tick) & ", line " & integer'image(n_line); end if;
            end if;
            last_pz := tick;
          end if;
          pz_d := a_pz;
          if a_hbl = '0' and hbl_d = '1' then          -- the line starts (HBL falls)
            if last_hbl >= 0 then
              n_line := n_line + 1;
              if tick - last_hbl /= 912 then bad_line := bad_line + 1; end if;
              if line_pz16 /= 1 then bad_l16 := bad_l16 + 1; end if;
            end if;
            line_pz16 := 0;
            last_hbl := tick;
          end if;
          hbl_d := a_hbl;
          if a_vbl = '0' and vbl_d = '1' then          -- the frame starts (VBL falls)
            if last_vbl >= 0 then
              n_frame := n_frame + 1;
              if tick - last_vbl /= 238944 then bad_frame := bad_frame + 1;
                report "frame of " & integer'image(tick - last_vbl) & " enables"; end if;
              report "frame " & integer'image(n_frame) & ": " & integer'image(tick - last_vbl) & " enables";
              if n_frame >= FRAMES then
                report "enables in 16 sys_clk: windows not 7: " & integer'image(bad_win) & " of " & integer'image(sys_clks);
                report "sys_clk " & integer'image(sys_clks) & ", enables " & integer'image(ce_total) & " (7/16 = " & integer'image(sys_clks * 7 / 16) & ")";
                report "PHI0 periods: 14 x " & integer'image(n_pz14) & ", 16 x " & integer'image(n_pz16) & ", other " & integer'image(n_pzx);
                report "lines " & integer'image(n_line) & ", not 912: " & integer'image(bad_line) & ", lines without exactly one long cycle: " & integer'image(bad_l16);
                if bad_win = 0 and ce_total = (sys_clks * 7 + 15) / 16 and n_pzx = 0 and bad_line = 0 and bad_l16 = 0 and bad_frame = 0 and n_pz16 = n_line then
                  report "PASS tb_a2_clk 7/16 enable, PHI0 14/16, line 912, frame 238944";
                else
                  report "FAIL tb_a2_clk";
                end if;
                done <= true;
              end if;
            end if;
            last_vbl := tick;
          end if;
          vbl_d := a_vbl;
        end if;
      end if;
    end if;
  end process;

  stop : process (done)
  begin
    if done then std.env.stop; end if;
  end process;
end architecture;
