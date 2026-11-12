-- TRX Apple //e core (T89 D5): the Apple //e picture into a frame buffer in the DDR2 for the host's pixel layer
-- (pix_layer.v): RGB565, 560 x 384 source pixels (every one of the 192 Apple lines written twice: the layer scales
-- uniformly), 1120 bytes a row, four buffers (the layer shows one, the app flips on the frame-done report, the core writes the next
-- buffer of the ring) - the circuit of nes_pixw.v of the NES core, in VHDL, with the row stride of this picture.
-- Written for this project (TREX), no upstream code. The colour decode (a2_pixc) is that of NanoApple2's vga_controller.vhd
-- (see rtl/NOTICE): the last six bits of the 1-bit video stream at 14.318 MHz, rotated by the pixel phase, pick one of 16 palette
-- colours per 14M pixel.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.a2_palette_pkg.all;

----------------------------------------------------------------------------------------------
-- Colour decode and the capture window, in the machine's enable cycles (ce).
--   video, color_line, hbl, vbl     the machine's (apple2.vhd), updated on ce
--   mono   0 colour 1 B&W 2 green 3 amber;  pal 0 original 1 IIgs 2 AppleWin 3 custom
--   pv     (registered) this ce cycle's pixel is inside the window; rgb is its colour (RGB565)
--   ls     the first pixel of a line, fs the first pixel of the first line, lrow the line (0..191)
----------------------------------------------------------------------------------------------
entity a2_pixc is
  generic (
    X0    : integer := 0;                      -- hcount of the window's first pixel
    COLS  : integer := 560;
    LINES : integer := 192
  );
  port (
    clk        : in  std_logic;
    ce         : in  std_logic;
    video      : in  std_logic;
    color_line : in  std_logic;
    hbl        : in  std_logic;
    vbl        : in  std_logic;
    col80      : in  std_logic;                        -- 80 columns: the picture leads the 40 column one by half a cell (7 dots), the window starts there
    mono       : in  std_logic_vector(1 downto 0);
    pal        : in  std_logic_vector(1 downto 0);
    custom     : in  std_logic_vector(383 downto 0);   -- 16 x RRGGBB, entry n at bits 24 n + 23 .. 24 n
    pv         : out std_logic;
    rgb        : out std_logic_vector(15 downto 0);
    ls         : out std_logic;
    fs         : out std_logic;
    lrow       : out unsigned(7 downto 0);
    hq_o       : out unsigned(10 downto 0)
  );
end entity;

architecture rtl of a2_pixc is
  signal shr      : std_logic_vector(5 downto 0) := (others => '0');
  signal hcount   : unsigned(10 downto 0) := (others => '0');
  signal last_hbl : std_logic := '1';
  signal ly       : unsigned(7 downto 0) := (others => '1');
  signal rgb_r    : std_logic_vector(15 downto 0) := (others => '0');
  signal pv_r, ls_r, fs_r : std_logic := '0';
  signal lrow_r   : unsigned(7 downto 0) := (others => '0');
  signal hq       : unsigned(10 downto 0) := (others => '0');
  signal xs       : integer range 0 to 2047 := X0;      -- the window's first hcount of this line

  function cust (c : std_logic_vector(383 downto 0); n : integer) return std_logic_vector is
  begin
    return c(24 * n + 23 downto 24 * n);
  end function;
begin
  pv <= pv_r; rgb <= rgb_r; ls <= ls_r; fs <= fs_r; lrow <= lrow_r; hq_o <= hq;

  process (clk)
    variable rgb24 : std_logic_vector(23 downto 0);
    variable sc    : unsigned(3 downto 0);
    variable sc_i  : integer range 0 to 15;
    variable cl    : std_logic;
    variable inw   : boolean;
    variable ph    : integer range 0 to 3;
  begin
    if rising_edge(clk) then
      if ce = '1' then
        last_hbl <= hbl;
        if last_hbl = '1' and hbl = '0' then
          hcount <= (others => '0');
          if col80 = '1' then xs <= X0 - 7; else xs <= X0; end if;
          if vbl = '1' then ly <= (others => '1'); else ly <= ly + 1; end if;
        else
          hcount <= hcount + 1;
        end if;
        shr <= video & shr(5 downto 1);

        -- the colour of the pixel the six bits describe (values before this update, as vga_controller.vhd)
        cl := color_line;
        if mono /= "00" then cl := '0'; end if;
        rgb24 := x"000000";
        case mono is
          when "00" =>
            if pal = "11" then rgb24 := cust(custom, 0); end if;
          when "10"   => rgb24 := x"000F01";
          when "11"   => rgb24 := x"200801";
          when others => rgb24 := x"000000";
        end case;
        ph := to_integer(hcount(1 downto 0));
        if cl = '0' then
          if shr(2) = '1' then
            case mono is
              when "00" =>
                if pal = "00" then rgb24 := x"F1F1F1";
                elsif pal = "11" then rgb24 := cust(custom, 15);
                else rgb24 := x"FFFFFF"; end if;
              when "01"   => rgb24 := x"FFFFFF";
              when "10"   => rgb24 := x"00C001";
              when others => rgb24 := x"FF8001";
            end case;
          end if;
        elsif shr(0) = shr(4) and shr(5) = shr(1) then
          -- rotate the 4-bit value by the pixel phase
          sc := unsigned(shr(4 downto 1));
          case ph is
            when 0 => sc := sc;
            when 1 => sc := sc(2 downto 0) & sc(3);
            when 2 => sc := sc(1 downto 0) & sc(3 downto 2);
            when others => sc := sc(0) & sc(3 downto 1);
          end case;
          sc_i := to_integer(sc);
          case pal is
            when "00"   => rgb24 := PAL_NTSC(sc_i);
            when "01"   => rgb24 := PAL_IIGS(sc_i);
            when "10"   => rgb24 := PAL_APPLEWIN(sc_i);
            when others => rgb24 := cust(custom, CUSTOM_MAP(sc_i));
          end case;
        else
          -- the tint is changing: black, grey or white
          case shr(3 downto 2) is
            when "11" =>
              if pal = "00" then rgb24 := x"F1F1F1";
              elsif pal = "11" then rgb24 := cust(custom, 15);
              else rgb24 := x"FFFFFF"; end if;
            when "01" | "10" =>
              if pal = "11" then rgb24 := cust(custom, 5); else rgb24 := x"636363"; end if;
            when others => rgb24 := x"000000";
          end case;
        end if;
        rgb_r <= rgb24(23 downto 19) & rgb24(15 downto 10) & rgb24(7 downto 3);

        hq <= hcount;
        inw := (hcount >= xs) and (hcount < xs + COLS) and (ly < LINES);
        if inw then pv_r <= '1'; else pv_r <= '0'; end if;
        if inw and hcount = xs then ls_r <= '1'; else ls_r <= '0'; end if;
        if inw and hcount = xs and ly = 0 then fs_r <= '1'; else fs_r <= '0'; end if;
        lrow_r <= ly;
      end if;
    end if;
  end process;
end architecture;

----------------------------------------------------------------------------------------------
-- The frame writer. Pixels (rgb, one per `strobe`) pack into 128 bit words (8 pixels); a line is 70 words; a finished line goes
-- to the ui_clk side by a toggle, which writes it twice (rows 2 y and 2 y + 1, the second dimmed with `scan`) through the host's
-- e_ port (MIG command with the data, one command at a time).
----------------------------------------------------------------------------------------------
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity a2_pixw is
  generic (
    LINES : integer := 192
  );
  port (
    sys_clk  : in  std_logic;
    sys_rst  : in  std_logic;
    strobe   : in  std_logic;                        -- a pixel of the window (a ce cycle)
    rgb565   : in  std_logic_vector(15 downto 0);
    ls       : in  std_logic;
    fs       : in  std_logic;
    lrow     : in  unsigned(7 downto 0);
    quiet    : out std_logic;
    frame_tgl : out std_logic;
    latest   : out std_logic_vector(7 downto 0);
    seq      : out std_logic_vector(7 downto 0);
    overruns : out std_logic_vector(15 downto 0);
    -- quasi static, from the app (cpu_clk)
    fb_base   : in  std_logic_vector(31 downto 0);
    fb_stride : in  std_logic_vector(31 downto 0);
    scan      : in  std_logic;
    -- ui_clk: the host's e_ port
    ui_clk   : in  std_logic;
    ui_rst   : in  std_logic;
    e_addr   : out std_logic_vector(26 downto 0);
    e_cmd    : out std_logic_vector(2 downto 0);
    e_en     : out std_logic;
    e_rdy    : in  std_logic;
    e_wdf_data : out std_logic_vector(127 downto 0);
    e_wdf_mask : out std_logic_vector(15 downto 0);
    e_wdf_wren : out std_logic
  );
end entity;

architecture rtl of a2_pixw is
  constant WORDS : integer := 70;
  -- sys_clk side
  signal acc     : std_logic_vector(127 downto 0) := (others => '0');
  signal pk      : unsigned(2 downto 0) := (others => '0');
  signal grp     : integer range 0 to 127 := 0;
  signal half    : std_logic := '0';
  signal fcnt    : unsigned(1 downto 0) := (others => '0');
  signal fwb     : unsigned(1 downto 0) := (others => '0');
  type lb_t is array (0 to 255) of std_logic_vector(127 downto 0);
  signal lb      : lb_t := (others => (others => '0'));
  attribute ram_style : string;
  attribute ram_style of lb : signal is "block";
  signal ln_tgl  : std_logic := '0';
  signal ln_row  : unsigned(7 downto 0) := (others => '0');
  signal ln_wb   : unsigned(1 downto 0) := (others => '0');
  signal ln_half : std_logic := '0';
  signal waiting_last : std_logic := '0';
  signal ack_tgl : std_logic := '0';
  signal ack_s   : std_logic_vector(1 downto 0) := "00";
  signal ft_r    : std_logic := '0';
  signal latest_r : std_logic_vector(7 downto 0) := (others => '0');
  signal seq_r   : unsigned(7 downto 0) := (others => '0');
  signal over_r  : unsigned(15 downto 0) := (others => '0');
  attribute ASYNC_REG : string;
  attribute ASYNC_REG of ack_s : signal is "TRUE";
  -- ui_clk side
  signal ln_s    : std_logic_vector(2 downto 0) := "000";
  attribute ASYNC_REG of ln_s : signal is "TRUE";
  signal ack_r   : std_logic := '0';
  signal lb_q    : std_logic_vector(127 downto 0) := (others => '0');
  signal rd_a    : unsigned(7 downto 0) := (others => '0');
  type us_t is (U_IDLE, U_BASE, U_RD, U_W1, U_W2, U_PUT, U_NEXT);
  signal us      : us_t := U_IDLE;
  signal ug      : integer range 0 to 127 := 0;
  signal pass    : std_logic := '0';
  signal u_row   : unsigned(7 downto 0) := (others => '0');
  signal u_wb    : unsigned(1 downto 0) := (others => '0');
  signal u_half  : std_logic := '0';
  signal b_a, b_b, b_q : std_logic_vector(31 downto 0) := x"01D00000";
  signal s_a, s_b, s_q : std_logic_vector(31 downto 0) := x"00100000";
  signal sc_a, sc_b    : std_logic := '0';
  attribute ASYNC_REG of b_a, s_a, sc_a : signal is "TRUE";
  signal buf_w   : unsigned(26 downto 0) := (others => '0');
  signal row_w   : unsigned(26 downto 0) := (others => '0');
  signal e_en_r  : std_logic := '0';
  signal e_addr_r : std_logic_vector(26 downto 0) := (others => '0');
  signal e_data_r : std_logic_vector(127 downto 0) := (others => '0');
  signal dimmed   : std_logic_vector(127 downto 0);
begin
  quiet <= '1' when ack_s(1) = ln_tgl else '0';
  frame_tgl <= ft_r;
  latest <= latest_r;
  seq <= std_logic_vector(seq_r);
  overruns <= std_logic_vector(over_r);
  ack_tgl <= ack_r;

  process (sys_clk)
    variable w     : std_logic_vector(127 downto 0);
    variable g_c   : integer range 0 to 127;
    variable pk_c  : unsigned(2 downto 0);
    variable pk_n  : unsigned(2 downto 0);
  begin
    if rising_edge(sys_clk) then
      ack_s <= ack_s(0) & ack_tgl;
      if strobe = '1' then
        if fs = '1' then
          fwb <= fcnt; fcnt <= fcnt + 1; waiting_last <= '0';
        end if;
        if ls = '1' then pk_c := (others => '0'); g_c := 0; else pk_c := pk; g_c := grp; end if;
        w := rgb565 & acc(127 downto 16);
        acc <= w;
        pk_n := pk_c + 1;
        pk <= pk_n;
        grp <= g_c;
        if pk_n = 0 then                                  -- the eighth pixel of the word
          lb(to_integer(half & to_unsigned(g_c, 7))) <= w;
          if g_c = WORDS - 1 then                         -- the line is complete: hand it to the ui side
            if ack_s(1) /= ln_tgl then over_r <= over_r + 1; end if;
            ln_tgl <= not ln_tgl;
            ln_row <= lrow; ln_wb <= fwb; ln_half <= half;
            half <= not half;
            grp <= 0;
            if lrow = LINES - 1 then waiting_last <= '1'; end if;
          else
            grp <= g_c + 1;
          end if;
        end if;
      end if;
      -- frame done: the last line is written (acknowledged by the ui side)
      if waiting_last = '1' and ack_s(1) = ln_tgl then
        waiting_last <= '0';
        ft_r <= not ft_r;
        latest_r <= "100000" & std_logic_vector(fwb);
        seq_r <= seq_r + 1;
      end if;
      if sys_rst = '1' then waiting_last <= '0'; end if;
    end if;
  end process;

  -- ------------------------------------------------------------------ ui_clk side
  g_dim : for i in 0 to 7 generate
    dimmed(16 * i + 15 downto 16 * i) <= ('0' & lb_q(16 * i + 15 downto 16 * i + 1)) and x"7BEF";
  end generate;

  e_addr <= e_addr_r;
  e_cmd <= "000";
  e_en <= e_en_r;
  e_wdf_data <= e_data_r;
  e_wdf_mask <= x"0000";
  e_wdf_wren <= e_en_r;

  process (ui_clk)
    variable base_w   : unsigned(26 downto 0);
    variable stride_w : unsigned(26 downto 0);
    variable rowx     : unsigned(8 downto 0);
  begin
    if rising_edge(ui_clk) then
      ln_s <= ln_s(1 downto 0) & ln_tgl;
      lb_q <= lb(to_integer(rd_a));
      b_a <= fb_base;   b_b <= b_a;   if b_a = b_b then b_q <= b_b; end if;
      s_a <= fb_stride; s_b <= s_a;   if s_a = s_b then s_q <= s_b; end if;
      sc_a <= scan;     sc_b <= sc_a;
      base_w := unsigned(b_q(27 downto 1));
      stride_w := unsigned(s_q(27 downto 1));
      if ui_rst = '1' then
        us <= U_IDLE; e_en_r <= '0';
      else
        case us is
          when U_IDLE =>
            if ln_s(2) /= ln_s(1) then
              u_row <= ln_row; u_wb <= ln_wb; u_half <= ln_half; ug <= 0; pass <= '0';
              us <= U_BASE;
            end if;
          when U_BASE =>                                  -- buffer base and row offset, in 16-bit units
            case u_wb is
              when "00"   => buf_w <= base_w;
              when "01"   => buf_w <= base_w + stride_w;
              when "10"   => buf_w <= base_w + shift_left(stride_w, 1);
              when others => buf_w <= base_w + shift_left(stride_w, 1) + stride_w;
            end case;
            rowx := u_row & pass;
            row_w <= resize(shift_left(resize(rowx, 27), 9) + shift_left(resize(rowx, 27), 5) + shift_left(resize(rowx, 27), 4), 27);   -- 560 per row
            us <= U_RD;
          when U_RD =>
            rd_a <= u_half & to_unsigned(ug, 7);
            us <= U_W1;
          when U_W1 => us <= U_W2;
          when U_W2 =>                                    -- lb_q holds the word
            if pass = '1' and sc_b = '1' then e_data_r <= dimmed; else e_data_r <= lb_q; end if;
            e_addr_r <= std_logic_vector(buf_w + row_w + shift_left(to_unsigned(ug, 27), 3));
            e_en_r <= '1';
            us <= U_PUT;
          when U_PUT =>
            if e_rdy = '1' then e_en_r <= '0'; us <= U_NEXT; end if;
          when U_NEXT =>
            if ug = WORDS - 1 then
              if pass = '0' then
                pass <= '1'; ug <= 0; us <= U_BASE;
              else
                ack_r <= ln_s(2); us <= U_IDLE;
              end if;
            else
              ug <= ug + 1; us <= U_RD;
            end if;
        end case;
      end if;
    end if;
  end process;
end architecture;
