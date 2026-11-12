-- T149 D5: the capability bit. The control register of the SD block (io address 8) reads bit 7 = 1 in sd_spi_peripheral_io
-- (cores/amiga1200/sd/sd_spi.vhd) and 0 in Gideon's spi_peripheral_io, the block of the host gateware. The driver (sdio.cc,
-- sdio_set_rw_speed) runs 25 MHz only when it reads the 1. The other bits must read as before: force_ss, level_ss, detect, write protect.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.textio.all;
library work;
use work.io_bus_pkg.all;

entity tb_sd_cap is
generic ( g_new : boolean := true );
end tb_sd_cap;

architecture tb of tb_sd_cap is
    signal clock  : std_logic := '0';
    signal reset  : std_logic := '1';
    signal req    : t_io_req := c_io_req_init;
    signal resp   : t_io_resp;
    signal ssn, sck, mosi : std_logic;
    signal det_n  : std_logic := '0';     -- card present
    signal wp_n   : std_logic := '1';     -- not protected
begin
    clock <= not clock after 5 ns;

    g_n: if g_new generate
        dut: entity work.sd_spi_peripheral_io
        port map (clock => clock, reset => reset, io_req => req, io_resp => resp, busy => open,
                  SD_DETECTn => det_n, SD_WRPROTn => wp_n, SPI_SSn => ssn, SPI_CLK => sck, SPI_MOSI => mosi, SPI_MISO => '1');
    end generate;
    g_o: if not g_new generate
        dut: entity work.spi_peripheral_io
        port map (clock => clock, reset => reset, io_req => req, io_resp => resp, busy => open,
                  SD_DETECTn => det_n, SD_WRPROTn => wp_n, SPI_SSn => ssn, SPI_CLK => sck, SPI_MOSI => mosi, SPI_MISO => '1');
    end generate;

    stim: process
        variable l : line;
        variable v : std_logic_vector(7 downto 0);
    begin
        wait for 100 ns;
        wait until rising_edge(clock);
        reset <= '0';
        wait until rising_edge(clock);
        req.address <= to_unsigned(8, 24);
        req.read    <= '1';
        wait until rising_edge(clock) and resp.ack = '1';
        v := resp.data;
        req.read    <= '0';
        write(l, string'("RESULT cap new=" & boolean'image(g_new) & " ctrl=" & to_hstring(v)));
        writeline(output, l);
        std.env.finish;
    end process;
end tb;
