-- T119 S1: the VC-20 core (vc20_core with its frame writer vc20_pixw) runs a hand-written test program (gen_testrom.py: the VIC set up
-- as the KERNAL does, white background, cyan border, every screen code on the screen, a character set of non-repeating patterns
-- written through the app's ROM block window - no Commodore ROM is involved) and writes its picture to the model of the host's e_ port.
-- A reference picture is taken independently from the 6561's own outputs (colour number, hblank, vblank at every pixel enable, the
-- bench's own palette and its own rule for three VIC-20 pixels -> five frame buffer pixels); every complete frame the core reports is
-- compared pixel by pixel with it, in the buffer the core names. Also checked: the registers (FB_BASE / FB_STRIDE written through
-- the app's window and read back, FB_LATEST and FRAME_SEQ follow the reports), every command on the e_ port is a plain write
-- (cmd 0, data with the command, mask 0, 16-byte aligned, inside the four buffers), no line was late, the buffers go round, and
-- the picture is not trivial (border, background, text colours all there; the first pixel is the border's RGB565).
-- MUT_PIXW <> 0 is the mutation VC20_MUT_PIXW (the core's window starts that many pixels later): the bench must FAIL.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.vc20_testrom_pkg.all;

entity tb_vc20_pixw is
  generic (MUT_PIXW : integer := 0; FRAMES : integer := 3);
end entity;

architecture sim of tb_vc20_pixw is
  signal sys_clk, cpu_clk, mclk, ui_clk : std_logic := '0';
  signal sys_rst   : std_logic := '1';
  signal reset_req : std_logic := '1';
  signal b_req, b_we, b_rom : std_logic := '0';
  signal b_addr  : std_logic_vector(15 downto 0) := (others => '0');
  signal b_wdata : std_logic_vector(7 downto 0) := (others => '0');
  signal port_req, port_rom, port_we : std_logic;
  signal port_addr  : std_logic_vector(15 downto 0);
  signal port_wdata : std_logic_vector(7 downto 0);
  signal win_use, win_rom, win_we : std_logic;
  signal win_addr  : std_logic_vector(15 downto 0);
  signal win_wdata : std_logic_vector(7 downto 0);
  signal pix_x     : std_logic_vector(9 downto 0);
  signal pix_y     : std_logic_vector(8 downto 0);
  signal cw_we, cw_re : std_logic := '0';
  signal cw_addr   : std_logic_vector(16 downto 0) := (others => '0');
  signal cw_wdata  : std_logic_vector(7 downto 0) := (others => '0');
  signal cw_rdata  : std_logic_vector(7 downto 0);
  signal cw_ack    : std_logic;
  signal e_addr    : std_logic_vector(26 downto 0);
  signal e_cmd     : std_logic_vector(2 downto 0);
  signal e_en, e_wdf_wren : std_logic;
  signal e_rdy     : std_logic := '1';
  signal e_wdf_data : std_logic_vector(127 downto 0);
  signal e_wdf_mask : std_logic_vector(15 downto 0);
  signal done      : boolean := false;
  signal pix_tap   : std_logic_vector(6 downto 0);
  signal fb_tap    : std_logic_vector(31 downto 0);
  -- the 6561's own outputs and the writer's report, from the core's tap ports
  alias x_ce     : std_logic is pix_tap(6);
  alias x_hb     : std_logic is pix_tap(5);
  alias x_vb     : std_logic is pix_tap(4);
  alias x_col    : std_logic_vector(3 downto 0) is pix_tap(3 downto 0);
  alias x_seq    : std_logic_vector(7 downto 0) is fb_tap(15 downto 8);
  alias x_latest : std_logic_vector(7 downto 0) is fb_tap(7 downto 0);
  alias x_over   : std_logic_vector(15 downto 0) is fb_tap(31 downto 16);

  -- what the model reports to the stimulus process
  signal reports   : integer := 0;
  signal n_fail    : integer := 0;
  signal bad_cmd   : integer := 0;
  signal writes    : integer := 0;
  signal n_border, n_back, n_text : integer := 0;
  signal first_px  : std_logic_vector(15 downto 0) := (others => '0');
  signal first_buf : integer := -1;
  signal last_buf  : integer := -1;
  signal buf_ok    : boolean := true;
  signal lat_seen, seq_seen : std_logic_vector(7 downto 0) := (others => '0');

  -- the bench's own palette (the 6561's colours, RGB444) and its RGB565
  type pal_t is array (0 to 15) of integer;
  constant PAL : pal_t := (16#000#, 16#FFF#, 16#B11#, 16#5ED#, 16#C3D#, 16#4E3#, 16#33C#, 16#DE2#,
                           16#C60#, 16#EB8#, 16#E99#, 16#AFF#, 16#EAE#, 16#AFA#, 16#A9E#, 16#FFA#);
  function rgb565 (c : integer) return std_logic_vector is
    variable r, g, b : integer;
  begin
    r := c / 256; g := (c / 16) mod 16; b := c mod 16;
    -- a 4 bit channel widened by repeating its top bit(s)
    return std_logic_vector(to_unsigned((r * 2 + r / 8) * 2048 + (g * 4 + g / 4) * 32 + (b * 2 + b / 8), 16));
  end function;

  constant WIN_X0 : integer := 2;            -- the bench's window: the first frame buffer pixel is the third of the line
  constant NCOL   : integer := 376;
  constant NROW   : integer := 283;
  constant WPR    : integer := NCOL / 8;     -- words a row
  constant BUFW   : integer := 16#4000#;     -- words a buffer (stride 0x40000 bytes)
begin
  sys_clk <= not sys_clk after 15.859 ns when not done;     -- 31.5278 MHz
  cpu_clk <= not cpu_clk after 5 ns when not done;
  ui_clk  <= not ui_clk after 3.333 ns when not done;
  mclk    <= not mclk after 40.69 ns when not done;

  port_req   <= b_req or win_use;
  port_addr  <= b_addr  when b_req = '1' else win_addr;
  port_rom   <= b_rom   when b_req = '1' else win_rom;
  port_we    <= b_we    when b_req = '1' else win_we;
  port_wdata <= b_wdata when b_req = '1' else win_wdata;

  dut : entity work.vc20_core
    generic map (MUT_PIXW => MUT_PIXW)
    port map (
      sys_clk => sys_clk, sys_rst => sys_rst, cpu_clk => cpu_clk, mclk => mclk,
      reset_req => reset_req, halt_req => '0', halted => open,
      keys => (others => '0'), joy1 => "00000", joy2 => "00000", restore => '0',
      iec_atn_i => '1', iec_clk_i => '1', iec_data_i => '1',
      iec_atn_o => open, iec_clk_o => open, iec_data_o => open,
      dma_req_tgl => '0', dma_addr => x"0000", dma_we => '0', dma_wdata => x"00",
      dma_rom => '0', dma_done_tgl => open, dma_rdata => open, dbg_busy => b_req,
      win_port_use => win_use, win_port_addr => win_addr, win_port_rom => win_rom,
      win_port_we => win_we, win_port_wdata => win_wdata,
      port_req => port_req, port_addr => port_addr, port_rom => port_rom, port_we => port_we,
      port_wdata => port_wdata, ram_rdata => open,
      pix_color => open, pix_x => pix_x, pix_y => pix_y, audio_l => open, audio_r => open,
      cw_we => cw_we, cw_re => cw_re, cw_addr => cw_addr, cw_wdata => cw_wdata, cw_rdata => cw_rdata,
      cw_ack => cw_ack, cw_irq => open, px_we => open, px_re => open, px_rdata => x"00",
      px_ev_vsync => '0', px_ev_flip => '0', build => x"00000000", ddr_cal => (others => '0'),
      ui_clk => ui_clk, ui_rst => '0', pix_tap => pix_tap, fb_tap => fb_tap,
      e_addr => e_addr, e_cmd => e_cmd, e_en => e_en, e_rdy => e_rdy, e_wdf_data => e_wdf_data,
      e_wdf_mask => e_wdf_mask, e_wdf_wren => e_wdf_wren);

  -- the host's e_ port (a command is taken when e_en and e_rdy are both high; busy one clock in eight), the reference picture
  -- and the comparison at every report
  model : process
    type ddr_t is array (0 to 4 * BUFW - 1) of std_logic_vector(127 downto 0);
    variable ddr : ddr_t := (others => (others => 'U'));
    type ref_t is array (0 to NROW - 1, 0 to 255) of integer range 0 to 15;
    variable ref : ref_t := (others => (others => 0));
    variable lfsr : unsigned(15 downto 0) := x"ACE1";
    variable lnr, pxi : integer := -1;
    variable hb_prev : std_logic := '1';
    variable seq_prev : std_logic_vector(7 downto 0) := (others => '0');
    variable w : integer;
    variable bad, f, p, sel, buf, nb, nbk, nt, ncy : integer;
    variable word : std_logic_vector(127 downto 0);
    variable got, exp : std_logic_vector(15 downto 0);
    variable rep_n : integer := 0;
    variable nfail : integer := 0;
    variable shown : integer;
  begin
    wait until rising_edge(sys_clk) or rising_edge(ui_clk);
    if rising_edge(ui_clk) then
      lfsr := lfsr(14 downto 0) & (lfsr(15) xor lfsr(13) xor lfsr(12) xor lfsr(10));
      e_rdy <= '1' when lfsr(2 downto 0) /= "000" else '0';
      if e_en = '1' and e_rdy = '1' then
        if e_cmd /= "000" or e_wdf_wren /= '1' or e_wdf_mask /= x"0000" or e_addr(2 downto 0) /= "000" or to_integer(unsigned(e_addr(26 downto 3))) >= 4 * BUFW then
          bad_cmd <= bad_cmd + 1;
        else
          ddr(to_integer(unsigned(e_addr(26 downto 3)))) := e_wdf_data;
        end if;
        writes <= writes + 1;
      end if;
    end if;
    if rising_edge(sys_clk) then
      -- the reference: what the 6561 shows at the pixel enable
      if x_ce = '1' then
        if x_vb = '1' then
          lnr := -1; pxi := 0;
        else
          if hb_prev = '1' and x_hb = '0' then lnr := lnr + 1; pxi := 0; end if;
          if x_hb = '0' and lnr >= 0 and lnr < NROW and pxi < 256 then
            ref(lnr, pxi) := to_integer(unsigned(x_col));
            pxi := pxi + 1;
          end if;
        end if;
        hb_prev := x_hb;
      end if;
      -- a complete frame is reported: compare the buffer it names
      if x_seq /= seq_prev then
        seq_prev := x_seq;
        rep_n := rep_n + 1;
        buf := to_integer(unsigned(x_latest(1 downto 0)));
        bad := 0; nbk := 0; nb := 0; nt := 0; ncy := 0; shown := 0;
        for r in 0 to NROW - 1 loop
          for c in 0 to NCOL - 1 loop
            f := c + WIN_X0;
            case f mod 5 is
              when 0 | 1 => sel := 0;
              when 2     => sel := 1;
              when others => sel := 2;
            end case;
            p := 3 * (f / 5) + sel;
            exp := rgb565(PAL(ref(r, p)));
            word := ddr(buf * BUFW + r * WPR + c / 8);
            got := word(((c mod 8) + 1) * 16 - 1 downto (c mod 8) * 16);
            if got /= exp then
              if shown < 4 then
                report "     buf " & integer'image(buf) & " r " & integer'image(r) & " c " & integer'image(c) &
                       ": got " & integer'image(to_integer(unsigned(got))) & ", expected " & integer'image(to_integer(unsigned(exp)));
                shown := shown + 1;
              end if;
              bad := bad + 1;
            end if;
            if exp = rgb565(PAL(3)) then ncy := ncy + 1;
            elsif exp = rgb565(PAL(1)) then nbk := nbk + 1;
            elsif exp = rgb565(PAL(0)) then nt := nt + 1; end if;
            if r = 0 and c = 0 then first_px <= got; end if;
          end loop;
        end loop;
        report "frame " & integer'image(rep_n) & " in buffer " & integer'image(buf) & ": " & integer'image(bad) & " of " & integer'image(NROW * NCOL) &
               " pixels differ; border " & integer'image(ncy) & ", background " & integer'image(nbk) & ", text " & integer'image(nt);
        if bad /= 0 then n_fail <= n_fail + 1; end if;
        n_border <= ncy; n_back <= nbk; n_text <= nt;
        if rep_n = 1 then first_buf <= buf; else
          if buf /= (last_buf + 1) mod 4 then buf_ok <= false; end if;
        end if;
        last_buf <= buf;
        lat_seen <= x_latest; seq_seen <= x_seq;
        reports <= rep_n;
      end if;
    end if;
  end process;

  stim : process
    variable fails : integer := 0;
    variable v : std_logic_vector(7 downto 0);
    procedure chk (c : boolean; msg : string) is
    begin
      if c then report "ok   " & msg; else report "FAIL " & msg severity error; fails := fails + 1; end if;
    end procedure;
    procedure pwrite (rom : std_logic; a : integer; d : std_logic_vector(7 downto 0)) is
    begin
      wait until rising_edge(sys_clk);
      b_req <= '1'; b_we <= '1'; b_rom <= rom;
      b_addr <= std_logic_vector(to_unsigned(a, 16)); b_wdata <= d;
      wait until rising_edge(sys_clk);
      b_req <= '0'; b_we <= '0';
    end procedure;
    procedure cwwrite (a : integer; d : std_logic_vector(7 downto 0)) is
    begin
      wait until rising_edge(cpu_clk);
      cw_addr <= std_logic_vector(to_unsigned(a, 17)); cw_wdata <= d; cw_we <= '1';
      wait until rising_edge(cpu_clk);
      cw_we <= '0';
      wait until rising_edge(cpu_clk) and cw_ack = '1';
    end procedure;
    procedure cwread (a : integer; v : out std_logic_vector(7 downto 0)) is
    begin
      wait until rising_edge(cpu_clk);
      cw_addr <= std_logic_vector(to_unsigned(a, 17)); cw_re <= '1';
      wait until rising_edge(cpu_clk);
      cw_re <= '0';
      wait until rising_edge(cpu_clk) and cw_ack = '1';
      v := cw_rdata;
    end procedure;
    variable cyc : integer;
    variable stuck : boolean;
  begin
    wait for 200 ns;
    sys_rst <= '0';
    -- the frame buffer registers: the reset values, then the bench's own (buffers at 0, 0x40000 apart)
    cwread(16#0C4#, v); chk(v = x"00", "FB_BASE reads its reset value (byte 0)");
    cwread(16#0C7#, v); chk(v = x"01", "FB_BASE reads its reset value (byte 3: 0x01D00000)");
    cwread(16#0CA#, v); chk(v = x"10", "FB_STRIDE reads its reset value (byte 2: 0x00100000)");
    for i in 0 to 3 loop cwwrite(16#0C4# + i, x"00"); end loop;
    cwwrite(16#0C8#, x"00"); cwwrite(16#0C9#, x"00"); cwwrite(16#0CA#, x"04"); cwwrite(16#0CB#, x"00");
    cwread(16#0CA#, v); chk(v = x"04", "FB_STRIDE written through the app's window reads back");
    cwread(16#0C0#, v); chk(v = x"00", "FB_LATEST: nothing reported yet");
    -- the program and a character set of patterns that do not repeat
    for i in 0 to 8191 loop pwrite('1', 16#2000# + i, KERNAL(i)); end loop;
    for i in 0 to 2047 loop pwrite('1', i, std_logic_vector(to_unsigned((i * 73 + 11 + i / 8) mod 256, 8))); end loop;
    reset_req <= '0';
    -- run until the core has reported FRAMES frames (the first is the one that starts at the first vblank, the picture
    -- of the program is stable from the second)
    cyc := 0; stuck := false;
    while reports < FRAMES and not stuck loop
      wait for 1 ms;
      cyc := cyc + 1;
      if cyc > 120 then stuck := true; end if;
    end loop;
    wait for 100 us;
    chk(not stuck, "the core reports " & integer'image(FRAMES) & " frames (" & integer'image(reports) & " in " & integer'image(cyc) & " ms)");
    chk(n_fail = 0, "every pixel of every reported frame equals the 6561's picture in the buffer the core names (" & integer'image(n_fail) & " frames differ)");
    chk(first_px = rgb565(PAL(3)), "the first pixel of the buffer is the border colour (cyan, RGB565 " & integer'image(to_integer(unsigned(first_px))) & ")");
    chk(n_border > 10000 and n_back > 10000 and n_text > 5000,
        "the picture has border, background and text colours (" & integer'image(n_border) & ", " & integer'image(n_back) & ", " & integer'image(n_text) & ")");
    chk(buf_ok, "the buffers go round (first frame in buffer " & integer'image(first_buf) & ")");
    chk(bad_cmd = 0, "every command is a plain write: cmd 0, data with the command, mask 0, 16 byte aligned, inside the buffers");
    chk(writes >= (reports) * NROW * WPR, "at least " & integer'image(reports * NROW * WPR) & " words written (" & integer'image(writes) & ")");
    chk(x_over = x"0000", "no line was late");
    cwread(16#0C0#, v); chk(v(7) = '1', "FB_LATEST: valid bit set after the first frame");
    cwread(16#0C1#, v); chk(v = x_seq, "FRAME_SEQ reads the writer's counter (" & integer'image(to_integer(unsigned(v))) & ")");
    cwread(16#0C2#, v); chk(v = x"00", "FB_OVERRUNS reads 0");
    chk(pix_x /= "UUUUUUUUUU" and pix_y /= "UUUUUUUUU", "the idle raster for the scaler runs");
    if fails = 0 then report "PASS tb_vc20_pixw"; else report "FAILED tb_vc20_pixw: " & integer'image(fails) severity error; end if;
    done <= true;
    wait;
  end process;
end architecture;
