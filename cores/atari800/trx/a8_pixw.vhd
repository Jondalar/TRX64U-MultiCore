-- SPDX-License-Identifier: GPL-3.0-or-later
-- T88 D5: the Atari picture into a frame buffer in the DDR2 for the host's pixel layer (pix_layer.v): RGB565, COLS x LINES
-- source pixels, COLS*2 bytes a row, four buffers (the layer shows one, the app flips on the frame-done report, the core
-- writes the next buffer in the ring). Written for this project (TREX), no upstream code; the same circuit as c264_pixw.v
-- (line buffer in sys_clk, written to the DDR2 in ui_clk, a line is handed over when its last word is stored).
--
-- Everything on the sys_clk side moves only on `dotst`, one strobe per pixel of the machine (the machine's tick that carries
-- the highres colour clock), so the pause (no ticks) stops it too. A pixel is one hires pixel; the window is COLS pixels wide
-- from the X0'th pixel after hblank ends (hblank does not gate it: see below) and LINES lines from the Y0'th line after vblank ends. Pixels are packed little endian,
-- 8 to a 128-bit word (COLS/8 words a row). A completed line goes to the ui_clk side by a toggle; that side reads the word from
-- the line buffer and writes it through the host's e_ port (MIG command, data with the command, one command at a time).
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity a8_pixw is
  generic (
    X0    : integer := 2;
    Y0    : integer := 18;
    COLS  : integer := 384;
    LINES : integer := 288);
  port (
    sys_clk   : in  std_logic;
    sys_rst   : in  std_logic;
    dotst     : in  std_logic;
    rgb565    : in  std_logic_vector(15 downto 0);
    hblank    : in  std_logic;
    vblank    : in  std_logic;
    quiet     : out std_logic;                         -- sys_clk: no line is on its way to the DDR2
    frame_tgl : out std_logic;                         -- toggles with every complete frame
    latest    : out std_logic_vector(7 downto 0);      -- b7 valid, b1:0 the buffer of the newest complete frame
    seq       : out std_logic_vector(7 downto 0);
    overruns  : out std_logic_vector(15 downto 0);
    -- quasi static, from the app (cpu_clk)
    fb_base   : in  std_logic_vector(31 downto 0);     -- DDR2 byte address of buffer 0
    fb_stride : in  std_logic_vector(31 downto 0);     -- bytes between the buffers
    -- ui_clk: the host's e_ port
    ui_clk    : in  std_logic;
    ui_rst    : in  std_logic;
    e_addr    : out std_logic_vector(26 downto 0);
    e_cmd     : out std_logic_vector(2 downto 0);
    e_en      : out std_logic;
    e_rdy     : in  std_logic;
    e_wdf_data : out std_logic_vector(127 downto 0);
    e_wdf_mask : out std_logic_vector(15 downto 0);
    e_wdf_wren : out std_logic);
end entity;

architecture rtl of a8_pixw is
  constant WORDS : integer := COLS / 8;
  attribute ASYNC_REG : string;
  -- sys_clk side
  signal hb_d, vb_d     : std_logic := '1';
  signal in_frame       : std_logic := '0';
  signal x              : unsigned(9 downto 0) := (others => '0');
  signal ly             : unsigned(9 downto 0) := (others => '1');
  signal cap            : std_logic := '0';
  signal last_px        : std_logic_vector(15 downto 0) := (others => '0');
  signal cap_row        : unsigned(9 downto 0) := (others => '0');
  signal acc            : std_logic_vector(127 downto 0) := (others => '0');
  signal pk             : unsigned(2 downto 0) := (others => '0');
  signal grp            : unsigned(5 downto 0) := (others => '0');
  signal half           : std_logic := '0';
  signal fcnt, fwb      : unsigned(1 downto 0) := (others => '0');
  type lb_t is array (0 to 127) of std_logic_vector(127 downto 0);
  signal lb             : lb_t := (others => (others => '0'));
  signal ln_tgl         : std_logic := '0';
  signal ln_row         : unsigned(9 downto 0) := (others => '0');
  signal ln_wb          : unsigned(1 downto 0) := (others => '0');
  signal ln_half        : std_logic := '0';
  signal waiting_last   : std_logic := '0';
  signal ack_tgl        : std_logic := '0';
  signal ack_s          : std_logic_vector(1 downto 0) := "00";
  attribute ASYNC_REG of ack_s : signal is "TRUE";
  signal frame_tgl_i    : std_logic := '0';
  signal latest_i       : std_logic_vector(7 downto 0) := (others => '0');
  signal seq_i          : unsigned(7 downto 0) := (others => '0');
  signal overruns_i     : unsigned(15 downto 0) := (others => '0');
  -- ui_clk side
  type us_t is (U_IDLE, U_BASE, U_RD, U_W1, U_W2, U_PUT, U_NEXT);
  signal us             : us_t := U_IDLE;
  signal ln_s           : std_logic_vector(2 downto 0) := "000";
  attribute ASYNC_REG of ln_s : signal is "TRUE";
  signal ack_r          : std_logic := '0';
  signal lb_q           : std_logic_vector(127 downto 0) := (others => '0');
  signal rd_a           : unsigned(6 downto 0) := (others => '0');
  signal ug             : unsigned(5 downto 0) := (others => '0');
  signal u_row          : unsigned(9 downto 0) := (others => '0');
  signal u_wb           : unsigned(1 downto 0) := (others => '0');
  signal u_half         : std_logic := '0';
  signal b_a, b_b, b_q  : std_logic_vector(31 downto 0) := (others => '0');
  signal s_a, s_b, s_q  : std_logic_vector(31 downto 0) := (others => '0');
  signal buf_w          : unsigned(26 downto 0) := (others => '0');
  signal e_en_i         : std_logic := '0';
  signal e_cmd_i        : std_logic_vector(2 downto 0) := "000";
begin
  quiet     <= '1' when ack_s(1) = ln_tgl else '0';
  frame_tgl <= frame_tgl_i;
  latest    <= latest_i;
  seq       <= std_logic_vector(seq_i);
  overruns  <= std_logic_vector(overruns_i);

  -- ------------------------------------------------------------------ sys_clk side
  process (sys_clk)
    variable ls, fs, fe, in_n, cap_n, last_word, pix_cap : boolean;
    variable x_n, ly_n, row_n : unsigned(9 downto 0);
    variable pk_n  : unsigned(2 downto 0);
    variable grp_n : unsigned(5 downto 0);
    variable hb, vb : boolean;
    variable word : std_logic_vector(127 downto 0);
    variable px_in : std_logic_vector(15 downto 0);
  begin
    if rising_edge(sys_clk) then
      ack_s <= ack_s(0) & ack_tgl;
      if dotst = '1' then
        hb := hblank = '1'; vb := vblank = '1';
        ls := hb_d = '1' and not hb;
        fs := vb_d = '1' and not vb;
        fe := vb_d = '0' and vb;
        in_n := (in_frame = '1' or fs) and not fe;
        x_n := x; ly_n := ly; cap_n := cap = '1'; pk_n := pk; grp_n := grp; row_n := cap_row;
        last_word := false; pix_cap := false;
        if fs then ly_n := (others => '1'); cap_n := false; end if;
        if fe then cap_n := false; end if;
        if ls then
          x_n := (others => '0'); pk_n := (others => '0'); grp_n := (others => '0');
          if in_n and not vb then
            ly_n := ly_n + 1;
            cap_n := (to_integer(ly_n) >= Y0) and (to_integer(ly_n) < Y0 + LINES);
            row_n := ly_n - to_unsigned(Y0, 10);
          else
            cap_n := false;
          end if;
        end if;
        -- x counts every pixel from the end of hblank to the next end of hblank; the window is X0 .. X0+COLS-1 of it. ANTIC's
        -- hblank leaves 356 of the 384 pixels, the rest of the window (after the visible part) repeats the last visible pixel,
        -- which is the border colour on every normal screen.
        if cap_n and to_integer(x_n) >= X0 and to_integer(x_n) < X0 + COLS then
          pix_cap := true;
          pk_n := pk_n + 1;
          if pk_n = 0 then
            grp_n := grp_n + 1;
            if to_integer(grp_n) = WORDS then last_word := true; end if;
          end if;
        end if;
        if x_n /= 1023 then x_n := x_n + 1; end if;
        if hb then px_in := last_px; else px_in := rgb565; last_px <= rgb565; end if;

        hb_d <= hblank; vb_d <= vblank;
        if fs then fwb <= fcnt; fcnt <= fcnt + 1; waiting_last <= '0'; end if;
        if pix_cap then
          word := px_in & acc(127 downto 16);
          acc <= word;
          if pk_n = 0 then lb(to_integer(half & grp)) <= word; end if;
        end if;
        x <= x_n; ly <= ly_n; pk <= pk_n; grp <= grp_n; cap_row <= row_n;
        if cap_n then cap <= '1'; else cap <= '0'; end if;
        if in_n then in_frame <= '1'; else in_frame <= '0'; end if;
        if last_word then                                 -- the line is complete: hand it to the ui side
          if ack_s(1) /= ln_tgl then overruns_i <= overruns_i + 1; end if;
          ln_tgl <= not ln_tgl;
          ln_row <= row_n; ln_wb <= fwb; ln_half <= half;
          half <= not half;
          cap <= '0';
          if to_integer(row_n) = LINES - 1 then waiting_last <= '1'; end if;
        end if;
      end if;
      -- frame done: the last line is written (acknowledged by the ui side)
      if waiting_last = '1' and ack_s(1) = ln_tgl then
        waiting_last <= '0';
        frame_tgl_i <= not frame_tgl_i;
        latest_i <= '1' & "00000" & std_logic_vector(fwb);
        seq_i <= seq_i + 1;
      end if;
      if sys_rst = '1' then in_frame <= '0'; waiting_last <= '0'; cap <= '0'; end if;
    end if;
  end process;

  -- ------------------------------------------------------------------ ui_clk side
  process (ui_clk)
    variable base_w, stride_w, row_w : unsigned(26 downto 0);
  begin
    if rising_edge(ui_clk) then
      ln_s <= ln_s(1 downto 0) & ln_tgl;
      lb_q <= lb(to_integer(rd_a));
      b_a <= fb_base;   b_b <= b_a;   if b_a = b_b then b_q <= b_b; end if;
      s_a <= fb_stride; s_b <= s_a;   if s_a = s_b then s_q <= s_b; end if;
      base_w := unsigned(b_q(27 downto 1));
      stride_w := unsigned(s_q(27 downto 1));
      row_w := resize(u_row * to_unsigned(COLS, 10), 27);                              -- row * COLS (16-bit units)
      if ui_rst = '1' then
        us <= U_IDLE; e_en_i <= '0';
      else
        case us is
          when U_IDLE =>
            if ln_s(2) /= ln_s(1) then
              u_row <= ln_row; u_wb <= ln_wb; u_half <= ln_half; ug <= (others => '0'); us <= U_BASE;
            end if;
          when U_BASE =>                                   -- buffer base in 16-bit units
            case u_wb is
              when "00" => buf_w <= base_w;
              when "01" => buf_w <= base_w + stride_w;
              when "10" => buf_w <= base_w + shift_left(stride_w, 1);
              when others => buf_w <= base_w + shift_left(stride_w, 1) + stride_w;
            end case;
            us <= U_RD;
          when U_RD =>
            rd_a <= u_half & ug; us <= U_W1;
          when U_W1 => us <= U_W2;
          when U_W2 =>                                     -- lb_q holds the word
            e_wdf_data <= lb_q;
            e_addr <= std_logic_vector(buf_w + row_w + shift_left(resize(ug, 27), 3));
            e_cmd_i <= "000";
            e_en_i <= '1';
            us <= U_PUT;
          when U_PUT =>
            if e_rdy = '1' then e_en_i <= '0'; us <= U_NEXT; end if;
          when U_NEXT =>
            if to_integer(ug) = WORDS - 1 then
              ack_r <= ln_s(2); us <= U_IDLE;
            else
              ug <= ug + 1; us <= U_RD;
            end if;
        end case;
      end if;
    end if;
  end process;
  ack_tgl    <= ack_r;
  e_en       <= e_en_i;
  e_cmd      <= e_cmd_i;
  e_wdf_mask <= (others => '0');
  e_wdf_wren <= e_en_i when e_cmd_i = "000" else '0';
end architecture;
