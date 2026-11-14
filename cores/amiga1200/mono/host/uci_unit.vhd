--------------------------------------------------------------------------------
-- uci_unit: the UCI (Ultimate Command Interface, TRX64-Ultimate/docs/specs/T28-uci.md) as in
-- Gideon's bitstreams: his command_interface (vendor/gideon, unchanged) in the
-- C64's clock, its 8 KB IO range (0x10044000) from the app's clock through
-- his io_bus_bridge2, and the slot bus built from our C64 bus.
--
-- Slot side (c64_clock, the ungated sys_clk). command_protocol uses these
-- fields of t_slot_req, nothing else:
--   bus_address  the bus address now (the read data and reg_output follow it;
--                the response/status RAM is read one clock after it)
--   io_address, data, io_read, io_write
--                one pulse after each CPU-half that touched $DE00-$DFFF with
--                I/O mapped (ev with ev_io), address and data as they were
--                at that half's end (the edge the CPU latches its data on);
--                a read moves the response/status pointer only then
--   late_write   any write; with io_address = $FF00 it is write_ff00 (the
--                "trigger" freeze, command_protocol)
-- The answer (data, reg_output, irq) is registered here once more: the C64
-- reads it at the end of the half, 16 clocks after the address.
--
-- freeze (a C64 program's request through the control register) goes to
-- top.v, which holds the CPU with it (RDY), as Gideon's dma_n does.
-- io_irq is Gideon's level, taken into io_clock (ITU irq_in(4)).
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;
use work.slot_bus_pkg.all;

entity uci_unit is
port (
    io_clock    : in  std_logic;
    io_reset    : in  std_logic;
    io_req      : in  t_io_req;                 -- the 8 KB range (address bits 12:0)
    io_resp     : out t_io_resp;
    io_irq      : out std_logic;                -- io_clock

    c64_clock   : in  std_logic;
    bus_addr    : in  std_logic_vector(15 downto 0);
    ev          : in  std_logic;                -- a CPU-half (or DMA cycle) just ended
    ev_addr     : in  std_logic_vector(15 downto 0);
    ev_data     : in  std_logic_vector(7 downto 0);
    ev_we       : in  std_logic;
    ev_io       : in  std_logic;                -- it was $DE00-$DFFF, I/O mapped
    dout        : out std_logic_vector(7 downto 0);
    oe          : out std_logic;                -- the UCI answers at bus_addr
    c64_irq     : out std_logic;
    freeze      : out std_logic );
end entity;

architecture rtl of uci_unit is
    signal c64_reset_s  : std_logic_vector(1 downto 0) := "11";
    signal c64_reset    : std_logic;
    signal cif_req      : t_io_req;
    signal cif_resp     : t_io_resp;
    signal slot_req     : t_slot_req;
    signal io_address   : unsigned(15 downto 0) := (others => '0');
    signal io_data      : std_logic_vector(7 downto 0) := X"00";
    signal io_read      : std_logic := '0';
    signal io_write     : std_logic := '0';
    signal late_write   : std_logic := '0';
    signal slot_resp    : t_slot_resp;
    signal write_ff00   : std_logic;
    signal cif_irq      : std_logic;
    signal irq_s        : std_logic_vector(1 downto 0) := "00";

    attribute ASYNC_REG : string;
    attribute ASYNC_REG of c64_reset_s : signal is "TRUE";
    attribute ASYNC_REG of irq_s       : signal is "TRUE";
begin
    process(c64_clock)
    begin
        if rising_edge(c64_clock) then
            c64_reset_s <= c64_reset_s(0) & io_reset;
        end if;
    end process;
    c64_reset <= c64_reset_s(1);

    i_bridge: entity work.io_bus_bridge2
    generic map (
        g_addr_width => 13 )
    port map (
        clock_a  => io_clock,
        reset_a  => io_reset,
        req_a    => io_req,
        resp_a   => io_resp,
        clock_b  => c64_clock,
        reset_b  => c64_reset,
        req_b    => cif_req,
        resp_b   => cif_resp );

    -- the slot bus
    process(c64_clock)
    begin
        if rising_edge(c64_clock) then
            io_read    <= '0';
            io_write   <= '0';
            late_write <= '0';
            if ev = '1' then
                io_address <= unsigned(ev_addr);
                io_data    <= ev_data;
                io_read    <= ev_io and not ev_we;
                io_write   <= ev_io and ev_we;
                late_write <= ev_we;
            end if;
        end if;
    end process;
    process(bus_addr, io_address, io_data, io_read, io_write, late_write)
    begin
        slot_req             <= c_slot_req_init;
        slot_req.bus_address <= unsigned(bus_addr);
        slot_req.io_address  <= io_address;
        slot_req.data        <= io_data;
        slot_req.io_read     <= io_read;
        slot_req.io_write    <= io_write;
        slot_req.late_write  <= late_write;
    end process;
    write_ff00 <= '1' when late_write = '1' and io_address = X"FF00" else '0';

    i_cmd: entity work.command_interface
    port map (
        clock      => c64_clock,
        reset      => c64_reset,
        slot_req   => slot_req,
        slot_resp  => slot_resp,
        freeze     => freeze,
        write_ff00 => write_ff00,
        io_req     => cif_req,
        io_resp    => cif_resp,
        io_irq     => cif_irq );

    process(c64_clock)
    begin
        if rising_edge(c64_clock) then
            dout    <= slot_resp.data;
            oe      <= slot_resp.reg_output;
            c64_irq <= slot_resp.irq;
        end if;
    end process;

    process(io_clock)
    begin
        if rising_edge(io_clock) then
            irq_s <= irq_s(0) & cif_irq;
        end if;
    end process;
    io_irq <= irq_s(1);
end architecture;
