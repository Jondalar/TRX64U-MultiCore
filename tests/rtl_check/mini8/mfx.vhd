-- tests/rtl_check/mini8 (T239.3 review 8): hib(wd(31 downto 8)) with return v(15 downto 8): the slice keeps its range (wd(15 downto 8)); refused, not renumbered from 0.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity mfx is
  port (
    clk   : in  std_logic;
    we    : in  std_logic;
    re    : in  std_logic;
    addr  : in  std_logic_vector(7 downto 0);
    wdata : in  std_logic_vector(7 downto 0);
    rdata : out std_logic_vector(7 downto 0)
  );
end entity;
architecture rtl of mfx is
  signal r  : std_logic_vector(7 downto 0) := (others => '0');
  signal wd : std_logic_vector(31 downto 0);
  signal q  : std_logic_vector(7 downto 0) := (others => '0');
begin
  rdata <= q;
  wd <= x"00" & r & x"00" & x"00";
  process (clk)
    function hib(v : std_logic_vector) return std_logic_vector is
    begin
      return v(15 downto 8);
    end function;
  begin
    if rising_edge(clk) then
      if we = '1' then
        case addr is
          when x"20" => r <= wdata;
          when others => null;
        end case;
      end if;
      q <= x"00";
      if re = '1' then
        case addr is
          when x"20" => q <= r;
          when others => q <= hib(wd(31 downto 8));
        end case;
      end if;
    end if;
  end process;
end architecture;
