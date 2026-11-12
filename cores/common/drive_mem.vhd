--------------------------------------------------------------------------------
-- drive_mem: between Gideon's c1541_drive and its own DDR2 port (mig_arb
-- client 2, a toggle handshake into ui_clk).
--
-- Everything is in DDR2, as in Gideon's design: the drive's 64 K image at
-- g_ram_base (RAM, ROM, extra RAM), the GCR tracks, the sound samples. One
-- access at a time goes to the port; the drive's CPU reads also go through a
-- small line cache (32 lines of the 16-byte MIG word, direct mapped, in LUTs):
--   - a hit answers in two clocks, a miss reads the whole line;
--   - CPU writes go straight to DDR2 (the app reads its device number at
--     $77/$78 there) and into the line if it is cached;
--   - writes of every other master into the image (snooped on the system bus
--     where cpu_mem_bridge takes them) drop the line, and for a while no line
--     read back from DDR2 is kept, because one may have passed the write on
--     its way to MIG.
-- The drive's 8-bit requests carry byte addresses with the byte in lane 0;
-- the port gets aligned words with one byte enable.
--------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.mem_bus_pkg.all;

entity drive_mem is
generic (
    g_cpu_tag    : std_logic_vector(7 downto 0) := X"01";
    g_ram_base   : unsigned(27 downto 0) := X"0EE0000" );
port (
    clock       : in  std_logic;
    reset       : in  std_logic;

    drv_req     : in  t_mem_req_32;     -- from c1541_drive
    drv_resp    : out t_mem_resp_32;

    dp_req_tgl  : out std_logic;        -- to mig_arb
    dp_addr     : out std_logic_vector(25 downto 0);   -- 16-bit word address
    dp_we       : out std_logic;
    dp_wdata    : out std_logic_vector(31 downto 0);
    dp_be       : out std_logic_vector(3 downto 0);
    dp_done_tgl : in  std_logic;        -- mig_arb (ui_clk)
    dp_rdata    : in  std_logic_vector(127 downto 0);  -- stable when done_tgl toggles

    snoop_req   : in  t_mem_req_32;     -- the system bus after the arbiter
    snoop_resp  : in  t_mem_resp_32 );
end entity;

architecture rtl of drive_mem is
    type t_lines is array(0 to 31) of std_logic_vector(127 downto 0);
    type t_tags is array(0 to 31) of unsigned(6 downto 0);
    signal lines        : t_lines;
    signal tags         : t_tags;
    attribute ram_style : string;
    attribute ram_style of lines : signal is "distributed";
    signal valid        : std_logic_vector(31 downto 0) := (others => '0');
    signal poison       : unsigned(7 downto 0) := (others => '0');

    signal p_tgl        : std_logic := '0';
    signal p_busy       : std_logic := '0';
    signal p_read       : std_logic;
    signal p_line       : std_logic;
    signal p_answer     : std_logic := '0';
    signal p_tag        : std_logic_vector(7 downto 0);
    signal p_addr       : unsigned(25 downto 0);
    attribute ASYNC_REG : string;
    signal done_s       : std_logic_vector(1 downto 0) := "00";
    attribute ASYNC_REG of done_s : signal is "TRUE";
    signal p_done       : boolean;

    signal r_rack       : std_logic := '0';
    signal r_rack_tag   : std_logic_vector(7 downto 0) := X"00";
    signal r_dack_tag   : std_logic_vector(7 downto 0) := X"00";
    signal r_data       : std_logic_vector(7 downto 0) := X"00";

    signal snoop_prev   : t_mem_req_32 := c_mem_req_32_init;
    signal snoop_hit    : boolean;

    function in_image(a : unsigned(25 downto 0)) return boolean is
    begin
        return a(25 downto 16) = g_ram_base(25 downto 16);
    end function;

    function byte_of(line : std_logic_vector(127 downto 0); b : unsigned(3 downto 0)) return std_logic_vector is
    begin
        return line(8 * to_integer(b) + 7 downto 8 * to_integer(b));
    end function;
begin
    p_done <= p_busy = '1' and done_s(1) = p_tgl;

    -- a write is seen where cpu_mem_bridge racks it, one clock after it saw it
    snoop_hit <= snoop_resp.rack = '1' and snoop_prev.request = '1' and snoop_prev.read_writen = '0' and
                 snoop_resp.rack_tag = snoop_prev.tag and in_image(snoop_prev.address);

    process(clock)
        variable a    : unsigned(25 downto 0);
        variable idx  : integer range 0 to 31;
        variable line : std_logic_vector(127 downto 0);
        variable hit  : boolean;
        variable rd   : boolean;
        variable w_en : boolean;
        variable w_ix : integer range 0 to 31;
        variable w_ln : std_logic_vector(127 downto 0);
    begin
        if rising_edge(clock) then
            done_s     <= done_s(0) & dp_done_tgl;
            snoop_prev <= snoop_req;
            r_rack     <= '0';
            r_rack_tag <= X"00";
            r_dack_tag <= X"00";
            if poison /= 0 then
                poison <= poison - 1;
            end if;

            a    := drv_req.address;
            idx  := to_integer(a(8 downto 4));
            line := lines(idx);
            hit  := valid(idx) = '1' and tags(idx) = a(15 downto 9) and in_image(a);
            rd   := drv_req.read_writen = '1';
            w_en := false;
            w_ix := idx;
            w_ln := dp_rdata;

            if p_done then
                p_busy <= '0';
                if p_read = '1' and p_answer = '1' then
                    r_dack_tag <= p_tag;
                    r_data     <= byte_of(dp_rdata, p_addr(3 downto 0));
                end if;
                if p_line = '1' and poison = 0 and not snoop_hit then
                    w_en := true;
                    w_ix := to_integer(p_addr(8 downto 4));
                    tags(to_integer(p_addr(8 downto 4)))  <= p_addr(15 downto 9);
                    valid(to_integer(p_addr(8 downto 4))) <= '1';
                end if;
            elsif drv_req.request = '1' and r_rack = '0' then
                if drv_req.tag = g_cpu_tag and rd and hit then
                    r_rack     <= '1';
                    r_rack_tag <= drv_req.tag;
                    r_dack_tag <= drv_req.tag;
                    r_data     <= byte_of(line, a(3 downto 0));
                elsif p_busy = '0' then
                    p_tgl    <= not p_tgl;
                    p_busy   <= '1';
                    p_read   <= drv_req.read_writen;
                    p_line   <= '0';
                    p_answer <= '1';
                    p_tag    <= drv_req.tag;
                    p_addr   <= a;
                    dp_addr  <= '0' & std_logic_vector(a(25 downto 1));
                    dp_we    <= not drv_req.read_writen;
                    dp_wdata <= drv_req.data(7 downto 0) & drv_req.data(7 downto 0) &
                                drv_req.data(7 downto 0) & drv_req.data(7 downto 0);
                    case a(1 downto 0) is
                    when "00"   => dp_be <= "0001";
                    when "01"   => dp_be <= "0010";
                    when "10"   => dp_be <= "0100";
                    when others => dp_be <= "1000";
                    end case;
                    if drv_req.tag = g_cpu_tag and rd and in_image(a) then
                        p_line <= '1';
                    end if;
                    r_rack     <= '1';
                    r_rack_tag <= drv_req.tag;
                    if not rd and hit then
                        w_ln := line;
                        w_ln(8 * to_integer(a(3 downto 0)) + 7 downto 8 * to_integer(a(3 downto 0))) :=
                            drv_req.data(7 downto 0);
                        w_en := true;
                    end if;
                end if;
            end if;

            if w_en then
                lines(w_ix) <= w_ln;
            end if;

            if snoop_hit then
                valid(to_integer(snoop_prev.address(8 downto 4))) <= '0';
                poison <= (others => '1');
            end if;

            if reset = '1' then
                valid      <= (others => '0');
                p_answer   <= '0';
                r_rack     <= '0';
                r_dack_tag <= X"00";
            end if;
        end if;
    end process;
    dp_req_tgl <= p_tgl;

    drv_resp.data     <= X"000000" & r_data;
    drv_resp.rack     <= r_rack;
    drv_resp.rack_tag <= r_rack_tag;
    drv_resp.dack_tag <= r_dack_tag;
end architecture;
