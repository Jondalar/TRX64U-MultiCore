-- A bus module with a request splitter (port 0: registers with a process procedure, an alias and a block; port 1: a RAM).
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
library work;
use work.mpkg.all;

entity m2sys is
port ( clock : in std_logic; reset : in std_logic );
end entity;

architecture rtl of m2sys is
    signal io_req    : t_io_req;
    signal io_resp   : t_io_resp;
    signal req_reg   : t_io_req;
    signal resp_reg  : t_io_resp;
    signal req_ram   : t_io_req;
    signal resp_ram  : t_io_resp;
    signal a_reg     : std_logic_vector(7 downto 0);
    signal b_reg     : std_logic_vector(7 downto 0);
    signal ram_out   : std_logic_vector(7 downto 0);
    signal ack_ram   : std_logic;
begin
    i_split: entity work.io_bus_splitter
    generic map ( g_range_lo => 8, g_range_hi => 8, g_ports => 2 )
    port map (
        clock    => clock,
        req      => io_req,
        resp     => io_resp,
        reqs(0)  => req_reg,
        reqs(1)  => req_ram,
        resps(0) => resp_reg,
        resps(1) => resp_ram );

    process(clock)
        alias local_addr : unsigned(3 downto 0) is req_reg.address(3 downto 0);
        procedure put_b(d : std_logic_vector(7 downto 0)) is
        begin
            b_reg <= d;
        end procedure;
    begin
        if rising_edge(clock) then
            resp_reg <= c_io_resp_init;
            if req_reg.write = '1' then
                resp_reg.ack <= '1';
                case local_addr is
                when X"0" => a_reg <= req_reg.data;
                when c_reg_b => put_b(req_reg.data);
                when others => null;
                end case;
            end if;
            if req_reg.read = '1' then
                resp_reg.ack <= '1';
                case local_addr is
                when c_reg_b => resp_reg.data <= b_reg;
                when others => null;
                end case;
            end if;
        end if;
    end process;

    b_ram: block
        signal en : std_logic;
    begin
        en <= req_ram.read or req_ram.write;
        i_ram: entity work.dpram_8x16
        port map ( CLKA => clock, ENA => en, WEA => req_ram.write, ADDRA => std_logic_vector(req_ram.address(10 downto 0)),
                   DIA => req_ram.data, DOA => ram_out );
        ack_ram <= en when rising_edge(clock);
        resp_ram.data <= ram_out when ack_ram = '1' else X"00";
        resp_ram.ack  <= ack_ram;
    end block b_ram;
end architecture;
