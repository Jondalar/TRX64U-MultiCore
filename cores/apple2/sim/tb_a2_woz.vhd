-- T89.1 S1-S4, S10: the Disk II as a BIT engine. asm/woz.s drives the controller like DOS 3.3 (polling $C08C, stepping the magnets, writing sync
-- and data bytes with the RWTS timing); this bench is the application: it mounts an image in drive 1, answers the QUARTER track requests of the core
-- with bit streams built here (bytes with 10-bit self sync, a weak region, an unformatted track), and checks what the program read and what it wrote.
--   S1  the bytes read are a run of the pattern's byte sequence, over more than a revolution (the track wraps at bit_count), and 3 pages take
--       768 x 32 CPU cycles (24.0 ms)
--   S3  the requests are for quarter tracks 0, 2, 4, 3, 4 (RECAL, two half steps, a quarter position, back)
--   S2  the weak region reads differently from revolution to revolution, the records around it are intact; an unformatted track is random
--   S4  the written bytes land in the buffer as bits (10-bit sync, MSB first), nothing else changes, the buffer is dirty and comes back in a write-back request
--   S10 the counters: longest request, requests, reads while the track was missing, noise cells; DISK_CFG keeps the old track playing
-- The pattern builder and the checker are written here separately from the engine: the expected bytes come from the bytes that were put in.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.textio.all;
use ieee.std_logic_textio.all;
use work.a2_tb_pkg.all;

entity tb_a2_woz is
  generic (DIR : string := ".");
end entity;

architecture sim of tb_a2_woz is
  signal sclk, pclk, uclk, mclk : std_logic;
  signal sys_rst : std_logic := '1';
  signal app : app_t := APP_IDLE;
  signal ain : app_in_t;
  signal led, hd : std_logic;
begin
  h : entity work.a2_harness
    generic map (AUX_DDR => 0, COLD_CLKS => 20000)
    port map (sys_clk => sclk, cpu_clk => pclk, ui_clk => uclk, mclk => mclk, sys_rst => sys_rst,
              dma_req_tgl => app.dma_tgl, dma_addr => app.dma_addr, dma_we => app.dma_we, dma_wdata => app.dma_wdata, dma_rom => app.dma_rom,
              dma_done_tgl => ain.dma_done, dma_rdata => ain.dma_rdata,
              cw_we => app.cw_we, cw_re => app.cw_re, cw_addr => app.cw_addr, cw_wdata => app.cw_wdata, cw_rdata => ain.cw_rdata, cw_ack => ain.cw_ack,
              drv_led => led, drv_hd => hd);

  stim : process
    type ia is array (natural range <>) of integer;
    constant ENC : ia(0 to 63) := (
      16#96#, 16#97#, 16#9A#, 16#9B#, 16#9D#, 16#9E#, 16#9F#, 16#A6#, 16#A7#, 16#AB#, 16#AC#, 16#AD#, 16#AE#, 16#AF#, 16#B2#, 16#B3#,
      16#B4#, 16#B5#, 16#B6#, 16#B7#, 16#B9#, 16#BA#, 16#BB#, 16#BC#, 16#BD#, 16#BE#, 16#BF#, 16#CB#, 16#CD#, 16#CE#, 16#CF#, 16#D3#,
      16#D6#, 16#D7#, 16#D9#, 16#DA#, 16#DB#, 16#DC#, 16#DD#, 16#DE#, 16#DF#, 16#E5#, 16#E6#, 16#E7#, 16#E9#, 16#EA#, 16#EB#, 16#EC#,
      16#ED#, 16#EE#, 16#EF#, 16#F2#, 16#F3#, 16#F4#, 16#F5#, 16#F6#, 16#F7#, 16#F9#, 16#FA#, 16#FB#, 16#FC#, 16#FD#, 16#FE#, 16#FF#);
    variable tb : ia(0 to 53247) := (others => 0);               -- a 6,656 byte track is 53,248 bits
    variable ob, sb, eb : ia(0 to 8191) := (others => 0);               -- bits: the track being built, the original of the write test, the saved track, the expected write
    variable tn, ebn : integer := 0;
    variable bseq : ia(0 to 6999) := (others => 0);
    variable blen : integer := 0;
    variable rd : ia(0 to 4095) := (others => 0);
    variable saved : ia(0 to 1023) := (others => 0);
    variable qlog : ia(0 to 63) := (others => 0);
    variable qn : integer := 0;
    variable bad : integer := 0;
    variable noise_flag : integer := 0;
    variable sv_t : time;
    variable open_t : time := 0 ns;
    variable lastq : integer := -1;
    variable v, st, q, j, k, cnt : integer;
    variable ok, got, save : boolean;
    variable t0, t1 : time;

    procedure pbits (val : in integer; n : in integer) is
    begin
      for i in n - 1 downto 0 loop
        if i < 30 then tb(tn) := (val / (2 ** i)) mod 2; else tb(tn) := 0; end if;
        tn := tn + 1;
      end loop;
    end procedure;
    procedure pbyte (val : in integer; sync : in boolean) is
    begin
      pbits(val, 8);
      if sync then pbits(0, 2); end if;
      bseq(blen) := val; blen := blen + 1;
    end procedure;
    procedure unit_normal (qq : in integer) is
    begin
      for i in 1 to 4 loop pbyte(16#FF#, true); end loop;
      pbyte(16#D5#, false); pbyte(16#AA#, false); pbyte(16#96#, false);
      for i in 0 to 7 loop pbyte(ENC((qq + i) mod 64), false); end loop;
      pbyte(16#DE#, false); pbyte(16#AA#, false); pbyte(16#EB#, false);
      for i in 1 to 3 loop pbyte(16#FF#, true); end loop;
    end procedure;
    -- a record with a weak region: sync, header, sync, 40 zero bits, sync, data record, sync
    procedure unit_weak is
    begin
      for i in 1 to 4 loop pbyte(16#FF#, true); end loop;
      pbyte(16#D5#, false); pbyte(16#AA#, false); pbyte(16#96#, false);
      for i in 0 to 3 loop pbyte(ENC(10 + i), false); end loop;
      pbyte(16#DE#, false); pbyte(16#AA#, false); pbyte(16#EB#, false);
      for i in 1 to 2 loop pbyte(16#FF#, true); end loop;
      pbits(0, 40);
      for i in 1 to 4 loop pbyte(16#FF#, true); end loop;
      pbyte(16#D5#, false); pbyte(16#AA#, false); pbyte(16#AD#, false);
      for i in 0 to 3 loop pbyte(ENC(20 + i), false); end loop;
      pbyte(16#DE#, false); pbyte(16#AA#, false); pbyte(16#EB#, false);
      for i in 1 to 2 loop pbyte(16#FF#, true); end loop;
    end procedure;

    -- S5: the track the application makes of a sector image (gen_track.cc): its bits into tb, the nibbles a shift register finds in it into bseq
    procedure load_sector_track is
      file f : text;
      variable l : line;
      variable v : std_logic_vector(7 downto 0);
      variable st : file_open_status;
      variable k : integer := 0;
    begin
      tn := 0; blen := 0;
      file_open(st, f, DIR & "/sectrack.hex", read_mode);
      while not endfile(f) loop
        readline(f, l); hread(l, v);
        for t in 7 downto 0 loop
          tb(tn) := to_integer(unsigned(v(t downto t))); tn := tn + 1;
        end loop;
      end loop;
      file_close(f);
      file_open(st, f, DIR & "/sectnib.hex", read_mode);
      while not endfile(f) loop
        readline(f, l); hread(l, v);
        bseq(blen) := to_integer(unsigned(v)); blen := blen + 1;
      end loop;
      file_close(f);
    end procedure;

    -- the application: a request of the core is answered with the track of quarter track qq
    procedure build_for (qq : in integer) is
    begin
      tn := 0; blen := 0; noise_flag := 0;
      if qq = 4 then
        noise_flag := 1; tn := 1000;
      elsif qq = 10 then
        unit_weak;
      elsif qq = 26 then
        load_sector_track;
      elsif qq = 14 then
        for i in 1 to 8 loop unit_normal(qq); end loop;
        for i in 0 to tn - 1 loop ob(i) := tb(i); end loop;
      else
        unit_normal(qq);
      end if;
    end procedure;

    procedure put_track (drive : in integer) is
      variable nb, b : integer;
    begin
      cw_wr(pclk, app, ain, 16#0D4#, 3 + drive);
      nb := (tn + 7) / 8;
      for i in 0 to nb - 1 loop
        b := 0;
        for t in 0 to 7 loop
          b := b * 2;
          if i * 8 + t < tn then b := b + tb(i * 8 + t); end if;
        end loop;
        win_wr(app, ain, '1', i, b);
      end loop;
      cw_wr(pclk, app, ain, 16#0E4# + 8 * drive, tn mod 256);
      cw_wr(pclk, app, ain, 16#0E5# + 8 * drive, tn / 256);
      cw_wr(pclk, app, ain, 16#0E6# + 8 * drive, noise_flag);
    end procedure;

    -- one poll: is a request open; if so, serve it
    procedure serve_one (got : out boolean; qq : out integer; sv : out boolean; hold : in time) is
      variable s, qv, nb, b, hd : integer;
    begin
      got := false; qq := -1; sv := false;
      cw_rd(pclk, app, ain, 16#0E0#, s);
      if s mod 2 = 1 then
        if open_t = 0 ns then open_t := now; end if;
        cw_rd(pclk, app, ain, 16#0E1#, qv);
        cw_rd(pclk, app, ain, 16#0E2#, hd);
        got := true; qq := qv; sv := (s / 2) mod 2 = 1;
        report "request: quarter track " & integer'image(qv) & ", write-back " & boolean'image(sv) & ", head at " & integer'image(hd) & " (opened " & integer'image((now - open_t) / 1 us) & " us ago)";
        if hold > 0 ns and now - open_t < hold then got := false; return; end if;
        if sv then
          cw_wr(pclk, app, ain, 16#0D4#, 3);
          for i in 0 to tn / 8 loop win_rd(app, ain, '1', i, saved(i)); end loop;
          sv_t := now - open_t;
        else
          build_for(qv);
          put_track(0);
          qlog(qn) := qv; qn := qn + 1;
        end if;
        cw_wr(pclk, app, ain, 16#0E3#, 1);
        open_t := 0 ns;
        for i in 1 to 50 loop
          wait for 20 us;
          cw_rd(pclk, app, ain, 16#0E0#, s);
          exit when s mod 2 = 0;
        end loop;
      end if;
    end procedure;

    -- serve requests until one for quarter track `want` (or a write-back with want = -2) has been served
    procedure serve_until (want : in integer; ms : in integer) is
      variable g, sv : boolean; variable qq : integer;
    begin
      for i in 1 to ms * 10 loop
        serve_one(g, qq, sv, 0 ns);
        if g and ((want = -2 and sv) or (qq = want and not sv)) then return; end if;
        wait for 100 us;
      end loop;
      report "FAIL no request for quarter track " & integer'image(want); bad := bad + 1;
    end procedure;

    procedure run_cmd (cmd : in integer; a1 : in integer; ms : in integer) is
      variable g, sv : boolean; variable qq, m : integer;
    begin
      win_wr(app, ain, '0', 16#0201#, a1);
      win_wr(app, ain, '0', 16#0202#, 0);
      win_wr(app, ain, '0', 16#0200#, cmd);
      for i in 1 to ms * 10 loop
        wait for 100 us;
        win_rd(app, ain, '0', 16#0200#, m);
        exit when m = 0;
        if i = ms * 10 then report "FAIL command " & integer'image(cmd) & " did not finish"; bad := bad + 1; end if;
      end loop;
    end procedure;

    procedure read_buf (pages : in integer) is
      variable d : integer;
    begin
      for i in 0 to pages * 256 - 1 loop win_rd(app, ain, '0', 16#2000# + i, d); rd(i) := d; end loop;
    end procedure;

    -- does rd(from .. n-1) continue the byte sequence bseq (cyclic) from some offset?
    procedure run_ok (from : in integer; n : in integer; res : out boolean) is
      variable good : boolean;
    begin
      res := false;
      for jj in 0 to blen - 1 loop
        good := true;
        for kk in from to n - 1 loop
          if rd(kk) /= bseq((jj + kk) mod blen) then good := false; exit; end if;
        end loop;
        if good then res := true; return; end if;
      end loop;
    end procedure;

    -- how many of rd(0 .. n-1) are distinct, and does D5 AA 96 occur
    procedure noise_stats (n : in integer; distinct : out integer; hdr : out boolean) is
      type seen_t is array (0 to 255) of boolean;
      variable seen : seen_t := (others => false);
    begin
      distinct := 0; hdr := false;
      for i in 0 to n - 1 loop
        if not seen(rd(i)) then seen(rd(i)) := true; distinct := distinct + 1; end if;
        if i >= 2 and rd(i - 2) = 16#D5# and rd(i - 1) = 16#AA# and rd(i) = 16#96# then hdr := true; end if;
      end loop;
    end procedure;

    procedure rd32 (a : in integer; r : out integer) is
      variable b0, b1, b2, b3 : integer;
    begin
      cw_rd(pclk, app, ain, a, b0); cw_rd(pclk, app, ain, a + 1, b1); cw_rd(pclk, app, ain, a + 2, b2); cw_rd(pclk, app, ain, a + 3, b3);
      r := b0 + 256 * b1 + 65536 * b2 + 16777216 * (b3 mod 128);
    end procedure;

    variable dist : integer;
    variable hdr : boolean;
    variable gaps : integer;
    variable gap_len : ia(0 to 63) := (others => 0);
    variable gap_val : ia(0 to 63) := (others => 0);
    variable ngap, pre_ok, post_ok, differ, bsum, hv : integer;
    variable lat1, nreq, starved, ncell, exp_us : integer;
    variable stt : integer;
    variable sstart, mism : integer;
  begin
    wait for 300 ns;
    load_hex(DIR & "/woz.hex", 0, 16384, pclk, app, ain);
    load_hex(DIR & "/chr.hex", 1, 8192, pclk, app, ain);
    cw_wr(pclk, app, ain, 16#0D4#, 0);
    cw_wr(pclk, app, ain, 16#0F0#, 1);                   -- drive 1 has an image
    cw_wr(pclk, app, ain, 16#0F1#, 1);                   -- image changed: the buffer is invalid
    sys_rst <= '0';
    -- T159 D-all: the case LED 1 source is off before the program has switched the motor on, and on once it runs (motor on: lda $C0E9 in woz.s)
    wait for 1 us;
    if led /= '0' or hd /= '0' then report "FAIL LED drv_led / drv_hd should be low before the program turns the motor on"; bad := bad + 1; end if;
    -- the head starts at quarter track 70 (the drive's reset phase): the first request
    serve_until(70, 30);
    -- RECAL: 48 half steps down, the head ends at quarter track 2 (the stepper's count: magnet 0 holds whole track 0 at 2)
    run_cmd(4, 0, 30);
    serve_until(2, 30);
    -- the program has run by now (it took the RECAL command): its motor-on switch has been made
    if led /= '1' or hd /= '0' then report "FAIL LED drv_led should be high while the Disk II motor runs (drv_hd low)"; bad := bad + 1; end if;
    report "T159 case LED 1 (drv_led) " & std_logic'image(led) & " with the motor on, " & std_logic'image(hd) & " hard disk";
    -- S1: a page of latch bytes from track 0 = 256 bytes = 11 revolutions of the 182 bit track
    t0 := now;
    run_cmd(1, 1, 100);
    t1 := now;
    read_buf(1);
    run_ok(24, 256, ok);
    report "S1 256 bytes of quarter track 2: run of the pattern " & boolean'image(ok) & ", " & integer'image((t1 - t0) / 1 us) & " us for the command";
    if not ok then
      report "FAIL S1 the bytes read are not the pattern put in";
      bad := bad + 1;
      for i in 0 to 31 loop report "  read " & integer'image(i) & ": " & integer'image(rd(i)) & "   pattern " & integer'image(bseq(i mod blen)); end loop;
    end if;
    -- the 182 bit track has 21 bytes, 7 of them self sync (10 bit cells): 8.67 cells a byte, a cell is 4 CPU cycles = 3.911 us (56 of the 14.318 MHz clock)
    exp_us := 256 * tn * 3911 / (blen * 1000);
    report "S1 expected " & integer'image(exp_us) & " us for 256 bytes of this track";
    if (t1 - t0) < exp_us * 95 / 100 * 1 us or (t1 - t0) > exp_us * 105 / 100 * 1 us then report "FAIL S1 256 bytes should take " & integer'image(exp_us) & " us (a cell is 4 CPU cycles)"; bad := bad + 1; end if;
    -- S3: two half steps up; the first lands on an unformatted track (random flux), the second on whole track 1 (quarter track 6)
    run_cmd(2, 1, 5);
    serve_until(4, 30);
    run_cmd(1, 1, 30);
    read_buf(1);
    noise_stats(256, dist, hdr);
    report "S2 unformatted track: " & integer'image(dist) & " distinct values in 256 bytes, header " & boolean'image(hdr);
    if dist < 40 or hdr then report "FAIL S2 an unformatted track must read random bytes without a header"; bad := bad + 1; end if;
    run_cmd(2, 1, 5);
    serve_until(6, 30);
    run_cmd(1, 1, 30);
    read_buf(1);
    run_ok(24, 256, ok);
    report "S3 quarter track 6: pattern " & boolean'image(ok);
    if not ok then report "FAIL S3 the track served for quarter track 6 is not what is read"; bad := bad + 1; end if;
    -- a quarter position: the previous magnet on as well (magnet 2 is on, switch 1 on): quarter track 5
    run_cmd(3, 16#81#, 5);
    serve_until(5, 30);
    run_cmd(1, 1, 30);
    read_buf(1);
    run_ok(24, 256, ok);
    report "S3 quarter track 5: pattern " & boolean'image(ok);
    if not ok then report "FAIL S3 the track served for quarter track 5 is not what is read"; bad := bad + 1; end if;
    run_cmd(3, 16#01#, 5);                                -- magnet 1 off: back to 6
    serve_until(6, 30);
    -- S2 weak bits: two half steps up to quarter track 10, a record with a run of zero bits
    run_cmd(2, 2, 5);
    serve_until(10, 30);
    run_cmd(1, 2, 30);
    read_buf(2);
    -- the header record D5 AA 96 / 4 nibbles / DE AA EB, then the bytes up to the next D5 AA AD, then the data record
    ngap := 0; pre_ok := 0; post_ok := 0;
    for i in 12 to 511 - 12 loop
      if rd(i) = 16#D5# and rd(i + 1) = 16#AA# and rd(i + 2) = 16#96# and rd(i + 3) = ENC(10) and rd(i + 6) = ENC(13) and rd(i + 7) = 16#DE# and rd(i + 9) = 16#EB# then
        pre_ok := pre_ok + 1;
        k := i + 10; cnt := 0; bsum := 0;
        while k < 500 and not (rd(k) = 16#D5# and rd(k + 1) = 16#AA# and rd(k + 2) = 16#AD#) loop
          bsum := (bsum * 31 + rd(k)) mod 1000003; cnt := cnt + 1; k := k + 1;
        end loop;
        if k < 500 and rd(k + 3) = ENC(20) and rd(k + 6) = ENC(23) and rd(k + 7) = 16#DE# and rd(k + 9) = 16#EB# then
          post_ok := post_ok + 1;
          if ngap < 64 then gap_len(ngap) := cnt; gap_val(ngap) := bsum; ngap := ngap + 1; end if;
        end if;
      end if;
    end loop;
    differ := 0;
    for a in 1 to ngap - 1 loop
      if gap_len(a) /= gap_len(0) or gap_val(a) /= gap_val(0) then differ := differ + 1; end if;
    end loop;
    report "S2 weak region: " & integer'image(pre_ok) & " header records, " & integer'image(post_ok) & " data records intact, " & integer'image(differ) & " of " & integer'image(ngap) & " gaps differ from the first";
    if post_ok < 5 then report "FAIL S2 the records around the weak region are not intact"; bad := bad + 1; end if;
    if differ < 2 then report "FAIL S2 the weak region reads the same every revolution"; bad := bad + 1; end if;
    -- S4: two half steps up to quarter track 14 (8 units, 1456 bits), the program writes 27 bytes
    run_cmd(2, 2, 5);
    serve_until(14, 30);
    cw_rd(pclk, app, ain, 16#0E0#, st);
    if (st / 4) mod 2 = 1 then report "FAIL S4 dirty before any write"; bad := bad + 1; end if;
    run_cmd(5, 0, 30);
    wait for 200 us;
    cw_rd(pclk, app, ain, 16#0E0#, st);
    report "S4 after the write: status " & integer'image(st) & " (bit 2 = dirty)";
    if (st / 4) mod 2 /= 1 then report "FAIL S4 the buffer is not dirty after a write"; bad := bad + 1; end if;
    cw_wr(pclk, app, ain, 16#0F1#, 4);                    -- flush drive 1: the app takes the track back
    serve_until(-2, 30);
    -- the expected bits: FF + 00 six times, D5 AA AD, A0..AF, FF 00, FF (two more zeros are the idle tail)
    ebn := 0;
    for i in 1 to 6 loop
      for t in 7 downto 0 loop eb(ebn) := 1; ebn := ebn + 1; end loop;
      eb(ebn) := 0; ebn := ebn + 1; eb(ebn) := 0; ebn := ebn + 1;
    end loop;
    for bv in 0 to 18 loop
      if bv = 0 then v := 16#D5#; elsif bv = 1 then v := 16#AA#; elsif bv = 2 then v := 16#AD#; else v := 16#A0# + bv - 3; end if;
      for t in 7 downto 0 loop eb(ebn) := (v / (2 ** t)) mod 2; ebn := ebn + 1; end loop;
    end loop;
    for i in 1 to 2 loop
      for t in 7 downto 0 loop eb(ebn) := 1; ebn := ebn + 1; end loop;
      if i = 1 then eb(ebn) := 0; ebn := ebn + 1; eb(ebn) := 0; ebn := ebn + 1; end if;
    end loop;
    -- the saved track as bits
    for i in 0 to tn - 1 loop sb(i) := (saved(i / 8) / (2 ** (7 - (i mod 8)))) mod 2; end loop;
    sstart := -1;
    for s in 0 to tn - 1 loop
      ok := true;
      for i in 0 to ebn - 1 loop
        if sb((s + i) mod tn) /= eb(i) then ok := false; exit; end if;
      end loop;
      if ok then sstart := s; exit; end if;
    end loop;
    report "S4 write-back request served in " & integer'image(sv_t / 1 us) & " us; the written bits (" & integer'image(ebn) & ") are in the saved track at bit " & integer'image(sstart);
    if sstart < 0 then
      report "FAIL S4 the written bytes are not in the track as the bits the program wrote"; bad := bad + 1;
    else
      mism := 0;
      -- the cell before the first byte may be a written zero (the shifter is empty from the moment Q7 is set to the first load) and up to 3 cells after
      -- the last byte are zeros (Q7 is cleared a little after the byte is out): neither is part of the span
      for i in ebn + 4 to tn - 2 loop
        if sb((sstart + i) mod tn) /= ob((sstart + i) mod tn) then mism := mism + 1; end if;
      end loop;
      report "S4 bits outside the written span that differ from the original: " & integer'image(mism);
      if mism /= 0 then report "FAIL S4 the write changed bits outside its span"; bad := bad + 1; end if;
    end if;
    cw_rd(pclk, app, ain, 16#0E0#, st);
    if st mod 2 = 1 or (st / 4) mod 2 = 1 then report "FAIL S4 the request or the dirty flag stayed after the acknowledgement"; bad := bad + 1; end if;
    -- S10: the head moves to quarter track 18 and the app holds the request open for 3 ms while the program reads a page
    run_cmd(2, 2, 5);
    for i in 1 to 100 loop
      cw_rd(pclk, app, ain, 16#0E0#, st);
      exit when st mod 2 = 1;
      wait for 50 us;
    end loop;
    open_t := now;
    run_cmd(1, 1, 60);
    read_buf(1);
    run_ok(24, 256, ok);
    report "S10 page read while the track loads (noise policy): pattern of the old track " & boolean'image(ok);
    if ok then report "FAIL S10 a page read while a track loads must not be the old track"; bad := bad + 1; end if;
    -- the request stayed open: hold it a little longer, then serve it
    for i in 1 to 30 loop wait for 100 us; end loop;
    serve_until(18, 30);
    rd32(16#160#, lat1);
    cw_rd(pclk, app, ain, 16#168#, nreq);
    cw_rd(pclk, app, ain, 16#16C#, starved);
    rd32(16#170#, ncell);
    report "S10 counters: longest request " & integer'image(lat1) & " clocks (" & integer'image(lat1 / 32727) & " ms), requests " & integer'image(nreq) & ", reads without a track " & integer'image(starved) & ", noise cells " & integer'image(ncell);
    if lat1 < 32727 * 3 or lat1 > 32727 * 40 then report "FAIL S10 the longest request should be between 3 and 40 ms"; bad := bad + 1; end if;
    if starved < 100 then report "FAIL S10 the page read while the track loaded was not counted"; bad := bad + 1; end if;
    if ncell < 1000 then report "FAIL S10 no noise cells counted"; bad := bad + 1; end if;
    -- DISK_CFG b0: while a track loads the old one goes on playing
    cw_wr(pclk, app, ain, 16#0F2#, 1);
    run_cmd(2, 2, 5);
    for i in 1 to 100 loop
      cw_rd(pclk, app, ain, 16#0E0#, st);
      exit when st mod 2 = 1;
      wait for 50 us;
    end loop;
    tn := 0; blen := 0; unit_normal(18);                  -- bseq is the track in the buffer: quarter track 18
    run_cmd(1, 1, 60);
    read_buf(1);
    run_ok(24, 256, ok);
    report "S10 page read while the track loads (keep policy): pattern of the old track " & boolean'image(ok);
    if not ok then report "FAIL S10 with DISK_CFG b0 the old track must go on playing while a new one loads"; bad := bad + 1; end if;
    serve_until(22, 30);
    -- S5: the track of a sector image as the application makes it (51,264 bits, 10-bit sync in the gaps): a page of what the program reads is a run of the
    -- nibbles that gen_track.cc found in the same bits; two half steps up to quarter track 26
    cw_wr(pclk, app, ain, 16#0F2#, 0);
    run_cmd(2, 2, 5);
    serve_until(26, 60);
    run_cmd(1, 2, 60);
    read_buf(2);
    run_ok(24, 512, ok);
    report "S5 sector track (" & integer'image(tn) & " bits, " & integer'image(blen) & " nibbles): 512 bytes are a run of its nibbles " & boolean'image(ok);
    if not ok or tn /= 51264 then report "FAIL S5 the bytes read are not the nibbles of the track the application made"; bad := bad + 1; end if;
    if bad = 0 then report "PASS tb_a2_woz"; else report "FAIL tb_a2_woz " & integer'image(bad) & " differences"; end if;
    std.env.stop;
  end process;
end architecture;
