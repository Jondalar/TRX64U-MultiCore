-- TRX Apple //e core (T89 D5): the three built-in 16 colour palettes of the artifact decode, indexed by the 4 bit value
-- (the last four video bits, rotated by the 14M pixel phase), as RRGGBB. DERIVED from NanoApple2 src/vga_controller.vhd
-- (4977e049, GPL v3, Szombathelyi / Edwards; colour theory of Linards Ticmanis and Newsdee, see rtl/NOTICE): the same
-- values, in a table instead of a case. The custom palette (palette select 3) is the app's: 16 x RGB in the registers.
library ieee;
use ieee.std_logic_1164.all;

package a2_palette_pkg is
  type pal_t is array (0 to 15) of std_logic_vector(23 downto 0);
  constant PAL_NTSC : pal_t := (
    x"000000",
    x"496500",
    x"9F1B48",
    x"D87300",
    x"4832EB",
    x"818181",
    x"D643EF",
    x"FB8FBC",
    x"197544",
    x"3CCC00",
    x"818181",
    x"BCD600",
    x"3692FF",
    x"6CE6B8",
    x"D89EFF",
    x"F1F1F1");
  constant PAL_IIGS : pal_t := (
    x"000000",
    x"916400",
    x"DB1F42",
    x"FA7700",
    x"0C11A4",
    x"B3B3B3",
    x"DC43E1",
    x"FBA593",
    x"1C8231",
    x"40DE00",
    x"636363",
    x"FEFE00",
    x"393DFF",
    x"67FCA4",
    x"7AB3FF",
    x"FFFFFF");
  constant PAL_APPLEWIN : pal_t := (
    x"000000",
    x"624C00",
    x"930B7C",
    x"F9561D",
    x"1F35D3",
    x"7E7E7E",
    x"BB36FF",
    x"FF81EC",
    x"00760C",
    x"43C800",
    x"7E7E7E",
    x"DCCD16",
    x"07A8E0",
    x"5DF784",
    x"9DACFF",
    x"FFFFFF");
  -- shift_color value -> index of the custom palette entry (the Nano's CURRENT_COL0..15)
  type cmap_t is array (0 to 15) of integer range 0 to 15;
  constant CUSTOM_MAP : cmap_t := (0, 8, 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15);
end package;
