-- tests/rtl_check/mini9 (T239.3 review 9): a range arm (`when 16#21# to 255 => q <= x"00"`) answers the constant 0 at 0x25: no read.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity mn1 is
  port (
    clk   : in  std_logic;
    we    : in  std_logic;
    re    : in  std_logic;
    mode  : in  std_logic;
    addr  : in  std_logic_vector(7 downto 0);
    wdata : in  std_logic_vector(7 downto 0);
    rdata : out std_logic_vector(7 downto 0)
  );
end entity;
architecture rtl of mn1 is
  signal r : std_logic_vector(7 downto 0) := (others => '0');
  signal q : std_logic_vector(7 downto 0) := (others => '0');
begin
  rdata <= q;
  process (clk)
  begin
    if rising_edge(clk) then
      q <= x"00";
      if we = '1' then
        case addr is
          when x"20" => r <= wdata;
          when others => null;
        end case;
      elsif re = '1' then
        case to_integer(unsigned(addr)) is
          when 16#20# => q <= r;
          when 16#21# to 255 => q <= x"00";
          when others => null;
        end case;
      end if;
    end if;
  end process;
end architecture;
