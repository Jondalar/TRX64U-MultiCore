--------------------------------------------------------------------------------
-- drive_unit: drive A of the Ultimate app (0x10020000): Gideon's 1541
-- (c1541_drive: 6502, two VIAs, GCR head on the app's track images in DDR,
-- registers, parameter RAM, dirty flags, sounds) with drive_mem in front of
-- its memory port: its own DDR2 port (mig_arb client 2). Tags and addresses as in his ultimate_logic_32 (no MM
-- drive, so capability bit 11 stays off and the app runs it as a 1541).
-- IEC levels are '0' = line pulled low; the inputs are the bus as a whole,
-- synchronized to clock.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;
use work.mem_bus_pkg.all;

entity drive_unit is
generic (
    g_audio         : boolean := true );
port (
    clock           : in  std_logic;
    reset           : in  std_logic;
    tick_16MHz      : in  std_logic;
    tick_4MHz       : in  std_logic;
    tick_1kHz       : in  std_logic;

    io_req          : in  t_io_req;
    io_resp         : out t_io_resp;

    dp_req_tgl      : out std_logic;        -- DDR2 port, see drive_mem
    dp_addr         : out std_logic_vector(25 downto 0);
    dp_we           : out std_logic;
    dp_wdata        : out std_logic_vector(31 downto 0);
    dp_be           : out std_logic_vector(3 downto 0);
    dp_done_tgl     : in  std_logic;
    dp_rdata        : in  std_logic_vector(127 downto 0);
    snoop_req       : in  t_mem_req_32;     -- the system bus after the arbiter
    snoop_resp      : in  t_mem_resp_32;

    drive_stop      : in  std_logic;        -- C64 stopped (the app's freeze)
    halt            : in  std_logic := '0'; -- machine pause: no ticks, so the CPU,
                                            -- VIAs, head and GCR stream stand still
    c64_reset_n     : in  std_logic;

    iec_reset_n     : in  std_logic := '1'; -- the bus RESET line (T62 G19)
    iec_atn_i       : in  std_logic;
    iec_clk_i       : in  std_logic;
    iec_data_i      : in  std_logic;
    iec_clk_o       : out std_logic;
    iec_data_o      : out std_logic;

    act_led_n       : out std_logic;
    motor_led_n     : out std_logic;
    dirty_led_n     : out std_logic;
    audio_sample    : out signed(12 downto 0);

    -- for the JTAG debug map: last GCR fetch address; GCR fetches and their
    -- answers; the drive CPU's last bus cycle {0, ATN, DATA, CLK, SYNC, SO,
    -- IRQ, read, data, address}; SYNC and byte-ready (SO) edges, last GCR byte;
    -- {c1541_drive's dbg_status: half track, step, motor, mode, write_prot_n,
    -- inserted, power, reset, byte ready; last write to VIA 2 port B ($1C00);
    -- highest half track since reset}; parameter RAM words as the app wrote
    -- them: half track 0 {length - 1 | bit time << 16}, half track 34 the same;
    -- {GCR fetches in the last 100 ms, drive CPU cycles (wraps)}
    dbg             : out std_logic_vector(255 downto 0);
    -- the drive CPU's cycles as they complete (cpu_part_1541's debug stream,
    -- one clock after the cycle): {opcode fetch (patch 0004), ATN, DATA, CLK,
    -- disk SYNC, SO, IRQ, read, data, address}, for drv_dbg.v
    cpu_trace       : out std_logic_vector(31 downto 0);
    cpu_trace_valid : out std_logic );
end entity;

architecture rtl of drive_unit is
    signal tick_16_run  : std_logic;
    signal tick_4_run   : std_logic;
    signal tick_1k_run  : std_logic;
    signal drv_req      : t_mem_req_32;
    signal drv_resp     : t_mem_resp_32;
    signal trace        : std_logic_vector(31 downto 0);
    signal trace_valid  : std_logic;
    signal gcr_addr     : unsigned(25 downto 0) := (others => '0');
    signal gcr_reqs     : unsigned(15 downto 0) := (others => '0');
    signal gcr_dacks    : unsigned(15 downto 0) := (others => '0');
    signal gcr_byte     : std_logic_vector(7 downto 0) := X"00";
    signal sync_edges   : unsigned(11 downto 0) := (others => '0');
    signal so_edges     : unsigned(11 downto 0) := (others => '0');
    signal last_trace   : std_logic_vector(31 downto 0) := (others => '0');
    signal req_d        : std_logic := '0';
    signal status       : std_logic_vector(15 downto 0);
    signal pb_written   : std_logic_vector(7 downto 0) := X"00";
    signal max_track    : unsigned(6 downto 0) := (others => '0');
    signal param_0      : std_logic_vector(31 downto 0) := (others => '0');
    signal param_34     : std_logic_vector(31 downto 0) := (others => '0');
    signal rate         : unsigned(15 downto 0) := (others => '0');
    signal ms           : unsigned(6 downto 0) := (others => '0');
    signal reqs_at      : unsigned(15 downto 0) := (others => '0');
    signal cycles       : unsigned(15 downto 0) := (others => '0');
begin
    tick_16_run <= tick_16MHz and not halt;
    tick_4_run  <= tick_4MHz and not halt;
    tick_1k_run <= tick_1kHz and not halt;

    i_drive: entity work.c1541_drive
    generic map (
        g_big_endian    => false,
        g_cpu_tag       => X"01",
        g_floppy_tag    => X"02",
        g_audio_tag     => X"04",
        g_audio         => g_audio,
        g_audio_base    => X"0EC0000",
        g_ram_base      => X"0EE0000" )
    port map (
        clock           => clock,
        reset           => reset,
        drive_stop      => drive_stop,
        tick_16MHz      => tick_16_run,
        tick_4MHz       => tick_4_run,
        tick_1kHz       => tick_1k_run,
        io_req          => io_req,
        io_resp         => io_resp,
        mem_req         => drv_req,
        mem_resp        => drv_resp,
        atn_o           => open,
        atn_i           => iec_atn_i,
        clk_o           => iec_clk_o,
        clk_i           => iec_clk_i,
        data_o          => iec_data_o,
        data_i          => iec_data_i,
        iec_reset_n     => iec_reset_n,
        c64_reset_n     => c64_reset_n,
        debug_data      => trace,
        debug_valid     => trace_valid,
        track_is_0      => open,
        via1_port_a_o   => open,
        via1_port_a_i   => X"FF",
        via1_port_a_t   => open,
        via1_ca2_o      => open,
        via1_ca2_i      => '1',
        via1_ca2_t      => open,
        via1_cb1_o      => open,
        via1_cb1_i      => '1',
        via1_cb1_t      => open,
        act_led_n       => act_led_n,
        motor_led_n     => motor_led_n,
        dirty_led_n     => dirty_led_n,
        audio_sample    => audio_sample,
        dbg_status      => status );

    i_mem: entity work.drive_mem
    generic map (
        g_cpu_tag       => X"01",
        g_ram_base      => X"0EE0000" )
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

    process(clock)
    begin
        if rising_edge(clock) then
            if drv_resp.rack_tag = X"02" then
                req_d <= '0';
            elsif drv_req.tag = X"02" and drv_req.request = '1' then
                req_d <= '1';
            end if;
            if drv_req.tag = X"02" and drv_req.request = '1' and drv_req.read_writen = '1' and req_d = '0' then
                gcr_addr <= drv_req.address;
                gcr_reqs <= gcr_reqs + 1;
            end if;
            if drv_resp.dack_tag = X"02" then
                gcr_dacks <= gcr_dacks + 1;
                gcr_byte  <= drv_resp.data(7 downto 0);
            end if;
            if unsigned(status(15 downto 9)) > max_track then
                max_track <= unsigned(status(15 downto 9));
            end if;
            if trace_valid = '1' then
                cycles     <= cycles + 1;
                last_trace <= trace;
                if trace(24) = '0' and trace(15 downto 0) = X"1C00" then
                    pb_written <= trace(23 downto 16);
                end if;
                if last_trace(27) = '1' and trace(27) = '0' then
                    sync_edges <= sync_edges + 1;
                end if;
                if last_trace(26) = '1' and trace(26) = '0' then
                    so_edges <= so_edges + 1;
                end if;
            end if;
        end if;
    end process;
    process(clock)
        variable a : integer range 0 to 8191;
    begin
        if rising_edge(clock) then
            a := to_integer(io_req.address(12 downto 0));
            if io_req.write = '1' then
                if a >= 16#1004# and a <= 16#1007# then
                    param_0((a - 16#1004#) * 8 + 7 downto (a - 16#1004#) * 8) <= io_req.data;
                end if;
                if a >= 16#1114# and a <= 16#1117# then
                    param_34((a - 16#1114#) * 8 + 7 downto (a - 16#1114#) * 8) <= io_req.data;
                end if;
            end if;
            if tick_1kHz = '1' then
                if ms = 99 then
                    ms <= (others => '0');
                    rate <= gcr_reqs - reqs_at;
                    reqs_at <= gcr_reqs;
                else
                    ms <= ms + 1;
                end if;
            end if;
        end if;
    end process;

    cpu_trace       <= trace;
    cpu_trace_valid <= trace_valid;
    dbg <= std_logic_vector(rate) & std_logic_vector(cycles) & param_34 & param_0 & status & pb_written & std_logic_vector(max_track) & "0" &
           std_logic_vector(sync_edges) & std_logic_vector(so_edges) & gcr_byte & last_trace &
           std_logic_vector(gcr_reqs) & std_logic_vector(gcr_dacks) & "000000" & std_logic_vector(gcr_addr);
end architecture;
