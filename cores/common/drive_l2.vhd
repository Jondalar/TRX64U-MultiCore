-- T79: the 1541 drive (drive_unit) and the GCR codec, outside ultimate_sys so
-- they can live in layer 2 (layer2.v). The I/O bus ports and the system bus
-- snoop come as flat vectors (Verilog has no VHDL records); this wrapper
-- turns them back into t_io_req/t_io_resp and t_mem_req_32/t_mem_resp_32.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;
use work.mem_bus_pkg.all;

entity drive_l2 is
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
    tick_16MHz      : in  std_logic;
    tick_4MHz       : in  std_logic;
    tick_1kHz       : in  std_logic;

    -- drive registers (region r_drive of ultimate_sys)
    drv_io_read     : in  std_logic;
    drv_io_write    : in  std_logic;
    drv_io_addr     : in  std_logic_vector(23 downto 0);
    drv_io_wdata    : in  std_logic_vector(7 downto 0);
    drv_io_rdata    : out std_logic_vector(7 downto 0);
    drv_io_ack      : out std_logic;
    drv_io_nc       : out std_logic;
    -- GCR codec (region r_gcr)
    gcr_io_read     : in  std_logic;
    gcr_io_write    : in  std_logic;
    gcr_io_addr     : in  std_logic_vector(23 downto 0);
    gcr_io_wdata    : in  std_logic_vector(7 downto 0);
    gcr_io_rdata    : out std_logic_vector(7 downto 0);
    gcr_io_ack      : out std_logic;
    gcr_io_nc       : out std_logic;

    -- DDR2 port (drive_mem.vhd), toggle handshake with mig_arb
    dp_req_tgl      : out std_logic;
    dp_addr         : out std_logic_vector(25 downto 0);
    dp_we           : out std_logic;
    dp_wdata        : out std_logic_vector(31 downto 0);
    dp_be           : out std_logic_vector(3 downto 0);
    dp_done_tgl     : in  std_logic;
    dp_rdata        : in  std_logic_vector(127 downto 0);

    -- the system bus after the arbiter (ultimate_sys mem_req/mem_resp)
    snoop_tag       : in  std_logic_vector(7 downto 0);
    snoop_request   : in  std_logic;
    snoop_rwn       : in  std_logic;
    snoop_addr      : in  std_logic_vector(25 downto 0);
    snoop_be        : in  std_logic_vector(3 downto 0);
    snoop_wdata     : in  std_logic_vector(31 downto 0);
    snoop_rdata     : in  std_logic_vector(31 downto 0);
    snoop_rack      : in  std_logic;
    snoop_rack_tag  : in  std_logic_vector(7 downto 0);
    snoop_dack_tag  : in  std_logic_vector(7 downto 0);

    drive_stop      : in  std_logic;
    halt            : in  std_logic;
    c64_reset_n     : in  std_logic;
    iec_reset_n     : in  std_logic;
    iec_atn_i       : in  std_logic;
    iec_clk_i       : in  std_logic;
    iec_data_i      : in  std_logic;
    iec_clk_o       : out std_logic;
    iec_data_o      : out std_logic;
    leds_n          : out std_logic_vector(2 downto 0);   -- activity, motor, dirty
    dbg             : out std_logic_vector(255 downto 0);
    cpu_trace       : out std_logic_vector(31 downto 0);
    cpu_trace_valid : out std_logic );
end entity;

architecture rtl of drive_l2 is
    signal drv_req, gcr_req   : t_io_req;
    signal drv_resp, gcr_resp : t_io_resp;
    signal snoop_req          : t_mem_req_32;
    signal snoop_resp         : t_mem_resp_32;
begin
    drv_req.read    <= drv_io_read;
    drv_req.write   <= drv_io_write;
    drv_req.address <= unsigned(drv_io_addr);
    drv_req.data    <= drv_io_wdata;
    drv_io_rdata    <= drv_resp.data;
    drv_io_ack      <= drv_resp.ack;
    drv_io_nc       <= drv_resp.nc;

    gcr_req.read    <= gcr_io_read;
    gcr_req.write   <= gcr_io_write;
    gcr_req.address <= unsigned(gcr_io_addr);
    gcr_req.data    <= gcr_io_wdata;
    gcr_io_rdata    <= gcr_resp.data;
    gcr_io_ack      <= gcr_resp.ack;
    gcr_io_nc       <= gcr_resp.nc;

    snoop_req.tag         <= snoop_tag;
    snoop_req.request     <= snoop_request;
    snoop_req.read_writen <= snoop_rwn;
    snoop_req.address     <= unsigned(snoop_addr);
    snoop_req.byte_en     <= snoop_be;
    snoop_req.data        <= snoop_wdata;
    snoop_resp.data       <= snoop_rdata;
    snoop_resp.rack       <= snoop_rack;
    snoop_resp.rack_tag   <= snoop_rack_tag;
    snoop_resp.dack_tag   <= snoop_dack_tag;

    -- GCR encoder/decoder the app's disk image code uses (HARDWARE_ENCODING)
    i_gcr: entity work.gcr_codec
    port map (
        clock       => clock,
        reset       => reset,
        req         => gcr_req,
        resp        => gcr_resp );

    -- drive A, as in ultimate_logic_32
    i_drive: entity work.drive_unit
    port map (
        clock        => clock,
        reset        => reset,
        tick_16MHz   => tick_16MHz,
        tick_4MHz    => tick_4MHz,
        tick_1kHz    => tick_1kHz,
        io_req       => drv_req,
        io_resp      => drv_resp,
        dp_req_tgl   => dp_req_tgl,
        dp_addr      => dp_addr,
        dp_we        => dp_we,
        dp_wdata     => dp_wdata,
        dp_be        => dp_be,
        dp_done_tgl  => dp_done_tgl,
        dp_rdata     => dp_rdata,
        snoop_req    => snoop_req,
        snoop_resp   => snoop_resp,
        drive_stop   => drive_stop,
        halt         => halt,
        c64_reset_n  => c64_reset_n,
        iec_reset_n  => iec_reset_n,
        iec_atn_i    => iec_atn_i,
        iec_clk_i    => iec_clk_i,
        iec_data_i   => iec_data_i,
        iec_clk_o    => iec_clk_o,
        iec_data_o   => iec_data_o,
        act_led_n    => leds_n(0),
        motor_led_n  => leds_n(1),
        dirty_led_n  => leds_n(2),
        audio_sample => open,
        dbg          => dbg,
        cpu_trace    => cpu_trace,
        cpu_trace_valid => cpu_trace_valid );
end architecture;
