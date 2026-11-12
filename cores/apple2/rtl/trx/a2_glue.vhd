-- TRX Apple //e core (T89): the glue around the Apple //e machine (cores/apple2/rtl/src), written for this
-- project (TREX), no upstream code. Everything here is in sys_clk (the host's C64 clock, 32.7273 MHz in its
-- NTSC mode) unless noted; `ce` is the machine's 14.318 MHz enable.
--   a2_nco     the 14M enable: 7 of 16 sys_clk
--   a2_keys    the C64 keyboard matrix into the Apple //e keyboard (key code, strobe, any-key-down, buttons)
--   a2_game    the game port: four 558 one-shot timers, the buttons
--   a2_audio   the speaker toggle into 24 bit samples in mclk
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

----------------------------------------------------------------------------------------------
-- Clock enable. The Apple's 14.31818 MHz is exactly 7/16 of the host's NTSC sys_clk (32.7273 MHz,
-- T40; 14.3182 MHz * 16 / 7 = 32.7273 MHz): add 7 to a 4 bit accumulator, fire on the carry.
-- Two or three sys_clk between enables. `run` = 0 holds the accumulator too (Freeze, a wait for
-- the DDR2), so the enables resume in the same rhythm. en is a register: one flip-flop net.
----------------------------------------------------------------------------------------------
entity a2_nco is
  port (
    clk : in  std_logic;
    rst : in  std_logic;
    run : in  std_logic;
    en  : out std_logic
  );
end entity;

architecture rtl of a2_nco is
  signal acc : unsigned(3 downto 0) := (others => '0');
  signal q   : std_logic := '0';
begin
  process (clk)
    variable n : unsigned(4 downto 0);
  begin
    if rising_edge(clk) then
      q <= '0';
      if rst = '1' then
        acc <= (others => '0');
      elsif run = '1' then
        n := ('0' & acc) + 7;
        acc <= n(3 downto 0);
        q   <= n(4);
      end if;
    end if;
  end process;
  en <= q;
end architecture;

----------------------------------------------------------------------------------------------
-- Keyboard. keys[col*8 + row] is the C64 matrix (CIA 1 port A = column, port B = row), 1 = down.
--     col\row   0     1     2     3     4     5     6     7
--       0      DEL  RETURN  CRSR>  F7    F1    F3    F5   CRSRv
--       1       3     W      A     4     Z     S     E   LSHIFT
--       2       5     R      D     6     C     F     T     X
--       3       7     Y      G     8     B     H     U     V
--       4       9     I      J     0     M     K     O     N
--       5       +     P      L     -     .     :     @     ,
--       6      GBP    *      ;    HOME  RSHIFT =    UP-ARR  /
--       7       1   LEFT-ARR CTRL  2   SPACE   C=     Q    STOP
-- The Apple //e key is the ASCII byte of the symbol on the C64 keycap (VICE's symbolic mapping): the key that
-- prints `"` on the C64 gives `"` (SHIFT+2). The C64's PETSCII sits on the ASCII positions of [ \ ] ^ _ (the keys
-- GBP = \, UP-ARROW = ^, LEFT-ARROW = _). What the C64 has not got is on the shifted keys: see `shifted`.
--   STOP = ESC, DEL = DELETE ($7F), CRSR right/down = right/down arrow, with SHIFT left/up arrow,
--   CTRL clears bits 6:5 of $40-$7F (CTRL-I is TAB), CAPS LOCK is the app's: register 0A2 (the C64U's SHIFT LOCK
--   with lock_off set holds no SHIFT; letters are capitals while it is 1, as the //e's key does),
--   C= is Open Apple (the button PB0), HOME is Closed Apple (PB1).
-- Auto repeat in 1 MHz cycles of the machine: 534,000 to the first repeat, then every 91,000 (the encoder
-- of the //e: half a second, ten a second), so it stops with the machine when it is frozen.
----------------------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity a2_keys is
  generic (
    REPEAT_DELAY : integer := 534000;
    REPEAT_RATE  : integer := 91000
  );
  port (
    clk      : in  std_logic;
    rst      : in  std_logic;
    ce       : in  std_logic;
    cyc      : in  std_logic;                       -- one machine cycle (1 MHz) = PHASE_ZERO_R
    keys     : in  std_logic_vector(63 downto 0);
    keystrobe : in std_logic;                       -- the machine read $C010
    inj_we   : in  std_logic;                       -- the app types: a 7 bit code into the queue
    inj_data : in  std_logic_vector(6 downto 0);
    k        : out std_logic_vector(7 downto 0);    -- bit 7: strobe
    akd      : out std_logic;
    open_apple   : out std_logic;
    closed_apple : out std_logic;
    caps_in  : in  std_logic;                       -- CAPS LOCK from the app (register 0A2; sys_clk side)
    caps     : out std_logic;
    queued   : out std_logic_vector(4 downto 0)
  );
end entity;

architecture rtl of a2_keys is
  type tab_t is array (0 to 63) of std_logic_vector(7 downto 0);
  function c (ch : character) return std_logic_vector is
  begin
    return std_logic_vector(to_unsigned(character'pos(ch), 8));
  end function;
  constant NO   : std_logic_vector(7 downto 0) := x"00";
  -- index = col*8 + row; 0 = no code; letters as capitals
  constant plain : tab_t := (
    x"7F", x"0D", x"15", NO, NO, NO, NO, x"0A",
    c('3'), c('W'), c('A'), c('4'), c('Z'), c('S'), c('E'), NO,
    c('5'), c('R'), c('D'), c('6'), c('C'), c('F'), c('T'), c('X'),
    c('7'), c('Y'), c('G'), c('8'), c('B'), c('H'), c('U'), c('V'),
    c('9'), c('I'), c('J'), c('0'), c('M'), c('K'), c('O'), c('N'),
    c('+'), c('P'), c('L'), c('-'), c('.'), c(':'), c('@'), c(','),
    c('\'), c('*'), c(';'), NO, NO, c('='), c('^'), c('/'),
    c('1'), c('_'), NO, c('2'), c(' '), NO, c('Q'), x"1B");
  constant shifted : tab_t := (
    x"7F", x"0D", x"08", NO, NO, NO, NO, x"0B",
    c('#'), c('W'), c('A'), c('$'), c('Z'), c('S'), c('E'), NO,
    c('%'), c('R'), c('D'), c('&'), c('C'), c('F'), c('T'), c('X'),
    c('''), c('Y'), c('G'), c('('), c('B'), c('H'), c('U'), c('V'),
    c(')'), c('I'), c('J'), c('0'), c('M'), c('K'), c('O'), c('N'),
    c('{'), c('P'), c('L'), c('}'), c('>'), c('['), c('`'), c('<'),
    c('|'), c('*'), c(']'), NO, NO, c('='), c('~'), c('?'),
    c('!'), c('_'), NO, c('"'), c(' '), NO, c('Q'), x"1B");

  signal shift, ctrl, anykey : std_logic;
  signal code    : std_logic_vector(7 downto 0);     -- the code of the first key down, 0 = none
  signal ccode   : std_logic_vector(7 downto 0);     -- with CTRL / lower case applied
  signal last    : std_logic_vector(7 downto 0) := (others => '0');
  signal rcnt    : integer range 0 to 1048575 := 0;
  signal rrate   : std_logic := '0';
  signal k_r     : std_logic_vector(7 downto 0) := (others => '0');
  type fifo_t is array (0 to 15) of std_logic_vector(6 downto 0);
  signal fifo    : fifo_t := (others => (others => '0'));
  signal wp, rp  : unsigned(4 downto 0) := (others => '0');
begin
  shift <= keys(1*8+7) or keys(6*8+4);
  ctrl  <= keys(7*8+2);
  open_apple   <= keys(7*8+5);
  closed_apple <= keys(6*8+3);
  caps <= caps_in;
  queued <= std_logic_vector(wp - rp);

  process (keys, shift)
    variable cd : std_logic_vector(7 downto 0);
  begin
    cd := NO;
    anykey <= '0';
    for i in 63 downto 0 loop
      if keys(i) = '1' then
        if shift = '1' then
          if shifted(i) /= NO then cd := shifted(i); end if;
        else
          if plain(i) /= NO then cd := plain(i); end if;
        end if;
      end if;
    end loop;
    code <= cd;
  end process;

  process (code, ctrl, caps_in, shift)
    variable cd : unsigned(7 downto 0);
  begin
    cd := unsigned(code);
    if ctrl = '1' and cd >= x"40" and cd <= x"7F" then
      cd := cd and x"1F";
    elsif caps_in = '0' and shift = '0' and cd >= x"41" and cd <= x"5A" then
      cd := cd or x"20";
    end if;
    ccode <= std_logic_vector(cd);
  end process;

  akd <= '1' when code /= NO else '0';

  process (clk)
    variable emit : boolean;
    variable ec   : std_logic_vector(7 downto 0);
  begin
    if rising_edge(clk) then
      if rst = '1' then
        k_r <= (others => '0'); last <= (others => '0'); rcnt <= 0; rrate <= '0';
        wp <= (others => '0'); rp <= (others => '0');
      else
        -- the app's queue is fed in sys_clk, whatever the machine does
        if inj_we = '1' and (wp - rp) /= 16 then
          fifo(to_integer(wp(3 downto 0))) <= inj_data;
          wp <= wp + 1;
        end if;
        if ce = '1' then
          emit := false;
          ec := ccode;
          if keystrobe = '1' then k_r(7) <= '0'; end if;
          if code = NO then
            last <= (others => '0'); rcnt <= 0; rrate <= '0';
          elsif code /= last then
            last <= code; rcnt <= 0; rrate <= '0';
            emit := true;
          elsif cyc = '1' then
            if (rrate = '0' and rcnt = REPEAT_DELAY) or (rrate = '1' and rcnt = REPEAT_RATE) then
              rcnt <= 0; rrate <= '1';
              emit := true;
            else
              rcnt <= rcnt + 1;
            end if;
          end if;
          if emit then
            k_r <= '1' & ec(6 downto 0);
          elsif wp /= rp and k_r(7) = '0' and keystrobe = '0' then
            k_r <= '1' & fifo(to_integer(rp(3 downto 0)));
            rp <= rp + 1;
          end if;
        end if;
      end if;
    end if;
  end process;
  k <= k_r;
end architecture;

----------------------------------------------------------------------------------------------
-- Game port. The //e reads the one-shot timers (a 558 behind the paddle) at $C064-$C067 bit 7; $C070
-- fires all four. A paddle of value v (0..255) keeps its timer high for 11 * v machine cycles, which is
-- what the monitor's PREAD routine (an 11 cycle loop) counts out as PDL(n). A digital stick gives 0 (left /
-- up), 127 (centred) and 255 (right / down). Port 1 is paddles 0 (x) and 1 (y), port 2 is 2 and 3;
-- the buttons PB0/PB1 ($C061/$C062) are fire 1 / fire 2 or Open / Closed Apple (wired together as on the
-- machine, where the buttons share the pin).
-- gameport(i) is the bit read at $C060 + i, the order apple2.vhd takes it in.
----------------------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity a2_game is
  port (
    clk    : in  std_logic;
    rst    : in  std_logic;
    ce     : in  std_logic;
    cyc    : in  std_logic;
    strobe : in  std_logic;                         -- PDL_STROBE: $C07x is addressed
    joy1   : in  std_logic_vector(4 downto 0);      -- 0 up, 1 down, 2 left, 3 right, 4 fire
    joy2   : in  std_logic_vector(4 downto 0);
    swap   : in  std_logic;                         -- the app swaps the ports
    open_apple   : in std_logic;
    closed_apple : in std_logic;
    gameport : out std_logic_vector(7 downto 0)
  );
end entity;

architecture rtl of a2_game is
  type cnt_t is array (0 to 3) of unsigned(11 downto 0);
  signal cnt : cnt_t := (others => (others => '0'));
  signal bit_r : std_logic_vector(3 downto 0) := (others => '0');
  signal strobe_d : std_logic := '0';
  signal ja, jb : std_logic_vector(4 downto 0);
  type val_t is array (0 to 3) of unsigned(7 downto 0);
  signal val : val_t;
begin
  ja <= joy2 when swap = '1' else joy1;
  jb <= joy1 when swap = '1' else joy2;
  val(0) <= x"00" when ja(2) = '1' else x"FF" when ja(3) = '1' else x"7F";
  val(1) <= x"00" when ja(0) = '1' else x"FF" when ja(1) = '1' else x"7F";
  val(2) <= x"00" when jb(2) = '1' else x"FF" when jb(3) = '1' else x"7F";
  val(3) <= x"00" when jb(0) = '1' else x"FF" when jb(1) = '1' else x"7F";

  process (clk)
    variable v : unsigned(11 downto 0);
  begin
    if rising_edge(clk) then
      if rst = '1' then
        bit_r <= (others => '0'); strobe_d <= '0';
        cnt <= (others => (others => '0'));
      elsif ce = '1' then
        strobe_d <= strobe;
        for i in 0 to 3 loop
          if strobe = '1' and strobe_d = '0' then
            v := resize(val(i) & "000", 12) + resize(val(i) & "0", 12) + resize(val(i), 12);   -- 8 v + 2 v + v
            cnt(i) <= v;
            if v /= 0 then bit_r(i) <= '1'; else bit_r(i) <= '0'; end if;
          elsif cyc = '1' and bit_r(i) = '1' then
            if cnt(i) = 1 or cnt(i) = 0 then
              bit_r(i) <= '0'; cnt(i) <= (others => '0');
            else
              cnt(i) <= cnt(i) - 1;
            end if;
          end if;
        end loop;
      end if;
    end if;
  end process;

  gameport <= bit_r(3) & bit_r(2) & bit_r(1) & bit_r(0) & '0' &
              (jb(4) or closed_apple) & (ja(4) or open_apple) & '0';
end architecture;

----------------------------------------------------------------------------------------------
-- Audio: the speaker (1 bit, toggled by $C030) averaged over 682 sys_clk (47.99 kHz at 32.7273 MHz), centred,
-- smoothed by a first order low pass (y += 3/4 (x - y), about 10.6 kHz), scaled to a signed 24 bit sample and
-- handed to mclk by a toggle: the sample is stable for 682 clocks, mclk reads it two clocks after the toggle.
-- `mute` (Freeze) forces the next samples to exactly 0.
----------------------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity a2_audio is
  port (
    clk     : in  std_logic;
    spk     : in  std_logic;
    mute    : in  std_logic;
    mclk    : in  std_logic;
    audio_l : out signed(23 downto 0);
    audio_r : out signed(23 downto 0)
  );
end entity;

architecture rtl of a2_audio is
  constant WIN : integer := 682;
  signal acc  : unsigned(9 downto 0) := (others => '0');
  signal cnt  : integer range 0 to WIN-1 := 0;
  signal y    : signed(23 downto 0) := (others => '0');
  signal tgl  : std_logic := '0';
  signal t_s  : std_logic_vector(2 downto 0) := (others => '0');
  signal smp  : signed(23 downto 0) := (others => '0');
  attribute ASYNC_REG : string;
  attribute ASYNC_REG of t_s : signal is "TRUE";
begin
  process (clk)
    variable x : signed(23 downto 0);
    variable s : unsigned(9 downto 0);
    variable ny : signed(23 downto 0);
  begin
    if rising_edge(clk) then
      s := acc;
      if spk = '1' then s := acc + 1; end if;
      if cnt = WIN-1 then
        cnt <= 0;
        acc <= (others => '0');
        -- s is 0..682: x = (s - 341) * 4096 (+-1.4 M of +-8.4 M full scale)
        x := shift_left(resize(signed(resize(s, 11)) - to_signed(WIN / 2, 11), 24), 12);
        if mute = '1' then
          y <= (others => '0');
        else
          ny := y + shift_right(x - y, 1) + shift_right(x - y, 2);
          y <= ny;
        end if;
        tgl <= not tgl;
      else
        cnt <= cnt + 1;
        acc <= s;
      end if;
    end if;
  end process;

  process (mclk)
  begin
    if rising_edge(mclk) then
      t_s <= t_s(1 downto 0) & tgl;
      if t_s(2) /= t_s(1) then
        smp <= y;
      end if;
    end if;
  end process;
  audio_l <= smp;
  audio_r <= smp;
end architecture;
