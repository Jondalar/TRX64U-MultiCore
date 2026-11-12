--------------------------------------------------------------------------------
-- T133: write-through data cache for the rvlite (docs/specs/T133-host-dcache.md).
--
-- Sits between the core's data port (t_dmem_req / t_dmem_resp, the icache's
-- records) and the data-side bus_converter. Direct mapped, one 32-bit word per
-- line (the CPU's memory bus has no bursts). A hit answers in the clock after
-- the request; a miss, every store, and every access outside the cacheable
-- window are handed to the bus converter as they are.
--
-- Cacheable: adr(31:16) < g_limit. Stores update the cached lanes (sel) when
-- the line is present and never allocate. A read miss fills the line.
--
-- Invalidation (nothing stale may be served):
--   snoop_*     cpu_clk: a write by another master on the memory bus (exact index)
--   ext_tgl     other clock: toggled for every write into the window that is not
--               on the CPU's bus; starts a clear walk of the whole tag RAM
--   a read miss that is open while either arrives cannot leave a stale line: a snooped write keeps its
--               tag write (it has priority over the fill) until the other master's write is taken, which
--               is after the open read returned; the toggle starts the walk, which blocks fills
--   reset       starts the same walk
-- While the walk runs every access is a miss without fill.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

use work.core_pkg.all;

entity rv_dcache is
generic (
    g_size_bits : natural := 12;                                -- bytes = 2**g_size_bits
    g_line_bits : natural := 2;                                 -- log2 of the words per line: 0 = one word (fill from the bus), 2 = 16 bytes (fill from line_i)
    g_limit     : std_logic_vector(31 downto 16) := X"00EA" );  -- cacheable: adr(31:16) < g_limit
port (
    clock       : in  std_logic;
    reset       : in  std_logic;

    dmem_i      : in  t_dmem_req;       -- from the core
    dmem_o      : out t_dmem_resp;      -- to the core

    mem_o       : out t_dmem_req;       -- to the bus converter
    mem_i       : in  t_dmem_resp;

    snoop_valid : in  std_logic := '0'; -- a write by another master is on the memory bus
    snoop_addr  : in  std_logic_vector(25 downto 0) := (others => '0');   -- its byte address
    ext_tgl     : in  std_logic := '0'; -- toggles per write by a master that is not on the CPU's bus (any clock)

    -- debug (JTAG, sys_clk; synchronised here): [0] cache on, [1] snoop on, [2] ext walk on, [3] check mode
    -- (a would-be hit is read from DDR2 anyway and compared), [4] clear toggle, [15:12] counter select
    dbg_ctl     : in  std_logic_vector(15 downto 0) := X"0007";
    -- line fill (g_line_bits = 2): the 128-bit word of the CPU's last DDR2 read and its line address (byte address 25:4), valid while the read's
    -- answer is passed on (cpu_mem_bridge)
    line_i      : in  std_logic_vector(127 downto 0) := (others => '0');
    line_adr_i  : in  std_logic_vector(25 downto 4) := (others => '0');
    -- instruction cache side of the perf counters (rv_icache): stalled this clock, a miss starts, line (byte address 25:4) of the miss
    ic_stall    : in  std_logic := '0';
    ic_miss     : in  std_logic := '0';
    ic_line     : in  std_logic_vector(25 downto 4) := (others => '0');
    dbg_data    : out std_logic_vector(31 downto 0);    -- selected counter (cpu_clk, register)

    stat_hit    : out std_logic_vector(11 downto 0);    -- upper bits of the hit counter
    stat_miss   : out std_logic_vector(11 downto 0) );  -- upper bits of the miss counter
end entity;

architecture arch of rv_dcache is
    constant c_depth : natural := g_size_bits - 2 - g_line_bits;   -- index bits (lines)
    constant c_lw    : natural := 2 ** g_line_bits;                -- words per line
    constant c_w     : natural := 32 * c_lw;                       -- data RAM width
    constant c_tagw  : natural := 24 - g_size_bits;     -- address bits 23 .. g_size_bits

    type t_tagram is array (0 to 2**c_depth - 1) of std_logic_vector(c_tagw downto 0);   -- valid & tag
    type t_dataram is array (0 to 2**c_depth - 1) of std_logic_vector(c_w - 1 downto 0);
    signal tag_ram  : t_tagram := (others => (others => '0'));
    signal data_ram : t_dataram := (others => (others => '0'));
    attribute ram_style : string;
    attribute ram_style of tag_ram  : signal is "distributed";   -- T133: saves a block RAM tile for pb_l1 (the tag RAM is 512 x 13)
    attribute ram_style of data_ram : signal is "block";

    signal rd_adr     : std_logic_vector(c_depth - 1 downto 0);
    signal tag_rdata  : std_logic_vector(c_tagw downto 0) := (others => '0');
    signal data_rdata : std_logic_vector(c_w - 1 downto 0) := (others => '0');
    signal rd_word    : std_logic_vector(31 downto 0);
    signal fill_data  : std_logic_vector(c_w - 1 downto 0);
    signal line_ok    : std_logic;
    signal tag_we     : std_logic;
    signal tag_wadr   : std_logic_vector(c_depth - 1 downto 0);
    signal tag_wdata  : std_logic_vector(c_tagw downto 0);
    signal data_we    : std_logic_vector(c_w / 8 - 1 downto 0);
    signal data_wadr  : std_logic_vector(c_depth - 1 downto 0);
    signal data_wdata : std_logic_vector(c_w - 1 downto 0);

    type t_state is (idle, fill, resp);
    signal state      : t_state := idle;
    signal r          : t_dmem_req := (dat_o => (others => '0'), adr_o => (others => '0'), sel_o => "0000", we_o => '0', ena_o => '0');
    signal rdy        : std_logic;       -- the core's ena_i: a request offered in this clock is taken
    signal r_fillop   : std_logic := '0';
    signal mem_r      : std_logic_vector(31 downto 0) := (others => '0');

    signal r_cache    : std_logic;
    signal r_idx      : std_logic_vector(c_depth - 1 downto 0);
    signal tag_ok     : std_logic;
    signal hit_rd     : std_logic;
    signal hit_wr     : std_logic;
    signal fwd        : std_logic;
    signal fill_we    : std_logic;

    -- snoop, registered; external toggle, synchronised
    signal snoop_v_r  : std_logic := '0';
    signal snoop_a_r  : std_logic_vector(25 downto 0) := (others => '0');
    signal snoop_inv  : std_logic;
    signal ext_s      : std_logic_vector(2 downto 0) := "000";
    attribute async_reg : string;
    attribute async_reg of ext_s : signal is "true";
    signal ext_ev     : std_logic;

    -- clear walk
    signal walk_busy  : std_logic := '1';
    signal walk_busy_d: std_logic := '1';
    signal walk_cnt   : unsigned(c_depth - 1 downto 0) := (others => '0');

    signal hit_cnt    : unsigned(31 downto 0) := (others => '0');
    signal miss_cnt   : unsigned(31 downto 0) := (others => '0');
    -- perf counters (T133, measurement of the cache stalls; cleared with the clear toggle)
    signal r_cacheable: std_logic;      -- address in the cacheable window, whatever the cache switch says
    signal p_clk      : unsigned(31 downto 0) := (others => '0');
    signal p_istall   : unsigned(31 downto 0) := (others => '0');
    signal p_dst_rd   : unsigned(31 downto 0) := (others => '0');
    signal p_dst_ot   : unsigned(31 downto 0) := (others => '0');
    signal p_imiss    : unsigned(31 downto 0) := (others => '0');
    signal p_isame    : unsigned(31 downto 0) := (others => '0');
    signal p_dsame    : unsigned(31 downto 0) := (others => '0');
    signal p_iline    : std_logic_vector(25 downto 4) := (others => '1');
    signal p_dline    : std_logic_vector(25 downto 4) := (others => '1');
    signal dmiss_ev   : std_logic;

    -- debug: control (synchronised), counters, first mismatch of the check mode
    signal ctl_s1     : std_logic_vector(15 downto 0) := (others => '0');
    signal ctl_s2     : std_logic_vector(15 downto 0) := (others => '0');
    attribute async_reg of ctl_s1 : signal is "true";
    attribute async_reg of ctl_s2 : signal is "true";
    signal en_d       : std_logic := '0';
    signal clr_d      : std_logic := '0';
    signal clr_ev     : std_logic;
    signal en_on, snoop_on, ext_on, chk_on : std_logic;
    signal hit_raw    : std_logic;
    signal walk_go    : std_logic;
    signal mm_cnt     : unsigned(15 downto 0) := (others => '0');
    signal mm_have    : std_logic := '0';
    signal mm_adr     : std_logic_vector(31 downto 0) := (others => '0');
    signal mm_cached  : std_logic_vector(31 downto 0) := (others => '0');
    signal mm_real    : std_logic_vector(31 downto 0) := (others => '0');
    signal chk_pend   : std_logic := '0';
    signal chk_data   : std_logic_vector(31 downto 0) := (others => '0');
begin
    ctl_s1  <= dbg_ctl when rising_edge(clock);
    ctl_s2  <= ctl_s1 when rising_edge(clock);
    en_on    <= ctl_s2(0);
    snoop_on <= ctl_s2(1);
    ext_on   <= ctl_s2(2);
    chk_on   <= ctl_s2(3);
    clr_ev   <= ctl_s2(4) xor clr_d;

    ----------------------------------------------------------------------------
    -- RAMs: one read port (every clock the core offers a request), one write port each
    ----------------------------------------------------------------------------
    assert g_line_bits = 0 or g_line_bits = 2 report "rv_dcache: g_line_bits is 0 or 2" severity failure;
    rd_adr <= dmem_i.adr_o(g_size_bits - 1 downto 2 + g_line_bits);

    process(clock)
    begin
        if rising_edge(clock) then
            if dmem_i.ena_o = '1' then
                tag_rdata <= tag_ram(to_integer(unsigned(rd_adr)));
            end if;
            if tag_we = '1' then
                tag_ram(to_integer(unsigned(tag_wadr))) <= tag_wdata;
            end if;
        end if;
    end process;

    process(clock)
    begin
        if rising_edge(clock) then
            if dmem_i.ena_o = '1' then
                data_rdata <= data_ram(to_integer(unsigned(rd_adr)));
            end if;
            for i in 0 to c_w / 8 - 1 loop
                if data_we(i) = '1' then
                    data_ram(to_integer(unsigned(data_wadr)))(8 * i + 7 downto 8 * i) <= data_wdata(8 * i + 7 downto 8 * i);
                end if;
            end loop;
        end if;
    end process;

    ----------------------------------------------------------------------------
    -- lookup of the registered request
    ----------------------------------------------------------------------------
    r_cacheable <= '1' when unsigned(r.adr_o(31 downto 16)) < unsigned(g_limit) else '0';
    dmiss_ev <= '1' when fwd = '1' and r_cacheable = '1' and r.we_o = '0' and hit_raw = '0' else '0';
    r_cache <= '1' when en_on = '1' and unsigned(r.adr_o(31 downto 16)) < unsigned(g_limit) else '0';
    r_idx   <= r.adr_o(g_size_bits - 1 downto 2 + g_line_bits);
    tag_ok  <= '1' when tag_rdata(c_tagw) = '1' and tag_rdata(c_tagw - 1 downto 0) = r.adr_o(23 downto g_size_bits) else '0';

    hit_raw <= '1' when state = idle and r.ena_o = '1' and r.we_o = '0' and r_cache = '1' and tag_ok = '1'
                        and walk_busy = '0' and walk_busy_d = '0' else '0';
    hit_rd  <= hit_raw and not chk_on;      -- check mode: a hit is read from DDR2 anyway
    hit_wr <= '1' when state = idle and r.ena_o = '1' and r.we_o = '1' and r_cache = '1' and tag_ok = '1'
                       and walk_busy = '0' and walk_busy_d = '0' else '0';
    fwd    <= '1' when state = idle and r.ena_o = '1' and hit_rd = '0' else '0';

    -- to the bus converter
    mem_o.adr_o <= r.adr_o;
    mem_o.dat_o <= r.dat_o;
    mem_o.sel_o <= r.sel_o;
    mem_o.we_o  <= r.we_o;
    mem_o.ena_o <= fwd;

    -- to the core
    process(state, fwd, r_fillop, mem_i, rd_word, mem_r)
    begin
        dmem_o.ena_i <= '0';
        dmem_o.dat_i <= rd_word;
        case state is
        when idle =>
            dmem_o.ena_i <= not fwd;
        when fill =>
            dmem_o.ena_i <= mem_i.ena_i and not r_fillop;
            dmem_o.dat_i <= mem_i.dat_i;
        when resp =>
            dmem_o.ena_i <= '1';
            dmem_o.dat_i <= mem_r;
        end case;
    end process;
    rdy <= '1' when (state = idle and fwd = '0') or (state = fill and mem_i.ena_i = '1' and r_fillop = '0') or state = resp else '0';

    ----------------------------------------------------------------------------
    -- invalidation events
    ----------------------------------------------------------------------------
    snoop_inv <= '1' when snoop_on = '1' and snoop_v_r = '1' and unsigned(snoop_a_r(25 downto 16)) < unsigned(g_limit(25 downto 16)) else '0';
    ext_ev    <= ext_s(2) xor ext_s(1);
    walk_go   <= (ext_ev and ext_on) or (en_on and not en_d);

    -- not in a clock where the walk or a snoop owns the tag port: the data word would land under the line's old, still valid tag
    -- (board, T133 check mode with snoop on: cached 0 under the tag of 0x105D5C whose DDR2 word was 0x0004CB64)
    fill_we <= '1' when state = fill and mem_i.ena_i = '1' and r_fillop = '1' and walk_busy = '0' and snoop_inv = '0' and line_ok = '1' else '0';

    -- tag write: the walk, else a snooped write, else a fill
    process(walk_busy, walk_cnt, snoop_inv, snoop_a_r, fill_we, r_idx, r)
    begin
        tag_we    <= '0';
        tag_wadr  <= r_idx;
        tag_wdata <= '1' & r.adr_o(23 downto g_size_bits);
        if walk_busy = '1' then
            tag_we    <= '1';
            tag_wadr  <= std_logic_vector(walk_cnt);
            tag_wdata <= (others => '0');
        elsif snoop_inv = '1' then
            tag_we    <= '1';
            tag_wadr  <= snoop_a_r(g_size_bits - 1 downto 2 + g_line_bits);
            tag_wdata <= (others => '0');
        elsif fill_we = '1' then
            tag_we    <= '1';
        end if;
    end process;

    -- data write: a fill (all lanes) or a store that hit (its lanes)
    data_wadr  <= r_idx;
    process(fill_we, fill_data, r, hit_wr)
        variable w : integer range 0 to 3;
    begin
        data_we <= (others => '0');
        for k in 0 to c_lw - 1 loop
            data_wdata(32 * k + 31 downto 32 * k) <= r.dat_o;       -- a store: its word in every position, only its lanes are written
        end loop;
        if fill_we = '1' then
            data_wdata <= fill_data;
            data_we    <= (others => '1');
        elsif hit_wr = '1' then
            w := 0;
            if g_line_bits = 2 then w := to_integer(unsigned(r.adr_o(3 downto 2))); end if;
            data_we(4 * w + 3 downto 4 * w) <= r.sel_o;
        end if;
    end process;

    -- the word the core asked for, from the line read
    g_rw1: if g_line_bits = 0 generate
        rd_word <= data_rdata;
    end generate;
    g_rw4: if g_line_bits = 2 generate
        rd_word <= data_rdata(31 downto 0)   when r.adr_o(3 downto 2) = "00" else
                   data_rdata(63 downto 32)  when r.adr_o(3 downto 2) = "01" else
                   data_rdata(95 downto 64)  when r.adr_o(3 downto 2) = "10" else
                   data_rdata(127 downto 96);
    end generate;
    g_fill1: if g_line_bits = 0 generate
        fill_data <= mem_i.dat_i;
        line_ok   <= '1';
    end generate;
    g_fill4: if g_line_bits = 2 generate
        fill_data <= line_i;
        line_ok   <= '1' when line_adr_i = r.adr_o(25 downto 4) else '0';   -- the line register belongs to this read
    end generate;

    ----------------------------------------------------------------------------
    process(clock)
    begin
        if rising_edge(clock) then
            -- events
            snoop_v_r <= snoop_valid;
            snoop_a_r <= snoop_addr;
            ext_s     <= ext_s(1 downto 0) & ext_tgl;
            walk_busy_d <= walk_busy;

            -- the request register: a new request is taken when ena_i is 1
            if rdy = '1' then
                r <= dmem_i;
            end if;

            case state is
            when idle =>
                if fwd = '1' then
                    state    <= fill;
                    r_fillop <= r_cache and not r.we_o;
                    chk_pend <= hit_raw;            -- check mode: compare this line with DDR2 when it returns
                    chk_data <= rd_word;
                end if;
            when fill =>
                if mem_i.ena_i = '1' then
                    chk_pend <= '0';
                    if r_fillop = '1' then
                        mem_r <= mem_i.dat_i;
                        state <= resp;
                    else
                        state <= idle;
                    end if;
                end if;
            when resp =>
                state <= idle;
            end case;

            -- the clear walk
            if walk_go = '1' then
                walk_busy <= '1';
                walk_cnt  <= (others => '0');
            elsif walk_busy = '1' then
                walk_cnt <= walk_cnt + 1;
                if walk_cnt = to_unsigned(2**c_depth - 1, c_depth) then
                    walk_busy <= '0';
                end if;
            end if;

            -- counters (cacheable reads), 32 bit; cleared by the toggle in dbg_ctl(4)
            en_d  <= en_on;
            clr_d <= ctl_s2(4);
            if hit_raw = '1' then
                hit_cnt <= hit_cnt + 1;
            end if;
            if dmiss_ev = '1' then
                miss_cnt <= miss_cnt + 1;
                p_dline  <= r.adr_o(25 downto 4);
                if r.adr_o(25 downto 4) = p_dline then
                    p_dsame <= p_dsame + 1;
                end if;
            end if;
            p_clk <= p_clk + 1;
            if ic_stall = '1' then
                p_istall <= p_istall + 1;
            end if;
            if ic_miss = '1' then
                p_imiss <= p_imiss + 1;
                p_iline <= ic_line;
                if ic_line = p_iline then
                    p_isame <= p_isame + 1;
                end if;
            end if;
            if r.ena_o = '1' and rdy = '0' then
                if r_cacheable = '1' and r.we_o = '0' then
                    p_dst_rd <= p_dst_rd + 1;
                else
                    p_dst_ot <= p_dst_ot + 1;
                end if;
            end if;
            if state = fill and mem_i.ena_i = '1' and chk_pend = '1' and mem_i.dat_i /= chk_data then
                mm_cnt <= mm_cnt + 1;
                if mm_have = '0' then
                    mm_have   <= '1';
                    mm_adr    <= r.adr_o;
                    mm_cached <= chk_data;
                    mm_real   <= mem_i.dat_i;
                end if;
            end if;
            if clr_ev = '1' then
                hit_cnt <= (others => '0'); miss_cnt <= (others => '0');
                p_clk <= (others => '0'); p_istall <= (others => '0'); p_dst_rd <= (others => '0'); p_dst_ot <= (others => '0');
                p_imiss <= (others => '0'); p_isame <= (others => '0'); p_dsame <= (others => '0');
                mm_cnt <= (others => '0'); mm_have <= '0';
            end if;

            if reset = '1' then
                state       <= idle;
                r.ena_o     <= '0';
                walk_busy   <= '1';
                walk_cnt    <= (others => '0');
            end if;
        end if;
    end process;

    stat_hit  <= std_logic_vector(hit_cnt(19 downto 8));
    stat_miss <= std_logic_vector(miss_cnt(19 downto 8));

    -- selected word: 0 check mismatches (16 bit), 1/2/3 first mismatch: address, cached word, DDR2 word, else status
    -- {state, mismatch latched, walk busy, chk, ext, snoop, en}
    process(clock)
    begin
        if rising_edge(clock) then
            case ctl_s2(15 downto 12) is
            when X"0" => dbg_data <= X"0000" & std_logic_vector(mm_cnt);
            when X"1" => dbg_data <= mm_adr;
            when X"2" => dbg_data <= mm_cached;
            when X"3" => dbg_data <= mm_real;
            when X"5" => dbg_data <= std_logic_vector(p_clk);
            when X"6" => dbg_data <= std_logic_vector(p_istall);
            when X"7" => dbg_data <= std_logic_vector(p_dst_rd);
            when X"8" => dbg_data <= std_logic_vector(p_dst_ot);
            when X"9" => dbg_data <= std_logic_vector(p_imiss);
            when X"A" => dbg_data <= std_logic_vector(miss_cnt);
            when X"B" => dbg_data <= std_logic_vector(p_isame);
            when X"C" => dbg_data <= std_logic_vector(p_dsame);
            when X"D" => dbg_data <= std_logic_vector(hit_cnt);
            when others =>
                dbg_data <= (others => '0');
                dbg_data(0) <= en_on; dbg_data(1) <= snoop_on; dbg_data(2) <= ext_on; dbg_data(3) <= chk_on;
                dbg_data(4) <= walk_busy; dbg_data(5) <= mm_have;
                dbg_data(7 downto 6) <= std_logic_vector(to_unsigned(t_state'pos(state), 2));
            end case;
        end if;
    end process;
end architecture;
