-------------------------------------------------------------------------------
-- TRX64U MultiCore T89 / T89.1: signals start at 0 (a Xilinx flip-flop does; the simulator's 'U' never leaves a toggle flip-flop).
-- Changed from NanoApple2 src/drive_ii.vhd (see ../NOTICE): enable CE_14M on every clocked process; T89.1: the byte ring of
-- pre-nibblized bytes is replaced by a BIT engine (the stepper process `update_phase` below is upstream's, unchanged).
--
-- PROVENANCE (T89.1): the behaviours below follow flux_drive.v of https://github.com/MiSTer-devel/Apple-IIgs_MiSTer
-- (commit 9adbba8622f378253765b7d438b6cbcc4d03fc57, rtl/flux_drive.v, GPL-3.0-or-later, Copyright (c) 2026 Alan Steremberg),
-- re-implemented here, no text copied: the 5.25" bit cell of 56 clocks of the 14 MHz machine clock (4 us), the quarter track head
-- (phase 0..139), the position that wraps at the track's bit count, the weak bit rule of the MC3470 read amplifier (after a run of
-- zero bits the next zero cells are random), random flux on an unformatted track, bits written by read-modify-write of the track
-- byte.
-------------------------------------------------------------------------------
--
-- Disk II emulator - drive part
--
-- Original by Stephen A. Edwards, sedwards@cs.columbia.edu
-- Write support by (c)2022 Gyorgy Szombathelyi
--
-------------------------------------------------------------------------------
-- What a drive does here: the disk turns while the motor runs (DISK_ACTIVE): one cell of 56 enables = 8 x 7 of the machine's 14M, so a
-- byte of 8 cells is 448 enables = 32 CPU cycles. The track under the head is a bit stream in the buffer (BUF_*, up to 8,191 bytes = 65,528 bits,
-- MSB first, as a WOZ file has it), BIT_COUNT bits long. Read mode: every cell the bit under the head goes into the shift register;
-- when its bit 7 is set the byte moves to the latch and the register clears (zeros between bytes shift through the empty register: that is
-- the 10-bit self sync). The latch holds until the CPU reads $C08C. Write mode: a byte is loaded by the CPU (LOAD_REQ), starts shifting at
-- the next cell boundary when the register is empty, a cell with an empty register writes a zero.
-- The cell timeline (enables since the boundary): 0 the cell is used; 2 a written bit goes into the buffer; 4 the position advances;
-- 8 the byte at the new position is read (BUF_DO is valid one enable after BUF_ADDR changed).
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity drive_ii is
  port (
    CLK_14M       : in  std_logic;
    CE_14M        : in  std_logic;
    RESET         : in  std_logic;
    MOUNTED       : in  std_logic;                      -- an image is in the drive
    TRACK_READY   : in  std_logic;                      -- the buffer holds the head's quarter track (a2_trk)
    TRACK_BUSY    : in  std_logic;                      -- a request is open: no write into the buffer
    KEEP_OLD      : in  std_logic;                      -- while a track loads, go on playing the buffer instead of noise
    NOISE_TRACK   : in  std_logic;                      -- the loaded track is unformatted: random flux
    BIT_COUNT     : in  unsigned(15 downto 0);          -- bits of the loaded track
    D_IN          : in  unsigned( 7 downto 0);          -- From 6502
    D_OUT         : out unsigned( 7 downto 0);          -- To 6502
    STEP          : in  std_logic;                      -- the CPU's bus cycle ends in this enable
    DISK_ACTIVE   : in  std_logic;                      -- Disk motor on
    MOTOR_PHASE   : in  std_logic_vector(3 downto 0);
    WRITE_MODE    : in  std_logic;                      -- Q7
    LOAD_REQ      : in  std_logic;                      -- the CPU loads the write latch in this cycle (Q6 and Q7 set, a write)
    READ_DISK     : in  std_logic;                      -- the CPU reads $C08C in this cycle
    -- the head and the track buffer
    TRACK         : out unsigned( 7 downto 0);          -- Quarter track (0-139): track 0 is 0, track 1 is 4, a half track is 2
    BUF_ADDR      : out unsigned(12 downto 0);
    BUF_DI        : out unsigned( 7 downto 0);
    BUF_DO        : in  unsigned( 7 downto 0);
    BUF_WE        : out std_logic;
    BIT_WRITTEN   : out std_logic;                      -- one clock: a bit went into the buffer
    CELL_NOISE    : out std_logic                       -- one clock per cell played as noise because the track is loading
    );
end drive_ii;

architecture rtl of drive_ii is

  -- Current phase of the head.  This is in quarter tracks to assign
  -- a unique position to the case, say, when both phase 0 and phase 1 are
  -- on simultaneously.  phase(7 downto 2) is the track number
  signal phase : unsigned(7 downto 0) := (others => '0');  -- 0 - 139

  signal cnt     : integer range 0 to 55 := 0;
  signal pos     : unsigned(15 downto 0) := (others => '0');
  signal cur     : std_logic_vector(7 downto 0) := (others => '0');   -- the byte at `pos`
  signal rsh     : std_logic_vector(7 downto 0) := (others => '0');
  signal latch   : std_logic_vector(7 downto 0) := (others => '0');
  signal zr      : integer range 0 to 7 := 0;                          -- consecutive zero bits met
  signal lfsr    : std_logic_vector(15 downto 0) := x"ACE1";
  signal wsh     : std_logic_vector(7 downto 0) := (others => '0');
  signal wlat    : std_logic_vector(7 downto 0) := (others => '0');
  signal wcnt    : integer range 0 to 8 := 0;
  signal wpend   : std_logic := '0';
  signal we_pend : std_logic := '0';
  signal rd_pend : std_logic := '0';
  signal wbyte   : std_logic_vector(7 downto 0) := (others => '0');

begin

  update_phase : process (CLK_14M, reset)
    variable phase_change : integer;
    variable new_phase : integer;
    variable rel_phase : std_logic_vector(3 downto 0);
  begin
      if reset = '1' then
        phase <= TO_UNSIGNED(70, 8);    -- Deliberately odd to test reset
      elsif rising_edge(CLK_14M) and CE_14M = '1' then
        if DISK_ACTIVE = '1' then
          phase_change := 0;
          new_phase := TO_INTEGER(phase);
          rel_phase := MOTOR_PHASE;
          case phase(2 downto 1) is
            when "00" =>
              rel_phase := rel_phase(1 downto 0) & rel_phase(3 downto 2);
            when "01" =>
              rel_phase := rel_phase(2 downto 0) & rel_phase(3);
            when "10" => null;
            when "11" =>
              rel_phase := rel_phase(0) & rel_phase(3 downto 1);
            when others => null;
          end case;

          if phase(0) = '1' then            -- Phase is odd
            case rel_phase is
              when "0000" => phase_change := 0;
              when "0001" => phase_change := -3;
              when "0010" => phase_change := -1;
              when "0011" => phase_change := -2;
              when "0100" => phase_change := 1;
              when "0101" => phase_change := -1;
              when "0110" => phase_change := 0;
              when "0111" => phase_change := -1;
              when "1000" => phase_change := 3;
              when "1001" => phase_change := 0;
              when "1010" => phase_change := 1;
              when "1011" => phase_change := -3;
              when "1111" => phase_change := 0;
              when others => null;
            end case;
          else                              -- Phase is even
            case rel_phase is
              when "0000" => phase_change := 0;
              when "0001" => phase_change := -2;
              when "0010" => phase_change := 0;
              when "0011" => phase_change := -1;
              when "0100" => phase_change := 2;
              when "0101" => phase_change := 0;
              when "0110" => phase_change := 1;
              when "0111" => phase_change := 0;
              when "1000" => phase_change := 0;
              when "1001" => phase_change := 1;
              when "1010" => phase_change := 2;
              when "1011" => phase_change := -2;
              when "1111" => phase_change := 0;
              when others => null;
            end case;
          end if;

          if new_phase + phase_change <= 0 then
            new_phase := 0;
          elsif new_phase + phase_change > 139 then
            new_phase := 139;
          else
            new_phase := new_phase + phase_change;
          end if;
          phase <= TO_UNSIGNED(new_phase, 8);
        end if;
      end if;
  end process;

  TRACK <= phase;
  BUF_ADDR <= pos(15 downto 3);
  BUF_DI <= unsigned(wbyte);
  D_OUT <= unsigned(latch);

  engine : process (CLK_14M)
    variable sbit, ebit, wb : std_logic;
    variable rn   : std_logic_vector(7 downto 0);
    variable ws   : std_logic_vector(7 downto 0);
    variable wc   : integer range 0 to 8;
    variable nb   : std_logic_vector(7 downto 0);
    variable np   : unsigned(15 downto 0);
    variable stored, noise : boolean;
  begin
    if rising_edge(CLK_14M) then
      if RESET = '1' then
        cnt <= 0; pos <= (others => '0'); rsh <= (others => '0'); latch <= (others => '0'); zr <= 0;
        wsh <= (others => '0'); wcnt <= 0; wpend <= '0'; we_pend <= '0'; rd_pend <= '0';
        BUF_WE <= '0'; BIT_WRITTEN <= '0'; CELL_NOISE <= '0';
      elsif CE_14M = '1' then
        BUF_WE <= '0'; BIT_WRITTEN <= '0'; CELL_NOISE <= '0';
        lfsr <= lfsr(14 downto 0) & (lfsr(15) xor lfsr(13) xor lfsr(12) xor lfsr(10));
        -- the CPU's cycle on the latch: loading the write latch, and the byte consumed by a read of $C08C
        if STEP = '1' then
          if LOAD_REQ = '1' then
            wlat <= std_logic_vector(D_IN); wpend <= '1';
          end if;
          if READ_DISK = '1' and WRITE_MODE = '0' then rd_pend <= '1'; end if;
        end if;
        if rd_pend = '1' then
          latch <= (others => '0'); rd_pend <= '0';
        end if;

        if DISK_ACTIVE = '1' then
          if cnt = 55 then cnt <= 0; else cnt <= cnt + 1; end if;
          case cnt is
            when 0 =>
              -- the cell under the head
              stored := MOUNTED = '1' and (TRACK_READY = '1' or KEEP_OLD = '1') and NOISE_TRACK = '0' and BIT_COUNT /= 0;
              noise  := MOUNTED = '1' and NOISE_TRACK = '1' and TRACK_READY = '1';
              if MOUNTED = '1' and TRACK_READY = '0' and KEEP_OLD = '0' then
                noise := true; CELL_NOISE <= '1';
              end if;
              if MOUNTED = '1' and TRACK_READY = '1' and BIT_COUNT = 0 then noise := true; end if;
              sbit := cur(7 - to_integer(pos(2 downto 0)));
              if WRITE_MODE = '1' then
                -- write mode: bits go out of the shift register, the buffer takes them
                ws := wsh; wc := wcnt;
                if wc = 0 and wpend = '1' then
                  ws := wlat; wc := 8; wpend <= '0';
                end if;
                if wc > 0 then
                  wb := ws(7); ws := ws(6 downto 0) & '0'; wc := wc - 1;
                else
                  wb := '0';
                end if;
                wsh <= ws; wcnt <= wc;
                rsh <= (others => '0');
                if MOUNTED = '1' and TRACK_READY = '1' and TRACK_BUSY = '0' and NOISE_TRACK = '0' and BIT_COUNT /= 0 then
                  nb := cur;
                  nb(7 - to_integer(pos(2 downto 0))) := wb;
                  wbyte <= nb; cur <= nb; we_pend <= '1';
                end if;
              else
                -- read mode
                if stored then
                  ebit := sbit;
                  if sbit = '0' and zr >= 3 then
                    ebit := lfsr(3) and lfsr(11);                -- weak bits: a quarter of the cells of a run of zeros are ones
                  end if;
                  if sbit = '0' then
                    if zr < 7 then zr <= zr + 1; end if;
                  else
                    zr <= 0;
                  end if;
                elsif noise then
                  ebit := lfsr(7);
                  zr <= 0;
                else
                  ebit := '0';
                  zr <= 0;
                end if;
                rn := rsh(6 downto 0) & ebit;
                if rn(7) = '1' then
                  latch <= rn; rsh <= (others => '0'); rd_pend <= '0';
                else
                  rsh <= rn;
                end if;
              end if;
            when 2 =>
              if we_pend = '1' then
                BUF_WE <= '1'; BIT_WRITTEN <= '1'; we_pend <= '0';
              end if;
            when 4 =>
              np := pos + 1;
              if np >= BIT_COUNT then np := (others => '0'); end if;
              pos <= np;
            when 8 =>
              cur <= std_logic_vector(BUF_DO);
            when others => null;
          end case;
        end if;
      end if;
    end if;
  end process;

end rtl;
