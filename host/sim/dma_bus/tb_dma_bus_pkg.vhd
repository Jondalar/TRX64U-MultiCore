-- tb_dma_bus: the signals the BFM and the testbench share
library ieee;
use ieee.std_logic_1164.all;
package tb_dma_bus_pkg is
    signal bfm_step   : natural := 0;
    signal bfm_errors : natural := 0;
    signal pk_cnt     : natural := 0;
    signal pk_bus, pk_rom, pk_we : std_logic;
    signal ic_cnt     : natural := 0;
    signal ic_first, ic_last : std_logic_vector(31 downto 0) := (others => '0');
    signal pk_addr    : std_logic_vector(15 downto 0);
end package;
