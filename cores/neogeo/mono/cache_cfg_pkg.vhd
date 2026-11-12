-- T143 (from cores/c128/mono/cache_cfg_pkg.vhd, T133): the cache sizes of the Neo Geo's flat build when the host's 8 KB + 8 KB do not fit (select with TRX_CACHE_PKG=1).
-- host/cache_cfg_pkg.vhd is replaced by this file in cores/neogeo/mono/build_mono.tcl; nothing under host/ changes.
package cache_cfg_pkg is
    constant c_dcache_on   : boolean := true;
    constant c_dcache_bits : natural := 11;     -- 2 KB
    constant c_icache_bits : natural := 11;     -- 2 KB
    constant c_line_bits   : natural := 2;
end package;
