#!/usr/bin/env python3
"""T89.1 S13 bench helper: write a copy of apple2.vhd that logs every CPU bus cycle of a time window to trace.txt (simulation only, never built for the board).
   trace_patch.py <apple2.vhd> <out.vhd> <t0_ms> <t1_ms>
   a line: <us> <addr> R|W <data> S(=opcode fetch)|. then the switches RAMRD RAMWRT ALTZP 80STORE PAGE2 HRAM_READ HRAM_WR_N HRAM_BANK1 aux CXROM (1/0)"""
import sys
src, dst, t0, t1 = sys.argv[1], sys.argv[2], float(sys.argv[3]), float(sys.argv[4])
s = open(src).read()
def once(old, new):
    global s
    assert s.count(old) == 1, old
    s = s.replace(old, new)
once("use ieee.numeric_std.all;\n", "use ieee.numeric_std.all;\nuse std.textio.all;\n")
once("  signal R65C02_WE_N : std_logic := '0';\n", "  signal R65C02_WE_N : std_logic := '0';\n  signal DBG_SYNC : std_logic;\n")
once("sync => open,", "sync => DBG_SYNC,")
proc = f"""
  trace_p : process (CLK_14M)
    file tf : text open write_mode is "trace.txt";
    variable ln : line;
    variable rw, sy : string(1 to 1);
    variable dv : unsigned(7 downto 0);
  begin
    if rising_edge(CLK_14M) then
      if CPU_EN = '1' and CE_14M = '1' and CPU_WAIT = '0' and now >= {int(t0*1000)} us and now < {int(t1*1000)} us then
        rw := "R"; dv := D_IN; if we = '1' then rw := "W"; dv := R65C02_DO; end if;
        sy := "."; if DBG_SYNC = '1' then sy := "S"; end if;
        write(ln, integer'image(now / 1 us) & " " & to_hstring(R65C02_A) & " " & rw & " " & to_hstring(dv) & " " & sy & " "
          & std_logic'image(RAMRD)(2) & std_logic'image(RAMWRT)(2) & std_logic'image(ALTZP)(2) & std_logic'image(STORE80)(2) & std_logic'image(PAGE2)(2) & " "
          & std_logic'image(HRAM_READ)(2) & std_logic'image(HRAM_WR_N)(2) & std_logic'image(HRAM_BANK1)(2) & " "
          & std_logic'image(aux)(2) & std_logic'image(CXROM)(2));
        writeline(tf, ln);
      end if;
    end if;
  end process;

end rtl;"""
i = s.rindex("end rtl;")
s = s[:i].rstrip() + "\n" + proc + s[i + len("end rtl;"):]
open(dst, "w").write(s)
