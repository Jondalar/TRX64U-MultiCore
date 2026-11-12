-- T149 S2: the SD card's SPI block (cores/amiga1200/sd/sd_spi.vhd, or Gideon's original spi.vhd as the control) against a card model
-- whose MISO changes g_t_card + g_t_board after the falling SCK edge at the card's pin (mode 0). The pins are modelled by
-- delays: SCK out g_t_out after the clock edge that launches it, MISO in g_t_in before the fabric/IOB flop.
-- The card sends a fixed pseudo-random byte stream and checks the bytes the block sends (MOSI sampled on the rising
-- SCK edge at the pin). The result line is read by run.sh.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.textio.all;

entity tb_sd_spi is
generic (
    g_new     : boolean := true;       -- true: cores/amiga1200/sd/sd_spi.vhd, false: Gideon's spi.vhd
    g_rate    : integer := 1;
    g_t_out   : integer := 5000;       -- ps: clock edge -> SCK/MOSI at the pad
    g_t_card  : integer := 14000;      -- ps: SD card output delay after the falling SCK edge
    g_t_board : integer := 3000;       -- ps
    g_t_in    : integer := 1500;       -- ps: pad -> flop D
    g_nbytes  : integer := 2000 );
end tb_sd_spi;

architecture tb of tb_sd_spi is
    signal clock    : std_logic := '0';
    signal reset    : std_logic := '1';
    signal do_send  : std_logic := '0';
    signal busy     : std_logic;
    signal wdata    : std_logic_vector(7 downto 0) := X"FF";
    signal rdata    : std_logic_vector(7 downto 0);
    signal ssn, sck, mosi : std_logic;
    signal pad_sck, pad_mosi : std_logic := '0';
    signal miso_card : std_logic;
    signal miso_pad  : std_logic;
    signal force_ss  : std_logic := '0';
    signal rate      : std_logic_vector(8 downto 0);
    signal mosi_errors : integer := 0;

    -- 16-bit LFSR byte streams: card -> block (rxs) and block -> card (txs)
    function lfsr_byte(seed : integer; k : integer) return std_logic_vector is
        variable r : unsigned(15 downto 0) := to_unsigned(seed, 16);
        variable b : std_logic_vector(7 downto 0);
    begin
        for i in 0 to k * 8 + 7 loop
            r := r(14 downto 0) & (r(15) xor r(13) xor r(12) xor r(10));
            if i >= k * 8 then
                b(7 - (i - k * 8)) := r(0);
            end if;
        end loop;
        return b;
    end function;
    function rx_byte(k : integer) return std_logic_vector is
    begin
        return lfsr_byte(16#ACE1#, k);
    end function;
    function tx_byte(k : integer) return std_logic_vector is
    begin
        return lfsr_byte(16#1D2B#, k);
    end function;
begin
    clock <= not clock after 5 ns;
    rate  <= std_logic_vector(to_unsigned(g_rate, 9));

    g_n: if g_new generate
        dut: entity work.sd_spi
        generic map (g_crc => true)
        port map (clock => clock, reset => reset, do_send => do_send, clear_crc => '0', force_ss => force_ss,
                  level_ss => '0', busy => busy, rate => rate, cpol => '0', cpha => '0', wdata => wdata,
                  rdata => rdata, crc_out => open, SPI_SSn => ssn, SPI_CLK => sck, SPI_MOSI => mosi,
                  SPI_MISO => miso_pad);
    end generate;
    g_o: if not g_new generate
        dut: entity work.spi
        generic map (g_crc => true)
        port map (clock => clock, reset => reset, do_send => do_send, clear_crc => '0', force_ss => force_ss,
                  level_ss => '0', busy => busy, rate => rate, cpol => '0', cpha => '0', wdata => wdata,
                  rdata => rdata, crc_out => open, SPI_SSn => ssn, SPI_CLK => sck, SPI_MOSI => mosi,
                  SPI_MISO => miso_pad);
    end generate;

    pad_sck  <= transport sck  after g_t_out * 1 ps;
    pad_mosi <= transport mosi after g_t_out * 1 ps;
    miso_pad <= transport miso_card after g_t_in * 1 ps;

    -- the card: bit index nfall of the stream is on MISO from g_t_card + g_t_board after the nfall-th falling edge
    card: process
        variable nfall : integer := 0;
        variable nrise : integer := 0;
        variable acc   : std_logic_vector(7 downto 0);
        variable bi    : integer;
    begin
        miso_card <= rx_byte(0)(7);
        loop
            wait until pad_sck'event;
            if rising_edge(pad_sck) then
                acc := acc(6 downto 0) & pad_mosi;
                nrise := nrise + 1;
                if nrise mod 8 = 0 and acc /= tx_byte(nrise / 8 - 1) then
                    mosi_errors <= mosi_errors + 1;
                end if;
            elsif falling_edge(pad_sck) then
                nfall := nfall + 1;
                bi := nfall mod 8;
                miso_card <= transport rx_byte(nfall / 8)(7 - bi) after (g_t_card + g_t_board) * 1 ps;
            end if;
        end loop;
    end process;

    stim: process
        variable errs : integer := 0;
        variable l : line;
    begin
        wait for 100 ns;
        wait until rising_edge(clock);
        reset <= '0';
        force_ss <= '1';
        wait until rising_edge(clock);
        for k in 0 to g_nbytes - 1 loop
            wait until rising_edge(clock) and busy = '0';
            wdata   <= tx_byte(k);
            do_send <= '1';
            wait until rising_edge(clock);
            do_send <= '0';
            wait until rising_edge(clock) and busy = '1';
            wait until rising_edge(clock) and busy = '0';
            if rdata /= rx_byte(k) then
                errs := errs + 1;
            end if;
        end loop;
        wait for 200 ns;
        write(l, string'("RESULT new=" & boolean'image(g_new) & " rate=" & integer'image(g_rate) &
              " t_card_ps=" & integer'image(g_t_card) & " rx_errors=" & integer'image(errs) & "/" & integer'image(g_nbytes) &
              " mosi_errors=" & integer'image(mosi_errors)));
        writeline(output, l);
        std.env.finish;
    end process;
end tb;
