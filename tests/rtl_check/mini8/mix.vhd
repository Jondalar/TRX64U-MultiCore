-- tests/rtl_check/mini8 (T239.3 review 8): bit_of(fl, o - 16#30#) for o 0x2F and 0x3F indexes outside the 8 bits of fl: refused, not a 0.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
entity mix is
  port (clk : in std_logic; we : in std_logic; re : in std_logic; addr : in std_logic_vector(7 downto 0);
        wdata : in std_logic_vector(7 downto 0); rdata : out std_logic_vector(7 downto 0));
end entity;
architecture rtl of mix is
  signal fl : std_logic_vector(7 downto 0) := (others => '0');
  signal q  : std_logic_vector(7 downto 0) := (others => '0');
begin
  rdata <= q;
  process (clk)
    variable o : integer;
    function bit_of(v : std_logic_vector; i : integer) return std_logic is
    begin
      return v(i);
    end function;
  begin
    if rising_edge(clk) then
      if we = '1' then
        case addr is when x"30" => fl <= wdata; when others => null; end case;
      end if;
      q <= x"00";
      if re = '1' then
        o := to_integer(unsigned(addr));
        case o is
          when 16#2F# to 16#3F# => q <= "0000000" & bit_of(fl, o - 16#30#);
          when others => null;
        end case;
      end if;
    end if;
  end process;
end architecture;
