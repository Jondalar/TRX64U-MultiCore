-- Behavioural SRL16E for GHDL (srl_fifo-xilinx needs it), library unisim.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity SRL16E is
generic (
    INIT : bit_vector(15 downto 0) := X"0000" );
port (
    Q   : out std_logic;
    A0  : in  std_logic;
    A1  : in  std_logic;
    A2  : in  std_logic;
    A3  : in  std_logic;
    CE  : in  std_logic;
    CLK : in  std_logic;
    D   : in  std_logic );
end entity;

architecture sim of SRL16E is
    signal sr : std_logic_vector(15 downto 0) := to_stdlogicvector(INIT);
begin
    process(CLK)
    begin
        if rising_edge(CLK) and CE = '1' then
            sr <= sr(14 downto 0) & D;
        end if;
    end process;
    Q <= sr(to_integer(unsigned(std_logic_vector'(A3 & A2 & A1 & A0))));
end architecture;

library ieee;
use ieee.std_logic_1164.all;

package vcomponents is
    component SRL16E is
    generic (
        INIT : bit_vector(15 downto 0) := X"0000" );
    port (
        Q   : out std_logic;
        A0  : in  std_logic;
        A1  : in  std_logic;
        A2  : in  std_logic;
        A3  : in  std_logic;
        CE  : in  std_logic;
        CLK : in  std_logic;
        D   : in  std_logic );
    end component;
end package;
