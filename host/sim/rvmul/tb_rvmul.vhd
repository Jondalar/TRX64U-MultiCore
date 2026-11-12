--------------------------------------------------------------------------------
-- rvlite core (the vendored core.vhd with our patches) running a generated
-- RV32IM program (gen.py) against memory models that behave like bus_converter:
-- instruction bus hits in one cycle, data bus answers a load/store with 1..4
-- wait cycles (dmem_resp.ena_i low, so the writeback stage stalls). A store to
-- 0x30000000 ends the run; the data memory is dumped as hex words for gen.py,
-- which compares it to its reference model.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.textio.all;
use std.env.all;
use work.core_pkg.all;

entity tb_rvmul is
generic (
    g_prog  : string := "prog.hex";
    g_out   : string := "dmem.hex";
    g_trace : string := "trace.txt";
    g_waits : boolean := true;
    g_max   : natural := 400000
);
end entity;

architecture sim of tb_rvmul is
    constant c_words : natural := 4096;
    type t_mem is array(0 to c_words - 1) of std_logic_vector(31 downto 0);

    impure function load_prog return t_mem is
        file f : text open read_mode is g_prog;
        variable l : line;
        variable m : t_mem := (others => X"00000013");
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

    signal imem     : t_mem := load_prog;
    signal dmem     : t_mem := (others => (others => '0'));
    signal clock    : std_logic := '0';
    signal reset    : std_logic := '1';
    signal imem_req : t_imem_req;
    signal imem_resp: t_imem_resp;
    signal dmem_req : t_dmem_req;
    signal dmem_resp: t_dmem_resp;
    signal dbg_pc   : std_logic_vector(31 downto 0);
    signal cycles   : natural := 0;
    signal busy     : natural := 0;
    signal lfsr     : unsigned(15 downto 0) := X"ACE1";
begin
    clock <= not clock after 5 ns;
    reset <= '0' after 55 ns;

    i_core: entity work.core
    generic map ( g_start_addr => X"00000000" )
    port map (
        clock => clock, reset => reset, irq => '0',
        imem_req => imem_req, imem_resp => imem_resp,
        dmem_req => dmem_req, dmem_resp => dmem_resp );

    -- instruction bus: data one cycle after the request, always acknowledged
    imem_resp.ena_i <= '1';
    process(clock)
    begin
        if rising_edge(clock) then
            if imem_req.ena_o = '1' then
                imem_resp.dat_i <= imem(to_integer(unsigned(imem_req.adr_o(13 downto 2))));
            end if;
        end if;
    end process;

    -- data bus
    dmem_resp.ena_i <= '1' when busy = 0 else '0';

    process(clock)
        variable adr  : natural;
        variable w    : std_logic_vector(31 downto 0);
        variable k    : natural;
        variable l    : line;
        file f        : text;
        file ft       : text open write_mode is g_trace;
        variable lt   : line;
        variable pend_adr : natural := 0;
        variable pend_rd  : boolean := false;
    begin
        if rising_edge(clock) then
            cycles <= cycles + 1;
            lfsr <= lfsr(14 downto 0) & (lfsr(15) xor lfsr(13) xor lfsr(12) xor lfsr(10));
            if cycles = g_max then
                report "TB: timeout" severity failure;
            end if;
            if busy > 0 then
                busy <= busy - 1;
                if busy = 1 and pend_rd then
                    dmem_resp.dat_i <= dmem(pend_adr);
                end if;
            elsif dmem_req.ena_o = '1' and reset = '0' then
                if dmem_req.adr_o(31 downto 28) = X"3" then
                    report "TB: done after " & integer'image(cycles) & " cycles";
                    file_open(f, g_out, write_mode);
                    for i in 0 to c_words - 1 loop
                        hwrite(l, dmem(i));
                        writeline(f, l);
                    end loop;
                    file_close(f);
                    finish;
                end if;
                adr := to_integer(unsigned(dmem_req.adr_o(13 downto 2)));
                k := 1;
                if g_waits then
                    k := 1 + to_integer(lfsr(1 downto 0));
                end if;
                if dmem_req.we_o = '1' then
                    w := dmem(adr);
                    for b in 0 to 3 loop
                        if dmem_req.sel_o(b) = '1' then
                            w(8 * b + 7 downto 8 * b) := dmem_req.dat_o(8 * b + 7 downto 8 * b);
                        end if;
                    end loop;
                    dmem(adr) <= w;
                    hwrite(lt, dmem_req.adr_o);
                    write(lt, string'(" "));
                    hwrite(lt, w);
                    writeline(ft, lt);
                    pend_rd := false;
                    busy <= k;
                else
                    pend_adr := adr;
                    pend_rd := true;
                    dmem_resp.dat_i <= (others => '0');
                    busy <= k;
                end if;
            end if;
        end if;
    end process;
end architecture;
