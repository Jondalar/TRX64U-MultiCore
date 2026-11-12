-- The part of Gideon's tl_string_util_pkg his RTL uses in simulation-only
-- code (hstr); his own file is VHDL-93 ("default" as a name), this run is -08.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package tl_string_util_pkg is
    function hstr(v : std_logic_vector) return string;
    function hstr(v : unsigned) return string;
end package;

package body tl_string_util_pkg is
    function hstr(v : std_logic_vector) return string is
    begin
        return to_hstring(v);
    end function;
    function hstr(v : unsigned) return string is
    begin
        return to_hstring(v);
    end function;
end package body;
