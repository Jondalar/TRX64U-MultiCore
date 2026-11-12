-------------------------------------------------------------------------------
-- TRX64U MultiCore T89: signals start at 0 (a Xilinx flip-flop does; the simulator's 'U' never leaves a toggle flip-flop).
-- Changed from NanoApple2 src/apple2.vhd (4977e049, see ../NOTICE):
--   * every clocked process runs on CLK_14M with the enable CE_14M (the machine counts 14.318 MHz
--     enables of the host's sys_clk, not a clock of its own)
--   * no ROM, no Gowin block, no ioctl port: the system ROM and the character ROM are outside
--     (rom_addr/rom_out, video_rom_addr/video_rom_out), RAM was outside already
--   * one CPU, the 65C02 (R65C02): the Enhanced Apple //e only (T89 Q1); T65 and the `cpu` pin are gone
--   * Q3_RISE: the next enable toggles the 2 MHz clock Q3 (and PHI0 with it): the RAM takes the address of the new
--     phase one enable later (the SDRAM of NanoApple2 starts its cycle after the rising edge of CLK_2M), and shows the
--     data from the enable after that on; the CPU address, the video address and the CPU's write enable are outputs so
--     that a memory behind a latency can look the next phase's address up before the toggle
--   * DHIRES_MODE, COL80, ALTCHAR and the other soft switches are outputs for the glue
--   * T89.1: CPU_STEP, the enable in which the CPU's bus cycle ends (the cards act on it: the write latch of the Disk II, the hard disk's commands)
-------------------------------------------------------------------------------
-- Top level of an Apple //e
-- Szombathelyi György
--
-- Based on:
-- Top level of an Apple ][+
--
-- Stephen A. Edwards, sedwards@cs.columbia.edu
--
-------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity apple2 is
  port (
    CLK_14M        : in  std_logic;              -- the host's sys_clk
    CE_14M         : in  std_logic;              -- 14.31818 MHz master clock enable
    PALMODE        : in  std_logic := '0';       -- PAL/NTSC selection
    ROMSWITCH      : in std_logic;
    CLK_2M         : out std_logic;
    Q3_RISE        : out std_logic;              -- the next enable toggles Q3 / PHI0: the RAM samples the new phase's address one enable later
    CPU_WAIT       : in  std_logic := '0';
    CPU_STEP       : out std_logic;              -- T89.1: the enable in which the CPU's bus cycle ends (it steps; not while CPU_WAIT)
    PHASE_ZERO     : buffer std_logic;
    PHASE_ZERO_R   : buffer std_logic;           -- next clock is PHI0=1
    PHASE_ZERO_F   : buffer std_logic;           -- next clock is PHI0=0
    FLASH_CLK      : in  std_logic;              -- approx. 2 Hz flashing char clock
    reset          : in  std_logic;
    ADDR           : out unsigned(15 downto 0);  -- CPU address
    ram_addr       : out unsigned(15 downto 0);  -- RAM address
    D              : out unsigned(7 downto 0);   -- Data to RAM
    ram_do         : in unsigned(15 downto 0);   -- Data from RAM (lo byte: MAIN RAM, hi byte: AUX RAM)
    aux            : buffer std_logic;           -- Write to MAIN or AUX RAM
    PD             : in unsigned(7 downto 0);    -- Data to CPU from peripherals
    CPU_WE         : out std_logic;
    IRQ_n          : in std_logic;
    NMI_n          : in std_logic;
    ram_we         : out std_logic;              -- RAM write enable
    VIDEO          : out std_logic;
    COLOR_LINE     : out std_logic;
    TEXT_MODE      : buffer std_logic;
    HBL            : out std_logic;
    VBL            : buffer std_logic;
    K              : in unsigned(7 downto 0);    -- Keyboard data
    KEYSTROBE      : out std_logic;              -- Processor has read key
    AKD            : in std_logic;               -- Any key down flag
    AN             : buffer std_logic_vector(3 downto 0);  -- Annunciator outputs
    -- GAMEPORT input bits:
    --  7    6    5    4    3   2   1    0
    -- pdl3 pdl2 pdl1 pdl0 pb3 pb2 pb1 casette
    GAMEPORT       : in std_logic_vector(7 downto 0);
    PDL_STROBE     : buffer std_logic;           -- Pulses high when C07x read
    STB            : buffer std_logic;           -- Pulses high when C04x read
    IO_SELECT      : out std_logic_vector(7 downto 0);
    DEVICE_SELECT  : out std_logic_vector(7 downto 0);
    -- the system ROM and the character ROM, synchronous, outside (one enable cycle of latency)
    rom_addr       : out unsigned(13 downto 0);
    rom_out        : in  unsigned(7 downto 0);
    video_rom_addr : out std_logic_vector(12 downto 0);
    video_rom_out  : in  unsigned(7 downto 0);
    CPU_ADDR_RAM   : out unsigned(15 downto 0);  -- the RAM address of the CPU's cycle (bank switched), valid from the CPU's enable on
    VIDEO_ADDR     : out unsigned(15 downto 0);  -- the RAM address of the scanner's cycle
    CPU_RAM_WR     : out std_logic;              -- the CPU writes RAM in this cycle (not gated by the phase)
    CPU_RAM_SEL    : out std_logic;              -- the CPU's address is in RAM (main or language card), not ROM or I/O
    FLOAT_BUS      : out unsigned(7 downto 0);   -- the video data latch: what a bus nobody answers shows
    -- soft switches the glue needs
    COL80_O        : out std_logic;
    ALTCHAR_O      : out std_logic;
    HIRES_O        : out std_logic;
    PAGE2_O        : out std_logic;
    MIXED_O        : out std_logic;
    STORE80_O      : out std_logic;
    DHIRES_O       : out std_logic;
    HRAM_BANK1_O   : out std_logic;              -- T89.1 S13: language card bank 1 is selected for $D000-$DFFF (the CPU sees it at the RAM $C000-$CFFF)

    speaker        : out std_logic              -- One-bit speaker output
    );
end apple2;

architecture rtl of apple2 is

  -- Clocks
  signal CLK_7M : std_logic := '0';
  signal Q3, RAS_N, CAS_N, AX : std_logic := '0';
  signal COLOR_REF : std_logic := '0';
  signal CPU_EN : std_logic := '0';
  signal PHASE_ZERO_D : std_logic := '0';

  -- From the timing generator
  signal VIDEO_ADDRESS : unsigned(15 downto 0) := (others => '0');
  signal LDPS_N : std_logic := '0';
  signal WNDW_N, GR2 : std_logic := '0';
  signal SEGA, SEGB, SEGC : std_logic := '0';
  
  -- Soft switches
  signal soft_switches : std_logic_vector(7 downto 0) := "00000000";
  signal MIXED_MODE : std_logic := '0';
  signal PAGE2 : std_logic := '0';
  signal HIRES_MODE : std_logic := '0';
  signal DHIRES_MODE : std_logic := '0';

  -- ][e auxilary switches
  signal RAMRD : std_logic := '0';
  signal RAMWRT : std_logic := '0';
  signal CXROM : std_logic := '0';
  signal STORE80 : std_logic := '0';
  signal C3ROM : std_logic := '0';
  signal C8ROM : std_logic := '0';
  signal ALTZP : std_logic := '0';
  signal ALTCHAR : std_logic := '0';
  signal COL80 : std_logic := '0';
  signal SF_D : std_logic := '0';

  -- CPU signals
  signal D_IN : unsigned(7 downto 0) := (others => '0');
  signal D_OUT: unsigned(7 downto 0) := (others => '0');
  signal A : unsigned(15 downto 0) := (others => '0');
  signal R65C02_A : unsigned(15 downto 0) := (others => '0');
  signal R65C02_DO : unsigned(7 downto 0) := (others => '0');
  signal R65C02_WE_N : std_logic := '0';
  signal we : std_logic := '0';


  -- Address decoder signals
  signal RAM_SELECT : std_logic := '1';
  signal READ_KEY : std_logic := '0';
  signal KEYBOARD_SELECT : std_logic := '0';
  signal TAPE_OUT : std_logic := '0';
  signal SPEAKER_SELECT : std_logic := '0';
  signal SOFTSWITCH_SELECT : std_logic := '0';
  signal ROM_SELECT : std_logic := '0';
  signal GAMEPORT_SELECT : std_logic := '0';
  signal IO_STROBE : std_logic := '0';
  signal HRAM_CONTROL : std_logic := '0';

  -- Speaker signal
  signal speaker_sig : std_logic := '0';        

  signal CPU_DL, VIDEO_DL : unsigned(7 downto 0) := (others => '0');     -- Latched RAM data
  signal VIDEO_DL_LATCH : unsigned(15 downto 0) := (others => '0');
  
  -- Bank Switched RAM signals
  signal Dxxx : std_logic := '0';
  signal HRAM_READ : std_logic := '0';
  signal HRAM_PRE_WR : std_logic := '0';
  signal HRAM_WR_N : std_logic := '0';
  signal HRAM_BANK1 : std_logic := '0';
  signal CPU_RAM_ADDR : unsigned(15 downto 0) := (others => '0');

  signal HRAM_READ_EN : std_logic := '0';
  signal HRAM_WRITE_EN : std_logic := '0';

  signal ioselect  : std_logic_vector(7 downto 0) := (others => '0');
  signal devselect : std_logic_vector(7 downto 0) := (others => '0');
  
  signal R_W_n     : std_logic := '0';

begin

  CLK_2M <= Q3;

  ram_addr <= CPU_RAM_ADDR when PHASE_ZERO = '1' else VIDEO_ADDRESS;
  ram_we <= ((we and RAM_SELECT) or (we and HRAM_WRITE_EN)) when PHASE_ZERO = '1' else '0';
  CPU_WE <= we;

	CPU_DL <= ram_do(7 downto 0) when aux = '0' else ram_do(15 downto 8);

  RAM_data_latch : process (CLK_14M)
  begin
    if rising_edge(CLK_14M) and CE_14M = '1' then
      if AX = '1' and CAS_N = '0' and RAS_N = '1' and Q3 = '0' and PHASE_ZERO = '0' then
        -- Latch video data at Phase 0
        VIDEO_DL_LATCH <= ram_do;
      end if;
    end if;
  end process;
  VIDEO_DL <= VIDEO_DL_LATCH(7 downto 0) when PHASE_ZERO = '0' else VIDEO_DL_LATCH(15 downto 8);

  ADDR <= A;
  D <= D_OUT;

  IO_SELECT <= ioselect;
  DEVICE_SELECT <= devselect;

  -- Address decoding
  rom_addr <= A(13 downto 0);

  address_decoder: process (A, C3ROM, C8ROM, CXROM, we)
  begin
    ROM_SELECT <= '0';
    RAM_SELECT <= '0';
    KEYBOARD_SELECT <= '0';
    KEYSTROBE <= '0';
    READ_KEY <= '0';
    TAPE_OUT <= '0';
    SPEAKER_SELECT <= '0';
    SOFTSWITCH_SELECT <= '0';
    GAMEPORT_SELECT <= '0';
    PDL_STROBE <= '0';
    STB <= '0';
    HRAM_CONTROL <= '0';
    ioselect <= (others => '0');
    devselect <= (others => '0');
    IO_STROBE <= '0';
    case A(15 downto 14) is
      when "00" | "01" | "10" =>         -- 0000 - BFFF
        RAM_SELECT <= '1';
      when "11" => -- C000 - FFFF
        case A(13 downto 12) is
          when "00" =>                  -- C000 - CFFF
            case A(11 downto 8) is
              when x"0" =>              -- C000 - C0FF
                case A(7 downto 4) is
                  when x"0" =>          -- C000 - C00F
                     KEYBOARD_SELECT <= '1';
                  when x"1" =>          -- C010 - C01F
                     READ_KEY <= '1';
                     if we = '1' or A(3 downto 0) = x"0" then
                         KEYSTROBE <= '1';
                     end if;
                  when x"2" =>          -- C020 - C02F
                    TAPE_OUT <= '1';
                  when x"3" =>          -- C030 - C03F
                    SPEAKER_SELECT <= '1';
                  when x"4" =>          -- C040 - C04F
                    STB <= '1';
                  when x"5" =>          -- C050 - C05F
                    SOFTSWITCH_SELECT <= '1';
                  when x"6" =>          -- C060 - C06F
                    GAMEPORT_SELECT <= '1';
                  when x"7" =>          -- C070 - C07F
                    PDL_STROBE <= '1';
                  when x"8" =>          -- C080 - C08F
                    HRAM_CONTROL <= '1';
                  when x"9" | x"A" | x"B" | -- C090 - C0FF
                       x"C" | x"D" | x"E" | x"F" =>
                    devselect(TO_INTEGER(A(6 downto 4))) <= '1';
                  when others => null;                
                end case;
              when x"1" | x"2" |   -- C100 - C2FF, C400-C7FF
                   x"4" | x"5" | x"6" | x"7" =>
                if CXROM = '1' then
                  ROM_SELECT <= '1';
                else
                  ioselect(TO_INTEGER(A(10 downto 8))) <= '1';
                end if;
              when x"3" => -- C300 - C3FF
                if CXROM = '1' or C3ROM = '0' then
                  ROM_SELECT <= '1';
                else
                  ioselect(TO_INTEGER(A(10 downto 8))) <= '1';
                end if;
              when x"8" | x"9" | x"A" |  -- C800 - CFFF
                   x"B" | x"C" | x"D" | x"E" | x"F" =>
                if CXROM = '1' or C8ROM = '1' then
                  ROM_SELECT <= '1';
                else
                  IO_STROBE <= '1';
                end if;
              when others => null;
            end case;
          when "01" | "10" | "11" =>    -- D000 - FFFF
            ROM_SELECT <= '1';
          when others =>
            null;
        end case;
      when others => null;
    end case;        
  end process address_decoder;

  aux_ctrl: process(A, we, RAMRD, RAMWRT, STORE80, HIRES_MODE, PAGE2, ALTZP)
  begin
    aux <= '0';
    if A(15 downto 9) = "0000000" or A(15 downto 14) = "11" then -- Page 00,01,C0-FF
        aux <= ALTZP;
    elsif A(15 downto 10) = "000001" then -- Page 04-07
        aux <= (STORE80 and PAGE2) or (not STORE80 and (( RAMRD and not we ) or ( RAMWRT and we)));
    elsif A(15 downto 13) = "001" then -- Page 20-3F
        aux <= (STORE80 and PAGE2 and HIRES_MODE) or ((not STORE80 or not HIRES_MODE) and (( RAMRD and not we ) or ( RAMWRT and we)));
    else
        aux <= ( RAMRD and not we ) or ( RAMWRT and we);
    end if;
  end process aux_ctrl;

  speaker_ctrl: process (CLK_14M)
  begin
    if rising_edge(CLK_14M) and CE_14M = '1' then
      if PHASE_ZERO_R = '1' and SPEAKER_SELECT = '1' then
        speaker_sig <= not speaker_sig;
      end if;
    end if;
  end process speaker_ctrl;

  softswitches: process (CLK_14M)
  begin
    if rising_edge(CLK_14M) and CE_14M = '1' then
      if PHASE_ZERO_R = '1' and SOFTSWITCH_SELECT = '1' then
        soft_switches(TO_INTEGER(A(3 downto 1))) <= A(0);
      end if;
    end if;
  end process softswitches;

  TEXT_MODE <= soft_switches(0);
  MIXED_MODE <= soft_switches(1);
  PAGE2 <= soft_switches(2);
  HIRES_MODE <= soft_switches(3);
  AN <= soft_switches(7 downto 4);
  DHIRES_MODE <= AN(3);

  hram_ctrl: process (CLK_14M, reset)
  begin
    if reset = '1' then
        HRAM_PRE_WR <= '0';
        HRAM_READ <= '0';
        HRAM_WR_N <= '0';
        HRAM_BANK1 <= '0';
    elsif rising_edge(CLK_14M) and CE_14M = '1' then
      if PHASE_ZERO_R = '1' and HRAM_CONTROL = '1' then
        HRAM_BANK1 <= A(3);
        HRAM_PRE_WR <= A(0) and not we;
        if (HRAM_PRE_WR and not we and A(0)) = '1' then
            HRAM_WR_N <= '0';
        elsif A(0) = '0' then
            HRAM_WR_N <= '1';
        end if;
        HRAM_READ <= not (A(0) xor A(1));
      end if;
    end if;
  end process hram_ctrl;

  HRAM_BANK1_O <= HRAM_BANK1;
  Dxxx <= '1' when A(15 downto 12) = x"D" else '0';
  CPU_RAM_ADDR <= A(15 downto 13) & (A(12) and not (HRAM_BANK1 and Dxxx)) & A(11 downto 0);
  HRAM_READ_EN <= HRAM_READ and A(15) and A(14) and (A(13) or A(12)); -- Dxxx-Fxxx
  HRAM_WRITE_EN <= not HRAM_WR_N and A(15) and A(14) and (A(13) or A(12)); -- Dxxx-Fxxx

  softswitches_IIe: process (CLK_14M, reset)
  begin
    if reset = '1' then
      STORE80 <= '0';
      RAMRD <= '0';
      RAMWRT <= '0';
      CXROM <= '0';
      ALTZP <= '0';
      C3ROM <= '0';
      C8ROM <= '0';
      COL80 <= '0';
      ALTCHAR <= '0';
    elsif rising_edge(CLK_14M) and CE_14M = '1' then
      if A(15 downto 8) = x"C3" and C3ROM = '0' then
        C8ROM <= '1';
      elsif A = x"CFFF" then
        C8ROM <= '0';
      end if;
      if PHASE_ZERO_R = '1' and KEYBOARD_SELECT = '1' and we = '1' then
        case A(3 downto 1) is
        when "000" => STORE80 <= A(0);
        when "001" => RAMRD <= A(0);
        when "010" => RAMWRT <= A(0);
        when "011" => CXROM <= A(0);
        when "100" => ALTZP <= A(0);
        when "101" => C3ROM <= A(0);
        when "110" => COL80 <= A(0);
        when "111" => ALTCHAR <= A(0);
        when others => null;
        end case;
      elsif READ_KEY = '1' and we = '0' then
        case A(3 downto 0) is
        when x"0" => SF_D <= AKD;
        when x"1" => SF_D <= not HRAM_BANK1;
        when x"2" => SF_D <= HRAM_READ;
        when x"3" => SF_D <= RAMRD;
        when x"4" => SF_D <= RAMWRT;
        when x"5" => SF_D <= CXROM;
        when x"6" => SF_D <= ALTZP;
        when x"7" => SF_D <= C3ROM;
        when x"8" => SF_D <= STORE80;
        when x"9" => SF_D <= not VBL;
        when x"A" => SF_D <= TEXT_MODE;
        when x"B" => SF_D <= MIXED_MODE;
        when x"C" => SF_D <= PAGE2;
        when x"D" => SF_D <= HIRES_MODE;
        when x"E" => SF_D <= ALTCHAR;
        when x"F" => SF_D <= COL80;
        when others => null;
        end case;
      end if;
    end if;
  end process softswitches_IIe;

  speaker <= speaker_sig;

  D_IN <= CPU_DL when RAM_SELECT = '1' or HRAM_READ_EN = '1' else  -- RAM
          K when KEYBOARD_SELECT = '1' else  -- Keyboard
          SF_D & K(6 downto 0) when READ_KEY = '1' else -- ][e softswitches
          GAMEPORT(TO_INTEGER(A(2 downto 0))) & VIDEO_DL_LATCH(6 downto 0)  -- Gameport
             when GAMEPORT_SELECT = '1' else
          rom_out when ROM_SELECT = '1' else  -- ROMs
          VIDEO_DL_LATCH(7 downto 0) when TAPE_OUT = '1' or SPEAKER_SELECT = '1' or STB = '1' or
                        SOFTSWITCH_SELECT = '1' or PDL_STROBE = '1' or
                        HRAM_CONTROL = '1' or A = x"CFFF" else  -- Floating bus
          PD;                           -- Peripherals

  timing : entity work.timing_generator port map (
    CLK_14M        => CLK_14M,
    CE_14M         => CE_14M,
    PALMODE        => PALMODE,
    VID7M          => CLK_7M,
    CAS_N          => CAS_N,
    RAS_N          => RAS_N,
    Q3	           => Q3,
    Q3_RISE        => Q3_RISE,
    AX             => AX,
    PHI0           => PHASE_ZERO,
    PHI0_EN_R      => PHASE_ZERO_R,
    PHI0_EN_F      => PHASE_ZERO_F,
    COLOR_REF      => COLOR_REF,
    TEXT_MODE      => TEXT_MODE,
    PAGE2          => PAGE2,
    HIRES_MODE     => HIRES_MODE,
    MIXED_MODE     => MIXED_MODE,
    COL80          => COL80,
    STORE80        => STORE80,
    DHIRES_MODE    => DHIRES_MODE,
    VID7           => VIDEO_DL(7),
    VIDEO_ADDRESS  => VIDEO_ADDRESS,
    SEGA           => SEGA,
    SEGB           => SEGB,
    SEGC           => SEGC,
    GR1            => COLOR_LINE,
    GR2            => GR2,
    VBLANK         => VBL,
    HBLANK         => HBL,
    WNDW_N         => WNDW_N,
    LDPS_N         => LDPS_N);

  video_display : entity work.video_generator port map (
    CLK_14M    => CLK_14M,
    CE_14M     => CE_14M,
    CLK_7M     => CLK_7M,
    GR2        => GR2,
    SEGA       => SEGA,
    SEGB       => SEGB,
    SEGC       => SEGC,
    ALTCHAR    => ALTCHAR,
    ROMSWITCH  => ROMSWITCH,
    WNDW_N     => WNDW_N,
    DL         => VIDEO_DL,
    LDPS_N     => LDPS_N,
    FLASH_CLK  => FLASH_CLK,
    rom_addr   => video_rom_addr,
    rom_out    => video_rom_out,
    VIDEO      => VIDEO);

  we <= not R65C02_WE_N;
  A <= R65C02_A;
  D_OUT <= R65C02_DO;

  CPU_EN <= '1' when PHASE_ZERO_D = '1' and PHASE_ZERO = '0' else '0';

  cpu_enable: process (CLK_14M)
  begin
    if rising_edge(CLK_14M) and CE_14M = '1' then
      PHASE_ZERO_D <= PHASE_ZERO;
    end if;
  end process cpu_enable;

  CPU_STEP <= CPU_EN and CE_14M and (not CPU_WAIT);

  -- one clock of CLK_14M per enable cycle: the CPU steps once per CPU_EN
  cpu65c02: entity work.R65C02
    port map (
        reset => not reset,
        clk => CLK_14M,
        enable => CPU_EN and CE_14M and (not CPU_WAIT),
        nmi_n => NMI_N,
        irq_n => IRQ_N,
        di => D_IN,
        do => R65C02_DO,
        addr => R65C02_A,
        nwe => R65C02_WE_N,
        sync => open,
        sync_irq => open,
        Regs => open
    );

  FLOAT_BUS <= VIDEO_DL_LATCH(7 downto 0);
  CPU_RAM_SEL <= RAM_SELECT or HRAM_READ_EN;
  CPU_ADDR_RAM <= CPU_RAM_ADDR;
  VIDEO_ADDR <= VIDEO_ADDRESS;
  CPU_RAM_WR <= (we and RAM_SELECT) or (we and HRAM_WRITE_EN);
  COL80_O <= COL80;
  ALTCHAR_O <= ALTCHAR;
  HIRES_O <= HIRES_MODE;
  PAGE2_O <= PAGE2;
  MIXED_O <= MIXED_MODE;
  STORE80_O <= STORE80;
  DHIRES_O <= DHIRES_MODE;

end rtl;
