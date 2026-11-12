-- Architecture for Gideon's pseudo_dpram_8x32 entity (vendor/gideon) that
-- infers one 7-series block RAM: 512 x 32 with byte write enables. His
-- Xilinx variant uses RAMB16_S9_S36, which 7-series no longer has.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

architecture x7 of pseudo_dpram_8x32 is
    type t_words is array(0 to 511) of std_logic_vector(31 downto 0);
    signal mem : t_words := (others => (others => '0'));
begin
    process(wr_clock)
    begin
        if rising_edge(wr_clock) then
            for i in 0 to 3 loop
                if wr_en = '1' and to_integer(wr_address(1 downto 0)) = i then
                    mem(to_integer(wr_address(10 downto 2)))(i*8+7 downto i*8) <= wr_data;
                end if;
            end loop;
        end if;
    end process;

    process(rd_clock)
    begin
        if rising_edge(rd_clock) then
            if rd_en = '1' then
                rd_data <= mem(to_integer(rd_address));
            end if;
        end if;
    end process;
end architecture;
