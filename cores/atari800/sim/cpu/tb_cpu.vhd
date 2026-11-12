-- SPDX-License-Identifier: GPL-3.0-or-later
-- T88: bench for cores/atari800/cpu.vhd (the 6502 of the Atari core) the way the Atari core drives
-- it: a global clock enable CE (9 of 10 sys_clk), a once-per-machine-cycle pulse (ENMODE 1: every 16th CE tick, as
-- shared_enable), MEMORY_READY wait states with the "extra enable" of shared_enable (MEMMODE 1),
-- RDY from ANTIC (RDYMODE), and junk on DI whenever the memory is not ready.
-- 64 KB RAM, loaded from a hex file (65,536 lines of two hex digits). A "trap" (an opcode fetch at the
-- address of the previous opcode fetch: jmp *, bne *) ends the run: PASS when it is at SUCCESS.
-- TRACE writes one line for every enabled cycle:  cyc addr rw data sync adv   (adv 0 = the cycle was
-- stopped by RDY and repeats).
-- IPORT: Klaus Dormann's interrupt feedback register: a write to that address drives IRQ (bit 0)
-- and NMI (bit 1) of the CPU, active while the bit is set.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.textio.all;
use ieee.std_logic_textio.all;
use std.env.all;

entity tb_cpu is
generic (
    IMAGE   : string  := "";
    TRACE   : string  := "";
    MAXCYC  : natural := 200000000;   -- advancing CPU cycles
    CEMODE  : natural := 1;           -- 0 CE always, 1 CE 9 of 10
    ENMODE  : natural := 0;           -- 0 a cycle on every CE tick, 1 a cycle every 16th CE tick
    MEMMODE : natural := 0;           -- 0 memory always ready, 1 random wait states
    RDYMODE : natural := 0;           -- 0 RDY high, 1 random bursts, 2 mostly low, 3 9 low per 114 ticks, 4 9 low per 115 ticks
    SEED    : natural := 1;
    SUCCESS : integer := -1;
    RESADDR : integer := -1;          -- a byte that must be 0 at the trap (Bruce Clark's ERROR)
    IPORT   : integer := -1 );
end tb_cpu;

architecture sim of tb_cpu is
    type ram_t is protected
        procedure load(fname : string);
        impure function rd(a : natural) return std_logic_vector;
        procedure wr(a : natural; d : std_logic_vector(7 downto 0));
    end protected;

    type ram_t is protected body
        type mem_t is array (0 to 65535) of std_logic_vector(7 downto 0);
        variable m : mem_t := (others => x"00");
        procedure load(fname : string) is
            file f : text;
            variable l : line;
            variable b : std_logic_vector(7 downto 0);
            variable i : natural := 0;
        begin
            file_open(f, fname, read_mode);
            while not endfile(f) and i < 65536 loop
                readline(f, l);
                hread(l, b);
                m(i) := b;
                i := i + 1;
            end loop;
            file_close(f);
        end procedure;
        impure function rd(a : natural) return std_logic_vector is
        begin
            return m(a);
        end function;
        procedure wr(a : natural; d : std_logic_vector(7 downto 0)) is
        begin
            m(a) := d;
        end procedure;
    end protected body;

    shared variable ram : ram_t;

    signal clk, reset : std_logic := '0';
    signal ce, pulse, en_c, thr, rdy, mem_rdy : std_logic := '0';
    signal di, do_s : std_logic_vector(7 downto 0);
    signal a_s : std_logic_vector(15 downto 0);
    signal rw_n, sync, fetch : std_logic;
    signal irq_n, nmi_n : std_logic;
    signal port_val : std_logic_vector(7 downto 0) := x"00";
    signal wr_tick : std_logic := '0';
    signal extra : std_logic := '0';
    signal lfsr : unsigned(31 downto 0) := x"ACE1ACE1";
begin
    clk <= not clk after 5 ns;

    dut : entity work.cpu
    port map (
        CLK => clk, RESET => reset, ENABLE => en_c, CE => ce, DI => di, IRQ_n => irq_n, NMI_n => nmi_n,
        MEMORY_READY => mem_rdy, THROTTLE => thr, RDY => rdy, DO => do_s, A => a_s, R_W_n => rw_n,
        CPU_FETCH => fetch, SYNC => sync );

    en_c <= '1';
    -- Dormann's feedback register is a latch: its output follows the data of the write cycle itself (the
    -- 6502 sees the new level at the end of that cycle), and holds it after
    irq_n <= not do_s(0) when (to_integer(unsigned(a_s)) = IPORT and rw_n = '0') else not port_val(0);
    nmi_n <= not do_s(1) when (to_integer(unsigned(a_s)) = IPORT and rw_n = '0') else not port_val(1);
    -- shared_enable: a cycle is the pulse or an extra enable left over from a cycle that met no ready memory
    thr  <= pulse or extra;

    -- memory: junk on DI while it is not ready and on a read that RDY stops
    process (a_s, mem_rdy, wr_tick, rdy, rw_n)
    begin
        if mem_rdy = '1' and (rdy = '1' or rw_n = '0') then
            di <= ram.rd(to_integer(unsigned(a_s)));
        else
            di <= x"A5" xor a_s(7 downto 0);
        end if;
    end process;

    process
        file tf : text;
        variable l : line;
        variable cyc : natural := 0;
        variable clks : natural := 0;
        variable cnt10 : natural := 0;
        variable cnt16 : natural := 0;
        variable n_stall : natural := 0;
        variable n_wr_stall : natural := 0;
        variable lastsync : integer := -1;
        variable adv, ev : boolean;
        variable ce_v, thr_v : std_logic;
        variable lf : unsigned(31 downto 0) := to_unsigned(SEED, 32) xor x"ACE1ACE1";
        variable r : std_logic;
        variable rdy_v : std_logic := '1';
        variable rdy_run : natural := 0;
        variable cnt_rdy : natural := 0;
        variable a_i : natural;
        variable d_v : std_logic_vector(7 downto 0);
        variable fin : boolean := false;
    begin
        ram.load(IMAGE);
        if TRACE'length > 0 then
            file_open(tf, TRACE, write_mode);
        end if;
        reset <= '1';
        for i in 1 to 40 loop
            wait until rising_edge(clk);
        end loop;
        reset <= '0';
        loop
            wait until rising_edge(clk);
            clks := clks + 1;
            -- the cycle that this edge ends, from the signals as they were before the edge
            ce_v  := ce;
            thr_v := thr;
            ev := (ce = '1' and thr = '1' and mem_rdy = '1' and reset = '0');
            if ev then
                adv := (rdy = '1') or (rw_n = '0');
                a_i := to_integer(unsigned(a_s));
                if rw_n = '0' then d_v := do_s; else d_v := di; end if;
                if TRACE'length > 0 then
                    write(l, cyc); write(l, string'(" "));
                    hwrite(l, a_s); write(l, string'(" "));
                    if rw_n = '1' then write(l, string'("R ")); else write(l, string'("W ")); end if;
                    hwrite(l, d_v); write(l, string'(" "));
                    if sync = '1' then write(l, string'("1 ")); else write(l, string'("0 ")); end if;
                    if adv then write(l, string'("1")); else write(l, string'("0")); end if;
                    writeline(tf, l);
                end if;
                if adv then
                    cyc := cyc + 1;
                    if rw_n = '0' then
                        ram.wr(a_i, do_s);
                        wr_tick <= not wr_tick;
                        if a_i = IPORT then
                            port_val <= do_s;
                        end if;
                    end if;
                    if sync = '1' then
                        if a_i = lastsync then
                            if a_i = SUCCESS and (RESADDR < 0 or ram.rd(RESADDR) = x"00") then
                                report "PASS trap at " & integer'image(a_i) & " cycles " & integer'image(cyc) &
                                       " clks " & integer'image(clks) & " stalls " & integer'image(n_stall);
                            else
                                report "FAIL trap at " & integer'image(a_i) & " cycles " & integer'image(cyc) severity failure;
                            end if;
                            fin := true;
                        end if;
                        lastsync := a_i;
                    end if;
                else
                    n_stall := n_stall + 1;
                end if;
            end if;
            if cyc >= MAXCYC then
                report "FAIL no trap in " & integer'image(cyc) & " cycles, last sync " & integer'image(lastsync) severity failure;
                fin := true;
            end if;
            exit when fin;

            -- stimulus for the next clock
            lf := lf(30 downto 0) & (lf(31) xor lf(21) xor lf(1) xor lf(0));
            if CEMODE = 0 then
                ce <= '1';
            else
                cnt10 := (cnt10 + 1) mod 10;
                if cnt10 = 9 then ce <= '0'; else ce <= '1'; end if;
            end if;
            -- everything the machine clocks moves on CE ticks only
            if ce = '1' then
                if ENMODE = 0 then
                    pulse <= '1';
                else
                    cnt16 := (cnt16 + 1) mod 16;
                    if cnt16 = 0 then pulse <= '1'; else pulse <= '0'; end if;
                end if;
                extra <= thr_v and not mem_rdy;
                -- ANTIC_RDY
                if RDYMODE = 1 then
                    if rdy_run = 0 then
                        rdy_v := not rdy_v;
                        if rdy_v = '0' then rdy_run := 1 + to_integer(lf(3 downto 0)); else rdy_run := 1 + to_integer(lf(5 downto 2)) * 3; end if;
                    else
                        rdy_run := rdy_run - 1;
                    end if;
                elsif RDYMODE >= 3 then
                    if cnt_rdy >= 40 and cnt_rdy < 49 then rdy_v := '0'; else rdy_v := '1'; end if;
                    cnt_rdy := (cnt_rdy + 1) mod (111 + RDYMODE);
                elsif RDYMODE = 2 then
                    if lf(2 downto 0) = "000" then rdy_v := '1'; else rdy_v := '0'; end if;
                else
                    rdy_v := '1';
                end if;
                rdy <= rdy_v;
            end if;
            if MEMMODE = 1 then
                mem_rdy <= lf(7) or lf(8);
            else
                mem_rdy <= '1';
            end if;
        end loop;
        if TRACE'length > 0 then
            file_close(tf);
        end if;
        finish;
    end process;
end sim;
