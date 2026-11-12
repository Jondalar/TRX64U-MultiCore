-- T133 bench: rv_dcache in front of Gideon's bus_converter (the data side of rvlite_wrapper), a 2-port
-- arbiter with a DMA master, and a memory model that behaves like cpu_mem_bridge + DDR2 (one access
-- open at a time, posted writes, reads answered after 6-20 clocks with the data taken when the read
-- was accepted). The CPU is modelled by the protocol of the core (writeback.vhd: an offered request
-- is taken in a clock where ena_i = 1; its data is valid in a later clock where ena_i = 1; the next
-- request is offered at once, so a hit can complete and the next be taken in the same clock).
-- Output: "ERR:" lines for every wrong value, "RESULT: PASS|FAIL".
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.math_real.all;

library work;
use work.mem_bus_pkg.all;
use work.io_bus_pkg.all;
use work.core_pkg.all;

entity tb_dcache is
generic (
    g_seed    : integer := 1;
    g_batches : integer := 300 );
end entity;

architecture sim of tb_dcache is
    constant c_hi : unsigned(25 downto 0) := to_unsigned(16#EA0000#, 26);   -- first byte outside the window
    type t_words is array (0 to 8191) of std_logic_vector(31 downto 0);

    function init_word(i : integer) return std_logic_vector is
    begin
        return std_logic_vector(to_unsigned((i * 7919 + 12345) mod 65536, 16)) & std_logic_vector(to_unsigned((i * 104729) mod 65536, 16));
    end function;
    function init_mem return t_words is
        variable m : t_words;
    begin
        for i in m'range loop m(i) := init_word(i); end loop;
        return m;
    end function;

    signal clk : std_logic := '0';
    signal rst : std_logic := '1';
    signal c_req  : t_dmem_req := (dat_o => (others => '0'), adr_o => (others => '0'), sel_o => "0000", we_o => '0', ena_o => '0');
    signal c_resp : t_dmem_resp;
    signal b_req  : t_dmem_req;
    signal b_resp : t_dmem_resp;
    signal cpu_req, dma_req, arb_req : t_mem_req_32 := c_mem_req_32_init;
    signal cpu_resp, dma_resp, arb_resp : t_mem_resp_32 := c_mem_resp_32_init;
    signal bram_req : t_bram_req;
    signal bram_resp : t_bram_resp := (data => (others => '0'));
    signal io_req : t_io_req;
    signal io_resp : t_io_resp := c_io_resp_init;
    signal ext_tgl : std_logic := '0';
    signal snoop_valid : std_logic;
    signal stat_hit, stat_miss : std_logic_vector(11 downto 0);

    signal mem : t_words := init_mem;
    signal bd_we : std_logic := '0';
    signal bd_idx : integer range 0 to 8191 := 0;
    signal bd_dat : std_logic_vector(31 downto 0) := (others => '0');
    signal boot_gen : unsigned(7 downto 0) := (others => '0');
    signal io_cnt : unsigned(7 downto 0) := (others => '0');

    signal fwd_rd, fwd_wr : natural := 0;       -- requests the cache handed to the bus converter

    -- DMA agent
    signal dma_cmd, dma_done : std_logic := '0';
    signal dma_addr : unsigned(25 downto 0) := (others => '0');
    signal dma_data : std_logic_vector(31 downto 0) := (others => '0');
    signal dma_be : std_logic_vector(3 downto 0) := "1111";
    signal dma_delay : natural := 0;
    signal ext_cmd, ext_done, ext_toggle : std_logic := '0';
    signal ext_idx : integer range 0 to 8191 := 0;
    signal ext_dat : std_logic_vector(31 downto 0) := (others => '0');
    signal ext_delay : natural := 0;

    function widx(a : unsigned(25 downto 0)) return integer is
    begin
        if a >= c_hi then
            return 4096 + to_integer((a - c_hi) / 4) mod 4096;
        else
            return to_integer(a / 4) mod 4096;
        end if;
    end function;

    function merge(old : std_logic_vector(31 downto 0); d : std_logic_vector(31 downto 0); be : std_logic_vector(3 downto 0))
        return std_logic_vector is
        variable r : std_logic_vector(31 downto 0) := old;
    begin
        for i in 0 to 3 loop
            if be(i) = '1' then r(8 * i + 7 downto 8 * i) := d(8 * i + 7 downto 8 * i); end if;
        end loop;
        return r;
    end function;
begin
    clk <= not clk after 5 ns;

    ----------------------------------------------------------------------------
    -- device under test
    ----------------------------------------------------------------------------
    dut: entity work.rv_dcache
    generic map ( g_line_bits => 0 )       -- the bench's memory model returns words; the 16-byte line fill is tested on the board (T133)
    port map (
        clock => clk, reset => rst,
        dmem_i => c_req, dmem_o => c_resp,
        mem_o => b_req, mem_i => b_resp,
        snoop_valid => snoop_valid, snoop_addr => std_logic_vector(arb_req.address),
        ext_tgl => ext_tgl,
        stat_hit => stat_hit, stat_miss => stat_miss );

    -- as ultimate_sys.vhd: another master's write on the arbiter output (not the CPU's own tags)
    snoop_valid <= arb_req.request when arb_req.read_writen = '0' and arb_req.tag /= X"20" and arb_req.tag /= X"21" else '0';

    bc: entity work.bus_converter
    generic map (g_tag => X"21", g_io_bit => 28, g_bootrom => true, g_bootaddr => X"8000", g_support_io => true)
    port map (
        clock => clk, reset => rst,
        dmem_req => b_req, dmem_resp => b_resp,
        mem_req => cpu_req, mem_resp => cpu_resp,
        bram_req => bram_req, bram_resp => bram_resp,
        io_busy => open, io_req => io_req, io_resp => io_resp );

    arb: entity work.mem_bus_arbiter_pri_32
    generic map (g_registered => false, g_ports => 2)
    port map (
        clock => clk, reset => rst,
        reqs(0) => dma_req, reqs(1) => cpu_req,
        resps(0) => dma_resp, resps(1) => cpu_resp,
        req => arb_req, resp => arb_resp );

    ----------------------------------------------------------------------------
    -- memory behind the bus (cpu_mem_bridge + DDR2): one access open at a time
    ----------------------------------------------------------------------------
    mem_model: process(clk)
        variable seed1, seed2 : positive := 7;
        variable rnd : real;
        variable busy : natural := 0;           -- clocks until the next request is taken
        variable rd_wait : boolean := false;
        variable rd_cnt : natural := 0;
        variable rd_tag : std_logic_vector(7 downto 0);
        variable rd_data : std_logic_vector(31 downto 0);
        variable first : boolean := true;
        variable idx : integer;
        procedure rand(lo, hi : natural; variable v : out natural) is
        begin
            uniform(seed1, seed2, rnd);
            v := lo + integer(floor(rnd * real(hi - lo + 1)));
            if v > hi then v := hi; end if;
        end procedure;
        variable n : natural;
    begin
        if rising_edge(clk) then
            if first then seed1 := 100 + g_seed; seed2 := 200 + g_seed; first := false; end if;
            arb_resp.rack <= '0';
            arb_resp.rack_tag <= X"00";
            arb_resp.dack_tag <= X"00";
            if bd_we = '1' then
                mem(bd_idx) <= bd_dat;
            end if;
            if rd_wait then
                if rd_cnt = 0 then
                    arb_resp.data <= rd_data;
                    arb_resp.dack_tag <= rd_tag;
                    rd_wait := false;
                else
                    rd_cnt := rd_cnt - 1;
                end if;
            elsif busy > 0 then
                busy := busy - 1;
            elsif arb_req.request = '1' and rst = '0' and arb_resp.rack = '0' then
                arb_resp.rack <= '1';
                arb_resp.rack_tag <= arb_req.tag;
                idx := widx(arb_req.address);
                if arb_req.read_writen = '0' then
                    mem(idx) <= merge(mem(idx), arb_req.data, arb_req.byte_en);
                    rand(0, 5, n); busy := n;
                else
                    rd_data := mem(idx);
                    if bd_we = '1' and bd_idx = idx then rd_data := bd_dat; end if;
                    rd_tag := arb_req.tag;
                    rand(6, 20, n); rd_cnt := n; rd_wait := true;
                end if;
            end if;
        end if;
    end process;

    -- I/O: every byte read returns a counter that runs on; the boot ROM answers with an address pattern
    -- and a generation number the bench changes
    io_model: process(clk)
    begin
        if rising_edge(clk) then
            io_resp.ack <= io_req.read or io_req.write;
            if io_req.read = '1' then
                io_resp.data <= std_logic_vector(io_cnt);
                io_cnt <= io_cnt + 1;
            end if;
            bram_resp.data <= std_logic_vector(boot_gen) & X"5A" & std_logic_vector(bram_req.address) & "00";
        end if;
    end process;

    -- what the cache hands to the bus converter
    monitor: process(clk)
    begin
        if rising_edge(clk) then
            if b_req.ena_o = '1' then
                if b_req.we_o = '1' then fwd_wr <= fwd_wr + 1; else fwd_rd <= fwd_rd + 1; end if;
            end if;
        end if;
    end process;

    -- a second master (the MAC's DMA): writes one word when commanded, after dma_delay clocks
    dma_agent: process
        variable seen : std_logic := '0';
    begin
        loop
            wait until rising_edge(clk);
            if dma_cmd /= seen then
                seen := dma_cmd;
                for i in 1 to dma_delay loop wait until rising_edge(clk); end loop;
                dma_req <= (tag => X"12", request => '1', read_writen => '0', address => dma_addr, byte_en => dma_be, data => dma_data);
                loop
                    wait until rising_edge(clk);
                    exit when arb_resp.rack = '1' and arb_resp.rack_tag = X"12";
                end loop;
                dma_req <= c_mem_req_32_init;
                dma_done <= not dma_done;
            end if;
        end loop;
    end process;

    -- a writer that is only known through the toggle (cart_fill, drive, REU, JTAG, engine, video): writes the
    -- model's memory behind the bus and toggles ext_tgl, ext_delay clocks after the command
    ext_agent: process
        variable seen : std_logic := '0';
    begin
        loop
            wait until rising_edge(clk);
            if ext_cmd /= seen then
                seen := ext_cmd;
                for i in 1 to ext_delay loop wait until rising_edge(clk); end loop;
                bd_we <= '1'; bd_idx <= ext_idx; bd_dat <= ext_dat;
                if ext_toggle = '1' then ext_tgl <= not ext_tgl; end if;
                wait until rising_edge(clk);
                bd_we <= '0';
                ext_done <= not ext_done;
            end if;
        end loop;
    end process;

    ----------------------------------------------------------------------------
    -- the test
    ----------------------------------------------------------------------------
    test: process
        variable ref : t_words := init_mem;
        variable errors : natural := 0;
        variable seed1, seed2 : positive := 1;
        variable rnd : real;

        impure function r32 return std_logic_vector is
            variable v : std_logic_vector(31 downto 0);
        begin
            uniform(seed1, seed2, rnd); v(31 downto 16) := std_logic_vector(to_unsigned(integer(floor(rnd * 65535.0)), 16));
            uniform(seed1, seed2, rnd); v(15 downto 0) := std_logic_vector(to_unsigned(integer(floor(rnd * 65535.0)), 16));
            return v;
        end function;
        impure function rint(lo, hi : integer) return integer is
        begin
            uniform(seed1, seed2, rnd);
            return minimum(hi, lo + integer(floor(rnd * real(hi - lo + 1))));
        end function;

        procedure err(s : string) is
        begin
            errors := errors + 1;
            if errors <= 12 then report "ERR: " & s severity note; end if;
        end procedure;

        procedure clocks(n : natural) is
        begin
            for i in 1 to n loop wait until rising_edge(clk); end loop;
        end procedure;

        -- one access, alone (no overlap); returns the clocks from the taking edge to the data edge
        procedure access1(addr : unsigned(31 downto 0); sel : std_logic_vector(3 downto 0); we : std_logic;
                          wdata : std_logic_vector(31 downto 0); rdata : out std_logic_vector(31 downto 0); lat : out natural) is
            variable n : natural := 0;
        begin
            c_req <= (dat_o => wdata, adr_o => std_logic_vector(addr), sel_o => sel, we_o => we, ena_o => '1');
            loop
                wait until rising_edge(clk);
                exit when c_resp.ena_i = '1';
                n := n + 1;
                if n > 3000 then err("timeout before taking " & integer'image(to_integer(addr(23 downto 0)))); exit; end if;
            end loop;
            c_req.ena_o <= '0';
            n := 0;
            loop
                wait until rising_edge(clk);
                n := n + 1;
                if c_resp.ena_i = '1' then rdata := c_resp.dat_i; exit; end if;
                if n > 3000 then err("timeout before the data " & integer'image(to_integer(addr(23 downto 0)))); rdata := (others => 'X'); exit; end if;
            end loop;
            lat := n;
        end procedure;

        procedure ld(addr : natural; expect : std_logic_vector(31 downto 0); what : string) is
            variable d : std_logic_vector(31 downto 0);
            variable l : natural;
        begin
            access1(to_unsigned(addr, 32), "1111", '0', X"00000000", d, l);
            if d /= expect then err(what & ": load " & integer'image(addr) & " got " & to_hstring(d) & " expected " & to_hstring(expect)); end if;
        end procedure;

        procedure st(addr : natural; sel : std_logic_vector(3 downto 0); d : std_logic_vector(31 downto 0)) is
            variable x : std_logic_vector(31 downto 0);
            variable l : natural;
            variable i : integer := widx(to_unsigned(addr, 26));
        begin
            access1(to_unsigned(addr, 32), sel, '1', d, x, l);
            ref(i) := merge(ref(i), d, sel);
        end procedure;

        procedure dma_write(addr : natural; d : std_logic_vector(31 downto 0); be : std_logic_vector(3 downto 0); delay : natural) is
            variable i : integer := widx(to_unsigned(addr, 26));
        begin
            dma_addr <= to_unsigned(addr, 26); dma_data <= d; dma_be <= be; dma_delay <= delay;
            dma_cmd <= not dma_cmd;
            ref(i) := merge(ref(i), d, be);
        end procedure;
        procedure dma_wait is
            variable want : std_logic;
        begin
            clocks(1);
            want := dma_cmd;
            for i in 0 to 400 loop
                exit when dma_done = want;
                clocks(1);
            end loop;
            if dma_done /= want then err("DMA write did not finish"); end if;
        end procedure;

        -- a write by a master the cache is told about only through the toggle (cart_fill, drive, REU, JTAG, engine, video)
        procedure ext_post(addr : natural; d : std_logic_vector(31 downto 0); toggle : boolean; delay : natural) is
            variable i : integer := widx(to_unsigned(addr, 26));
        begin
            ext_idx <= i; ext_dat <= d; ext_delay <= delay;
            if toggle then ext_toggle <= '1'; else ext_toggle <= '0'; end if;
            ref(i) := d;
            clocks(1);
            ext_cmd <= not ext_cmd;
        end procedure;
        procedure ext_wait is
            variable want : std_logic;
        begin
            clocks(1);
            want := ext_cmd;
            for i in 0 to 400 loop
                exit when ext_done = want;
                clocks(1);
            end loop;
            if ext_done /= want then err("external write did not finish"); end if;
        end procedure;
        procedure ext_write(addr : natural; d : std_logic_vector(31 downto 0); toggle : boolean) is
        begin
            ext_post(addr, d, toggle, 0);
            ext_wait;
        end procedure;

        procedure wait_walk is
        begin
            clocks(1100);
        end procedure;

        procedure settle is
        begin
            clocks(40);
        end procedure;

        -- the write-through check: DDR2 (the model) equals the reference
        procedure check_mem(what : string) is
        begin
            clocks(60);
            for i in 0 to 8191 loop
                if mem(i) /= ref(i) then
                    err(what & ": memory word " & integer'image(i) & " is " & to_hstring(mem(i)) & " reference " & to_hstring(ref(i)));
                    exit;
                end if;
            end loop;
        end procedure;

        ------------------------------------------------------------------------
        -- random pipelined batch as the core does it
        ------------------------------------------------------------------------
        type t_op is record
            adr : natural;
            sel : std_logic_vector(3 downto 0);
            we  : std_logic;
            dat : std_logic_vector(31 downto 0);
        end record;

        procedure gen_op(variable o : out t_op) is
            variable a : natural;
            variable k : natural;
        begin
            k := rint(0, 99);
            if k < 70 then a := 4 * rint(0, 255) + 16#100#;          -- hot set
            elsif k < 80 then a := 4 * rint(0, 4095);                   -- all of the low range
            else a := 16#EA0000# + 4 * rint(0, 255);                    -- outside the window
            end if;
            o.adr := a + rint(0, 3);
            o.we := '0';
            o.sel := "1111";
            o.dat := r32;
            k := rint(0, 99);
            if k < 25 then
                o.we := '1';
                o.sel := std_logic_vector(to_unsigned(rint(1, 15), 4));
            else
                case rint(0, 3) is
                when 0 => o.sel := "0001"; when 1 => o.sel := "0100"; when 2 => o.sel := "0011"; when others => o.sel := "1111";
                end case;
            end if;
        end procedure;

        procedure run_batch(n : natural) is
            variable cur : t_op;
            variable pend : boolean := false;
            variable p_exp : std_logic_vector(31 downto 0);
            variable p_adr : natural;
            variable w : natural;
            variable i : integer;
        begin
            for k in 1 to n loop
                gen_op(cur);
                c_req <= (dat_o => cur.dat, adr_o => std_logic_vector(to_unsigned(cur.adr, 32)), sel_o => cur.sel, we_o => cur.we, ena_o => '1');
                w := 0;
                loop
                    wait until rising_edge(clk);
                    if c_resp.ena_i = '1' then
                        if pend then
                            if c_resp.dat_i /= p_exp then
                                err("random: load " & integer'image(p_adr) & " got " & to_hstring(c_resp.dat_i) & " expected " & to_hstring(p_exp));
                            end if;
                            pend := false;
                        end if;
                        i := widx(to_unsigned(cur.adr, 26));
                        if cur.we = '1' then
                            ref(i) := merge(ref(i), cur.dat, cur.sel);
                        else
                            pend := true; p_exp := ref(i); p_adr := cur.adr;
                        end if;
                        exit;
                    end if;
                    w := w + 1;
                    if w > 3000 then err("random: timeout"); exit; end if;
                end loop;
            end loop;
            c_req.ena_o <= '0';
            w := 0;
            loop
                wait until rising_edge(clk);
                if c_resp.ena_i = '1' then
                    if pend and c_resp.dat_i /= p_exp then
                        err("random: last load " & integer'image(p_adr) & " got " & to_hstring(c_resp.dat_i) & " expected " & to_hstring(p_exp));
                    end if;
                    exit;
                end if;
                w := w + 1;
                if w > 3000 then err("random: timeout at the end"); exit; end if;
            end loop;
        end procedure;

        variable d, e : std_logic_vector(31 downto 0);
        variable l : natural;
        variable f0 : natural;
        variable a : natural;
        variable hits_before : natural;
        variable newv : std_logic_vector(31 downto 0);
        variable stale : natural;
    begin
        seed1 := 1000 + g_seed; seed2 := 2000 + g_seed;
        clocks(5);
        rst <= '0';
        clocks(6);

        ------------------------------------------------------------------ S9a: during the first walk nothing hits or fills
        a := 16#0100#;
        f0 := fwd_rd; ld(a, ref(widx(to_unsigned(a, 26))), "walk"); ld(a, ref(widx(to_unsigned(a, 26))), "walk");
        if fwd_rd - f0 /= 2 then err("S9: a load in the walk was served from the cache or filled it (" & integer'image(fwd_rd - f0) & " bus reads, expected 2)"); end if;
        wait_walk;

        ------------------------------------------------------------------ S1/S2: miss, then hit
        a := 16#0200#;
        f0 := fwd_rd;
        access1(to_unsigned(a, 32), "1111", '0', X"00000000", d, l);
        if d /= ref(widx(to_unsigned(a, 26))) then err("S2: miss returned " & to_hstring(d)); end if;
        if fwd_rd - f0 /= 1 then err("S2: the miss made " & integer'image(fwd_rd - f0) & " bus reads"); end if;
        f0 := fwd_rd;
        access1(to_unsigned(a, 32), "1111", '0', X"00000000", d, l);
        if d /= ref(widx(to_unsigned(a, 26))) then err("S1: hit returned " & to_hstring(d)); end if;
        if fwd_rd /= f0 then err("S1: the second load went to the bus"); end if;
        if l /= 1 then err("S1: a hit took " & integer'image(l) & " clocks, expected 1"); end if;
        report "S1/S2 done: hit latency " & integer'image(l) & " clock(s)" severity note;

        ------------------------------------------------------------------ S3: store lanes against a cached line
        a := 16#0300#;
        ld(a, ref(widx(to_unsigned(a, 26))), "S3");
        st(a + 1, "0010", X"0000AB00");
        ld(a, ref(widx(to_unsigned(a, 26))), "S3 byte");
        st(a + 2, "1100", X"CDEF0000");
        ld(a, ref(widx(to_unsigned(a, 26))), "S3 half");
        st(a, "1111", X"12345678");
        f0 := fwd_rd;
        ld(a, X"12345678", "S3 word");
        if fwd_rd /= f0 then err("S3: a load after a store hit went to the bus"); end if;
        check_mem("S3");

        ------------------------------------------------------------------ S4: a store does not allocate
        a := 16#0400#;
        st(a, "1111", X"0BADCAFE");
        f0 := fwd_rd;
        ld(a, X"0BADCAFE", "S4");
        if fwd_rd - f0 /= 1 then err("S4: the load after a store to an absent line made " & integer'image(fwd_rd - f0) & " bus reads, expected 1 (no allocate)"); end if;
        f0 := fwd_rd;
        ld(a, X"0BADCAFE", "S4 again");
        if fwd_rd /= f0 then err("S4: the second load missed"); end if;

        ------------------------------------------------------------------ S5: I/O, boot ROM, outside the window
        f0 := fwd_rd;
        access1(X"10000100", "1111", '0', X"00000000", d, l);
        access1(X"10000100", "1111", '0', X"00000000", e, l);
        if d = e then err("S5: two I/O reads gave the same value " & to_hstring(d) & " (cached)"); end if;
        if fwd_rd - f0 /= 2 then err("S5: I/O reads did not both reach the bus converter"); end if;
        access1(X"80000010", "1111", '0', X"00000000", d, l);
        boot_gen <= boot_gen + 1; clocks(3);
        access1(X"80000010", "1111", '0', X"00000000", e, l);
        if d = e then err("S5: two boot ROM reads gave the same value " & to_hstring(d) & " (cached)"); end if;
        a := 16#EA0000# + 16#40#;
        ld(a, ref(widx(to_unsigned(a, 26))), "S5 outside the window");
        ext_write(a, X"A5A5A5A5", false);             -- a master nobody tells (e.g. the drive's DDR2 image)
        clocks(8);
        ld(a, X"A5A5A5A5", "S5 outside the window, written by a master the cache does not see");
        st(a, "1111", X"5A5A5A5A");
        f0 := fwd_rd;
        ld(a, X"5A5A5A5A", "S5 store outside"); ld(a, X"5A5A5A5A", "S5 store outside");
        if fwd_rd - f0 /= 2 then err("S5: loads outside the window were cached"); end if;

        ------------------------------------------------------------------ S6: a write by another master on the bus
        a := 16#0500#;
        ld(a, ref(widx(to_unsigned(a, 26))), "S6"); ld(a, ref(widx(to_unsigned(a, 26))), "S6");
        dma_write(a, X"DEAD0001", "1111", 0); dma_wait; settle;
        ld(a, X"DEAD0001", "S6 stale after a DMA write");
        a := 16#0504#;
        ld(a, ref(widx(to_unsigned(a, 26))), "S6"); ld(a, ref(widx(to_unsigned(a, 26))), "S6");
        dma_write(a, X"000000EE", "0001", 0); dma_wait; settle;
        ld(a, ref(widx(to_unsigned(a, 26))), "S6 stale after a partial DMA write");

        ------------------------------------------------------------------ S7: a DMA write while a read of the line is open
        stale := 0;
        for dl in 0 to 40 loop
            a := 16#1000# + 4 * dl;
            newv := X"7E570000" or std_logic_vector(to_unsigned(dl, 32));
            dma_write(a, newv, "1111", dl);                 -- starts dl clocks from now
            access1(to_unsigned(a, 32), "1111", '0', X"00000000", d, l);   -- a miss, the read goes to the bus at about the same time
            dma_wait; settle;
            f0 := fwd_rd;
            access1(to_unsigned(a, 32), "1111", '0', X"00000000", d, l);
            if d /= newv then stale := stale + 1; err("S7: delay " & integer'image(dl) & ": after the DMA write the load returned " & to_hstring(d) & " expected " & to_hstring(newv)); end if;
        end loop;

        ------------------------------------------------------------------ S8: the toggle clears the cache
        a := 16#0600#;
        for k in 0 to 3 loop
            ld(a + 4 * k, ref(widx(to_unsigned(a + 4 * k, 26))), "S8"); ld(a + 4 * k, ref(widx(to_unsigned(a + 4 * k, 26))), "S8");
        end loop;
        ext_write(a, X"C0FFEE00", true);
        clocks(8);
        ld(a, X"C0FFEE00", "S8 stale after a toggle");
        f0 := fwd_rd;
        ld(a + 4, ref(widx(to_unsigned(a + 4, 26))), "S8 neighbour"); ld(a + 8, ref(widx(to_unsigned(a + 8, 26))), "S8 neighbour");
        if fwd_rd - f0 /= 2 then err("S8: during the walk loads were served from the cache or filled it"); end if;
        wait_walk;
        f0 := fwd_rd;
        ld(a + 4, ref(widx(to_unsigned(a + 4, 26))), "S8 after the walk");
        if fwd_rd - f0 /= 1 then err("S8: after the walk the first load was not a miss (" & integer'image(fwd_rd - f0) & ")"); end if;
        f0 := fwd_rd;
        ld(a + 4, ref(widx(to_unsigned(a + 4, 26))), "S8 after the walk");
        if fwd_rd /= f0 then err("S8: the cache did not fill again after the walk"); end if;
        -- a toggle while a read is open, every phase
        for dl in 0 to 30 loop
            wait_walk;
            a := 16#2000# + 4 * dl;
            newv := X"E0000000" or std_logic_vector(to_unsigned(dl, 32));
            ext_post(a, newv, true, dl);
            access1(to_unsigned(a, 32), "1111", '0', X"00000000", d, l);      -- a miss, in flight while the writer acts
            ext_wait;
            wait_walk;                      -- the walk bypasses the cache; a line that was filled in it shows only after
            access1(to_unsigned(a, 32), "1111", '0', X"00000000", d, l);
            if d /= newv then err("S8: delay " & integer'image(dl) & ": after the toggle the load returned " & to_hstring(d) & " expected " & to_hstring(newv)); end if;
        end loop;

        ------------------------------------------------------------------ S9b: reset leaves nothing valid
        wait_walk;
        a := 16#0700#;
        ld(a, ref(widx(to_unsigned(a, 26))), "S9"); ld(a, ref(widx(to_unsigned(a, 26))), "S9");
        rst <= '1'; clocks(3);
        ext_write(a, X"00C0DE00", false);                 -- changed while the CPU was in reset (JTAG window)
        clocks(2);
        rst <= '0'; clocks(6);
        ld(a, X"00C0DE00", "S9 stale after a reset");
        wait_walk;

        ------------------------------------------------------------------ S10: random
        for b in 1 to g_batches loop
            run_batch(rint(1, 24));
            case rint(0, 19) is
            when 0 =>
                a := 4 * rint(0, 4095);
                dma_write(a, r32, std_logic_vector(to_unsigned(rint(1, 15), 4)), rint(0, 3)); dma_wait; settle;
            when 1 =>
                a := 4 * rint(0, 255) + 16#100#;
                ext_write(a, r32, true); clocks(8);
            when 2 =>
                a := 16#EA0000# + 4 * rint(0, 255);
                ext_write(a, r32, false); clocks(4);
            when 3 =>
                clocks(rint(1, 30));
            when others => null;
            end case;
        end loop;
        check_mem("S10");

        if errors = 0 then
            report "RESULT: PASS (dcache, seed " & integer'image(g_seed) & ", " & integer'image(g_batches) & " batches)" severity note;
        else
            report "RESULT: FAIL (" & integer'image(errors) & " errors)" severity note;
        end if;
        std.env.stop;
        wait;
    end process;
end architecture;
