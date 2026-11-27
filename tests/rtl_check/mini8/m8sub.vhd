-- tests/rtl_check/mini8 (T239.3 review 6): b4 of a signal declared with a subtype name (15 downto 8): refused, not a crash.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity m8sub is
  port (
    clk   : in  std_logic;
    we    : in  std_logic;
    re    : in  std_logic;
    addr  : in  std_logic_vector(7 downto 0);
    wdata : in  std_logic_vector(7 downto 0);
    rdata : out std_logic_vector(7 downto 0)
  );
end entity;

architecture rtl of m8sub is
  subtype t_hi is std_logic_vector(15 downto 8);
  signal hb : t_hi := (others => '0');
  signal q  : std_logic_vector(7 downto 0) := (others => '0');
begin
  rdata <= q;
  process (clk)
    function b4(v : std_logic_vector; i : integer) return std_logic_vector is
      variable w : std_logic_vector(v'length - 1 downto 0);
    begin
      w := v;
      return w(8 * i + 7 downto 8 * i);
    end function;
  begin
    if rising_edge(clk) then
      if we = '1' then
        case addr is
          when x"20" => hb <= wdata;
          when others => null;
        end case;
      end if;
      q <= x"00";
      if re = '1' then
        case addr is
          when x"20" => q <= b4(hb, 0);
          when others => null;
        end case;
      end if;
    end if;
  end process;
end architecture;
