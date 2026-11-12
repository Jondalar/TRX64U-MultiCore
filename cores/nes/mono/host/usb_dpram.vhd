--------------------------------------------------------------------------------
-- Architectures for Gideon's dpram_8x16 (nano program/data RAM) and dpram_8x32
-- (USB packet buffer) on 7-series: 2 KB, byte port A, wide port B, both
-- read-first. Written after the asymmetric true dual-port template in UG901,
-- so Vivado maps each onto one RAMB18E1.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

architecture xilinx7 of dpram_8x16 is
    type t_ram is array(0 to 2047) of std_logic_vector(7 downto 0);
    shared variable ram : t_ram := (others => X"00");
begin
    process(CLKA)
    begin
        if rising_edge(CLKA) then
            if ENA = '1' then
                DOA <= ram(to_integer(unsigned(ADDRA)));
                if WEA = '1' then
                    ram(to_integer(unsigned(ADDRA))) := DIA;
                end if;
            end if;
        end if;
    end process;

    process(CLKB)
    begin
        if rising_edge(CLKB) then
            for i in 0 to 1 loop
                if ENB = '1' then
                    DOB(8*i+7 downto 8*i) <= ram(to_integer(unsigned(ADDRB) & to_unsigned(i, 1)));
                    if WEB = '1' then
                        ram(to_integer(unsigned(ADDRB) & to_unsigned(i, 1))) := DIB(8*i+7 downto 8*i);
                    end if;
                end if;
            end loop;
        end if;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

architecture xilinx7 of dpram_8x32 is
    type t_ram is array(0 to 2047) of std_logic_vector(7 downto 0);
    shared variable ram : t_ram := (others => X"00");
begin
    process(CLKA)
    begin
        if rising_edge(CLKA) then
            if ENA = '1' then
                DOA <= ram(to_integer(unsigned(ADDRA)));
                if WEA = '1' then
                    ram(to_integer(unsigned(ADDRA))) := DIA;
                end if;
            end if;
        end if;
    end process;

    process(CLKB)
    begin
        if rising_edge(CLKB) then
            for i in 0 to 3 loop
                if ENB = '1' then
                    DOB(8*i+7 downto 8*i) <= ram(to_integer(unsigned(ADDRB) & to_unsigned(i, 2)));
                    if WEB = '1' then
                        ram(to_integer(unsigned(ADDRB) & to_unsigned(i, 2))) := DIB(8*i+7 downto 8*i);
                    end if;
                end if;
            end loop;
        end if;
    end process;
end architecture;
