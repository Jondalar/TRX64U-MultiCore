library ieee;
use ieee.std_logic_1164.all;
entity m3nib is
port ( d_in : in std_logic_vector(7 downto 0); d_out : out std_logic_vector(7 downto 0) );
end entity;
architecture rtl of m3nib is
begin
    d_out <= not d_in;
end architecture;
