"""W65C816 opcode table (256 entries) and the NMOS 6502 legal-opcode set.

Layout verified against the W65C816S datasheet opcode matrix (Table 5-4 / Table 7-1 style).
Modes: imp acc immM immX imm8 dp dpx dpy abs absx absy long longx ind(dp) indx((dp,X))
indy((dp),Y) ldp([dp]) ldpy([dp],Y) sr sry((sr,S),Y) absind absindx absindl rel rell blk pea pei per
"""

# ALU group: base opcode -> mnemonic (low nibble columns shared)
_ALU = {0x00: "ORA", 0x20: "AND", 0x40: "EOR", 0x60: "ADC",
        0x80: "STA", 0xA0: "LDA", 0xC0: "CMP", 0xE0: "SBC"}
_ALU_COLS = {0x01: "indx", 0x03: "sr", 0x05: "dp", 0x07: "ldp", 0x09: "immM",
             0x0D: "abs", 0x0F: "long", 0x11: "indy", 0x12: "ind", 0x13: "sry",
             0x15: "dpx", 0x17: "ldpy", 0x19: "absy", 0x1D: "absx", 0x1F: "longx"}

_OTHER = {
    0x00: ("BRK", "imm8"), 0x02: ("COP", "imm8"),
    0x04: ("TSB", "dp"), 0x06: ("ASL", "dp"), 0x08: ("PHP", "imp"), 0x0A: ("ASL", "acc"),
    0x0B: ("PHD", "imp"), 0x0C: ("TSB", "abs"), 0x0E: ("ASL", "abs"),
    0x10: ("BPL", "rel"), 0x14: ("TRB", "dp"), 0x16: ("ASL", "dpx"), 0x18: ("CLC", "imp"),
    0x1A: ("INC", "acc"), 0x1B: ("TCS", "imp"), 0x1C: ("TRB", "abs"), 0x1E: ("ASL", "absx"),
    0x20: ("JSR", "abs"), 0x22: ("JSL", "long"), 0x24: ("BIT", "dp"), 0x26: ("ROL", "dp"),
    0x28: ("PLP", "imp"), 0x2A: ("ROL", "acc"), 0x2B: ("PLD", "imp"), 0x2C: ("BIT", "abs"),
    0x2E: ("ROL", "abs"),
    0x30: ("BMI", "rel"), 0x34: ("BIT", "dpx"), 0x36: ("ROL", "dpx"), 0x38: ("SEC", "imp"),
    0x3A: ("DEC", "acc"), 0x3B: ("TSC", "imp"), 0x3C: ("BIT", "absx"), 0x3E: ("ROL", "absx"),
    0x40: ("RTI", "imp"), 0x42: ("WDM", "imm8"), 0x44: ("MVP", "blk"), 0x46: ("LSR", "dp"),
    0x48: ("PHA", "imp"), 0x4A: ("LSR", "acc"), 0x4B: ("PHK", "imp"), 0x4C: ("JMP", "abs"),
    0x4E: ("LSR", "abs"),
    0x50: ("BVC", "rel"), 0x54: ("MVN", "blk"), 0x56: ("LSR", "dpx"), 0x58: ("CLI", "imp"),
    0x5A: ("PHY", "imp"), 0x5B: ("TCD", "imp"), 0x5C: ("JML", "long"), 0x5E: ("LSR", "absx"),
    0x60: ("RTS", "imp"), 0x62: ("PER", "per"), 0x64: ("STZ", "dp"), 0x66: ("ROR", "dp"),
    0x68: ("PLA", "imp"), 0x6A: ("ROR", "acc"), 0x6B: ("RTL", "imp"), 0x6C: ("JMP", "absind"),
    0x6E: ("ROR", "abs"),
    0x70: ("BVS", "rel"), 0x74: ("STZ", "dpx"), 0x76: ("ROR", "dpx"), 0x78: ("SEI", "imp"),
    0x7A: ("PLY", "imp"), 0x7B: ("TDC", "imp"), 0x7C: ("JMP", "absindx"), 0x7E: ("ROR", "absx"),
    0x80: ("BRA", "rel"), 0x82: ("BRL", "rell"), 0x84: ("STY", "dp"), 0x86: ("STX", "dp"),
    0x88: ("DEY", "imp"), 0x8A: ("TXA", "imp"), 0x8B: ("PHB", "imp"), 0x8C: ("STY", "abs"),
    0x8E: ("STX", "abs"),
    0x90: ("BCC", "rel"), 0x94: ("STY", "dpx"), 0x96: ("STX", "dpy"), 0x98: ("TYA", "imp"),
    0x9A: ("TXS", "imp"), 0x9B: ("TXY", "imp"), 0x9C: ("STZ", "abs"), 0x9E: ("STZ", "absx"),
    0xA0: ("LDY", "immX"), 0xA2: ("LDX", "immX"), 0xA4: ("LDY", "dp"), 0xA6: ("LDX", "dp"),
    0xA8: ("TAY", "imp"), 0xAA: ("TAX", "imp"), 0xAB: ("PLB", "imp"), 0xAC: ("LDY", "abs"),
    0xAE: ("LDX", "abs"),
    0xB0: ("BCS", "rel"), 0xB4: ("LDY", "dpx"), 0xB6: ("LDX", "dpy"), 0xB8: ("CLV", "imp"),
    0xBA: ("TSX", "imp"), 0xBB: ("TYX", "imp"), 0xBC: ("LDY", "absx"), 0xBE: ("LDX", "absy"),
    0xC0: ("CPY", "immX"), 0xC2: ("REP", "imm8"), 0xC4: ("CPY", "dp"), 0xC6: ("DEC", "dp"),
    0xC8: ("INY", "imp"), 0xCA: ("DEX", "imp"), 0xCB: ("WAI", "imp"), 0xCC: ("CPY", "abs"),
    0xCE: ("DEC", "abs"),
    0xD0: ("BNE", "rel"), 0xD4: ("PEI", "pei"), 0xD6: ("DEC", "dpx"), 0xD8: ("CLD", "imp"),
    0xDA: ("PHX", "imp"), 0xDB: ("STP", "imp"), 0xDC: ("JML", "absindl"), 0xDE: ("DEC", "absx"),
    0xE0: ("CPX", "immX"), 0xE2: ("SEP", "imm8"), 0xE4: ("CPX", "dp"), 0xE6: ("INC", "dp"),
    0xE8: ("INX", "imp"), 0xEA: ("NOP", "imp"), 0xEB: ("XBA", "imp"), 0xEC: ("CPX", "abs"),
    0xEE: ("INC", "abs"),
    0xF0: ("BEQ", "rel"), 0xF4: ("PEA", "pea"), 0xF6: ("INC", "dpx"), 0xF8: ("SED", "imp"),
    0xFA: ("PLX", "imp"), 0xFB: ("XCE", "imp"), 0xFC: ("JSR", "absindx"), 0xFE: ("INC", "absx"),
}


def _build():
    t = [None] * 256
    for base, mn in _ALU.items():
        for col, mode in _ALU_COLS.items():
            t[base + col] = (mn, mode)
    t[0x89] = ("BIT", "immM")  # replaces STA #imm
    for op, e in _OTHER.items():
        assert t[op] is None, hex(op)
        t[op] = e
    missing = [hex(i) for i in range(256) if t[i] is None]
    assert not missing, missing
    return t


TABLE = _build()

# Operand byte count by mode (immM/immX handled by width state)
MODE_LEN = {"imp": 0, "acc": 0, "immM": None, "immX": None, "imm8": 1, "dp": 1, "dpx": 1,
            "dpy": 1, "abs": 2, "absx": 2, "absy": 2, "long": 3, "longx": 3, "ind": 1,
            "indx": 1, "indy": 1, "ldp": 1, "ldpy": 1, "sr": 1, "sry": 1, "absind": 2,
            "absindx": 2, "absindl": 2, "rel": 1, "rell": 2, "blk": 2, "pea": 2, "pei": 1,
            "per": 2}


def _legal6502():
    s = set()
    cols = (0x01, 0x05, 0x09, 0x0D, 0x11, 0x15, 0x19, 0x1D)
    for base in (0x00, 0x20, 0x40, 0x60, 0x80, 0xA0, 0xC0, 0xE0):
        for c in cols:
            s.add(base + c)
    s.discard(0x89)  # STA #imm does not exist on NMOS
    for base in (0x00, 0x20, 0x40, 0x60):  # ASL ROL LSR ROR
        for c in (0x06, 0x0A, 0x0E, 0x16, 0x1E):
            s.add(base + c)
    s.update((0xC6, 0xCE, 0xD6, 0xDE, 0xE6, 0xEE, 0xF6, 0xFE))      # DEC INC
    s.update((0x86, 0x8E, 0x96, 0xA2, 0xA6, 0xAE, 0xB6, 0xBE))      # STX LDX
    s.update((0x84, 0x8C, 0x94, 0xA0, 0xA4, 0xAC, 0xB4, 0xBC))      # STY LDY
    s.update((0xE0, 0xE4, 0xEC, 0xC0, 0xC4, 0xCC))                  # CPX CPY
    s.update((0x24, 0x2C))                                          # BIT
    s.update((0x10, 0x30, 0x50, 0x70, 0x90, 0xB0, 0xD0, 0xF0))      # branches
    s.update((0x4C, 0x6C, 0x20, 0x60, 0x40, 0x00))                  # JMP JSR RTS RTI BRK
    s.update((0x08, 0x18, 0x28, 0x38, 0x48, 0x58, 0x68, 0x78, 0x88, 0x8A, 0x98, 0x9A,
              0xA8, 0xAA, 0xB8, 0xBA, 0xC8, 0xCA, 0xD8, 0xE8, 0xEA, 0xF8))
    return frozenset(s)


LEGAL_6502 = _legal6502()
assert len(LEGAL_6502) == 151
