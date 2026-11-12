-- T119: the VIC-20 picture into a frame buffer in the DDR2 for the host's pixel layer (pix_layer.v): RGB565, COLS x LINES
-- source pixels, COLS*2 bytes a row, four buffers (the layer shows one, the app flips on the frame-done report, the core writes
-- the next buffer in the ring). Written for this project (TREX), no upstream code; the shape is cores/c264/rtl/trx/c264_pixw.v
-- (line buffer in sys_clk, written to the DDR2 in ui_clk, the line is handed over when its last word is stored).
--
-- Pixel shape. The 6561 delivers 228 x 283 visible pixels (hblank low for 228 pixel clocks, vblank low from line 29 to line 311) of about 1.67 : 1 (wide). The layer scales by whole numbers, so the
-- writer makes the pixels square before: every three VIC-20 pixels become five frame buffer pixels (widths 2, 1, 2), 380 per
-- line; with COLS = 376 the window drops two at each side (about 1.2 VIC-20 pixels of border). Lines are 1:1: LINES = 283 is
-- the whole visible height, from the first line after vblank.
--
-- Everything on the sys_clk side moves on `fpix`, one strobe per frame buffer pixel, made from the machine's pixel enable
-- `ce_pix` (one or two strobes after it, the colour, hblank and vblank are held for them), so the pause (the machine keeps
-- its pixel enable, the 6561 runs on) needs nothing here. A completed line goes to the ui_clk side by a toggle; that side
-- reads the 128-bit word (8 pixels, little endian, 47 words a row) from the line buffer and writes it through the host's e_
-- port (MIG command, data with the command, one command at a time).
--
-- MUT_PIXW <> 0: the window starts that many pixels later. The bench (VC20_MUT_PIXW) must fail with it.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vc20_pixw is
  generic (
    COLS     : integer := 376;
    LINES    : integer := 283;
    X0       : integer := 2;
    Y0       : integer := 0;
    MUT_PIXW : integer := 0
  );
  port (
    sys_clk  : in  std_logic;
    sys_rst  : in  std_logic;
    ce_pix   : in  std_logic;                        -- the machine's pixel enable; colour/hblank/vblank are those of this pixel
    colour   : in  std_logic_vector(3 downto 0);     -- the 6561's colour number
    hblank   : in  std_logic;
    vblank   : in  std_logic;
    frame_tgl : out std_logic;                       -- toggles with every complete frame
    latest   : out std_logic_vector(7 downto 0);     -- b7 valid, b1:0 the buffer of the newest complete frame
    seq      : out std_logic_vector(7 downto 0);
    overruns : out std_logic_vector(15 downto 0);
    -- quasi static, from the app (cpu_clk)
    fb_base   : in  std_logic_vector(31 downto 0);   -- DDR2 byte address of buffer 0
    fb_stride : in  std_logic_vector(31 downto 0);   -- bytes between the buffers
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

architecture rtl of vc20_pixw is
  constant WORDS : integer := COLS / 8;
  constant XS    : integer := X0 + MUT_PIXW;

  -- the colours of the 6561 (m6561.vhd col0..colF), RGB444 -> RGB565
  type pal_t is array (0 to 15) of std_logic_vector(11 downto 0);
  constant PAL : pal_t := (x"000", x"FFF", x"B11", x"5ED", x"C3D", x"4E3", x"33C", x"DE2",
                           x"C60", x"EB8", x"E99", x"AFF", x"EAE", x"AFA", x"A9E", x"FFA");
  function to565 (c : std_logic_vector(11 downto 0)) return std_logic_vector is
  begin
    return c(11 downto 8) & c(11) & c(7 downto 4) & c(7 downto 6) & c(3 downto 0) & c(3);
  end function;

  attribute ASYNC_REG : string;

  -- ------------------------------------------------------------------ sys_clk side
  signal np      : unsigned(1 downto 0) := "00";     -- frame buffer pixels still to come for the last ce_pix
  signal tri     : unsigned(1 downto 0) := "00";     -- position inside the group of three VIC-20 pixels
  signal fpix    : std_logic := '0';
  signal col     : std_logic_vector(15 downto 0) := (others => '0');
  signal hbl     : std_logic := '1';
  signal vbl     : std_logic := '1';

  signal hb_d, vb_d : std_logic := '1';
  signal in_frame : std_logic := '0';
  signal x       : unsigned(9 downto 0) := (others => '0');       -- frame buffer pixels since hblank ended
  signal ly      : unsigned(9 downto 0) := (others => '1');       -- lines since vblank ended
  signal cap     : std_logic := '0';
  signal cap_row : unsigned(9 downto 0) := (others => '0');
  signal acc     : std_logic_vector(127 downto 0) := (others => '0');
  signal pk      : unsigned(2 downto 0) := (others => '0');
  signal grp     : unsigned(5 downto 0) := (others => '0');
  signal half    : std_logic := '0';
  signal fcnt    : unsigned(1 downto 0) := "00";
  signal fwb     : unsigned(1 downto 0) := "00";
  type lb_t is array (0 to 127) of std_logic_vector(127 downto 0);
  signal lb      : lb_t;
  signal lb_we   : std_logic;
  signal lb_wa   : unsigned(6 downto 0);
  signal lb_wd   : std_logic_vector(127 downto 0);
  signal ln_tgl  : std_logic := '0';
  signal ln_row  : unsigned(9 downto 0) := (others => '0');
  signal ln_wb   : unsigned(1 downto 0) := "00";
  signal ln_half : std_logic := '0';
  signal waiting_last : std_logic := '0';
  signal ack_tgl : std_logic := '0';
  signal ack_s   : std_logic_vector(1 downto 0) := "00";
  signal over_i  : unsigned(15 downto 0) := (others => '0');
  signal seq_i   : unsigned(7 downto 0) := (others => '0');
  signal ftgl_i  : std_logic := '0';
  signal latest_i : std_logic_vector(7 downto 0) := (others => '0');
  attribute ASYNC_REG of ack_s : signal is "TRUE";

  -- the next state of one frame buffer pixel
  signal ls, fs, fe : boolean;
  signal n_x, n_ly, n_row : unsigned(9 downto 0);
  signal n_pk    : unsigned(2 downto 0);
  signal n_grp   : unsigned(5 downto 0);
  signal n_cap, n_in, pix_cap, last_word : boolean;

  -- ------------------------------------------------------------------ ui_clk side
  type us_t is (U_IDLE, U_BASE, U_RD, U_W1, U_W2, U_PUT, U_NEXT);
  signal us      : us_t := U_IDLE;
  signal ln_s    : std_logic_vector(2 downto 0) := "000";
  signal ack_r   : std_logic := '0';
  signal lb_q    : std_logic_vector(127 downto 0) := (others => '0');
  signal rd_a    : unsigned(6 downto 0) := (others => '0');
  signal ug      : unsigned(5 downto 0) := (others => '0');
  signal u_row   : unsigned(9 downto 0) := (others => '0');
  signal u_wb    : unsigned(1 downto 0) := "00";
  signal u_half  : std_logic := '0';
  -- the registers cross from cpu_clk: a value counts when two samples in a row agree
  signal b_a, b_b, s_a, s_b : std_logic_vector(31 downto 0) := (others => '0');
  signal b_q     : std_logic_vector(31 downto 0) := x"01D00000";
  signal s_q     : std_logic_vector(31 downto 0) := x"00100000";
  signal buf_w   : unsigned(26 downto 0) := (others => '0');
  signal e_en_i  : std_logic := '0';
  signal e_cmd_i : std_logic_vector(2 downto 0) := "000";
  attribute ASYNC_REG of ln_s : signal is "TRUE";
begin
  assert COLS = 376 report "vc20_pixw: the row address is built for COLS = 376" severity failure;
  frame_tgl <= ftgl_i;
  seq       <= std_logic_vector(seq_i);
  overruns  <= std_logic_vector(over_i);
  latest    <= latest_i;

  -- ce_pix -> up to two strobes: widths 2, 1, 2 per group of three visible pixels; a blanked pixel gives one strobe and restarts the group
  process (sys_clk)
  begin
    if rising_edge(sys_clk) then
      fpix <= '0';
      if ce_pix = '1' then
        col <= to565(PAL(to_integer(unsigned(colour))));
        hbl <= hblank;
        vbl <= vblank;
        if hblank = '1' then
          np <= "01"; tri <= "00";
        else
          if tri = 1 then np <= "01"; else np <= "10"; end if;
          if tri = 2 then tri <= "00"; else tri <= tri + 1; end if;
        end if;
      elsif np /= 0 then
        fpix <= '1';
        np <= np - 1;
      end if;
    end if;
  end process;

  -- the next state of one frame buffer pixel, combinational
  process (hb_d, hbl, vb_d, vbl, in_frame, x, ly, cap, pk, grp, cap_row)   -- VHDL-93: Vivado reads this file without -vhdl2008
    variable v_x, v_ly, v_row : unsigned(9 downto 0);
    variable v_pk  : unsigned(2 downto 0);
    variable v_grp : unsigned(5 downto 0);
    variable v_cap, v_in, v_pc, v_lw, v_ls, v_fs, v_fe : boolean;
  begin
    v_ls := hb_d = '1' and hbl = '0';
    v_fs := vb_d = '1' and vbl = '0';
    v_fe := vb_d = '0' and vbl = '1';
    v_in := (in_frame = '1' or v_fs) and not v_fe;
    v_x := x; v_ly := ly; v_cap := (cap = '1'); v_pk := pk; v_grp := grp; v_row := cap_row;
    v_lw := false; v_pc := false;
    if v_fs then v_ly := (others => '1'); v_cap := false; end if;
    if v_fe then v_cap := false; end if;
    if v_ls then
      v_x := (others => '0'); v_pk := (others => '0'); v_grp := (others => '0');
      if v_in and vbl = '0' then
        v_ly := v_ly + 1;
        v_cap := (to_integer(v_ly) >= Y0) and (to_integer(v_ly) < Y0 + LINES);
        v_row := v_ly - to_unsigned(Y0, 10);
      else
        v_cap := false;
      end if;
    end if;
    if hbl = '0' then
      if v_cap and to_integer(v_x) >= XS and to_integer(v_x) < XS + COLS then
        v_pc := true;
        v_pk := v_pk + 1;
        if v_pk = 0 then                                 -- the eighth pixel of the word
          v_grp := v_grp + 1;
          if to_integer(v_grp) = WORDS then v_lw := true; end if;
        end if;
      end if;
      v_x := v_x + 1;
    end if;
    ls <= v_ls; fs <= v_fs; fe <= v_fe;
    n_x <= v_x; n_ly <= v_ly; n_row <= v_row; n_pk <= v_pk; n_grp <= v_grp;
    n_cap <= v_cap; n_in <= v_in; pix_cap <= v_pc; last_word <= v_lw;
  end process;

  -- the line buffer: written here (one port), read in ui_clk
  lb_we <= '1' when fpix = '1' and pix_cap and n_pk = 0 else '0';
  lb_wa <= half & grp;
  lb_wd <= col & acc(127 downto 16);
  process (sys_clk)
  begin
    if rising_edge(sys_clk) then
      if lb_we = '1' then lb(to_integer(lb_wa)) <= lb_wd; end if;
    end if;
  end process;

  process (sys_clk)
  begin
    if rising_edge(sys_clk) then
      ack_s <= ack_s(0) & ack_tgl;
      if fpix = '1' then
        hb_d <= hbl; vb_d <= vbl;
        if fs then
          fwb <= fcnt; fcnt <= fcnt + 1; waiting_last <= '0';
        end if;
        if pix_cap then acc <= col & acc(127 downto 16); end if;
        x <= n_x; pk <= n_pk; grp <= n_grp; cap_row <= n_row; ly <= n_ly;
        if n_cap then cap <= '1'; else cap <= '0'; end if;
        if n_in then in_frame <= '1'; else in_frame <= '0'; end if;
        if last_word then                                -- the line is complete: hand it to the ui side
          if ack_s(1) /= ln_tgl then over_i <= over_i + 1; end if;
          ln_tgl <= not ln_tgl;
          ln_row <= n_row; ln_wb <= fwb; ln_half <= half;
          half <= not half;
          cap <= '0';
          if to_integer(n_row) = LINES - 1 then waiting_last <= '1'; end if;
        end if;
      end if;
      -- frame done: the last line is written (acknowledged by the ui side)
      if waiting_last = '1' and ack_s(1) = ln_tgl then
        waiting_last <= '0';
        ftgl_i <= not ftgl_i;
        latest_i <= "100000" & std_logic_vector(fwb);
        seq_i <= seq_i + 1;
      end if;
      if sys_rst = '1' then in_frame <= '0'; waiting_last <= '0'; cap <= '0'; end if;
    end if;
  end process;

  e_wdf_mask <= x"0000";
  e_en       <= e_en_i;
  e_cmd      <= e_cmd_i;
  e_wdf_wren <= e_en_i when e_cmd_i = "000" else '0';
  ack_tgl    <= ack_r;

  process (ui_clk)
    variable base_w, stride_w, row_w, r : unsigned(26 downto 0);
  begin
    if rising_edge(ui_clk) then
      ln_s <= ln_s(1 downto 0) & ln_tgl;
      lb_q <= lb(to_integer(rd_a));
      b_a <= fb_base;   b_b <= b_a;   if b_a = b_b then b_q <= b_b; end if;
      s_a <= fb_stride; s_b <= s_a;   if s_a = s_b then s_q <= s_b; end if;
      base_w   := unsigned(b_q(27 downto 1));            -- 16-bit units
      stride_w := unsigned(s_q(27 downto 1));
      r := resize(u_row, 27);                             -- a row is COLS units: 376 = 256 + 64 + 32 + 16 + 8
      row_w := shift_left(r, 8) + shift_left(r, 6) + shift_left(r, 5) + shift_left(r, 4) + shift_left(r, 3);
      if ui_rst = '1' then
        us <= U_IDLE; e_en_i <= '0';
      else
        case us is
          when U_IDLE =>
            if ln_s(2) /= ln_s(1) then
              u_row <= ln_row; u_wb <= ln_wb; u_half <= ln_half; ug <= (others => '0');
              us <= U_BASE;
            end if;
          when U_BASE =>                                  -- buffer base in 16-bit units
            case u_wb is
              when "00"   => buf_w <= base_w;
              when "01"   => buf_w <= base_w + stride_w;
              when "10"   => buf_w <= base_w + shift_left(stride_w, 1);
              when others => buf_w <= base_w + shift_left(stride_w, 1) + stride_w;
            end case;
            us <= U_RD;
          when U_RD =>
            rd_a <= u_half & ug; us <= U_W1;
          when U_W1 =>
            us <= U_W2;
          when U_W2 =>                                    -- lb_q holds the word
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
end architecture;
