-- SPDX-License-Identifier: GPL-3.0-or-later
-- T88 (Q8): the sound chip block of the Atari 800 XL as the original has it: ONE mono POKEY. Watson's
-- atari800core.vhd instantiates `pokeymax` (the PokeyMAX emulation: up to four POKEYs, two SIDs, two PSGs, a
-- sample engine, configuration registers) unconditionally; that is out by Q8, so this entity of the same name and
-- the same ports takes its place and atari800core.vhd stays untouched. What is left of it:
--   * pokey.vhdl (Watson, unchanged) at every address $D200-$D2FF (its 16 registers repeat, as on the machine),
--   * the covox mirror ($D600-$D603, the four 8-bit latches of covox.vhd, mixed in at about a quarter of the POKEY),
--   * the mixer of pokey_mixer_simple.vhdl (Watson: the PokeyMAX volume table and the output low-pass filter),
--   * the console speaker (GTIA_SOUND) as a small square wave on top.
-- No second POKEY, SID, PSG or sample RAM: SAMPLE_RAM_*/SID_ROM_* requests stay low, SIO_SOUND (tape audio) is not
-- mixed. DRIVE_DATA_OUT is always '1': POKEY drives the bus on every read of $D2xx (address_decoder.vhdl).
-- AUDIO_L/R are signed 16 bit, the same sample on both (mono).
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity pokeymax is
  port (
    CE28                    : in  std_logic := '1';
    CLK                     : in  std_logic;
    RESET_N                 : in  std_logic;
    ENABLE_179              : in  std_logic;
    ENABLE_179_DOUBLE       : in  std_logic;
    ADDR                    : in  std_logic_vector(7 downto 0);
    DATA_IN                 : in  std_logic_vector(7 downto 0);
    REQUEST                 : in  std_logic;
    WR_EN                   : in  std_logic;
    D6_WR_EN                : in  std_logic;
    DATA_OUT                : out std_logic_vector(7 downto 0);
    DRIVE_DATA_OUT          : out std_logic;
    keyboard_scan           : out std_logic_vector(5 downto 0);
    keyboard_response       : in  std_logic_vector(1 downto 0);
    POT_IN                  : in  std_logic_vector(7 downto 0);
    SIO_IN1                 : in  std_logic;
    SIO_OUT1                : out std_logic;
    SIO_CLOCKIN_IN          : in  std_logic;
    SIO_CLOCKIN_OUT         : out std_logic;
    SIO_CLOCKIN_OE          : out std_logic;
    SIO_CLOCKOUT            : out std_logic;
    IRQ_N_OUT               : out std_logic;
    INIT_CONFIG             : in  std_logic_vector(38 downto 0);
    GTIA_SOUND              : in  std_logic;
    SIO_SOUND               : in  std_logic_vector(7 downto 0);
    AUDIO_L                 : out std_logic_vector(15 downto 0);
    AUDIO_R                 : out std_logic_vector(15 downto 0);
    POT_RESET               : out std_logic;
    SAMPLE_RAM_ADDRESS      : out std_logic_vector(15 downto 0);
    SAMPLE_RAM_WRITE_ENABLE : out std_logic;
    SAMPLE_RAM_REQUEST      : out std_logic;
    SAMPLE_RAM_READY        : in  std_logic;
    SAMPLE_RAM_READ_DATA    : in  std_logic_vector(7 downto 0);
    SAMPLE_RAM_WRITE_DATA   : out std_logic_vector(7 downto 0);
    SID_ROM_ADDRESS         : out std_logic_vector(16 downto 0);
    SID_ROM_REQUEST         : out std_logic;
    SID_ROM_READY           : in  std_logic;
    SID_ROM_READ_DATA       : in  std_logic_vector(31 downto 0));
end entity;

architecture trx of pokeymax is
  signal pokey_we   : std_logic;
  signal ch0, ch1, ch2, ch3 : std_logic_vector(3 downto 0);
  signal vol_l, vol_r : std_logic_vector(15 downto 0);
  signal cov0, cov1, cov2, cov3 : std_logic_vector(7 downto 0);
  signal cov_sum    : unsigned(9 downto 0);
  signal mix        : signed(15 downto 0);
begin
  pokey_we <= REQUEST and WR_EN;

  pokey1 : entity work.pokey
    port map (CE28 => CE28, CLK => CLK, ENABLE_179 => ENABLE_179, WR_EN => pokey_we, RESET_N => RESET_N,
              SIO_IN1 => SIO_IN1, SIO_CLOCKIN_IN => SIO_CLOCKIN_IN, SIO_CLOCKIN_OUT => SIO_CLOCKIN_OUT,
              SIO_CLOCKIN_OE => SIO_CLOCKIN_OE, ADDR => ADDR(3 downto 0), DATA_IN => DATA_IN,
              keyboard_response => keyboard_response, POT_IN => POT_IN, IRQ_N_OUT => IRQ_N_OUT,
              SIO_OUT1 => SIO_OUT1, SIO_OUT2 => open, SIO_OUT3 => open, SIO_CLOCKOUT => SIO_CLOCKOUT,
              POT_RESET => POT_RESET, CHANNEL_0_OUT => ch0, CHANNEL_1_OUT => ch1, CHANNEL_2_OUT => ch2,
              CHANNEL_3_OUT => ch3, DATA_OUT => DATA_OUT, keyboard_scan => keyboard_scan);
  DRIVE_DATA_OUT <= '1';

  covox1 : entity work.covox
    port map (CE28 => CE28, CLK => CLK, RESET_N => RESET_N, ADDR => ADDR(1 downto 0), DATA_IN => DATA_IN,
              WR_EN => D6_WR_EN, covox_channel0 => cov0, covox_channel1 => cov1, covox_channel2 => cov2,
              covox_channel3 => cov3);

  mixer : entity work.pokey_mixer_simple
    port map (CE28 => CE28, CLK => CLK, ENABLE_179 => ENABLE_179, CHANNEL_0 => ch0, CHANNEL_1 => ch1,
              CHANNEL_2 => ch2, CHANNEL_3 => ch3, VOLUME_OUT_L => vol_l, VOLUME_OUT_R => vol_r);

  -- POKEY +-16384, covox up to +8160, the console speaker +2048: below the 16 bit limit
  cov_sum <= resize(unsigned(cov0), 10) + unsigned(cov1) + unsigned(cov2) + unsigned(cov3);
  mix <= signed(vol_l) + signed(resize(cov_sum & "000", 16)) + to_signed(2048, 16) when GTIA_SOUND = '1'
         else signed(vol_l) + signed(resize(cov_sum & "000", 16));
  AUDIO_L <= std_logic_vector(mix);
  AUDIO_R <= std_logic_vector(mix);

  SAMPLE_RAM_ADDRESS      <= (others => '0');
  SAMPLE_RAM_WRITE_ENABLE <= '0';
  SAMPLE_RAM_REQUEST      <= '0';
  SAMPLE_RAM_WRITE_DATA   <= (others => '0');
  SID_ROM_ADDRESS         <= (others => '0');
  SID_ROM_REQUEST         <= '0';
end architecture;
