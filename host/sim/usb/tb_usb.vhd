--------------------------------------------------------------------------------
-- tb_usb: ultimate_sys with rvlite_bfm in place of the RISC-V, the ULPI pins
-- on ulpi_phy_model (60 MHz). Passes when the BFM's accesses check out, the
-- hub and ULPI resets follow u2p_io, and the nano program, started by the BFM,
-- writes and reads the PHY registers in the order nano_minimal.nan does.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.tb_usb_pkg.all;

entity tb_usb is
generic (
    g_product_low : std_logic_vector(7 downto 0) := X"06" );
end entity;

architecture tb of tb_usb is
    signal clock      : std_logic := '0';
    signal reset      : std_logic := '1';
    signal ulpi_clock : std_logic := '0';
    signal mem_request, mem_rwn, mem_rack : std_logic := '0';
    signal mem_tag, mem_rack_tag, mem_dack_tag : std_logic_vector(7 downto 0) := X"00";
    signal mem_address : std_logic_vector(25 downto 0);
    signal ulpi_data   : std_logic_vector(7 downto 0);
    signal ulpi_data_i, ulpi_data_o : std_logic_vector(7 downto 0);
    signal ulpi_dir, ulpi_nxt, ulpi_stp, ulpi_data_t : std_logic;
    signal ulpi_reset_req, hub_reset_n : std_logic;
    signal phy_done   : boolean;
    signal phy_errors : natural;
    signal tb_errors  : natural := 0;
    signal finished   : boolean := false;
begin
    clock      <= not clock after 5 ns when not finished;           -- 100 MHz
    ulpi_clock <= not ulpi_clock after 8.333 ns when not finished;  -- 60 MHz from the PHY
    reset      <= '0' after 100 ns;

    dut: entity work.ultimate_sys
    port map (
        clock => clock, reset => reset,
        mem_request => mem_request, mem_tag => mem_tag, mem_read_writen => mem_rwn,
        mem_address => mem_address, mem_byte_en => open, mem_wdata => open,
        mem_rdata => X"00000000", mem_rack => mem_rack, mem_rack_tag => mem_rack_tag,
        mem_dack_tag => mem_dack_tag,
        ddr_ready => '1',
        uart_txd => open, uart_rxd => '1',
        con_valid => open, con_data => open,
        eth_clock => clock, eth_reset => reset,
        eth_tx_data => open, eth_tx_eof => open, eth_tx_valid => open, eth_tx_ready => '0',
        eth_rx_data => X"00", eth_rx_sof => '0', eth_rx_eof => '0', eth_rx_valid => '0',
        mdc => open, mdio_i => '1', mdio_o => open,
        wifi_txd => open, wifi_rxd => '1', wifi_rts => open, wifi_cts => '0',
        sd_ssn => open, sd_clk => open, sd_mosi => open, sd_miso => '1', sd_detectn => '1',
        flash_csn => open, flash_sck => open, flash_mosi => open, flash_miso => '1',
        keys => (others => '0'),
        joys => (others => '0'),
        c64_reset_req => open, c64_pause_req => open, c64_ultimax_req => open,
        cart_type => open, cart_kill_tgl => open, cart_force_tgl => open, cart_active => '0',
        dma_req_tgl => open, dma_addr => open, dma_we => open, dma_wdata => open, dma_bus => open,
        dma_done_tgl => '0', dma_rdata => X"00",
        i2c_we => open, i2c_re => open, i2c_addr => open, i2c_wdata => open, i2c_rdata => X"00",
        ovl_we => open, ovl_addr => open, ovl_wdata => open, ovl_rdata => X"00",
        pal_we => open, pal_addr => open, pal_wdata => open,
        bl_we => open, bl_addr => open, bl_wdata => open, bl_rdata => X"00", bl_irq => '0',
        caseled_sel => open, caseled_set => open, busy_any => open, busy_usb => open,
        ulpi_clock => ulpi_clock, ulpi_nxt => ulpi_nxt, ulpi_dir => ulpi_dir, ulpi_stp => ulpi_stp,
        ulpi_data_i => ulpi_data_i, ulpi_data_o => ulpi_data_o, ulpi_data_t => ulpi_data_t,
        ulpi_reset_req => ulpi_reset_req, hub_reset_n => hub_reset_n,
        tick_16MHz_o => open, tick_4MHz_o => open, tick_1kHz_o => open, c64_stop_o => open, c64_reset_n_o => open,
        drv_io_read => open, drv_io_write => open, drv_io_addr => open, drv_io_wdata => open,
        drv_io_rdata => X"00", drv_io_ack => '0', drv_io_nc => '0',
        gcr_io_read => open, gcr_io_write => open, gcr_io_addr => open, gcr_io_wdata => open,
        gcr_io_rdata => X"00", gcr_io_ack => '0', gcr_io_nc => '0',
        snoop_tag => open, snoop_request => open, snoop_rwn => open, snoop_addr => open, snoop_be => open,
        snoop_wdata => open, snoop_rdata => open, snoop_rack => open, snoop_rack_tag => open, snoop_dack_tag => open );

    -- memory: every request taken at once, reads answered the next clock
    process(clock)
    begin
        if rising_edge(clock) then
            mem_rack <= '0'; mem_rack_tag <= X"00"; mem_dack_tag <= X"00";
            if mem_request = '1' and mem_rack = '0' then
                mem_rack <= '1'; mem_rack_tag <= mem_tag;
                report "memory request from tag " & to_hstring(mem_tag) severity warning;
            end if;
        end if;
    end process;

    -- the data lines: the link drives them while ulpi_data_t, the PHY while DIR
    ulpi_data   <= ulpi_data_o when ulpi_data_t = '1' else (others => 'Z');
    ulpi_data_i <= to_X01(ulpi_data);

    phy: entity work.ulpi_phy_model
    generic map (
        g_product_low => g_product_low )
    port map (
        clock  => ulpi_clock,
        data   => ulpi_data,
        dir    => ulpi_dir,
        nxt    => ulpi_nxt,
        stp    => ulpi_stp,
        done   => phy_done,
        errors => phy_errors );

    process
        variable errs : natural := 0;
        procedure expect(name : string; v, e : std_logic) is
        begin
            if v /= e then
                report name & " is " & std_logic'image(v) & " at step " & integer'image(bfm_step) severity error;
                errs := errs + 1;
            end if;
        end procedure;
    begin
        wait until bfm_step = 2; wait for 1 ns;
        expect("ulpi_reset_req", ulpi_reset_req, '1'); expect("hub_reset_n", hub_reset_n, '1');
        wait until bfm_step = 3; wait for 1 ns;
        expect("hub_reset_n", hub_reset_n, '0');
        wait until bfm_step = 4; wait for 1 ns;
        expect("ulpi_reset_req", ulpi_reset_req, '0'); expect("hub_reset_n", hub_reset_n, '1');
        tb_errors <= errs;
        wait;
    end process;

    process
    begin
        wait until phy_done or now > 2 ms for 2 ms;
        wait for 1 us;
        if not phy_done then
            report "PHY never saw the whole register sequence" severity error;
        end if;
        if phy_done and phy_errors = 0 and bfm_errors = 0 and tb_errors = 0 and bfm_step = 5 then
            report "PASS (product " & to_hstring(g_product_low) & ", " & time'image(now) & ")";
        else
            report "FAIL: phy " & integer'image(phy_errors) & ", bfm " & integer'image(bfm_errors) &
                   ", tb " & integer'image(tb_errors) & ", step " & integer'image(bfm_step) severity failure;
        end if;
        finished <= true;
        wait;
    end process;
end architecture;
