-- T83 bench: the VC-20 core (cores/vc20/rtl) boots a hand-written test program (gen_testrom.py,
-- no Commodore ROM) and the checks below say whether it lives:
--   1  the ID register reads "VC20" (the app's window, cpu_clk)
--   2  the host's ROM block window writes KERNAL and CHAR (port_rom = 1, as the app's apertures)
--   3  the 6502 runs: screen RAM $1E00.. holds the codes the program wrote (read back through the
--      app's DMA window, the toggle handshake, as REST readmem does)
--   4  the idle raster for the host's scaler: pix_y wraps every 628,992 sys_clk (one VIC-II frame, 312 x 2016), also across
--      a machine reset in the middle of the run (it is not tied to the 6561)
--   5  (the picture itself is in the frame buffer since T119: tb_vc20_pixw)
--   6  the keyboard map: the C64 key 'A' lands at VIC-20 scan line 2, read line 1
--   7  the IEC lines are defined, ATN released
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.vc20_testrom_pkg.all;

entity tb_vc20 is
end entity;

architecture sim of tb_vc20 is
  signal sys_clk, cpu_clk, mclk : std_logic := '0';
  signal sys_rst   : std_logic := '1';
  signal reset_req : std_logic := '1';
  signal keys      : std_logic_vector(63 downto 0) := (others => '0');
  -- host port
  signal b_req, b_we, b_rom : std_logic := '0';
  signal b_addr  : std_logic_vector(15 downto 0) := (others => '0');
  signal b_wdata : std_logic_vector(7 downto 0) := (others => '0');
  signal port_req, port_rom, port_we : std_logic;
  signal port_addr  : std_logic_vector(15 downto 0);
  signal port_wdata : std_logic_vector(7 downto 0);
  signal ram_rdata  : std_logic_vector(7 downto 0);
  signal win_use, win_rom, win_we : std_logic;
  signal win_addr  : std_logic_vector(15 downto 0);
  signal win_wdata : std_logic_vector(7 downto 0);
  signal dma_req_tgl : std_logic := '0';
  signal dma_done_tgl : std_logic;
  signal dma_addr  : std_logic_vector(15 downto 0) := (others => '0');
  signal dma_rdata : std_logic_vector(7 downto 0);
  signal pix_color : std_logic_vector(3 downto 0);
  signal pix_x     : std_logic_vector(9 downto 0);
  signal pix_y     : std_logic_vector(8 downto 0);
  signal cw_re     : std_logic := '0';
  signal cw_addr   : std_logic_vector(16 downto 0) := (others => '0');
  signal cw_rdata  : std_logic_vector(7 downto 0);
  signal cw_ack    : std_logic;
  signal iec_atn_o, iec_clk_o, iec_data_o : std_logic;
  signal audio_l, audio_r : std_logic_vector(23 downto 0);
  signal halted : std_logic;
  signal kb_out, kb_in : std_logic_vector(7 downto 0) := (others => '1');
  signal done : boolean := false;
begin
  sys_clk <= not sys_clk after 15.859 ns when not done;     -- 31.5278 MHz
  cpu_clk <= not cpu_clk after 5 ns when not done;
  mclk    <= not mclk after 40.69 ns when not done;

  -- the host: the DMA window's request wins nothing here, the bench's own port use comes first
  port_req   <= b_req or win_use;
  port_addr  <= b_addr  when b_req = '1' else win_addr;
  port_rom   <= b_rom   when b_req = '1' else win_rom;
  port_we    <= b_we    when b_req = '1' else win_we;
  port_wdata <= b_wdata when b_req = '1' else win_wdata;

  dut : entity work.vc20_core
    port map (
      sys_clk => sys_clk, sys_rst => sys_rst, cpu_clk => cpu_clk, mclk => mclk,
      reset_req => reset_req, halt_req => '0', halted => halted,
      keys => keys, joy1 => "00000", joy2 => "00000", restore => '0',
      iec_atn_i => '1', iec_clk_i => '1', iec_data_i => '1',
      iec_atn_o => iec_atn_o, iec_clk_o => iec_clk_o, iec_data_o => iec_data_o,
      dma_req_tgl => dma_req_tgl, dma_addr => dma_addr, dma_we => '0', dma_wdata => x"00",
      dma_rom => '0', dma_done_tgl => dma_done_tgl, dma_rdata => dma_rdata, dbg_busy => b_req,
      win_port_use => win_use, win_port_addr => win_addr, win_port_rom => win_rom,
      win_port_we => win_we, win_port_wdata => win_wdata,
      port_req => port_req, port_addr => port_addr, port_rom => port_rom, port_we => port_we,
      port_wdata => port_wdata, ram_rdata => ram_rdata,
      pix_color => pix_color, pix_x => pix_x, pix_y => pix_y, audio_l => audio_l, audio_r => audio_r,
      cw_we => '0', cw_re => cw_re, cw_addr => cw_addr, cw_wdata => x"00", cw_rdata => cw_rdata,
      cw_ack => cw_ack, cw_irq => open, px_we => open, px_re => open, px_rdata => x"00",
      px_ev_vsync => '0', px_ev_flip => '0', build => x"12345678", ddr_cal => (others => '0'));

  -- the keyboard map on its own
  km : entity work.vc20_keys port map (keys => keys, col_out => kb_out, row_in => kb_in);

  stim : process
    variable fails : integer := 0;
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
    procedure cwread (a : integer; v : out std_logic_vector(7 downto 0)) is
    begin
      wait until rising_edge(cpu_clk);
      cw_addr <= std_logic_vector(to_unsigned(a, 17)); cw_re <= '1';
      wait until rising_edge(cpu_clk);
      cw_re <= '0';
      wait until rising_edge(cpu_clk) and cw_ack = '1';
      v := cw_rdata;
    end procedure;
    procedure dmaread (a : integer; v : out std_logic_vector(7 downto 0)) is
      variable t : std_logic;
    begin
      t := dma_done_tgl;
      wait until rising_edge(sys_clk);
      dma_addr <= std_logic_vector(to_unsigned(a, 16));
      dma_req_tgl <= not dma_req_tgl;
      wait until rising_edge(sys_clk) and dma_done_tgl /= t;
      v := dma_rdata;
    end procedure;
    variable v : std_logic_vector(7 downto 0);
    variable last_wrap, period : integer := 0;
    variable cyc : integer := 0;
    variable wraps : integer := 0;
    variable bad_period : integer := 0;
    variable y20_prev, y20_t, y20_bad : integer := 0;
    variable y20_n : integer := 0;
    variable prev_y : std_logic_vector(8 downto 0) := (others => '0');
  begin
    -- 1 identity
    wait for 200 ns;
    sys_rst <= '0';
    cwread(0, v); chk(v = x"56", "ID byte 0 = V");
    cwread(1, v); chk(v = x"43", "ID byte 1 = C");
    cwread(2, v); chk(v = x"32", "ID byte 2 = 2");
    cwread(3, v); chk(v = x"30", "ID byte 3 = 0");
    cwread(8, v); chk(v = x"78", "BUILD byte 0");
    -- 2 ROMs through the app's ROM block window
    for i in 0 to 8191 loop pwrite('1', 16#2000# + i, KERNAL(i)); end loop;
    for i in 0 to 2047 loop pwrite('1', i, x"AA"); end loop;
    -- the colour RAM aperture (ROM block $6000) and the plain map: write and read back
    pwrite('1', 16#6000# + 5, x"0C"); pwrite('0', 16#0200#, x"5A");
    wait until rising_edge(sys_clk);
    b_req <= '1'; b_we <= '0'; b_rom <= '0'; b_addr <= x"0200";
    wait until rising_edge(sys_clk); b_req <= '0';
    wait until rising_edge(sys_clk);
    chk(ram_rdata = x"5A", "RAM $0200 reads back through the host port");
    wait until rising_edge(sys_clk);
    b_req <= '1'; b_we <= '0'; b_rom <= '1'; b_addr <= std_logic_vector(to_unsigned(16#6000# + 5, 16));
    wait until rising_edge(sys_clk); b_req <= '0';
    wait until rising_edge(sys_clk);
    chk(ram_rdata = x"0C", "colour RAM aperture reads back");
    -- 6 keyboard map: C64 'A' (index 10) sits at VIC-20 scan line 2 (port B), read line 1 (port A):
    -- the boot bench with a real KERNAL showed that (scan 1, read 2) is "R"
    keys(10) <= '1'; kb_out <= "11111011";
    wait for 1 ns;
    chk(kb_in = "11111101", "key A: scan line 2 selected, read line 1 answers");
    kb_out <= "11111101"; wait for 1 ns;
    chk(kb_in = "11111111", "key A: scan line 1 selected, no answer");
    keys(10) <= '0';
    keys(0) <= '1'; kb_out <= "11111110"; wait for 1 ns;     -- C64 DEL (col 0, row 0): scan line 0, read line 7
    chk(kb_in = "01111111", "key DEL: scan line 0 selected, read line 7 answers");
    keys(0) <= '0';
    -- run
    reset_req <= '0';
    -- 3 frames: watch the pixel stream for 3.3 frames
    cyc := 0;
    while cyc < 3 * 628992 + 300000 loop
      wait until rising_edge(sys_clk);
      cyc := cyc + 1;
      -- a machine reset in the middle of the run: the 6561 has no reset pin, the frame must not move
      if cyc = 900000 then reset_req <= '1'; end if;
      if cyc = 903000 then reset_req <= '0'; end if;
      if prev_y = std_logic_vector(to_unsigned(311, 9)) and pix_y = std_logic_vector(to_unsigned(0, 9)) then
        wraps := wraps + 1;
        if wraps >= 2 then period := cyc - last_wrap; if period /= 628992 then bad_period := bad_period + 1; end if; end if;
        last_wrap := cyc;
      end if;
      -- the scaler locks to the moment y reaches 20 (its ALIGN): every one of them, the first included, one frame apart
      if pix_y = std_logic_vector(to_unsigned(20, 9)) and prev_y /= std_logic_vector(to_unsigned(20, 9)) then
        if y20_n > 0 and cyc - y20_t /= 628992 then y20_bad := y20_bad + 1; end if;
        y20_n := y20_n + 1; y20_t := cyc;
      end if;
      prev_y := pix_y;
    end loop;
    chk(wraps >= 3, "frames come out (pix_y wraps: " & integer'image(wraps) & ")");
    chk(y20_bad = 0 and y20_n >= 3, "y reaches the scaler's lock line every frame, the first included (" & integer'image(y20_n) & " times, " & integer'image(y20_bad) & " off)");
    chk(bad_period = 0, "no frame moved across a machine reset (" & integer'image(bad_period) & " wrong periods)");
    chk(period = 628992, "frame period " & integer'image(period) & " sys_clk (VIC-II frame 628992)");
    -- 3 the program's writes in screen RAM, read through the app's DMA window
    dmaread(16#1E00# + 1, v);   chk(v = x"01", "screen RAM $1E01 = $01 (got " & integer'image(to_integer(unsigned(v))) & ")");
    dmaread(16#1E00# + 200, v); chk(v = x"C8", "screen RAM $1EC8 = $C8 (got " & integer'image(to_integer(unsigned(v))) & ")");
    dmaread(16#9600# + 7, v);   chk(v(3 downto 0) = x"0", "colour RAM $9607 = 0");
    -- CLK and DATA are pulled low until the KERNAL has initialised VIA 2 (CA2 and CB2 as inputs float
    -- high behind the inverters, as on the real machine, which does it for a few ms after reset); ATN
    -- is released
    chk(iec_atn_o = '1' and (iec_clk_o = '0' or iec_clk_o = '1') and (iec_data_o = '0' or iec_data_o = '1'), "IEC ATN released, CLK and DATA defined");
    chk(halted = '0', "not halted");
    if fails = 0 then report "PASS tb_vc20"; else report "FAILED tb_vc20: " & integer'image(fails) severity error; end if;
    done <= true;
    wait;
  end process;
end architecture;
