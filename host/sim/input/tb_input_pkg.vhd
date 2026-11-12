-- tb_input: the signals the BFM and the testbench share
library ieee;
use ieee.std_logic_1164.all;
package tb_input_pkg is
    signal bfm_step       : natural := 0;
    signal bfm_errors     : natural := 0;
    signal pk_sense       : std_logic := '0';
    signal pk_app_keys    : std_logic_vector(63 downto 0);
    signal pk_app_reset, pk_app_restore, pk_app_freeze : std_logic;
    signal pk_app_joy1, pk_app_joy2 : std_logic_vector(4 downto 0);
    -- T59: what ultimate_sys puts out for C64_PALETTE writes
    signal pk_pal_cnt     : natural := 0;
    signal pk_pal_addr    : std_logic_vector(5 downto 0);
    signal pk_pal_data    : std_logic_vector(7 downto 0);
end package;
