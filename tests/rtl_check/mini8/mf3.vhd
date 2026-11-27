-- tests/rtl_check/mini8 (T239.3 review 7): a process function that calls itself: refused (not an opaque call read as a register).
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity mf3 is
  port (
    clk   : in  std_logic;
    we    : in  std_logic;
    re    : in  std_logic;
    addr  : in  std_logic_vector(7 downto 0);
    wdata : in  std_logic_vector(7 downto 0);
    rdata : out std_logic_vector(7 downto 0)
  );
end entity;
architecture rtl of mf3 is
  signal r   : std_logic_vector(7 downto 0) := (others => '0');
  signal acc : std_logic_vector(7 downto 0) := (others => '0');
  signal q   : std_logic_vector(7 downto 0) := (others => '0');
begin
  rdata <= q;
  process (clk)
    function f(v : std_logic_vector) return std_logic_vector is
    begin
      return f(v);
    end function;
  begin
    if rising_edge(clk) then
      if we = '1' then
        case addr is
          when x"20" => r <= wdata;
          when x"21" => acc <= wdata;
          when others => null;
        end case;
      end if;
      q <= x"00";
      if re = '1' then
        case addr is
          when x"20" => q <= r;
          when others => q <= f(x"00");
        end case;
      end if;
    end if;
  end process;
end architecture;
