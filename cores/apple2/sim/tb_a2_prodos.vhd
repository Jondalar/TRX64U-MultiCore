-- T89.1 S13: a real ProDOS 2.4 boots from the slot 7 card on the real Enhanced //e ROM. The bench is the application: it serves blocks 0..127 of a
-- real ProDOS 2.4 volume (hex file IMG, 512 bytes a block, one byte a line; every other block reads as zeros) and polls the text page.
-- PASS when ProDOS gets past its relocator and the volume directory: no "RELOCATION" text on the screen and WZREPLAY.SYSTEM (key block 64, data blocks 65..111, the
-- second .SYSTEM file of the volume; CLOCK.SYSTEM is the first) is loaded up to its last block; FAIL on the error text or when block 111 is never asked for.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.textio.all;
use work.a2_tb_pkg.all;

entity tb_a2_prodos is
  generic (DIR : string := "."; MAXMS : integer := 900; AUX_DDR : integer := 0);
end entity;

architecture sim of tb_a2_prodos is
  signal sclk, pclk, uclk, mclk : std_logic;
  signal sys_rst : std_logic := '1';
  signal app : app_t := APP_IDLE;
  signal ain : app_in_t;
  signal keys : std_logic_vector(63 downto 0) := (others => '0');
  signal jr : std_logic_vector(31 downto 0);
  signal dbga : std_logic_vector(11 downto 0) := x"003";
begin
  h : entity work.a2_harness
    generic map (AUX_DDR => AUX_DDR, COLD_CLKS => 20000)
    port map (sys_clk => sclk, cpu_clk => pclk, ui_clk => uclk, mclk => mclk, sys_rst => sys_rst, keys => keys, dbg_addr => dbga, j_rdata => jr,
              dma_req_tgl => app.dma_tgl, dma_addr => app.dma_addr, dma_we => app.dma_we, dma_wdata => app.dma_wdata, dma_rom => app.dma_rom,
              dma_done_tgl => ain.dma_done, dma_rdata => ain.dma_rdata,
              cw_we => app.cw_we, cw_re => app.cw_re, cw_addr => app.cw_addr, cw_wdata => app.cw_wdata, cw_rdata => ain.cw_rdata, cw_ack => ain.cw_ack);

  stim : process
    type img_t is array (0 to 128 * 512 - 1) of std_logic_vector(7 downto 0);
    variable img : img_t := (others => (others => '0'));
    variable bad, nreq, maxblk, lastblk, stop_t : integer := 0;
    variable s, bl, bh, ml, mh, b, mem, d, a : integer;
    variable row_s : string(1 to 40);
    variable err, boots : boolean := false;
    variable lastrow : string(1 to 40) := (others => ' ');
    file f : text;
    variable l : line; variable v : std_logic_vector(7 downto 0); variable k : integer := 0;

    procedure serve is
    begin
      cw_rd(pclk, app, ain, 16#180#, s);
      if s mod 2 = 0 then return; end if;
      cw_rd(pclk, app, ain, 16#182#, bl); cw_rd(pclk, app, ain, 16#183#, bh);
      cw_rd(pclk, app, ain, 16#184#, ml); cw_rd(pclk, app, ain, 16#185#, mh);
      b := bl + 256 * bh; mem := ml + 256 * mh;
      nreq := nreq + 1; lastblk := b;
      if b > maxblk then maxblk := b; end if;
      report "t=" & time'image(now) & " request " & integer'image((s / 2) mod 2) & " (1 = write) block " & integer'image(b) & " mem $" & to_hstring(to_unsigned(mem, 16)) & " LC bank 1 " & integer'image((s / 8) mod 2);
      if (s / 2) mod 2 = 0 then
        for j in 0 to 511 loop
          a := (mem + j) mod 65536;
          if (s / 8) mod 2 = 1 and a >= 16#D000# and a < 16#E000# then a := a - 16#1000#; end if;   -- as coreapp/cores/apple2_hdd.cc: the CPU's LC bank 1 is the RAM $C000-$CFFF
          if b < 128 then win_wr(app, ain, '0', a, to_integer(unsigned(img(b * 512 + j)))); else win_wr(app, ain, '0', a, 0); end if;
        end loop;
      end if;
      cw_wr(pclk, app, ain, 16#186#, 0);
      cw_wr(pclk, app, ain, 16#187#, 1);
    end procedure;

    procedure dump (show : boolean) is
    begin
      for r in 0 to 23 loop
        a := 16#400# + (r mod 8) * 128 + (r / 8) * 40;
        for c in 0 to 39 loop
          win_rd(app, ain, '0', a + c, d);
          row_s(c + 1) := scr_char(d);
        end loop;
        for c in 1 to 40 - 10 loop
          if row_s(c to c + 9) = "RELOCATION" or row_s(c to c + 9) = "Relocation" then err := true; end if;
        end loop;
        if show and row_s /= "                                        " then report "|" & row_s & "|"; end if;
      end loop;
    end procedure;
  begin
    wait for 300 ns;
    file_open(f, DIR & "/img.hex", read_mode);
    while not endfile(f) and k < 128 * 512 loop
      readline(f, l); hread(l, v); img(k) := v; k := k + 1;
    end loop;
    file_close(f);
    load_hex(DIR & "/apple2e.hex", 0, 16384, pclk, app, ain);
    load_hex(DIR & "/apple2e-video.hex", 1, 8192, pclk, app, ain);
    load_hex(DIR & "/hddrom.hex", 8, 256, pclk, app, ain);
    cw_wr(pclk, app, ain, 16#0D4#, 0);
    cw_wr(pclk, app, ain, 16#18A#, 255); cw_wr(pclk, app, ain, 16#18B#, 255);
    cw_wr(pclk, app, ain, 16#188#, 1);
    sys_rst <= '0';
    for t in 1 to MAXMS loop
      for q in 1 to 10 loop wait for 100 us; serve; end loop;
      if maxblk >= 111 and stop_t = 0 then stop_t := t + 40; end if;      -- the program file is in: let it run for 40 ms, then look at the screen
      exit when stop_t /= 0 and t >= stop_t;
      if t mod 50 = 0 then
        dump(false);
        exit when err;
      end if;
    end loop;
    dump(true);
    for q in 1 to 12 loop wait for 700 us; report "cpu address $" & to_hstring(jr(15 downto 0)); end loop;
    report "requests " & integer'image(nreq) & ", highest block " & integer'image(maxblk) & ", last block " & integer'image(lastblk) & " at " & time'image(now);
    cw_wr(pclk, app, ain, 16#0D4#, 0);
    if err then report "FAIL tb_a2_prodos the screen shows ProDOS's relocation/configuration error"; bad := bad + 1; end if;
    if maxblk < 111 then report "FAIL tb_a2_prodos ProDOS never loaded WZREPLAY.SYSTEM (highest block " & integer'image(maxblk) & ", 111 is its last)"; bad := bad + 1; end if;
    if bad = 0 then report "PASS tb_a2_prodos"; end if;
    std.env.stop;
  end process;
end architecture;
