-- SPDX-License-Identifier: GPL-3.0-or-later
-- T88 D6: the POKEY's mix (16 bit signed, mono, updated at the machine's rate) into 24 bit samples of the host's audio clock:
-- averaged over 656 sys_clk (48.06 kHz), the DC level taken out by a slow tracker (so a silent POKEY is exactly 0 and the
-- host's mixer sees no step), scaled to the 24 bit range and handed to mclk by a toggle (stable for 656 clocks, mclk reads
-- it two clocks after the toggle). The pause mutes: the next samples are exactly 0 and the tracker stands. Mono.
-- The same circuit as c264_audio.v, written for this project (TREX), no upstream code.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity a8_audio is
  port (
    clk     : in  std_logic;
    a_in    : in  signed(15 downto 0);
    mute    : in  std_logic;
    mclk    : in  std_logic;
    audio_l : out signed(23 downto 0);
    audio_r : out signed(23 downto 0));
end entity;

architecture rtl of a8_audio is
  signal acc  : signed(27 downto 0) := (others => '0');          -- 656 x +-32768 = +-21.5 M
  signal cnt  : unsigned(9 downto 0) := (others => '0');
  signal dc   : signed(27 downto 0) := (others => '0');          -- the average the tracker follows (same scale as acc)
  signal smp  : signed(23 downto 0) := (others => '0');
  signal tgl  : std_logic := '0';
  signal t_s  : std_logic_vector(2 downto 0) := "000";
  attribute ASYNC_REG : string;
  attribute ASYNC_REG of t_s : signal is "TRUE";
  signal al, ar : signed(23 downto 0) := (others => '0');
  signal acc_n, diff : signed(27 downto 0);
  signal scaled, s8  : signed(35 downto 0);
  signal sat  : signed(23 downto 0);
begin
  acc_n  <= acc + resize(a_in, 28);
  diff   <= acc_n - dc;
  -- sample = (acc - dc) * 100 / 256, i.e. (avg - dc/656) * 256: full swing +-32768 -> +-8.4 M (24 bit)
  scaled <= shift_left(resize(diff, 36), 6) + shift_left(resize(diff, 36), 5) + shift_left(resize(diff, 36), 2);
  s8     <= shift_right(scaled, 8);
  sat    <= to_signed(8388607, 24)  when s8 > to_signed(8388607, 36) else
            to_signed(-8388608, 24) when s8 < to_signed(-8388608, 36) else
            s8(23 downto 0);

  process (clk)
  begin
    if rising_edge(clk) then
      if cnt = 655 then
        cnt <= (others => '0');
        acc <= (others => '0');
        if mute = '1' then
          smp <= (others => '0');
        else
          smp <= sat;
          dc  <= dc + shift_right(diff, 8);
        end if;
        tgl <= not tgl;
      else
        cnt <= cnt + 1;
        acc <= acc_n;
      end if;
    end if;
  end process;

  process (mclk)
  begin
    if rising_edge(mclk) then
      t_s <= t_s(1 downto 0) & tgl;
      if t_s(2) /= t_s(1) then al <= smp; ar <= smp; end if;
    end if;
  end process;
  audio_l <= al;
  audio_r <= ar;
end architecture;
