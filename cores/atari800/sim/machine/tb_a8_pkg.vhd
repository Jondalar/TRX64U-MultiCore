-- SPDX-License-Identifier: GPL-3.0-or-later
-- T88: what the whole-machine benches of the Atari core share: a model of the host's DDR2 window (one protected memory for the
-- two ports c_* and e_*), the host's side of the app's window, and a few helpers. The memory is sparse: the 16 MB window of
-- the core (byte 0x1000000 on) is seen as three pieces - the extra RAM at +0 (1 MB), the cartridge at +0x400000 (256 KB) and
-- the four frame buffers at +0xD00000 + n * 0x100000 (256 KB each); anything else is a bench error.
library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use std.textio.all;

package tb_a8_pkg is
  type ddr_t is protected
    procedure wr128(offset : natural; data : std_logic_vector(127 downto 0); mask : std_logic_vector(15 downto 0));  -- mask 1 = keep
    impure function rd128(offset : natural) return std_logic_vector;
    procedure wr8(offset : natural; d : std_logic_vector(7 downto 0));
    impure function rd8(offset : natural) return std_logic_vector;
    impure function errors return natural;
  end protected;
  shared variable ddr : ddr_t;

  impure function hex_byte(l : string) return std_logic_vector;
  procedure load_hex(file_name : string; base : natural; kind : integer; n : out natural);   -- kind 0: into the DDR2 window at base
end package;

package body tb_a8_pkg is
  type ddr_t is protected body
    constant L_X : natural := 65536;
    constant L_C : natural := 16384;
    constant L_F : natural := 16384;
    type arr_x is array (0 to L_X - 1) of std_logic_vector(127 downto 0);
    type arr_c is array (0 to L_C - 1) of std_logic_vector(127 downto 0);
    type arr_f is array (0 to 4 * L_F - 1) of std_logic_vector(127 downto 0);
    variable mx : arr_x := (others => (others => '0'));
    variable mc : arr_c := (others => (others => '0'));
    variable mf : arr_f := (others => (others => '0'));
    variable errs : natural := 0;

    -- returns 0 extra RAM, 1 cartridge, 2 frame buffers; idx = the line index in that array, -1 = outside
    procedure map_line(offset : natural; region : out integer; idx : out integer) is
      variable ln : integer := offset / 16;
      variable b  : integer;
    begin
      region := -1; idx := -1;
      if offset < 16#100000# then region := 0; idx := ln;
      elsif offset >= 16#400000# and offset < 16#400000# + L_C * 16 then region := 1; idx := (offset - 16#400000#) / 16;
      elsif offset >= 16#D00000# and offset < 16#D00000# + 4 * 16#100000# then
        b := (offset - 16#D00000#) / 16#100000#;
        if (offset - 16#D00000#) mod 16#100000# < L_F * 16 then region := 2; idx := b * L_F + ((offset - 16#D00000#) mod 16#100000#) / 16; end if;
      end if;
      if idx < 0 then
        errs := errs + 1;
        report "DDR2 model: window offset 0x" & to_hstring(to_unsigned(offset, 32)) & " is outside the pieces the bench models" severity warning;
        region := 0; idx := 0;
      end if;
    end procedure;

    impure function rd128(offset : natural) return std_logic_vector is
      variable r, i : integer;
    begin
      map_line(offset, r, i);
      case r is
        when 0 => return mx(i);
        when 1 => return mc(i);
        when others => return mf(i);
      end case;
    end function;

    procedure wr128(offset : natural; data : std_logic_vector(127 downto 0); mask : std_logic_vector(15 downto 0)) is
      variable r, i : integer;
      variable v : std_logic_vector(127 downto 0);
    begin
      map_line(offset, r, i);
      case r is when 0 => v := mx(i); when 1 => v := mc(i); when others => v := mf(i); end case;
      for k in 0 to 15 loop
        if mask(k) = '0' then v(8 * k + 7 downto 8 * k) := data(8 * k + 7 downto 8 * k); end if;
      end loop;
      case r is when 0 => mx(i) := v; when 1 => mc(i) := v; when others => mf(i) := v; end case;
    end procedure;

    procedure wr8(offset : natural; d : std_logic_vector(7 downto 0)) is
      variable m : std_logic_vector(15 downto 0) := (others => '1');
      variable dd : std_logic_vector(127 downto 0) := (others => '0');
      variable k : natural := offset mod 16;
    begin
      m(k) := '0';
      dd(8 * k + 7 downto 8 * k) := d;
      wr128(offset - k, dd, m);
    end procedure;

    impure function rd8(offset : natural) return std_logic_vector is
      variable v : std_logic_vector(127 downto 0);
      variable k : natural := offset mod 16;
    begin
      v := rd128(offset - k);
      return v(8 * k + 7 downto 8 * k);
    end function;

    impure function errors return natural is begin return errs; end function;
  end protected body;

  impure function hex_byte(l : string) return std_logic_vector is
    variable v : std_logic_vector(7 downto 0) := (others => '0');
    variable d : integer;
  begin
    for i in l'range loop
      case l(i) is
        when '0' to '9' => d := character'pos(l(i)) - character'pos('0');
        when 'A' to 'F' => d := character'pos(l(i)) - character'pos('A') + 10;
        when 'a' to 'f' => d := character'pos(l(i)) - character'pos('a') + 10;
        when others => d := -1;
      end case;
      if d >= 0 then v := v(3 downto 0) & std_logic_vector(to_unsigned(d, 4)); end if;
    end loop;
    return v;
  end function;

  procedure load_hex(file_name : string; base : natural; kind : integer; n : out natural) is
    file f : text;
    variable l : line;
    variable cnt : natural := 0;
    variable st : file_open_status;
  begin
    file_open(st, f, file_name, read_mode);
    assert st = open_ok report "cannot open " & file_name severity failure;
    while not endfile(f) loop
      readline(f, l);
      if l'length >= 2 then
        ddr.wr8(base + cnt, hex_byte(l.all(1 to 2)));
        cnt := cnt + 1;
      end if;
    end loop;
    file_close(f);
    n := cnt;
  end procedure;
end package body;
