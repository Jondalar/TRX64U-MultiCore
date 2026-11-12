-- T83, LOCAL ONLY: the VC-20 core boots a real KERNAL/BASIC/CHAR set (images on the developer's own
-- machine, vc20_rom_pkg generated into the ignored build directory by gen_rompkg.py; never in CI).
-- Waits for the KERNAL's start screen, reads the screen back through the app's DMA window, then
-- types an "A" on the C64 matrix' A key and looks for it on the READY line.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.vc20_rom_pkg.all;

entity tb_vc20_boot is
  generic (WAIT_MS : integer := 1500; EXT : integer := 0; RO : integer := 0; SCRADR : integer := 16#1E00#);
end entity;

architecture sim of tb_vc20_boot is
  signal sys_clk, cpu_clk, mclk : std_logic := '0';
  signal sys_rst   : std_logic := '1';
  signal reset_req : std_logic := '1';
  signal keys      : std_logic_vector(63 downto 0) := (others => '0');
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
  signal audio_l, audio_r : std_logic_vector(23 downto 0);
  signal halted, iec_atn_o, iec_clk_o, iec_data_o : std_logic;
  signal done : boolean := false;
  signal cw_we : std_logic := '0';
  signal cw_addr : std_logic_vector(16 downto 0) := (others => '0');
  signal cw_wdata : std_logic_vector(7 downto 0) := (others => '0');
begin
  sys_clk <= not sys_clk after 15.859 ns when not done;
  cpu_clk <= not cpu_clk after 5 ns when not done;
  mclk    <= not mclk after 40.69 ns when not done;
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
      cw_we => cw_we, cw_re => '0', cw_addr => cw_addr, cw_wdata => cw_wdata, cw_rdata => open,
      cw_ack => open, cw_irq => open, px_we => open, px_re => open, px_rdata => x"00",
      px_ev_vsync => '0', px_ev_flip => '0', build => x"00000000", ddr_cal => (others => '0'));

  stim : process
    type screen_t is array (0 to 505) of integer;
    variable scr : screen_t;
    variable fails : integer := 0;
    procedure pwrite (a : integer; d : std_logic_vector(7 downto 0)) is
    begin
      wait until rising_edge(sys_clk);
      b_req <= '1'; b_we <= '1'; b_rom <= '0';
      b_addr <= std_logic_vector(to_unsigned(a, 16)); b_wdata <= d;
      wait until rising_edge(sys_clk);
      b_req <= '0'; b_we <= '0';
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
    function asc (c : integer) return character is
    begin
      if c >= 1 and c <= 26 then return character'val(64 + c);
      elsif c >= 32 and c <= 63 then return character'val(c);
      elsif c = 0 then return '@';
      else return '.'; end if;
    end function;
    procedure grab is
      variable v : std_logic_vector(7 downto 0);
    begin
      for i in 0 to 505 loop dmaread(SCRADR + i, v); scr(i) := to_integer(unsigned(v)); end loop;
    end procedure;
    procedure show is
      variable l : string(1 to 22);
    begin
      for r in 0 to 22 loop
        for c in 0 to 21 loop l(c + 1) := asc(scr(r * 22 + c)); end loop;
        report "|" & l & "|";
      end loop;
    end procedure;
    function has (s : string) return boolean is
      variable ok : boolean;
    begin
      for p in 0 to 506 - s'length loop
        ok := true;
        for i in s'range loop
          if asc(scr(p + i - s'low)) /= s(i) then ok := false; end if;
        end loop;
        if ok then return true; end if;
      end loop;
      return false;
    end function;
    variable cyc : integer := 0;
  begin
    wait for 200 ns;
    sys_rst <= '0';
    for i in 0 to 4095 loop pwrite(16#8000# + i, CHAR(i)); end loop;
    for i in 0 to 8191 loop pwrite(16#C000# + i, BASIC(i)); end loop;
    for i in 0 to 8191 loop pwrite(16#E000# + i, KERNAL(i)); end loop;
    report "ROMs written";
    -- the memory expansion, as the app writes it (RAM_EXT 0x0A1, RAM_RO 0x0A2), then the reset goes
    wait until rising_edge(cpu_clk);
    cw_addr <= std_logic_vector(to_unsigned(16#0A1#, 17)); cw_wdata <= std_logic_vector(to_unsigned(EXT, 8)); cw_we <= '1';
    wait until rising_edge(cpu_clk); cw_we <= '0';
    wait until rising_edge(cpu_clk);
    cw_addr <= std_logic_vector(to_unsigned(16#0A2#, 17)); cw_wdata <= std_logic_vector(to_unsigned(RO, 8)); cw_we <= '1';
    wait until rising_edge(cpu_clk); cw_we <= '0';
    wait for 1 us;
    reset_req <= '0';
    wait for WAIT_MS * 1 ms;
    grab; show;
    if has(" BYTES FREE") then report "ok   BYTES FREE on the screen"; else report "FAIL no BYTES FREE" severity error; fails := fails + 1; end if;
    if has("READY.") then report "ok   READY."; else report "FAIL no READY." severity error; fails := fails + 1; end if;
    -- type A (C64 matrix: column 1, row 2)
    keys(10) <= '1';
    wait for 120 ms;
    keys(10) <= '0';
    wait for 120 ms;
    grab; show;
    if scr(5 * 22) = 1 then report "ok   the A key typed an A"; else report "FAIL the A key typed screen code " & integer'image(scr(5 * 22)) severity error; fails := fails + 1; end if;
    if fails = 0 then report "PASS tb_vc20_boot"; else report "FAILED tb_vc20_boot" severity error; end if;
    done <= true;
    wait;
  end process;
end architecture;
