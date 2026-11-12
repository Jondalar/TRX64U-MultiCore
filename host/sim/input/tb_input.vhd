--------------------------------------------------------------------------------
-- tb_input (T38/T39): ultimate_sys with rvlite_bfm_input in place of the
-- RISC-V. The BFM writes MATRIX_KEYB, the joystick outputs and C64_MODE, and
-- checks the app_* ports through tb_input_pkg; it drives the reset sense and
-- checks C64_CLOCK_DETECT bit 4 and ITU interrupt bit 7. Port map generated
-- from the entity (inputs idle, outputs open).
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.tb_input_pkg.all;

entity tb_input is
end entity;

architecture tb of tb_input is
    signal clock    : std_logic := '0';
    signal reset    : std_logic := '1';
    signal finished : boolean := false;
    signal pal_we    : std_logic;
    signal pal_addr  : std_logic_vector(5 downto 0);
    signal pal_wdata : std_logic_vector(7 downto 0);
begin
    -- T59: count and keep the palette writes
    process(clock)
    begin
        if rising_edge(clock) and pal_we = '1' then
            pk_pal_cnt  <= pk_pal_cnt + 1;
            pk_pal_addr <= pal_addr;
            pk_pal_data <= pal_wdata;
        end if;
    end process;

    clock <= not clock after 5 ns when not finished;
    reset <= '0' after 100 ns;

    dut: entity work.ultimate_sys
    port map (
        clock => clock,
        reset => reset,
        mem_request => open,
        mem_tag => open,
        mem_read_writen => open,
        mem_address => open,
        mem_byte_en => open,
        mem_wdata => open,
        mem_rdata => (others => '0'),
        mem_rack => '0',
        mem_rack_tag => (others => '0'),
        mem_dack_tag => (others => '0'),
        ddr_ready => '1',
        uart_txd => open,
        uart_rxd => '1',
        con_valid => open,
        con_data => open,
        eth_clock => clock,
        eth_reset => reset,
        eth_tx_data => open,
        eth_tx_eof => open,
        eth_tx_valid => open,
        eth_tx_ready => '0',
        eth_rx_data => (others => '0'),
        eth_rx_sof => '0',
        eth_rx_eof => '0',
        eth_rx_valid => '0',
        mdc => open,
        mdio_i => '1',
        mdio_o => open,
        wifi_txd => open,
        wifi_rxd => '1',
        wifi_rts => open,
        wifi_cts => '0',
        sd_ssn => open,
        sd_clk => open,
        sd_mosi => open,
        sd_miso => '1',
        sd_detectn => '1',
        flash_csn => open,
        flash_sck => open,
        flash_mosi => open,
        flash_miso => '1',
        keys => (others => '0'),
        joys => (others => '0'),
        app_keys => pk_app_keys,
        app_reset => pk_app_reset,
        app_restore => pk_app_restore,
        app_freeze => pk_app_freeze,
        app_joy1 => pk_app_joy1,
        app_joy2 => pk_app_joy2,
        c64_reset_sense => pk_sense,
        hdmi_hpd => '1',
        hdmi_ddc_en => open,
        c64_reset_req => open,
        c64_pause_req => open,
        c64_ultimax_req => open,
        cart_type => open,
        cart_kill_tgl => open,
        cart_force_tgl => open,
        cart_active => '0',
        dma_req_tgl => open,
        dma_addr => open,
        dma_we => open,
        dma_wdata => open,
        dma_bus => open,
        dma_rom => open,
        dma_done_tgl => '0',
        dma_rdata => (others => '0'),
        i2c_we => open,
        i2c_re => open,
        i2c_addr => open,
        i2c_wdata => open,
        i2c_rdata => (others => '0'),
        ovl_we => open,
        ovl_addr => open,
        ovl_wdata => open,
        ovl_rdata => (others => '0'),
        pal_we => pal_we,
        pal_addr => pal_addr,
        pal_wdata => pal_wdata,
        bl_we => open,
        bl_addr => open,
        bl_wdata => open,
        bl_rdata => (others => '0'),
        bl_irq => '0',
        cw_we => open,
        cw_re => open,
        cw_addr => open,
        cw_wdata => open,
        cw_rdata => (others => '0'),
        cw_ack => '0',
        cw_irq => '0',
        caseled_sel => open,
        caseled_set => open,
        busy_any => open,
        busy_usb => open,
        speaker_en => open,
        mix_l => open,
        mix_r => open,
        mix_spk => open,
        speaker_vol => open,
        sid_base => open,
        sid_mask => open,
        sid_split => open,
        sid_waves => open,
        sid_res => open,
        sid_digi => open,
        sidcv_we => open,
        sidcv_addr => open,
        sidcv_wdata => open,
        sid_lv_addr => open,
        sid_lv_data => (others => '0'),
        sid_gains => open,
        ulpi_clock => clock,
        ulpi_nxt => '0',
        ulpi_dir => '0',
        ulpi_stp => open,
        ulpi_data_i => (others => '0'),
        ulpi_data_o => open,
        ulpi_data_t => open,
        ulpi_reset_req => open,
        hub_reset_n => open,
        tick_16MHz_o => open,
        tick_4MHz_o => open,
        tick_1kHz_o => open,
        c64_stop_o => open,
        c64_reset_n_o => open,
        drv_io_read => open,
        drv_io_write => open,
        drv_io_addr => open,
        drv_io_wdata => open,
        drv_io_rdata => X"00",
        drv_io_ack => '0',
        drv_io_nc => '0',
        gcr_io_read => open,
        gcr_io_write => open,
        gcr_io_addr => open,
        gcr_io_wdata => open,
        gcr_io_rdata => X"00",
        gcr_io_ack => '0',
        gcr_io_nc => '0',
        snoop_tag => open,
        snoop_request => open,
        snoop_rwn => open,
        snoop_addr => open,
        snoop_be => open,
        snoop_wdata => open,
        snoop_rdata => open,
        snoop_rack => open,
        snoop_rack_tag => open,
        snoop_dack_tag => open );

    process
    begin
        wait until bfm_step = 99 or now > 1 ms for 1 ms;
        if bfm_step = 99 and bfm_errors = 0 then
            report "PASS (" & time'image(now) & ")";
        else
            report "FAIL: bfm " & integer'image(bfm_errors) & ", step " & integer'image(bfm_step) severity failure;
        end if;
        finished <= true;
        wait;
    end process;
end architecture;
