--------------------------------------------------------------------------------
-- tb_dma_bus (T16 Q8): a stand-in for the RISC-V inside ultimate_sys. It makes
-- the app's DMA writes the way c64.cc does (C64_STOP, C64_DMA_MEMONLY, bytes
-- through the window at 0x050000) and checks whether the window hands the byte
-- to the C64 as a bus cycle (dma_bus): only the CPU-held, not-MEMONLY ones, and
-- the MEMONLY writes of $0000/$0001, which must reach the 6510 port.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.io_bus_pkg.all;
use work.mem_bus_pkg.all;
use work.tb_dma_bus_pkg.all;

architecture bfm of rvlite_wrapper is
begin
    mem_req <= c_mem_req_32_init;
    io_busy <= '0';

    process
        variable errs : natural := 0;
        variable dummy : std_logic_vector(7 downto 0);

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
                if n = 200 then
                    report "BFM: no ack at " & to_hstring(to_unsigned(addr, 24)) severity error;
                    errs := errs + 1;
                    exit;
                end if;
            end loop;
            rdata := io_resp.data;
        end procedure;

        procedure wr(addr : natural; data : std_logic_vector(7 downto 0)) is
        begin
            io_access(addr, true, data, dummy);
        end procedure;

        -- one window access; bus = was it handed over as a bus cycle
        procedure win(name : string; a : natural; w : boolean; want : std_logic) is
            variable c0 : natural := pk_cnt;
        begin
            io_access(16#050000# + a, w, X"37", dummy);
            for i in 1 to 4 loop wait until rising_edge(clock); end loop;
            if pk_cnt /= c0 + 1 then
                report "BFM: " & name & ": no request reached the C64 side" severity error;
                errs := errs + 1;
            elsif pk_bus /= want then
                report "BFM: " & name & ": dma_bus is " & std_logic'image(pk_bus) & ", expected " & std_logic'image(want)
                    severity error;
                errs := errs + 1;
            end if;
        end procedure;

        constant STOP    : natural := 16#040001#;
        constant MEMONLY : natural := 16#180003#;
    begin
        io_req <= c_io_req_init;
        wait until reset = '0';
        for i in 1 to 10 loop wait until rising_edge(clock); end loop;

        -- CPU held, bus mode: everything is a bus cycle (the REST pause's plain DMA)
        wr(STOP, X"01"); wr(MEMONLY, X"00");
        win("bus write $0001", 16#0001#, true, '1');
        win("bus write $0500", 16#0500#, true, '1');
        win("bus read $0001", 16#0001#, false, '1');
        bfm_step <= 1;

        -- CPU held, MEMONLY (the app's raw transfer): RAM only, except a write of $00/$01
        wr(MEMONLY, X"01");
        win("memonly write $0001", 16#0001#, true, '1');
        win("memonly write $0000", 16#0000#, true, '1');
        win("memonly write $0002", 16#0002#, true, '0');
        win("memonly write $0400", 16#0400#, true, '0');
        win("memonly write $0500", 16#0500#, true, '0');
        win("memonly read $0001", 16#0001#, false, '0');
        win("memonly read $0000", 16#0000#, false, '0');
        bfm_step <= 2;

        -- CPU running: no bus cycles at all
        wr(STOP, X"00"); wr(MEMONLY, X"01");
        win("running memonly write $0001", 16#0001#, true, '0');
        wr(MEMONLY, X"00");
        win("running write $0001", 16#0001#, true, '0');
        bfm_step <= 3;

        -- T79 ICAP loader registers (0x060600): ID, inert Gideon offsets, bytes ignored while disabled,
        -- word assembly (first byte most significant), word counter, enable
        io_access(16#060600#, false, X"00", dummy);
        if dummy /= X"7A" then report "BFM: ICAP ID " & to_hstring(dummy) severity error; errs := errs + 1; end if;
        wr(16#060604#, X"00"); wr(16#060608#, X"AA"); wr(16#060610#, X"11"); wr(16#060610#, X"22");
        wr(16#060610#, X"33"); wr(16#060610#, X"44");
        io_access(16#060604#, false, X"00", dummy);
        if dummy /= X"00" then report "BFM: ICAP +04 reads " & to_hstring(dummy) severity error; errs := errs + 1; end if;
        for i in 1 to 4 loop wait until rising_edge(clock); end loop;
        if ic_cnt /= 0 then report "BFM: ICAP word while disabled" severity error; errs := errs + 1; end if;
        wr(16#060614#, X"01");
        io_access(16#060614#, false, X"00", dummy);
        if dummy /= X"01" then report "BFM: ICAP status " & to_hstring(dummy) severity error; errs := errs + 1; end if;
        wr(16#060610#, X"DE"); wr(16#060610#, X"AD"); wr(16#060610#, X"BE"); wr(16#060610#, X"EF");
        wr(16#060604#, X"00"); wr(16#060608#, X"AA");                  -- still inert with the enable set
        wr(16#060610#, X"01"); wr(16#060610#, X"02"); wr(16#060610#, X"03"); wr(16#060610#, X"04");
        wr(16#060610#, X"99");                                          -- a fifth byte: no word yet
        for i in 1 to 4 loop wait until rising_edge(clock); end loop;
        if ic_cnt /= 2 or ic_first /= X"DEADBEEF" or ic_last /= X"01020304" then
            report "BFM: ICAP words " & integer'image(ic_cnt) & " " & to_hstring(ic_first) & " " & to_hstring(ic_last)
                severity error;
            errs := errs + 1;
        end if;
        io_access(16#060618#, false, X"00", dummy);
        if dummy /= X"02" then report "BFM: ICAP count " & to_hstring(dummy) severity error; errs := errs + 1; end if;
        io_access(16#06061B#, false, X"00", dummy);
        if dummy /= X"00" then report "BFM: ICAP count b3 " & to_hstring(dummy) severity error; errs := errs + 1; end if;
        wr(16#060614#, X"00");                                          -- disable clears the byte counter
        io_access(16#060614#, false, X"00", dummy);
        if dummy /= X"00" then report "BFM: ICAP status off " & to_hstring(dummy) severity error; errs := errs + 1; end if;
        wr(16#060614#, X"01");                                          -- a new session: count restarts, 4 bytes = one word
        io_access(16#060618#, false, X"00", dummy);
        if dummy /= X"00" then report "BFM: ICAP count not cleared " & to_hstring(dummy) severity error; errs := errs + 1; end if;
        wr(16#060610#, X"A5"); wr(16#060614#, X"00"); wr(16#060614#, X"01");
        wr(16#060610#, X"B1"); wr(16#060610#, X"B2"); wr(16#060610#, X"B3"); wr(16#060610#, X"B4");
        for i in 1 to 4 loop wait until rising_edge(clock); end loop;
        if ic_cnt /= 3 or ic_last /= X"B1B2B3B4" then
            report "BFM: ICAP after re-enable " & integer'image(ic_cnt) & " " & to_hstring(ic_last) severity error;
            errs := errs + 1;
        end if;
        wr(16#060614#, X"00");
        bfm_step <= 4;

        -- T79 app decouple (+0x20): write bit0 sets the request, read gives bit0 = request, bit1 = effective
        io_access(16#060620#, false, X"00", dummy);
        if dummy /= X"00" then report "BFM: app decouple at reset " & to_hstring(dummy) severity error; errs := errs + 1; end if;
        wr(16#060620#, X"01");
        for i in 1 to 4 loop wait until rising_edge(clock); end loop;
        io_access(16#060620#, false, X"00", dummy);
        if dummy /= X"03" then report "BFM: app decouple set " & to_hstring(dummy) severity error; errs := errs + 1; end if;
        wr(16#060620#, X"00");
        for i in 1 to 4 loop wait until rising_edge(clock); end loop;
        io_access(16#060620#, false, X"00", dummy);
        if dummy /= X"00" then report "BFM: app decouple cleared " & to_hstring(dummy) severity error; errs := errs + 1; end if;
        bfm_step <= 5;

        bfm_errors <= errs;
        bfm_step <= 99;
        wait;
    end process;
end architecture;
