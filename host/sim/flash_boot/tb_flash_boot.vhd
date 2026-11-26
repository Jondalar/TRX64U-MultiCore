-- T15: the boot ROM (host/bootrom_pkg.vhd) on Gideon's rvlite, as
-- i_cpu in ultimate_sys.vhd starts it, with Gideon's spi_peripheral_io set up
-- as i_flash and a read-only flash model holding g_flash. The memory model is
-- a sparse 64 MB DDR2; g_pre lists words put into it before the reset ends
-- ("hexaddr hexword" per line: the boot words a JTAG load or request writes,
-- a staged image). IO stubs: ITU (UART, buttons, capabilities), board
-- revision, MIG calibrated, the keyboard scan register, our page 0 (CAPS2,
-- BUILD). The run ends when the console prints "Waiting for JTAG image." or
-- "Lock", when the test app's marker appears at 0xFFB0, or at the time limit.
-- g_out gets the console, the result block, the marker, the flash model's
-- counters and the words of [g_cmp_addr, g_cmp_addr + 4 * g_cmp_words) for
-- run.py to check.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.textio.all;

library work;
use work.io_bus_pkg.all;
use work.mem_bus_pkg.all;

entity tb_flash_boot is
generic (
    g_flash     : string := "flash.bin";
    g_pre       : string := "pre.txt";
    g_out       : string := "out.txt";
    g_caps      : std_logic_vector(31 downto 0) := X"25C40222";
    g_caps2     : std_logic_vector(7 downto 0) := X"03";
    g_build     : std_logic_vector(31 downto 0) := X"00000000";
    g_button2   : boolean := false;
    g_cmp_addr  : natural := 16#30000#;
    g_cmp_words : natural := 0;
    g_max_us    : natural := 20000;
    g_trace     : boolean := false );
end entity;

architecture tb of tb_flash_boot is
    signal clock    : std_logic := '0';
    signal reset    : std_logic := '1';
    signal done     : boolean := false;
    signal io_req   : t_io_req;
    signal io_resp  : t_io_resp;
    signal mem_req  : t_mem_req_32;
    signal mem_resp : t_mem_resp_32 := (data => (others => '0'), rack => '0',
                                        rack_tag => X"00", dack_tag => X"00");
    signal fl_req   : t_io_req;
    signal fl_resp  : t_io_resp;
    signal csn, sck, mosi, miso : std_logic;
    signal is_fl    : std_logic := '0';
    signal o_ack    : std_logic := '0';
    signal o_data   : std_logic_vector(7 downto 0) := X"00";
    signal viol, n_xfer, n_read : natural;
    signal waiting  : boolean := false;

    -- 64 MB in 64 KB pages, made on first use
    type t_mem is protected
        procedure put(a : natural; v : std_logic_vector(31 downto 0); be : std_logic_vector(3 downto 0));
        impure function get(a : natural) return std_logic_vector;
    end protected;
    type t_mem is protected body
        type t_page is array(0 to 16#3FFF#) of std_logic_vector(31 downto 0);
        type t_page_p is access t_page;
        type t_pages is array(0 to 1023) of t_page_p;
        variable p : t_pages;
        procedure put(a : natural; v : std_logic_vector(31 downto 0); be : std_logic_vector(3 downto 0)) is
            variable pg : natural := (a / 65536) mod 1024;
            variable w  : natural := (a / 4) mod 16#4000#;
        begin
            if p(pg) = null then
                p(pg) := new t_page;
                for i in t_page'range loop
                    p(pg)(i) := X"00000000";
                end loop;
            end if;
            for i in 0 to 3 loop
                if be(i) = '1' then
                    p(pg)(w)(i * 8 + 7 downto i * 8) := v(i * 8 + 7 downto i * 8);
                end if;
            end loop;
        end procedure;
        impure function get(a : natural) return std_logic_vector is
            variable pg : natural := (a / 65536) mod 1024;
        begin
            if p(pg) = null then
                return X"00000000";
            end if;
            return p(pg)((a / 4) mod 16#4000#);
        end function;
    end protected body;
    shared variable mem : t_mem;

    shared variable con : line;         -- console text of the whole run
begin
    clock <= not clock after 5 ns when not done;

    i_cpu: entity work.rvlite_wrapper
    port map (
        clock    => clock,
        reset    => reset,
        irq_i    => '0',
        io_req   => io_req,
        io_resp  => io_resp,
        io_busy  => open,
        mem_req  => mem_req,
        mem_resp => mem_resp );

    -- IO: the flash window as decoded in ultimate_sys.vhd, stubs for the rest
    process(io_req)
    begin
        fl_req <= io_req;
        if io_req.address(23 downto 8) /= X"0602" then
            fl_req.read <= '0'; fl_req.write <= '0';
        end if;
    end process;

    process(clock)
        variable a    : natural;
        variable line_s : line;
        variable c    : character;
    begin
        if rising_edge(clock) then
            o_ack <= '0';
            o_data <= X"00";
            if io_req.read = '1' or io_req.write = '1' then
                if io_req.address(23 downto 8) = X"0602" then
                    is_fl <= '1';
                else
                    is_fl <= '0';
                    o_ack <= '1';
                    a := to_integer(io_req.address);
                    if io_req.read = '1' then
                        case a is
                        when 16#00000A# =>
                            if g_button2 then o_data <= X"80"; end if;
                        when 16#00000C# => o_data <= g_caps(31 downto 24);
                        when 16#00000D# => o_data <= g_caps(23 downto 16);
                        when 16#00000E# => o_data <= g_caps(15 downto 8);
                        when 16#00000F# => o_data <= g_caps(7 downto 0);
                        when 16#000012# => o_data <= X"00";        -- UART not full
                        when 16#10000C# => o_data <= X"B8";        -- board revision
                        when 16#100100# => o_data <= X"01";        -- MIG calibrated
                        when 16#1A0005# => o_data <= g_caps2;
                        when 16#1A0008# => o_data <= g_build(7 downto 0);
                        when 16#1A0009# => o_data <= g_build(15 downto 8);
                        when 16#1A000A# => o_data <= g_build(23 downto 16);
                        when 16#1A000B# => o_data <= g_build(31 downto 24);
                        when others =>
                            report "IO read at 0x" & to_hstring(io_req.address) severity note;
                        end case;
                    else
                        case a is
                        when 16#000010# =>
                            c := character'val(to_integer(unsigned(io_req.data)));
                            if c = LF then
                                if con = null then write(con, string'("")); end if;
                                if line_s /= null then
                                    report "console: " & line_s.all;
                                    if line_s.all = "Waiting for JTAG image." or line_s.all = "Lock" then
                                        waiting <= true;
                                    end if;
                                    write(con, line_s.all & "|");
                                    deallocate(line_s);
                                else
                                    write(con, string'("|"));
                                end if;
                            elsif c /= CR then
                                write(line_s, c);
                            end if;
                        when 16#100708# => null;                   -- keyboard scan off
                        when others =>
                            report "IO write at 0x" & to_hstring(io_req.address) severity note;
                        end case;
                    end if;
                end if;
            end if;
        end if;
    end process;
    io_resp <= fl_resp when is_fl = '1' else (data => o_data, ack => o_ack, nc => '0');

    i_flash: entity work.spi_peripheral_io
    generic map (g_fixed_rate => true, g_init_rate => 1, g_crc => false)
    port map (
        clock => clock, reset => reset, io_req => fl_req, io_resp => fl_resp, busy => open,
        SPI_SSn => csn, SPI_CLK => sck, SPI_MOSI => mosi, SPI_MISO => miso );

    i_model: entity work.flash_rom_model
    generic map (g_file => g_flash, g_trace => g_trace)
    port map (csn => csn, sck => sck, mosi => mosi, miso => miso,
              viol => viol, n_xfer => n_xfer, n_read => n_read);

    -- memory port: rack in the clock after the request, read data one later
    process(clock)
        variable pend : boolean := false;
        variable ptag : std_logic_vector(7 downto 0);
        variable padr : natural;
    begin
        if rising_edge(clock) then
            mem_resp.rack     <= '0';
            mem_resp.rack_tag <= X"00";
            mem_resp.dack_tag <= X"00";
            if pend then
                mem_resp.dack_tag <= ptag;
                mem_resp.data     <= mem.get(padr);
                pend := false;
            end if;
            if mem_req.request = '1' and mem_resp.rack = '0' then
                mem_resp.rack     <= '1';
                mem_resp.rack_tag <= mem_req.tag;
                if mem_req.read_writen = '1' then
                    pend := true;
                    ptag := mem_req.tag;
                    padr := to_integer(mem_req.address);
                else
                    mem.put(to_integer(mem_req.address), mem_req.data, mem_req.byte_en);
                end if;
            end if;
        end if;
    end process;

    -- host: the words a JTAG load would write with the CPU in reset, then run
    process
        file fp, fo   : text;
        variable st   : file_open_status;
        variable l    : line;
        variable a, w : std_logic_vector(31 downto 0);
        variable n_us : natural := 0;
        variable why  : line;
    begin
        file_open(st, fp, g_pre, read_mode);
        if st = open_ok then
            while not endfile(fp) loop
                readline(fp, l);
                if l'length > 0 then
                    hread(l, a);
                    hread(l, w);
                    mem.put(to_integer(unsigned(a(25 downto 0))), w, "1111");
                end if;
            end loop;
            file_close(fp);
        end if;
        reset <= '1';
        wait for 200 ns;
        wait until rising_edge(clock);
        reset <= '0';
        loop
            wait for 1 us;
            n_us := n_us + 1;
            if waiting then
                write(why, string'("waiting"));
                wait for 20 us;                 -- the wait loop must not leave
                exit;
            elsif mem.get(16#FFB0#) /= X"00000000" then
                write(why, string'("marker"));
                wait for 5 us;
                exit;
            elsif n_us >= g_max_us then
                write(why, string'("timeout"));
                exit;
            end if;
        end loop;
        file_open(fo, g_out, write_mode);
        write(l, string'("END ") & why.all & " " & integer'image(n_us)); writeline(fo, l);
        if con = null then write(con, string'("")); end if;
        write(l, string'("CONSOLE ") & con.all); writeline(fo, l);
        write(l, string'("RES"));
        for i in 0 to 15 loop
            write(l, string'(" "));
            hwrite(l, mem.get(16#FFC0# + 4 * i));
        end loop;
        writeline(fo, l);
        write(l, string'("GUARD"));
        for i in 0 to 15 loop
            write(l, string'(" "));
            hwrite(l, mem.get(16#FF60# + 4 * i));
        end loop;
        writeline(fo, l);
        write(l, string'("MARKER ")); hwrite(l, mem.get(16#FFB0#)); writeline(fo, l);
        write(l, string'("BOOTWORDS ")); hwrite(l, mem.get(16#FFFC#)); write(l, string'(" "));
        hwrite(l, mem.get(16#FFF4#)); writeline(fo, l);
        write(l, string'("FLASH ") & integer'image(viol) & " " & integer'image(n_xfer) & " " &
                 integer'image(n_read));
        writeline(fo, l);
        for i in 0 to g_cmp_words - 1 loop
            hwrite(l, mem.get(g_cmp_addr + 4 * i));
            writeline(fo, l);
        end loop;
        file_close(fo);
        done <= true;
        wait;
    end process;
end architecture;
