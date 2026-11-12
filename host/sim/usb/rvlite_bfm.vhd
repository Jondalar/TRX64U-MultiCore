--------------------------------------------------------------------------------
-- tb_usb: a stand-in for the RISC-V inside ultimate_sys. Analyzed after the
-- real rvlite_wrapper, so ultimate_sys binds this architecture. It makes the
-- IO accesses the app makes for USB (initialize_usb_hub, UsbBase::init):
-- capability bit 23, hub and ULPI reset through u2p_io, nano program into
-- 0x10080000 and read back, NANO_START.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.textio.all;
use work.io_bus_pkg.all;
use work.mem_bus_pkg.all;
use work.tb_usb_pkg.all;

architecture bfm of rvlite_wrapper is
begin
    mem_req <= c_mem_req_32_init;
    io_busy <= '0';

    process
        variable errs : natural := 0;
        type t_bytes is array(0 to 2047) of std_logic_vector(7 downto 0);
        variable prog : t_bytes := (others => X"00");
        variable size : natural := 0;
        file f        : text;
        variable l    : line;
        variable b    : std_logic_vector(7 downto 0);
        variable rd   : std_logic_vector(7 downto 0);

        procedure io_access(addr : natural; wr : boolean; wdata : std_logic_vector(7 downto 0);
                         rdata : out std_logic_vector(7 downto 0)) is
            variable n : natural := 0;
        begin
            io_req <= c_io_req_init;
            io_req.address <= to_unsigned(addr, 24);
            io_req.data <= wdata;
            if wr then io_req.write <= '1'; else io_req.read <= '1'; end if;
            wait until rising_edge(clock);
            io_req <= c_io_req_init;
            loop
                wait until rising_edge(clock);
                exit when io_resp.ack = '1';
                n := n + 1;
                if n = 100 then
                    report "BFM: no ack at " & to_hstring(to_unsigned(addr, 24)) severity error;
                    errs := errs + 1;
                    exit;
                end if;
            end loop;
            rdata := io_resp.data;
        end procedure;

        procedure wr(addr : natural; data : std_logic_vector(7 downto 0)) is
            variable dummy : std_logic_vector(7 downto 0);
        begin
            io_access(addr, true, data, dummy);
        end procedure;

        procedure check(addr : natural; expect : std_logic_vector(7 downto 0)) is
            variable v : std_logic_vector(7 downto 0);
        begin
            io_access(addr, false, X"00", v);
            if v /= expect then
                report "BFM: " & to_hstring(to_unsigned(addr, 24)) & " reads " & to_hstring(v) &
                       ", expected " & to_hstring(expect) severity error;
                errs := errs + 1;
            end if;
        end procedure;
    begin
        io_req <= c_io_req_init;
        file_open(f, "nano_minimal.hex", read_mode);
        while not endfile(f) loop
            readline(f, l);
            hread(l, b);
            prog(size) := b;
            size := size + 1;
        end loop;
        file_close(f);

        wait until reset = '0';
        for i in 1 to 10 loop wait until rising_edge(clock); end loop;

        -- ITU capabilities 0x1000000C-F: 0x25800222
        check(16#00000C#, X"25"); check(16#00000D#, X"80");
        check(16#00000E#, X"02"); check(16#00000F#, X"22");
        bfm_step <= 1;

        -- initialize_usb_hub: NANO_START = 0, ULPI_RESET = 1, nano RAM zero,
        -- hub reset pulse (USB2513Init)
        wr(16#080800#, X"00");
        wr(16#10000F#, X"01");
        bfm_step <= 2;
        wr(16#10000D#, X"01");
        bfm_step <= 3;
        wr(16#10000D#, X"00");
        wr(16#10000F#, X"00");
        wr(16#10000F#, X"80");
        bfm_step <= 4;

        -- UsbBase: the nano program, rest zero; first bytes read back
        for i in 0 to 2047 loop
            wr(16#080000# + i, prog(i));
        end loop;
        for i in 0 to 7 loop
            check(16#080000# + i, prog(i));
        end loop;
        report "BFM: " & integer'image(size) & " bytes of nano code loaded";
        wr(16#000004#, X"04");                  -- ITU_IRQ_CLEAR, USB
        wr(16#000001#, X"04");                  -- ITU_IRQ_ENABLE, USB
        wr(16#080800#, X"01");                  -- NANO_START
        bfm_errors <= errs;
        bfm_step <= 5;
        wait;
    end process;
end architecture;
