--------------------------------------------------------------------------------
-- tb_nonet (T140 Q2b): a stand-in for the RISC-V inside the C128 large core's ultimate_sys. It makes
-- I/O accesses into the window of the dropped Ethernet MAC 0x060800-0x06083F
-- and into a neighbour that still exists (ICAP ID at 0x060600), and
-- checks that every access is acknowledged within a few clocks (the RISC-V must never hang on one),
-- that a read gives a defined value (0 from the dropped MAC), and that the pins and lines of the
-- MAC stay idle while the testbench drives noise at their inputs.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.io_bus_pkg.all;
use work.mem_bus_pkg.all;
use work.tb_nonet_pkg.all;

architecture bfm of rvlite_wrapper is
begin
    mem_req <= c_mem_req_32_init;
    io_busy <= '0';

    process
        variable errs : natural := 0;
        variable dummy : std_logic_vector(7 downto 0);

        procedure io_access(name : string; addr : natural; wr : boolean; wdata : std_logic_vector(7 downto 0);
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
                if n = 20 then
                    report "BFM: no ack at " & name & " " & to_hstring(to_unsigned(addr, 24)) severity error;
                    errs := errs + 1;
                    exit;
                end if;
            end loop;
            rdata := io_resp.data;
        end procedure;

        procedure window(name : string; base : natural; last : natural) is
        begin
            for a in base to last loop
                io_access(name & " write", a, true, X"FF", dummy);
                io_access(name & " read", a, false, X"00", dummy);
                if is_x(dummy) or dummy /= X"00" then
                    report "BFM: " & name & " read at " & to_hstring(to_unsigned(a, 24)) & " gives " & to_hstring(dummy) severity error;
                    errs := errs + 1;
                end if;
            end loop;
        end procedure;
    begin
        io_req <= c_io_req_init;
        wait until reset = '0';
        for i in 1 to 10 loop wait until rising_edge(clock); end loop;

        window("eth", 16#060800#, 16#06083F#);
        bfm_step <= 1;
        bfm_step <= 2;

        -- a neighbour that is still there: the ICAP loader's ID register is 0x7A
        io_access("icap", 16#060600#, false, X"00", dummy);
        if dummy /= X"7A" then report "BFM: ICAP ID " & to_hstring(dummy) severity error; errs := errs + 1; end if;
        bfm_step <= 3;

        -- let the noise on the dropped blocks' inputs run, then look at their outputs
        for i in 1 to 400 loop wait until rising_edge(clock); end loop;
        if nt_eth_tx /= 0 then report "BFM: Ethernet TX stream moved " & integer'image(nt_eth_tx) severity error; errs := errs + 1; end if;
        -- the interrupt lines through the ITU: enable all, read what is active (low inputs 5 and 6 are the MAC's)
        io_access("itu", 16#000001#, true, X"FF", dummy);
        io_access("itu", 16#000005#, false, X"00", dummy);
        if is_x(dummy) or (dummy and X"60") /= X"00" then report "BFM: Ethernet interrupt active " & to_hstring(dummy) severity error; errs := errs + 1; end if;
        if nt_dma /= 0 then report "BFM: a memory request (the MAC's DMA port; the WiFi UART is idle) " & integer'image(nt_dma) severity error; errs := errs + 1; end if;
        bfm_step <= 4;

        bfm_errors <= errs;
        bfm_step <= 99;
        wait;
    end process;
end architecture;
