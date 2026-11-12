-- T118: the VC-20 core's pix_x / pix_y stream, written to a file for the HDMI geometry bench
-- (tb_hdmigeom.v replays it into the host's vic_scaler). One record "<sys_clk index> <x> <y>" per change of
-- (pix_x, pix_y), counted from the release of reset_req. The core is the same vc20_core tb_vc20 runs; no program is
-- needed, the 6561 raster runs by itself (tb_vc20 check 4 proves the frame period 628,992).
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.textio.all;

entity tb_vc20_pix is
end entity;

architecture sim of tb_vc20_pix is
  signal sys_clk, cpu_clk, mclk : std_logic := '0';
  signal sys_rst   : std_logic := '1';
  signal reset_req : std_logic := '1';
  signal keys      : std_logic_vector(63 downto 0) := (others => '0');
  signal port_req, port_rom, port_we : std_logic;
  signal port_addr  : std_logic_vector(15 downto 0);
  signal port_wdata : std_logic_vector(7 downto 0);
  signal ram_rdata  : std_logic_vector(7 downto 0);
  signal win_use, win_rom, win_we : std_logic;
  signal win_addr  : std_logic_vector(15 downto 0);
  signal win_wdata : std_logic_vector(7 downto 0);
  signal dma_done_tgl : std_logic;
  signal dma_rdata : std_logic_vector(7 downto 0);
  signal pix_color : std_logic_vector(3 downto 0);
  signal pix_x     : std_logic_vector(9 downto 0);
  signal pix_y     : std_logic_vector(8 downto 0);
  signal cw_rdata  : std_logic_vector(7 downto 0);
  signal cw_ack    : std_logic;
  signal iec_atn_o, iec_clk_o, iec_data_o : std_logic;
  signal audio_l, audio_r : std_logic_vector(23 downto 0);
  signal halted : std_logic;
  signal done : boolean := false;
  file f : text;
begin
  sys_clk <= not sys_clk after 15.859 ns when not done;     -- 31.5278 MHz
  cpu_clk <= not cpu_clk after 5 ns when not done;
  mclk    <= not mclk after 40.69 ns when not done;

  port_req   <= win_use;
  port_addr  <= win_addr;
  port_rom   <= win_rom;
  port_we    <= win_we;
  port_wdata <= win_wdata;

  dut : entity work.vc20_core
    port map (
      sys_clk => sys_clk, sys_rst => sys_rst, cpu_clk => cpu_clk, mclk => mclk,
      reset_req => reset_req, halt_req => '0', halted => halted,
      keys => keys, joy1 => "00000", joy2 => "00000", restore => '0',
      iec_atn_i => '1', iec_clk_i => '1', iec_data_i => '1',
      iec_atn_o => iec_atn_o, iec_clk_o => iec_clk_o, iec_data_o => iec_data_o,
      dma_req_tgl => '0', dma_addr => x"0000", dma_we => '0', dma_wdata => x"00",
      dma_rom => '0', dma_done_tgl => dma_done_tgl, dma_rdata => dma_rdata, dbg_busy => '0',
      win_port_use => win_use, win_port_addr => win_addr, win_port_rom => win_rom,
      win_port_we => win_we, win_port_wdata => win_wdata,
      port_req => port_req, port_addr => port_addr, port_rom => port_rom, port_we => port_we,
      port_wdata => port_wdata, ram_rdata => ram_rdata,
      pix_color => pix_color, pix_x => pix_x, pix_y => pix_y, audio_l => audio_l, audio_r => audio_r,
      cw_we => '0', cw_re => '0', cw_addr => (others => '0'), cw_wdata => x"00", cw_rdata => cw_rdata,
      cw_ack => cw_ack, cw_irq => open, px_we => open, px_re => open, px_rdata => x"00",
      px_ev_vsync => '0', px_ev_flip => '0', build => x"12345678", ddr_cal => (others => '0'));

  dump : process
    variable cyc : integer := 0;
    variable l   : line;
    variable lx  : std_logic_vector(9 downto 0) := (others => 'U');
    variable ly  : std_logic_vector(8 downto 0) := (others => 'U');
    variable n   : integer := 0;
  begin
    file_open(f, "vc20_pix.txt", write_mode);
    wait for 200 ns;
    sys_rst <= '0';
    wait for 2 us;
    reset_req <= '0';
    -- 5 frames after the release
    while cyc < 5 * 628992 loop
      wait until rising_edge(sys_clk);
      cyc := cyc + 1;
      if pix_x /= lx or pix_y /= ly then
        lx := pix_x; ly := pix_y;
        write(l, cyc); write(l, string'(" "));
        write(l, to_integer(unsigned(pix_x))); write(l, string'(" "));
        write(l, to_integer(unsigned(pix_y)));
        writeline(f, l);
        n := n + 1;
      end if;
    end loop;
    file_close(f);
    report "records " & integer'image(n);
    done <= true;
    wait;
  end process;
end architecture;
