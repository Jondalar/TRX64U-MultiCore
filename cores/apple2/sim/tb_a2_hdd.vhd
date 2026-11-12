-- T89.1 S7, S8: the ProDOS hard disk card in slot 7. asm/hdd.s calls the card's ProDOS entry ($C70A) like ProDOS does and boots through $C700; the
-- ROM is the card's own (rtl/roms/hdd.a65, assembled); this bench is the application: it serves the blocks of two volumes (a 32 MB one and a small one)
-- through the main RAM window, holds every request open for 1.5 ms and checks that the CPU waits (the command has not finished), the result comes back as the
-- ProDOS result in A and the carry, and block 0 boots (the card loads block 0 to $0800 and jumps to $0801, as ProDOS's boot does).
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.a2_tb_pkg.all;

entity tb_a2_hdd is
  generic (DIR : string := ".");
end entity;

architecture sim of tb_a2_hdd is
  signal sclk, pclk, uclk, mclk : std_logic;
  signal sys_rst : std_logic := '1';
  signal app : app_t := APP_IDLE;
  signal ain : app_in_t;
  signal keys : std_logic_vector(63 downto 0) := (others => '0');
  signal led, hd : std_logic;
begin
  h : entity work.a2_harness
    generic map (AUX_DDR => 0, COLD_CLKS => 20000)
    port map (sys_clk => sclk, cpu_clk => pclk, ui_clk => uclk, mclk => mclk, sys_rst => sys_rst, keys => keys,
              dma_req_tgl => app.dma_tgl, dma_addr => app.dma_addr, dma_we => app.dma_we, dma_wdata => app.dma_wdata, dma_rom => app.dma_rom,
              dma_done_tgl => ain.dma_done, dma_rdata => ain.dma_rdata,
              cw_we => app.cw_we, cw_re => app.cw_re, cw_addr => app.cw_addr, cw_wdata => app.cw_wdata, cw_rdata => ain.cw_rdata, cw_ack => ain.cw_ack,
              drv_led => led, drv_hd => hd);

  stim : process
    variable bad : integer := 0;
    variable reqs, held_ok, wr_data_ok : integer := 0;
    variable last_unit, last_write, last_blk, last_mem : integer := 0;
    variable served, wr_bad, fill_seed, led_ok : integer := 0;
    variable g : boolean;
    variable v, d, m, a, x, y, c, i, boots : integer;

    -- what the volume holds: byte i of block b of unit u (block 0 of unit 0 is the boot block)
    function blkbyte (u, b, i : integer) return integer is
    begin
      if u = 0 and b = 0 then
        case i is
          when 0 => return 1;
          when 1 => return 16#A9#;  when 2 => return 16#5A#;                   -- lda #$5A
          when 3 => return 16#8D#;  when 4 => return 16#00#;  when 5 => return 16#04#;   -- sta $0400
          when 6 => return 16#A9#;  when 7 => return 16#A5#;                   -- lda #$A5
          when 8 => return 16#8D#;  when 9 => return 16#01#;  when 10 => return 16#04#;  -- sta $0401
          when 11 => return 16#8E#; when 12 => return 16#02#; when 13 => return 16#04#;  -- stx $0402: the slot << 4
          when 14 => return 16#4C#; when 15 => return 16#0E#; when 16 => return 16#08#;  -- jmp $080E
          when others => return 0;
        end case;
      end if;
      return (b * 7 + i * 13 + u * 3 + 1) mod 256;
    end function;

    function xor8 (p, q : integer) return integer is
    begin
      return to_integer(unsigned(std_logic_vector(to_unsigned(p mod 256, 8)) xor std_logic_vector(to_unsigned(q mod 256, 8))));
    end function;
    -- what asm/hdd.s FILL leaves in the buffer: page 0 j xor seed, page 1 j xor seed xor $55
    function fillbyte (j, seed : integer) return integer is
    begin
      if j < 256 then return xor8(j, seed); end if;
      return xor8(xor8(j - 256, seed), 16#55#);
    end function;

    -- serve one request if there is one: after holding it for 1.5 ms
    procedure serve (got : out boolean) is
      variable s, bl, bh, ml, mh, u, w, b, mem, k, e : integer;
      begin
      got := false;
      cw_rd(pclk, app, ain, 16#180#, s);
      if s mod 2 = 0 then return; end if;
      got := true;
      cw_rd(pclk, app, ain, 16#182#, bl); cw_rd(pclk, app, ain, 16#183#, bh);
      cw_rd(pclk, app, ain, 16#184#, ml); cw_rd(pclk, app, ain, 16#185#, mh);
      w := (s / 2) mod 2; u := (s / 4) mod 2;
      b := bl + 256 * bh; mem := ml + 256 * mh;
      last_unit := u; last_write := w; last_blk := b; last_mem := mem;
      reqs := reqs + 1;
      -- the CPU waits: the command is still open after 1.5 ms
      wait for 1500 us;
      if led = '1' and hd = '1' then led_ok := led_ok + 1; end if;
      win_rd(app, ain, '0', 16#0200#, k);
      if k = 1 then held_ok := held_ok + 1; end if;
      if w = 1 then
        for j in 0 to 511 loop
          win_rd(app, ain, '0', (mem + j) mod 65536, k);
          if k /= fillbyte(j, fill_seed) then wr_bad := wr_bad + 1; end if;
        end loop;
        wr_data_ok := wr_data_ok + 1;
      else
        for j in 0 to 511 loop
          win_wr(app, ain, '0', (mem + j) mod 65536, blkbyte(u, b, j));
        end loop;
      end if;
      cw_wr(pclk, app, ain, 16#186#, 0);
      cw_wr(pclk, app, ain, 16#187#, 1);
      for q in 1 to 50 loop
        wait for 20 us;
        cw_rd(pclk, app, ain, 16#180#, s);
        exit when s mod 2 = 0;
      end loop;
    end procedure;

    procedure command (cmd, unit, mem, blk, bcmd : in integer; ms : in integer) is
      variable g : boolean; variable k : integer;
    begin
      win_wr(app, ain, '0', 16#0201#, unit);
      win_wr(app, ain, '0', 16#0202#, mem mod 256); win_wr(app, ain, '0', 16#0203#, mem / 256);
      win_wr(app, ain, '0', 16#0204#, blk mod 256); win_wr(app, ain, '0', 16#0205#, blk / 256);
      win_wr(app, ain, '0', 16#0206#, bcmd);
      win_wr(app, ain, '0', 16#0200#, cmd);
      for q in 1 to ms * 10 loop
        wait for 100 us;
        serve(g);
        win_rd(app, ain, '0', 16#0200#, k);
        exit when k = 0;
        if q = ms * 10 then report "FAIL command " & integer'image(cmd) & " did not finish"; bad := bad + 1; end if;
      end loop;
    end procedure;

    procedure results is
    begin
      win_rd(app, ain, '0', 16#0210#, a); win_rd(app, ain, '0', 16#0211#, x); win_rd(app, ain, '0', 16#0212#, y); win_rd(app, ain, '0', 16#0213#, c);
    end procedure;

    procedure check_ram (base, u, b : in integer; what : in string) is
      variable ok : boolean := true; variable dd : integer;
    begin
      for j in 0 to 511 loop
        win_rd(app, ain, '0', base + j, dd);
        if dd /= blkbyte(u, b, j) then ok := false; end if;
      end loop;
      if not ok then report "FAIL " & what & ": the 512 bytes in RAM are not the block"; bad := bad + 1; end if;
    end procedure;
  begin
    wait for 300 ns;
    load_hex(DIR & "/hdd.hex", 0, 16384, pclk, app, ain);
    load_hex(DIR & "/chr.hex", 1, 8192, pclk, app, ain);
    load_hex(DIR & "/hddrom.hex", 8, 256, pclk, app, ain);        -- the card's ROM into array 8 (WIN_SEL 8)
    cw_wr(pclk, app, ain, 16#0D4#, 0);
    -- two units: 65,535 blocks (32 MB) and 100 blocks
    cw_wr(pclk, app, ain, 16#18A#, 255); cw_wr(pclk, app, ain, 16#18B#, 255);
    cw_wr(pclk, app, ain, 16#18C#, 100); cw_wr(pclk, app, ain, 16#18D#, 0);
    cw_wr(pclk, app, ain, 16#188#, 3);
    sys_rst <= '0';
    wait for 2 ms;
    -- S7 STATUS: the sizes in X (low) and Y (high), A = 0, carry clear
    command(1, 16#70#, 0, 0, 0, 20);
    results;
    report "S7 STATUS unit 1: A " & integer'image(a) & " X " & integer'image(x) & " Y " & integer'image(y) & " C " & integer'image(c);
    if a /= 0 or x /= 255 or y /= 255 or c /= 0 then report "FAIL S7 STATUS of unit 1 should be 0 / 65535 blocks / carry clear"; bad := bad + 1; end if;
    command(1, 16#F0#, 0, 0, 0, 20);
    results;
    if a /= 0 or x /= 100 or y /= 0 or c /= 0 then report "FAIL S7 STATUS of unit 2 should be 100 blocks"; bad := bad + 1; end if;
    -- READ block $1234 to $2000
    reqs := 0; held_ok := 0; led_ok := 0;
    command(1, 16#70#, 16#2000#, 16#1234#, 1, 30);
    results;
    report "S7 READ: A " & integer'image(a) & " C " & integer'image(c) & ", request unit " & integer'image(last_unit) & " block " & integer'image(last_blk) & " buffer " & integer'image(last_mem) & ", held " & integer'image(held_ok);
    if a /= 0 or c /= 0 then report "FAIL S7 READ should return A = 0 with the carry clear (the CPU has to wait for the result)"; bad := bad + 1; end if;
    if reqs /= 1 or last_unit /= 0 or last_write /= 0 or last_blk /= 16#1234# or last_mem /= 16#2000# then report "FAIL S7 READ request fields"; bad := bad + 1; end if;
    if held_ok /= 1 then report "FAIL S7 the CPU did not wait for the application (the command was done before it served)"; bad := bad + 1; end if;
    if led_ok /= 1 then report "FAIL T159 drv_led and drv_hd should be high while a block request is open"; bad := bad + 1; end if;
    wait for 60 ms;
    if led /= '0' or hd /= '0' then report "FAIL T159 drv_led / drv_hd should fall 50 ms after the request ended"; bad := bad + 1; end if;
    check_ram(16#2000#, 0, 16#1234#, "S7 READ block 1234");
    -- the last block of 32 MB
    command(1, 16#70#, 16#2400#, 16#FFFF#, 1, 30);
    results;
    if a /= 0 or c /= 0 or last_blk /= 16#FFFF# then report "FAIL S7 block 65535"; bad := bad + 1; end if;
    check_ram(16#2400#, 0, 16#FFFF#, "S7 READ block FFFF");
    -- WRITE block 5 from $3000: the program fills the buffer first (page 0: j xor 3C, page 1: j xor 3C xor 55), the application reads it during the command
    fill_seed := 16#3C#;
    win_wr(app, ain, '0', 16#0202#, 0); win_wr(app, ain, '0', 16#0203#, 16#30#); win_wr(app, ain, '0', 16#0204#, 16#3C#);
    win_wr(app, ain, '0', 16#0200#, 2);
    for q in 1 to 100 loop wait for 100 us; win_rd(app, ain, '0', 16#0200#, v); exit when v = 0; end loop;
    wr_bad := 0; wr_data_ok := 0;
    command(1, 16#70#, 16#3000#, 5, 2, 30);
    results;
    report "S7 WRITE: " & integer'image(wr_data_ok) & " write served, " & integer'image(wr_bad) & " bytes of the buffer differ from the program's fill, A " & integer'image(a) & " C " & integer'image(c);
    if wr_data_ok /= 1 or wr_bad /= 0 or last_write /= 1 or last_blk /= 5 or last_mem /= 16#3000# then report "FAIL S7 WRITE: the application did not get what the program wrote"; bad := bad + 1; end if;
    if a /= 0 or c /= 0 then report "FAIL S7 WRITE result"; bad := bad + 1; end if;
    -- unit 2: its own volume and the unit flag of the request
    command(1, 16#F0#, 16#2800#, 99, 1, 30);
    results;
    if a /= 0 or c /= 0 or last_unit /= 1 then report "FAIL S7 unit 2 (bit 7 of the unit byte) was not passed on"; bad := bad + 1; end if;
    check_ram(16#2800#, 1, 99, "S7 READ unit 2 block 99");
    -- a unit that is not mounted: error 1 with the carry set, and no request reaches the application
    cw_wr(pclk, app, ain, 16#188#, 0);
    reqs := 0;
    command(1, 16#70#, 16#2000#, 7, 1, 30);
    results;
    report "S7 unmounted: A " & integer'image(a) & " C " & integer'image(c) & ", requests " & integer'image(reqs);
    if a /= 1 or c /= 1 or reqs /= 0 then report "FAIL S7 an unmounted unit should answer error 1, carry set, without a request"; bad := bad + 1; end if;
    -- a protected unit: a write is refused with $2B and no request
    cw_wr(pclk, app, ain, 16#188#, 5);
    command(1, 16#70#, 16#3000#, 7, 2, 30);
    results;
    report "S7 protected write: A " & integer'image(a) & " C " & integer'image(c) & ", requests " & integer'image(reqs);
    if a /= 16#2B# or c /= 1 or reqs /= 0 then report "FAIL S7 a write to a protected unit should answer $2B, carry set, without a request"; bad := bad + 1; end if;
    command(1, 16#70#, 16#3200#, 8, 1, 30);                -- a read of it works
    results;
    if a /= 0 or c /= 0 or reqs /= 1 then report "FAIL S7 a read of a protected unit should work"; bad := bad + 1; end if;
    cw_rd(pclk, app, ain, 16#18E#, v);
    report "S7 block commands counted by the card: " & integer'image(v);
    if v /= 5 then report "FAIL S7 the card should have counted 5 block commands"; bad := bad + 1; end if;
    -- S8 boot: unit 1 mounted, block 0 is a program of ours; the card loads it to $0800 and jumps to $0801
    cw_wr(pclk, app, ain, 16#188#, 1);
    win_rd(app, ain, '0', 16#0214#, boots);
    win_wr(app, ain, '0', 16#0400#, 0); win_wr(app, ain, '0', 16#0401#, 0); win_wr(app, ain, '0', 16#0402#, 0);
    win_wr(app, ain, '0', 16#0200#, 3);
    for q in 1 to 100 loop
      wait for 100 us;
      serve(g);
    end loop;
    win_rd(app, ain, '0', 16#0400#, a); win_rd(app, ain, '0', 16#0401#, x); win_rd(app, ain, '0', 16#0402#, y);
    report "S8 boot: block 0 ran: $0400 " & integer'image(a) & ", $0401 " & integer'image(x) & ", X at entry " & integer'image(y) & " (the slot << 4 = 112)";
    if a /= 16#5A# or x /= 16#A5# or y /= 16#70# then report "FAIL S8 the card did not boot block 0 of unit 1"; bad := bad + 1; end if;
    if last_blk /= 0 or last_mem /= 16#0800# then report "FAIL S8 the boot read should be block 0 to $0800"; bad := bad + 1; end if;
    -- Open Apple (C=, key 7*8+5) held while the card boots: the firmware looks at button 0 and goes to slot 6 although unit 1 is mounted (Alex, Q7)
    cw_wr(pclk, app, ain, 16#0A0#, 1);
    wait for 400 us;
    cw_wr(pclk, app, ain, 16#0A0#, 0);
    wait for 400 us;
    win_wr(app, ain, '0', 16#0400#, 0); win_wr(app, ain, '0', 16#0401#, 0); win_wr(app, ain, '0', 16#0402#, 0);
    win_rd(app, ain, '0', 16#0214#, boots);
    keys(61) <= '1';
    wait for 200 us;
    win_wr(app, ain, '0', 16#0200#, 3);
    for q in 1 to 100 loop
      wait for 100 us;
      serve(g);
    end loop;
    keys(61) <= '0';
    win_rd(app, ain, '0', 16#0214#, a); win_rd(app, ain, '0', 16#0400#, x);
    report "S8 Open Apple held with unit 1 mounted: resets of the program " & integer'image(boots) & " -> " & integer'image(a) & ", block 0 ran (marker) " & integer'image(x);
    if a /= boots + 1 or x /= 0 then report "FAIL S8 with Open Apple held the card should go to slot 6, not boot block 0"; bad := bad + 1; end if;
    -- unit 1 not mounted (only unit 2): the card goes on to slot 6; the empty slot 6 ROM is BRK, which resets this bench program
    cw_wr(pclk, app, ain, 16#0A0#, 1);
    wait for 400 us;
    cw_wr(pclk, app, ain, 16#0A0#, 0);
    wait for 400 us;
    cw_wr(pclk, app, ain, 16#188#, 2);
    win_rd(app, ain, '0', 16#0214#, boots);
    win_wr(app, ain, '0', 16#0200#, 3);
    for q in 1 to 100 loop wait for 100 us; end loop;
    win_rd(app, ain, '0', 16#0214#, a);
    report "S8 no unit 1: resets of the program " & integer'image(boots) & " -> " & integer'image(a);
    if a /= boots + 1 then report "FAIL S8 with unit 1 not mounted the card should go on to slot 6"; bad := bad + 1; end if;
    if bad = 0 then report "PASS tb_a2_hdd"; else report "FAIL tb_a2_hdd " & integer'image(bad) & " differences"; end if;
    std.env.stop;
  end process;
end architecture;
