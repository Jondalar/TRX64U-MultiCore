-- The MAC side of ultimate_sys for the Ethernet TX bench (tb_eth_tx.v):
-- ethernet_interface with its DMA on mem_bus_arbiter_pri_32 port 0 and the
-- CPU on port 3, as ultimate_sys has them, behind plain ports.
library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;
library work;
    use work.io_bus_pkg.all;
    use work.mem_bus_pkg.all;

entity eth_flat is
port (
    clock        : in  std_logic;
    reset        : in  std_logic;
    io_read      : in  std_logic;
    io_write     : in  std_logic;
    io_address   : in  std_logic_vector(7 downto 0);
    io_wdata     : in  std_logic_vector(7 downto 0);
    io_rdata     : out std_logic_vector(7 downto 0);
    io_ack       : out std_logic;
    irq_tx       : out std_logic;
    -- the CPU's memory port (arbiter port 3)
    c_request    : in  std_logic;
    c_rwn        : in  std_logic;
    c_address    : in  std_logic_vector(25 downto 0);
    c_rack       : out std_logic;
    c_dack       : out std_logic;
    -- the memory (cpu_mem_bridge)
    m_request    : out std_logic;
    m_tag        : out std_logic_vector(7 downto 0);
    m_rwn        : out std_logic;
    m_address    : out std_logic_vector(25 downto 0);
    m_byte_en    : out std_logic_vector(3 downto 0);
    m_wdata      : out std_logic_vector(31 downto 0);
    m_rdata      : in  std_logic_vector(31 downto 0);
    m_rack       : in  std_logic;
    m_rack_tag   : in  std_logic_vector(7 downto 0);
    m_dack_tag   : in  std_logic_vector(7 downto 0);
    eth_clock    : in  std_logic;
    eth_reset    : in  std_logic;
    eth_tx_data  : out std_logic_vector(7 downto 0);
    eth_tx_eof   : out std_logic;
    eth_tx_valid : out std_logic;
    eth_tx_ready : in  std_logic );
end entity;

architecture bench of eth_flat is
    signal io_req    : t_io_req := c_io_req_init;
    signal io_resp   : t_io_resp;
    signal eth_req   : t_mem_req_32;
    signal eth_resp  : t_mem_resp_32;
    signal cpu_req   : t_mem_req_32 := c_mem_req_32_init;
    signal cpu_resp  : t_mem_resp_32;
    signal mem_req   : t_mem_req_32;
    signal mem_resp  : t_mem_resp_32;
    signal resp_1, resp_2 : t_mem_resp_32;
    signal irq_rx_nc, sof_nc : std_logic;
begin
    io_req.read    <= io_read;
    io_req.write   <= io_write;
    io_req.address <= X"0000" & unsigned(io_address);
    io_req.data    <= io_wdata;
    io_rdata <= io_resp.data;
    io_ack   <= io_resp.ack;

    i_eth: entity work.ethernet_interface
    generic map ( g_mem_tag => X"12" )
    port map (
        sys_clock => clock, sys_reset => reset,
        io_req => io_req, io_resp => io_resp, io_irq_tx => irq_tx, io_irq_rx => irq_rx_nc,
        mem_req => eth_req, mem_resp => eth_resp,
        eth_clock => eth_clock, eth_reset => eth_reset,
        eth_tx_data => eth_tx_data, eth_tx_sof => sof_nc, eth_tx_eof => eth_tx_eof,
        eth_tx_valid => eth_tx_valid, eth_tx_ready => eth_tx_ready,
        eth_rx_data => X"00", eth_rx_sof => '0', eth_rx_eof => '0', eth_rx_valid => '0' );

    cpu_req.tag         <= X"21";
    cpu_req.request     <= c_request;
    cpu_req.read_writen <= c_rwn;
    cpu_req.address     <= unsigned(c_address);
    cpu_req.byte_en     <= "1111";
    c_rack <= '1' when cpu_resp.rack = '1' and cpu_resp.rack_tag = X"21" else '0';
    c_dack <= '1' when cpu_resp.dack_tag = X"21" else '0';

    i_arb: entity work.mem_bus_arbiter_pri_32
    generic map ( g_registered => false, g_ports => 4 )
    port map (
        clock => clock, reset => reset,
        reqs(0) => eth_req, reqs(1) => c_mem_req_32_init, reqs(2) => c_mem_req_32_init, reqs(3) => cpu_req,
        resps(0) => eth_resp, resps(1) => resp_1, resps(2) => resp_2, resps(3) => cpu_resp,
        req => mem_req, resp => mem_resp );

    m_request <= mem_req.request;
    m_tag     <= mem_req.tag;
    m_rwn     <= mem_req.read_writen;
    m_address <= std_logic_vector(mem_req.address);
    m_byte_en <= mem_req.byte_en;
    m_wdata   <= mem_req.data;
    mem_resp.data     <= m_rdata;
    mem_resp.rack     <= m_rack;
    mem_resp.rack_tag <= m_rack_tag;
    mem_resp.dack_tag <= m_dack_tag;
end architecture;
