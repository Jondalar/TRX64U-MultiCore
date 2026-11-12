-- tb_nonet: the signals the BFM and the testbench share
library ieee;
use ieee.std_logic_1164.all;
package tb_nonet_pkg is
    signal bfm_step   : natural := 0;
    signal bfm_errors : natural := 0;
    signal nt_eth_tx, nt_dma : natural := 0;
end package;
