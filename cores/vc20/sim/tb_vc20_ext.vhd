-- T83: the memory expansion. A hand-written program (gen_testrom.py, KERNAL_MEM) writes $A5 to the first and last byte of
-- each expansion block and reads it back through the CPU; the results are read out of RAM. The expansion registers are
-- written as the app writes them (RAM_EXT 0x0A1, RAM_RO 0x0A2) and the machine reset.
--   unexpanded           blocks read $FF (nothing there); $1000 and $0200 read $A5
--   all blocks (0x1F)    every block reads $A5
--   all, BLK5 read only  BLK5 keeps its zeros (a write is refused), the others $A5
--   3K only, BLK1 only   exactly those blocks
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.vc20_testrom_pkg.all;

entity tb_vc20_ext is
end entity;

architecture sim of tb_vc20_ext is
  signal sys_clk, cpu_clk, mclk : std_logic := '0';
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
  signal dma_req_tgl : std_logic := '0';
  signal dma_done_tgl : std_logic;
  signal dma_addr  : std_logic_vector(15 downto 0) := (others => '0');
  signal dma_rdata : std_logic_vector(7 downto 0);
  signal cw_we : std_logic := '0';
  signal cw_addr : std_logic_vector(16 downto 0) := (others => '0');
  signal cw_wdata : std_logic_vector(7 downto 0) := (others => '0');
  signal done : boolean := false;
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
      reset_req => reset_req, halt_req => '0', halted => open,
      keys => (others => '0'), joy1 => "00000", joy2 => "00000", restore => '0',
      iec_atn_i => '1', iec_clk_i => '1', iec_data_i => '1',
      iec_atn_o => open, iec_clk_o => open, iec_data_o => open,
      dma_req_tgl => dma_req_tgl, dma_addr => dma_addr, dma_we => '0', dma_wdata => x"00",
      dma_rom => '0', dma_done_tgl => dma_done_tgl, dma_rdata => dma_rdata, dbg_busy => b_req,
      win_port_use => win_use, win_port_addr => win_addr, win_port_rom => win_rom,
      win_port_we => win_we, win_port_wdata => win_wdata,
      port_req => port_req, port_addr => port_addr, port_rom => port_rom, port_we => port_we,
      port_wdata => port_wdata, ram_rdata => open,
      pix_color => open, pix_x => open, pix_y => open, audio_l => open, audio_r => open,
      cw_we => cw_we, cw_re => '0', cw_addr => cw_addr, cw_wdata => cw_wdata, cw_rdata => open,
      cw_ack => open, cw_irq => open, px_we => open, px_re => open, px_rdata => x"00",
      px_ev_vsync => '0', px_ev_flip => '0', build => x"00000000", ddr_cal => (others => '0'));

  stim : process
    variable fails : integer := 0;
    variable v : std_logic_vector(7 downto 0);
    procedure pwrite (rom : std_logic; a : integer; d : std_logic_vector(7 downto 0)) is
    begin
      wait until rising_edge(sys_clk);
      b_req <= '1'; b_we <= '1'; b_rom <= rom;
      b_addr <= std_logic_vector(to_unsigned(a, 16)); b_wdata <= d;
      wait until rising_edge(sys_clk);
      b_req <= '0'; b_we <= '0';
    end procedure;
    procedure cwwrite (a : integer; d : integer) is
    begin
      wait until rising_edge(cpu_clk);
      cw_addr <= std_logic_vector(to_unsigned(a, 17)); cw_wdata <= std_logic_vector(to_unsigned(d, 8)); cw_we <= '1';
      wait until rising_edge(cpu_clk); cw_we <= '0';
    end procedure;
    procedure dmaread (a : integer; r : out std_logic_vector(7 downto 0)) is
      variable t : std_logic;
    begin
      t := dma_done_tgl;
      wait until rising_edge(sys_clk);
      dma_addr <= std_logic_vector(to_unsigned(a, 16));
      dma_req_tgl <= not dma_req_tgl;
      wait until rising_edge(sys_clk) and dma_done_tgl /= t;
      r := dma_rdata;
    end procedure;
    -- order of the test addresses: 0400 0FFF 2000 3FFF 4000 5FFF 6000 7FFF A000 BFFF 1000 0200
    type exp_t is array (0 to 11) of std_logic_vector(7 downto 0);
    procedure run (name : string; ext, ro : integer; e : exp_t) is
    begin
      reset_req <= '1';
      cwwrite(16#0A1#, ext);
      cwwrite(16#0A2#, ro);
      wait for 2 us;
      reset_req <= '0';
      wait for 4 ms;
      for i in 0 to 11 loop
        dmaread(16#0300# + i, v);
        if v /= e(i) then
          report "FAIL " & name & ": result " & integer'image(i) & " is " & integer'image(to_integer(unsigned(v))) &
                 ", expected " & integer'image(to_integer(unsigned(e(i)))) severity error;
          fails := fails + 1;
        end if;
      end loop;
      report "ok   " & name;
    end procedure;
    constant FF : std_logic_vector(7 downto 0) := x"FF";
    constant A5 : std_logic_vector(7 downto 0) := x"A5";
    constant Z0 : std_logic_vector(7 downto 0) := x"00";
  begin
    wait for 200 ns;
    sys_rst <= '0';
    for i in 0 to 8191 loop pwrite('1', 16#2000# + i, KERNAL_MEM(i)); end loop;
    run("unexpanded", 0, 0,          (FF, FF, FF, FF, FF, FF, FF, FF, FF, FF, A5, A5));
    run("+3K", 1, 0,                 (A5, A5, FF, FF, FF, FF, FF, FF, FF, FF, A5, A5));
    run("+8K (BLK1)", 2, 0,          (FF, FF, A5, A5, FF, FF, FF, FF, FF, FF, A5, A5));
    run("+16K (BLK1+2)", 6, 0,       (FF, FF, A5, A5, A5, A5, FF, FF, FF, FF, A5, A5));
    run("+24K (BLK1-3)", 14, 0,      (FF, FF, A5, A5, A5, A5, A5, A5, FF, FF, A5, A5));
    run("+3K +24K", 15, 0,           (A5, A5, A5, A5, A5, A5, A5, A5, FF, FF, A5, A5));
    run("all, BLK5 RAM", 31, 0,      (A5, A5, A5, A5, A5, A5, A5, A5, A5, A5, A5, A5));
    -- RAM keeps its contents across resets: clear BLK5 through the window, then a ROM there must refuse the program's write
    pwrite('0', 16#A000#, x"00"); pwrite('0', 16#BFFF#, x"00");
    run("all, BLK5 ROM", 31, 16,     (A5, A5, A5, A5, A5, A5, A5, A5, Z0, Z0, A5, A5));
    if fails = 0 then report "PASS tb_vc20_ext"; else report "FAILED tb_vc20_ext: " & integer'image(fails) severity error; end if;
    done <= true;
    wait;
  end process;
end architecture;
