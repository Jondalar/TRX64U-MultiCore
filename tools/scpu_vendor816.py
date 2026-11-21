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
    16-field records; a registered 'STATE = 0' bit takes the place of the reset value of the output register (same behaviour: the row after a fetch is the constant);
  - in emulation mode (EF = 1) the modify cycle of a read-modify-write instruction writes the byte it read, then the last cycle writes the new byte, as the 6510 does and as
    VICE models the W65C816S (65816core.c:500-502 dp, :584 dp,X, :888-890 abs, :1032-1034 abs,X: `if (reg_emul) STORE_LONG(ea, var)`); upstream made it an internal cycle
    (VDA = VPA = 0, no write), so ASL $D019 / INC $D019 never acknowledged a VIC interrupt (T241). The instructions are the 28 of upstream's MLB list (`rmw`: ASL, ROL, LSR,
    ROR, DEC, INC on dp, dp,X, abs, abs,X; TSB, TRB on dp, abs), the set VICE gives that store; the cycle is the one row of each with STATE_CTRL 110 and VA 00 (the row
    after its 8-bit read, which has STATE_CTRL 110 and VA 10; every other row of these instructions has another state control). In it RMW_DW = 1: D_OUT = DR (the byte of
    the read, latched at its enable), WE = 0, VDA = 1, and the address increment is 0 (the row's is 01, its address would be the read's + 1). Native mode (EF = 0; VICE:
    LOAD_LONG_DUMMY, no store) and the number of cycles of every instruction are unchanged.
  - RTI in emulation mode (EF = 1) takes 6 cycles, as on the W65C816S (datasheet: 7 cycles with E = 0, 6 with E = 1) and in VICE (65816core.c:2217-2240: INC_PC makes no clock, two
    FETCH_PARAM_DUMMY, three PULL (four in native mode), one clock each, and the opcode fetch at the loop top, :2585/:2589); upstream gave it 5 (state control 111 of the third pull: `EF = 1 and IR = x"40"` ends it). The pull of the
    high byte of PC (state 4, STATE_CTRL 111) now goes on to state 7 in that case, and the microcode row of RTI's state 7 (all X upstream, read as zero, never visited: native RTI ends in state 6)
    becomes STATE_CTRL 010 with everything else 0: an internal cycle (no VDA, no VPA, no register load) that ends the instruction. Native mode (EF = 0, 7 cycles) is unchanged. T241 S2.
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


RTI_E_ROW = 0x40 * 8 + 6      # the microcode ROM is indexed by IR * 8 + (STATE - 1): row of opcode $40 (RTI) in state 7


def mcode(t):
    body = t[t.index('constant M_TAB'):t.index('type ALUCtrl_t')]
    rows = re.findall(r'^\s*\(((?:"[^"]*",?){16})\)', body, flags=re.M)
    assert len(rows) == 2048, len(rows)
    words, raw = [], []
    for r in rows:
        f = re.findall(r'"([^"]*)"', r)
        assert [len(x) for x in f] == FIELDS, f
        raw.append(''.join(f))
        words.append(raw[-1].replace('X', '0'))
    assert raw[RTI_E_ROW] == 'X' * 53, raw[RTI_E_ROW]      # upstream's row of RTI state 7 is all X (don't care): MCode.vhd, row after "-- 40 RTI" + 6
    words[RTI_E_ROW] = '010' + '0' * 50          # T241 S2: RTI's state 7 = the internal last cycle of the emulation-mode RTI (STATE_CTRL 010, nothing else; see cpu() rti_emul)
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


def sub1(t, old, new):
    assert t.count(old) == 1, old
    return t.replace(old, new, 1)


def rmw_write(t):
    """T241: the write of the unmodified byte in the modify cycle of a read-modify-write instruction in emulation mode (docstring above)"""
    t = sub1(t, "\tsignal RDY_IN_DELAYED : std_logic;\n",
             "\tsignal RDY_IN_DELAYED : std_logic;\n"
             "\tsignal RMW_DW : std_logic;\t\t-- T241: the modify cycle of a read-modify-write instruction in emulation mode writes the byte it read (tools/scpu_vendor816.py)\n")
    t = sub1(t, "\t\t\t\tDR when MC.OUT_BUS = \"110\" else\n",
             "\t\t\t\tDR when MC.OUT_BUS = \"110\" or RMW_DW = '1' else\n")
    t = sub1(t, "\tprocess(MC, IsResetInterrupt)\n\tbegin\n\t\tWE <= '1';\n\t\tif MC.OUT_BUS /= \"000\" and IsResetInterrupt = '0' then\n",
             "\tprocess(MC, IsResetInterrupt, RMW_DW)\n\tbegin\n\t\tWE <= '1';\n\t\tif (MC.OUT_BUS /= \"000\" or RMW_DW = '1') and IsResetInterrupt = '0' then\n")
    t = sub1(t, "IsNMIInterrupt, IsIRQInterrupt, IsCOPInterrupt)\n\tvariable ADDR_INC : unsigned(15 downto 0);\n\tbegin\n"
                "\t\tADDR_INC := (15 downto 2 => '0', 1 => MC.ADDR_INC(1), 0 => MC.ADDR_INC(0));\n",
             "IsNMIInterrupt, IsIRQInterrupt, IsCOPInterrupt, RMW_DW)\n\tvariable ADDR_INC : unsigned(15 downto 0);\n\tbegin\n"
             "\t\tADDR_INC := (15 downto 2 => '0', 1 => MC.ADDR_INC(1) and not RMW_DW, 0 => MC.ADDR_INC(0) and not RMW_DW);\t-- T241: the write goes to the read's address\n")
    t = sub1(t, "IsBRKInterrupt, IsCOPInterrupt, GotInterrupt )\n\t\tvariable rmw : std_logic;\n\t\tvariable twoCls, softInt : std_logic;\n",
             "IsBRKInterrupt, IsCOPInterrupt, GotInterrupt, EF )\n\t\tvariable rmw : std_logic;\n\t\tvariable twoCls, softInt : std_logic;\n\t\tvariable rmwWr : std_logic;\n")
    t = sub1(t, "\t\t\trmw := '0';\n\t\tend if;\n",
             "\t\t\trmw := '0';\n\t\tend if;\n"
             "\t\t-- T241: emulation mode, the modify cycle (state control 110 without VDA/VPA, the row after the 8-bit read): write the byte read, as VICE's 65816 in emulation mode\n"
             "\t\tif rmw = '1' and EF = '1' and MC.STATE_CTRL = \"110\" and MC.VA = \"00\" then\n"
             "\t\t\trmwWr := '1';\n"
             "\t\telse\n"
             "\t\t\trmwWr := '0';\n"
             "\t\tend if;\n"
             "\t\tRMW_DW <= rmwWr;\n")
    t = sub1(t, "\t\tVDA <= MC.VA(1);\n", "\t\tVDA <= MC.VA(1) or rmwWr;\n")
    return t


def rti_emul(t):
    """T241 S2: RTI in emulation mode takes 6 cycles (docstring above): after the pull of PCH (state 4) one more internal cycle, state 7 (the row patched in mcode())"""
    return sub1(t, "\t\t\t\telsif EF = '1' and IR = x\"40\" then\t--RTI Emulation mode\n\t\t\t\t\tNextState <= \"0000\";\n",
                "\t\t\t\telsif EF = '1' and IR = x\"40\" then\t--RTI Emulation mode (T241: one internal cycle more, state 7)\n\t\t\t\t\tNextState <= \"0111\";\n")


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
    t = rmw_write(t)
    t = rti_emul(t)
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
