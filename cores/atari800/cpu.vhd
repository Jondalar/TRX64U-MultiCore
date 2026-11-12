-- SPDX-License-Identifier: GPL-3.0-or-later
-- T88: the 6502 of the Atari 800 XL core. Replaces two files of Mark Watson's tree
-- (rtl/common/a8core/cpu.vhd and cpu_65xx.vhd, the latter "All Rights Reserved" by
-- Peter Wendrich, not used): same entity name `cpu`, same ports and same meaning as
-- Watson's, plus CE (the core's global clock enable, 9 of 10 sys_clk) and SYNC (test aid).
-- The CPU proper is proc_core_a8 (this directory): the 6502N the C64 core already runs
-- (vendor/icepi/6502n, Gideon Zweijtzer, GPL-3) with the Atari's direct NMI, kept here so the
-- C64's own files stay untouched.
--
-- What the Atari core asks of its CPU (read from atari800core.vhd / shared_enable.vhdl):
--   * ENABLE and THROTTLE gate a cycle, MEMORY_READY_CPU lets it end: the CPU advances one
--     bus cycle on the clock edge where ENABLE and THROTTLE and MEMORY_READY are all high;
--     DI is sampled on that edge for the address (A, R_W_n) presented before it, and A, R_W_n,
--     DO of the next cycle are there after it.
--   * CPU_FETCH (= ENABLE and THROTTLE, without MEMORY_READY) starts the memory request of a
--     cycle, also on a cycle that then waits for MEMORY_READY.
--   * RDY (ANTIC_READY) low stops the CPU on a read cycle only; a write cycle completes.
--     proc_core's `ready` is that rule (stall when ready = 0 and the cycle is a read), so RDY
--     goes straight in.
--   * IRQ_n is a level, NMI_n an edge (a falling edge, as the 6502 takes it). ANTIC holds NMI_n low during reset
--     and releases it a few cycles after (antic.vhdl: nmi_reg is '0' in reset), which Watson's cpu_65xx never saw as an
--     edge; proc_core would, so NMI_n is masked until it has been seen high once since reset (`armed`).
library ieee;
use ieee.std_logic_1164.all;

entity cpu is
port (
    CLK, RESET, ENABLE : in  std_logic;
    CE                 : in  std_logic := '1';
    DI                 : in  std_logic_vector(7 downto 0);
    IRQ_n              : in  std_logic;
    NMI_n              : in  std_logic;
    MEMORY_READY       : in  std_logic;
    THROTTLE           : in  std_logic;
    RDY                : in  std_logic;
    DO                 : out std_logic_vector(7 downto 0);
    A                  : out std_logic_vector(15 downto 0);
    R_W_n              : out std_logic;
    CPU_FETCH          : out std_logic;
    SYNC               : out std_logic );
end cpu;

architecture trx of cpu is
    signal cpu_enable : std_logic;
    signal addr17     : std_logic_vector(16 downto 0);
    signal armed      : std_logic := '0';
    signal nmi_f      : std_logic;
begin
    cpu_enable <= CE and ENABLE and MEMORY_READY and THROTTLE;

    process (CLK)
    begin
        if rising_edge(CLK) then
            if RESET = '1' then
                armed <= '0';
            elsif CE = '1' and NMI_n = '1' then
                armed <= '1';
            end if;
        end if;
    end process;
    nmi_f <= NMI_n or not armed;

    core : entity work.proc_core_a8
    port map (
        clock        => CLK,
        clock_en     => cpu_enable,
        reset        => RESET,
        ready        => RDY,
        irq_n        => IRQ_n,
        nmi_n        => nmi_f,
        so_n         => '1',
        sync_out     => SYNC,
        addr_out     => addr17,
        data_in      => DI,
        data_out     => DO,
        read_write_n => R_W_n );

    A         <= addr17(15 downto 0);
    CPU_FETCH <= ENABLE and THROTTLE;
end trx;
