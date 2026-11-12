-- SPDX-License-Identifier: GPL-3.0-or-later
-- T88 §5: Watson's ANTIC and shared_enable (the real ones, not touched) around a 6502, in a mini machine of our own:
-- 64 KB RAM, the test OS (testos.asm) at $C000, ANTIC registers at $D400, a POKEY stand-in (RANDOM, IRQST, a timer
-- IRQ the program starts with IRQEN/STIMER), everything else at $D000-$D7FF reads $FF and swallows writes.
-- ANTIC fetches from the same RAM (display list, screen, players); WSYNC, DLI, VBI and its refresh cycles hold the
-- 6502 through RDY exactly as in the Atari core. Memory is ready at once (MEMMODE 0) or after random waits
-- (MEMMODE 1, CPU and ANTIC side). Plain CLK, cycle_length 16, no CE.
-- One line per clock where the CPU's enable is high:   clk rdy ready addr rw data
-- The bench runs with Watson's cpu.vhd + cpu_65xx.vhd ("orig") and with cores/atari800/cpu.vhd ("ours"): same logs.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.textio.all;
use ieee.std_logic_textio.all;
use std.env.all;

entity tb_mini is
generic (
    ROM     : string  := "testos.hex";
    TRACE   : string  := "trace.txt";
    CPUCYC  : natural := 150000;     -- stop after this many executed 6502 cycles
    MEMMODE : natural := 0;
    SEED    : natural := 1;
    THROTTLE : std_logic_vector(5 downto 0) := "000001";   -- 1x; bit i adds a 6502 cycle every 16/2^i clocks (turbo)
    DBG0    : natural := 0;            -- debug: every clock from DBG0 to DBG1 into dbg.txt
    DBG1    : natural := 0;
    HOLD1   : boolean := false );      -- keep the CPU in reset for the first machine cycle after reset_n (Watson's 6502 starts one cycle earlier than ours)
end tb_mini;

architecture sim of tb_mini is
    type ram_t is protected
        procedure load(fname : string);
        impure function rd(a : natural) return std_logic_vector;
        procedure wr(a : natural; d : std_logic_vector(7 downto 0));
    end protected;
    type ram_t is protected body
        type mem_t is array (0 to 65535) of std_logic_vector(7 downto 0);
        variable m : mem_t := (others => x"00");
        procedure load(fname : string) is
            file f : text open read_mode is fname;
            variable l : line;
            variable b : std_logic_vector(7 downto 0);
            variable i : natural := 0;
        begin
            while not endfile(f) and i < 16384 loop
                readline(f, l);
                hread(l, b);
                m(16#C000# + i) := b;
                i := i + 1;
            end loop;
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
    signal wr_tick : std_logic := '0';

    signal clk : std_logic := '0';
    signal reset_n : std_logic := '0';
    signal done : boolean := false;
    signal mrdy_cpu, mrdy_antic : std_logic := '1';
    signal cpu_en, antic_en179, antic_en179d, refresh : std_logic;
    signal rdy, nmi_n, irq_n : std_logic;
    signal a_cpu : std_logic_vector(15 downto 0);
    signal di, do_cpu : std_logic_vector(7 downto 0);
    signal rw_n, fetch : std_logic;
    signal antic_wr : std_logic;
    signal antic_do, antic_mem : std_logic_vector(7 downto 0);
    signal dma_fetch : std_logic;
    signal dma_addr : std_logic_vector(15 downto 0);
    signal an, nct : std_logic_vector(2 downto 0);
    signal dmaclk : std_logic_vector(3 downto 0);
    signal shift_o, hcount : std_logic_vector(7 downto 0);
    signal vcount : std_logic_vector(8 downto 0);
    signal cc, hcc, vcc, hblank, turbo, vblank, dma_en, oldcpu : std_logic;
    signal lfsr : std_logic_vector(7 downto 0) := x"A5";
    signal irq_pending, irq_enabled : std_logic := '0';
    signal timer : natural := 0;
    signal cnt_irq_enable : natural := 0;
    signal cpu_reset, hold : std_logic;
begin
    clk <= not clk after 17.62 ns when not done;
    reset_n <= '1' after 400 ns;

    enables : entity work.shared_enable
    generic map (cycle_length => 16)
    port map (CLK => clk, RESET_N => reset_n, ANTIC_REFRESH => refresh, MEMORY_READY_CPU => mrdy_cpu,
              MEMORY_READY_ANTIC => mrdy_antic, PAUSE_6502 => '0', THROTTLE_COUNT_6502 => THROTTLE,
              ANTIC_ENABLE_179 => antic_en179, oldcpu_enable => oldcpu, CPU_ENABLE_OUT => cpu_en,
              ANTIC_ENABLE_179_DOUBLE => antic_en179d);

    cpu1 : entity work.cpu
    port map (CLK => clk, RESET => cpu_reset, ENABLE => reset_n, DI => di, IRQ_n => irq_n, NMI_n => nmi_n,
              MEMORY_READY => mrdy_cpu, THROTTLE => cpu_en, RDY => rdy, DO => do_cpu, A => a_cpu,
              R_W_n => rw_n, CPU_FETCH => fetch);

    -- the write strobe of the address decoder: with the cycle's own address and data, in the clock that ends the cycle
    antic_wr <= '1' when cpu_en = '1' and mrdy_cpu = '1' and rw_n = '0' and a_cpu(15 downto 8) = x"D4" else '0';
    cpu_reset <= (not reset_n) or hold;
    process (clk)
    begin
        if rising_edge(clk) then
            if reset_n = '0' then
                hold <= '1' when HOLD1 else '0';
            elsif cpu_en = '1' and mrdy_cpu = '1' then
                hold <= '0';
            end if;
        end if;
    end process;

    antic1 : entity work.antic
    generic map (cycle_length => 16)
    port map (CLK => clk, WR_EN => antic_wr, RESET_N => reset_n, RNMI_N => '1', MEMORY_READY_ANTIC => mrdy_antic,
              MEMORY_READY_CPU => mrdy_cpu, ANTIC_ENABLE_179 => antic_en179, PAL => '1', lightpen => '1',
              ADDR => a_cpu(3 downto 0), CPU_DATA_IN => do_cpu, MEMORY_DATA_IN => antic_mem,
              NMI_N_OUT => nmi_n, ANTIC_READY => rdy, COLOUR_CLOCK_OUT => cc, HIGHRES_COLOUR_CLOCK_OUT => hcc,
              VBXE_COLOUR_CLOCK_OUT => vcc, ANTIC_DMA_ENABLED => dma_en, dma_fetch_out => dma_fetch,
              HBLANK => hblank, vblank_out => vblank, AN => an, DATA_OUT => antic_do, refresh_out => refresh,
              dma_address_out => dma_addr, next_cycle_type => nct, turbo_out => turbo, shift_out => shift_o,
              dma_clock_out => dmaclk, hcount_out => hcount, vcount_out => vcount);

    process (dma_addr, wr_tick)
    begin
        antic_mem <= ram.rd(to_integer(unsigned(dma_addr)));
    end process;

    -- the 6502's read data: RAM/ROM, ANTIC at $D4xx, POKEY stand-in at $D2xx
    process (a_cpu, antic_do, lfsr, irq_pending, wr_tick)
        variable a : natural;
    begin
        a := to_integer(unsigned(a_cpu));
        if a >= 16#D400# and a < 16#D500# then
            di <= to_stdlogicvector(to_bitvector(antic_do));   -- ANTIC's NMIST is 'X' until its first VBI: a clean 0 here, in both runs
        elsif a = 16#D20A# then
            di <= lfsr;
        elsif a = 16#D20E# then
            di <= "1111111" & not irq_pending;
        elsif a >= 16#D000# and a < 16#D800# then
            di <= x"FF";
        else
            di <= ram.rd(a);
        end if;
    end process;
    irq_n <= not irq_pending;

    process
        file tf : text open write_mode is TRACE;
        file df : text open write_mode is "dbg.txt";
        variable l : line;
        variable clks, cyc, t : natural := 0;
        variable lf : unsigned(31 downto 0) := to_unsigned(SEED, 32) xor x"ACE1ACE1";
        variable a : natural;
        variable adv : boolean;
    begin
        ram.load(ROM);
        wr_tick <= '1';
        loop
            wait until rising_edge(clk);
            clks := clks + 1;
            lf := lf(30 downto 0) & (lf(31) xor lf(21) xor lf(1) xor lf(0));
            if clks >= DBG0 and clks <= DBG1 then
                write(l, clks); write(l, string'(" en=")); write(l, cpu_en); write(l, string'(" rdy=")); write(l, rdy);
                write(l, string'(" mr=")); write(l, mrdy_cpu); write(l, string'(" a=")); hwrite(l, a_cpu); write(l, string'(" rw=")); write(l, rw_n);
                write(l, string'(" nmi=")); write(l, nmi_n); write(l, string'(" irq=")); write(l, irq_n); write(l, string'(" v=")); hwrite(l, "0000000" & vcount); write(l, string'(" h=")); hwrite(l, hcount); write(l, string'(" dma=")); write(l, dma_en); write(l, string'(" fetch=")); write(l, dma_fetch); write(l, string'(" ref=")); write(l, refresh);
                writeline(df, l);
            end if;
            if reset_n = '1' and cpu_en = '1' and mrdy_cpu = '1' then
                a := to_integer(unsigned(a_cpu));
                adv := (rdy = '1') or (rw_n = '0');
                write(l, clks); write(l, string'(" "));
                write(l, rdy); write(l, mrdy_cpu); write(l, string'(" "));
                hwrite(l, a_cpu); write(l, string'(" "));
                if rw_n = '1' then write(l, string'("R ")); hwrite(l, di); else write(l, string'("W ")); hwrite(l, do_cpu); end if;
                writeline(tf, l);
                if adv then
                    cyc := cyc + 1;
                    lfsr <= lfsr(6 downto 0) & (lfsr(7) xor lfsr(5) xor lfsr(4) xor lfsr(3));
                    if rw_n = '0' then
                        if a >= 16#D400# and a < 16#D500# then
                            null;
                        elsif a = 16#D20E# then
                            irq_enabled <= do_cpu(0);
                            if do_cpu(0) = '0' then irq_pending <= '0'; end if;
                        elsif a = 16#D209# then
                            t := 0;           -- STIMER: restart the timer
                        elsif a < 16#C000# then
                            ram.wr(a, do_cpu);
                            wr_tick <= not wr_tick;
                        end if;
                    end if;
                    -- POKEY stand-in timer 1: an IRQ every 3600 machine cycles while IRQEN bit 0 is set
                    t := t + 1;
                    if t >= 3600 then
                        t := 0;
                        if irq_enabled = '1' then irq_pending <= '1'; end if;
                    end if;
                end if;
                exit when cyc >= CPUCYC;
            end if;
            if MEMMODE = 1 then
                mrdy_cpu   <= lf(7) or lf(9) or lf(11);
                mrdy_antic <= lf(8) or lf(10) or lf(12);
            end if;
        end loop;
        report "done: " & integer'image(cyc) & " 6502 cycles in " & integer'image(clks) & " clocks";
        done <= true;
        wait;
    end process;
end sim;
