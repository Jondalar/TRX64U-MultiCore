--------------------------------------------------------------------------------
-- T97 5.13: the rvlite as the host builds it (rvlite_wrapper = core + I-cache +
-- bus_converter x2 + arbiter, the vendored sources with our patches) running
-- prog.S: two tasks under a FreeRTOS-style trap handler (ecall = yield, level
-- interrupt = tick) with a memory that answers after random wait states, an
-- interrupt that rises at random phases and falls on the handler's IO write
-- (and sometimes by itself).
--
-- A monitor spliced into the core (monitor.inc, splice.py) reads its own signals and checks,
-- with no software in the loop:
--   FETCH   every instruction word decode takes equals the program memory at the
--           PC it travels with;
--   PC      every retired instruction (execute stage advancing on a valid,
--           unflushed instruction) is the PC its predecessor leads to
--           (pc+4, branch/jal target, mtvec after a trap, mepc after mret);
--   TRAP    an ECALL-cause trap retires only on an ECALL word; an interrupt
--           dummy only while MIE = 1;
--   CSR     mstatus.MIE/MPIE and mepc follow a model that is updated by what
--           retires (trap: MPIE=MIE, MIE=0, mepc=pc; mret: MIE=MPIE, MPIE=1;
--           CSR writes); a CSR update that is dropped or applied twice shows.
-- and at the end the tasks' results (compared by run.sh with a run without
-- interrupts) and the software's own MIE check (FAILMIE).
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.textio.all;
use std.env.all;
use work.core_pkg.all;
use work.mem_bus_pkg.all;
use work.io_bus_pkg.all;
use work.rvtrap_pkg.all;

entity tb_rvtrap is
generic (
    g_prog       : string  := "prog.hex";
    g_seed       : natural := 1;
    g_irq_period : natural := 24;   -- mean cycles between interrupt assertions, 0 = none
    g_waits      : natural := 3;    -- 0..g_waits extra cycles on memory and IO
    g_max        : natural := 4000000;
    g_quiet      : boolean := false
);
end entity;

architecture sim of tb_rvtrap is
    constant c_words : natural := 16384;
    type t_mem is array(0 to c_words - 1) of std_logic_vector(31 downto 0);

    impure function load_prog return t_mem is
        file f : text open read_mode is g_prog;
        variable l : line;
        variable m : t_mem := (others => X"00000000");
        variable i : natural := 0;
        variable w : std_logic_vector(31 downto 0);
    begin
        while not endfile(f) loop
            readline(f, l);
            hread(l, w);
            m(i) := w;
            i := i + 1;
        end loop;
        return m;
    end function;

    signal ram      : t_mem := load_prog;
    signal clock    : std_logic := '0';
    signal reset    : std_logic := '1';
    signal irq      : std_logic := '0';
    signal io_req   : t_io_req;
    signal io_resp  : t_io_resp := c_io_resp_init;
    signal io_busy  : std_logic;
    signal mreq     : t_mem_req_32;
    signal mresp    : t_mem_resp_32 := c_mem_resp_32_init;
    signal dbg_pc   : std_logic_vector(31 downto 0);
    signal cycles   : natural := 0;
    signal lfsr     : unsigned(31 downto 0) := X"1ACE5EED" xor to_unsigned(g_seed * 40503 + 1, 32);
    signal ack_clr  : std_logic := '0';

    signal rack_ok  : std_logic := '0';
    signal dack_r   : std_logic_vector(7 downto 0) := X"00";
    signal ndone    : natural := 0;
    signal n_irq    : natural := 0;

begin
    clock <= not clock after 5 ns;
    reset <= '0' after 55 ns;

    dut: entity work.rvlite_wrapper
    generic map (
        g_mult       => true,
        g_start_addr => X"00000000",
        g_bootrom    => false,
        g_icache     => true )
    port map (
        clock => clock, reset => reset, irq_i => irq,
        io_req => io_req, io_resp => io_resp, io_busy => io_busy,
        mem_req => mreq, mem_resp => mresp, dbg_pc => dbg_pc );


    process(clock)
    begin
        if rising_edge(clock) then
            cycles <= cycles + 1;
            lfsr <= lfsr(30 downto 0) & (lfsr(31) xor lfsr(21) xor lfsr(1) xor lfsr(0));
        end if;
    end process;

    ----------------------------------------------------------------------------
    -- memory: one request in flight; rack comes 0..g_waits cycles after the
    -- previous transfer ended, read data 1..1+g_waits cycles after rack
    ----------------------------------------------------------------------------
    mresp.rack     <= rack_ok and mreq.request;
    mresp.rack_tag <= mreq.tag;
    mresp.dack_tag <= dack_r;

    p_mem: process(clock)
        variable adr    : natural;
        variable w      : std_logic_vector(31 downto 0);
        variable gap    : natural := 0;
        variable rd_cnt : natural := 0;
        variable p_tag  : std_logic_vector(7 downto 0);
        variable p_adr  : natural;
        variable r      : natural;
    begin
        if rising_edge(clock) then
            r := to_integer(lfsr(5 downto 3));
            dack_r <= X"00";
            if rd_cnt > 0 then
                rd_cnt := rd_cnt - 1;
                if rd_cnt = 0 then
                    dack_r <= p_tag;
                    mresp.data <= ram(p_adr);
                end if;
            elsif mresp.rack = '1' then
                adr := to_integer(mreq.address(15 downto 2));
                if mreq.read_writen = '1' then
                    p_tag := mreq.tag;
                    p_adr := adr;
                    rd_cnt := 1 + (r mod (g_waits + 1));
                else
                    w := ram(adr);
                    for b in 0 to 3 loop
                        if mreq.byte_en(b) = '1' then
                            w(8 * b + 7 downto 8 * b) := mreq.data(8 * b + 7 downto 8 * b);
                        end if;
                    end loop;
                    ram(adr) <= w;
                end if;
                gap := to_integer(lfsr(9 downto 7)) mod (g_waits + 1);
            elsif gap > 0 then
                gap := gap - 1;
            end if;
            if rd_cnt = 0 and gap = 0 and not (mresp.rack = '1') then
                rack_ok <= '1';
            else
                rack_ok <= '0';
            end if;
            if mresp.rack = '1' then
                rack_ok <= '0';
            end if;
        end if;
    end process;

    ----------------------------------------------------------------------------
    -- IO: byte accesses, ack after 1..1+g_waits cycles; a write to the device
    -- clears the interrupt
    ----------------------------------------------------------------------------
    p_io: process(clock)
        variable cnt : natural := 0;
    begin
        if rising_edge(clock) then
            io_resp.ack <= '0';
            ack_clr <= '0';
            io_resp.data <= X"A5";
            if cnt > 0 then
                cnt := cnt - 1;
                if cnt = 0 then
                    io_resp.ack <= '1';
                end if;
            elsif io_req.read = '1' or io_req.write = '1' then
                if io_req.write = '1' then
                    ack_clr <= '1';
                end if;
                cnt := 1 + (to_integer(lfsr(12 downto 10)) mod (g_waits + 1));
            end if;
        end if;
    end process;

    ----------------------------------------------------------------------------
    -- interrupt: level; rises at random times, falls on the device write, and
    -- now and then by itself
    ----------------------------------------------------------------------------
    p_irq: process(clock)
    begin
        if rising_edge(clock) then
            if g_irq_period > 0 and reset = '0' then
                if irq = '0' then
                    if to_integer(lfsr(23 downto 14)) mod g_irq_period = 0 then
                        irq <= '1';
                        n_irq <= n_irq + 1;
                    end if;
                else
                    if ack_clr = '1' then
                        irq <= '0';
                    elsif lfsr(28 downto 24) = "00000" then
                        irq <= '0';
                    end if;
                end if;
            end if;
        end if;
    end process;

    ----------------------------------------------------------------------------
    -- end of the run (the monitor is in the core: monitor.inc, run.sh)
    ----------------------------------------------------------------------------
    p_end: process(clock)
        variable l : line;
    begin
        if rising_edge(clock) then
            if reset = '0' then
                if ram(16#4110# / 4) = X"00000001" and ram(16#410C# / 4) = X"00000001" then
                    if not g_quiet then
                        write(l, string'("TB: finished @")); write(l, cycles);
                        write(l, string'(" retired ")); write(l, mon_retired);
                        write(l, string'(" ecall ")); write(l, mon_ecalls);
                        write(l, string'(" irq ")); write(l, mon_irqs);
                        write(l, string'(" mret ")); write(l, mon_mrets);
                        writeline(output, l);
                    end if;
                    write(l, string'("TB: result A=")); hwrite(l, ram(16#4114# / 4));
                    write(l, string'(" B=")); hwrite(l, ram(16#4118# / 4));
                    write(l, string'(" sw_MIE0=")); write(l, to_integer(unsigned(ram(16#4108# / 4))));
                    write(l, string'(" irqs=")); write(l, mon_irqs);
                    write(l, string'(" errors=")); write(l, mon_errors);
                    writeline(output, l);
                    finish;
                end if;
                if cycles = g_max then
                    write(l, string'("TB: timeout, errors=")); write(l, mon_errors);
                    write(l, string'(" retired ")); write(l, mon_retired); write(l, string'(" ecall ")); write(l, mon_ecalls);
                    write(l, string'(" irq ")); write(l, mon_irqs); write(l, string'(" doneA ")); hwrite(l, ram(16#410C# / 4));
                    write(l, string'(" doneB ")); hwrite(l, ram(16#4110# / 4)); write(l, string'(" pc ")); hwrite(l, dbg_pc);
                    writeline(output, l);
                    finish;
                end if;
            end if;
        end if;
    end process;
end architecture;
