-- tests/rtl_check/mini8 (T239.3 review 8): lo, assigned 0 before, takes addr(3 downto 0) inside the arm, then case lo: the arm is no last decision, its 0 no decoded read.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity mh1 is
  port (
    clk   : in  std_logic;
    we    : in  std_logic;
    re    : in  std_logic;
    addr  : in  std_logic_vector(7 downto 0);
    wdata : in  std_logic_vector(7 downto 0);
    rdata : out std_logic_vector(7 downto 0)
  );
end entity;
architecture rtl of mh1 is
  signal r : std_logic_vector(7 downto 0) := (others => '0');
  signal q : std_logic_vector(7 downto 0) := (others => '0');
begin
  rdata <= q;
  process (clk)
    variable lo : integer;
  begin
    if rising_edge(clk) then
      lo := 0;
      q <= x"00";
      if we = '1' then
        case addr is
          when x"20" => r <= wdata;
          when others => null;
        end case;
      elsif re = '1' then
        case addr(7 downto 4) is
          when x"2" =>
            q <= x"00";
            lo := to_integer(unsigned(addr(3 downto 0)));
            case lo is
              when 0 => q <= r;
              when others => null;
            end case;
          when others => null;
        end case;
      end if;
    end if;
  end process;
end architecture;
