-- T44: tools/flashgate_test.py end to end, without the board. Gideon's rvlite
-- (as i_cpu in ultimate_sys.vhd, started at 0x30000 instead of the boot ROM)
-- runs tools/flashgate/mailbox.S out of a memory model; the IO window
-- 0x10060200 goes through flash_gate.vhd and spi_peripheral_io to a flash
-- model. The host side reads the access lists the script wrote (g_lists),
-- puts each into the mailbox as the script does over JTAG, and writes status
-- and result bytes per list to g_out for the script to check.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.textio.all;

library work;
use work.io_bus_pkg.all;
use work.mem_bus_pkg.all;

entity tb_cpu is
generic (
    g_wren_pass : boolean := false;
    g_variant   : natural := 0;
    g_prog      : string := "prog.hex";       -- helper, one hex word per line, from 0x30000
    g_lists     : string := "lists.txt";      -- "L n" then n hex op words, per list
    g_out       : string := "results.txt" );
end entity;

architecture tb of tb_cpu is
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
    signal spi_req  : t_io_req;
    signal spi_resp : t_io_resp;
    signal busy, ssn, csn, sck, mosi, miso : std_logic;
    signal is_fl    : std_logic := '0';
    signal other_ack: std_logic := '0';

    -- 0x00000-0x3FFFF, shared between the memory port and the host process
    type t_mem is protected
        procedure put(a : natural; v : std_logic_vector(31 downto 0); be : std_logic_vector(3 downto 0));
        impure function get(a : natural) return std_logic_vector;
    end protected;
    type t_mem is protected body
        type t_arr is array(0 to 16#FFFF#) of std_logic_vector(31 downto 0);
        variable m : t_arr := (others => (others => '0'));
        procedure put(a : natural; v : std_logic_vector(31 downto 0); be : std_logic_vector(3 downto 0)) is
        begin
            for i in 0 to 3 loop
                if be(i) = '1' then
                    m((a / 4) mod 16#10000#)(i * 8 + 7 downto i * 8) := v(i * 8 + 7 downto i * 8);
                end if;
            end loop;
        end procedure;
        impure function get(a : natural) return std_logic_vector is
        begin
            return m((a / 4) mod 16#10000#);
        end function;
    end protected body;
    shared variable mem : t_mem;

    constant MBOX : natural := 16#38000#;
begin
    clock <= not clock after 5 ns when not done;

    i_cpu: entity work.rvlite_wrapper
    generic map (
        g_start_addr => X"00030000",
        g_bootrom    => false )
    port map (
        clock    => clock,
        reset    => reset,
        irq_i    => '0',
        io_req   => io_req,
        io_resp  => io_resp,
        io_busy  => open,
        mem_req  => mem_req,
        mem_resp => mem_resp );

    -- IO: the flash window as decoded in ultimate_sys.vhd; anything else acked
    process(io_req, is_fl)
    begin
        fl_req <= io_req;
        if io_req.address(23 downto 8) /= X"0602" then
            fl_req.read <= '0'; fl_req.write <= '0';
        end if;
    end process;
    process(clock)
    begin
        if rising_edge(clock) then
            other_ack <= '0';
            if io_req.read = '1' or io_req.write = '1' then
                if io_req.address(23 downto 8) = X"0602" then
                    is_fl <= '1';
                else
                    is_fl <= '0';
                    other_ack <= '1';
                    report "IO access outside the flash window at 0x" & to_hstring(io_req.address)
                        severity error;
                end if;
            end if;
        end if;
    end process;
    io_resp <= fl_resp when is_fl = '1' else (data => X"00", ack => other_ack, nc => '0');

    i_gate: entity work.flash_gate
    generic map (g_wren_pass => g_wren_pass)
    port map (
        clock => clock, reset => reset, cpu_req => fl_req, cpu_resp => fl_resp,
        spi_req => spi_req, spi_resp => spi_resp, spi_busy => busy, spi_ssn => ssn,
        flash_csn => csn );

    i_flash: entity work.spi_peripheral_io
    generic map (g_fixed_rate => true, g_init_rate => 1, g_crc => false)
    port map (
        clock => clock, reset => reset, io_req => spi_req, io_resp => spi_resp, busy => busy,
        SPI_SSn => ssn, SPI_CLK => sck, SPI_MOSI => mosi, SPI_MISO => miso );

    i_model: entity work.flash_model
    generic map (g_variant => g_variant)
    port map (cfg => reset, csn => csn, sck => sck, mosi => mosi, miso => miso,
              hash => open, viol => open, n_xfer => open, n_exec => open, n_chk => open,
              n_sub => open, wel => open, last_op => open, last_addr => open);

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

    -- host: as tools/flashgate_test.py over JTAG
    process
        file fp, fl, fo : text;
        variable l, lo  : line;
        variable w      : std_logic_vector(31 downto 0);
        variable a, n   : natural;
        variable tag    : character;
        variable seq    : natural := 0;
        variable cycles : natural;
        variable nres   : natural;
        variable ok     : boolean;
    begin
        file_open(fp, g_prog, read_mode);
        a := 16#30000#;
        while not endfile(fp) loop
            readline(fp, l);
            hread(l, w);
            mem.put(a, w, "1111");
            a := a + 4;
        end loop;
        reset <= '1';
        wait for 100 ns;
        wait until rising_edge(clock);
        reset <= '0';
        cycles := 0;
        while mem.get(MBOX + 8) /= X"464C4741" loop
            wait until rising_edge(clock);
            cycles := cycles + 1;
            assert cycles < 100000 report "helper did not start" severity failure;
        end loop;

        file_open(fl, g_lists, read_mode);
        file_open(fo, g_out, write_mode);
        while not endfile(fl) loop
            readline(fl, l);
            read(l, tag);
            read(l, n);
            for i in 0 to n - 1 loop
                readline(fl, l);
                hread(l, w);
                mem.put(MBOX + 16#100# + 4 * i, w, "1111");
            end loop;
            mem.put(MBOX + 16#100# + 4 * n, X"00000000", "1111");
            seq := seq + 1;
            mem.put(MBOX, std_logic_vector(to_unsigned(seq, 32)), "1111");
            cycles := 0;
            while to_integer(unsigned(mem.get(MBOX + 4))) /= seq loop
                wait until rising_edge(clock);
                cycles := cycles + 1;
                assert cycles < 2000000 report "helper did not finish a list" severity failure;
            end loop;
            nres := to_integer(unsigned(mem.get(MBOX + 16)));
            write(lo, string'("R ") & integer'image(to_integer(unsigned(mem.get(MBOX + 12)))) &
                      " " & integer'image(nres));
            writeline(fo, lo);
            for i in 0 to nres - 1 loop
                w := mem.get(MBOX + 16#2000# + i);
                hwrite(lo, w((i mod 4) * 8 + 7 downto (i mod 4) * 8));
                writeline(fo, lo);
            end loop;
        end loop;
        file_close(fo);
        report "lists done: " & integer'image(seq);
        done <= true;
        wait;
    end process;
end architecture;
