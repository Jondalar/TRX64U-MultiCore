-- SPDX-License-Identifier: GPL-3.0-or-later
-- T88 D3: the machine's clock enable. The Atari core wants 16 x 1.7734475 MHz = 28.37516 MHz (PAL); sys_clk is
-- 17.734475 MHz x 16/9 = 31.527778 MHz, so the core clock is sys_clk x 9/10 exactly: a phase accumulator that fires nine
-- times in ten clocks. `hold` freezes the accumulator, so a resume continues in the phase it stopped in (the same state a
-- skipped clock leaves). Written for this project (TREX), no upstream code; the same circuit as c264_nco.v.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity a8_nco is
  port (
    clk  : in  std_logic;
    hold : in  std_logic;                  -- pause: no enable, accumulator frozen
    en   : out std_logic);
end entity;

architecture rtl of a8_nco is
  signal acc : unsigned(3 downto 0) := (others => '0');   -- 0, 9, 8, ... 1: fires unless 0 (acc + 9 >= 10)
begin
  en <= '1' when acc /= 0 and hold = '0' else '0';
  process (clk)
  begin
    if rising_edge(clk) then
      if hold = '0' then
        if acc /= 0 then acc <= acc - 1; else acc <= to_unsigned(9, 4); end if;
      end if;
    end if;
  end process;
end architecture;
