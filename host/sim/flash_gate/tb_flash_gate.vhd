-- T44 §7: flash_gate.vhd with Gideon's spi_peripheral_io (as i_flash in
-- ultimate_sys.vhd: fixed rate 1, no CRC) and three flash models on the same
-- pins. The CPU side issues IO bus byte accesses as bus_converter does (strobe
-- for one clock, the next one at the earliest in the clock after the ack; a
-- 32-bit access is four byte accesses at +0..+3).
--
-- g_only = 0 runs every test, n runs test n only. Each test ends with check:
-- the three models saw the same transfers as the bytes the gate forwarded
-- while the pin was low, and none received a forbidden command.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.math_real.all;

library work;
use work.io_bus_pkg.all;

entity tb_flash_gate is
generic (
    g_wren_pass : boolean := false;
    g_4byte     : boolean := false;
    g_cs_gap    : natural := 10;
    g_miso      : natural := 0;             -- the model that drives MISO
    g_tdesel    : natural := 50;            -- model 1: CS-high pulses shorter than this (ns) are missed
    g_seed      : positive := 1;
    g_nrand     : natural := 20000;
    g_only      : natural := 0;
    g_log       : string  := "" );          -- model 0 writes every transfer here
end entity;

architecture tb of tb_flash_gate is
    constant c_base : natural := 16#060200#;
    signal clock    : std_logic := '0';
    signal reset    : std_logic := '1';
    signal cfg      : std_logic := '0';
    signal done     : boolean := false;

    signal cpu_req  : t_io_req := c_io_req_init;
    signal cpu_resp : t_io_resp;
    signal spi_req  : t_io_req;
    signal spi_resp : t_io_resp;
    signal busy     : std_logic;
    signal ssn      : std_logic;
    signal csn      : std_logic;
    signal sck      : std_logic;
    signal mosi     : std_logic;
    signal miso     : std_logic;

    type t_nat3 is array(0 to 2) of natural;
    type t_sl3  is array(0 to 2) of std_logic;
    type t_b3   is array(0 to 2) of std_logic_vector(7 downto 0);
    type t_a3   is array(0 to 2) of std_logic_vector(23 downto 0);
    signal m_miso   : t_sl3;
    signal m_hash, m_viol, m_xfer, m_exec, m_chk, m_sub : t_nat3;
    signal m_wel    : t_sl3;
    signal m_op     : t_b3;
    signal m_addr   : t_a3;
    signal mon_hash : natural := 0;
    signal mon_rst  : std_logic := '0';

    function hx(v : std_logic_vector) return string is
    begin
        return to_hstring(v);
    end function;
    function b(n : natural) return std_logic_vector is
    begin
        return std_logic_vector(to_unsigned(n mod 256, 8));
    end function;
begin
    clock <= not clock after 5 ns when not done;      -- 100 MHz

    i_gate: entity work.flash_gate
    generic map (
        g_wren_pass => g_wren_pass,
        g_low_limit => 16#40#,
        g_4byte     => g_4byte,
        g_cs_gap    => g_cs_gap )
    port map (
        clock     => clock,
        reset     => reset,
        cpu_req   => cpu_req,
        cpu_resp  => cpu_resp,
        spi_req   => spi_req,
        spi_resp  => spi_resp,
        spi_busy  => busy,
        spi_ssn   => ssn,
        flash_csn => csn );

    i_flash: entity work.spi_peripheral_io
    generic map (
        g_fixed_rate => true,
        g_init_rate  => 1,
        g_crc        => false )
    port map (
        clock    => clock,
        reset    => reset,
        io_req   => spi_req,
        io_resp  => spi_resp,
        busy     => busy,
        SPI_SSn  => ssn,
        SPI_CLK  => sck,
        SPI_MOSI => mosi,
        SPI_MISO => miso );

    g_models: for v in 0 to 2 generate
        i_m: entity work.flash_model
        generic map (
            g_variant   => v,
            g_low_limit => 16#40#,
            g_4byte     => g_4byte,
            g_tdesel    => g_tdesel * 1 ns,
            g_log       => g_log )
        port map (
            cfg => cfg, csn => csn, sck => sck, mosi => mosi, miso => m_miso(v),
            hash => m_hash(v), viol => m_viol(v), n_xfer => m_xfer(v), n_exec => m_exec(v),
            n_chk => m_chk(v), n_sub => m_sub(v), wel => m_wel(v), last_op => m_op(v),
            last_addr => m_addr(v) );
    end generate;
    miso <= m_miso(g_miso);

    -- the bytes the gate forwards, clocked while the pin is low, framed by the pin
    process(clock)
        variable h      : natural := 0;
        variable lastb  : natural := 0;
        variable p_csn  : std_logic := '1';
        variable p_busy : std_logic := '0';
        procedure fold(v : natural) is
        begin
            h := (h * 33 + v) mod 16777213;
        end procedure;
    begin
        if rising_edge(clock) then
            if p_csn = '0' and csn = '1' then
                fold(512);
            elsif p_csn = '1' and csn = '0' then
                fold(256);
            end if;
            if p_busy = '0' and busy = '1' and csn = '0' then
                fold(lastb);
            end if;
            if spi_req.address(3 downto 2) = "00" then
                if spi_req.write = '1' then
                    lastb := to_integer(unsigned(spi_req.data));
                elsif spi_req.read = '1' then
                    lastb := 255;
                end if;
            end if;
            p_csn  := csn;
            p_busy := busy;
            mon_hash <= h;
        end if;
    end process;

    process
        variable r      : std_logic_vector(7 downto 0);
        variable e_blk  : natural := 0;         -- expected gate counters
        variable e_ok   : natural := 0;
        variable s_chk, s_sub, s_exec, s_xfer : natural;
        variable errors : natural := 0;
        type t_seen is array(0 to 2) of natural;
        variable viol_seen : t_seen := (0, 0, 0);
        variable s1, s2 : positive;
        variable rnd    : real;

        procedure fail(msg : string) is
        begin
            report msg severity error;
            errors := errors + 1;
        end procedure;

        procedure acc(rd : boolean; off : natural; d : std_logic_vector(7 downto 0);
                      res : out std_logic_vector(7 downto 0)) is
            variable n : natural := 0;
        begin
            cpu_req.address <= to_unsigned(c_base + off, 24);
            cpu_req.data    <= d;
            if rd then cpu_req.read <= '1'; else cpu_req.write <= '1'; end if;
            wait until rising_edge(clock);
            cpu_req.read  <= '0';
            cpu_req.write <= '0';
            loop
                wait until rising_edge(clock);
                exit when cpu_resp.ack = '1';
                n := n + 1;
                assert n < 1000 report "no ack" severity failure;
            end loop;
            res := cpu_resp.data;
        end procedure;

        procedure wr(off : natural; d : std_logic_vector(7 downto 0)) is
            variable x : std_logic_vector(7 downto 0);
        begin
            acc(false, off, d, x);
        end procedure;
        procedure rd(off : natural; res : out std_logic_vector(7 downto 0)) is
        begin
            acc(true, off, X"00", res);
        end procedure;
        procedure ctrl(v : natural) is begin wr(8, b(v)); end procedure;
        procedure dat(v : natural) is begin wr(0, b(v)); end procedure;
        procedure dread(res : out std_logic_vector(7 downto 0)) is begin rd(0, res); end procedure;
        -- SPI_FLASH_DATA_32 = w: four byte stores, low byte first
        procedure st32(w : std_logic_vector(31 downto 0)) is
        begin
            for i in 0 to 3 loop
                wr(i, w(i * 8 + 7 downto i * 8));
            end loop;
        end procedure;
        procedure greg(off : natural; res : out std_logic_vector(7 downto 0)) is
        begin
            rd(16#10# + off, res);
        end procedure;

        procedure idle(n : natural) is
        begin
            for i in 1 to n loop
                wait until rising_edge(clock);
            end loop;
        end procedure;

        -- system reset, between accesses, after the last byte left
        procedure sys_reset is
        begin
            while busy = '1' loop
                wait until rising_edge(clock);
            end loop;
            idle(4);
            reset <= '1';
            cfg   <= '1';
            idle(3);
            reset <= '0';
            cfg   <= '0';
            idle(2);
        end procedure;

        procedure snap is
        begin
            s_chk  := m_chk(0);
            s_sub  := m_sub(0);
            s_exec := m_exec(0);
            s_xfer := m_xfer(0);
        end procedure;

        -- the models agree with each other and with the forwarded stream
        procedure check(name : string) is
        begin
            while busy = '1' loop
                wait until rising_edge(clock);
            end loop;
            idle(g_tdesel / 10 + 20);
            for v in 0 to 2 loop
                if m_viol(v) /= viol_seen(v) then
                    fail(name & ": model " & integer'image(v) & " received forbidden commands: " &
                         integer'image(m_viol(v) - viol_seen(v)));
                    viol_seen(v) := m_viol(v);
                end if;
                if m_hash(v) /= mon_hash then
                    fail(name & ": model " & integer'image(v) & " saw another byte stream than the gate sent");
                end if;
            end loop;
        end procedure;

        -- gate counters against the expected ones; op/addr of the last block
        procedure gate_expect(name : string; op : integer; addr : integer) is
            variable x : std_logic_vector(7 downto 0);
            variable a : std_logic_vector(23 downto 0);
        begin
            greg(1, x);
            if to_integer(unsigned(x)) /= e_blk mod 256 and not (e_blk > 255 and x = X"FF") then
                fail(name & ": blocked count " & integer'image(to_integer(unsigned(x))) &
                     ", expected " & integer'image(e_blk));
            end if;
            greg(6, x);
            if to_integer(unsigned(x)) /= e_ok mod 256 and not (e_ok > 255 and x = X"FF") then
                fail(name & ": allowed count " & integer'image(to_integer(unsigned(x))) &
                     ", expected " & integer'image(e_ok));
            end if;
            if op >= 0 then
                greg(2, x);
                if to_integer(unsigned(x)) /= op then
                    fail(name & ": last blocked opcode " & hx(x) & ", expected " & hx(b(op)));
                end if;
            end if;
            if addr >= 0 then
                greg(3, a(23 downto 16));
                greg(4, a(15 downto 8));
                greg(5, a(7 downto 0));
                if to_integer(unsigned(a)) /= addr then
                    fail(name & ": last blocked address " & hx(a) & ", expected " &
                         hx(std_logic_vector(to_unsigned(addr, 24))));
                end if;
            end if;
        end procedure;

        -- ---- the app's sequences (software/io/flash/*.cc), byte for byte ----
        procedure wait_ready is
            variable x : std_logic_vector(7 downto 0);
        begin
            ctrl(1);
            dat(16#05#);
            for i in 1 to 100 loop
                dread(x);
                exit when x(0) = '0';
            end loop;
            ctrl(3);
        end procedure;

        procedure w25q_tester is
            variable m, t, c : std_logic_vector(7 downto 0);
        begin
            ctrl(3); dat(16#FF#); ctrl(1);
            dat(16#9F#); dread(m); dread(t); dread(c);
            ctrl(3);
            if m & t & c /= X"EF4018" then
                fail("w25q tester: JEDEC ID " & hx(m & t & c));
            end if;
        end procedure;

        procedure s25fl_tester is
            variable m, t, c : std_logic_vector(7 downto 0);
        begin
            ctrl(3); dat(16#FF#);
            ctrl(1);
            dat(16#9F#); dread(m); dread(t); dread(c);
            ctrl(3);
        end procedure;

        procedure s25fl_l_tester is
            variable m, t, c : std_logic_vector(7 downto 0);
        begin
            ctrl(1); dat(16#66#); dat(16#99#); ctrl(3);
            wait for 1 us;
            ctrl(1); dat(16#66#); ctrl(3);
            ctrl(1); dat(16#99#); ctrl(3);
            wait for 1 us;
            ctrl(1); dat(16#FF#); ctrl(3);
            wait for 1 us;
            ctrl(1);
            dat(16#9F#); dread(m); dread(t); dread(c);
            ctrl(3);
        end procedure;

        procedure read_serial is
            variable x : std_logic_vector(7 downto 0);
        begin
            ctrl(1);
            dat(16#4B#);
            st32(X"00000000");
            for i in 0 to 7 loop
                dread(x);
            end loop;
            ctrl(3);
        end procedure;

        function pat(a : natural; i : natural) return natural is
        begin
            return (i * 13 + a / 4096 + a / 256) mod 256;
        end function;

        function init_byte(a : natural) return natural is
        begin
            if a < 16#400000# then
                return ((a / 256) * 7 + a) mod 256;
            end if;
            return 255;
        end function;

        -- read_dev_addr, and compare with what should be there
        procedure read_dev_addr(a : natural; len : natural; programmed : boolean; name : string) is
            variable x : std_logic_vector(7 downto 0);
            variable want : natural;
        begin
            ctrl(1);
            dat(16#03#);
            dat(a / 65536); dat(a / 256); dat(a);
            for i in 0 to len - 1 loop
                dread(x);
                if programmed then
                    want := pat(a - a mod 256, (a mod 256) + i);
                else
                    want := init_byte(a + i);
                end if;
                if to_integer(unsigned(x)) /= want then
                    fail(name & ": read 0x" & hx(std_logic_vector(to_unsigned(a + i, 24))) & " = " &
                         hx(x) & ", expected " & hx(b(want)));
                    exit;
                end if;
            end loop;
            ctrl(3);
        end procedure;

        procedure write_page(a : natural; aligned : boolean) is
            variable w : std_logic_vector(31 downto 0);
        begin
            ctrl(0);
            dat(16#06#);
            ctrl(1);
            dat(16#02#);
            dat(a / 65536); dat(a / 256); dat(a);
            if aligned then
                for i in 0 to 63 loop
                    w := b(pat(a, i * 4 + 3)) & b(pat(a, i * 4 + 2)) & b(pat(a, i * 4 + 1)) & b(pat(a, i * 4));
                    st32(w);
                end loop;
            else
                for i in 0 to 255 loop
                    dat(pat(a, i));
                end loop;
            end if;
            ctrl(3);
            wait_ready;
            ctrl(0);
            dat(16#04#);
            ctrl(3);
        end procedure;

        procedure erase_sector(a : natural) is
        begin
            ctrl(0);
            dat(16#06#);
            ctrl(1);
            dat(16#20#);
            dat(a / 65536); dat(a / 256); dat(a);
            ctrl(3);
            wait_ready;
            ctrl(0);
            dat(16#04#);
            ctrl(3);
            dat(16#00#);
            ctrl(3);
            wait_ready;
            ctrl(0);
            dat(16#04#);
        end procedure;

        -- deltas of model 0 since snap
        procedure model_expect(name : string; chk, sub, exe : natural) is
        begin
            idle(g_tdesel / 10 + 20);
            if m_chk(0) - s_chk /= chk then
                fail(name & ": program/erase opcodes at the chip " & integer'image(m_chk(0) - s_chk) &
                     ", expected " & integer'image(chk));
            end if;
            if m_sub(0) - s_sub /= sub then
                fail(name & ": 0x9F at the chip " & integer'image(m_sub(0) - s_sub) &
                     ", expected " & integer'image(sub));
            end if;
            if m_exec(0) - s_exec /= exe then
                fail(name & ": executed " & integer'image(m_exec(0) - s_exec) &
                     ", expected " & integer'image(exe));
            end if;
        end procedure;

        function wp return natural is
        begin
            if g_wren_pass then return 1; end if;
            return 0;
        end function;

        procedure wren is
        begin
            ctrl(0); dat(16#06#);
        end procedure;
        procedure wrdi is
        begin
            ctrl(0); dat(16#04#);
        end procedure;

        type t_list is array(natural range <>) of natural;
        constant c_allowed   : t_list := (16#400000#, 16#7FF000#, 16#FE8000#, 16#FFF000#);
        constant c_forbidden : t_list := (16#000000#, 16#21F000#, 16#220000#, 16#3FF000#);
        constant c_chk3      : t_list := (16#02#, 16#32#, 16#20#, 16#52#, 16#D8#);
        constant c_op4       : t_list := (16#12#, 16#34#, 16#21#, 16#53#, 16#DC#);
        constant c_blocked   : t_list := (16#C7#, 16#60#, 16#01#, 16#31#, 16#11#, 16#50#, 16#42#,
                                          16#44#, 16#7E#, 16#98#, 16#36#, 16#39#, 16#B7#, 16#E9#,
                                          16#C5#, 16#38#, 16#EB#, 16#BB#, 16#E7#, 16#75#, 16#7A#,
                                          16#B9#, 16#71#, 16#48#, 16#3B#, 16#6B#, 16#00#, 16#FE#,
                                          16#A3#, 16#B0#);
        constant c_interest  : t_list := (16#03#, 16#0B#, 16#05#, 16#35#, 16#15#, 16#9F#, 16#4B#,
                                          16#90#, 16#AB#, 16#5A#, 16#06#, 16#04#, 16#66#, 16#99#,
                                          16#FF#, 16#02#, 16#32#, 16#20#, 16#52#, 16#D8#, 16#12#,
                                          16#34#, 16#21#, 16#53#, 16#DC#, 16#C7#, 16#60#, 16#01#,
                                          16#31#, 16#11#, 16#50#, 16#B7#, 16#38#, 16#EB#, 16#00#,
                                          16#3F#, 16#40#, 16#41#, 16#22#, 16#B9#);
        constant c_edge      : t_list := (16#00#, 16#3F#, 16#40#, 16#FF#, 16#01#, 16#22#);
        constant c_step4     : t_list := (16#C7#, 16#60#, 16#01#, 16#31#, 16#50#);

        impure function urand(n : natural) return natural is
        begin
            uniform(s1, s2, rnd);
            return integer(trunc(rnd * real(n))) mod n;
        end function;
        impure function rbyte return natural is
            variable k : natural;
        begin
            k := urand(10);
            if k < 5 then
                return c_interest(urand(c_interest'length));
            elsif k < 7 then
                return c_edge(urand(c_edge'length));
            end if;
            return urand(256);
        end function;

        function sel(c : boolean; a, bb : natural) return natural is
        begin
            if c then return a; end if;
            return bb;
        end function;

        variable x, y, z : std_logic_vector(7 downto 0);
        variable a, k    : natural;
    begin
        s1 := g_seed;
        s2 := 7 + g_seed * 3;
        reset <= '1';
        cfg   <= '1';
        idle(5);
        reset <= '0';
        cfg   <= '0';
        idle(5);

        -- the boot ROM's flush (bootrom.c)
        ctrl(3); dat(16#FF#); ctrl(1); dat(16#FF#); ctrl(3);
        check("boot flush");

        -- registers
        greg(0, x);
        if x(7 downto 4) /= "0101" or (x(1) = '1') /= g_wren_pass or (x(2) = '1') /= g_4byte or x(3) = '1' then
            fail("status register " & hx(x));
        end if;

        -- ---- 1: the app's sequences ----
        if g_only = 0 or g_only = 1 then
            snap;
            w25q_tester;
            s25fl_tester;
            s25fl_l_tester;
            read_serial;
            wait_ready;
            model_expect("probes", 0, 3, 0);
            gate_expect("probes", -1, -1);
            check("probes");

            for i in c_allowed'range loop
                a := c_allowed(i);
                snap;
                erase_sector(a);
                e_ok := e_ok + 1;
                model_expect("erase allowed", 1, 0, wp);
                write_page(a, true);
                e_ok := e_ok + 1;
                write_page(a + 256, false);
                e_ok := e_ok + 1;
                model_expect("program allowed", 3, 0, 3 * wp);
                gate_expect("allowed", -1, -1);
                read_dev_addr(a, 256, g_wren_pass, "allowed readback");
                read_dev_addr(a + 256, 256, g_wren_pass, "allowed readback");
                check("app at allowed address");
            end loop;

            for i in c_forbidden'range loop
                a := c_forbidden(i);
                snap;
                erase_sector(a);
                e_blk := e_blk + 1;
                model_expect("erase forbidden", 0, 1, 0);
                gate_expect("erase forbidden", 16#20#, a);
                write_page(a, true);
                e_blk := e_blk + 1;
                gate_expect("program forbidden", 16#02#, a);
                write_page(a + 256, false);
                e_blk := e_blk + 1;
                model_expect("program forbidden", 0, 3, 0);
                gate_expect("program forbidden", 16#02#, a + 256);
                read_dev_addr(a, 512, false, "forbidden readback");
                check("app at forbidden address");
            end loop;

            -- the 0x9F the chip got instead also shifted the ID out
            greg(0, x);
            if x(0) /= '1' then
                fail("sticky blocked bit not set");
            end if;
            wr(16#10#, X"01");
            greg(0, x);
            if x(0) /= '0' then
                fail("sticky blocked bit not cleared");
            end if;
        end if;

        -- ---- 2: boundaries, every checked 3-byte opcode, WEL set first ----
        if g_only = 0 or g_only = 2 then
            for i in c_chk3'range loop
                for hi in 16#3F# to 16#40# loop
                    snap;
                    wren;
                    ctrl(1);
                    dat(c_chk3(i)); dat(hi); dat(0); dat(0);
                    if c_chk3(i) = 16#02# or c_chk3(i) = 16#32# then
                        dat(16#A5#); dat(16#5A#);
                    end if;
                    ctrl(3);
                    wait_ready;
                    wrdi;
                    if hi = 16#3F# then
                        e_blk := e_blk + 1;
                        model_expect("boundary 3F", 0, 1, 0);
                        gate_expect("boundary 3F", c_chk3(i), 16#3F0000#);
                    else
                        e_ok := e_ok + 1;
                        model_expect("boundary 40", 1, 0, wp);
                        gate_expect("boundary 40", -1, -1);
                    end if;
                end loop;
            end loop;
            check("boundaries");
        end if;

        -- ---- 3: blocked opcodes alone (CTRL 0) and in a transfer (CTRL 1) ----
        if g_only = 0 or g_only = 3 then
            for i in c_blocked'range loop
                snap;
                wren;
                ctrl(0);
                dat(c_blocked(i));
                e_blk := e_blk + 1;
                gate_expect("blocked CTRL 0", c_blocked(i), 0);
                wren;
                ctrl(1);
                dat(c_blocked(i)); dat(16#12#); dat(16#34#); dat(16#56#);
                ctrl(3);
                e_blk := e_blk + 1;
                gate_expect("blocked CTRL 1", c_blocked(i), 0);
                wrdi;
                model_expect("blocked", 0, 2, 0);
            end loop;
            -- program/erase in their own CS pulse: no address, 0x9F
            for i in c_chk3'range loop
                snap;
                wren;
                ctrl(0);
                dat(c_chk3(i));
                e_blk := e_blk + 1;
                wrdi;
                model_expect("checked alone", 0, 1, 0);
                gate_expect("checked alone", c_chk3(i), 0);
            end loop;
            check("blocked opcodes");
        end if;

        -- ---- 4: 4-byte opcodes ----
        if g_only = 0 or g_only = 4 then
            for i in c_op4'range loop
                for v in 0 to 3 loop
                    snap;
                    wren;
                    ctrl(1);
                    dat(c_op4(i));
                    case v is
                    when 0 => dat(16#00#); dat(16#40#);
                    when 1 => dat(16#00#); dat(16#3F#);
                    when 2 => dat(16#01#); dat(16#40#);
                    when others => dread(x); dat(16#40#);       -- A31..A24 read: 0xFF
                    end case;
                    dat(16#10#); dat(16#00#);
                    if c_op4(i) = 16#12# or c_op4(i) = 16#34# then
                        dat(16#77#);
                    end if;
                    ctrl(3);
                    wait_ready;
                    wrdi;
                    if v = 0 and g_4byte then
                        e_ok := e_ok + 1;
                        model_expect("4-byte allowed", 1, 0, wp);
                        gate_expect("4-byte allowed", -1, -1);
                    else
                        e_blk := e_blk + 1;
                        model_expect("4-byte blocked", 0, 1, 0);
                        case v is
                        when 0 => gate_expect("4-byte blocked", c_op4(i), 16#401000#);
                        when 1 => gate_expect("4-byte blocked", c_op4(i), 16#3F1000#);
                        when 2 => gate_expect("4-byte blocked", c_op4(i), 16#401000#);
                        when others => gate_expect("4-byte blocked", c_op4(i), 16#401000#);
                        end case;
                    end if;
                end loop;
            end loop;
            check("4-byte opcodes");
        end if;

        -- ---- 5: framing attacks ----
        if g_only = 0 or g_only = 5 then
            -- a: CTRL 1 again while an erase is held: the held opcode is dropped,
            --    the next byte is the chip's opcode (here WRSR)
            snap;
            wren;
            ctrl(1); dat(16#20#); ctrl(1); dat(16#01#); dat(16#00#); ctrl(3);
            e_blk := e_blk + 1;
            wrdi;
            model_expect("CTRL 1,1 held", 0, 1, 0);
            gate_expect("CTRL 1,1 held", 16#01#, 0);
            check("CTRL 1,1 held");
            -- a2: CTRL 1,1 in a read: the transfer goes on
            ctrl(1); dat(16#03#); dat(16#40#); ctrl(1); dat(0); dat(0); dread(x); dread(y); ctrl(3);
            check("CTRL 1,1 read");
            -- b: CTRL 3 then 1 back to back after a held erase: the address byte is a new opcode
            snap;
            wren;
            ctrl(1); dat(16#20#); ctrl(3); ctrl(1); dat(16#00#); dat(16#00#); dat(16#00#); ctrl(3);
            e_blk := e_blk + 1;
            wrdi;
            model_expect("CTRL 3,1 held", 0, 1, 0);
            gate_expect("CTRL 3,1 held", 16#00#, 0);
            -- c: CTRL 3 then 1 back to back between a read and an erase
            snap;
            wren;
            ctrl(1); dat(16#05#); dread(x); ctrl(3); ctrl(1);
            dat(16#20#); dat(16#3F#); dat(16#00#); dat(16#00#); ctrl(3);
            e_blk := e_blk + 1;
            wrdi;
            model_expect("CTRL 3,1 gap", 0, 1, 0);
            gate_expect("CTRL 3,1 gap", 16#20#, 16#3F0000#);
            check("CTRL 3,1");
            -- d: CTRL 0 in the middle of a transfer
            snap;
            wren;
            ctrl(1); dat(16#20#); ctrl(0); dat(16#00#); dat(16#40#);
            e_blk := e_blk + 2;
            ctrl(1); dat(16#03#); dat(16#40#); ctrl(0); dat(16#20#);
            e_blk := e_blk + 1;
            wrdi;
            model_expect("CTRL 0 mid", 0, 3, 0);
            gate_expect("CTRL 0 mid", 16#20#, 0);
            check("CTRL 0 mid");
            -- e: a DATA read as the address byte: 0xFF, allowed
            snap;
            ctrl(1); dat(16#20#); dread(x); dat(16#00#); dat(16#00#); ctrl(3);
            e_ok := e_ok + 1;
            idle(g_tdesel / 10 + 20);
            if m_addr(0) /= X"FF0000" then
                fail("read as address: chip got " & hx(m_addr(0)));
            end if;
            wait_ready;
            model_expect("read as address", 1, 0, 0);
            gate_expect("read as address", -1, -1);
            -- f: 32-bit store carrying opcode and address
            snap;
            wren;
            ctrl(1); st32(X"00003F20"); ctrl(3);
            e_blk := e_blk + 1;
            gate_expect("32-bit store 3F", 16#20#, 16#3F0000#);
            ctrl(1); st32(X"00004020"); ctrl(3);
            e_ok := e_ok + 1;
            wait_ready;
            wrdi;
            model_expect("32-bit store", 1, 1, wp);
            gate_expect("32-bit store 40", 16#20#, 16#3F0000#);
            check("32-bit store");
            -- g: system reset with an opcode held
            snap;
            wren;
            ctrl(1); dat(16#20#);
            greg(0, x);
            if x(3) /= '1' then
                fail("held bit not set");
            end if;
            sys_reset;
            e_blk := 0; e_ok := 0;
            -- CTRL is 0 after reset: the first byte is the flush, the others
            -- are opcodes in their own CS pulse
            dat(16#40#); dat(16#00#); dat(16#00#);
            e_blk := 2;
            ctrl(3);
            greg(0, x);
            if x(3) /= '0' then
                fail("held bit after reset");
            end if;
            wrdi;
            model_expect("reset held", 0, 2, 0);
            gate_expect("reset held", 16#00#, 0);
            check("reset held");
            -- h: gate registers read in the middle of a held erase
            snap;
            ctrl(1); dat(16#20#); greg(0, x); greg(1, y); dat(16#40#); dat(0); dat(0); ctrl(3);
            e_ok := e_ok + 1;
            wait_ready;
            model_expect("register read while held", 1, 0, 0);
            -- i: bytes with CS high: not seen, not counted
            snap;
            ctrl(3); dat(16#20#); dat(16#00#); dat(16#06#); dat(16#C7#); dread(x);
            model_expect("CS high", 0, 0, 0);
            gate_expect("CS high", -1, -1);
            check("CS high");
            -- j: start-up flush: right after reset, CTRL 1 and a JEDEC read: the
            --    chip sees none of it; the next transfer is normal
            snap;
            sys_reset;
            e_blk := 0; e_ok := 0;
            ctrl(1); dat(16#9F#); dread(x); dread(y); ctrl(3);
            idle(20);
            if m_xfer(0) /= s_xfer then
                fail("flush: the chip saw the first transfer after reset");
            end if;
            ctrl(1); dat(16#9F#); dread(x); dread(y); dread(z); ctrl(3);
            if x & y & z /= X"EF4018" then
                fail("after flush: JEDEC ID " & hx(x & y & z));
            end if;
            check("flush");
            -- k: SPEED/CRC accesses during a held erase do not drop it
            snap;
            ctrl(1); dat(16#20#); wr(4, X"10"); rd(16#C#, x); wr(16#C#, X"00");
            dat(16#41#); dat(0); dat(0); ctrl(3);
            e_ok := e_ok + 1;
            wait_ready;
            model_expect("SPEED/CRC while held", 1, 0, 0);
            gate_expect("SPEED/CRC while held", -1, -1);
            check("framing");
        end if;

        -- ---- 8: the board's stage 1 (spec §8), WEL forced off first. Run with
        --      g_wren_pass true as well (same stream for test 7; there WREN
        --      sets WEL and the gate is the only barrier, which is what the
        --      board never does) ----
        if g_only = 0 or g_only = 8 then
            wrdi; ctrl(3);
            ctrl(1); dat(16#05#); dread(x); ctrl(3);
            if x(1) /= '0' then
                fail("stage 1: WEL set");
            end if;
            wren; ctrl(3);
            ctrl(1); dat(16#05#); dread(x); ctrl(3);
            if x(1) /= '0' and not g_wren_pass then
                fail("stage 1: WREN set WEL");
            end if;
            ctrl(1); dat(16#20#); dat(16#00#); dread(x); dread(y); ctrl(3);
            e_blk := e_blk + 1;
            if x & y /= X"4018" then
                fail("stage 1 erase 0x00: read " & hx(x & y));
            end if;
            gate_expect("stage 1 erase", 16#20#, 16#00FFFF#);
            ctrl(1); dat(16#02#); dat(16#22#); dread(x); dread(y); ctrl(3);
            e_blk := e_blk + 1;
            if x & y /= X"4018" then
                fail("stage 1 program 0x22: read " & hx(x & y));
            end if;
            ctrl(1); dat(16#D8#); dat(16#3F#); dread(x); dread(y); ctrl(3);
            e_blk := e_blk + 1;
            if x & y /= X"4018" then
                fail("stage 1 erase64 0x3F: read " & hx(x & y));
            end if;
            -- 0x12: blocked at once, or (g_4byte) held and blocked at A23..A16 = 0
            ctrl(1); dat(16#12#); dat(16#00#); dat(16#00#); dread(x); ctrl(3);
            e_blk := e_blk + 1;
            if x /= X"18" then
                fail("stage 1 0x12: read " & hx(x));
            end if;
            for i in c_step4'range loop
                ctrl(0);
                dat(c_step4(i));
                e_blk := e_blk + 1;
            end loop;
            gate_expect("stage 1 step 4", 16#50#, 0);
            ctrl(1); dat(16#20#); dat(16#FE#); dread(x); dread(y); ctrl(3);
            e_ok := e_ok + 1;
            if x & y = X"4018" then
                fail("stage 1 control: allowed erase read as blocked");
            end if;
            gate_expect("stage 1 control", -1, -1);
            check("stage 1");
        end if;

        -- ---- 6: random streams ----
        if g_only = 0 or g_only = 6 then
            for n in 1 to g_nrand loop
                k := urand(1000);
                if k < 200 then
                    case urand(8) is
                    when 0 | 1 | 2 => ctrl(1);
                    when 3 | 4 => ctrl(3);
                    when 5 => ctrl(0);
                    when 6 => ctrl(2);
                    when others => ctrl(urand(256));
                    end case;
                elsif k < 550 then
                    dat(rbyte);
                elsif k < 700 then
                    dread(x);
                elsif k < 750 then
                    st32(b(rbyte) & b(rbyte) & b(rbyte) & b(rbyte));
                elsif k < 800 then
                    for i in 0 to 3 loop
                        rd(i, x);
                    end loop;
                elsif k < 850 then
                    greg(urand(16), x);
                elsif k < 860 then
                    wr(16#10# + urand(16), b(urand(256)));
                elsif k < 880 then
                    rd(4 + 8 * urand(2), x);
                elsif k < 890 then
                    wr(4 + 8 * urand(2), b(urand(256)));
                elsif k < 990 then
                    wren;
                elsif k < 998 then
                    -- the 0x20 at the address: what the gate holds
                    ctrl(1); dat(c_chk3(urand(5))); dat(sel(urand(2) = 0, 16#3F#, 16#40#));
                else
                    sys_reset;
                end if;
                if urand(4) = 0 then
                    idle(urand(4));
                end if;
                if n mod 2000 = 0 then
                    ctrl(3);
                    check("random " & integer'image(n));
                end if;
            end loop;
            ctrl(3);
            check("random");
            report "random: " & integer'image(g_nrand) & " accesses, " & integer'image(m_xfer(0)) &
                   " transfers, " & integer'image(m_chk(0)) & " program/erase at the chip, " &
                   integer'image(m_exec(0)) & " executed, " & integer'image(m_sub(0)) & " substituted";
        end if;

        if errors = 0 then
            report "PASS" severity note;
        else
            report "FAIL: " & integer'image(errors) & " errors" severity failure;
        end if;
        done <= true;
        wait;
    end process;
end architecture;
