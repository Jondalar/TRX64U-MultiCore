-- Read-only SPI NOR flash model for the T15 bench: W25Q128-like, 1-bit, 3-byte
-- addresses. 0x000000 up to the size of g_file comes from g_file (raw bytes),
-- the rest reads 0xFF. Answers 0x03 and 0x0B (read), 0x9F (JEDEC EF 40 18) and
-- 0x05 (status 0); 0xFF is the preamble's dummy. Every other opcode counts in
-- viol and is reported as an error: the boot ROM must only read.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity flash_rom_model is
generic (
    g_file      : string := "flash.bin";
    g_trace     : boolean := false );     -- report every transfer
port (
    csn         : in  std_logic;
    sck         : in  std_logic;
    mosi        : in  std_logic;
    miso        : out std_logic;
    viol        : out natural;
    n_xfer      : out natural;
    n_read      : out natural );      -- data bytes shifted out by 0x03/0x0B
end entity;

architecture sim of flash_rom_model is
    constant c_size : natural := 16#400000#;
    type t_mem is array(0 to c_size - 1) of natural range 0 to 255;
    type t_mem_p is access t_mem;
begin
    process
        type t_cfile is file of character;
        file     f      : t_cfile;
        variable st     : file_open_status;
        variable ch     : character;
        variable mem    : t_mem_p;
        variable n      : natural := 0;
        variable nb     : natural := 0;       -- complete bytes in this transfer
        variable bits   : natural := 0;
        variable op     : natural := 0;
        variable sr     : std_logic_vector(7 downto 0);
        variable outb   : std_logic_vector(7 downto 0) := X"FF";
        variable outv   : boolean := false;
        variable addr   : natural := 0;
        variable a0     : natural := 0;       -- first address of a read
        variable vi, nx, nr : natural := 0;

        impure function rd(a : natural) return natural is
        begin
            if a < c_size then
                return mem(a);
            end if;
            return 255;
        end function;

        -- the byte the chip shifts out after nb complete bytes
        procedure next_out is
        begin
            outv := false;
            outb := X"FF";
            if nb = 0 then
                return;
            end if;
            case op is
            when 16#9F# =>
                outv := true;
                case nb is
                when 1 => outb := X"EF";
                when 2 => outb := X"40";
                when 3 => outb := X"18";
                when others => null;
                end case;
            when 16#05# =>
                outv := true;
                outb := X"00";
            when 16#03# | 16#0B# =>
                if (op = 16#03# and nb >= 4) or nb >= 5 then
                    outv := true;
                    outb := std_logic_vector(to_unsigned(rd(addr), 8));
                    addr := (addr + 1) mod 16#1000000#;
                    nr := nr + 1;
                end if;
            when others =>
                null;
            end case;
        end procedure;
    begin
        mem := new t_mem;
        file_open(st, f, g_file, read_mode);
        if st = open_ok then
            while not endfile(f) and n < c_size loop
                read(f, ch);
                mem(n) := character'pos(ch);
                n := n + 1;
            end loop;
            file_close(f);
        end if;
        for i in n to c_size - 1 loop
            mem(i) := 255;
        end loop;
        report "flash model: " & integer'image(n) & " bytes from " & g_file;
        miso <= '1';
        viol <= 0; n_xfer <= 0; n_read <= 0;
        loop
            wait on csn, sck;
            if csn'event and csn = '0' then
                nb := 0; bits := 0; op := 0;
                outv := false;
                miso <= '1';
            elsif csn'event and csn = '1' then
                if nb > 0 then
                    nx := nx + 1;
                    if g_trace then
                        report "flash: op 0x" & to_hstring(to_unsigned(op, 8)) & " at 0x" &
                               to_hstring(to_unsigned(a0, 24)) & ", " & integer'image(nb) & " bytes";
                    end if;
                    case op is
                    when 16#03# | 16#0B# | 16#9F# | 16#05# | 16#FF# => null;
                    when others =>
                        vi := vi + 1;
                        report "flash model: forbidden opcode 0x" &
                               to_hstring(to_unsigned(op, 8)) severity error;
                    end case;
                end if;
                miso <= '1';
            elsif sck'event and csn = '0' then
                if sck = '1' then
                    sr := sr(6 downto 0) & mosi;
                    bits := bits + 1;
                    if bits mod 8 = 0 then
                        case nb is
                        when 0 => op := to_integer(unsigned(sr));
                        when 1 => addr := to_integer(unsigned(sr)) * 65536;
                        when 2 => addr := addr + to_integer(unsigned(sr)) * 256;
                        when 3 => addr := addr + to_integer(unsigned(sr)); a0 := addr;
                        when others => null;
                        end case;
                        nb := nb + 1;
                        next_out;
                    end if;
                else
                    if outv then
                        miso <= outb(7 - (bits mod 8));
                    else
                        miso <= '1';
                    end if;
                end if;
            end if;
            viol <= vi; n_xfer <= nx; n_read <= nr;
        end loop;
    end process;
end architecture;
