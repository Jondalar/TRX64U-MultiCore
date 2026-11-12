-- T133: the cache sizes of the C128's flat build (host/cache_cfg_pkg.vhd is replaced by this file in cores/c128/mono/build_mono.tcl).
-- The C128 needed 73.5 of 75 block RAM tiles before the caches; the host's 8 KB + 8 KB do not fit with it (run 1013), 2 KB + 2 KB add about one tile.
package cache_cfg_pkg is
    constant c_dcache_on   : boolean := true;
    constant c_dcache_bits : natural := 11;     -- 2 KB
    constant c_icache_bits : natural := 11;     -- 2 KB
    constant c_line_bits   : natural := 2;
end package;
