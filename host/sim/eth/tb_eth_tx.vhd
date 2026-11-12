-- Ethernet TX bench (run.sh): the app's frames through Gideon's
-- ethernet_interface (eth_transmit, its DMA on the memory bus arbiter as in
-- ultimate_sys, eth_flat.vhd), the RMII arbiter of top.v and Gideon's
-- rmii_transceiver, with or without a stream sender on the arbiter's other
-- port. Every frame on the RMII pins is decoded and checked byte for byte,
-- with its CRC.
--
--   memory : cpu_mem_bridge's protocol (one access at a time: rack when taken,
--            dack_tag with the data g_lat .. g_lat + g_jit sys clocks later);
--            the byte at address a is mem_byte(a), never 0xEE.
--   CPU    : g_cpu: random reads on arbiter port 3, behind the MAC's DMA.
--   app    : frame k (length frame_len(k), at frame_addr(k)) goes to the
--            MAC registers as the app's driver does it: address, length,
--            start, then wait until busy is clear, then the next frame.
--   stream : g_stream_us > 0: an 822-byte frame (first byte 0xEE) every
--            g_stream_us, on the port udp_stream/stream_mux use: req until
--            its eof byte is taken, the data held from valid until ready.
--   arbiter: top.v's, transliterated (host/top.v, "whole frames
--            in turn").
-- PASS: every app frame arrived in order, byte exact, with a good CRC, and
-- nothing else came out than whole stream frames.
library ieee;
    use ieee.std_logic_1164.all;
    use ieee.numeric_std.all;
    use ieee.math_real.all;

entity tb_eth_tx is
generic (
    g_frames    : natural := 40;
    g_lat       : natural := 60;        -- sys clocks (10 ns) from rack to data
    g_jit       : natural := 20;
    g_cpu       : boolean := true;
    g_stream_us : natural := 0;
    g_max_len   : natural := 1514 );
end entity;

architecture bench of tb_eth_tx is
    signal clock     : std_logic := '0';
    signal reset     : std_logic := '1';
    signal eth_clock : std_logic := '0';
    signal eth_reset : std_logic := '1';
    signal done      : boolean := false;

    signal io_read, io_write, io_ack : std_logic := '0';
    signal io_address, io_wdata, io_rdata : std_logic_vector(7 downto 0) := X"00";
    signal irq_tx    : std_logic;

    signal c_request, c_rack, c_dack : std_logic := '0';
    signal c_address : std_logic_vector(25 downto 0) := (others => '0');

    signal m_request, m_rwn, m_rack : std_logic := '0';
    signal m_tag, m_rack_tag, m_dack_tag : std_logic_vector(7 downto 0) := X"00";
    signal m_address : std_logic_vector(25 downto 0);
    signal m_byte_en : std_logic_vector(3 downto 0);
    signal m_wdata, m_rdata : std_logic_vector(31 downto 0) := (others => '0');

    signal mac_tx_data, st_tx_data, x_data : std_logic_vector(7 downto 0);
    signal mac_tx_eof, mac_tx_valid, mac_tx_ready : std_logic;
    signal st_tx_eof, st_tx_valid, st_req : std_logic := '0';
    signal x_eof, x_valid, x_ready : std_logic;
    signal own_mac, own_st : std_logic := '0';

    signal rmii_tx_en : std_logic;
    signal rmii_txd   : std_logic_vector(1 downto 0);

    signal st_sent, st_bad, mac_ok, mac_bad, other_bad : natural := 0;

    function mem_byte(a : natural) return std_logic_vector is
    begin
        return std_logic_vector(to_unsigned(((a mod 4093) * ((a mod 4091) + 17) + a / 4096) mod 233, 8));
    end function;

    type t_lens is array(natural range <>) of natural;
    constant c_lens : t_lens := (60, 1514, 64, 799, 800, 801, 1023, 1024, 1078, 1200,
                                 1300, 1374, 1380, 1400, 1460, 1514, 1514, 60, 1514, 1514);
    function frame_len(k : natural) return natural is
        variable l : natural;
    begin
        if k < c_lens'length then
            l := c_lens(k);
        else
            l := 60 + ((k * 7919) mod 1455);
        end if;
        if l > g_max_len then
            l := g_max_len;
        end if;
        return l;
    end function;
    function frame_addr(k : natural) return natural is
    begin
        return 16#4000# + (k mod 8) * 16#800# + (k mod 4);     -- also unaligned
    end function;

    -- CRC-32 of Ethernet, one byte at a time
    function crc_byte(c : std_logic_vector(31 downto 0); b : std_logic_vector(7 downto 0))
        return std_logic_vector is
        variable r : std_logic_vector(31 downto 0) := c;
    begin
        for i in 0 to 7 loop
            if (r(0) xor b(i)) = '1' then
                r := ('0' & r(31 downto 1)) xor X"EDB88320";
            else
                r := '0' & r(31 downto 1);
            end if;
        end loop;
        return r;
    end function;
begin
    clock     <= not clock after 5 ns when not done;
    eth_clock <= not eth_clock after 10.0007 ns when not done;
    reset     <= '0' after 200 ns;
    process(eth_clock)
        variable n : natural := 0;
    begin
        if rising_edge(eth_clock) then
            if n < 20 then n := n + 1; else eth_reset <= '0'; end if;
        end if;
    end process;

    i_mac: entity work.eth_flat
    port map (
        clock => clock, reset => reset,
        io_read => io_read, io_write => io_write, io_address => io_address, io_wdata => io_wdata,
        io_rdata => io_rdata, io_ack => io_ack, irq_tx => irq_tx,
        c_request => c_request, c_rwn => '1', c_address => c_address, c_rack => c_rack, c_dack => c_dack,
        m_request => m_request, m_tag => m_tag, m_rwn => m_rwn, m_address => m_address,
        m_byte_en => m_byte_en, m_wdata => m_wdata, m_rdata => m_rdata, m_rack => m_rack,
        m_rack_tag => m_rack_tag, m_dack_tag => m_dack_tag,
        eth_clock => eth_clock, eth_reset => eth_reset,
        eth_tx_data => mac_tx_data, eth_tx_eof => mac_tx_eof, eth_tx_valid => mac_tx_valid,
        eth_tx_ready => mac_tx_ready );

    -- memory: cpu_mem_bridge's handshake, one access at a time
    p_mem: process(clock)
        variable busy    : boolean := false;
        variable cnt     : natural := 0;
        variable tag     : std_logic_vector(7 downto 0);
        variable addr    : natural;
        variable s1, s2  : positive := 7;
        variable rnd     : real;
        variable w       : std_logic_vector(31 downto 0);
    begin
        if rising_edge(clock) then
            m_rack <= '0';
            m_rack_tag <= X"00";
            m_dack_tag <= X"00";
            if busy then
                if cnt = 0 then
                    for i in 0 to 3 loop
                        w(8*i+7 downto 8*i) := mem_byte(addr + i);
                    end loop;
                    m_rdata <= w;
                    m_dack_tag <= tag;
                    busy := false;
                else
                    cnt := cnt - 1;
                end if;
            elsif m_request = '1' and m_rack = '0' and reset = '0' then
                m_rack <= '1';
                m_rack_tag <= m_tag;
                tag  := m_tag;
                addr := to_integer(unsigned(m_address(25 downto 2))) * 4;
                busy := m_rwn = '1';
                uniform(s1, s2, rnd);
                cnt  := g_lat + integer(rnd * real(g_jit));
            end if;
        end if;
    end process;

    -- the CPU: a random read now and then, behind the DMA
    p_cpu: process(clock)
        variable s1, s2 : positive := 3;
        variable rnd    : real;
        variable open_r : boolean := false;
    begin
        if rising_edge(clock) then
            if c_rack = '1' then
                c_request <= '0';
            end if;
            if c_dack = '1' then
                open_r := false;
            end if;
            uniform(s1, s2, rnd);
            if g_cpu and not open_r and rnd < 0.3 and reset = '0' then
                c_request <= '1';
                c_address <= std_logic_vector(to_unsigned(integer(rnd * 1.0e6) * 4, 26));
                open_r := true;
            end if;
        end if;
    end process;

    -- the app: one frame after the other
    p_app: process
        procedure io_wr(a : natural; d : natural) is
        begin
            wait until rising_edge(clock);
            io_address <= std_logic_vector(to_unsigned(16#10# + a, 8));
            io_wdata <= std_logic_vector(to_unsigned(d mod 256, 8));
            io_write <= '1';
            wait until rising_edge(clock);
            io_write <= '0';
            while io_ack /= '1' loop
                wait until rising_edge(clock);
            end loop;
        end procedure;
        procedure io_rd(a : natural; d : out std_logic_vector(7 downto 0)) is
        begin
            wait until rising_edge(clock);
            io_address <= std_logic_vector(to_unsigned(16#10# + a, 8));
            io_read <= '1';
            wait until rising_edge(clock);
            io_read <= '0';
            while io_ack /= '1' loop
                wait until rising_edge(clock);
            end loop;
            d := io_rdata;
        end procedure;
        variable a, l : natural;
        variable st   : std_logic_vector(7 downto 0);
    begin
        wait until reset = '0' and eth_reset = '0';
        wait for 1 us;
        for k in 0 to g_frames - 1 loop
            a := frame_addr(k);
            l := frame_len(k);
            io_wr(0, a mod 256);
            io_wr(1, (a / 256) mod 256);
            io_wr(2, (a / 65536) mod 256);
            io_wr(3, 0);
            io_wr(4, l mod 256);
            io_wr(5, l / 256);
            io_wr(8, 1);
            loop
                io_rd(16#A#, st);
                exit when st(0) = '0';
            end loop;
            io_wr(9, 0);
        end loop;
        wait for 400 us;
        report "app frames " & integer'image(g_frames) & " sent; arrived ok " & integer'image(mac_ok) &
               ", bad " & integer'image(mac_bad) & "; stream frames " & integer'image(st_sent) &
               ", bad " & integer'image(st_bad) & "; other bad frames " & integer'image(other_bad);
        if mac_ok = g_frames and mac_bad = 0 and st_bad = 0 and other_bad = 0 then
            report "PASS";
        else
            report "FAIL";
        end if;
        done <= true;
        wait;
    end process;

    -- a stream sender as udp_stream/stream_mux drive the arbiter's stream port
    p_stream: process(eth_clock)
        variable t   : natural := 0;
        variable i   : natural := 0;
        variable pend: boolean := false;
        constant c_len : natural := 822;
    begin
        if rising_edge(eth_clock) then
            if g_stream_us > 0 and eth_reset = '0' then
                if t = g_stream_us * 50 - 1 then
                    t := 0;
                    pend := true;
                else
                    t := t + 1;
                end if;
            end if;
            if pend and st_req = '0' then
                st_req <= '1';
                i := 0;
                pend := false;
            end if;
            if st_req = '1' and own_st = '1' and st_tx_valid = '1' and x_ready = '1' then
                if i = c_len - 1 then
                    st_req <= '0';
                    st_tx_valid <= '0';
                    st_sent <= st_sent + 1;
                else
                    i := i + 1;
                end if;
            elsif st_req = '1' and own_st = '1' then
                st_tx_valid <= '1';
            end if;
            if i = 0 then
                st_tx_data <= X"EE";
            else
                st_tx_data <= std_logic_vector(to_unsigned(i mod 256, 8));
            end if;
            if i = c_len - 1 then st_tx_eof <= '1'; else st_tx_eof <= '0'; end if;
        end if;
    end process;

    -- top.v: whole frames in turn, the MAC first
    process(eth_clock)
    begin
        if rising_edge(eth_clock) then
            if eth_reset = '1' then
                own_mac <= '0'; own_st <= '0';
            elsif own_mac = '0' and own_st = '0' then
                if mac_tx_valid = '1' then own_mac <= '1';
                elsif st_req = '1' then own_st <= '1';
                end if;
            elsif own_mac = '1' and mac_tx_valid = '1' and mac_tx_eof = '1' and x_ready = '1' then
                own_mac <= '0';
            elsif own_st = '1' and st_tx_valid = '1' and st_tx_eof = '1' and x_ready = '1' then
                own_st <= '0';
            end if;
        end if;
    end process;
    x_data  <= st_tx_data when own_st = '1' else mac_tx_data;
    x_eof   <= st_tx_eof  when own_st = '1' else mac_tx_eof;
    x_valid <= st_tx_valid when own_st = '1' else (own_mac and mac_tx_valid);
    mac_tx_ready <= own_mac and x_ready;

    i_rmii: entity work.rmii_transceiver
    port map (
        clock => eth_clock, reset => eth_reset,
        rmii_crs_dv => '0', rmii_rxd => "00",
        rmii_tx_en => rmii_tx_en, rmii_txd => rmii_txd,
        eth_rx_data => open, eth_rx_sof => open, eth_rx_eof => open, eth_rx_valid => open,
        eth_tx_data => x_data, eth_tx_eof => x_eof, eth_tx_valid => x_valid, eth_tx_ready => x_ready,
        ten_meg_mode => '0' );

    -- the wire: preamble, SFD, bytes LSB first, then check
    p_check: process(eth_clock)
        type t_buf is array(0 to 2047) of std_logic_vector(7 downto 0);
        variable buf   : t_buf;
        variable n     : natural := 0;
        variable dib   : natural := 0;
        variable sh    : std_logic_vector(7 downto 0);
        variable in_fr : boolean := false;
        variable sfd   : boolean := false;
        variable was_en: std_logic := '0';
        variable k     : natural := 0;
        variable crc   : std_logic_vector(31 downto 0);
        variable ok    : boolean;
        variable fcs   : std_logic_vector(31 downto 0);
        variable a, l  : natural;
    begin
        if rising_edge(eth_clock) then
            if rmii_tx_en = '1' then
                if was_en = '0' then
                    sfd := false; n := 0; dib := 0;
                end if;
                if not sfd then
                    if rmii_txd = "11" then sfd := true; end if;
                else
                    sh := rmii_txd & sh(7 downto 2);
                    dib := dib + 1;
                    if dib = 4 then
                        dib := 0;
                        if n <= 2047 then buf(n) := sh; end if;
                        n := n + 1;
                    end if;
                end if;
            elsif was_en = '1' then
                -- end of a frame: n bytes incl. FCS
                ok := n >= 8 and dib = 0;
                if ok then
                    crc := X"FFFFFFFF";
                    for i in 0 to n - 5 loop
                        crc := crc_byte(crc, buf(i));
                    end loop;
                    crc := not crc;
                    fcs := buf(n-1) & buf(n-2) & buf(n-3) & buf(n-4);
                    ok := crc = fcs;
                end if;
                if ok and buf(0) = X"EE" and n - 4 = 822 then
                    for i in 1 to 821 loop
                        if buf(i) /= std_logic_vector(to_unsigned(i mod 256, 8)) then ok := false; end if;
                    end loop;
                    if not ok then
                        st_bad <= st_bad + 1;
                        report "stream frame with wrong content" severity warning;
                    end if;
                elsif not ok and n >= 1 and buf(0) = X"EE" then
                    st_bad <= st_bad + 1;
                    report "stream frame bad: " & integer'image(n) & " bytes, CRC wrong or cut" severity warning;
                else
                    a := frame_addr(k);
                    l := frame_len(k);
                    if ok and n - 4 = l then
                        for i in 0 to l - 1 loop
                            if buf(i) /= mem_byte(a + i) then ok := false; end if;
                        end loop;
                    else
                        ok := false;
                    end if;
                    if ok then
                        mac_ok <= mac_ok + 1;
                        k := k + 1;
                    elsif n - 4 /= l and n >= 5 and buf(0) = mem_byte(a) and buf(1) = mem_byte(a + 1) then
                        mac_bad <= mac_bad + 1;
                        report "app frame " & integer'image(k) & " (" & integer'image(l) &
                               " bytes) came out as " & integer'image(n) & " bytes on the wire" severity warning;
                        k := k + 1;     -- lost; the rest of it follows as a frame of its own
                    else
                        other_bad <= other_bad + 1;
                        report "frame of " & integer'image(n) & " bytes on the wire that is no app frame " &
                               "(waiting for app frame " & integer'image(k) & ")" severity warning;
                    end if;
                end if;
            end if;
            was_en := rmii_tx_en;
        end if;
    end process;
end architecture;
