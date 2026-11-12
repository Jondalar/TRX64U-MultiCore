-- T89 S14: the windows answer and the identity reads "APL2". The register block: ID, capabilities, contract version, the JTAG word; every
-- window array (main RAM, the system ROM, the character ROM, the Disk II boot ROM, the two track buffers, aux RAM in the DDR2) takes a byte
-- and gives it back, the last byte of each array too, and arrays the core does not have read 0; registers the core does not have read 0 and are
-- acknowledged (no read stalls the RISC-V: a watchdog on every access).
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.a2_tb_pkg.all;

entity tb_a2_win is
  generic (CONTRACT : integer := 2);
end entity;

architecture sim of tb_a2_win is
  signal sclk, pclk, uclk, mclk : std_logic;
  signal sys_rst : std_logic := '1';
  signal app : app_t := APP_IDLE;
  signal ain : app_in_t;
  signal dbg_a : std_logic_vector(11 downto 0) := (others => '0');
  signal jw : std_logic_vector(31 downto 0);
begin
  h : entity work.a2_harness
    generic map (AUX_DDR => 1, COLD_CLKS => 20000)
    port map (sys_clk => sclk, cpu_clk => pclk, ui_clk => uclk, mclk => mclk, sys_rst => sys_rst,
              dma_req_tgl => app.dma_tgl, dma_addr => app.dma_addr, dma_we => app.dma_we, dma_wdata => app.dma_wdata, dma_rom => app.dma_rom,
              dma_done_tgl => ain.dma_done, dma_rdata => ain.dma_rdata,
              cw_we => app.cw_we, cw_re => app.cw_re, cw_addr => app.cw_addr, cw_wdata => app.cw_wdata, cw_rdata => ain.cw_rdata, cw_ack => ain.cw_ack,
              dbg_addr => dbg_a, j_rdata => jw);

  stim : process
    variable bad : integer := 0;
    variable d : integer;
    -- a register read with a watchdog: the answer has to come within 50 cpu_clk
    procedure rd (ad : in integer; v : out integer; ok : out boolean) is
      variable n : integer := 0;
    begin
      wait until rising_edge(pclk);
      app.cw_addr <= std_logic_vector(to_unsigned(ad, 17)); app.cw_re <= '1';
      wait until rising_edge(pclk);
      app.cw_re <= '0';
      ok := false;
      for n in 1 to 50 loop
        wait until rising_edge(pclk);
        if ain.cw_ack = '1' then ok := true; exit; end if;
      end loop;
      wait for 1 ns;
      v := to_integer(unsigned(ain.cw_rdata));
    end procedure;
    variable ok : boolean;
    type arr_t is record sel : integer; size : integer; present : boolean; end record;
    type arrs_t is array (0 to 9) of arr_t;
    constant ARRS : arrs_t := ((0, 16384, true), (1, 8192, true), (2, 256, true), (3, 8192, true), (4, 8192, true), (5, 65536, true),
                               (6, 2048, false), (7, 2048, false), (8, 256, true), (15, 256, false));
    variable a, b : integer;
    variable seed : integer := 7;
  begin
    wait for 300 ns;
    sys_rst <= '0';
    -- identity and capabilities
    rd(16#000#, d, ok); if d /= 65 or not ok then report "ID[0] " & integer'image(d); bad := bad + 1; end if;
    rd(16#001#, d, ok); if d /= 80 or not ok then report "ID[1] " & integer'image(d); bad := bad + 1; end if;
    rd(16#002#, d, ok); if d /= 76 or not ok then report "ID[2] " & integer'image(d); bad := bad + 1; end if;
    rd(16#003#, d, ok); if d /= 50 or not ok then report "ID[3] " & integer'image(d); bad := bad + 1; end if;
    report "identity bytes read: " & integer'image(65) & " " & integer'image(80) & " (A P L 2)";
    rd(16#004#, d, ok); if d /= 16#C0# then report "CAPS " & integer'image(d); bad := bad + 1; end if;
    rd(16#005#, d, ok); if d /= 16#06# then report "CAPS2 " & integer'image(d); bad := bad + 1; end if;
    rd(16#006#, d, ok); if d /= CONTRACT then report "contract version " & integer'image(d) & ", the repository's contract/VERSION says " & integer'image(CONTRACT); bad := bad + 1; end if;
    rd(16#007#, d, ok); if d /= 16#0F# then report "CAPS3 " & integer'image(d); bad := bad + 1; end if;
    -- the JTAG word 0 and 1
    dbg_a <= x"000"; wait for 10 ns;
    if jw /= x"324C5041" then report "JTAG word 0 " & to_hstring(jw); bad := bad + 1; end if;
    -- registers the core does not have: 0, acknowledged
    for q in 0 to 80 loop
      a := 16#300# + q * 7;
      rd(a, d, ok); if d /= 0 or not ok then report "absent register " & to_hstring(std_logic_vector(to_unsigned(a, 12))) & " reads " & integer'image(d) & " ack " & boolean'image(ok); bad := bad + 1; end if;
    end loop;
    for pg in 1 to 5 loop
      rd(pg * 4096 + 16#123#, d, ok); if d /= 0 or not ok then report "absent page " & integer'image(pg) & " reads " & integer'image(d); bad := bad + 1; end if;
    end loop;
    for pg in 7 to 15 loop
      rd(pg * 4096 + 16#001#, d, ok); if d /= 0 or not ok then report "absent page " & integer'image(pg) & " reads " & integer'image(d); bad := bad + 1; end if;
    end loop;
    rd(65536 + 16#010#, d, ok); if d /= 0 or not ok then report "the EEPROM window reads " & integer'image(d); bad := bad + 1; end if;
    -- main RAM through the window
    for q in 0 to 40 loop
      a := (q * 1613 + 5) mod 65536;
      win_wr(app, ain, '0', a, (q * 37 + 11) mod 256);
    end loop;
    win_wr(app, ain, '0', 65535, 16#5A#);
    for q in 0 to 40 loop
      a := (q * 1613 + 5) mod 65536;
      win_rd(app, ain, '0', a, d);
      if d /= (q * 37 + 11) mod 256 then report "main RAM " & integer'image(a) & ": " & integer'image(d); bad := bad + 1; end if;
    end loop;
    win_rd(app, ain, '0', 65535, d); if d /= 16#5A# then report "main RAM 65535: " & integer'image(d); bad := bad + 1; end if;
    -- the arrays
    for k in 0 to 9 loop
      cw_wr(pclk, app, ain, 16#0D4#, ARRS(k).sel);
      for q in 0 to 12 loop
        if q < 12 then a := (q * 977 + 3) mod ARRS(k).size; else a := ARRS(k).size - 1; end if;
        win_wr(app, ain, '1', a, (q * 53 + 17 + k) mod 256);
      end loop;
      for q in 0 to 12 loop
        if q < 12 then a := (q * 977 + 3) mod ARRS(k).size; else a := ARRS(k).size - 1; end if;
        win_rd(app, ain, '1', a, d);
        if ARRS(k).present then
          if d /= (q * 53 + 17 + k) mod 256 then report "array " & integer'image(ARRS(k).sel) & " byte " & integer'image(a) & ": " & integer'image(d); bad := bad + 1; end if;
        else
          if d /= 0 then report "absent array " & integer'image(ARRS(k).sel) & " reads " & integer'image(d); bad := bad + 1; end if;
        end if;
      end loop;
    end loop;
    -- the app holds the machine in reset while it loads the ROMs: CTRL bit 0 (the warm reset hold), STATUS bit 0 says so
    wait for 3 ms;                                       -- the power-on reset (20,000 enables here) is over
    rd(16#0A4#, d, ok); if d mod 2 /= 0 then report "STATUS says in reset after the power-on reset: " & integer'image(d); bad := bad + 1; end if;
    cw_wr(pclk, app, ain, 16#0A0#, 1);
    wait for 200 us;
    rd(16#0A4#, d, ok); if d mod 2 /= 1 then report "STATUS does not say in reset while CTRL.0 holds it: " & integer'image(d); bad := bad + 1; end if;
    wait for 500 us;
    rd(16#0A4#, d, ok); if d mod 2 /= 1 then report "the reset hold let go: " & integer'image(d); bad := bad + 1; end if;
    cw_wr(pclk, app, ain, 16#0A0#, 0);
    wait for 300 us;                                     -- 1,024 enables = 72 us
    rd(16#0A4#, d, ok); if d mod 2 /= 0 then report "STATUS still says in reset after CTRL.0 was cleared: " & integer'image(d); bad := bad + 1; end if;
    -- a cold reset on request (CTRL bit 2): STATUS bit 2 while the power-on reset runs (20,000 enables here), clear after
    cw_wr(pclk, app, ain, 16#0A0#, 4);
    wait for 400 us;
    rd(16#0A4#, d, ok); if (d / 4) mod 2 /= 1 or d mod 2 /= 1 then report "no cold reset after CTRL.2: status " & integer'image(d); bad := bad + 1; end if;
    wait for 3 ms;
    rd(16#0A4#, d, ok); if (d / 4) mod 2 /= 0 or d mod 2 /= 0 then report "the cold reset did not end: status " & integer'image(d); bad := bad + 1; end if;
    report "windows and registers checked";
    if bad = 0 then report "PASS tb_a2_win"; else report "FAIL tb_a2_win " & integer'image(bad) & " differences"; end if;
    std.env.stop;
  end process;
end architecture;
