-- T89 S5: the picture. A bench program (text.s, hgr.s, lores.s: ours, no Apple ROM call) draws a screen; the bench waits for complete frames,
-- reads the newest frame buffer back out of the DDR2 model and writes it as frame.txt (560 RGB565 values of 4 hex digits on each of 384 lines,
-- the latest buffer's number first), which check_video.py judges. The synthetic character ROM and the program arrive as hex files in the
-- build directory (mkrom.py, mkchr.py). A PNG of the frame (frame.ppm) is written too, for eyes.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.textio.all;
use work.a2_tb_pkg.all;

entity tb_a2_video is
  generic (
    DIR    : string := ".";
    PROG   : string := "prog.hex";
    FRAMES : integer := 4;                 -- complete Apple frames to wait for
    SCAN   : integer := 0;
    SKIP   : integer := 8;                 -- ANIM: frames to let go by (the program fills its pages first)
    ANIM   : integer := 0;                 -- 1: watch the frames go by, every one has to be a whole picture (asm/flip.s)
    BUSY   : integer := 8
  );
end entity;

architecture sim of tb_a2_video is
  signal sclk, pclk, uclk, mclk : std_logic;
  signal sys_rst : std_logic := '1';
  signal app : app_t := APP_IDLE;
  signal ain : app_in_t;
  signal fb_a : integer := 0;
  signal fb_d : std_logic_vector(127 downto 0);
  signal ewr : integer;
begin
  h : entity work.a2_harness
    generic map (AUX_DDR => 0, COLD_CLKS => 20000, E_BUSY => BUSY)
    port map (sys_clk => sclk, cpu_clk => pclk, ui_clk => uclk, mclk => mclk, sys_rst => sys_rst,
              dma_req_tgl => app.dma_tgl, dma_addr => app.dma_addr, dma_we => app.dma_we, dma_wdata => app.dma_wdata, dma_rom => app.dma_rom,
              dma_done_tgl => ain.dma_done, dma_rdata => ain.dma_rdata,
              cw_we => app.cw_we, cw_re => app.cw_re, cw_addr => app.cw_addr, cw_wdata => app.cw_wdata, cw_rdata => ain.cw_rdata, cw_ack => ain.cw_ack,
              fb_rd_addr => fb_a, fb_rd_data => fb_d, e_writes => ewr);

  stim : process
    variable latest, seq0, seq : integer;
    file out_f : text;
    file ppm : text;
    variable l, lp : line;
    variable w : std_logic_vector(127 downto 0);
    variable px : std_logic_vector(15 downto 0);
    variable r, g, b : integer;
    variable base : integer;
    variable first, bad, whites, blacks, lastc, lastb, repeats, nfr, fbad : integer;
  begin
    wait for 200 ns;
    load_hex(DIR & "/" & PROG, 0, 16384, pclk, app, ain);
    load_hex(DIR & "/chr.hex", 1, 8192, pclk, app, ain);
    cw_wr(pclk, app, ain, 16#0A1#, SCAN * 16);
    sys_rst <= '0';
    if ANIM = 1 then
      -- the frames as they are completed: each is all white or all black (never a mix, never a half written one), the colour alternates,
      -- and the buffer of the newest frame steps through the ring of four
      lastc := -1; lastb := -1; whites := 0; blacks := 0; bad := 0; repeats := 0; nfr := 0;
      cw_rd(pclk, app, ain, 16#0C1#, seq0);
      seq := seq0;
      for q in 1 to SKIP + 10 loop
        loop
          wait for 500 us;
          cw_rd(pclk, app, ain, 16#0C1#, seq);
          exit when seq /= seq0;
        end loop;
        seq0 := seq;
        cw_rd(pclk, app, ain, 16#0C0#, latest);
        base := (latest mod 4) * (16#100000# / 16);
        wait for 6 ms;                                -- the layer starts reading late: with one buffer the writer has begun the next frame by then
        first := -1; fbad := 0;
        for row in 0 to 383 loop
          wait for 20 us;                             -- a line every 20 us (the host's layer needs 43 us; the faster reader keeps the frame count whole), across the next frame's writing
          for wd in 0 to 69 loop
            fb_a <= base + row * 70 + wd;
            wait for 1 ns;
            for k in 0 to 7 loop
              px := fb_d(16 * k + 15 downto 16 * k);
              if first < 0 then first := to_integer(unsigned(px)); end if;
              -- the last pixel of a line is the colour decode's edge (it sees the black border after it): not part of the uniform picture
              if to_integer(unsigned(px)) /= first and 8 * wd + k < 559 then
                if fbad = 0 then report "frame " & integer'image(q) & ": first odd pixel at row " & integer'image(row) & " x " & integer'image(8 * wd + k) & " = " & to_hstring(px) & ", the frame's first pixel is " & to_hstring(std_logic_vector(to_unsigned(first, 16))); end if;
                fbad := fbad + 1;
              end if;
            end loop;
          end loop;
        end loop;
        report "frame " & integer'image(q) & ": " & integer'image(fbad) & " odd pixels";
        if q > SKIP then
          nfr := nfr + 1; bad := bad + fbad;
          if first = lastc then repeats := repeats + 1; end if;
          if first = 0 then blacks := blacks + 1; elsif first = 16#F79E# then whites := whites + 1; else bad := bad + 1000; end if;
          if lastb >= 0 and (latest mod 4) /= (lastb + 1) mod 4 and (latest mod 4) /= lastb then bad := bad + 100000; end if;
        end if;
        lastc := first; lastb := latest mod 4;
      end loop;
      report "frames seen " & integer'image(nfr) & ": " & integer'image(whites) & " white, " & integer'image(blacks) & " black, same colour twice in a row " & integer'image(repeats) & ", mixed pixels " & integer'image(bad);
      if bad = 0 and whites >= 3 and blacks >= 3 then report "PASS tb_a2_video frames are whole"; else report "FAIL tb_a2_video torn or missing frames"; end if;
      std.env.stop;
    end if;
    -- wait for FRAMES complete frames: FRAME_SEQ (0C1) counts them
    cw_rd(pclk, app, ain, 16#0C1#, seq0);
    seq := seq0;
    while seq - seq0 < FRAMES loop
      wait for 1 ms;
      cw_rd(pclk, app, ain, 16#0C1#, seq);
    end loop;
    cw_rd(pclk, app, ain, 16#0C0#, latest);
    report "frame " & integer'image(seq) & ", latest buffer " & integer'image(latest mod 4) & ", e writes " & integer'image(ewr);
    base := (latest mod 4) * (16#100000# / 16);            -- the buffers are 1 MB apart (FB_STRIDE), 16 bytes a word
    file_open(out_f, DIR & "/frame.txt", write_mode);
    file_open(ppm, DIR & "/frame.ppm", write_mode);
    write(lp, string'("P3")); writeline(ppm, lp);
    write(lp, string'("560 384")); writeline(ppm, lp);
    write(lp, string'("255")); writeline(ppm, lp);
    for row in 0 to 383 loop
      write(l, to_hstring(std_logic_vector(to_unsigned(latest mod 4, 4))) & " ");
      for wd in 0 to 69 loop
        fb_a <= base + row * 70 + wd;
        wait for 1 ns;
        w := fb_d;
        for k in 0 to 7 loop
          px := w(16 * k + 15 downto 16 * k);
          write(l, to_hstring(px));
          r := to_integer(unsigned(px(15 downto 11))) * 255 / 31;
          g := to_integer(unsigned(px(10 downto 5))) * 255 / 63;
          b := to_integer(unsigned(px(4 downto 0))) * 255 / 31;
          write(lp, integer'image(r) & " " & integer'image(g) & " " & integer'image(b) & " ");
        end loop;
        if wd mod 10 = 9 then writeline(ppm, lp); end if;
      end loop;
      writeline(out_f, l);
    end loop;
    file_close(out_f);
    file_close(ppm);
    report "frame written";
    std.env.stop;
  end process;
end architecture;
