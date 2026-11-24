library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
package mpkg is
    type t_io_req is record
        address : unsigned(15 downto 0);
        data    : std_logic_vector(7 downto 0);
        read    : std_logic;
        write   : std_logic;
    end record;
    type t_io_resp is record
        data : std_logic_vector(7 downto 0);
        ack  : std_logic;
    end record;
    type t_io_req_array is array(natural range <>) of t_io_req;
    type t_io_resp_array is array(natural range <>) of t_io_resp;
    constant c_io_resp_init : t_io_resp := (data => X"00", ack => '0');
    constant c_reg_b : unsigned(3 downto 0) := X"1";
end package;
