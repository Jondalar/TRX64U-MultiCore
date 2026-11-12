--------------------------------------------------------------------------------
-- tb_input (T38/T39): a stand-in for the RISC-V inside ultimate_sys, analyzed
-- after the real rvlite_wrapper so that ultimate_sys binds it. It makes the
-- byte writes the app makes (Keyboard_USB::applyMatrixState,
-- JoystickOutput::apply, C64_MODE NMI, CommandInterface::run_reset_task) and
-- checks what comes out on the app_* ports and back on the IO bus.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.io_bus_pkg.all;
use work.mem_bus_pkg.all;
use work.tb_input_pkg.all;

architecture bfm of rvlite_wrapper is
begin
    mem_req <= c_mem_req_32_init;
    io_busy <= '0';

    process
        variable errs : natural := 0;
        variable v    : std_logic_vector(7 downto 0);

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

        procedure check(addr : natural; mask, expect : std_logic_vector(7 downto 0)) is
        begin
            io_access(addr, false, X"00", v);
            if (v and mask) /= expect then
                report "BFM: " & to_hstring(to_unsigned(addr, 24)) & " reads " & to_hstring(v) &
                       " (mask " & to_hstring(mask) & "), expected " & to_hstring(expect) severity error;
                errs := errs + 1;
            end if;
        end procedure;

        procedure expect(name : string; got, want : std_logic_vector) is
        begin
            if got /= want then
                report "BFM: " & name & " is " & to_hstring(got) & ", expected " & to_hstring(want)
                    severity error;
                errs := errs + 1;
            end if;
        end procedure;

        procedure expect(name : string; got, want : std_logic) is
        begin
            if got /= want then
                report "BFM: " & name & " is " & std_logic'image(got) severity error;
                errs := errs + 1;
            end if;
        end procedure;

        constant PAL    : natural := 16#180800#;
        variable c0     : natural;
        constant MATRIX : natural := 16#100300#;
        constant JOY    : natural := 16#180030#;
        constant MODE   : natural := 16#040000#;
        constant CLKDET : natural := 16#040003#;
        constant IRQ_EN : natural := 16#000001#;
        constant IRQ_CLR: natural := 16#000004#;
        constant IRQ_ACT: natural := 16#000005#;
    begin
        io_req <= c_io_req_init;
        wait until reset = '0';
        for i in 1 to 10 loop wait until rising_edge(clock); end loop;

        -- 1. after reset: nothing pressed, joysticks released
        expect("app_keys", pk_app_keys, X"0000000000000000");
        expect("app_joy1", pk_app_joy1, "00000"); expect("app_joy2", pk_app_joy2, "00000");
        expect("app_reset", pk_app_reset, '0'); expect("app_restore", pk_app_restore, '0');
        expect("app_freeze", pk_app_freeze, '0');
        bfm_step <= 1;

        -- 2. A (column 1, row 2) with left shift (column 7, row 1), as
        --    applyMatrixState writes them: bytes 0-7, then 8, 9, 10
        for i in 0 to 7 loop
            if i = 1 then wr(MATRIX + i, X"04");
            elsif i = 7 then wr(MATRIX + i, X"02");
            else wr(MATRIX + i, X"00"); end if;
        end loop;
        wr(MATRIX + 8, X"00"); wr(MATRIX + 9, X"00"); wr(MATRIX + 10, X"00");
        wait until rising_edge(clock);
        expect("app_keys", pk_app_keys, X"0200000000000400");
        check(MATRIX + 1, X"FF", X"04");            -- the page still reads back
        bfm_step <= 2;

        -- 3. reset, RESTORE, freeze: bit 0 of bytes 8, 9, 10
        wr(MATRIX + 8, X"01"); wait until rising_edge(clock);
        expect("app_reset", pk_app_reset, '1');
        wr(MATRIX + 8, X"00"); wr(MATRIX + 9, X"01"); wait until rising_edge(clock);
        expect("app_reset", pk_app_reset, '0'); expect("app_restore", pk_app_restore, '1');
        wr(MATRIX + 9, X"00"); wr(MATRIX + 10, X"01"); wait until rising_edge(clock);
        expect("app_restore", pk_app_restore, '0'); expect("app_freeze", pk_app_freeze, '1');
        wr(MATRIX + 10, X"00");
        -- C64_MODE bit 4 (NMI) is the same line
        wr(MODE, X"10"); wait until rising_edge(clock);
        expect("app_restore (MODE NMI)", pk_app_restore, '1');
        wr(MODE, X"00"); wait until rising_edge(clock);
        expect("app_restore", pk_app_restore, '0');
        -- the 32-bit MATRIX_WASD_TO_JOY store lands on 0x30B-0x30E, not on 8-10
        wr(MATRIX + 16#0B#, X"03"); wr(MATRIX + 16#0C#, X"00");
        wr(MATRIX + 16#0D#, X"00"); wr(MATRIX + 16#0E#, X"00");
        expect("app_keys", pk_app_keys, X"0200000000000400");
        expect("app_reset", pk_app_reset, '0'); expect("app_freeze", pk_app_freeze, '0');
        for i in 0 to 7 loop wr(MATRIX + i, X"00"); end loop;
        wait until rising_edge(clock);
        expect("app_keys", pk_app_keys, X"0000000000000000");
        bfm_step <= 3;

        -- 4. joysticks as JoystickOutput::apply writes them (port | 0xE0, low = pressed)
        wr(JOY, X"EF");                             -- port 1: fire
        wr(JOY + 1, X"EB");                         -- port 2: left, fire
        wait until rising_edge(clock);
        expect("app_joy1", pk_app_joy1, "10000"); expect("app_joy2", pk_app_joy2, "10100");
        check(JOY, X"FF", X"EF");
        wr(JOY, X"FF"); wr(JOY + 1, X"FF");
        wait until rising_edge(clock);
        expect("app_joy1", pk_app_joy1, "00000"); expect("app_joy2", pk_app_joy2, "00000");
        bfm_step <= 4;

        -- 5. reset sense (run_reset_task: clear, enable bit 7)
        wr(IRQ_CLR, X"80"); wr(IRQ_EN, X"80");
        check(CLKDET, X"FF", X"01");
        check(IRQ_ACT, X"80", X"00");
        pk_sense <= '1';
        for i in 1 to 4 loop wait until rising_edge(clock); end loop;
        check(CLKDET, X"FF", X"11");
        check(IRQ_ACT, X"80", X"80");
        wr(IRQ_CLR, X"80");                         -- edge: stays clear while held
        check(IRQ_ACT, X"80", X"00");
        pk_sense <= '0';
        for i in 1 to 4 loop wait until rising_edge(clock); end loop;
        check(CLKDET, X"FF", X"01");
        check(IRQ_ACT, X"80", X"00");
        pk_sense <= '1';                            -- the next reset sets it again
        for i in 1 to 4 loop wait until rising_edge(clock); end loop;
        pk_sense <= '0';
        check(IRQ_ACT, X"80", X"80");
        wr(IRQ_CLR, X"80");
        bfm_step <= 5;

        -- 6. T59: C64_PALETTE 0x180800: entry n's R, G, B at 4n+0..2 reach pal_*,
        --    the entries past 15 and the YUV table at +0x400 do not
        wait until rising_edge(clock);
        c0 := pk_pal_cnt;
        wr(PAL, X"12");
        wait until rising_edge(clock);
        expect("pal count 1", std_logic_vector(to_unsigned(pk_pal_cnt - c0, 8)), X"01");
        expect("pal addr 0", pk_pal_addr, "000000"); expect("pal data 0", pk_pal_data, X"12");
        wr(PAL + 16#3E#, X"34");                    -- entry 15, B
        wait until rising_edge(clock);
        expect("pal count 2", std_logic_vector(to_unsigned(pk_pal_cnt - c0, 8)), X"02");
        expect("pal addr 3E", pk_pal_addr, "111110"); expect("pal data 3E", pk_pal_data, X"34");
        wr(PAL + 16#40#, X"56");                    -- entry 16: none
        wr(PAL + 16#400#, X"78");                   -- YUV table (0x180C00): dropped
        wr(PAL + 16#7FF#, X"9A");                  -- 0x180FFF
        wr(16#180000# + 16#FF#, X"00");             -- core config page: not the palette
        wr(16#181000#, X"00");                      -- PLD page: not the palette
        wait until rising_edge(clock);
        expect("pal count 3", std_logic_vector(to_unsigned(pk_pal_cnt - c0, 8)), X"02");
        bfm_step <= 6;

        bfm_errors <= errs;
        bfm_step <= 99;
        wait;
    end process;
end architecture;
