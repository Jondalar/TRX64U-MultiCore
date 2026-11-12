-- TRX Apple //e core (T89.1 D4): the ProDOS hard disk card in slot 7. Written for this project (TREX); the register set and its
-- behaviour are those of the AppleWin hard disk controller as hdd.v of https://github.com/MiSTer-devel/Apple-IIgs_MiSTer implements it
-- (commit 9adbba8622f378253765b7d438b6cbcc4d03fc57, rtl/hdd.v, GPL-3.0-or-later, Copyright (c) Steven A. Wilson and the AppleWin
-- project), re-implemented here without its bus DMA and its sector buffer: the application moves the 512 bytes between the image file and
-- main RAM through the base RAM window while the card holds the CPU (CPU_WAIT; the machine goes on running).
-- PROVENANCE: registers and status codes from rtl/hdd.v; the card ROM is rtl/roms/hdd.a65 of the same repository (AppleWin firmware, GPL),
-- assembled by tools/build_hdd_rom.sh and written by the application into the ROM array (a2_core, WIN_SEL 8) while a unit is mounted.
--
-- $C0F0 + x  (slot 7 device select)         read / write
--   0  EXECUTE   read: runs the command in COMMAND. STATUS: 00 when the unit is mounted, else 01. READ / WRITE: the card stalls the CPU
--                until the application has moved the block, then returns its result (00 ok); a unit that is not mounted gives 01, a write
--                to a protected unit 2B
--   1  STATUS    read: b7 busy (never seen: the CPU waits), b0 error of the last command
--   2  COMMAND   0 status, 1 read, 2 write, 3 format (ignored)           r/w
--   3  UNIT      b7 = the second image                                   r/w
--   4  MEM_L, 5 MEM_H  the buffer address in main RAM                    r/w
--   6  BLK_L, 7 BLK_H  the block number (16 bit: 32 MB)                  r/w
--   9  SIZE_L, A SIZE_H  blocks of the selected unit (the application writes them, `size1`/`size2`)    r
-- $C700 - $C7FF the card ROM (rom_addr / rom_q, synchronous on the enable); a zero array has no card signature, the Autostart scan skips it.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity a2_hdd is
  port (
    clk       : in  std_logic;
    rst       : in  std_logic;
    step      : in  std_logic;                       -- the CPU's bus cycle ends in this clock
    dev_sel   : in  std_logic;                       -- $C0F0 - $C0FF
    io_sel    : in  std_logic;                       -- $C700 - $C7FF
    addr      : in  unsigned(15 downto 0);
    cpu_we    : in  std_logic;
    d_in      : in  unsigned(7 downto 0);
    d_out     : out unsigned(7 downto 0);
    rom_addr  : out unsigned(7 downto 0);
    rom_q     : in  unsigned(7 downto 0);
    cpu_wait  : out std_logic;
    -- the application (levels in sys_clk; ack is one clock)
    ctl       : in  std_logic_vector(3 downto 0);    -- b1:0 unit 2 / unit 1 mounted, b3:2 protected
    size1     : in  std_logic_vector(15 downto 0);
    size2     : in  std_logic_vector(15 downto 0);
    result    : in  std_logic_vector(7 downto 0);
    ack       : in  std_logic;
    req       : out std_logic;                       -- a block is wanted
    req_write : out std_logic;                       -- 1: the application takes the 512 bytes at req_mem out of RAM, 0: puts them in
    req_unit  : out std_logic;
    req_block : out std_logic_vector(15 downto 0);
    req_mem   : out std_logic_vector(15 downto 0);
    req_bank1 : out std_logic;                       -- the language card bank 1 select, sampled when the command was issued
    bank1     : in  std_logic;                       -- the machine's HRAM_BANK1
    cmds      : out unsigned(15 downto 0)            -- block commands issued
  );
end entity;

architecture rtl of a2_hdd is
  type st_t is (S_IDLE, S_REQ, S_DONE);
  signal st      : st_t := S_IDLE;
  signal r_cmd   : std_logic_vector(7 downto 0) := x"00";
  signal r_unit  : std_logic_vector(7 downto 0) := x"00";
  signal r_mem   : std_logic_vector(15 downto 0) := (others => '0');
  signal r_blk   : std_logic_vector(15 downto 0) := (others => '0');
  signal r_stat  : std_logic_vector(7 downto 0) := x"00";
  signal r_res   : std_logic_vector(7 downto 0) := x"00";
  signal ncmd    : unsigned(15 downto 0) := (others => '0');
  signal exec    : std_logic;
  signal mnt, prot : std_logic;
  signal need    : std_logic;
  signal u       : integer range 0 to 1;
  signal size    : std_logic_vector(15 downto 0);
  signal dev_q   : unsigned(7 downto 0);
  signal r_b1    : std_logic := '0';
  signal exec_v  : std_logic_vector(7 downto 0);
begin
  u    <= 1 when r_unit(7) = '1' else 0;
  mnt  <= ctl(u);
  prot <= ctl(2 + u);
  size <= size2 when u = 1 else size1;
  exec <= '1' when dev_sel = '1' and cpu_we = '0' and addr(3 downto 0) = x"0" else '0';
  -- a block command that the application has to serve
  need <= '1' when (r_cmd = x"01" or (r_cmd = x"02" and prot = '0')) and mnt = '1' else '0';
  cpu_wait <= '1' when exec = '1' and need = '1' and st /= S_DONE else '0';

  rom_addr  <= addr(7 downto 0);
  req       <= '1' when st = S_REQ else '0';
  req_write <= '1' when r_cmd = x"02" else '0';
  req_unit  <= r_unit(7);
  req_block <= r_blk;
  req_mem   <= r_mem;
  req_bank1 <= r_b1;
  cmds      <= ncmd;

  -- what a read of EXECUTE returns: the result the application gave, or the answer of a command that needs no application
  exec_v <= r_res                                                  when st = S_DONE else
            x"00"                                                  when r_cmd = x"00" and mnt = '1' else
            x"01"                                                  when r_cmd = x"00" else
            x"2B"                                                  when r_cmd = x"02" and mnt = '1' and prot = '1' else
            x"01"                                                  when (r_cmd = x"01" or r_cmd = x"02") and need = '0' else
            r_stat;

  with addr(3 downto 0) select dev_q <=
    unsigned(exec_v)                   when x"0",
    unsigned(r_stat)                   when x"1",
    unsigned(r_cmd)                    when x"2",
    unsigned(r_unit)                   when x"3",
    unsigned(r_mem(7 downto 0))        when x"4",
    unsigned(r_mem(15 downto 8))       when x"5",
    unsigned(r_blk(7 downto 0))        when x"6",
    unsigned(r_blk(15 downto 8))       when x"7",
    unsigned(size(7 downto 0))         when x"9",
    unsigned(size(15 downto 8))        when x"A",
    x"FF"                              when others;
  d_out <= rom_q when io_sel = '1' else dev_q;

  process (clk)
  begin
    if rising_edge(clk) then
      if rst = '1' then
        st <= S_IDLE; r_cmd <= x"00"; r_unit <= x"00"; r_mem <= (others => '0'); r_blk <= (others => '0'); r_stat <= x"00"; r_res <= x"00";
      else
        case st is
          when S_IDLE =>
            if exec = '1' and need = '1' then
              st <= S_REQ; ncmd <= ncmd + 1; r_b1 <= bank1;
            end if;
          when S_REQ =>
            if ack = '1' then
              st <= S_DONE; r_res <= result;
              if result = x"00" then r_stat <= x"00"; else r_stat <= x"01"; end if;
            end if;
          when S_DONE =>
            if step = '1' and exec = '1' then st <= S_IDLE; end if;
        end case;
        -- the CPU's bus cycle: writes to the registers; the execute read for the commands that do not wait
        if step = '1' and dev_sel = '1' then
          if cpu_we = '1' then
            case addr(3 downto 0) is
              when x"2" =>
                r_cmd <= std_logic_vector(d_in);
                if d_in = x"01" or d_in = x"02" then r_stat <= x"80"; else r_stat <= x"00"; end if;
              when x"3" => r_unit <= std_logic_vector(d_in);
              when x"4" => r_mem(7 downto 0) <= std_logic_vector(d_in);
              when x"5" => r_mem(15 downto 8) <= std_logic_vector(d_in);
              when x"6" => r_blk(7 downto 0) <= std_logic_vector(d_in);
              when x"7" => r_blk(15 downto 8) <= std_logic_vector(d_in);
              when others => null;
            end case;
          elsif addr(3 downto 0) = x"0" and need = '0' then
            case r_cmd is
              when x"00" =>
                if mnt = '1' then r_stat <= x"00"; else r_stat <= x"01"; end if;
              when x"01" | x"02" => r_stat <= x"01";            -- not mounted, or protected
              when others => null;
            end case;
          end if;
        end if;
      end if;
    end if;
  end process;
end architecture;
