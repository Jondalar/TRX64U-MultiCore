-- SPI NOR flash model for the T44 bench (ours): W25Q128-like, 1-bit, 3-byte
-- address mode only. Decodes on SCK, the bit count is reset by CS, a command
-- executes on the CS rise only when it ended on a byte boundary. Keeps WEL,
-- SR1-3 and a sparse 16 MB array (0x000000-0x3FFFFF holds a pattern, the rest
-- is erased), and checks what it receives against the gate's rules:
--   viol counts every transfer whose opcode is neither in the pass list nor a
--   program/erase with an allowed address, and every executed program/erase
--   below the limit. Execution is permissive (longer commands still execute),
--   the worst case for the gate.
-- g_lock: the lock class of the gate (T152 D9). The model keeps SR1 (bits 7:2) and SR2, executes 0x50 / 0x01 / 0x31
-- like the chip (volatile write enable, the 16-bit SR1 write also takes SR2, nothing once SRL is set) and counts as a
-- violation every one that is not exactly the lock: 0x50 then 0x01 with 0b001101xx, 0x50 then 0x31 with 0b000000x1,
-- each once per configuration pulse, which also stands for a power cycle of the chip here. SRP with SRL (the one-time
-- lock) counts as a violation in any case. Without g_lock every status register write is a violation.
-- Variants: 0 sees every CS pulse and SCK edge; 1 ignores CS-high pulses
-- shorter than g_tdesel (deselect time); 2 loses the first three SCK edges after
-- each cfg pulse (STARTUPE2 after configuration).
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.textio.all;

entity flash_model is
generic (
    g_variant   : natural := 0;
    g_low_limit : natural := 16#40#;
    g_4byte     : boolean := false;
    g_lock      : boolean := false;
    g_tdesel    : time    := 50 ns;       -- variant 1: shorter CS-high pulses are missed
    g_log       : string  := "" );
port (
    cfg         : in  std_logic;          -- pulse: the FPGA was (re)configured
    csn         : in  std_logic;
    sck         : in  std_logic;
    mosi        : in  std_logic;
    miso        : out std_logic;

    hash        : out natural;            -- of every transfer: bytes and framing
    viol        : out natural;
    n_xfer      : out natural;            -- transfers with at least one byte
    n_exec      : out natural;            -- program/erase executed
    n_chk       : out natural;            -- program/erase opcodes received
    n_sub       : out natural;            -- 0x9F received as opcode
    wel         : out std_logic;
    last_op     : out std_logic_vector(7 downto 0);
    last_addr   : out std_logic_vector(23 downto 0);
    sr1_o       : out std_logic_vector(7 downto 0);   -- SR1 as it reads (bits 1:0 = WEL, 0)
    sr2_o       : out std_logic_vector(7 downto 0);
    n_srw       : out natural );                      -- status register writes executed
end entity;

architecture sim of flash_model is
    signal cs_eff : std_logic := '1';
begin
    -- CS as the chip sees it
    process
    begin
        wait on csn;
        if csn = '0' then
            cs_eff <= '0';
        elsif g_variant = 1 then
            wait on csn for g_tdesel;
            if csn = '1' then
                cs_eff <= '1';
            end if;
        else
            cs_eff <= '1';
        end if;
    end process;

    process
        type t_sec is array(0 to 4095) of natural range 0 to 255;
        type t_sec_p is access t_sec;
        type t_mem is array(0 to 4095) of t_sec_p;
        variable mem    : t_mem;
        type t_buf is array(0 to 15) of natural range 0 to 255;
        variable buf    : t_buf;
        variable nb     : natural := 0;       -- complete bytes in this transfer
        variable bits   : natural := 0;
        variable sr     : std_logic_vector(7 downto 0);
        variable outb   : std_logic_vector(7 downto 0) := X"FF";
        variable outv   : boolean := false;
        variable rdaddr : natural := 0;
        variable lost   : natural := 0;
        variable h      : natural := 0;
        variable vi, nx, ne, nc, ns : natural := 0;
        variable s_wel  : std_logic := '0';
        variable sr2    : natural := 2;
        variable sr1v   : natural := 0;               -- SR1 bits 7:2, as a byte
        variable vwe    : boolean := false;           -- volatile write enable (0x50) pending
        variable lkd1, lkd2 : boolean := false;
        variable nsw    : natural := 0;
        variable sr_lock    : boolean := false;
        variable wasvwe : boolean := false;
        variable sr3    : natural := 16#60#;
        variable l      : line;
        file     fo     : text;
        variable opened : boolean := false;
        variable op, a  : natural;
        variable seen_op: boolean;

        procedure fold(v : natural) is
        begin
            h := (h * 33 + v) mod 16777213;
        end procedure;

        function init_byte(addr : natural) return natural is
        begin
            if addr < 16#400000# then
                return ((addr / 256) * 7 + addr) mod 256;
            end if;
            return 255;
        end function;

        impure function rd(addr : natural) return natural is
            variable ad : natural := addr mod 16#1000000#;
        begin
            if mem(ad / 4096) = null then
                return init_byte(ad);
            end if;
            return mem(ad / 4096)(ad mod 4096);
        end function;

        procedure touch(sec : natural) is
        begin
            if mem(sec) = null then
                mem(sec) := new t_sec;
                for i in 0 to 4095 loop
                    mem(sec)(i) := init_byte(sec * 4096 + i);
                end loop;
            end if;
        end procedure;

        procedure wr(addr : natural; v : natural) is
            variable ad : natural := addr mod 16#1000000#;
        begin
            touch(ad / 4096);
            mem(ad / 4096)(ad mod 4096) := v;
        end procedure;

        procedure erase(addr : natural; size : natural) is
            variable base : natural := (addr mod 16#1000000#) - ((addr mod 16#1000000#) mod size);
        begin
            for s in base / 4096 to (base + size) / 4096 - 1 loop
                touch(s);
                for i in 0 to 4095 loop
                    mem(s)(i) := 255;
                end loop;
            end loop;
        end procedure;

        -- the byte the chip shifts out next, after nb complete bytes
        procedure next_out is
        begin
            outv := false;
            outb := X"FF";
            if nb = 0 then
                return;
            end if;
            case buf(0) is
            when 16#9F# =>
                outv := true;
                case nb is
                when 1 => outb := X"EF";
                when 2 => outb := X"40";
                when 3 => outb := X"18";
                when others => outb := X"FF";
                end case;
            when 16#05# =>
                outv := true;
                outb := std_logic_vector(to_unsigned(sr1v, 8));
                outb(1 downto 0) := s_wel & '0';
            when 16#35# => outv := true; outb := std_logic_vector(to_unsigned(sr2, 8));
            when 16#15# => outv := true; outb := std_logic_vector(to_unsigned(sr3, 8));
            when 16#03# | 16#0B# =>
                if (buf(0) = 16#03# and nb >= 4) or nb >= 5 then
                    if (buf(0) = 16#03# and nb = 4) or (buf(0) = 16#0B# and nb = 5) then
                        rdaddr := buf(1) * 65536 + buf(2) * 256 + buf(3);
                    end if;
                    outv := true;
                    outb := std_logic_vector(to_unsigned(rd(rdaddr), 8));
                    rdaddr := (rdaddr + 1) mod 16#1000000#;
                end if;
            when 16#4B# =>
                if nb >= 5 then
                    outv := true;
                    outb := std_logic_vector(to_unsigned(16#A0# + nb, 8));
                end if;
            when 16#90# =>
                if nb >= 4 then
                    outv := true;
                    if nb mod 2 = 0 then outb := X"EF"; else outb := X"17"; end if;
                end if;
            when 16#AB# =>
                if nb >= 4 then outv := true; outb := X"17"; end if;
            when others =>
                null;
            end case;
        end procedure;

        function is_pass(o : natural) return boolean is
        begin
            case o is
            when 16#03# | 16#0B# | 16#05# | 16#35# | 16#15# | 16#9F# | 16#4B# | 16#90# |
                 16#AB# | 16#5A# | 16#06# | 16#04# | 16#66# | 16#99# | 16#FF# => return true;
            when 16#50# => return g_lock;
            when others => return false;
            end case;
        end function;

        function is_chk3(o : natural) return boolean is
        begin
            return o = 16#02# or o = 16#32# or o = 16#20# or o = 16#52# or o = 16#D8#;
        end function;

        function is_chk4(o : natural) return boolean is
        begin
            return o = 16#12# or o = 16#34# or o = 16#21# or o = 16#53# or o = 16#DC#;
        end function;

        procedure finish is
            variable ok : boolean;
            variable alen, need, addr : natural;
        begin
            fold(512 + bits mod 8);
            if nb = 0 then
                return;
            end if;
            nx := nx + 1;
            op := buf(0);
            if g_log /= "" then
                if not opened then
                    file_open(fo, g_log, write_mode);
                    opened := true;
                end if;
                for i in 0 to nb - 1 loop
                    if i < 16 then
                        hwrite(l, std_logic_vector(to_unsigned(buf(i), 8)));
                        write(l, string'(" "));
                    end if;
                end loop;
                write(l, string'("n=") & integer'image(nb) & " b=" & integer'image(bits mod 8));
                writeline(fo, l);
            end if;

            -- the gate's rules, on what the chip received
            ok := is_pass(op);
            if op = 16#01# then
                ok := g_lock and nb = 2 and vwe and not lkd1 and buf(1) / 256 = 0 and (buf(1) / 4) = 16#0D#;
            elsif op = 16#31# then
                ok := g_lock and nb = 2 and vwe and not lkd2 and (buf(1) / 4) = 0 and buf(1) mod 2 = 1;
            end if;
            if is_chk3(op) or is_chk4(op) then
                nc := nc + 1;
                if is_chk3(op) then
                    ok := nb >= 2 and buf(1) >= g_low_limit;
                else
                    ok := g_4byte and nb >= 3 and buf(1) = 0 and buf(2) >= g_low_limit;
                end if;
            end if;
            if op = 16#9F# then
                ns := ns + 1;
            end if;
            if not ok then
                vi := vi + 1;
                report "flash model " & integer'image(g_variant) & ": forbidden command 0x" &
                       to_hstring(to_unsigned(op, 8)) & " received, " & integer'image(nb) & " bytes"
                    severity error;
            end if;
            last_op <= std_logic_vector(to_unsigned(op, 8));
            if nb >= 4 then
                last_addr <= std_logic_vector(to_unsigned(buf(1), 8)) &
                             std_logic_vector(to_unsigned(buf(2), 8)) &
                             std_logic_vector(to_unsigned(buf(3), 8));
            else
                last_addr <= (others => '0');
            end if;

            wasvwe := vwe;
            if op /= 16#50# then
                vwe := false;                   -- any other command ends the volatile enable
            end if;
            if bits mod 8 /= 0 then
                return;                         -- not on a byte boundary: nothing
            end if;
            if op = 16#01# or op = 16#31# then
                -- status register write, executed on a byte boundary with the enable (volatile or WEL); the chip
                -- takes SR2 as the second data byte of a 0x01 write
                if (wasvwe or s_wel = '1') and ((op = 16#01# and (nb = 2 or nb = 3)) or (op = 16#31# and nb = 2)) and not sr_lock then
                    if op = 16#01# then
                        sr1v := (buf(1) / 4) * 4;
                        if nb = 3 then sr2 := buf(2); end if;
                        if sr1v >= 128 and nb = 3 and buf(2) mod 2 = 1 then
                            vi := vi + 1;
                            report "flash model: SRP with SRL, the one-time lock" severity error;
                        end if;
                    else
                        sr2 := buf(1);
                    end if;
                    if sr2 mod 2 = 1 then sr_lock := true; end if;
                    if sr_lock and sr1v >= 128 then
                        vi := vi + 1;
                        report "flash model: SRP with SRL, the one-time lock" severity error;
                    end if;
                    nsw := nsw + 1;
                    if ok then
                        if op = 16#01# then lkd1 := true; else lkd2 := true; end if;
                    end if;
                end if;
                s_wel := '0';
                return;
            end if;
            case op is
            when 16#50# => vwe := (nb = 1);
            when 16#06# => s_wel := '1';
            when 16#04# => s_wel := '0';
            when others =>
                if is_chk3(op) or is_chk4(op) then
                    if is_chk3(op) then alen := 3; else alen := 4; end if;
                    if op = 16#02# or op = 16#32# or op = 16#12# or op = 16#34# then
                        need := alen + 2;
                    else
                        need := alen + 1;
                    end if;
                    if nb >= need and s_wel = '1' then
                        if alen = 3 then
                            addr := buf(1) * 65536 + buf(2) * 256 + buf(3);
                        else
                            -- 16 MB: A31..A24 wrap
                            addr := buf(2) * 65536 + buf(3) * 256 + buf(4);
                        end if;
                        if addr / 65536 < g_low_limit or (alen = 4 and buf(1) /= 0) then
                            vi := vi + 1;
                            report "flash model: program/erase executed at 0x" &
                                   to_hstring(to_unsigned(addr, 24)) severity error;
                        end if;
                        ne := ne + 1;
                        case op is
                        when 16#20# | 16#21# => erase(addr, 4096);
                        when 16#52# | 16#53# => erase(addr, 32768);
                        when 16#D8# | 16#DC# => erase(addr, 65536);
                        when others =>
                            null;               -- page program: written above, from pbuf
                        end case;
                        s_wel := '0';
                    end if;
                elsif not is_pass(op) and s_wel = '1' then
                    s_wel := '0';
                end if;
            end case;
        end procedure;

        -- page program data, all bytes
        type t_page is array(0 to 255) of natural range 0 to 255;
        variable pbuf   : t_page;
        variable pn     : natural := 0;
        variable dstart : natural;
        variable paddr  : natural;
    begin
        miso <= '1';
        hash <= 0; viol <= 0; n_xfer <= 0; n_exec <= 0; n_chk <= 0; n_sub <= 0;
        sr1_o <= X"00"; sr2_o <= X"02"; n_srw <= 0;
        wel <= '0'; last_op <= X"00"; last_addr <= (others => '0');
        loop
            wait on cs_eff, sck, cfg;
            if cfg'event and cfg = '1' then
                if g_variant = 2 then
                    lost := 3;
                end if;
                sr1v := 0; sr2 := 2; sr_lock := false; vwe := false; lkd1 := false; lkd2 := false;
            end if;
            if cs_eff'event and cs_eff = '0' then
                nb := 0; bits := 0; pn := 0;
                fold(256);
                next_out;
                miso <= '1';
            elsif cs_eff'event and cs_eff = '1' then
                -- page program: write the collected data before finish clears WEL
                op := 0;
                if nb > 0 then op := buf(0); end if;
                if (op = 16#02# or op = 16#32# or op = 16#12# or op = 16#34#) and bits mod 8 = 0
                   and s_wel = '1' then
                    if op = 16#02# or op = 16#32# then dstart := 4; else dstart := 5; end if;
                    if nb > dstart then
                        if dstart = 4 then
                            paddr := buf(1) * 65536 + buf(2) * 256 + buf(3);
                        else
                            paddr := buf(2) * 65536 + buf(3) * 256 + buf(4);
                        end if;
                        -- more than 256 bytes: the last 256 wrap onto the page
                        for i in 0 to pn - 1 loop
                            if i + 256 >= pn then
                                a := (paddr - paddr mod 256) + (paddr + i) mod 256;
                                wr(a, to_integer(to_unsigned(rd(a), 8) and to_unsigned(pbuf(i mod 256), 8)));
                            end if;
                        end loop;
                    end if;
                end if;
                finish;
                miso <= '1';
            elsif sck'event and cs_eff = '0' then
                if sck = '1' then
                    if lost > 0 then
                        lost := lost - 1;
                    else
                        sr := sr(6 downto 0) & mosi;
                        bits := bits + 1;
                        if bits mod 8 = 0 then
                            if nb < 16 then
                                buf(nb) := to_integer(unsigned(sr));
                            end if;
                            -- page program data bytes
                            if nb >= 4 and (buf(0) = 16#02# or buf(0) = 16#32#) then
                                pbuf(pn mod 256) := to_integer(unsigned(sr));
                                pn := pn + 1;
                            elsif nb >= 5 and (buf(0) = 16#12# or buf(0) = 16#34#) then
                                pbuf(pn mod 256) := to_integer(unsigned(sr));
                                pn := pn + 1;
                            end if;
                            nb := nb + 1;
                            fold(to_integer(unsigned(sr)));
                            next_out;
                        end if;
                    end if;
                else
                    -- shift out on the falling edge, MSB first
                    if outv then
                        miso <= outb(7 - (bits mod 8));
                    else
                        miso <= '1';
                    end if;
                end if;
            elsif sck'event and lost > 0 and sck = '1' then
                lost := lost - 1;               -- lost edges, CS high or low alike
            end if;
            hash <= h; viol <= vi; n_xfer <= nx; n_exec <= ne; n_chk <= nc; n_sub <= ns;
            wel <= s_wel;
            if s_wel = '1' then
                sr1_o <= std_logic_vector(to_unsigned(sr1v + 2, 8));
            else
                sr1_o <= std_logic_vector(to_unsigned(sr1v, 8));
            end if;
            sr2_o <= std_logic_vector(to_unsigned(sr2, 8));
            n_srw <= nsw;
        end loop;
    end process;
end architecture;
