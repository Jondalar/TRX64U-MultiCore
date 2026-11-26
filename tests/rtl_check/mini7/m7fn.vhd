-- tests/rtl_check/mini7 (T239.3, apple2's a2_regs.vhd): a function declared in a process (b4), called with a port, with a register and with a slice of a port.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity m7fn is
  port (
    clk   : in  std_logic;
    we    : in  std_logic;
    re    : in  std_logic;
    addr  : in  std_logic_vector(7 downto 0);
    wdata : in  std_logic_vector(7 downto 0);
    rdata : out std_logic_vector(7 downto 0);
    build : in  std_logic_vector(31 downto 0);
    stat  : in  std_logic_vector(63 downto 0)
  );
end entity;

architecture rtl of m7fn is
  signal base_r : std_logic_vector(31 downto 0) := (others => '0');
  signal q      : std_logic_vector(7 downto 0) := (others => '0');
begin
  rdata <= q;

  process (clk)
    variable o : integer;
    function b4(v : std_logic_vector; i : integer) return std_logic_vector is
      variable w : std_logic_vector(v'length - 1 downto 0);
    begin
      w := v;
      return w(8 * i + 7 downto 8 * i);
    end function;
  begin
    if rising_edge(clk) then
      if we = '1' then
        case addr is
          when x"10" => base_r(7 downto 0) <= wdata;
          when x"11" => base_r(15 downto 8) <= wdata;
          when x"12" => base_r(23 downto 16) <= wdata;
          when x"13" => base_r(31 downto 24) <= wdata;
          when others => null;
        end case;
      end if;
      q <= x"00";
      if re = '1' then
        o := to_integer(unsigned(addr));
        case o is
          when 16#00# => q <= x"4D";
          when 16#01# => q <= x"37";
          when 16#02# => q <= x"46";
          when 16#03# => q <= x"4E";
          when 16#08# to 16#0B# => q <= b4(build, o - 16#08#);
          when 16#10# to 16#13# => q <= b4(base_r, o - 16#10#);
          when 16#14# to 16#17# => q <= b4(stat(63 downto 32), o - 16#14#);
          when others => null;
        end case;
      end if;
    end if;
  end process;
end architecture;
