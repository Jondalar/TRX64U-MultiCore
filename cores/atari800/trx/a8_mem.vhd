-- SPDX-License-Identifier: GPL-3.0-or-later
-- T88 D4: the memory of the Atari core. Everything is empty at synthesis and filled by the application (no ROM image is in
-- the bitstream); the machine reaches it through the two buses of atari800core.
--
--   RAM_*   the 64 KB base RAM: block RAM, one port for the machine, one for the application's window and the reset sweep
--           (a cold reset clears it: the OS tells a cold from a warm start by what it finds in RAM).
--   SDRAM_* everything else, by address (address_decoder.vhdl with ROM_IN_RAM = 1, sdram_start_bank = 4):
--           0x700000-0x707FFF  the local ROM, block RAM: BASIC (8 KB, +0), the PBI/XEX loader ROM (8 KB, +0x2000) and the XL
--                              OS (16 KB, +0x4000); the application writes them through the window
--           0x010000-...       the extra RAM banks of the 128 KB .. 1,088 KB configurations: DDR2 window offset address-0x10000
--           0x800000-0xBFFFFF  the cartridge image (read only, Q12): DDR2 window offset 0x400000 + (address-0x800000)
--         The DDR2 part sits behind a direct-mapped cache (256 lines of 16 bytes in one block RAM, write through; a write that
--         hits invalidates the line). A hit answers like the block RAM; a miss holds the whole machine (`stall`) until the line
--         is in, so the machine's bus cycles are those of a memory that always answers in the same two ticks.
--
-- The latency rule, the same for every memory: a request shows in the interval after tick t0 (the machine's registers
-- moved at t0); the arrays read on every sys_clk edge, so they are valid after the first edge; `done` is the request
-- pulse delayed by one TICK (not one clock), so the machine sees it in the interval after tick t0+1 and takes it at tick
-- t0+2, whether or not the gap of the 9/10 enable fell in between. A miss stalls from t0+1 on (no tick) and the access
-- ends with the same `done` when the line is there. A DDR2 write or fetch under way is finished by the engine on the free
-- running clock, also while the machine is held; `quiet` says the engine is idle.
-- Everything the stall depends on is a register: tag and tag compare, the access flags.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity a8_mem is
  generic (
    BASE_W : unsigned(26 downto 0) := to_unsigned(16#800000#, 27));   -- DDR2 window base, in 16-bit words (byte 0x1000000)
  port (
    clk        : in  std_logic;                       -- sys_clk
    rst        : in  std_logic;                       -- flush the cache (sys_clk)
    tick       : in  std_logic;                       -- the machine's tick (CE28)

    ram_addr   : in  std_logic_vector(18 downto 0);
    ram_req    : in  std_logic;
    ram_we     : in  std_logic;
    ram_wdata  : in  std_logic_vector(7 downto 0);
    ram_q      : out std_logic_vector(7 downto 0);
    ram_done   : out std_logic;
    sd_addr    : in  std_logic_vector(24 downto 0);
    sd_req     : in  std_logic;
    sd_we      : in  std_logic;
    sd_q       : out std_logic_vector(7 downto 0);
    sd_done    : out std_logic;
    stall      : out std_logic;                       -- hold the machine (a register function)
    quiet      : out std_logic;                       -- nothing of the DDR2 engine is under way

    -- the application's window (a8_win) and the reset sweep
    w_wr       : in  std_logic;
    w_hit      : in  std_logic;
    w_rom      : in  std_logic;
    w_sel      : in  std_logic_vector(1 downto 0);    -- 0 OS, 1 BASIC, 2 PBI/XEX loader ROM
    w_ai       : in  std_logic_vector(15 downto 0);
    w_di       : in  std_logic_vector(7 downto 0);
    w_do       : out std_logic_vector(7 downto 0);
    clr_en     : in  std_logic;
    clr_a      : in  std_logic_vector(15 downto 0);

    -- the host's real-time DDR2 port (ui_clk)
    ui_clk     : in  std_logic;
    ui_rst     : in  std_logic;
    c_addr     : out std_logic_vector(26 downto 0);
    c_cmd      : out std_logic_vector(2 downto 0);
    c_en       : out std_logic;
    c_rdy      : in  std_logic;
    c_wdf_data : out std_logic_vector(127 downto 0);
    c_wdf_mask : out std_logic_vector(15 downto 0);
    c_wdf_wren : out std_logic;
    c_rd_data  : in  std_logic_vector(127 downto 0);
    c_rd_valid : in  std_logic;

    -- counters for the bring-up (sys_clk)
    st_hits    : out std_logic_vector(31 downto 0);
    st_miss    : out std_logic_vector(31 downto 0);
    st_wr      : out std_logic_vector(31 downto 0);
    st_stall   : out std_logic_vector(31 downto 0));
end entity;

architecture rtl of a8_mem is
  attribute ram_style : string;
  attribute ASYNC_REG : string;

  -- ---------------------------------------------------------------- base RAM, local ROM
  -- Vivado's true dual-port template, the shape cores/vc20/rtl/trx/vc20_ram.vhd routes with: the
  -- array is a shared variable, every port has its OWN process with at most one write, and each
  -- port READS BEFORE IT WRITES. Measured 2026-10-14, three attempts: both ports in one process
  -- gives "RAM has multiple writes via different ports in same process" and no inference at all
  -- (Synth 8-4767, 8-3391); two processes that write before they read give "Infeasible attribute
  -- ram_style = block ... trying to implement using LUTRAM" and then "Unsupported RAM template"
  -- (8-6849, 8-2914). Reading first is write-through for nobody: a 6502 write cycle and a window
  -- write do not use the read data, and the real hardware does not return the new byte either.
  -- The arrays live in a
  -- block so their type stays invisible to the rest of the architecture: an array OF
  -- std_logic_vector makes every "literal & vector" outside ambiguous. GHDL takes the
  -- non-protected shared variable because sim/files.sh passes -frelaxed.
  signal ram_a_q, ram_b_q : std_logic_vector(7 downto 0) := (others => '0');
  signal ram_b_we         : std_logic;
  signal ram_b_a          : std_logic_vector(15 downto 0);
  signal ram_b_d          : std_logic_vector(7 downto 0);
  signal rom_a_q, rom_b_q : std_logic_vector(7 downto 0) := (others => '0');
  signal rom_b_we         : std_logic;
  signal rom_b_a          : std_logic_vector(14 downto 0);
  signal wrom_q, whit_q   : std_logic := '0';

  -- ---------------------------------------------------------------- which memory
  signal sd_local  : std_logic;
  signal sd_ddr    : std_logic;
  signal xoff      : unsigned(22 downto 0);
  signal off       : unsigned(22 downto 0);             -- DDR2 window byte offset of the access (combinational)
  signal off_q     : unsigned(22 downto 0) := (others => '0');
  signal local_q   : std_logic := '0';

  -- ---------------------------------------------------------------- tick-domain flags
  signal ram_ok    : std_logic := '0';
  signal loc_ok    : std_logic := '0';
  signal acc_q     : std_logic := '0';                   -- a DDR2 access has had its first tick
  signal we_q      : std_logic := '0';
  signal cart_q    : std_logic := '0';
  signal posted    : std_logic := '0';                   -- the write of this access is with the engine
  signal wd_q      : std_logic_vector(7 downto 0) := (others => '0');

  -- ---------------------------------------------------------------- cache (4 KB data in 32-bit words, 256 tags)
  signal dq       : std_logic_vector(31 downto 0) := (others => '0');
  signal tq       : std_logic_vector(11 downto 0) := (others => '0');
  signal want_q   : std_logic_vector(10 downto 0) := (others => '0');
  signal boff_q   : std_logic_vector(1 downto 0) := "00";
  signal cd_we    : std_logic;
  signal cd_waddr : std_logic_vector(9 downto 0);
  signal cd_wdata : std_logic_vector(31 downto 0);
  signal ct_we    : std_logic;
  signal ct_waddr : std_logic_vector(7 downto 0);
  signal ct_wdata : std_logic_vector(11 downto 0);
  signal hit      : std_logic;
  signal comp_ddr : std_logic;
  signal ok_ddr   : std_logic;

  -- ---------------------------------------------------------------- the DDR2 engine (sys_clk side)
  type ms_t is (M_IDLE, M_RD, M_FILL, M_WR);
  signal ms       : ms_t := M_IDLE;
  signal rq_tgl   : std_logic := '0';
  signal rq_wr    : std_logic := '0';
  signal rq_line  : std_logic_vector(18 downto 0) := (others => '0');
  signal rq_lane  : std_logic_vector(3 downto 0) := (others => '0');
  signal rq_byte  : std_logic_vector(7 downto 0) := (others => '0');
  signal dn_tgl   : std_logic := '0';
  signal dn_s     : std_logic_vector(2 downto 0) := "000";
  attribute ASYNC_REG of dn_s : signal is "TRUE";
  signal ui_line  : std_logic_vector(127 downto 0) := (others => '0');
  signal fill_i   : unsigned(2 downto 0) := (others => '0');
  signal fl_cnt   : unsigned(8 downto 0) := to_unsigned(0, 9);    -- the flush sweep of the tags (256 = done)
  signal tst_hits, tst_miss, tst_wr, tst_stall : unsigned(31 downto 0) := (others => '0');
  signal stall_i  : std_logic;

  -- ---------------------------------------------------------------- ui side
  type us_t is (U_IDLE, U_ISSUE, U_WAITR);
  signal us       : us_t := U_IDLE;
  signal rq_s     : std_logic_vector(2 downto 0) := "000";
  attribute ASYNC_REG of rq_s : signal is "TRUE";
  signal u_line   : std_logic_vector(18 downto 0) := (others => '0');
  signal u_wr     : std_logic := '0';
  signal u_lane   : std_logic_vector(3 downto 0) := (others => '0');
  signal u_byte   : std_logic_vector(7 downto 0) := (others => '0');
  signal c_en_i   : std_logic := '0';
  signal c_wr_i   : std_logic := '0';
begin
  -- ================================================================ base RAM
  base_ram : block
    type ram8_t is array (0 to 65535) of std_logic_vector(7 downto 0);
    shared variable ram : ram8_t := (others => (others => '0'));
    attribute ram_style of ram : variable is "block";
  begin
    -- port A: the machine (6502, ANTIC, the SIO bridge) through a8_core's bus
    process (clk)
    begin
      if rising_edge(clk) then
        ram_a_q <= ram(to_integer(unsigned(ram_addr(15 downto 0))));
        if ram_req = '1' and ram_we = '1' then ram(to_integer(unsigned(ram_addr(15 downto 0)))) := ram_wdata; end if;
      end if;
    end process;
    -- port B: the application's window (a8_win) and the power-up clear
    process (clk)
    begin
      if rising_edge(clk) then
        ram_b_q <= ram(to_integer(unsigned(ram_b_a)));
        if ram_b_we = '1' then ram(to_integer(unsigned(ram_b_a))) := ram_b_d; end if;
      end if;
    end process;
  end block;
  ram_b_a  <= clr_a when clr_en = '1' else w_ai;
  ram_b_we <= '1' when clr_en = '1' else (w_wr and w_hit and not w_rom);
  ram_b_d  <= x"00" when clr_en = '1' else w_di;
  ram_q    <= ram_a_q;

  -- ================================================================ local ROM
  rom_b_a  <= "1" & w_ai(13 downto 0)  when w_sel = "00" else       -- OS at +0x4000
              "00" & w_ai(12 downto 0) when w_sel = "01" else       -- BASIC at +0
              "01" & w_ai(12 downto 0);                             -- PBI/XEX loader ROM at +0x2000
  rom_b_we <= w_wr and w_hit and w_rom;
  local_rom : block
    type rom8_t is array (0 to 32767) of std_logic_vector(7 downto 0);
    shared variable rom : rom8_t := (others => (others => '0'));
    attribute ram_style of rom : variable is "block";
  begin
    -- port A: the machine's read of OS, BASIC and the loader ROM
    process (clk)
    begin
      if rising_edge(clk) then
        rom_a_q <= rom(to_integer(unsigned(sd_addr(14 downto 0))));
      end if;
    end process;
    -- port B: the application's window writes the images and reads them back
    process (clk)
    begin
      if rising_edge(clk) then
        rom_b_q <= rom(to_integer(unsigned(rom_b_a)));
        if rom_b_we = '1' then rom(to_integer(unsigned(rom_b_a))) := w_di; end if;
      end if;
    end process;
  end block;
  -- the window's side flags travel with its data, outside the block
  process (clk)
  begin
    if rising_edge(clk) then
      wrom_q <= w_rom; whit_q <= w_hit;
    end if;
  end process;
  -- the window's read data, one clock after the address (an absent aperture reads 0)
  w_do <= x"00" when whit_q = '0' else rom_b_q when wrom_q = '1' else ram_b_q;

  -- ================================================================ classification and offsets
  sd_local <= '1' when sd_addr(24 downto 15) = "0011100000" else '0';
  sd_ddr   <= not sd_local;
  xoff     <= unsigned(sd_addr(22 downto 0)) - to_unsigned(16#10000#, 23);
  off      <= unsigned("1" & sd_addr(21 downto 0)) when sd_addr(23) = '1' else xoff;

  -- ================================================================ cache arrays
  process (clk)
    type data_t is array (0 to 1023) of std_logic_vector(31 downto 0);
    variable cdata : data_t := (others => (others => '0'));
    attribute ram_style of cdata : variable is "block";
  begin
    if rising_edge(clk) then
      dq <= cdata(to_integer(off(11 downto 2)));
      if cd_we = '1' then cdata(to_integer(unsigned(cd_waddr))) := cd_wdata; end if;
    end if;
  end process;
  process (clk)
    type tag_t is array (0 to 255) of std_logic_vector(11 downto 0);
    variable ctag : tag_t := (others => (others => '0'));
    attribute ram_style of ctag : variable is "block";
  begin
    if rising_edge(clk) then
      tq <= ctag(to_integer(off(11 downto 4)));
      if ct_we = '1' then ctag(to_integer(unsigned(ct_waddr))) := ct_wdata; end if;
    end if;
  end process;

  -- ================================================================ tick-domain flags and the registered view of the access
  process (clk)
  begin
    if rising_edge(clk) then
      off_q   <= off;
      want_q  <= std_logic_vector(off(22 downto 12));
      boff_q  <= std_logic_vector(off(1 downto 0));
      local_q <= sd_local;
      if tick = '1' then
        ram_ok <= ram_req;
        loc_ok <= sd_req and sd_local;
        acc_q  <= sd_req and sd_ddr;
        we_q   <= sd_we;
        cart_q <= sd_addr(23);
      end if;
    end if;
  end process;

  hit      <= tq(11) when tq(10 downto 0) = want_q else '0';
  -- a DDR2 access is complete: a read when its line is in the cache, a write once the engine has it (a write to the cartridge
  -- window is ignored: read only images)
  ok_ddr   <= hit when we_q = '0' else (posted or cart_q);
  comp_ddr <= acc_q and ok_ddr;
  stall_i  <= acc_q and not ok_ddr;
  stall    <= stall_i;
  ram_done <= ram_ok;
  sd_done  <= loc_ok or comp_ddr;
  sd_q     <= rom_a_q when local_q = '1' else
              dq(7 downto 0)   when boff_q = "00" else
              dq(15 downto 8)  when boff_q = "01" else
              dq(23 downto 16) when boff_q = "10" else
              dq(31 downto 24);

  -- ================================================================ the engine
  with fill_i(1 downto 0) select cd_wdata <=
    ui_line(31 downto 0)    when "00",
    ui_line(63 downto 32)   when "01",
    ui_line(95 downto 64)   when "10",
    ui_line(127 downto 96)  when others;
  cd_waddr <= rq_line(7 downto 0) & std_logic_vector(fill_i(1 downto 0));
  cd_we    <= '1' when ms = M_FILL and fill_i(2) = '0' else '0';
  process (ms, fill_i, fl_cnt, rq_line, acc_q, we_q, cart_q, posted, hit, tq, want_q, off_q)
  begin
    ct_we <= '0'; ct_waddr <= rq_line(7 downto 0); ct_wdata <= (others => '0');
    if ms = M_FILL and fill_i = 4 then
      ct_we <= '1'; ct_wdata <= '1' & rq_line(18 downto 8);
    elsif ms = M_IDLE and fl_cnt /= 256 then
      ct_we <= '1'; ct_waddr <= std_logic_vector(fl_cnt(7 downto 0)); ct_wdata <= (others => '0');
    elsif ms = M_IDLE and acc_q = '1' and we_q = '1' and cart_q = '0' and posted = '0' and hit = '1' then
      ct_we <= '1'; ct_waddr <= std_logic_vector(off_q(11 downto 4)); ct_wdata <= (others => '0');   -- the write invalidates its line
    end if;
  end process;

  process (clk)
  begin
    if rising_edge(clk) then
      dn_s <= dn_s(1 downto 0) & dn_tgl;
      if tick = '1' and posted = '1' then posted <= '0'; end if;
      case ms is
        when M_IDLE =>
          if fl_cnt /= 256 then
            fl_cnt <= fl_cnt + 1;                                   -- sweep the tags
          elsif acc_q = '1' and we_q = '0' and hit = '0' then      -- read miss: fetch the line
            rq_line <= std_logic_vector(off_q(22 downto 4)); rq_wr <= '0'; rq_tgl <= not rq_tgl; ms <= M_RD;
            tst_miss <= tst_miss + 1;
          elsif acc_q = '1' and we_q = '1' and cart_q = '0' and posted = '0' then   -- write: post it
            rq_line <= std_logic_vector(off_q(22 downto 4)); rq_lane <= std_logic_vector(off_q(3 downto 0));
            rq_byte <= wd_q; rq_wr <= '1'; rq_tgl <= not rq_tgl; ms <= M_WR; posted <= '1';
            tst_wr <= tst_wr + 1;
          end if;
        when M_RD =>
          if dn_s(2) /= dn_s(1) then ms <= M_FILL; fill_i <= (others => '0'); end if;
        when M_FILL =>
          fill_i <= fill_i + 1;
          if fill_i = 4 then ms <= M_IDLE; end if;
        when M_WR =>
          if dn_s(2) /= dn_s(1) then ms <= M_IDLE; end if;
      end case;
      if tick = '1' and acc_q = '1' and we_q = '0' and hit = '1' then tst_hits <= tst_hits + 1; end if;
      if stall_i = '1' then tst_stall <= tst_stall + 1; end if;
      if rst = '1' then fl_cnt <= to_unsigned(0, 9); end if;
    end if;
  end process;
  quiet    <= '1' when ms = M_IDLE and rq_tgl = dn_s(1) else '0';
  st_hits  <= std_logic_vector(tst_hits);
  st_miss  <= std_logic_vector(tst_miss);
  st_wr    <= std_logic_vector(tst_wr);
  st_stall <= std_logic_vector(tst_stall);

  -- the byte of a write: the machine's write data is stable while the access is open (address_decoder's data_write_reg)
  process (clk)
  begin
    if rising_edge(clk) then
      wd_q <= ram_wdata;
    end if;
  end process;

  -- ================================================================ ui side
  process (ui_clk)
    variable w : unsigned(26 downto 0);
  begin
    if rising_edge(ui_clk) then
      rq_s <= rq_s(1 downto 0) & rq_tgl;
      if ui_rst = '1' then
        us <= U_IDLE; c_en_i <= '0'; c_wr_i <= '0';
      else
        case us is
          when U_IDLE =>
            if rq_s(2) /= rq_s(1) then
              u_line <= rq_line; u_wr <= rq_wr; u_lane <= rq_lane; u_byte <= rq_byte; us <= U_ISSUE;
            end if;
          when U_ISSUE =>
            c_en_i <= '1';
            w := BASE_W + shift_left(resize(unsigned(u_line), 27), 3);
            c_addr <= std_logic_vector(w);
            if u_wr = '1' then
              c_cmd <= "000"; c_wr_i <= '1';
              c_wdf_data <= (others => '0');
              for i in 0 to 15 loop c_wdf_data(8 * i + 7 downto 8 * i) <= u_byte; end loop;
              c_wdf_mask <= (others => '1');
              c_wdf_mask(to_integer(unsigned(u_lane))) <= '0';
            else
              c_cmd <= "001"; c_wr_i <= '0';
            end if;
            if c_en_i = '1' and c_rdy = '1' then
              c_en_i <= '0'; c_wr_i <= '0';
              if u_wr = '1' then dn_tgl <= not dn_tgl; us <= U_IDLE; else us <= U_WAITR; end if;
            end if;
          when U_WAITR =>
            if c_rd_valid = '1' then ui_line <= c_rd_data; dn_tgl <= not dn_tgl; us <= U_IDLE; end if;
        end case;
      end if;
    end if;
  end process;
  c_en       <= c_en_i;
  c_wdf_wren <= c_wr_i;
end architecture;
