-- tests/rtl_check/mini8 (T239.3 review 8): a2_regs' pattern inside an arm of addr(7 downto 4): o := addr(3 downto 0) holds the address and is no write; 0x25 has none.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity mv3 is
  port (
    clk   : in  std_logic;
    we    : in  std_logic;
    re    : in  std_logic;
    addr  : in  std_logic_vector(7 downto 0);
    wdata : in  std_logic_vector(7 downto 0);
    rdata : out std_logic_vector(7 downto 0)
  );
end entity;
architecture rtl of mv3 is
  signal r : std_logic_vector(7 downto 0) := (others => '0');
  signal q : std_logic_vector(7 downto 0) := (others => '0');
begin
  rdata <= q;
  process (clk)
    variable o : integer;
  begin
    if rising_edge(clk) then
      if we = '1' then
        case addr(7 downto 4) is
          when x"2" =>
            o := to_integer(unsigned(addr(3 downto 0)));
            case o is
              when 0 => r <= wdata;
              when others => null;
            end case;
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
