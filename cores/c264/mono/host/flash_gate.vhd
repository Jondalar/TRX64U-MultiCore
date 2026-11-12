-- T44: write gate between the IO bus decode and the configuration flash's SPI
-- block (Gideon's spi_peripheral_io, i_flash in ultimate_sys.vhd). It is the
-- only master of i_flash and owns the CS pin. Spec: docs/specs/T44-flash-write-gate.md.
--
-- Every access is passed to i_flash one at a time, and only while no byte is
-- on the wire, so the gate knows the CS level each byte is clocked with:
--   * a byte clocked while the pin is high is not seen by the chip; it goes out
--     unchanged (WREN still becomes WRDI unless g_wren_pass);
--   * a byte clocked while the pin is low has index idx in its transfer (idx is
--     cleared while the pin is high). At idx 0 it is the opcode:
--       pass list:    forwarded,
--       checked list: held (CPU acked, nothing sent) until the address byte
--                     A23..A16 is known; then the opcode, or 0x9F instead if
--                     A23..A16 < g_low_limit, is sent ahead of that byte,
--       anything else: 0x9F (JEDEC ID) is sent instead.
--     A checked opcode in its own CS pulse (CTRL 0) has no address: 0x9F.
--   * lock class (g_lock, T152 D9): exactly the status register lock the host app
--     writes (flashlock.cc) goes through, nothing else of the status registers.
--     0x50 (volatile write enable) is forwarded and arms the gate; the next
--     transfer may be 0x01 (held like a checked opcode) with SR1 = 0b001101xx
--     (SRP 0, SEC 0, TB 1, BP 101) or 0x31 with SR2 = 0b000000x1 (SUS, CMP, LB
--     clear, SRL set, QE either way). Each of the two is let through once per
--     reset and only straight after 0x50; anything else of 0x01 / 0x31 goes out
--     as 0x9F. After a lock write the pin is cut high at once, so a third byte
--     (the SR2 byte of a 16-bit SR1 write) is never seen by the chip.
--   * CTRL writes drop a held opcode; after every CS rise nothing is forwarded
--     for g_cs_gap clocks, so the chip sees every CS-high gap the gate counts;
--   * after reset the pin is kept high until one byte has been clocked.
-- Registers at window offset 0x10-0x17 (aliases every 32 bytes), see the spec §6.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

library work;
use work.io_bus_pkg.all;

entity flash_gate is
generic (
    g_wren_pass : boolean := false;             -- false: 0x06 goes out as 0x04
    g_low_limit : natural range 0 to 255 := 16#40#; -- lowest allowed A23..A16
    g_4byte     : boolean := false;             -- false: 4-byte opcodes are blocked
    g_lock      : boolean := false;             -- true: the lock class above
    g_cs_gap    : natural range 1 to 255 := 10 ); -- minimum CS-high time, clocks
port (
    clock       : in  std_logic;
    reset       : in  std_logic;

    cpu_req     : in  t_io_req;                 -- strobes only for the flash window
    cpu_resp    : out t_io_resp;

    spi_req     : out t_io_req;                 -- to i_flash
    spi_resp    : in  t_io_resp;
    spi_busy    : in  std_logic;                -- i_flash's busy
    spi_ssn     : in  std_logic;                -- i_flash's SPI_SSn

    flash_csn   : out std_logic );              -- the pin
end entity;

architecture rtl of flash_gate is
    constant c_sub  : std_logic_vector(7 downto 0) := X"9F";
    constant c_wren : std_logic_vector(7 downto 0) := X"06";
    constant c_wrdi : std_logic_vector(7 downto 0) := X"04";

    type t_class is (k_pass, k_chk3, k_chk4, k_block, k_arm, k_lock);

    function classify(b : std_logic_vector(7 downto 0)) return t_class is
    begin
        case b is
        when X"03" | X"0B" | X"05" | X"35" | X"15" | X"9F" | X"4B" | X"90" |
             X"AB" | X"5A" | X"06" | X"04" | X"66" | X"99" | X"FF" =>
            return k_pass;
        when X"02" | X"32" | X"20" | X"52" | X"D8" =>
            return k_chk3;
        when X"12" | X"34" | X"21" | X"53" | X"DC" =>
            if g_4byte then
                return k_chk4;
            end if;
            return k_block;
        when X"50" =>
            if g_lock then
                return k_arm;
            end if;
            return k_block;
        when X"01" | X"31" =>
            if g_lock then
                return k_lock;
            end if;
            return k_block;
        when others =>
            return k_block;
        end case;
    end function;

    -- the 4-byte program/erase opcodes, whether or not g_4byte decodes them:
    -- their blocked address is taken from idx 2-4 instead of 1-3
    function is_op4(b : std_logic_vector(7 downto 0)) return boolean is
    begin
        return b = X"12" or b = X"34" or b = X"21" or b = X"53" or b = X"DC";
    end function;

    function b2s(b : boolean) return std_logic is
    begin
        if b then return '1'; end if;
        return '0';
    end function;

    type t_bytes is array(0 to 2) of std_logic_vector(7 downto 0);
    type t_state is (idle, send, wait_ack, settle);
    signal state    : t_state := idle;

    -- the CPU access taken at its strobe
    signal p_valid  : std_logic := '0';
    signal p_read   : std_logic := '0';
    signal p_addr   : unsigned(4 downto 0) := (others => '0');
    signal p_data   : std_logic_vector(7 downto 0) := X"00";

    -- what goes to i_flash for it: q_len DATA bytes (the last one a read if
    -- q_rd), or one access to another register of i_flash (q_reg)
    signal q        : t_bytes := (others => X"00");
    signal q_len    : unsigned(1 downto 0) := "00";
    signal q_pos    : unsigned(1 downto 0) := "00";
    signal q_rd     : std_logic := '0';
    signal q_reg    : std_logic := '0';
    signal settle_c : unsigned(1 downto 0) := "00";

    -- CS: i_flash's CTRL as forwarded, the pin, the gap after each rise
    signal force_ss : std_logic := '0';
    signal level_ss : std_logic := '1';
    signal flush    : std_logic := '1';         -- pin high until a byte was clocked
    signal flushed  : std_logic := '0';
    signal ssn_d    : std_logic := '1';
    signal gap      : unsigned(7 downto 0) := (others => '0');
    signal pin_i    : std_logic;

    -- transfer: logical byte count (held bytes included), held opcode
    signal idx      : unsigned(2 downto 0) := "000";
    signal held     : std_logic := '0';
    signal held_op  : std_logic_vector(7 downto 0) := X"00";
    signal held4    : std_logic := '0';         -- 4-byte opcode held

    -- registers
    signal sticky   : std_logic := '0';
    signal blk_cnt  : unsigned(7 downto 0) := (others => '0');
    signal blk_op   : std_logic_vector(7 downto 0) := X"00";
    signal blk_addr : std_logic_vector(23 downto 0) := (others => '0');
    signal ok_cnt   : unsigned(7 downto 0) := (others => '0');
    signal cap      : std_logic := '0';         -- capture the blocked address
    signal cap4     : std_logic := '0';         -- from idx 2-4

    -- lock class: 0x50 seen and nothing since, each lock write done, pin cut
    signal arm      : std_logic := '0';
    signal lk1      : std_logic := '0';
    signal lk2      : std_logic := '0';
    signal cut_next : std_logic := '0';
    signal cut      : std_logic := '0';
begin
    pin_i     <= spi_ssn or flush or cut;
    flash_csn <= pin_i;

    process(clock)
        variable ob    : std_logic_vector(7 downto 0);    -- the byte the access clocks
        variable bi    : unsigned(2 downto 0);            -- its index in the transfer
        variable seen  : boolean;                         -- clocked with the pin low
        variable allow : boolean;
        variable blk   : boolean;                         -- block event
        variable bop   : std_logic_vector(7 downto 0);
        variable rd    : std_logic_vector(7 downto 0);
        variable ready : boolean;
        variable lk_ok : boolean;

        procedure capture(i : unsigned(2 downto 0); b : std_logic_vector(7 downto 0);
                          four : std_logic) is
        begin
            if four = '0' then
                case to_integer(i) is
                when 1 => blk_addr(23 downto 16) <= b;
                when 2 => blk_addr(15 downto 8)  <= b;
                when 3 => blk_addr(7 downto 0)   <= b;
                when others => null;
                end case;
            else
                case to_integer(i) is
                when 2 => blk_addr(23 downto 16) <= b;
                when 3 => blk_addr(15 downto 8)  <= b;
                when 4 => blk_addr(7 downto 0)   <= b;
                when others => null;
                end case;
            end if;
        end procedure;
    begin
        if rising_edge(clock) then
            cpu_resp      <= c_io_resp_init;
            spi_req.read  <= '0';
            spi_req.write <= '0';

            ssn_d <= spi_ssn;
            if spi_ssn = '1' and ssn_d = '0' then
                gap <= to_unsigned(g_cs_gap, 8);
            elsif gap /= 0 then
                gap <= gap - 1;
            end if;
            if spi_ssn = '1' then
                cut <= '0';
            end if;
            if pin_i = '1' then
                idx <= "000";
                cap <= '0';
                held <= '0';
            end if;

            if cpu_req.write = '1' or cpu_req.read = '1' then
                p_valid <= '1';
                p_read  <= cpu_req.read;
                p_addr  <= cpu_req.address(4 downto 0);
                p_data  <= cpu_req.data;
            end if;

            ready := spi_busy = '0' and gap = 0;

            case state is
            when idle =>
                if flush = '1' and flushed = '1' and spi_ssn = '1' and ready then
                    -- end of the start-up flush, in a clock of its own: the pin
                    -- may only follow SSn from the next byte on
                    flush <= '0';

                elsif p_valid = '1' and p_addr(4) = '1' then
                    -- gate registers, answered at once
                    p_valid <= '0';
                    cpu_resp.ack <= '1';
                    if p_read = '1' then
                        case to_integer(p_addr(3 downto 0)) is
                        when 0 => rd := "0101" & held & b2s(g_4byte) & b2s(g_wren_pass) & sticky;
                        when 1 => rd := std_logic_vector(blk_cnt);
                        when 2 => rd := blk_op;
                        when 3 => rd := blk_addr(23 downto 16);
                        when 4 => rd := blk_addr(15 downto 8);
                        when 5 => rd := blk_addr(7 downto 0);
                        when 6 => rd := std_logic_vector(ok_cnt);
                        when 7 => rd := "000000" & lk2 & lk1;
                        when others => rd := X"00";
                        end case;
                        cpu_resp.data <= rd;
                    elsif p_addr(3 downto 0) = 0 and p_data(0) = '1' then
                        sticky <= '0';
                    end if;

                elsif p_valid = '1' and ready then
                    p_valid <= '0';
                    q_pos   <= "00";
                    q_rd    <= '0';
                    q_reg   <= '0';
                    if p_addr(3 downto 2) /= "00" then
                        -- SPEED, CTRL, CRC of i_flash: forwarded; CTRL drops a held opcode
                        q_reg <= '1';
                        q_len <= "01";
                        state <= send;
                        if p_addr(3 downto 2) = "10" and p_read = '0' then
                            -- a held opcode was never clocked: the chip's
                            -- transfer has had no byte yet
                            if held = '1' then
                                idx <= "000";
                            end if;
                            held     <= '0';
                            force_ss <= p_data(0);
                            level_ss <= p_data(1);
                        end if;
                    else
                        if p_read = '1' then
                            ob := X"FF";
                        else
                            ob := p_data;
                        end if;
                        q_rd  <= p_read;
                        q_len <= "01";
                        q(0)  <= ob;
                        state <= send;
                        blk   := false;
                        bop   := ob;
                        if flush = '1' then
                            flushed <= '1';
                        end if;

                        -- the pin during this byte: low if it is low now (CTRL 1),
                        -- or if i_flash will pull it for this byte (CTRL 0)
                        seen := flush = '0' and cut = '0' and (spi_ssn = '0' or force_ss = '0');
                        if spi_ssn = '0' and flush = '0' then
                            bi := idx;
                        else
                            bi := "000";
                        end if;

                        if not seen then
                            -- the chip does not see it
                            if not g_wren_pass and p_read = '0' and ob = c_wren then
                                q(0) <= c_wrdi;
                            end if;

                        elsif held = '1' and (held_op = X"01" or held_op = X"31") then
                            -- the value byte of a held status register write
                            bop  := held_op;
                            held <= '0';
                            arm  <= '0';
                            if held_op = X"01" then
                                lk_ok := arm = '1' and lk1 = '0' and ob(7 downto 2) = "001101" and p_read = '0';
                            else
                                lk_ok := arm = '1' and lk2 = '0' and ob(7 downto 2) = "000000" and ob(0) = '1' and p_read = '0';
                            end if;
                            q(1)  <= ob;
                            q_len <= "10";
                            if lk_ok then
                                q(0) <= held_op;
                                cut_next <= '1';
                                if held_op = X"01" then lk1 <= '1'; else lk2 <= '1'; end if;
                                if ok_cnt /= X"FF" then
                                    ok_cnt <= ok_cnt + 1;
                                end if;
                            else
                                q(0) <= c_sub;
                                blk := true;
                            end if;

                        elsif held = '1' then
                            -- the address byte(s) of a held program/erase
                            bop := held_op;
                            if held4 = '1' and bi = 1 and ob = X"00" and p_read = '0' then
                                -- A31..A24 = 0: hold that too, decide on A23..A16
                                state <= idle;
                                cpu_resp.ack <= '1';
                            else
                                held <= '0';
                                if held4 = '1' and bi = 2 then
                                    allow := unsigned(ob) >= g_low_limit;
                                    q(1)  <= X"00";
                                    q(2)  <= ob;
                                    q_len <= "11";
                                else
                                    -- 3-byte: A23..A16; 4-byte with A31..A24 /= 0: never
                                    allow := held4 = '0' and unsigned(ob) >= g_low_limit;
                                    q(1)  <= ob;
                                    q_len <= "10";
                                end if;
                                if allow then
                                    q(0) <= held_op;
                                    if ok_cnt /= X"FF" then
                                        ok_cnt <= ok_cnt + 1;
                                    end if;
                                else
                                    q(0) <= c_sub;
                                    blk := true;
                                end if;
                            end if;

                        elsif bi = 0 then
                            -- the opcode
                            if classify(ob) /= k_arm and classify(ob) /= k_lock then
                                arm <= '0';
                            end if;
                            case classify(ob) is
                            when k_arm =>
                                if p_read = '0' then
                                    arm <= '1';
                                end if;
                            when k_lock =>
                                if spi_ssn = '0' and p_read = '0' then
                                    held    <= '1';
                                    held_op <= ob;
                                    held4   <= '0';
                                    state <= idle;
                                    cpu_resp.ack <= '1';
                                else
                                    arm  <= '0';
                                    q(0) <= c_sub;      -- alone in its CS pulse: no value
                                    blk := true;
                                end if;
                            when k_pass =>
                                if not g_wren_pass and p_read = '0' and ob = c_wren then
                                    q(0) <= c_wrdi;
                                end if;
                            when k_chk3 | k_chk4 =>
                                if spi_ssn = '0' and p_read = '0' then
                                    held    <= '1';
                                    held_op <= ob;
                                    if classify(ob) = k_chk4 then held4 <= '1'; else held4 <= '0'; end if;
                                    state <= idle;
                                    cpu_resp.ack <= '1';
                                else
                                    q(0) <= c_sub;      -- alone in its CS pulse: no address
                                    blk := true;
                                end if;
                            when k_block =>
                                q(0) <= c_sub;
                                blk := true;
                            end case;

                        elsif cap = '1' then
                            capture(bi, ob, cap4);
                        end if;

                        if blk then
                            sticky <= '1';
                            blk_op <= bop;
                            if blk_cnt /= X"FF" then
                                blk_cnt <= blk_cnt + 1;
                            end if;
                            blk_addr <= (others => '0');
                            if classify(bop) = k_chk3 or is_op4(bop) then
                                cap <= '1';
                                if is_op4(bop) then cap4 <= '1'; else cap4 <= '0'; end if;
                                if held = '1' then
                                    -- the address bytes already known
                                    if held4 = '1' and bi = 2 then
                                        blk_addr(23 downto 16) <= ob;
                                    elsif held4 = '0' then
                                        blk_addr(23 downto 16) <= ob;
                                    end if;
                                end if;
                            else
                                cap <= '0';
                            end if;
                        end if;

                        if seen and bi /= "111" then
                            idx <= bi + 1;
                        end if;
                    end if;
                end if;

            when send =>
                if q_reg = '1' then
                    spi_req.address <= X"0000" & "000" & p_addr;
                    spi_req.data    <= p_data;
                    spi_req.read    <= p_read;
                    spi_req.write   <= not p_read;
                else
                    spi_req.address <= (others => '0');
                    spi_req.data    <= q(to_integer(q_pos));
                    if q_pos = q_len - 1 and q_rd = '1' then
                        spi_req.read  <= '1';
                    else
                        spi_req.write <= '1';
                    end if;
                end if;
                state <= wait_ack;

            when wait_ack =>
                if spi_resp.ack = '1' then
                    if q_pos = q_len - 1 then
                        cpu_resp.ack  <= '1';
                        cpu_resp.data <= spi_resp.data;
                        q_len <= "00";
                    end if;
                    q_pos    <= q_pos + 1;
                    settle_c <= "10";
                    state    <= settle;
                end if;

            when settle =>
                -- i_flash raises busy and moves SSn a clock after its ack
                if settle_c /= 0 then
                    settle_c <= settle_c - 1;
                elsif ready then
                    if q_len /= 0 then
                        state <= send;
                    else
                        state <= idle;
                        if cut_next = '1' then
                            cut      <= '1';
                            cut_next <= '0';
                        end if;
                    end if;
                end if;
            end case;

            if reset = '1' then
                state    <= idle;
                p_valid  <= '0';
                q_len    <= "00";
                force_ss <= '0';
                level_ss <= '1';
                flush    <= '1';
                flushed  <= '0';
                gap      <= (others => '0');
                idx      <= "000";
                held     <= '0';
                sticky   <= '0';
                blk_cnt  <= (others => '0');
                blk_op   <= X"00";
                blk_addr <= (others => '0');
                ok_cnt   <= (others => '0');
                cap      <= '0';
                arm      <= '0';
                lk1      <= '0';
                lk2      <= '0';
                cut      <= '0';
                cut_next <= '0';
            end if;
        end if;
    end process;
end architecture;
