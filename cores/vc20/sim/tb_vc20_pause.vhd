-- T83: Freeze for the VC-20 core. Two identical machines run the pause test program (gen_testrom.py, KERNAL_PAUSE: sound
-- on, VIA2 T1 free running with its IRQ, T2 counting, a counting main loop). A runs free, B gets a pause for 64 ms
-- (three frames and a bit) in the middle of the run, asserted at an arbitrary sys_clk (halt_req, as the host drives it
-- from the app's C64_STOP).
--   1  B reports halted a few clocks after the request
--   2  while paused the 6502 address, the VIA timers, the zero-page counters and the IRQ count do not move
--   3  while paused the 6561 keeps running: pix_x / pix_y of B equal A's in every sys_clk of the whole run, pix_y wraps
--      three times, so the scaler never loses its lock
--   4  while paused the sound is exactly 0; before and after it is not
--   5  after the release B continues exactly where it stopped: B's bus cycles (address, R/W, written data, in order, the
--      written data include the VIA timer values the program copies) equal A's, cycle for cycle, to the end of B's log
--   6  a reset takes the hold away (the 6502 runs through its reset while the pause is still asserted)
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.vc20_testrom_pkg.all;

entity tb_vc20_pause is
end entity;

architecture sim of tb_vc20_pause is
  signal sys_clk, cpu_clk, mclk : std_logic := '0';
  signal sys_rst   : std_logic := '1';
  signal reset_a, reset_b : std_logic := '1';
  signal pause     : std_logic := '0';
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
  signal pix_x_a, pix_x_b : std_logic_vector(9 downto 0);
  signal pix_y_a, pix_y_b : std_logic_vector(8 downto 0);
  signal audio_a, audio_b, audio_ar, audio_br : std_logic_vector(23 downto 0);
  signal halted_a, halted_b : std_logic;
  signal unused_use_a, unused_rom_a, unused_we_a : std_logic;
  signal unused_addr_a  : std_logic_vector(15 downto 0);
  signal unused_wdata_a : std_logic_vector(7 downto 0);
  signal done : boolean := false;

  signal tap_a, tap_b : std_logic_vector(25 downto 0);     -- step, R/W, address, data out
  signal x_phase : integer := 0;                -- 0 run-in, 1 paused, 2 after the release
  signal vid_mismatch : integer := 0;
  signal wraps_paused : integer := 0;
  signal loud_before, loud_after : boolean := false;
  signal cmp_now : std_logic := '0';
  signal cmp_bad, cmp_n, cmp_na : integer := 0;
  signal reset_started : boolean := false;
  signal count_on : std_logic := '0';
  signal steps_in_reset : integer := 0;
begin
  sys_clk <= not sys_clk after 15.859 ns when not done;     -- 31.5278 MHz
  cpu_clk <= not cpu_clk after 5 ns when not done;
  mclk    <= not mclk after 40.69 ns when not done;
  -- one host port for both machines (A never uses the DMA window)
  port_req   <= b_req or win_use;
  port_addr  <= b_addr  when b_req = '1' else win_addr;
  port_rom   <= b_rom   when b_req = '1' else win_rom;
  port_we    <= b_we    when b_req = '1' else win_we;
  port_wdata <= b_wdata when b_req = '1' else win_wdata;

  dut_a : entity work.vc20_core
    port map (
      sys_clk => sys_clk, sys_rst => sys_rst, cpu_clk => cpu_clk, mclk => mclk,
      reset_req => reset_a, halt_req => '0', halted => halted_a, cpu_tap => tap_a,
      keys => (others => '0'), joy1 => "00000", joy2 => "00000", restore => '0',
      iec_atn_i => '1', iec_clk_i => '1', iec_data_i => '1',
      iec_atn_o => open, iec_clk_o => open, iec_data_o => open,
      dma_req_tgl => '0', dma_addr => x"0000", dma_we => '0', dma_wdata => x"00",
      dma_rom => '0', dma_done_tgl => open, dma_rdata => open, dbg_busy => b_req,
      win_port_use => unused_use_a, win_port_addr => unused_addr_a, win_port_rom => unused_rom_a,
      win_port_we => unused_we_a, win_port_wdata => unused_wdata_a,
      port_req => port_req, port_addr => port_addr, port_rom => port_rom, port_we => port_we,
      port_wdata => port_wdata, ram_rdata => open,
      pix_color => open, pix_x => pix_x_a, pix_y => pix_y_a, audio_l => audio_a, audio_r => audio_ar,
      cw_we => '0', cw_re => '0', cw_addr => (others => '0'), cw_wdata => x"00", cw_rdata => open,
      cw_ack => open, cw_irq => open, px_we => open, px_re => open, px_rdata => x"00",
      px_ev_vsync => '0', px_ev_flip => '0', build => x"00000000", ddr_cal => (others => '0'));

  dut_b : entity work.vc20_core
    port map (
      sys_clk => sys_clk, sys_rst => sys_rst, cpu_clk => cpu_clk, mclk => mclk,
      reset_req => reset_b, halt_req => pause, halted => halted_b, cpu_tap => tap_b,
      keys => (others => '0'), joy1 => "00000", joy2 => "00000", restore => '0',
      iec_atn_i => '1', iec_clk_i => '1', iec_data_i => '1',
      iec_atn_o => open, iec_clk_o => open, iec_data_o => open,
      dma_req_tgl => dma_req_tgl, dma_addr => dma_addr, dma_we => '0', dma_wdata => x"00",
      dma_rom => '0', dma_done_tgl => dma_done_tgl, dma_rdata => dma_rdata, dbg_busy => b_req,
      win_port_use => win_use, win_port_addr => win_addr, win_port_rom => win_rom,
      win_port_we => win_we, win_port_wdata => win_wdata,
      port_req => port_req, port_addr => port_addr, port_rom => port_rom, port_we => port_we,
      port_wdata => port_wdata, ram_rdata => open,
      pix_color => open, pix_x => pix_x_b, pix_y => pix_y_b, audio_l => audio_b, audio_r => audio_br,
      cw_we => '0', cw_re => '0', cw_addr => (others => '0'), cw_wdata => x"00", cw_rdata => open,
      cw_ack => open, cw_irq => open, px_we => open, px_re => open, px_rdata => x"00",
      px_ev_vsync => '0', px_ev_flip => '0', build => x"00000000", ddr_cal => (others => '0'));


  -- 3: the picture timing of the two machines is the same in every clock
  video : process (sys_clk)
    variable last_y : std_logic_vector(8 downto 0) := (others => '0');
  begin
    if rising_edge(sys_clk) then
      if sys_rst = '0' and reset_started and (pix_x_a /= pix_x_b or pix_y_a /= pix_y_b) then
        vid_mismatch <= vid_mismatch + 1;
      end if;
      if x_phase = 1 and pix_y_b /= last_y and pix_y_b = "000000000" then
        wraps_paused <= wraps_paused + 1;
      end if;
      last_y := pix_y_b;
      if count_on = '1' and tap_b(25) = '1' then steps_in_reset <= steps_in_reset + 1; end if;
    end if;
  end process;

  -- 4: the sound before and after (the paused samples are checked by the stimulus)
  sound : process (mclk)
  begin
    if rising_edge(mclk) then
      if x_phase = 0 and audio_b /= x"000000" then loud_before <= true; end if;
      if x_phase = 2 and audio_b /= x"000000" then loud_after <= true; end if;
    end if;
  end process;

  -- 5: the bus cycles of both machines, in order
  logger : process (sys_clk)
    type log_t is array (0 to 199999) of integer;
    variable la, lb : log_t;
    variable na, nb : integer := 0;
    variable bad : integer;
    variable first_bad : integer;
  begin
    if rising_edge(sys_clk) then
      if tap_a(25) = '1' and na < 200000 then
        la(na) := to_integer(unsigned(tap_a(24 downto 0))); na := na + 1;
      end if;
      if tap_b(25) = '1' and nb < 200000 and x_phase < 3 then
        lb(nb) := to_integer(unsigned(tap_b(24 downto 0))); nb := nb + 1;
      end if;
      if cmp_now = '1' then
        bad := 0; first_bad := -1;
        for i in 0 to nb - 1 loop
          if la(i) /= lb(i) then
            bad := bad + 1;
            if first_bad < 0 then first_bad := i; end if;
          end if;
        end loop;
        cmp_bad <= bad; cmp_n <= nb; cmp_na <= na;
        if first_bad >= 0 then report "first differing bus cycle: " & integer'image(first_bad) severity note; end if;
      end if;
    end if;
  end process;


  stim : process
    variable fails : integer := 0;
    variable addr0 : std_logic_vector(15 downto 0);
    variable ram0, ram1 : std_logic_vector(31 downto 0);
    variable moved : integer;
    variable loud_paused : integer := 0;
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
    procedure ram4 (r : out std_logic_vector(31 downto 0)) is
      variable b : std_logic_vector(7 downto 0);
    begin
      for i in 0 to 3 loop
        dmaread(16#FB# + i, b);
        r(8 * i + 7 downto 8 * i) := b;
      end loop;
    end procedure;
  begin
    wait for 200 ns;
    sys_rst <= '0';
    for i in 0 to 8191 loop pwrite('1', 16#2000# + i, KERNAL_PAUSE(i)); end loop;
    reset_started <= true;
    wait for 2 us;
    reset_a <= '0'; reset_b <= '0';
    wait for 3.0137 ms;                     -- the machines are in their main loop

    -- 1: the pause
    wait until rising_edge(sys_clk);
    x_phase <= 1;
    pause <= '1';
    wait for 2 us;
    chk(halted_b = '1', "halted a moment after the request");
    chk(halted_a = '0', "the free machine is not halted");
    wait for 200 us;                        -- the sound's one window
    addr0 := tap_b(23 downto 8);
    ram4(ram0);
    -- 2, 4: nothing moves for 64 ms; the sound stays 0
    moved := 0;
    for k in 1 to 64 loop
      wait for 1 ms;
      if tap_b(23 downto 8) /= addr0 or tap_b(25) = '1' then moved := moved + 1; end if;
      if audio_b /= x"000000" then loud_paused := loud_paused + 1; end if;
      if halted_b /= '1' then moved := moved + 1; end if;
    end loop;
    ram4(ram1);
    chk(moved = 0, "no 6502 step, address unchanged and halted over 64 ms");
    chk(ram0 = ram1, "zero page counters (main loop, IRQ count, T2 copy) unchanged");
    chk(loud_paused = 0, "sound exactly 0 for 64 ms paused");
    chk(wraps_paused >= 3, "6561 kept running: pix_y wrapped " & integer'image(wraps_paused) & " times while paused");
    chk(vid_mismatch = 0, "pix_x/pix_y equal to the free machine in every clock (mismatches " & integer'image(vid_mismatch) & ")");

    -- the release
    wait until rising_edge(sys_clk);
    x_phase <= 2;
    pause <= '0';
    wait for 2 us;
    chk(halted_b = '0', "halted drops after the release");
    wait for 4 ms;
    ram4(ram1);
    chk(ram1 /= ram0, "the program runs on after the release");
    chk(loud_before, "sound before the pause");
    chk(loud_after, "sound after the release");
    chk(vid_mismatch = 0, "pix_x/pix_y still equal after the release");
    wait until rising_edge(sys_clk);
    cmp_now <= '1';
    wait until rising_edge(sys_clk);
    cmp_now <= '0';
    wait until rising_edge(sys_clk);
    wait until rising_edge(sys_clk);
    chk(cmp_n > 3000 and cmp_na > cmp_n, "bus cycles logged: B " & integer'image(cmp_n) & ", A " & integer'image(cmp_na));
    chk(cmp_bad = 0, "all of B's bus cycles equal A's in order (" & integer'image(cmp_bad) & " differ)");

    -- 6: a reset while the pause is asserted: the 6502 must run through it
    x_phase <= 3;
    pause <= '1';
    wait for 100 us;
    chk(halted_b = '1', "paused again");
    reset_b <= '1';
    wait for 2 us;
    chk(halted_b = '0', "a reset takes the hold away");
    count_on <= '1';
    wait for 100 us;
    count_on <= '0';
    chk(steps_in_reset > 50, "the 6502 steps during the reset with the pause asserted (" & integer'image(steps_in_reset) & ")");
    reset_b <= '0';
    wait for 1 ms;
    chk(halted_b = '1', "the pause holds again once the reset is over");
    pause <= '0';

    if fails = 0 then report "PASS tb_vc20_pause"; else report "FAILED tb_vc20_pause: " & integer'image(fails) severity error; end if;
    done <= true;
    wait;
  end process;
end architecture;
