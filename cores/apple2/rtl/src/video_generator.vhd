-------------------------------------------------------------------------------
-- TRX64U MultiCore T89: signals start at 0 (a Xilinx flip-flop does; the simulator's 'U' never leaves a toggle flip-flop).
-- Changed from NanoApple2 src/video_generator.vhd (see ../NOTICE): enable
-- CE_14M on every clocked process; the character ROM is outside (rom_addr/rom_out) instead of
-- the Gowin dual-port block with its download port.
-------------------------------------------------------------------------------
--
-- Apple II/e Video Generation Logic
--
-- György Szombathelyi
--
-- Original Apple II+ Video Generation Logic by
-- Stephen A. Edwards, sedwards@cs.columbia.edu
--
-- This takes data from memory and various mode switches to produce the
-- lookup address in the video ROM, and the result is fed to the video shift
-- register.
--
-- Based on the book Understanding the Apple IIe by Jim Sather
--
-------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity video_generator is

  port (
    CLK_14M    : in std_logic;              -- the host's sys_clk
    CE_14M     : in std_logic;              -- 14.31818 MHz master clock enable
    CLK_7M     : in std_logic;
    ALTCHAR    : in std_logic;
    ROMSWITCH  : in std_logic;
    GR2        : in std_logic;
    SEGA       : in std_logic;
    SEGB       : in std_logic;
    SEGC       : in std_logic;
    WNDW_N     : in std_logic;
    DL         : in unsigned(7 downto 0);  -- Data from RAM
    LDPS_N     : in std_logic;

    -- the character ROM, outside, synchronous
    rom_addr   : out std_logic_vector(12 downto 0);
    rom_out    : in  unsigned(7 downto 0);

    FLASH_CLK  : in std_logic;            -- Low-frequency flashing text clock
    VIDEO      : out std_logic
    );

end video_generator;

architecture rtl of video_generator is

  -- IIe signals
  signal video_rom_addr : unsigned(11 downto 0) := (others => '0');
  signal video_rom_out : unsigned(7 downto 0) := (others => '0');
  signal video_shiftreg : unsigned(7 downto 0) := (others => '0');

  
  signal video_rom_input_addr: std_logic_vector(12 downto 0) := (others => '0');
begin

  -----------------------------------------------------------------------------
  --
  -- Apple II/e Video generator circuit
  --
  -- Chapter 8 of Understanding the Apple II by Jim Sather
  --
  -----------------------------------------------------------------------------

  video_rom_addr <= GR2 &
                    (DL(7) or (not GR2 and DL(6) and FLASH_CLK and not ALTCHAR)) &
                    (DL(6) and (ALTCHAR or GR2 or DL(7))) &
                    DL(5 downto 0) & SEGC & SEGB & SEGA;

  video_rom_input_addr <= std_logic_vector(ROMSWITCH & video_rom_addr);

  rom_addr <= video_rom_input_addr;
  video_rom_out <= rom_out;

  LS166 : process (CLK_14M)
  begin
    if rising_edge(CLK_14M) and CE_14M = '1' then
      if CLK_7M = '0' then
        if LDPS_N = '0' then        -- load
          if WNDW_N = '1' then
            video_shiftreg <= (others => '1');
          else
            video_shiftreg <= video_rom_out;
          end if;
        else                        -- shift
          video_shiftreg <= video_shiftreg(0) & video_shiftreg(7 downto 1);
        end if;
      end if;
    end if;
  end process;

  VIDEO <= not video_shiftreg(0);

end rtl;
