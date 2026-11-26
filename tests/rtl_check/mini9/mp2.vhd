-- tests/rtl_check/mini9 (T239.3 review 9): a process function without parameters called without parentheses (`q <= zb`): refused.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity mp2 is
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
architecture rtl of mp2 is
  signal r : std_logic_vector(7 downto 0) := (others => '0');
  signal q : std_logic_vector(7 downto 0) := (others => '0');
begin
  rdata <= q;
  process (clk)
    function zb return std_logic_vector is
    begin
      return x"00";
    end function;
  begin
    if rising_edge(clk) then
      q <= zb;
      if we = '1' then
        case addr is
          when x"20" => r <= wdata;
          when others => null;
        end case;
      elsif re = '1' then
        case addr is
          when x"20" => q <= r;
          when others => null;
        end case;
      end if;
    end if;
  end process;
end architecture;
