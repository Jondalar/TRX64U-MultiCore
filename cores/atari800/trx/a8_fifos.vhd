-- SPDX-License-Identifier: GPL-3.0-or-later
-- T88 (spec 5.6 b): the two FIFOs of the SIO handler, replacing Watson's Quartus scfifo wrappers
-- (rtl/sioemu/fifo_receive.vhd, fifo_transmit.vhd; altera_mf, no licence text) with entities of the same
-- name and ports: 256 words, show-ahead (q is the head word whenever empty = '0'), full / empty flags,
-- usedw = the word count modulo 256. A write into a full FIFO and a read from an empty one are ignored
-- (overflow_checking / underflow_checking "ON"). Every flip-flop takes the core's clock enable CE28 (D3).
-- The storage is a plain array (LUT RAM): the read is asynchronous, which is the show-ahead.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity a8_sfifo is
  generic (W : positive := 8);
  port (
    CE28  : in  std_logic := '1';
    clock : in  std_logic;
    data  : in  std_logic_vector(W - 1 downto 0);
    rdreq : in  std_logic;
    wrreq : in  std_logic;
    empty : out std_logic;
    full  : out std_logic;
    q     : out std_logic_vector(W - 1 downto 0);
    usedw : out std_logic_vector(7 downto 0));
end entity;

architecture rtl of a8_sfifo is
  type ram_t is array (0 to 255) of std_logic_vector(W - 1 downto 0);
  signal ram    : ram_t := (others => (others => '0'));
  signal rptr   : unsigned(7 downto 0) := (others => '0');
  signal wptr   : unsigned(7 downto 0) := (others => '0');
  signal cnt    : unsigned(8 downto 0) := (others => '0');
  signal is_e   : std_logic;
  signal is_f   : std_logic;
begin
  is_e  <= '1' when cnt = 0 else '0';
  is_f  <= '1' when cnt = 256 else '0';
  empty <= is_e;
  full  <= is_f;
  q     <= ram(to_integer(rptr));
  usedw <= std_logic_vector(cnt(7 downto 0));

  process (clock)
    variable do_w, do_r : boolean;
  begin
    if rising_edge(clock) and CE28 = '1' then
      do_w := wrreq = '1' and is_f = '0';
      do_r := rdreq = '1' and is_e = '0';
      if do_w then
        ram(to_integer(wptr)) <= data;
        wptr <= wptr + 1;
      end if;
      if do_r then rptr <= rptr + 1; end if;
      if do_w and not do_r then cnt <= cnt + 1;
      elsif do_r and not do_w then cnt <= cnt - 1;
      end if;
    end if;
  end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity fifo_receive is
  port (
    CE28  : in  std_logic := '1';
    clock : in  std_logic;
    data  : in  std_logic_vector(14 downto 0);
    rdreq : in  std_logic;
    wrreq : in  std_logic;
    empty : out std_logic;
    full  : out std_logic;
    q     : out std_logic_vector(14 downto 0);
    usedw : out std_logic_vector(7 downto 0));
end entity;

architecture trx of fifo_receive is
begin
  f : entity work.a8_sfifo generic map (W => 15)
    port map (CE28 => CE28, clock => clock, data => data, rdreq => rdreq, wrreq => wrreq,
              empty => empty, full => full, q => q, usedw => usedw);
end architecture;

library ieee;
use ieee.std_logic_1164.all;

entity fifo_transmit is
  port (
    CE28  : in  std_logic := '1';
    clock : in  std_logic;
    data  : in  std_logic_vector(7 downto 0);
    rdreq : in  std_logic;
    wrreq : in  std_logic;
    empty : out std_logic;
    full  : out std_logic;
    q     : out std_logic_vector(7 downto 0);
    usedw : out std_logic_vector(7 downto 0));
end entity;

architecture trx of fifo_transmit is
begin
  f : entity work.a8_sfifo generic map (W => 8)
    port map (CE28 => CE28, clock => clock, data => data, rdreq => rdreq, wrreq => wrreq,
              empty => empty, full => full, q => q, usedw => usedw);
end architecture;
