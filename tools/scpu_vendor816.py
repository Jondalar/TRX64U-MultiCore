#!/usr/bin/env python3
"""T182 step 2: the C64SCPU core's own copy of the MiSTer 65C816 (cores/snes/rtl/65C816, GPL-3.0, the SNES core keeps its copy untouched).
  python3 tools/scpu_vendor816.py            writes cores/c64scpu/cpu/*.vhd
  python3 tools/scpu_vendor816.py --check    fails when cores/c64scpu/cpu is not what this script makes of cores/snes/rtl/65C816
Changes against the upstream files (all of them, nothing else is touched):
  - names: every entity/package gets the prefix s816_ and the CPU is s816_cpu (all lower case: the ghdl netlist and Vivado's mixed-language instance agree on one
    spelling, and the 6502n's `alu` in library work does not meet the 65C816's `ALU`);
  - every `case` over all binary values that ends in `when others => null;` ends in `when others =>` instead (tools/snes_gen.py exhaustive_cases; the VHDL means the same, GHDL
    would make latches of it);
  - the CPU's ports are spelled in lower case (the Verilog that instantiates it, and the ghdl netlist, use one spelling);
  - the CPU has one more output, e_out = the emulation flag (EF), for $D0B6 bit 7 and the vector reroute (T182 T2);
  - the XCE branch of X, Y, SP (IR = $FB, P(0), MC.LOAD_P = 101) is gated with EN like every other write of those registers (T182 step 3a: it wrote constants in every clk2x cycle);
  - the P byte pushed by an interrupt has bit 4 (B) = 0 in emulation mode (upstream pushed P(4), which is 1 there: IRQ and NMI looked like BRK; BRK, COP, PHP still push 1;
    native mode pushes the X flag as before) (T182, real-ROM boot);
  - the microcode ROM is one flat array of 2048 x 53 bits with a registered output and no reset on it (rom_style block: 3 tiles, T182 5.1 finding), not an array of
    16-field records; a registered 'STATE = 0' bit takes the place of the reset value of the output register (same behaviour: the row after a fetch is the constant).
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import snes_gen  # noqa: E402  (exhaustive_cases: the same GHDL fix the SNES core's generator makes)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'cores/snes/rtl/65C816')
DST = os.path.join(ROOT, 'cores/c64scpu/cpu')

RENAME = [('P65816_pkg', 's816_pkg'), ('P65C816', 's816_cpu'), ('MCode', 's816_mcode'), ('AddrGen', 's816_addrgen'), ('ALU', 's816_alu'),
          ('AddSubBCD', 's816_addsubbcd'), ('BCDAdder', 's816_bcdadder'), ('bit_adder', 's816_bit_adder'), ('adder4', 's816_adder4')]
FILES = {'P65816_pkg.vhd': 's816_pkg.vhd', 'P65C816.vhd': 's816_cpu.vhd', 'MCode.vhd': 's816_mcode.vhd', 'AddrGen.vhd': 's816_addrgen.vhd',
         'ALU.vhd': 's816_alu.vhd', 'AddSubBCD.vhd': 's816_addsubbcd.vhd', 'BCDAdder.vhd': 's816_bcdadder.vhd'}
FIELDS = [3, 4, 2, 3, 2, 2, 8, 3, 3, 3, 2, 6, 5, 2, 3, 2]       # the record MicroInst_r of P65816_pkg.vhd, in order; sum = 53
HEAD = "-- T182 step 2: own copy of cores/snes/rtl/65C816/%s (MiSTer SNES, GPL-3.0), made by tools/scpu_vendor816.py - do not edit, change the script.\n"


def rename(t):
    for a, b in RENAME:
        t = re.sub(r'(?<![A-Za-z0-9_])%s(?![A-Za-z0-9_])' % re.escape(a), b, t, flags=re.I)
    return t


def mcode(t):
    body = t[t.index('constant M_TAB'):t.index('type ALUCtrl_t')]
    rows = re.findall(r'^\s*\(((?:"[^"]*",?){16})\)', body, flags=re.M)
    assert len(rows) == 2048, len(rows)
    words = []
    for r in rows:
        f = re.findall(r'"([^"]*)"', r)
        assert [len(x) for x in f] == FIELDS, f
        words.append(''.join(f).replace('X', '0'))
    lines = ',\n'.join('\t\t"%s"' % w for w in words)
    alu = t[t.index('type ALUCtrl_t'):t.index('signal MI ')]
    reset = '000' + '0000' + '00' + '000' + '00' + '00' + '00000000' + '001' + '000' + '000' + '00' + '000000' + '00000' + '00' + '000' + '11'
    assert len(reset) == 53
    cuts, o = [], 0
    for w in FIELDS:
        cuts.append((o, o + w))
        o += w
    names = ['stateCtrl', 'addrBus', 'addrInc', 'loadP', 'loadT', 'muxCtrl', 'addrCtrl', 'loadPC', 'loadSP', 'regAXY', 'loadDKB', 'busCtrl', 'ALUCtrl', 'byteSel',
             'outBus', 'va']
    assign = '\n'.join('\tMI.%s <= MIV(%d downto %d);' % (n, 52 - a, 52 - b + 1) for n, (a, b) in zip(names, cuts))
    head = t[:t.index('architecture rtl')]
    return head + '''architecture rtl of s816_mcode is

	type rom_t is array(0 to 2047) of std_logic_vector(52 downto 0);
	constant M_TAB: rom_t := (
%s
	);
	attribute rom_style : string;
	attribute rom_style of M_TAB : constant is "block";

%s
	constant FETCH : std_logic_vector(52 downto 0) := "%s";
	signal MI      : MicroInst_r;
	signal MIV     : std_logic_vector(52 downto 0);
	signal ROMQ    : std_logic_vector(52 downto 0);
	signal ST0     : std_logic;
	signal STATE2  : unsigned(3 downto 0);
	signal ALUFlags : ALUCtrl_r;

begin

	ALUFlags <= ALU_TAB(to_integer(unsigned(MI.ALUCtrl)));
	STATE2 <= STATE - 1;

	-- the table is read into a register without a reset (a block RAM has none); ST0 says that the row of the last cycle is the fetch row
	process(CLK)
	begin
		if rising_edge(CLK) then
			if EN = '1' then
				ROMQ <= M_TAB(to_integer(unsigned(IR) & STATE2(2 downto 0)));
			end if;
		end if;
	end process;

	process(CLK, RST_N)
	begin
		if RST_N = '0' then
			ST0 <= '1';
		elsif rising_edge(CLK) then
			if EN = '1' then
				if STATE = "0000" then ST0 <= '1'; else ST0 <= '0'; end if;
			end if;
		end if;
	end process;

	MIV <= FETCH when ST0 = '1' else ROMQ;
%s

	M <= (ALUFlags, MI.stateCtrl, MI.addrBus, MI.addrInc, MI.muxCtrl, MI.addrCtrl, MI.loadPC, MI.loadSP,
			MI.regAXY, MI.loadP, MI.loadT, MI.loadDKB, MI.busCtrl, MI.byteSel, MI.outBus, MI.va);

end rtl;
''' % (lines, alu.rstrip() + '\n', reset, assign)


def cpu(t):
    n = len(t)
    t = re.sub(r'(RDY_OUT\s*:\s*out std_logic;)', r'\1\n\t\t  E_OUT \t: out std_logic;', t, count=1)
    assert len(t) > n
    n = len(t)
    t = t.replace("RDY_OUT <= EN;", "RDY_OUT <= EN;\n\tE_OUT <= EF;", 1)
    assert len(t) > n
    old = "if (IR = x\"FB\" and P(0) = '1' and MC.LOAD_P = \"101\") then"
    assert t.count(old) == 1
    t = t.replace(old, "if (EN = '1' and IR = x\"FB\" and P(0) = '1' and MC.LOAD_P = \"101\") then", 1)     # T182 step 3a: the XCE branch of A/X/Y/SP writes only on the enable (the multicycle constraint is then exact)
    old = "(P(4) or (not (GotInterrupt) and EF))"
    assert t.count(old) == 1
    t = t.replace(old, "(P(4) and not (GotInterrupt and EF))", 1)     # T182 (real ROM, 2026-11-08): P(4) is 1 all through emulation mode, so a hardware IRQ/NMI pushed B = 1 and the KERNAL's IRQ entry ($FF48) took every IRQ for a BRK
    i = t.index('port(')
    j = t.index('end P65C816;') if 'end P65C816;' in t else t.index('end s816_cpu;')
    t = t[:i] + t[i:j].lower().replace('std_logic_vector', 'std_logic_vector') + t[j:]      # port names in lower case: the Verilog that instantiates the entity spells them so
    return t


def make():
    out = {}
    for s, d in FILES.items():
        t = open(os.path.join(SRC, s)).read()
        if s == 'MCode.vhd':
            t = mcode(rename(t))
        elif s == 'P65C816.vhd':
            t = rename(cpu(t))
        else:
            t = rename(t)
        t = snes_gen.exhaustive_cases(t, s)
        out[d] = HEAD % s + t
    return out


if __name__ == '__main__':
    out = make()
    if '--check' in sys.argv:
        bad = [d for d, t in out.items() if not os.path.exists(os.path.join(DST, d)) or open(os.path.join(DST, d)).read() != t]
        print('FAIL: %s differ from the script' % ', '.join(bad) if bad else 'ok: cores/c64scpu/cpu is what tools/scpu_vendor816.py makes')
        sys.exit(1 if bad else 0)
    os.makedirs(DST, exist_ok=True)
    for d, t in out.items():
        open(os.path.join(DST, d), 'w').write(t)
    print('wrote', len(out), 'files into', DST)
