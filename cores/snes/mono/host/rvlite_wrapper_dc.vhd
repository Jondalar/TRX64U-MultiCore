--------------------------------------------------------------------------------
-- T133: Gideon's rvlite_wrapper (vendor/gideon, patch 0005 applied: dbg_pc) with a
-- write-through data cache (rv_dcache.vhd) between the core's data port and the
-- data-side bus converter. Entity name and the old ports are unchanged, so the
-- benches whose BFMs bind rvlite_wrapper keep working; the new ports have defaults.
-- host/gideon_vhdl.tcl lists this file instead of the vendored wrapper.
--
-- Original description: This wrapper module combines the processor core with
--              instruction cache, bus converters to the bus definition used in
--              the Ultimate, and adds an optional boot rom at address 0.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
    use work.mem_bus_pkg.all;
    use work.io_bus_pkg.all;
    use work.core_pkg.all;
    use work.cache_cfg_pkg.all;

entity rvlite_wrapper is
generic (
    g_mult          : boolean := true;
    g_gprf_ram      : string := "auto";
    g_start_addr    : std_logic_vector(31 downto 0) := X"80000000";
    g_bootrom       : boolean := true;
    g_icache        : boolean := true;
    g_dcache        : boolean := c_dcache_on;       -- T133: the cache sizes come from cache_cfg_pkg.vhd (a build may list its own copy)
    g_dcache_bits   : natural := c_dcache_bits;
    g_icache_bits   : natural := c_icache_bits;     -- (Gideon's icache: 2 KB)
    g_line_bits     : natural := c_line_bits;       -- log2 of the words per cache line (2 = 16-byte lines filled from the DDR2 read's 128 bits; 0 = one word)
    g_tag_i         : std_logic_vector(7 downto 0) := X"20";
    g_tag_d         : std_logic_vector(7 downto 0) := X"21" );
port (
    clock           : in    std_logic;
    reset           : in    std_logic;

    irq_i           : in    std_logic := '0';

    io_req          : out   t_io_req;
    io_resp         : in    t_io_resp;
    io_busy         : out   std_logic;

    mem_req         : out   t_mem_req_32;
    mem_resp        : in    t_mem_resp_32;

    dbg_pc          : out   std_logic_vector(31 downto 0);

    -- data cache (T133): writes by other masters on the memory bus behind this wrapper, the toggle of
    -- the writers that are not on that bus, and two counters
    dc_snoop_valid  : in    std_logic := '0';
    dc_snoop_addr   : in    std_logic_vector(25 downto 0) := (others => '0');
    dc_ext_tgl      : in    std_logic := '0';
    dc_stat         : out   std_logic_vector(23 downto 0);
    dc_ctl          : in    std_logic_vector(15 downto 0) := X"0007";   -- rv_dcache dbg_ctl
    dc_data         : out   std_logic_vector(31 downto 0);
    dc_line         : in    std_logic_vector(127 downto 0) := (others => '0');   -- line fill (cpu_mem_bridge)
    dc_line_adr     : in    std_logic_vector(21 downto 0) := (others => '0') );

end entity;

architecture arch of rvlite_wrapper is

    signal dmem_o      : t_dmem_req;
    signal dmem_i      : t_dmem_resp;
    signal bmem_o      : t_dmem_req;    -- after the data cache
    signal bmem_i      : t_dmem_resp;
    signal imem_o      : t_dmem_req;
    signal imem_i      : t_dmem_resp;
    signal cimem_o     : t_imem_req;
    signal cimem_i     : t_imem_resp;
    signal ic_stall    : std_logic := '0';
    signal ic_miss     : std_logic := '0';
    signal ic_line     : std_logic_vector(25 downto 4) := (others => '0');

    signal dmem_req    : t_mem_req_32;
    signal dmem_resp   : t_mem_resp_32;
    signal imem_req    : t_mem_req_32;
    signal imem_resp   : t_mem_resp_32;

    signal ibram_req   : t_bram_req;
    signal ibram_resp  : t_bram_resp;
    signal dbram_req   : t_bram_req;
    signal dbram_resp  : t_bram_resp;
begin
    i_core: entity work.core
    generic map (
        g_mult       => g_mult,
        g_gprf_ram   => g_gprf_ram,
        g_start_addr => g_start_addr )
    port map (
        clock     => clock,
        reset     => reset,
        irq       => irq_i,
        imem_req  => cimem_o,
        imem_resp => cimem_i,
        dmem_req  => dmem_o,
        dmem_resp => dmem_i,
        dbg_pc    => dbg_pc
    );

    r_dcache: if g_dcache generate
        i_dcache: entity work.rv_dcache
        generic map (
            g_size_bits => g_dcache_bits,
            g_line_bits => g_line_bits )
        port map (
            clock       => clock,
            reset       => reset,
            dmem_i      => dmem_o,
            dmem_o      => dmem_i,
            mem_o       => bmem_o,
            mem_i       => bmem_i,
            snoop_valid => dc_snoop_valid,
            snoop_addr  => dc_snoop_addr,
            ext_tgl     => dc_ext_tgl,
            line_i      => dc_line,
            line_adr_i  => dc_line_adr,
            ic_stall    => ic_stall,
            ic_miss     => ic_miss,
            ic_line     => ic_line,
            dbg_ctl     => dc_ctl,
            dbg_data    => dc_data,
            stat_hit    => dc_stat(11 downto 0),
            stat_miss   => dc_stat(23 downto 12) );
    end generate;

    r_no_dcache: if not g_dcache generate
        bmem_o  <= dmem_o;
        dmem_i  <= bmem_i;
        dc_stat <= (others => '0');
        dc_data <= (others => '0');
    end generate;

    r_icache: if g_icache generate
        i_cache: entity work.rv_icache
        generic map (
            g_size_bits => g_icache_bits,
            g_line_bits => g_line_bits )
        port map (
            clock  => clock,
            reset  => reset,

            disable => '0',

            dmem_i  => cimem_o,
            dmem_o  => cimem_i,

            mem_o  => imem_o,
            mem_i  => imem_i,
            i_stall => ic_stall,
            i_miss  => ic_miss,
            i_line  => ic_line,
            line_i  => dc_line,
            line_adr_i => dc_line_adr );
        end generate;

    r_no_icache: if not g_icache generate
        imem_o.adr_o <= cimem_o.adr_o;
        imem_o.ena_o <= cimem_o.ena_o;
        imem_o.sel_o <= X"F";
        imem_o.we_o  <= '0';
        imem_o.dat_o <= (others => '0');

        cimem_i.dat_i <= imem_i.dat_i;
        cimem_i.ena_i <= imem_i.ena_i;
    end generate;

    i_imem: entity work.bus_converter
    generic map (
        g_tag        => g_tag_i,
        g_bootrom    => g_bootrom,
        g_bootaddr   => g_start_addr(31 downto 16),
        g_support_io => false )
    port map (
        clock        => clock,
        reset        => reset,
        dmem_req     => imem_o,
        dmem_resp    => imem_i,
        mem_req      => imem_req,
        mem_resp     => imem_resp,
        bram_req     => ibram_req,
        bram_resp    => ibram_resp,
        io_req       => open,
        io_resp      => c_io_resp_init );

    i_dmem: entity work.bus_converter
    generic map (
        g_tag        => g_tag_d,
        g_io_bit     => 28,
        g_bootrom    => g_bootrom,
        g_bootaddr   => g_start_addr(31 downto 16),
        g_support_io => true )
    port map (
        clock        => clock,
        reset        => reset,
        dmem_req     => bmem_o,
        dmem_resp    => bmem_i,
        mem_req      => dmem_req,
        mem_resp     => dmem_resp,
        bram_req     => dbram_req,
        bram_resp    => dbram_resp,
        io_busy      => io_busy,
        io_req       => io_req,
        io_resp      => io_resp );

    i_arb: entity work.mem_bus_arbiter_pri_32
    generic map (
        g_registered => false,
        g_ports      => 2 )
    port map (
        clock        => clock,
        reset        => reset,
        reqs(0)      => dmem_req,
        reqs(1)      => imem_req,
        resps(0)     => dmem_resp,
        resps(1)     => imem_resp,
        req          => mem_req,
        resp         => mem_resp );

    r_boot: if g_bootrom generate
        i_bootrom: entity work.bootrom
        port map (
            clock    => clock,
            reset    => reset,
            ireq     => ibram_req,
            iresp    => ibram_resp,
            dreq     => dbram_req,
            dresp    => dbram_resp
        );
    end generate;

end architecture;
