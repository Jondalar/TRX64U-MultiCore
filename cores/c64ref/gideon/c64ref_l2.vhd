--------------------------------------------------------------------------------
-- c64ref_l2: the VHDL blocks of the c64ref core (T103) with flat ports, so the Verilog layer 2 (layer2_c64ref.v) can
-- instantiate them. Gideon's records (t_io_req, t_io_resp, t_mem_req_32, ...) are built and taken apart here, as
-- cores/common/drive_l2.vhd does for the reference core. Three entities:
--   c64ref_drive_l2  one drive of the Ultimate app: Gideon's mm_drive (a 1541, 1571 or 1581 as the app selects,
--                  CAPAB_MM_DRIVE) with c64ref_drive_mem in front of its memory port, i.e. its own DDR2 port (toggle
--                  handshake), exactly as cores/common/drive_unit.vhd does it for c1541_drive. One instance here: drive A
--                  at 0x10020000 (tags 01-04, image 0x0EE0000).
--   c64ref_gcr_l2    the GCR codec the app's disk image code uses (HARDWARE_GCR, region 0x10060500)
--   c64ref_uci_l2    the UCI (uci_unit_c64ref.vhd) behind the cw window
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;
use work.mem_bus_pkg.all;

entity c64ref_drive_l2 is
generic (
    g_cpu_tag       : std_logic_vector(7 downto 0);
    g_floppy_tag    : std_logic_vector(7 downto 0);
    g_disk_tag      : std_logic_vector(7 downto 0);
    g_audio_tag     : std_logic_vector(7 downto 0);
    g_audio_base    : std_logic_vector(27 downto 0);   -- (a Verilog layer 2 maps vectors, not unsigned)
    g_ram_base      : std_logic_vector(27 downto 0) );
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
    tick_16MHz      : in  std_logic;
    tick_4MHz       : in  std_logic;
    tick_1kHz       : in  std_logic;

    -- the drive's 8 KB register window (address bits 12:0)
    io_read         : in  std_logic;
    io_write        : in  std_logic;
    io_addr         : in  std_logic_vector(23 downto 0);
    io_wdata        : in  std_logic_vector(7 downto 0);
    io_rdata        : out std_logic_vector(7 downto 0);
    io_ack          : out std_logic;
    io_nc           : out std_logic;
    io_irq          : out std_logic;                -- WD177x (1581), level

    -- DDR2 port (c64ref_drive_mem), toggle handshake
    dp_req_tgl      : out std_logic;
    dp_addr         : out std_logic_vector(25 downto 0);
    dp_we           : out std_logic;
    dp_wdata        : out std_logic_vector(31 downto 0);
    dp_be           : out std_logic_vector(3 downto 0);
    dp_done_tgl     : in  std_logic;
    dp_rdata        : in  std_logic_vector(127 downto 0);

    -- the system bus after the arbiter (ultimate_sys mem_req/mem_resp), snooped for writes into the image
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
    leds_n          : out std_logic_vector(2 downto 0) );   -- activity, motor, dirty
end entity;

architecture rtl of c64ref_drive_l2 is
    signal io_req      : t_io_req;
    signal io_resp     : t_io_resp;
    signal drv_req     : t_mem_req_32;
    signal drv_resp    : t_mem_resp_32;
    signal snoop_req   : t_mem_req_32;
    signal snoop_resp  : t_mem_resp_32;
    signal tick_16_run : std_logic;
    signal tick_4_run  : std_logic;
    signal tick_1k_run : std_logic;
    signal d_atn_o     : std_logic;
    signal d_srq_o     : std_logic;
    signal dbg_data    : std_logic_vector(31 downto 0);
    signal dbg_valid   : std_logic;
begin
    io_req.read    <= io_read;
    io_req.write   <= io_write;
    io_req.address <= unsigned(io_addr);
    io_req.data    <= io_wdata;
    io_rdata       <= io_resp.data;
    io_ack         <= io_resp.ack;
    io_nc          <= io_resp.nc;

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

    -- machine pause: no ticks, so the CPU, VIAs, head and GCR stream stand still
    tick_16_run <= tick_16MHz and not halt;
    tick_4_run  <= tick_4MHz and not halt;
    tick_1k_run <= tick_1kHz and not halt;

    i_drive: entity work.mm_drive
    generic map (
        g_big_endian    => false,
        g_audio_tag     => g_audio_tag,
        g_floppy_tag    => g_floppy_tag,
        g_disk_tag      => g_disk_tag,
        g_cpu_tag       => g_cpu_tag,
        g_audio         => false,               -- no drive sound (CAPAB_DRIVE_SOUND is not set)
        g_audio_base    => unsigned(g_audio_base),
        g_ram_base      => unsigned(g_ram_base) )
    port map (
        clock           => clock,
        reset           => reset,
        drive_stop      => drive_stop,
        tick_16MHz      => tick_16_run,
        tick_4MHz       => tick_4_run,
        tick_1kHz       => tick_1k_run,
        io_req          => io_req,
        io_resp         => io_resp,
        io_irq          => io_irq,
        mem_req         => drv_req,
        mem_resp        => drv_resp,
        atn_o           => d_atn_o,
        atn_i           => iec_atn_i,
        clk_o           => iec_clk_o,
        clk_i           => iec_clk_i,
        data_o          => iec_data_o,
        data_i          => iec_data_i,
        fast_clk_o      => d_srq_o,
        fast_clk_i      => '1',
        iec_reset_n     => iec_reset_n,
        c64_reset_n     => c64_reset_n,
        via1_port_a_o   => open,
        via1_port_a_i   => X"FF",
        via1_port_a_t   => open,
        via1_ca2_o      => open,
        via1_ca2_i      => '1',
        via1_ca2_t      => open,
        via1_cb1_o      => open,
        via1_cb1_i      => '1',
        via1_cb1_t      => open,
        debug_data      => dbg_data,
        debug_valid     => dbg_valid,
        act_led_n       => leds_n(0),
        motor_led_n     => leds_n(1),
        dirty_led_n     => leds_n(2),
        audio_sample    => open );

    i_mem: entity work.c64ref_drive_mem
    generic map (
        g_cpu_tag       => g_cpu_tag,
        g_ram_base      => unsigned(g_ram_base) )
    port map (
        clock           => clock,
        reset           => reset,
        drv_req         => drv_req,
        drv_resp        => drv_resp,
        dp_req_tgl      => dp_req_tgl,
        dp_addr         => dp_addr,
        dp_we           => dp_we,
        dp_wdata        => dp_wdata,
        dp_be           => dp_be,
        dp_done_tgl     => dp_done_tgl,
        dp_rdata        => dp_rdata,
        snoop_req       => snoop_req,
        snoop_resp      => snoop_resp );
end architecture;

--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;

entity c64ref_gcr_l2 is
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
    gcr_io_read     : in  std_logic;
    gcr_io_write    : in  std_logic;
    gcr_io_addr     : in  std_logic_vector(23 downto 0);
    gcr_io_wdata    : in  std_logic_vector(7 downto 0);
    gcr_io_rdata    : out std_logic_vector(7 downto 0);
    gcr_io_ack      : out std_logic;
    gcr_io_nc       : out std_logic );
end entity;

architecture rtl of c64ref_gcr_l2 is
    signal gcr_req  : t_io_req;
    signal gcr_resp : t_io_resp;
begin
    gcr_req.read    <= gcr_io_read;
    gcr_req.write   <= gcr_io_write;
    gcr_req.address <= unsigned(gcr_io_addr);
    gcr_req.data    <= gcr_io_wdata;
    gcr_io_rdata    <= gcr_resp.data;
    gcr_io_ack      <= gcr_resp.ack;
    gcr_io_nc       <= gcr_resp.nc;

    i_gcr: entity work.gcr_codec
    port map (
        clock       => clock,
        reset       => reset,
        req         => gcr_req,
        resp        => gcr_resp );
end architecture;

--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;

entity c64ref_uci_l2 is
port (
    io_clock    : in  std_logic;                    -- cpu_clk
    io_reset    : in  std_logic;
    io_read     : in  std_logic;
    io_write    : in  std_logic;
    io_addr     : in  std_logic_vector(12 downto 0);  -- the 8 KB range
    io_wdata    : in  std_logic_vector(7 downto 0);
    io_rdata    : out std_logic_vector(7 downto 0);
    io_ack      : out std_logic;
    io_irq      : out std_logic;                    -- io_clock

    c64_clock   : in  std_logic;                    -- the C64's ungated sys_clk
    bus_addr    : in  std_logic_vector(15 downto 0);
    ev          : in  std_logic;
    ev_addr     : in  std_logic_vector(15 downto 0);
    ev_data     : in  std_logic_vector(7 downto 0);
    ev_we       : in  std_logic;
    ev_io       : in  std_logic;
    dout        : out std_logic_vector(7 downto 0);
    oe          : out std_logic;
    c64_irq     : out std_logic;
    freeze      : out std_logic );
end entity;

architecture rtl of c64ref_uci_l2 is
    signal req  : t_io_req;
    signal resp : t_io_resp;
begin
    req.read    <= io_read;
    req.write   <= io_write;
    req.address <= resize(unsigned(io_addr), 24);
    req.data    <= io_wdata;
    io_rdata    <= resp.data;
    io_ack      <= resp.ack;

    i_uci: entity work.uci_unit_c64ref
    port map (
        io_clock  => io_clock,
        io_reset  => io_reset,
        io_req    => req,
        io_resp   => resp,
        io_irq    => io_irq,
        c64_clock => c64_clock,
        bus_addr  => bus_addr,
        ev        => ev,
        ev_addr   => ev_addr,
        ev_data   => ev_data,
        ev_we     => ev_we,
        ev_io     => ev_io,
        dout      => dout,
        oe        => oe,
        c64_irq   => c64_irq,
        freeze    => freeze );
end architecture;
