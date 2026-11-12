-- T89 S2/S4 (a local bench: it needs the Apple ROM images, which live outside the repository): the Enhanced Apple //e boots to the
-- Applesoft "]" prompt. The app's side: the ROMs go in through the DMA window (apple2e.hex 16,384 lines to array 0, apple2e-video.hex 8,192
-- to array 1), the machine is released, the text page is read back through the window. Then the app types two lines (POKE main and aux
-- RAM, PRINT PEEK) and the bench reads both RAMs back.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.textio.all;
use work.a2_tb_pkg.all;

entity tb_a2_boot is
  generic (
    ROMDIR  : string := "/sp/roms";
    AUX_DDR : integer := 0;
    DISK    : integer := 0                  -- 1: diskii.hex goes into array 2 too (slot 6 then boots from an empty drive, for ever) and the app presses CTRL-RESET
  );
end entity;

architecture sim of tb_a2_boot is
  signal sclk, pclk, uclk, mclk : std_logic;
  signal sys_rst : std_logic := '1';
  signal app : app_t := APP_IDLE;
  signal ain : app_in_t;
begin
  h : entity work.a2_harness
    generic map (AUX_DDR => AUX_DDR, COLD_CLKS => 20000)
    port map (sys_clk => sclk, cpu_clk => pclk, ui_clk => uclk, mclk => mclk, sys_rst => sys_rst,
              dma_req_tgl => app.dma_tgl, dma_addr => app.dma_addr, dma_we => app.dma_we, dma_wdata => app.dma_wdata, dma_rom => app.dma_rom,
              dma_done_tgl => ain.dma_done, dma_rdata => ain.dma_rdata,
              cw_we => app.cw_we, cw_re => app.cw_re, cw_addr => app.cw_addr, cw_wdata => app.cw_wdata, cw_rdata => ain.cw_rdata, cw_ack => ain.cw_ack);

  stim : process
    variable d, a : integer;
    variable row_s : string(1 to 40);
    variable seen_banner, seen_prompt, seen_85 : boolean := false;
  begin
    wait for 300 ns;
    load_hex(ROMDIR & "/apple2e.hex", 0, 16384, pclk, app, ain);
    load_hex(ROMDIR & "/apple2e-video.hex", 1, 8192, pclk, app, ain);
    if DISK = 1 then load_hex(ROMDIR & "/diskii.hex", 2, 256, pclk, app, ain); end if;
    cw_wr(pclk, app, ain, 16#0D4#, 0);
    win_rd(app, ain, '1', 16#3FFC#, d);
    report "ROM reset vector, low byte read back: " & integer'image(d);
    sys_rst <= '0';
    -- wait for the prompt, polling the text page every 10 ms
    for tries in 1 to 60 loop
      wait for 10 ms;
      seen_prompt := false; seen_banner := false;
      for r in 0 to 23 loop
        a := 16#400# + (r mod 8) * 128 + (r / 8) * 40;
        for c in 0 to 39 loop
          win_rd(app, ain, '0', a + c, d);
          row_s(c + 1) := scr_char(d);
        end loop;
        for c in 1 to 32 loop if row_s(c to c + 8) = "Apple //e" then seen_banner := true; end if; end loop;
        if row_s(1) = ']' then seen_prompt := true; end if;
      end loop;
      exit when seen_banner and (seen_prompt or DISK = 1);
    end loop;
    if DISK = 1 then
      report "no prompt with the Disk II ROM in slot 6 and no disk: " & boolean'image(not seen_prompt);
      if seen_prompt then report "FAIL tb_a2_boot the empty Disk II boot gave a prompt (it must spin)"; end if;
      cw_wr(pclk, app, ain, 16#0A0#, 1);                 -- CTRL-RESET, 200 ms
      wait for 200 ms;
      cw_wr(pclk, app, ain, 16#0A0#, 0);
      for tries in 1 to 60 loop
        wait for 10 ms;
        seen_prompt := false;
        for r in 0 to 23 loop
          a := 16#400# + (r mod 8) * 128 + (r / 8) * 40;
          win_rd(app, ain, '0', a, d);
          if scr_char(d) = ']' then seen_prompt := true; end if;
        end loop;
        exit when seen_prompt;
      end loop;
    end if;
    report "after " & time'image(now) & ": banner " & boolean'image(seen_banner) & ", prompt " & boolean'image(seen_prompt);
    if seen_banner and seen_prompt then report "PASS tb_a2_boot banner and prompt"; else report "FAIL tb_a2_boot banner=" & boolean'image(seen_banner) & " prompt=" & boolean'image(seen_prompt); end if;
    -- the aux byte is written with RAMWRT on (the interpreter reads its text from main), the main byte with it off
    type_line("POKE 49157,0:POKE 768,170:POKE 49156,0:POKE 768,85", pclk, app, ain);
    wait for 40 ms;
    type_line("PRINT PEEK(768)", pclk, app, ain);
    wait for 150 ms;
    win_rd(app, ain, '0', 768, d);
    report "main RAM 768 = " & integer'image(d);
    if d = 85 then report "PASS tb_a2_boot main RAM POKE"; else report "FAIL tb_a2_boot main RAM 768 = " & integer'image(d); end if;
    cw_wr(pclk, app, ain, 16#0D4#, 5);
    win_rd(app, ain, '1', 768, d);
    report "aux RAM 768 = " & integer'image(d);
    if d = 170 then report "PASS tb_a2_boot aux RAM POKE"; else report "FAIL tb_a2_boot aux RAM 768 = " & integer'image(d); end if;
    for r in 0 to 23 loop
      a := 16#400# + (r mod 8) * 128 + (r / 8) * 40;
      for c in 0 to 39 loop
        win_rd(app, ain, '0', a + c, d);
        row_s(c + 1) := scr_char(d);
      end loop;
      if row_s /= "                                        " then report "|" & row_s & "|"; end if;
      if row_s(1 to 2) = "85" then seen_85 := true; end if;
    end loop;
    if seen_85 then report "PASS tb_a2_boot PRINT PEEK(768) shows 85"; else report "FAIL tb_a2_boot PRINT PEEK(768) did not show 85"; end if;
    std.env.stop;
  end process;
end architecture;
