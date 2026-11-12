-- T152 D9 / T164 S15: flash_gate.vhd with g_lock in front of the flash model's status registers. The CPU side
-- sends the frames of the host app (flashlock.cc: SPI_FORCE_SS low, bytes, SPI_FORCE_SS high). The model
-- (g_lock) counts as a violation every 0x50 / 0x01 / 0x31 that reaches the chip and is not exactly the lock.
--
-- g_case: 0 the lock the app writes, 1 SR1 values that are not the lock, 2 SR2 values that are not the lock,
-- 3 a second sequence / no enable / the enable spent, 4 a third byte, 5 SRP after SRL, 6 random frames.
-- Every case ends with check: the model saw no violation and its registers are what the case expects.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use ieee.math_real.all;

library work;
use work.io_bus_pkg.all;

entity tb_flash_lock is
generic (
    g_case      : natural := 0;
    g_variant   : natural := 0;
    g_wren_pass : boolean := true;
    g_seed      : positive := 1;
    g_nrand     : natural := 3000 );
end entity;

architecture tb of tb_flash_lock is
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

    signal m_viol, m_xfer, m_exec, m_chk, m_sub, m_hash, m_nsr : natural;
    signal m_wel    : std_logic;
    signal m_op     : std_logic_vector(7 downto 0);
    signal m_addr   : std_logic_vector(23 downto 0);
    signal m_sr1, m_sr2 : std_logic_vector(7 downto 0);

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
        g_low_limit => 0,
        g_4byte     => false,
        g_lock      => true,
        g_cs_gap    => 10 )
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

    i_m: entity work.flash_model
    generic map (
        g_variant   => g_variant,
        g_low_limit => 0,
        g_4byte     => false,
        g_lock      => true,
        g_tdesel    => 50 ns )
    port map (
        cfg => cfg, csn => csn, sck => sck, mosi => mosi, miso => miso,
        hash => m_hash, viol => m_viol, n_xfer => m_xfer, n_exec => m_exec,
        n_chk => m_chk, n_sub => m_sub, wel => m_wel, last_op => m_op,
        last_addr => m_addr, sr1_o => m_sr1, sr2_o => m_sr2, n_srw => m_nsr );

    process
        variable errors : natural := 0;
        variable e_blk  : natural := 0;         -- gate blocked count expected
        variable e_ok   : natural := 0;
        variable nsr_base : natural := 0;       -- status register writes before the last system reset
        variable s1, s2 : positive;
        variable rnd    : real;
        variable x      : std_logic_vector(7 downto 0);

        procedure fail(msg : string) is
        begin
            report msg severity error;
            errors := errors + 1;
        end procedure;

        procedure acc(rdf : boolean; off : natural; d : std_logic_vector(7 downto 0);
                      res : out std_logic_vector(7 downto 0)) is
            variable n : natural := 0;
        begin
            cpu_req.address <= to_unsigned(c_base + off, 24);
            cpu_req.data    <= d;
            if rdf then cpu_req.read <= '1'; else cpu_req.write <= '1'; end if;
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
            variable y : std_logic_vector(7 downto 0);
        begin
            acc(false, off, d, y);
        end procedure;
        procedure rd(off : natural; res : out std_logic_vector(7 downto 0)) is
        begin
            acc(true, off, X"00", res);
        end procedure;
        procedure ctrl(v : natural) is begin wr(8, b(v)); end procedure;
        procedure dat(v : natural) is begin wr(0, b(v)); end procedure;
        procedure dread(res : out std_logic_vector(7 downto 0)) is begin rd(0, res); end procedure;
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
            e_blk := 0;
            e_ok  := 0;
            nsr_base := m_nsr;
            ctrl(3); dat(16#FF#); ctrl(1); dat(16#FF#); ctrl(3);     -- the boot ROM's flush
        end procedure;

        -- frames as FlashSpi in flashlock_board.cc does them
        procedure fr1(a : natural) is
        begin
            ctrl(1); dat(a); ctrl(3);
        end procedure;
        procedure fr2(a, v : natural) is
        begin
            ctrl(1); dat(a); dat(v); ctrl(3);
        end procedure;
        procedure fr3(a, v, w : natural) is
        begin
            ctrl(1); dat(a); dat(v); dat(w); ctrl(3);
        end procedure;
        procedure read_sr(op : natural; res : out std_logic_vector(7 downto 0)) is
        begin
            ctrl(1); dat(op); dread(res); ctrl(3);
        end procedure;

        -- fl_write_sr1 / fl_write_sr2: enable frame, write frame, BUSY poll
        procedure sr1_write(v : natural) is
        begin
            fr1(16#50#); fr2(16#01#, v); read_sr(16#05#, x);
        end procedure;
        procedure sr2_write(v : natural) is
        begin
            fr1(16#50#); fr2(16#31#, v); read_sr(16#05#, x);
        end procedure;

        -- the gate counted n_blk more refusals since the last call
        procedure refused(name : string; n : natural) is
            variable c : std_logic_vector(7 downto 0);
        begin
            e_blk := e_blk + n;
            greg(1, c);
            if to_integer(unsigned(c)) /= e_blk then
                fail(name & ": blocked count " & integer'image(to_integer(unsigned(c))) &
                     ", expected " & integer'image(e_blk));
            end if;
        end procedure;

        procedure passed(name : string; n : natural) is
            variable c : std_logic_vector(7 downto 0);
        begin
            e_ok := e_ok + n;
            greg(6, c);
            if to_integer(unsigned(c)) /= e_ok then
                fail(name & ": allowed count " & integer'image(to_integer(unsigned(c))) &
                     ", expected " & integer'image(e_ok));
            end if;
        end procedure;

        -- the chip as the model has it (ground truth) and as the app reads it back
        procedure check(name : string; sr1, sr2, nsr : natural) is
            variable r : std_logic_vector(7 downto 0);
        begin
            while busy = '1' loop
                wait until rising_edge(clock);
            end loop;
            idle(40);
            if m_viol /= 0 then
                fail(name & ": the chip received " & integer'image(m_viol) & " forbidden commands");
            end if;
            if m_sr1 /= b(sr1) then
                fail(name & ": chip SR1 " & hx(m_sr1) & ", expected " & hx(b(sr1)));
            end if;
            if m_sr2 /= b(sr2) then
                fail(name & ": chip SR2 " & hx(m_sr2) & ", expected " & hx(b(sr2)));
            end if;
            if m_nsr - nsr_base /= nsr then
                fail(name & ": status register writes executed " & integer'image(m_nsr - nsr_base) & ", expected " & integer'image(nsr));
            end if;
            read_sr(16#05#, r);
            if r /= b(sr1) then fail(name & ": SR1 read through the gate " & hx(r)); end if;
            read_sr(16#35#, r);
            if r /= b(sr2) then fail(name & ": SR2 read through the gate " & hx(r)); end if;
            ctrl(1); dat(16#9F#); dread(r); dread(r); dread(r); ctrl(3);
            if r /= X"18" then fail(name & ": JEDEC after the sequence " & hx(r)); end if;
        end procedure;

        impure function urand(n : natural) return natural is
        begin
            uniform(s1, s2, rnd);
            return natural(floor(rnd * real(n))) mod n;
        end function;

        type t_list is array(natural range <>) of natural;
        constant c_bad1 : t_list := (16#B4#, 16#F4#, 16#74#, 16#14#, 16#38#, 16#30#, 16#3C#, 16#24#, 16#00#, 16#FF#, 16#20#, 16#10#, 16#94#);
        constant c_bad2 : t_list := (16#43#, 16#83#, 16#0B#, 16#13#, 16#23#, 16#02#, 16#07#, 16#00#, 16#FF#, 16#82#, 16#C3#, 16#05#);
    begin
        s1 := g_seed;
        s2 := 7 + g_seed * 3;
        reset <= '1';
        cfg   <= '1';
        idle(5);
        reset <= '0';
        cfg   <= '0';
        idle(5);
        ctrl(3); dat(16#FF#); ctrl(1); dat(16#FF#); ctrl(3);

        greg(7, x);
        if x /= X"00" then fail("lock flags after reset " & hx(x)); end if;

        case g_case is
        when 0 =>
            -- flashlock_apply, byte for byte: SR1 = 0x34, SR2 = cur2 (QE) | SRL
            sr1_write(16#34#);
            read_sr(16#05#, x);
            if x /= X"34" then fail("lock: SR1 read back " & hx(x)); end if;
            sr2_write(16#03#);
            check("lock", 16#34#, 16#03#, 2);
            refused("lock", 0);
            passed("lock", 2);
            greg(7, x);
            if x /= X"03" then fail("lock flags " & hx(x)); end if;
            -- bits 1:0 of the SR1 byte are read-only in the chip: the gate lets them through
            sys_reset;
            sr1_write(16#37#);
            sr2_write(16#01#);
            check("lock, QE clear", 16#34#, 16#01#, 2);

        when 1 =>
            for i in c_bad1'range loop
                sr1_write(c_bad1(i));
            end loop;
            check("SR1 values", 16#00#, 16#02#, 0);
            refused("SR1 values", c_bad1'length);
            passed("SR1 values", 0);
            greg(2, x);
            if x /= X"01" then fail("last blocked opcode " & hx(x)); end if;
            -- the refusals did not spend the lock
            sr1_write(16#34#);
            check("SR1 values, then the lock", 16#34#, 16#02#, 1);
            passed("SR1 values, then the lock", 1);

        when 2 =>
            sr1_write(16#34#);
            for i in c_bad2'range loop
                sr2_write(c_bad2(i));
            end loop;
            check("SR2 values", 16#34#, 16#02#, 1);
            refused("SR2 values", c_bad2'length);
            greg(2, x);
            if x /= X"31" then fail("last blocked opcode " & hx(x)); end if;
            sr2_write(16#03#);
            check("SR2 values, then the lock", 16#34#, 16#03#, 2);

        when 3 =>
            -- no enable frame at all
            fr2(16#01#, 16#34#);
            fr2(16#31#, 16#03#);
            check("no enable", 16#00#, 16#02#, 0);
            refused("no enable", 2);
            -- the enable is gone after another command
            fr1(16#50#); read_sr(16#05#, x); fr2(16#01#, 16#34#);
            check("enable spent by a read", 16#00#, 16#02#, 0);
            refused("enable spent by a read", 1);
            -- two enables are one enable
            fr1(16#50#); fr1(16#50#); fr2(16#01#, 16#34#);
            check("double enable", 16#34#, 16#02#, 1);
            passed("double enable", 1);
            -- a second sequence: with its own enable, and without
            sr1_write(16#34#);
            fr2(16#01#, 16#34#);
            fr1(16#50#); fr2(16#01#, 16#34#);
            check("second SR1 sequence", 16#34#, 16#02#, 1);
            refused("second SR1 sequence", 3);
            passed("second SR1 sequence", 0);
            sr2_write(16#03#);
            sr2_write(16#03#);
            fr2(16#31#, 16#03#);
            check("second SR2 sequence", 16#34#, 16#03#, 2);
            refused("second SR2 sequence", 2);
            passed("second SR2 sequence", 1);
            -- a gate reset (the host is configured again) gives the lock back
            sys_reset;
            sr1_write(16#34#);
            sr2_write(16#03#);
            check("after the gate reset", 16#34#, 16#03#, 2);
            passed("after the gate reset", 2);

        when 4 =>
            -- SR1 write with a second data byte would write SR2 (CMP, SRL, LB): the chip must never see it
            fr1(16#50#); fr3(16#01#, 16#34#, 16#43#); read_sr(16#05#, x);
            check("16-bit SR1 write", 16#34#, 16#02#, 1);
            passed("16-bit SR1 write", 1);
            fr1(16#50#); fr3(16#31#, 16#03#, 16#FF#); read_sr(16#05#, x);
            check("SR2 write with a second byte", 16#34#, 16#03#, 2);
            -- 0x01 and 0x31 in their own CS pulse (CTRL 0), alone, or dropped by CTRL before the value
            sys_reset;
            fr1(16#50#); ctrl(0); dat(16#01#);
            fr1(16#50#); ctrl(0); dat(16#31#);
            fr1(16#50#); fr1(16#01#); fr1(16#31#);
            fr1(16#50#); ctrl(1); dat(16#01#); ctrl(3); dat(16#34#); ctrl(3);
            check("no value", 16#00#, 16#02#, 0);

        when 5 =>
            -- SRL first (as an attacker would), then SRP: refused by the gate, not only by the chip
            sr2_write(16#03#);
            sr1_write(16#B4#);
            sr1_write(16#80#);
            fr1(16#50#); fr3(16#01#, 16#B4#, 16#03#);
            check("SRP after SRL", 16#00#, 16#03#, 1);
            sys_reset;
            -- SRP with the 16-bit write that also sets SRL, with and without the enable
            fr1(16#50#); fr3(16#01#, 16#80#, 16#01#);
            fr3(16#01#, 16#80#, 16#01#);
            check("SRP with SRL in one write", 16#00#, 16#02#, 0);

        when others =>
            for n in 1 to g_nrand loop
                case urand(12) is
                when 0 | 1 => fr1(16#50#);
                when 2 | 3 => fr2(16#01#, urand(2) * 16#34# + urand(4) + urand(3) * urand(256));
                when 4 | 5 => fr2(16#31#, urand(2) * 3 + urand(2) * urand(256));
                when 6 => fr3(16#01# + 16#30# * urand(2), urand(256), urand(256));
                when 7 => read_sr(16#05# + 16#30# * urand(2), x);
                when 8 => fr1(16#06#);
                when 9 => fr2(16#01# + 16#30# * urand(2), 16#34# + urand(2) * 16#B0#);
                when 10 => ctrl(0); dat(urand(256));
                when others =>
                    if urand(8) = 0 then
                        sys_reset;
                    else
                        fr1(16#04#);
                    end if;
                end case;
                if n mod 100 = 0 then
                    ctrl(3);
                    while busy = '1' loop
                        wait until rising_edge(clock);
                    end loop;
                    idle(40);
                    if m_viol /= 0 then
                        fail("random " & integer'image(n) & ": the chip received " & integer'image(m_viol) & " forbidden commands");
                        exit;
                    end if;
                end if;
            end loop;
            ctrl(3);
            idle(60);
            if m_viol /= 0 then
                fail("random: the chip received " & integer'image(m_viol) & " forbidden commands");
            end if;
            report "random: " & integer'image(g_nrand) & " frames, " & integer'image(m_xfer) & " transfers, " &
                   integer'image(m_nsr) & " status register writes executed, " & integer'image(m_sub) & " substituted";
            if m_nsr = 0 then fail("random: no status register write reached the chip, the case tests nothing"); end if;
        end case;

        if errors = 0 then
            report "PASS" severity note;
        else
            report "FAIL: " & integer'image(errors) & " errors" severity failure;
        end if;
        done <= true;
        wait;
    end process;
end architecture;
