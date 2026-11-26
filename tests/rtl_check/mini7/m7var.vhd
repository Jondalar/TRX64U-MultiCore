-- tests/rtl_check/mini7 (T239.3, apple2's a2_regs.vhd): the write branch starts with an assignment of the address to a process variable under the write flag
-- (o := ...); only the case arm of 0x30 takes the data. 0x31 is a read-only constant.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity m7var is
  port (
    clk   : in  std_logic;
    we    : in  std_logic;
    re    : in  std_logic;
    addr  : in  std_logic_vector(7 downto 0);
    wdata : in  std_logic_vector(7 downto 0);
    rdata : out std_logic_vector(7 downto 0)
  );
end entity;

architecture rtl of m7var is
  signal r : std_logic_vector(7 downto 0) := (others => '0');
  signal q : std_logic_vector(7 downto 0) := (others => '0');
begin
  rdata <= q;

  process (clk)
    variable o : integer;
  begin
    if rising_edge(clk) then
      if we = '1' then
        o := to_integer(unsigned(addr));
        case o is
          when 16#30# => r <= wdata;
          when others => null;
        end case;
      end if;
      q <= x"00";
      if re = '1' then
        case addr is
          when x"30" => q <= r;
          when x"31" => q <= x"5A";
          when others => null;
        end case;
      end if;
    end if;
  end process;
end architecture;
