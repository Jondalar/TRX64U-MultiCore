-- TRX Apple //e core (T89 D8, T89.1 D5): the track window of one Disk II drive. The drive (drive_ii.vhd) plays the bits of a track in a
-- buffer; this block says which QUARTER track the buffer has to hold and tells the app (which serves the image file) when to put one in or
-- take one out. Written for this project (TREX), no upstream code; the triggers are those of NanoApple2's floppy_track.sv (the track
-- changed, the buffer is dirty and the motor stopped), the transport is a request / acknowledge pair of registers instead of the SD
-- card's sector protocol.
--   mounted    the app has an image in the drive (a level)
--   change     one clock: another image (the buffer is invalid, a dirty track is dropped: the app flushed first)
--   flush      one clock: write a dirty track back now (before an unmount or a Freeze exit)
--   track      the head's quarter track (drive_ii TRACK, 0..139, a register of the machine)
--   active     the drive's motor is on (D*_ACTIVE)
--   wr         one clock: the drive wrote a bit into the buffer
--   req        a request is open; req_save = 1: take the buffer's track req_track out, 0: put req_track in
--   ack        one clock: the app is done with the open request; bits_in / flags_in (levels the app wrote before) describe the track it put in
--   ready      to the drive: the buffer holds the head's quarter track (the track plays)
--   busy       to the drive: a request is open (no write goes into the buffer)
--   bit_count  of the track in the buffer; noise: it is unformatted (random flux)
--   lat_max, reqs  the longest a request stayed open (sys_clk clocks) and how many were opened (the window's counters, T89.1 D6)
-- The head must rest SETTLE sys_clk clocks on a quarter track before the buffer is asked to follow (a seek of many tracks passes many).
-- Quarter tracks are the stepper's own count: whole track N is at 4N + 2 (magnet 0 holds the head at 2, drive_ii.vhd's table), a half track at 4N + 4.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity a2_trk is
  generic (
    SETTLE    : integer := 32768                       -- 1 ms of sys_clk
  );
  port (
    clk       : in  std_logic;
    rst       : in  std_logic;
    mounted   : in  std_logic;
    change    : in  std_logic;
    flush     : in  std_logic;
    track     : in  unsigned(7 downto 0);
    active    : in  std_logic;
    wr        : in  std_logic;
    ack       : in  std_logic;
    bits_in   : in  unsigned(15 downto 0);
    flags_in  : in  std_logic_vector(1 downto 0);
    req       : out std_logic;
    req_save  : out std_logic;
    req_track : out unsigned(7 downto 0);
    dirty     : out std_logic;
    valid     : out std_logic;
    ready     : out std_logic;
    busy      : out std_logic;
    bit_count : out unsigned(15 downto 0);
    noise     : out std_logic;
    lat_max   : out unsigned(31 downto 0);
    reqs      : out unsigned(15 downto 0);
    lat_clear : in  std_logic
  );
end entity;

architecture rtl of a2_trk is
  signal cur     : unsigned(7 downto 0) := (others => '0');
  signal valid_r : std_logic := '0';
  signal dirty_r : std_logic := '0';
  signal busy_r  : std_logic := '0';
  signal save_r  : std_logic := '0';
  signal rtrk    : unsigned(7 downto 0) := (others => '0');
  signal flush_r : std_logic := '0';
  signal prev    : unsigned(7 downto 0) := (others => '0');
  signal rest    : integer range 0 to SETTLE := 0;
  signal bits_r  : unsigned(15 downto 0) := (others => '0');
  signal noise_r : std_logic := '0';
  signal lat     : unsigned(31 downto 0) := (others => '0');
  signal latm    : unsigned(31 downto 0) := (others => '0');
  signal nreq    : unsigned(15 downto 0) := (others => '0');
  signal settled : std_logic;
begin
  req       <= busy_r;
  req_save  <= save_r;
  req_track <= rtrk;
  dirty     <= dirty_r;
  valid     <= valid_r;
  busy      <= busy_r;
  ready     <= '1' when mounted = '1' and valid_r = '1' and busy_r = '0' and rst = '0' and track = cur else '0';
  bit_count <= bits_r;
  noise     <= noise_r;
  lat_max   <= latm;
  reqs      <= nreq;
  settled   <= '1' when rest = SETTLE and track = prev else '0';      -- not in the clock after the head moved: prev is one clock behind

  process (clk)
  begin
    if rising_edge(clk) then
      prev <= track;
      if track /= prev then
        rest <= 0;
      elsif rest < SETTLE then
        rest <= rest + 1;
      end if;
      if lat_clear = '1' then latm <= (others => '0'); nreq <= (others => '0'); end if;
      if busy_r = '1' then lat <= lat + 1; end if;
      if rst = '1' or change = '1' then
        valid_r <= '0'; dirty_r <= '0'; busy_r <= '0'; save_r <= '0'; flush_r <= '0'; noise_r <= '0';
      else
        if flush = '1' then flush_r <= '1'; end if;
        if wr = '1' and valid_r = '1' then dirty_r <= '1'; end if;
        if busy_r = '1' then
          if ack = '1' then
            busy_r <= '0';
            if lat > latm and lat_clear = '0' then latm <= lat; end if;
            if save_r = '1' then
              dirty_r <= '0'; flush_r <= '0';
            else
              cur <= rtrk; valid_r <= '1'; dirty_r <= '0';
              bits_r <= bits_in; noise_r <= flags_in(0);
            end if;
          end if;
        elsif mounted = '1' then
          if valid_r = '1' and dirty_r = '1' and (active = '0' or flush_r = '1' or (track /= cur and settled = '1')) then
            save_r <= '1'; rtrk <= cur; busy_r <= '1'; lat <= (others => '0'); nreq <= nreq + 1;
          elsif (valid_r = '0' or track /= cur) and settled = '1' then
            save_r <= '0'; rtrk <= track; busy_r <= '1'; lat <= (others => '0'); nreq <= nreq + 1;
          end if;
        end if;
      end if;
    end if;
  end process;
end architecture;
