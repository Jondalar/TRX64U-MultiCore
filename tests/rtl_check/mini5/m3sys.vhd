-- A bus module with vectors declared ascending, a for-generate of instances over slices of them, an if-generate that opens a process, an attribute, a whole record
-- assigned as a value and a bank of bytes in a RAM, written four bytes apart.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
library work;
use work.mpkg.all;

entity m3sys is
generic ( g_en : boolean := true );
port ( clock : in std_logic; reset : in std_logic );
end entity;

architecture rtl of m3sys is
    signal io_req    : t_io_req;
    signal io_resp   : t_io_resp;
    signal resp_r    : t_io_resp;
    signal sr        : std_logic_vector(0 to 15) := X"5555";
    signal dec       : std_logic_vector(0 to 15);
    signal sh        : std_logic_vector(1 to 8);
    signal bank_we   : std_logic;
    signal bank_out  : std_logic_vector(7 downto 0);
    signal cfg       : std_logic_vector(7 downto 0);
    signal opt       : std_logic_vector(7 downto 0);
    attribute keep : string;
    attribute keep of sr : signal is "true";
begin
    process(clock)
    begin
        if rising_edge(clock) then
            resp_r <= c_io_resp_init;
            if io_req.write = '1' then
                resp_r.ack <= '1';
                case to_integer(io_req.address(7 downto 0)) is
                when 0  => sr <= sr(8 to 15) & io_req.data;
                when 32 => cfg <= io_req.data;
                when 34 => sh <= io_req.data(3 downto 0) & io_req.data(7 downto 4);
                when others => null;
                end case;
            end if;
            if io_req.read = '1' then
                resp_r.ack <= '1';
                case to_integer(io_req.address(7 downto 0)) is
                when 0  => resp_r.data <= dec(0 to 7);
                when 1  => resp_r.data <= dec(8 to 15);
                when 32 => resp_r.data <= cfg;
                when 33 => resp_r.data <= opt;
                when 34 => resp_r.data <= sh(1 to 4) & sh(5 to 8);
                when others => null;
                end case;
            end if;
        end if;
    end process;

    g_dec: for i in 0 to 1 generate
        i_nib: entity work.m3nib
        port map ( d_in => sr(8*i to 8*i+7), d_out => dec(8*i to 8*i+7) );
    end generate;

    g_opt: if g_en generate
        process(clock)
        begin
            if rising_edge(clock) then
                if io_req.write = '1' and to_integer(io_req.address(7 downto 0)) = 33 then
                    opt <= io_req.data;
                end if;
            end if;
        end process;
    end generate;

    bank_we <= io_req.write when io_req.address(7 downto 4) = X"1" else '0';
    i_bank: entity work.dpram_8x16
    port map ( CLKA => clock, ENA => bank_we, WEA => bank_we, ADDRA => std_logic_vector(io_req.address(10 downto 0)), DIA => io_req.data, DOA => bank_out );

    io_resp <= resp_r;
end architecture;
