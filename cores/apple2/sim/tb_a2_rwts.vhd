-- T89.1 (t89-write): a Disk II sector WRITE the way DOS 3.3's RWTS does it, and the read back. asm/rwts.s is a documented RWTS (RDADR16, READ16 + POSTNIB,
-- PRENIBBLE + WRITE16 with the 40 / 32 cycle byte times, the INIT track format); this bench is the application: it serves the track a sector image gives
-- (gen_rwts.cc, apple2_nib.cc: 51,264 bits, 10-bit sync) and takes the track back when the core asks for the write-back.
--   S1  the 6502 reads a sector of the application's track and gets the bytes the image holds (the bench and the read path agree before any write)
--   S2  the program writes physical sector 5 (PRENIBBLE, address field search, WRITE16) and reads it back with RDADR16 / READ16: the 256 bytes it wrote; the
--       sector 6 behind it is unchanged (its address field is still found)
--   S3  the buffer is dirty, the write-back request carries the track; the saved bits go to saved_wr.hex for chk_rwts (the application's own decoder)
--   S4  drive 2 (an image in it, the same formatted track): the program formats the track (INIT layout: 14 sync, 16 sectors of address field, 6 sync, 343 zero
--       nibbles, 14 sync) with the same byte times and reads two sectors back: zeros; the saved bits go to saved_fmt.hex
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.textio.all;
use ieee.std_logic_textio.all;
use work.a2_tb_pkg.all;

entity tb_a2_rwts is
  generic (DIR : string := "."; D1NIB : integer := 1; RUN_S4 : integer := 1);   -- D1NIB = 1: drive 1 plays the bits of a .nib image of the sectors, 0: of a sector image
end entity;

architecture sim of tb_a2_rwts is
  signal sclk, pclk, uclk, mclk : std_logic;
  signal sys_rst : std_logic := '1';
  signal app : app_t := APP_IDLE;
  signal ain : app_in_t;
begin
  h : entity work.a2_harness
    generic map (AUX_DDR => 0, COLD_CLKS => 20000)
    port map (sys_clk => sclk, cpu_clk => pclk, ui_clk => uclk, mclk => mclk, sys_rst => sys_rst,
              dma_req_tgl => app.dma_tgl, dma_addr => app.dma_addr, dma_we => app.dma_we, dma_wdata => app.dma_wdata, dma_rom => app.dma_rom,
              dma_done_tgl => ain.dma_done, dma_rdata => ain.dma_rdata,
              cw_we => app.cw_we, cw_re => app.cw_re, cw_addr => app.cw_addr, cw_wdata => app.cw_wdata, cw_rdata => ain.cw_rdata, cw_ack => ain.cw_ack);

  stim : process
    type ia is array (natural range <>) of integer;
    constant NBITS : integer := 51264;
    constant NBYTES : integer := 6408;
    variable n1bits : integer := NBITS;                      -- bits / bytes of the track drive 1 plays
    variable n1bytes : integer := NBYTES;
    variable trkb : ia(0 to 8191) := (others => 0);          -- the sector image's track the application serves (bytes, MSB first)
    variable trk1 : ia(0 to 8191) := (others => 0);          -- the track of drive 1
    variable expd : ia(0 to 4095) := (others => 0);          -- the 16 physical sectors of it
    variable saved : ia(0 to 8191) := (others => 0);
    variable bad : integer := 0;
    variable nreq : integer := 0;
    variable nsave : integer := 0;                           -- write-backs served
    variable sav_q : integer := -1;                          -- the quarter track of the last write-back: served again from `saved` (the application's file)
    variable v, st, res : integer;
    variable ok : boolean;
    variable t0, t1 : time;

    function pat (i : integer) return integer is
    begin
      return (i * 11 + 90) mod 256;
    end function;

    procedure load_files is
      file f : text;
      variable l : line;
      variable b : std_logic_vector(7 downto 0);
      variable st : file_open_status;
      variable k : integer := 0;
    begin
      file_open(st, f, DIR & "/sectrack.hex", read_mode);
      while not endfile(f) loop readline(f, l); hread(l, b); trkb(k) := to_integer(unsigned(b)); k := k + 1; end loop;
      file_close(f);
      trk1 := trkb;
      if D1NIB = 1 then
        k := 0;
        file_open(st, f, DIR & "/nibtrack.hex", read_mode);
        while not endfile(f) loop readline(f, l); hread(l, b); trk1(k) := to_integer(unsigned(b)); k := k + 1; end loop;
        file_close(f);
        n1bytes := k;
        file_open(st, f, DIR & "/nibbits.txt", read_mode);
        readline(f, l); read(l, n1bits);
        file_close(f);
      end if;
      k := 0;
      file_open(st, f, DIR & "/sectexp.hex", read_mode);
      while not endfile(f) loop readline(f, l); hread(l, b); expd(k) := to_integer(unsigned(b)); k := k + 1; end loop;
      file_close(f);
    end procedure;

    procedure dump_saved (name : in string; nbytes : in integer) is
      file f : text;
      variable l : line;
      variable st : file_open_status;
    begin
      file_open(st, f, DIR & "/" & name, write_mode);
      for i in 0 to nbytes - 1 loop
        write(l, to_hstring(std_logic_vector(to_unsigned(saved(i), 8))));
        writeline(f, l);
      end loop;
      file_close(f);
    end procedure;

    procedure put_track (drive : in integer) is
      variable nby, nbi : integer;
    begin
      if drive = 0 then nby := n1bytes; nbi := n1bits; else nby := NBYTES; nbi := NBITS; end if;
      cw_wr(pclk, app, ain, 16#0D4#, 3 + drive);
      for i in 0 to nby - 1 loop
        if drive = 0 then win_wr(app, ain, '1', i, trk1(i)); else win_wr(app, ain, '1', i, trkb(i)); end if;
      end loop;
      cw_wr(pclk, app, ain, 16#0E4# + 8 * drive, nbi mod 256);
      cw_wr(pclk, app, ain, 16#0E5# + 8 * drive, nbi / 256);
      cw_wr(pclk, app, ain, 16#0E6# + 8 * drive, 0);
    end procedure;

    -- one poll of a drive: a request is served (the same track for every quarter track; a write-back goes into `saved`)
    procedure serve_one (drive : in integer; got : out boolean) is
      variable s, qv, hd : integer;
      variable sv : boolean;
    begin
      got := false;
      cw_rd(pclk, app, ain, 16#0E0# + 8 * drive, s);
      if s mod 2 = 1 then
        cw_rd(pclk, app, ain, 16#0E1# + 8 * drive, qv);
        cw_rd(pclk, app, ain, 16#0E2# + 8 * drive, hd);
        sv := (s / 2) mod 2 = 1;
        got := true;
        report "request D" & integer'image(drive + 1) & ": quarter track " & integer'image(qv) & ", write-back " & boolean'image(sv) & ", head at " & integer'image(hd);
        if sv then
          cw_wr(pclk, app, ain, 16#0D4#, 3 + drive);
          for i in 0 to (n1bytes * (1 - drive) + NBYTES * drive) - 1 loop win_rd(app, ain, '1', i, saved(i)); end loop;
          if drive = 0 then sav_q := qv; end if;
          nsave := nsave + 1;
        elsif drive = 0 and qv = sav_q then
          cw_wr(pclk, app, ain, 16#0D4#, 3);
          for i in 0 to n1bytes - 1 loop win_wr(app, ain, '1', i, saved(i)); end loop;
          cw_wr(pclk, app, ain, 16#0E4#, n1bits mod 256);
          cw_wr(pclk, app, ain, 16#0E5#, n1bits / 256);
          cw_wr(pclk, app, ain, 16#0E6#, 0);
          nreq := nreq + 1;
        else
          put_track(drive);
          nreq := nreq + 1;
        end if;
        cw_wr(pclk, app, ain, 16#0E3# + 8 * drive, 1);
        for i in 1 to 50 loop
          wait for 20 us;
          cw_rd(pclk, app, ain, 16#0E0# + 8 * drive, s);
          exit when s mod 2 = 0;
        end loop;
      end if;
    end procedure;

    -- serve until a request of `drive` has been served (want_save: it has to be a write-back)
    procedure serve_until (drive : in integer; want_save : in boolean; ms : in integer) is
      variable s : integer; variable g : boolean;
    begin
      for i in 1 to ms * 10 loop
        cw_rd(pclk, app, ain, 16#0E0# + 8 * drive, s);
        if s mod 2 = 1 and ((s / 2) mod 2 = 1) = want_save then serve_one(drive, g); return; end if;
        if s mod 2 = 1 then serve_one(drive, g); end if;
        wait for 100 us;
      end loop;
      report "FAIL no request on drive " & integer'image(drive + 1) & " (write-back " & boolean'image(want_save) & ")"; bad := bad + 1;
    end procedure;

    -- a command of the program; the requests of the selected drive are served while it runs
    procedure run_cmd (cmd : in integer; a1, a2 : in integer; drive : in integer; ms : in integer) is
      variable m : integer; variable g : boolean;
    begin
      win_wr(app, ain, '0', 16#0201#, a1);
      win_wr(app, ain, '0', 16#0202#, a2);
      win_wr(app, ain, '0', 16#0200#, cmd);
      for i in 1 to ms * 10 loop
        wait for 100 us;
        win_rd(app, ain, '0', 16#0200#, m);
        exit when m = 0;
        if i mod 10 = 0 then serve_one(drive, g); end if;
        if i = ms * 10 then report "FAIL command " & integer'image(cmd) & " did not finish"; bad := bad + 1; end if;
      end loop;
      win_rd(app, ain, '0', 16#0203#, res);
    end procedure;

    -- READSEC and compare with the bytes `want(base + i)` (kind 0: the image, 1: the pattern written, 2: zeros)
    procedure read_check (sec : in integer; kind : in integer; drive : in integer; name : in string) is
      variable d, e, mism, first : integer;
    begin
      run_cmd(1, sec, 0, drive, 600);
      mism := 0; first := -1;
      for i in 0 to 255 loop
        win_rd(app, ain, '0', 16#2800# + i, d);
        if kind = 0 then e := expd(sec * 256 + i); elsif kind = 1 then e := pat(i); else e := 0; end if;
        if d /= e then mism := mism + 1; if first < 0 then first := i; end if; end if;
      end loop;
      report name & " sector " & integer'image(sec) & ": result " & integer'image(res) & ", " & integer'image(mism) & " of 256 bytes differ, first at " & integer'image(first);
      if res /= 0 or mism /= 0 then report "FAIL " & name & " sector " & integer'image(sec) & " does not read back"; bad := bad + 1; end if;
    end procedure;
  begin
    wait for 300 ns;
    load_files;
    load_hex(DIR & "/rwts.hex", 0, 16384, pclk, app, ain);
    load_hex(DIR & "/chr.hex", 1, 8192, pclk, app, ain);
    cw_wr(pclk, app, ain, 16#0D4#, 0);
    cw_wr(pclk, app, ain, 16#0F0#, 1);                   -- drive 1 has an image
    cw_wr(pclk, app, ain, 16#0F1#, 1);                   -- image changed: the buffer is invalid
    sys_rst <= '0';
    serve_until(0, false, 30);                           -- the head starts at quarter track 70
    run_cmd(3, 0, 0, 0, 30);                             -- RECAL
    serve_until(0, false, 30);
    -- S1
    read_check(5, 0, 0, "S1");
    -- S2
    for i in 0 to 255 loop win_wr(app, ain, '0', 16#2000# + i, pat(i)); end loop;
    t0 := now;
    run_cmd(2, 5, 0, 0, 600);
    t1 := now;
    report "S2 WRITESEC sector 5: result " & integer'image(res) & ", " & integer'image((t1 - t0) / 1 us) & " us";
    if res /= 0 then report "FAIL S2 the write returned " & integer'image(res); bad := bad + 1; end if;
    read_check(5, 1, 0, "S2");
    read_check(6, 0, 0, "S2");
    -- S3
    cw_rd(pclk, app, ain, 16#0E0#, st);
    if (st / 4) mod 2 /= 1 then report "FAIL S3 the buffer is not dirty after the write"; bad := bad + 1; end if;
    cw_wr(pclk, app, ain, 16#0F1#, 4);                    -- flush drive 1
    serve_until(0, true, 60);
    dump_saved("saved_wr.hex", n1bytes);
    -- S5: write sector 7, the head steps one track up (the core asks for the write-back by itself), comes back, the application serves its copy
    run_cmd(2, 7, 0, 0, 600);
    if res /= 0 then report "FAIL S5 the write returned " & integer'image(res); bad := bad + 1; end if;
    read_check(7, 1, 0, "S5 before the step");
    t0 := now;
    v := nsave;
    run_cmd(7, 0, 2, 0, 100);                             -- STEP up 2 half steps = one track
    for i in 1 to 60 loop serve_one(0, ok); wait for 100 us; end loop;
    if nsave /= v + 1 then report "FAIL S5 the step away asked for " & integer'image(nsave - v) & " write-backs, not 1"; bad := bad + 1; end if;
    run_cmd(7, 128, 2, 0, 100);                           -- and down again
    for i in 1 to 60 loop serve_one(0, ok); wait for 100 us; end loop;
    read_check(7, 1, 0, "S5 after the step");
    read_check(5, 1, 0, "S5 after the step");
    read_check(6, 0, 0, "S5 after the step");
    -- S4: drive 2
    if RUN_S4 = 1 then
    cw_wr(pclk, app, ain, 16#0F0#, 3);                    -- both drives have an image
    cw_wr(pclk, app, ain, 16#0F1#, 2);                    -- drive 2: image changed
    serve_until(1, false, 30);
    run_cmd(5, 1, 0, 1, 10);                              -- SELECT drive 2
    run_cmd(3, 0, 0, 1, 30);                              -- RECAL
    serve_until(1, false, 30);
    t0 := now;
    run_cmd(4, 0, 0, 1, 600);                             -- FORMAT track 0
    t1 := now;
    report "S4 FORMAT: result " & integer'image(res) & ", " & integer'image((t1 - t0) / 1 us) & " us";
    if res /= 0 then report "FAIL S4 the format returned " & integer'image(res); bad := bad + 1; end if;
    read_check(0, 2, 1, "S4");
    read_check(15, 2, 1, "S4");
    cw_wr(pclk, app, ain, 16#0F1#, 8);                    -- flush drive 2
    serve_until(1, true, 60);
    dump_saved("saved_fmt.hex", NBYTES);
    end if;
    if bad = 0 then report "PASS tb_a2_rwts"; else report "FAIL tb_a2_rwts " & integer'image(bad) & " differences"; end if;
    std.env.stop;
  end process;
end architecture;
