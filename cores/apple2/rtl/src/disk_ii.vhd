-------------------------------------------------------------------------------
-- TRX64U MultiCore T89 / T89.1: signals start at 0 (a Xilinx flip-flop does; the simulator's 'U' never leaves a toggle flip-flop).
-- Changed from NanoApple2 src/disk_ii.vhd (see ../NOTICE): enable CE_14M
-- on every clocked process; the boot ROM is outside (rom_addr/rom_dout, synchronous on the
-- enable); the track buffers are the glue's (a2_trk), as before. T89.1: the drives are bit engines (drive_ii.vhd) on a
-- buffer of the track's bits, a quarter track at a time; the soft switch decoding below is upstream's.
-------------------------------------------------------------------------------
--
-- Disk II emulator
--
-- This feeds "pre-nibblized" data to the processor.
--
-- Original by Stephen A. Edwards, sedwards@cs.columbia.edu
-- Write and 2 disks support by (c)2022 Gyorgy Szombathelyi
--
-------------------------------------------------------------------------------
--
-- (upstream: each track is 0x1A00 pre-nibblized bytes; T89.1: it is the bit stream of the track, up to 8,191 bytes)
--
-- X = $60 for slot 6
--
--  Off          On
-- C080,X      C081,X		Phase 0  Head Stepper Motor Control
-- C082,X      C083,X		Phase 1
-- C084,X      C085,X		Phase 2
-- C086,X      C087,X		Phase 3
-- C088,X      C089,X           Motor On
-- C08A,X      C08B,X           Select Drive 2 (select drive 1 when off)
-- C08C,X      C08D,X           Q6  (Shift/load?)
-- C08E,X      C08F,X           Q7  (Write request to drive)
--
--
-- Q7 Q6
-- 0  0  Read
-- 0  1  Sense write protect
-- 1  0  Write
-- 1  1  Load Write Latch
--
-- Reading a byte:
--        LDA $C08E,X  set read mode
-- ...
-- READ   LDA $C08C,X
--        BPL READ
--
-- Sense write protect:
--   LDA $C08D,X
--   LDA $C08E,X
--   BMI PROTECTED
--
-- Writing
--   STA $C08F,X   set write mode
--   CMP $C08C,X   shift byte to disk
--   ..
--   LDA DATA
--   STA $C08D,X   load byte to write
--   CMP $C08C,X   shift byte to disk
--
-- Data bytes must be written in 32 cycle loops.
--
-- There are 70 phases for the head stepper and and 35 tracks,
-- i.e., two phase changes per track.
--
-- The disk spins at 300 rpm; one new bit arrives every 4 us
-- The processor's clock is 1 MHz = 1 us, so it takes 8 * 4 = 32 cycles
-- for a new byte to arrive
--
-------------------------------------------------------------------------------

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity disk_ii is
  port (
    CLK_14M        : in  std_logic;
    CE_14M         : in  std_logic;
    STEP           : in  std_logic;             -- the CPU's bus cycle ends in this enable
    IO_SELECT      : in  std_logic;             -- e.g., C600 - C6FF ROM
    DEVICE_SELECT  : in  std_logic;             -- e.g., C0E0 - C0EF I/O locations
    RESET          : in  std_logic;
    A              : in  unsigned(15 downto 0);
    CPU_WE         : in  std_logic;             -- the CPU's cycle is a write
    D_IN           : in  unsigned( 7 downto 0); -- From 6502
    D_OUT          : out unsigned( 7 downto 0); -- To 6502
    D1_ACTIVE      : buffer std_logic;             -- Disk 1 motor on
    D2_ACTIVE      : buffer std_logic;             -- Disk 2 motor on
    D1_WP          : in std_logic;
    D2_WP          : in std_logic;
    KEEP_OLD       : in std_logic;              -- while a track loads, play the buffer instead of noise
    -- per drive: the track window's state (a2_trk) and the buffer
    MOUNTED        : in  std_logic_vector(1 downto 0);
    TRACK_READY    : in  std_logic_vector(1 downto 0);
    TRACK_BUSY     : in  std_logic_vector(1 downto 0);
    NOISE_TRACK    : in  std_logic_vector(1 downto 0);
    BIT_COUNT1     : in  unsigned(15 downto 0);
    BIT_COUNT2     : in  unsigned(15 downto 0);
    TRACK1         : out unsigned( 7 downto 0); -- Current quarter track (0-139)
    TRACK1_ADDR    : out unsigned(12 downto 0);
    TRACK1_DI      : out unsigned( 7 downto 0);
    TRACK1_DO      : in  unsigned( 7 downto 0);
    TRACK1_WE      : out std_logic;
    TRACK2         : out unsigned( 7 downto 0);
    TRACK2_ADDR    : out unsigned(12 downto 0);
    TRACK2_DI      : out unsigned( 7 downto 0);
    TRACK2_DO      : in  unsigned( 7 downto 0);
    TRACK2_WE      : out std_logic;
    BIT_WRITTEN    : out std_logic_vector(1 downto 0);  -- one clock: a bit went into the buffer of the drive
    -- what the window's counters want to know
    STARVED        : out std_logic;             -- one clock: the CPU read $C08C of a mounted drive whose track is not in the buffer
    CELL_NOISE     : out std_logic;             -- one clock per cell of the selected drive played as noise while a track loads
    -- the 256 byte boot ROM, outside
    ROM_ADDR       : out unsigned(7 downto 0);
    ROM_DOUT       : in  unsigned(7 downto 0)
    );
end disk_ii;

architecture rtl of disk_ii is

  signal motor_phase : std_logic_vector(3 downto 0) := (others => '0');
  signal drive_on : std_logic := '0';
  signal drive_real_on : std_logic := '0'; -- 1 sec delay for turning off the drive
  signal drive2_select : std_logic := '0';
  signal q6, q7 : std_logic := '0';

  signal d_out1   : unsigned(7 downto 0) := (others => '0');
  signal d_out2   : unsigned(7 downto 0) := (others => '0');
  signal data_reg : unsigned(7 downto 0) := (others => '0');

  signal read_disk : std_logic := '0';    -- C08C accessed
  signal load_req  : std_logic := '0';    -- C08D/C08F written with Q6 and Q7 set: the write latch
  signal write_protect_bits : unsigned(7 downto 0) := (others => '0');
  signal cell_n1, cell_n2 : std_logic := '0';
  signal sel_ready, sel_mounted : std_logic;
  signal rd_c08c : std_logic;

begin

  interpret_io : process (CLK_14M)
  begin
    if rising_edge(CLK_14M) and CE_14M = '1' then
      if reset = '1' then
        motor_phase <= (others => '0');
        drive_on <= '0';
        drive2_select <= '0';
        q6 <= '0';
        q7 <= '0';
      else
        if DEVICE_SELECT = '1' then
          if A(3) = '0' then                      -- C080 - C087
            motor_phase(TO_INTEGER(A(2 downto 1))) <= A(0);
          else
            case A(2 downto 1) is
              when "00" => drive_on <= A(0);      -- C088 - C089
              when "01" => drive2_select <= A(0); -- C08A - C08B
              when "10" => q6 <= A(0);            -- C08C - C08D
              when "11" => q7 <= A(0);            -- C08E - C08F
              when others => null;
            end case;
          end if;
        end if;
      end if;
    end if;
  end process;
  
  drive_on_delay: process (CLK_14M, reset)
    variable spindown_delay : unsigned(23 downto 0);  -- Accounts for disk spin rate
    variable drive_on_old : std_logic;
  begin
    if reset = '1' then
      spindown_delay := (others => '0');
      drive_real_on <= '0';
    elsif rising_edge(CLK_14M) and CE_14M = '1' then
      if spindown_delay /= 0 then
        spindown_delay := spindown_delay - 1;
        if spindown_delay = 0 then
          drive_real_on <= '0';
        end if;
      end if;

      if drive_on = '1' then
        spindown_delay := (others => '0');
        drive_real_on <= '1';
      elsif drive_on_old = '1' then
        spindown_delay := to_unsigned(14000000, 24); -- 1 sec delay
      end if;

      drive_on_old := drive_on;

    end if;
  end process;

  D1_ACTIVE <= drive_real_on and not drive2_select;
  D2_ACTIVE <= drive_real_on and drive2_select;

  rd_c08c <= '1' when DEVICE_SELECT = '1' and CPU_WE = '0' and A(3 downto 0) = x"C" else '0';
  read_disk <= rd_c08c;
  -- C08D / C08F written while Q6 and Q7 are both set (the registers hold the effect of this access already: the address is stable for the whole cycle)
  load_req <= '1' when DEVICE_SELECT = '1' and CPU_WE = '1' and A(3 downto 2) = "11" and A(0) = '1' and q6 = '1' and q7 = '1' else '0';

  write_protect_bits <= x"80" when drive2_select = '1' and D2_WP = '1' else x"80" when drive2_select = '0' and D1_WP = '1' else x"00";

  D_OUT <= ROM_DOUT when IO_SELECT = '1' else data_reg when q6 = '0' else write_protect_bits;
  data_reg <= d_out1 when drive2_select = '0' else d_out2;

  drive_1 : entity work.drive_ii
  port map (
    CLK_14M        => CLK_14M,
    CE_14M         => CE_14M,
    RESET          => RESET,
    MOUNTED        => MOUNTED(0),
    TRACK_READY    => TRACK_READY(0),
    TRACK_BUSY     => TRACK_BUSY(0),
    KEEP_OLD       => KEEP_OLD,
    NOISE_TRACK    => NOISE_TRACK(0),
    BIT_COUNT      => BIT_COUNT1,
    D_IN           => D_IN,         -- From 6502
    D_OUT          => d_out1,       -- To 6502
    STEP           => STEP,
    DISK_ACTIVE    => D1_ACTIVE,    -- Disk motor on
    MOTOR_PHASE    => motor_phase,
    WRITE_MODE     => q7,
    LOAD_REQ       => load_req and not drive2_select,
    READ_DISK      => read_disk and not drive2_select,
    TRACK          => TRACK1,
    BUF_ADDR       => TRACK1_ADDR,
    BUF_DI         => TRACK1_DI,
    BUF_DO         => TRACK1_DO,
    BUF_WE         => TRACK1_WE,
    BIT_WRITTEN    => BIT_WRITTEN(0),
    CELL_NOISE     => cell_n1
  );

  drive_2 : entity work.drive_ii
  port map (
    CLK_14M        => CLK_14M,
    CE_14M         => CE_14M,
    RESET          => RESET,
    MOUNTED        => MOUNTED(1),
    TRACK_READY    => TRACK_READY(1),
    TRACK_BUSY     => TRACK_BUSY(1),
    KEEP_OLD       => KEEP_OLD,
    NOISE_TRACK    => NOISE_TRACK(1),
    BIT_COUNT      => BIT_COUNT2,
    D_IN           => D_IN,         -- From 6502
    D_OUT          => d_out2,       -- To 6502
    STEP           => STEP,
    DISK_ACTIVE    => D2_ACTIVE,    -- Disk motor on
    MOTOR_PHASE    => motor_phase,
    WRITE_MODE     => q7,
    LOAD_REQ       => load_req and drive2_select,
    READ_DISK      => read_disk and drive2_select,
    TRACK          => TRACK2,
    BUF_ADDR       => TRACK2_ADDR,
    BUF_DI         => TRACK2_DI,
    BUF_DO         => TRACK2_DO,
    BUF_WE         => TRACK2_WE,
    BIT_WRITTEN    => BIT_WRITTEN(1),
    CELL_NOISE     => cell_n2
  );

  -- the counters of the window: a read of the data latch while the selected drive has an image but not its track
  sel_mounted <= MOUNTED(1) when drive2_select = '1' else MOUNTED(0);
  sel_ready   <= TRACK_READY(1) when drive2_select = '1' else TRACK_READY(0);
  STARVED <= '1' when STEP = '1' and rd_c08c = '1' and sel_mounted = '1' and sel_ready = '0' and drive_real_on = '1' else '0';
  CELL_NOISE <= cell_n2 when drive2_select = '1' else cell_n1;

  ROM_ADDR <= A(7 downto 0);

end rtl;
