-- drive_mem with flat ports for tb_drive_mem.v (ghdl synth makes Verilog of
-- it): the drive's requests, its DDR2 port, the system bus it snoops.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.mem_bus_pkg.all;

entity drive_mem_flat is
port (
    clock       : in  std_logic;
    reset       : in  std_logic;
    d_request   : in  std_logic;
    d_tag       : in  std_logic_vector(7 downto 0);
    d_rwn       : in  std_logic;
    d_address   : in  std_logic_vector(25 downto 0);
    d_wdata     : in  std_logic_vector(7 downto 0);
    d_rdata     : out std_logic_vector(7 downto 0);
    d_rack      : out std_logic;
    d_rack_tag  : out std_logic_vector(7 downto 0);
    d_dack_tag  : out std_logic_vector(7 downto 0);
    dp_req_tgl  : out std_logic;
    dp_addr     : out std_logic_vector(25 downto 0);
    dp_we       : out std_logic;
    dp_wdata    : out std_logic_vector(31 downto 0);
    dp_be       : out std_logic_vector(3 downto 0);
    dp_done_tgl : in  std_logic;
    dp_rdata    : in  std_logic_vector(127 downto 0);
    s_request   : in  std_logic;
    s_tag       : in  std_logic_vector(7 downto 0);
    s_rwn       : in  std_logic;
    s_address   : in  std_logic_vector(25 downto 0);
    s_byte_en   : in  std_logic_vector(3 downto 0);
    s_wdata     : in  std_logic_vector(31 downto 0);
    s_rack      : in  std_logic;
    s_rack_tag  : in  std_logic_vector(7 downto 0);
    s_dack_tag  : in  std_logic_vector(7 downto 0) );
end entity;

architecture rtl of drive_mem_flat is
    signal drv_req   : t_mem_req_32;
    signal drv_resp  : t_mem_resp_32;
    signal snoop_req : t_mem_req_32;
    signal snoop_resp: t_mem_resp_32;
begin
    drv_req.request     <= d_request;
    drv_req.tag         <= d_tag;
    drv_req.read_writen <= d_rwn;
    drv_req.address     <= unsigned(d_address);
    drv_req.byte_en     <= "0001";
    drv_req.data        <= X"000000" & d_wdata;
    d_rdata    <= drv_resp.data(7 downto 0);
    d_rack     <= drv_resp.rack;
    d_rack_tag <= drv_resp.rack_tag;
    d_dack_tag <= drv_resp.dack_tag;

    snoop_req.request     <= s_request;
    snoop_req.tag         <= s_tag;
    snoop_req.read_writen <= s_rwn;
    snoop_req.address     <= unsigned(s_address);
    snoop_req.byte_en     <= s_byte_en;
    snoop_req.data        <= s_wdata;
    snoop_resp.data       <= (others => '0');
    snoop_resp.rack       <= s_rack;
    snoop_resp.rack_tag   <= s_rack_tag;
    snoop_resp.dack_tag   <= s_dack_tag;

    i_mem: entity work.drive_mem
    port map (
        clock       => clock,
        reset       => reset,
        drv_req     => drv_req,
        drv_resp    => drv_resp,
        dp_req_tgl  => dp_req_tgl,
        dp_addr     => dp_addr,
        dp_we       => dp_we,
        dp_wdata    => dp_wdata,
        dp_be       => dp_be,
        dp_done_tgl => dp_done_tgl,
        dp_rdata    => dp_rdata,
        snoop_req   => snoop_req,
        snoop_resp  => snoop_resp );
end architecture;
