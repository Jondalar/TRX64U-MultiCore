-- TRX Apple //e core (T89 Q4: plan B from the start): the 64 KB of aux RAM in the DDR2, behind a cache, on the host's real-time port c_*.
-- Written for this project (TREX), no upstream code; the engine and its clock crossing follow nes_mem.v (the NES core's cartridge cache).
--
-- What the machine needs: the Apple's RAM takes its address once per phase of the machine cycle. The edge that ends a phase of the 2 MHz clock Q3 / PHI0
-- toggles them (apple2.vhd Q3_RISE: the next enable is that edge); one enable later the RAM samples the address of the phase that began (the SDRAM
-- of NanoApple2 starts its cycle after the rising edge of CLK_2M) and shows the data from the enable after that on: the video latch takes
-- the scanner's byte at the end of the same phase, the CPU takes its byte at the start of the next one. Aux RAM is needed by
--   - the CPU, in its phase (PHASE_ZERO = 1), when it reads aux RAM ($0000-$01FF with ALTZP, the RAMRD / 80STORE pages, the language card RAM with ALTZP;
--     a ROM or I/O address with ALTZP on is no aux access)
--   - the video scanner, in its phase (PHASE_ZERO = 0), only while COL80 is on (40 columns never show the aux byte)
-- and written (write-through, one byte) by the CPU when it writes aux.
-- The address of a phase is known long before it begins: the scanner's address changes at the start of the CPU's phase and the CPU's address a few
-- enables after its enable, so the next phase's address (`a_nxt`: the other one of the two) is looked up all the time. The cache: 256 lines of 16 bytes
-- (4 KB, one RAMB36 as 1024 x 32 with byte writes), direct mapped, index = line[7:0] xor line[11:8], tag = line[11:8] and a valid bit. At the edge that ends
-- a phase, a read that is not in the cache HOLDS THE WHOLE MACHINE (`stall` takes the 14M enable away, the accumulator that makes it too) until
-- the line is in, so the machine's bus cycles are those of a memory that always answers in time and nothing is out of step; the machine only runs a little
-- slower while it waits (the stall is counted). A write to a line in the cache updates it in place; write misses are not allocated. The window's byte read and
-- write go through the same engine (one job at a time).
--
-- Lookup: the next address is registered, the tag and the data word are read from it, the compare is valid two clocks later; a result counts when the
-- address has not changed for three clocks (`stab`), so a toggle that comes shortly after an address change waits instead of trusting a stale compare.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity a2_aux is
  port (
    clk        : in  std_logic;
    rst        : in  std_logic;
    -- the machine
    ce         : in  std_logic;                       -- the machine's enable
    samp_en    : in  std_logic;                       -- ce, one enable after the toggle: the RAM samples the new phase's address
    q3_rise    : in  std_logic;                       -- the next enable toggles the phase (Q3_RISE)
    cpu_addr   : in  std_logic_vector(15 downto 0);   -- the RAM address of the CPU's cycle (CPU_ADDR_RAM)
    vid_addr   : in  std_logic_vector(15 downto 0);   -- the RAM address of the scanner's cycle (VIDEO_ADDR)
    cpu_phase  : in  std_logic;                       -- PHASE_ZERO: the phase that is running (1: the CPU's)
    col80      : in  std_logic;
    aux_sel    : in  std_logic;                       -- the machine's aux flag (the CPU's access goes to aux)
    cpu_ram    : in  std_logic;                       -- the CPU's address is RAM (not ROM or I/O): only then does aux matter
    cpu_we     : in  std_logic;                       -- the CPU writes this cycle (CPU_WE)
    cpu_ram_wr : in  std_logic;                       -- the CPU writes RAM this cycle (CPU_RAM_WR)
    wdata      : in  std_logic_vector(7 downto 0);
    dout       : out std_logic_vector(7 downto 0);    -- the aux byte, registered at the sample
    stall      : out std_logic;
    -- the app's window (sys_clk)
    w_start    : in  std_logic;
    w_we       : in  std_logic;
    w_addr     : in  std_logic_vector(15 downto 0);
    w_wdata    : in  std_logic_vector(7 downto 0);
    w_done     : out std_logic;
    w_rdata    : out std_logic_vector(7 downto 0);
    -- quasi static
    aux_base   : in  std_logic_vector(31 downto 0);   -- sys_clk side (the caller synchronised it)
    -- counters
    st_miss    : out std_logic_vector(31 downto 0);
    st_stall   : out std_logic_vector(31 downto 0);
    st_wait    : out std_logic;
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
    c_rd_valid : in  std_logic
  );
end entity;

architecture rtl of a2_aux is
  attribute ASYNC_REG : string;
  attribute ram_style : string;
  subtype s8 is std_logic_vector(7 downto 0);
  subtype s10 is std_logic_vector(9 downto 0);
  function idx_of (a : std_logic_vector(15 downto 0)) return s8 is
  begin
    return a(11 downto 4) xor (a(15 downto 12) & a(15 downto 12));
  end function;
  -- the data word of an address: {index, address bits 3:2}
  function word_of (a : std_logic_vector(15 downto 0)) return s10 is
  begin
    return idx_of(a) & a(3 downto 2);
  end function;
  type data_t is array (0 to 1023) of std_logic_vector(31 downto 0);
  signal cdata : data_t := (others => (others => '0'));
  attribute ram_style of cdata : signal is "block";
  type tag_t is array (0 to 255) of std_logic_vector(4 downto 0);      -- {valid, tag}
  signal ctag  : tag_t := (others => (others => '0'));
  attribute ram_style of ctag : signal is "distributed";
  signal a_q, a_qq : std_logic_vector(15 downto 0) := (others => '0');
  signal stab    : integer range 0 to 3 := 0;
  signal d_q     : std_logic_vector(31 downto 0) := (others => '0');
  signal t_q     : std_logic_vector(4 downto 0) := (others => '0');
  signal hit, rd_nxt, wr_nxt, miss_ev : std_logic;
  signal a_nxt   : std_logic_vector(15 downto 0);
  signal hd      : std_logic_vector(7 downto 0) := (others => '0');
  signal hh, rd_pend, wr_pend : std_logic := '0';
  signal ha      : std_logic_vector(15 downto 0) := (others => '0');
  signal flushing : std_logic := '1';
  signal fl_cnt  : unsigned(8 downto 0) := (others => '0');
  signal base_q  : std_logic_vector(31 downto 0) := (others => '0');
  -- the write side of the arrays (one writer, the engine process)
  signal d_we    : std_logic_vector(3 downto 0) := (others => '0');
  signal d_waddr : std_logic_vector(9 downto 0) := (others => '0');
  signal d_wdata : std_logic_vector(31 downto 0) := (others => '0');
  signal t_we    : std_logic := '0';
  signal t_waddr : std_logic_vector(7 downto 0) := (others => '0');
  signal t_wdata : std_logic_vector(4 downto 0) := (others => '0');
  -- the engine (sys_clk side)
  type ms_t is (M_IDLE, M_RD, M_FILL, M_WR);
  signal ms      : ms_t := M_IDLE;
  type jt_t is (J_FILL, J_WIN_RD, J_WIN_WR, J_WR);
  signal jt      : jt_t := J_FILL;
  signal j_line  : std_logic_vector(11 downto 0) := (others => '0');
  signal j_lane  : integer range 0 to 15 := 0;
  signal rq_tgl  : std_logic := '0';
  signal rq_wr   : std_logic := '0';
  signal rq_addr : std_logic_vector(26 downto 0) := (others => '0');
  signal rq_lane : unsigned(3 downto 0) := (others => '0');
  signal rq_byte : std_logic_vector(7 downto 0) := (others => '0');
  signal dn_tgl  : std_logic := '0';
  signal dn_s    : std_logic_vector(2 downto 0) := (others => '0');
  attribute ASYNC_REG of dn_s : signal is "TRUE";
  signal ui_line : std_logic_vector(127 downto 0) := (others => '0');
  signal fill_i  : integer range 0 to 7 := 0;
  signal wq_v    : std_logic := '0';                 -- a CPU write waits for the engine
  signal wq_a    : std_logic_vector(15 downto 0) := (others => '0');
  signal wq_d    : std_logic_vector(7 downto 0) := (others => '0');
  signal wc_v    : std_logic := '0';                 -- its cache update is due
  signal wc_a    : std_logic_vector(15 downto 0) := (others => '0');
  signal wc_d    : std_logic_vector(7 downto 0) := (others => '0');
  signal wc_hit  : std_logic := '0';
  signal wj_v    : std_logic := '0';                 -- a window job waits
  signal wj_we   : std_logic := '0';
  signal wj_a    : std_logic_vector(15 downto 0) := (others => '0');
  signal wj_d    : std_logic_vector(7 downto 0) := (others => '0');
  signal stall_i : std_logic;
  signal miss_c, stall_c : unsigned(31 downto 0) := (others => '0');
  signal dout_r  : std_logic_vector(7 downto 0) := (others => '0');
  signal d_sel   : std_logic_vector(7 downto 0);
  signal w_done_r : std_logic := '0';
  signal w_rdata_r : std_logic_vector(7 downto 0) := (others => '0');
  signal rd_line_off : integer range 0 to 127;
  -- the ui side
  type us_t is (U_IDLE, U_ISSUE, U_WAITR);
  signal us      : us_t := U_IDLE;
  signal rq_s    : std_logic_vector(2 downto 0) := (others => '0');
  attribute ASYNC_REG of rq_s : signal is "TRUE";
  signal u_addr  : std_logic_vector(26 downto 0) := (others => '0');
  signal u_wr    : std_logic := '0';
  signal u_lane  : unsigned(3 downto 0) := (others => '0');
  signal u_byte  : std_logic_vector(7 downto 0) := (others => '0');
  signal c_en_i, c_wr_i : std_logic := '0';
  signal c_addr_r : std_logic_vector(26 downto 0) := (others => '0');
  signal c_cmd_r : std_logic_vector(2 downto 0) := "000";
  signal c_data_r : std_logic_vector(127 downto 0) := (others => '0');
  signal c_mask_r : std_logic_vector(15 downto 0) := (others => '1');

  -- the DDR2 word address (16 bit units) of a line
  function line_addr (base : std_logic_vector(31 downto 0); line : std_logic_vector(11 downto 0)) return std_logic_vector is
    variable w : unsigned(26 downto 0);
  begin
    w := unsigned(base(27 downto 1)) + shift_left(resize(unsigned(line), 27), 3);
    return std_logic_vector(w);
  end function;
begin
  -- ================================================================ lookup and stall
  hit <= '1' when t_q(4) = '1' and t_q(3 downto 0) = a_qq(15 downto 12) else '0';
  -- the phase that begins at the toggle: the video phase after the CPU's, and the other way round
  a_nxt   <= vid_addr when cpu_phase = '1' else cpu_addr;
  rd_nxt  <= col80 when cpu_phase = '1' else (aux_sel and cpu_ram and not cpu_we);
  wr_nxt  <= (not cpu_phase) and aux_sel and cpu_ram_wr;
  -- the machine waits at the toggle when the byte of the next phase is not there (or the compare is not valid yet), or the write has nowhere to go
  stall_i <= '1' when q3_rise = '1' and
               ((rd_nxt = '1' and not (stab = 3 and hit = '1' and flushing = '0'))
                or (wr_nxt = '1' and not (stab = 3 and flushing = '0' and wq_v = '0')))
             else '0';
  miss_ev <= '1' when q3_rise = '1' and rd_nxt = '1' and stab = 3 and hit = '0' and flushing = '0' else '0';
  stall <= stall_i;
  st_wait <= stall_i;
  st_miss <= std_logic_vector(miss_c);
  st_stall <= std_logic_vector(stall_c);
  dout <= dout_r;
  w_done <= w_done_r;
  w_rdata <= w_rdata_r;

  with a_qq(1 downto 0) select d_sel <=
    d_q(7 downto 0)   when "00",
    d_q(15 downto 8)  when "01",
    d_q(23 downto 16) when "10",
    d_q(31 downto 24) when others;

  -- the arrays: one writer each, the lookup reads
  process (clk)
  begin
    if rising_edge(clk) then
      for i in 0 to 3 loop
        if d_we(i) = '1' then cdata(to_integer(unsigned(d_waddr)))(8 * i + 7 downto 8 * i) <= d_wdata(8 * i + 7 downto 8 * i); end if;
      end loop;
      d_q <= cdata(to_integer(unsigned(word_of(a_q))));
      if t_we = '1' then ctag(to_integer(unsigned(t_waddr))) <= t_wdata; end if;
      t_q <= ctag(to_integer(unsigned(idx_of(a_q))));
    end if;
  end process;

  -- ================================================================ the engine, the lookup bookkeeping
  process (clk)
    variable lane_i : integer range 0 to 15;
    variable w4 : std_logic_vector(31 downto 0);
  begin
    if rising_edge(clk) then
      a_q <= a_nxt; a_qq <= a_q;
      if a_nxt = a_q and a_q = a_qq then
        if stab < 3 then stab <= stab + 1; end if;
      else
        stab <= 0;
      end if;
      d_we <= "0000"; t_we <= '0'; w_done_r <= '0';
      dn_s <= dn_s(1 downto 0) & dn_tgl;
      if stall_i = '1' then stall_c <= stall_c + 1; end if;

      -- the toggle (the next enable is the one that ends the phase and begins the next): keep what the lookup of the next phase's address found
      if ce = '1' and q3_rise = '1' then
        hd <= d_sel; hh <= hit; ha <= a_qq; rd_pend <= rd_nxt; wr_pend <= wr_nxt;
      end if;
      -- the sample, one enable later: the byte; the write
      if samp_en = '1' then
        if rd_pend = '1' then dout_r <= hd; end if;
        if wr_pend = '1' then
          wq_v <= '1'; wq_a <= ha; wq_d <= wdata;
          wc_v <= '1'; wc_a <= ha; wc_d <= wdata; wc_hit <= hh;
        end if;
        rd_pend <= '0'; wr_pend <= '0';
      end if;
      if w_start = '1' then
        wj_v <= '1'; wj_we <= w_we; wj_a <= w_addr; wj_d <= w_wdata;
      end if;

      -- the cache update of a write that hit (a clock after the sample, while the lookup is blind anyway)
      if wc_v = '1' and samp_en = '0' then
        wc_v <= '0';
        if wc_hit = '1' then
          d_waddr <= word_of(wc_a);
          d_wdata <= wc_d & wc_d & wc_d & wc_d;
          case wc_a(1 downto 0) is
            when "00"   => d_we <= "0001";
            when "01"   => d_we <= "0010";
            when "10"   => d_we <= "0100";
            when others => d_we <= "1000";
          end case;
        end if;
        stab <= 0;
      end if;

      case ms is
        when M_IDLE =>
          if flushing = '1' then
            -- invalidate every line (after a change of the image base, and at power-up)
            t_we <= '1'; t_waddr <= std_logic_vector(fl_cnt(7 downto 0)); t_wdata <= "00000";
            if fl_cnt = 255 then flushing <= '0'; else fl_cnt <= fl_cnt + 1; end if;
          elsif base_q /= aux_base then
            base_q <= aux_base; flushing <= '1'; fl_cnt <= (others => '0');
          elsif wq_v = '1' then
            wq_v <= '0';
            rq_addr <= line_addr(base_q, wq_a(15 downto 4));
            rq_lane <= unsigned(wq_a(3 downto 0)); rq_byte <= wq_d; rq_wr <= '1';
            rq_tgl <= not rq_tgl; jt <= J_WR; ms <= M_WR;
          elsif miss_ev = '1' and wc_v = '0' then
            j_line <= a_qq(15 downto 4);
            rq_addr <= line_addr(base_q, a_qq(15 downto 4)); rq_wr <= '0';
            rq_tgl <= not rq_tgl; jt <= J_FILL; ms <= M_RD;
            miss_c <= miss_c + 1;
          elsif wj_v = '1' then
            wj_v <= '0';
            rq_addr <= line_addr(base_q, wj_a(15 downto 4));
            rq_lane <= unsigned(wj_a(3 downto 0)); rq_byte <= wj_d; rq_wr <= wj_we;
            rq_tgl <= not rq_tgl;
            if wj_we = '1' then
              -- the line is about to change under the cache: drop it
              t_we <= '1'; t_waddr <= idx_of(wj_a); t_wdata <= "00000";
              jt <= J_WIN_WR; ms <= M_WR;
            else
              jt <= J_WIN_RD; ms <= M_RD;
            end if;
          end if;
        when M_RD =>
          if dn_s(2) /= dn_s(1) then
            if jt = J_WIN_RD then
              lane_i := to_integer(rq_lane);
              w_rdata_r <= ui_line(8 * lane_i + 7 downto 8 * lane_i);
              w_done_r <= '1'; ms <= M_IDLE;
            else
              ms <= M_FILL; fill_i <= 0;
            end if;
          end if;
        when M_FILL =>
          fill_i <= fill_i + 1;
          if fill_i < 4 then
            w4 := ui_line(32 * (fill_i mod 4) + 31 downto 32 * (fill_i mod 4));
            d_we <= "1111"; d_waddr <= idx_of(j_line & "0000") & std_logic_vector(to_unsigned(fill_i mod 4, 2)); d_wdata <= w4;
          else
            t_we <= '1'; t_waddr <= idx_of(j_line & "0000"); t_wdata <= '1' & j_line(11 downto 8);
            stab <= 0; ms <= M_IDLE;
          end if;
        when M_WR =>
          if dn_s(2) /= dn_s(1) then
            if jt = J_WIN_WR then w_done_r <= '1'; end if;
            ms <= M_IDLE;
          end if;
      end case;
    end if;
  end process;

  -- ================================================================ the ui side
  c_addr <= c_addr_r; c_cmd <= c_cmd_r; c_en <= c_en_i; c_wdf_data <= c_data_r; c_wdf_mask <= c_mask_r; c_wdf_wren <= c_wr_i;
  process (ui_clk)
  begin
    if rising_edge(ui_clk) then
      rq_s <= rq_s(1 downto 0) & rq_tgl;
      if ui_rst = '1' then
        us <= U_IDLE; c_en_i <= '0'; c_wr_i <= '0';
      else
        case us is
          when U_IDLE =>
            if rq_s(2) /= rq_s(1) then
              u_addr <= rq_addr; u_wr <= rq_wr; u_lane <= rq_lane; u_byte <= rq_byte; us <= U_ISSUE;
            end if;
          when U_ISSUE =>
            c_en_i <= '1';
            c_addr_r <= u_addr;
            if u_wr = '1' then
              c_cmd_r <= "000"; c_wr_i <= '1';
              for li in 0 to 15 loop c_data_r(8 * li + 7 downto 8 * li) <= u_byte; end loop;
              c_mask_r <= (others => '1');
              c_mask_r(to_integer(u_lane)) <= '0';
            else
              c_cmd_r <= "001"; c_wr_i <= '0';
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
end architecture;
