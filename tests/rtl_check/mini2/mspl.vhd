-- The entities the check models itself (its architectures are Gideon's vendor files): the request splitter and a byte RAM. Only the ports are here.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
library work;
use work.mpkg.all;

entity io_bus_splitter is
generic ( g_range_lo : natural := 16; g_range_hi : natural := 19; g_ports : positive := 3 );
port ( clock : in std_logic; req : in t_io_req; resp : out t_io_resp;
       reqs : out t_io_req_array(0 to g_ports-1); resps : in t_io_resp_array(0 to g_ports-1) );
end entity;

library ieee;
use ieee.std_logic_1164.all;
entity dpram_8x16 is
port ( CLKA : in std_logic; ENA : in std_logic; WEA : in std_logic; ADDRA : in std_logic_vector(10 downto 0);
       DIA : in std_logic_vector(7 downto 0); DOA : out std_logic_vector(7 downto 0) );
end entity;
