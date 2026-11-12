--------------------------------------------------------------------------------
-- T28 §7.1: uci_unit (Gideon's command_interface behind our slot adapter and
-- his io_bus_bridge2) between a C64 bus and the app.
--
-- C64 side: 32 c64_clock edges per bus cycle, phi2 high in the second half;
-- the address is the CPU's from the start of the cycle (the VIC's in the first
-- half), the read data is taken on the edge that ends the half, and ev pulses
-- the clock after it with that edge's address/data/we, as c64_core does.
-- App side: a model of command_intf.cc's run_task and copy_result on the IO
-- bus (100 MHz), driven by the UCI's interrupt (ITU irq_in(4)).
--
-- Cases: DOS identify round trip at $DF1C (with the C64 IRQ and the freeze
-- request), a two-part response (DATA_MORE, accept, the second part), an abort
-- in the middle of a response, the reset of a busy interface, and the $DE1C
-- base (slot base 7) with the interface switched off and on.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;

entity tb_uci is
end entity;

architecture sim of tb_uci is
    signal io_clock   : std_logic := '0';
    signal c64_clock  : std_logic := '0';
    signal io_reset   : std_logic := '1';
    signal io_req     : t_io_req := c_io_req_init;
    signal io_resp    : t_io_resp;
    signal io_irq     : std_logic;
    signal bus_addr   : std_logic_vector(15 downto 0) := X"0000";
    signal ev         : std_logic := '0';
    signal ev_addr    : std_logic_vector(15 downto 0) := X"0000";
    signal ev_data    : std_logic_vector(7 downto 0) := X"00";
    signal ev_we      : std_logic := '0';
    signal ev_io      : std_logic := '0';
    signal dout       : std_logic_vector(7 downto 0);
    signal oe         : std_logic;
    signal c64_irq    : std_logic;
    signal freeze     : std_logic;
    signal done       : boolean := false;

    -- the C64 program's bus requests, served by the bus process
    signal c_go       : boolean := false;
    signal c_ack      : boolean := false;
    signal c_addr     : std_logic_vector(15 downto 0);
    signal c_we       : std_logic;
    signal c_wdata    : std_logic_vector(7 downto 0);
    signal c_rdata    : std_logic_vector(7 downto 0);

    -- what the app model is told to answer (set by the C64 program process)
    type t_mode is (m_identify, m_two_parts, m_long);
    signal app_mode   : t_mode := m_identify;
    signal app_seen   : natural := 0;           -- commands the app has taken
    signal app_aborts : natural := 0;
    signal app_ready  : boolean := false;
    signal base_go    : boolean := false;       -- set SLOT_ENABLE, SLOT_BASE
    signal base_ack   : boolean := false;
    signal base_en    : std_logic_vector(7 downto 0) := X"01";
    signal base_val   : std_logic_vector(7 downto 0) := X"47";

    constant IDENT    : string := "ULTIMATE-II DOS V1.2";
    constant STAT_OK  : string := "00,OK";

    function to_slv(c : character) return std_logic_vector is
    begin
        return std_logic_vector(to_unsigned(character'pos(c), 8));
    end function;
begin
    io_clock  <= not io_clock after 5 ns when not done;
    c64_clock <= not c64_clock after 15.859 ns when not done;   -- 31.5278 MHz

    dut: entity work.uci_unit
    port map (
        io_clock => io_clock, io_reset => io_reset, io_req => io_req, io_resp => io_resp,
        io_irq => io_irq, c64_clock => c64_clock, bus_addr => bus_addr, ev => ev,
        ev_addr => ev_addr, ev_data => ev_data, ev_we => ev_we, ev_io => ev_io,
        dout => dout, oe => oe, c64_irq => c64_irq, freeze => freeze );

    -- ---- the C64 bus: one cycle per request (idle cycles in between) ---------
    p_bus: process
        variable ph   : natural := 0;
        variable cur  : std_logic_vector(15 downto 0) := X"0000";
        variable we   : std_logic := '0';
        variable wd   : std_logic_vector(7 downto 0) := X"00";
        variable busy : boolean := false;
        variable io   : std_logic;
        variable rd   : std_logic_vector(7 downto 0);
    begin
        wait until rising_edge(c64_clock);
        ev <= '0';
        if ph = 0 then
            busy := c_go /= c_ack;
            if busy then
                cur := c_addr; we := c_we; wd := c_wdata;
            else
                cur := X"E5A0"; we := '0';      -- a ROM fetch: not I/O
            end if;
        end if;
        if ph < 16 then
            bus_addr <= X"0400";                -- the VIC's half
        else
            bus_addr <= cur;
        end if;
        if ph = 31 then
            -- the edge that ends the CPU half: the read data is taken here
            io := '0';
            if cur(15 downto 9) = "1101111" then io := '1'; end if;
            if oe = '1' and io = '1' then rd := dout; else rd := X"FF"; end if;
            ev      <= '1';
            ev_addr <= cur;
            ev_we   <= we;
            ev_data <= wd;
            ev_io   <= io;
            if busy then
                c_rdata <= rd;
                c_ack   <= c_go;
            end if;
        end if;
        ph := (ph + 1) mod 32;
    end process;

    -- ---- the app: command_intf.cc's ISR and task ------------------------------
    p_app: process
        variable d     : std_logic_vector(7 downto 0);
        variable stat  : std_logic_vector(7 downto 0);
        variable flags : std_logic_vector(7 downto 0);
        variable mask  : std_logic_vector(7 downto 0);
        variable len   : natural;
        variable part  : natural := 0;
        type t_bytes is array(0 to 895) of std_logic_vector(7 downto 0);
        variable cmd   : t_bytes;

        procedure wr(a : natural; v : std_logic_vector(7 downto 0)) is
        begin
            io_req.address <= to_unsigned(a, 24);
            io_req.data    <= v;
            io_req.write   <= '1';
            wait until rising_edge(io_clock);
            io_req.write   <= '0';
            while io_resp.ack /= '1' loop wait until rising_edge(io_clock); end loop;
        end procedure;
        procedure rd(a : natural; v : out std_logic_vector(7 downto 0)) is
        begin
            io_req.address <= to_unsigned(a, 24);
            io_req.read    <= '1';
            wait until rising_edge(io_clock);
            io_req.read    <= '0';
            while io_resp.ack /= '1' loop wait until rising_edge(io_clock); end loop;
            v := io_resp.data;
        end procedure;
        procedure put_string(base : natural; s : string) is
        begin
            for i in s'range loop
                wr(base + i - s'low, to_slv(s(i)));
            end loop;
        end procedure;
        -- copy_result: response and status into the RAM, lengths, validate
        procedure reply(s : string; st : string; last : boolean) is
        begin
            put_string(16#800# + 896, s);
            put_string(16#800# + 1792, st);
            wr(16#D#, std_logic_vector(to_unsigned(s'length / 256, 8)));
            wr(16#C#, std_logic_vector(to_unsigned(s'length mod 256, 8)));
            wr(16#A#, std_logic_vector(to_unsigned(st'length, 8)));
            if last then wr(16#2#, X"10"); else wr(16#2#, X"30"); end if;
        end procedure;
    begin
        wait for 200 ns;
        wait until rising_edge(io_clock);
        io_reset <= '0';
        wait for 200 ns;
        -- the buffer layout the app reads (start/end registers, 8-byte units)
        rd(4, d); assert d = X"00" report "COMMAND_START" severity failure;
        rd(5, d); assert d = X"6F" report "COMMAND_END" severity failure;
        rd(6, d); assert d = X"70" report "RESPONSE_START" severity failure;
        rd(8, d); assert d = X"E0" report "STATUS_START" severity failure;
        -- u64_config.cc / c64.cc and the constructor: enable, $DF1C, reset
        wr(1, X"01");
        wr(0, X"47");
        wr(2, X"87");
        wr(5, X"07");                           -- run_task: IRQMASK_CLEAR = 7
        rd(0, d); assert d = X"46" report "SLOT_BASE readback" severity failure;
        rd(1, d); assert d = X"01" report "SLOT_ENABLE readback" severity failure;
        app_ready <= true;
        loop
            if io_irq /= '1' and base_go = base_ack then
                wait until io_irq = '1' or base_go /= base_ack or done;
            end if;
            exit when done;
            if base_go /= base_ack then
                wr(1, base_en);
                wr(0, base_val);
                base_ack <= base_go;
                wait until rising_edge(io_clock);
                next;
            end if;
            -- ISR: new_flags = STATUSBYTE & ~IRQMASK; IRQMASK_SET = new_flags
            rd(3, stat);
            rd(16#B#, mask);
            flags := stat and not mask;
            wr(4, flags);
            -- task
            if flags(2) = '1' then                          -- abort
                app_aborts <= app_aborts + 1;
                wr(2, X"87");
                wr(5, X"04");
            end if;
            if flags(1) = '1' then                          -- data accepted: more
                part := part + 1;
                if app_mode = m_two_parts then
                    reply("PART TWO", STAT_OK, true);
                else
                    reply("MORE DATA " & integer'image(part), STAT_OK, false);
                end if;
                wr(2, X"02");
                wr(5, X"02");
            end if;
            if flags(0) = '1' then                          -- new command
                rd(16#E#, d); len := to_integer(unsigned(d));
                rd(16#F#, d); len := len + 256 * to_integer(unsigned(d(2 downto 0)));
                for i in 0 to len - 1 loop
                    rd(16#800# + i, cmd(i));
                end loop;
                app_seen <= app_seen + 1;
                wr(2, X"01");                               -- ACCEPT_COMMAND
                part := 0;
                case app_mode is
                when m_identify =>
                    assert len = 2 and cmd(0) = X"01" and cmd(1) = X"01"
                        report "identify command bytes" severity failure;
                    reply(IDENT, STAT_OK, true);
                when m_two_parts =>
                    assert len = 3 and cmd(0) = X"01" and cmd(2) = X"5A"
                        report "second command bytes" severity failure;
                    reply("PART ONE", STAT_OK, false);
                when m_long =>
                    reply("MORE DATA 0", STAT_OK, false);
                end case;
                wr(5, X"01");
            end if;
        end loop;
        wait;
    end process;

    -- ---- the C64 program -----------------------------------------------------
    p_c64: process
        variable d     : std_logic_vector(7 downto 0);
        variable s     : string(1 to 64);
        variable n     : natural;

        procedure c64(a : std_logic_vector(15 downto 0); w : std_logic;
                      v : std_logic_vector(7 downto 0); r : out std_logic_vector(7 downto 0)) is
        begin
            c_addr <= a; c_we <= w; c_wdata <= v;
            c_go   <= not c_go;
            wait until c_ack = c_go;
            r := c_rdata;
        end procedure;
        procedure poke(a : std_logic_vector(15 downto 0); v : std_logic_vector(7 downto 0)) is
            variable dummy : std_logic_vector(7 downto 0);
        begin
            c64(a, '1', v, dummy);
        end procedure;
        procedure peek(a : std_logic_vector(15 downto 0); r : out std_logic_vector(7 downto 0)) is
        begin
            c64(a, '0', X"00", r);
        end procedure;
        -- wait while the state is "processing" (01)
        procedure wait_data(base : std_logic_vector(15 downto 0); st : out std_logic_vector(7 downto 0)) is
            variable c : std_logic_vector(7 downto 0);
        begin
            for i in 0 to 20000 loop
                peek(base, c);
                exit when c(5 downto 4) /= "01";
            end loop;
            assert c(5 downto 4) /= "01" report "no answer" severity failure;
            st := c;
        end procedure;
        -- read the response while bit 7 says there is data
        procedure read_resp(base : std_logic_vector(15 downto 0); maxn : natural;
                            str : out string; cnt : out natural) is
            variable c, b : std_logic_vector(7 downto 0);
            variable k    : natural := 0;
        begin
            loop
                peek(base, c);
                exit when c(7) = '0' or k = maxn;
                peek(std_logic_vector(unsigned(base) + 2), b);
                k := k + 1;
                str(k) := character'val(to_integer(unsigned(b)));
            end loop;
            cnt := k;
        end procedure;
        procedure read_stat(base : std_logic_vector(15 downto 0); str : out string; cnt : out natural) is
            variable c, b : std_logic_vector(7 downto 0);
            variable k    : natural := 0;
        begin
            loop
                peek(base, c);
                exit when c(6) = '0';
                peek(std_logic_vector(unsigned(base) + 3), b);
                k := k + 1;
                str(k) := character'val(to_integer(unsigned(b)));
            end loop;
            cnt := k;
        end procedure;
        variable st : string(1 to 16);
        variable sn : natural;
    begin
        wait until app_ready;
        wait for 1 us;

        -- 1. DOS identify at $DF1C, with the C64 IRQ and the freeze request
        peek(X"DF1C", d);
        assert d(5 downto 4) = "00" report "not idle at start" severity failure;
        peek(X"DF1D", d);
        assert d = X"C9" report "command register idle value" severity failure;
        peek(X"DF1B", d);
        assert d = X"00" report "bus id" severity failure;
        poke(X"DF1D", X"01");                   -- target DOS 1
        poke(X"DF1D", X"01");                   -- DOS_CMD_IDENTIFY
        poke(X"DF1C", X"A1");                   -- push, IRQ wanted, freeze
        wait for 200 ns;
        assert freeze = '1' report "freeze not set by control bit 7" severity failure;
        wait_data(X"DF1C", d);
        assert d(5 downto 4) = "10" report "identify: not DATA_LAST" severity failure;
        assert freeze = '0' report "freeze still set after validate" severity failure;
        assert c64_irq = '1' report "no C64 IRQ with data ready" severity failure;
        read_resp(X"DF1C", 64, s, n);
        assert n = IDENT'length and s(1 to n) = IDENT
            report "identify reply: '" & s(1 to n) & "'" severity failure;
        assert c64_irq = '0' report "C64 IRQ not cleared by the response read" severity failure;
        read_stat(X"DF1C", st, sn);
        assert sn = 5 and st(1 to 5) = STAT_OK report "identify status" severity failure;
        poke(X"DF1C", X"02");                   -- accept: back to idle
        wait for 1 us;
        peek(X"DF1C", d);
        assert d(5 downto 4) = "00" report "not idle after accept" severity failure;
        report "identify round trip: '" & s(1 to n) & "' / '" & st(1 to sn) & "'";

        -- 2. two-part response
        app_mode <= m_two_parts;
        poke(X"DF1D", X"01"); poke(X"DF1D", X"02"); poke(X"DF1D", X"5A");
        poke(X"DF1C", X"01");
        wait_data(X"DF1C", d);
        assert d(5 downto 4) = "11" report "part 1: not DATA_MORE" severity failure;
        read_resp(X"DF1C", 64, s, n);
        assert s(1 to n) = "PART ONE" report "part 1: '" & s(1 to n) & "'" severity failure;
        poke(X"DF1C", X"02");                   -- accept: the app gets DATA_ACCEPTED
        wait_data(X"DF1C", d);
        assert d(5 downto 4) = "10" report "part 2: not DATA_LAST" severity failure;
        read_resp(X"DF1C", 64, s, n);
        assert s(1 to n) = "PART TWO" report "part 2: '" & s(1 to n) & "'" severity failure;
        poke(X"DF1C", X"02");
        wait for 1 us;
        peek(X"DF1C", d);
        assert d(5 downto 4) = "00" report "not idle after two parts" severity failure;
        report "two-part response done";

        -- 3. abort in the middle of a response
        app_mode <= m_long;
        poke(X"DF1D", X"01"); poke(X"DF1D", X"10");
        poke(X"DF1C", X"01");
        wait_data(X"DF1C", d);
        assert d(5 downto 4) = "11" report "long: not DATA_MORE" severity failure;
        read_resp(X"DF1C", 4, s, n);           -- four bytes of it
        assert s(1 to 4) = "MORE" report "long: first bytes" severity failure;
        poke(X"DF1C", X"04");                   -- abort
        peek(X"DF1C", d);
        assert d(2) = '1' or d(5 downto 4) = "00" report "abort flag not set" severity failure;
        assert d(7) = '0' report "response still valid while aborting" severity failure;
        for i in 0 to 20000 loop
            peek(X"DF1C", d);
            exit when d(5 downto 4) = "00" and d(2) = '0';
        end loop;
        assert d(5 downto 4) = "00" and d(2) = '0' report "abort not completed" severity failure;
        assert app_aborts = 1 report "app did not see the abort" severity failure;
        report "abort done";

        -- 4. a push while not idle is an error (bit 3), cleared by writing it
        app_mode <= m_identify;
        poke(X"DF1D", X"01"); poke(X"DF1D", X"01");
        poke(X"DF1C", X"01");
        poke(X"DF1C", X"01");                   -- second push while processing
        peek(X"DF1C", d);
        assert d(3) = '1' report "busy push did not set the error bit" severity failure;
        poke(X"DF1C", X"08");
        peek(X"DF1C", d);
        assert d(3) = '0' report "error bit not cleared" severity failure;
        wait_data(X"DF1C", d);
        read_resp(X"DF1C", 64, s, n);
        poke(X"DF1C", X"02");
        wait for 1 us;
        peek(X"DF1C", d);
        assert d(5 downto 3) = "000" report "not idle after the error case: " & integer'image(to_integer(unsigned(d))) severity failure;
        report "error bit done";

        -- 5. $DE1C: slot base 7 (EasyFlash); switched off nothing answers
        base_val <= X"07"; base_en <= X"01";
        base_go <= not base_go; wait until base_ack = base_go;
        peek(X"DF1C", d);
        assert d = X"FF" report "$DF1C still answers with base 7" severity failure;
        peek(X"DE1C", d);
        assert d(5 downto 4) = "00" and d /= X"FF" report "$DE1C does not answer: " & integer'image(to_integer(unsigned(d))) severity failure;
        poke(X"DE1D", X"01"); poke(X"DE1D", X"01");
        poke(X"DE1C", X"01");
        wait_data(X"DE1C", d);
        read_resp(X"DE1C", 64, s, n);
        assert s(1 to n) = IDENT report "identify at $DE1C: '" & s(1 to n) & "'" severity failure;
        read_stat(X"DE1C", st, sn);
        poke(X"DE1C", X"02");
        report "identify at $DE1C: '" & s(1 to n) & "'";
        base_en <= X"00";
        base_go <= not base_go; wait until base_ack = base_go;
        peek(X"DE1C", d);
        assert d = X"FF" report "$DE1C answers while off" severity failure;
        assert app_seen = 5 report "app saw " & integer'image(app_seen) & " commands" severity failure;

        report "PASS";
        done <= true;
        wait;
    end process;
end architecture;
