--------------------------------------------------------------------------------
-- ULPI PHY model for tb_usb: register write and read (ULPI 1.1, figures for
-- register write / read; outputs change g_tco after the clock), with a USB3320
-- style register file. Every access is checked in order against c_expect for
-- the product ID given; done goes high after the last one, errors counts.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity ulpi_phy_model is
generic (
    g_product_low : std_logic_vector(7 downto 0) := X"06";
    g_tco         : time := 2 ns );
port (
    clock     : in    std_logic;
    data      : inout std_logic_vector(7 downto 0);
    dir       : out   std_logic := '0';
    nxt       : out   std_logic := '0';
    stp       : in    std_logic;
    done      : out   boolean := false;
    errors    : out   natural := 0 );
end entity;

architecture model of ulpi_phy_model is
    type t_access is record
        write : boolean;
        addr  : natural;
        data  : std_logic_vector(7 downto 0);
    end record;
    type t_accesses is array(natural range <>) of t_access;

    -- nano_minimal.nan from 'begin' up to the long power-up delay
    constant c_expect_06 : t_accesses := (
        (true, 16#0A#, X"06"), (true, 16#04#, X"65"), (false, 16#02#, X"06"),
        (true, 16#0A#, X"66"), (true, 16#04#, X"45") );
    constant c_expect_other : t_accesses := (
        (true, 16#0A#, X"06"), (true, 16#04#, X"65"), (false, 16#02#, g_product_low),
        (true, 16#0A#, X"0E"), (false, 16#13#, X"00"),
        (true, 16#0A#, X"66"), (true, 16#04#, X"45") );

    function expect return t_accesses is
    begin
        if g_product_low = X"06" then return c_expect_06; end if;
        return c_expect_other;
    end function;
    constant c_expect : t_accesses := expect;

    type t_regs is array(0 to 63) of std_logic_vector(7 downto 0);
    type t_state is (idle, w1, w2, w3, r1, r2, r3);
    signal state  : t_state := idle;
    signal drive  : std_logic_vector(7 downto 0) := X"00";
    signal dir_i  : std_logic := '0';
begin
    data <= drive when dir_i = '1' else (others => 'Z');
    dir  <= dir_i;

    process(clock)
        variable regs   : t_regs := (0 => X"24", 1 => X"04", 2 => g_product_low, 3 => X"00",
                                     4 => X"41", 5 => X"41", 6 => X"41", others => X"00");
        variable addr   : natural;
        variable n      : natural := 0;
        variable errs   : natural := 0;
        variable d      : std_logic_vector(7 downto 0);

        procedure log(wr : boolean; a : natural; v : std_logic_vector(7 downto 0)) is
        begin
            if wr then
                report "PHY: write reg " & to_hstring(to_unsigned(a, 8)) & " = " & to_hstring(v);
            else
                report "PHY: read  reg " & to_hstring(to_unsigned(a, 8)) & " -> " & to_hstring(v);
            end if;
            if n < c_expect'length then
                if c_expect(n).write /= wr or c_expect(n).addr /= a or c_expect(n).data /= v then
                    report "PHY: access " & integer'image(n) & " not as expected" severity error;
                    errs := errs + 1;
                end if;
                n := n + 1;
                if n = c_expect'length then
                    done <= true;
                end if;
            end if;
            errors <= errs;
        end procedure;

        procedure reg_write(a : natural; v : std_logic_vector(7 downto 0)) is
            variable base : natural;
        begin
            -- write / set / clear triples (function, interface, OTG, irq enables, scratch)
            case a is
            when 16#04# | 16#07# | 16#0A# | 16#0D# | 16#10# | 16#16# => regs(a) := v;
            when 16#05# | 16#08# | 16#0B# | 16#0E# | 16#11# | 16#17# =>
                base := a - 1; regs(base) := regs(base) or v;
            when 16#06# | 16#09# | 16#0C# | 16#0F# | 16#12# | 16#18# =>
                base := a - 2; regs(base) := regs(base) and not v;
            when others => null;
            end case;
        end procedure;

        impure function reg_read(a : natural) return std_logic_vector is
        begin
            case a is
            when 16#05# | 16#06# | 16#08# | 16#09# | 16#0B# | 16#0C# |
                 16#0E# | 16#0F# | 16#11# | 16#12# | 16#17# | 16#18# =>
                return regs(a - ((a - 16#04#) mod 3));
            when others =>
                return regs(a);
            end case;
        end function;
    begin
        if rising_edge(clock) then
            d := to_X01(data);
            case state is
            when idle =>
                if dir_i = '0' and d(7 downto 6) = "10" then
                    addr := to_integer(unsigned(d(5 downto 0)));
                    nxt <= '1' after g_tco;
                    state <= w1;
                elsif dir_i = '0' and d(7 downto 6) = "11" then
                    addr := to_integer(unsigned(d(5 downto 0)));
                    nxt <= '1' after g_tco;
                    state <= r1;
                elsif dir_i = '0' and d(7 downto 6) = "01" then
                    report "PHY: transmit command " & to_hstring(d) & " (not modelled)" severity warning;
                end if;
            when w1 =>
                state <= w2;
            when w2 =>
                reg_write(addr, d);
                log(true, addr, d);
                nxt <= '0' after g_tco;
                state <= w3;
            when w3 =>
                if stp /= '1' then
                    report "PHY: no STP after register write" severity error;
                    errs := errs + 1;
                    errors <= errs;
                end if;
                state <= idle;
            when r1 =>                          -- turnaround
                nxt   <= '0' after g_tco;
                dir_i <= '1' after g_tco;
                drive <= X"00" after g_tco;
                state <= r2;
            when r2 =>
                drive <= reg_read(addr) after g_tco;
                state <= r3;
            when r3 =>
                log(false, addr, reg_read(addr));
                dir_i <= '0' after g_tco;
                state <= idle;
            end case;
        end if;
    end process;
end architecture;
