-- tests/rtl_check/mini8 (T239.3 review 5): a process variable is the register: `when x"40" => pend := '1'` under the write flag is a write of 0x40.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity mb is
  port (
    clk   : in  std_logic;
    we    : in  std_logic;
    re    : in  std_logic;
    addr  : in  std_logic_vector(7 downto 0);
    wdata : in  std_logic_vector(7 downto 0);
    rdata : out std_logic_vector(7 downto 0)
  );
end entity;
architecture rtl of mb is
  signal r : std_logic_vector(7 downto 0) := (others => '0');
  signal q : std_logic_vector(7 downto 0) := (others => '0');
  signal irq : std_logic := '0';
begin
  rdata <= q;
  process (clk)
    variable pend : std_logic := '0';
  begin
    if rising_edge(clk) then
      irq <= pend;
      if we = '1' then
        case addr is
          when x"20" => r <= wdata;
          when x"40" => pend := '1';
          when others => null;
        end case;
      end if;
      q <= x"00";
      if re = '1' then
        case addr is
          when x"20" => q <= r;
          when x"41" => q <= "0000000" & irq;
          when others => null;
        end case;
      end if;
    end if;
  end process;
end architecture;
