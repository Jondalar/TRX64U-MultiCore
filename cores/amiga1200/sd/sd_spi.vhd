-- T149: the SD card's SPI block. Our copy of Gideon's spi.vhd and spi_peripheral_io.vhd (apps/ultimate stays
-- untouched; the flash keeps the originals). What differs from spi.vhd:
--  * SPI_CLK, SPI_MOSI and SPI_SSn come straight out of flops (sck_o, mosi_o, ssn_o) that only drive the pin: IOB output
--    flops (attribute IOB), a fixed clock-to-pad path, no fabric route.
--  * MISO goes through one IOB input flop (miso_i). The original took it in fabric in the cycle that raises SCK, 1 half
--    period after the falling edge that made the card change its output. Here the sample is the pad as of the cycle AFTER
--    the rising edge was launched (cap1/cap2 line the miso_i value up), 1.5 half periods after the falling edge at rate 1.
--  * the received bits go to their own register rx, not into the transmit shift register, so the later sample needs no
--    knowledge of where the shift register is at that moment.
-- Needs rate >= 1 (the sample is taken one clock after the SCK edge, the next SCK edge must be at least 2 clocks away).
-- Timing numbers: docs/specs/done/T149-sd-interface-timing.md; the constraints: cores/amiga1200/sd/sd_timing.xdc.
-- Only the flat builds of amiga1200 and zxnext use this block (cores/amiga1200/sd/sd_mono.tcl swaps it in for the host's
-- i_sd); the host gateware keeps Gideon's spi_peripheral_io. sd_spi_peripheral_io reads bit 7 of the control register
-- (SDCARD_BASE + 8) as 1: the driver (host-app sdio.cc, sdio_set_rw_speed) runs 25 MHz only when it sees that bit.
library ieee;
use ieee.std_logic_1164.all;
use ieee.std_logic_arith.all;
use ieee.std_logic_unsigned.all;

entity sd_spi is
generic (
    g_crc        : boolean := true );
port (
    clock       : in  std_logic;
    reset       : in  std_logic;

    do_send     : in  std_logic;
    clear_crc   : in  std_logic;
    force_ss    : in  std_logic;
    level_ss    : in  std_logic;
    busy        : out std_logic;

    rate        : in  std_logic_vector(8 downto 0);
    cpol        : in  std_logic;
    cpha        : in  std_logic;

    wdata       : in  std_logic_vector(7 downto 0);
    rdata       : out std_logic_vector(7 downto 0);
    crc_out     : out std_logic_vector(7 downto 0);

    SPI_SSn     : out std_logic;
    SPI_CLK     : out std_logic;
    SPI_MOSI    : out std_logic;
    SPI_MISO    : in  std_logic );
end sd_spi;

architecture gideon of sd_spi is
    signal bit_cnt  : std_logic_vector(3 downto 0);
    signal delay    : std_logic_vector(8 downto 0);
    type t_state is (idle, transceive, done, gap);
    signal state    : t_state;
    signal shift    : std_logic_vector(7 downto 0) := X"FF";
    signal rx       : std_logic_vector(7 downto 0) := (others => '0');
    signal crc      : std_logic_vector(6 downto 0) := (others => '0');
    signal sck_o    : std_logic;
    signal mosi_o   : std_logic;
    signal ssn_o    : std_logic;
    signal miso_i   : std_logic;
    signal cap1     : std_logic := '0';
    signal cap2     : std_logic := '0';
    attribute IOB : string;
    attribute IOB of sck_o  : signal is "TRUE";
    attribute IOB of mosi_o : signal is "TRUE";
    attribute IOB of ssn_o  : signal is "TRUE";
    attribute IOB of miso_i : signal is "TRUE";
begin
    SPI_CLK  <= sck_o;
    SPI_MOSI <= mosi_o;
    SPI_SSn  <= ssn_o;

    process(clock)
        procedure update_crc(din : std_logic) is
        begin
            crc(6 downto 1) <= crc(5 downto 0);
            crc(0) <= din xor crc(6);
            crc(3) <= crc(2) xor din xor crc(6);
        end procedure;

        variable s : std_logic;
    begin
        if rising_edge(clock) then
            miso_i <= SPI_MISO;
            cap1   <= '0';
            cap2   <= cap1;

            case state is
            when idle =>
                ssn_o   <= '1';
                sck_o   <= cpol;
                delay   <= rate;
                bit_cnt <= "0000";

                if do_send='1' then
                    busy     <= '1';
                    state    <= transceive;
                    ssn_o    <= '0';
                    if cpha='0' then -- output first bit immediately
                        update_crc(wdata(7));
                        mosi_o   <= wdata(7);
                        shift    <= wdata(6 downto 0) & '0';
                    else -- output first bit upon shift edge
                        shift    <= wdata;
                    end if;
                end if;

            when transceive =>
                if delay = 0 then
                    delay <= rate;
                    bit_cnt <= bit_cnt + 1;
                    sck_o <= not bit_cnt(0) xor cpol;
                    s := cpha xor bit_cnt(0);

                    if s = '0' then
                        cap1 <= '1';
                    end if;

                    if bit_cnt = "1111" then
                        state <= done;
                    else
                        if s = '1' then
                            update_crc(shift(7));
                            mosi_o <= shift(7);
                            shift <= shift(6 downto 0) & '0';
                        end if;
                    end if;
                else
                    delay <= delay - 1;
                end if;

            when done =>
                if delay = 0 then
                    delay   <= rate;
                    rdata   <= rx;
                    ssn_o   <= '1';
                    state   <= gap;
                else
                    delay <= delay - 1;
                end if;

            when gap =>
                if delay = 0 then
                    state <= idle;
                    busy    <= '0';
                else
                    delay <= delay - 1;
                end if;

            when others =>
                null;
            end case;

            if cap2='1' then
                rx <= rx(6 downto 0) & miso_i;
            end if;

            if clear_crc='1' then
                crc <= (others => '0');
            end if;
            if reset='1' then
                state <= idle;
                rdata <= X"00";
                busy  <= '0';
                mosi_o <= '1';
                crc   <= (others => '0');
                cap1  <= '0';
            end if;
            if force_ss='1' then
                ssn_o <= level_ss;
            end if;
        end if;
    end process;

    crc_out <= crc & '1' when g_crc else X"00";
end gideon;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;

entity sd_spi_peripheral_io is
generic (
    g_fixed_rate : boolean := false;
    g_init_rate  : integer := 500;
    g_crc        : boolean := true );
port (
    clock       : in  std_logic;
    reset       : in  std_logic;
    
    io_req      : in  t_io_req;
    io_resp     : out t_io_resp;

	busy		: out std_logic;
	        
    SD_DETECTn  : in  std_logic := '1';
    SD_WRPROTn  : in  std_logic := '1';
    SPI_SSn     : out std_logic;
    SPI_CLK     : out std_logic;
    SPI_MOSI    : out std_logic;
    SPI_MISO    : in  std_logic );
end sd_spi_peripheral_io;

architecture gideon of sd_spi_peripheral_io is
    signal do_send     : std_logic;
    signal force_ss    : std_logic := '0';
    signal level_ss    : std_logic := '0';
    signal busy_i      : std_logic;
    signal rate        : std_logic_vector(8 downto 0) := std_logic_vector(to_unsigned(g_init_rate, 9));
    signal rdata       : std_logic_vector(7 downto 0);
    signal wdata       : std_logic_vector(7 downto 0);
    signal clear_crc   : std_logic;
    signal crc_out     : std_logic_vector(7 downto 0);
    
    type t_state is (idle, writing, reading, receive);
    signal state       : t_state;
    
begin
    spi1: entity work.sd_spi
    generic map (
        g_crc       => g_crc )
    port map (
        clock       => clock,
        reset       => reset,
                               
        do_send     => do_send,
        clear_crc   => clear_crc,
        force_ss    => force_ss,
        level_ss    => level_ss,
        busy        => busy_i,
                               
        rate        => rate,
        cpol        => '0',
        cpha        => '0',
                               
        wdata       => wdata,
        rdata       => rdata,
        crc_out     => crc_out,
                               
        SPI_SSn     => SPI_SSn,
        SPI_CLK     => SPI_CLK,
        SPI_MOSI    => SPI_MOSI,
        SPI_MISO    => SPI_MISO );

    process(clock)
    begin
        if rising_edge(clock) then
            do_send   <= '0';
            clear_crc <= '0';
            io_resp   <= c_io_resp_init;
            
            case state is
            when idle =>
                if io_req.write='1' then
                    state <= writing;
                elsif io_req.read='1' then
                    state <= reading;
                end if;
            
            when writing =>
                if busy_i='0' then
                    io_resp.ack <= '1';
                    state <= idle;
                    
                    case io_req.address(3 downto 2) is
                    when "00" =>
                        do_send <= '1';
                        wdata   <= io_req.data;
                        
                    when "01" =>
                        if not g_fixed_rate then
                            rate(7 downto 0) <= io_req.data;
                            rate(8)          <= io_req.data(7);
                        end if;
                                        
                    when "10" =>
                        force_ss <= io_req.data(0);
                        level_ss <= io_req.data(1);
                    
                    when "11" =>
                        clear_crc <= '1'; 
                    
                    when others =>
                        null;
                    end case;
                end if;
                
            when reading =>
                if busy_i='0' then
                    case io_req.address(3 downto 2) is
                    when "00" =>
                        do_send <= '1';
                        wdata <= X"FF";
                        state <= receive;
                    
                    when "01" =>
                        io_resp.data <= rate(7 downto 0);
                        io_resp.ack <= '1';
                        state <= idle;
                    
                    when "10" =>
                        io_resp.data <= '1' & "000" & not SD_WRPROTn & not SD_DETECTn & level_ss & force_ss;
                        io_resp.ack <= '1';
                        state <= idle;
                    
                    when "11" =>
                        io_resp.data <= crc_out;
                        io_resp.ack <= '1';
                        state <= idle;

                    when others =>
                        null;
                    end case;
                end if;
                            
            when receive =>
                if do_send = '0' and busy_i = '0' then
                    io_resp.data <= rdata;
                    io_resp.ack <= '1';
                    state <= idle;
                end if;

            when others =>
                null;
            end case;

            if reset='1' then
                if not g_fixed_rate then
                    rate     <= std_logic_vector(to_unsigned(g_init_rate, 9));
                end if;
                force_ss <= '0';
                level_ss <= '1';
                wdata    <= X"FF";
            end if;
        end if;        
    end process;

	busy <= busy_i;     
end gideon;
