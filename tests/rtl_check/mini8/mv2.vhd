-- tests/rtl_check/mini8 (T239.3 review 8): o is the address on one path (case mode) and 0 on the other; the merged o (0x25 or 0) matches 0x20 by its unknown bits: refused.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity mv2 is
  port (
    clk   : in  std_logic;
    we    : in  std_logic;
    re    : in  std_logic;
    addr  : in  std_logic_vector(7 downto 0);
    wdata : in  std_logic_vector(7 downto 0);
    rdata : out std_logic_vector(7 downto 0)
  );
end entity;
architecture rtl of mv2 is
  signal r    : std_logic_vector(7 downto 0) := (others => '0');
  signal q    : std_logic_vector(7 downto 0) := (others => '0');
  signal mode : std_logic := '0';
begin
  rdata <= q;
  process (clk)
    variable o : integer;
  begin
    if rising_edge(clk) then
      if we = '1' then
        case mode is
          when '0' => o := to_integer(unsigned(addr));
          when others => o := 0;
        end case;
        case o is
          when 16#20# => r <= wdata;
          when 16#2F# => mode <= wdata(0);
          when others => null;
        end case;
      end if;
      q <= x"00";
      if re = '1' then
        case addr is
          when x"20" => q <= r;
          when others => null;
        end case;
      end if;
    end if;
  end process;
end architecture;
