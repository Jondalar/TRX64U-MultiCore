-- T133: the sizes of the host RISC-V's caches as constants of the build. The wrapper (rvlite_wrapper_dc.vhd) takes its generic defaults from here, so a
-- build that needs other sizes (a large core that is short of block RAM) lists its own copy of this package in place of this file in its source list
-- (cores/<id>/mono/cache_cfg_pkg.vhd; host/gideon_vhdl.tcl names this one) and nothing under host/ changes.
package cache_cfg_pkg is
    constant c_dcache_on   : boolean := true;
    constant c_dcache_bits : natural := 13;     -- bytes = 2**bits
    constant c_icache_bits : natural := 13;
    constant c_line_bits   : natural := 2;      -- log2 of the words per line (2 = 16 bytes filled from the DDR2 read's 128 bits, 0 = one word)
end package;
