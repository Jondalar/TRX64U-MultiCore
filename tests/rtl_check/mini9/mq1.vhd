-- tests/rtl_check/mini9 (T239.3 review 9): the VHDL variable of p9 (`sel := addr;` then `sel = x"20"` in the same if): no write of 0x25.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity mq1 is
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
architecture rtl of mq1 is
  signal r : std_logic_vector(7 downto 0) := (others => '0');
  signal q : std_logic_vector(7 downto 0) := (others => '0');
  signal wr_en : std_logic;
begin
  rdata <= q;
  process (mode, we, addr)
    variable sel : std_logic_vector(7 downto 0);
  begin
    sel := x"20";
    wr_en <= '0';
    if mode = '1' then
      sel := addr;
      if we = '1' and sel = x"20" then
        wr_en <= '1';
      end if;
    end if;
  end process;
  process (clk)
  begin
    if rising_edge(clk) then
      q <= x"00";
      if wr_en = '1' then
        r <= wdata;
      end if;
      if re = '1' and addr = x"20" then
        q <= r;
      end if;
    end if;
  end process;
end architecture;
