#!/usr/bin/env python3
"""W65C816 flow-following disassembler (project-local, Python 3 stdlib only).

Run:  python3 -I tools/dis65816/dis65816.py --config tools/dis65816/configs/game.json --verify
See README.md for options and limits.
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile

# --------------------------------------------------------------------------
# Opcode table: mnemonic:mode, 16 entries per row, rows $00..$F0.
# Modes: imp acc imm(M-width) immx(X-width) imm8 dp dpx dpy abs absx absy long
#        longx ind indx indy indl indly absind absindx absindl rel rell sr sry
#        blk pea pei per
# --------------------------------------------------------------------------
_ROWS = """
BRK:imm8 ORA:indx COP:imm8 ORA:sr TSB:dp ORA:dp ASL:dp ORA:indl PHP:imp ORA:imm ASL:acc PHD:imp TSB:abs ORA:abs ASL:abs ORA:long
BPL:rel ORA:indy ORA:ind ORA:sry TRB:dp ORA:dpx ASL:dpx ORA:indly CLC:imp ORA:absy INC:acc TCS:imp TRB:abs ORA:absx ASL:absx ORA:longx
JSR:abs AND:indx JSL:long AND:sr BIT:dp AND:dp ROL:dp AND:indl PLP:imp AND:imm ROL:acc PLD:imp BIT:abs AND:abs ROL:abs AND:long
BMI:rel AND:indy AND:ind AND:sry BIT:dpx AND:dpx ROL:dpx AND:indly SEC:imp AND:absy DEC:acc TSC:imp BIT:absx AND:absx ROL:absx AND:longx
RTI:imp EOR:indx WDM:imm8 EOR:sr MVP:blk EOR:dp LSR:dp EOR:indl PHA:imp EOR:imm LSR:acc PHK:imp JMP:abs EOR:abs LSR:abs EOR:long
BVC:rel EOR:indy EOR:ind EOR:sry MVN:blk EOR:dpx LSR:dpx EOR:indly CLI:imp EOR:absy PHY:imp TCD:imp JML:long EOR:absx LSR:absx EOR:longx
RTS:imp ADC:indx PER:per ADC:sr STZ:dp ADC:dp ROR:dp ADC:indl PLA:imp ADC:imm ROR:acc RTL:imp JMP:absind ADC:abs ROR:abs ADC:long
BVS:rel ADC:indy ADC:ind ADC:sry STZ:dpx ADC:dpx ROR:dpx ADC:indly SEI:imp ADC:absy PLY:imp TDC:imp JMP:absindx ADC:absx ROR:absx ADC:longx
BRA:rel STA:indx BRL:rell STA:sr STY:dp STA:dp STX:dp STA:indl DEY:imp BIT:imm TXA:imp PHB:imp STY:abs STA:abs STX:abs STA:long
BCC:rel STA:indy STA:ind STA:sry STY:dpx STA:dpx STX:dpy STA:indly TYA:imp STA:absy TXS:imp TXY:imp STZ:abs STA:absx STZ:absx STA:longx
LDY:immx LDA:indx LDX:immx LDA:sr LDY:dp LDA:dp LDX:dp LDA:indl TAY:imp LDA:imm TAX:imp PLB:imp LDY:abs LDA:abs LDX:abs LDA:long
BCS:rel LDA:indy LDA:ind LDA:sry LDY:dpx LDA:dpx LDX:dpy LDA:indly CLV:imp LDA:absy TSX:imp TYX:imp LDY:absx LDA:absx LDX:absy LDA:longx
CPY:immx CMP:indx REP:imm8 CMP:sr CPY:dp CMP:dp DEC:dp CMP:indl INY:imp CMP:imm DEX:imp WAI:imp CPY:abs CMP:abs DEC:abs CMP:long
BNE:rel CMP:indy CMP:ind CMP:sry PEI:pei CMP:dpx DEC:dpx CMP:indly CLD:imp CMP:absy PHX:imp STP:imp JML:absindl CMP:absx DEC:absx CMP:longx
CPX:immx SBC:indx SEP:imm8 SBC:sr CPX:dp SBC:dp INC:dp SBC:indl INX:imp SBC:imm NOP:imp XBA:imp CPX:abs SBC:abs INC:abs SBC:long
BEQ:rel SBC:indy SBC:ind SBC:sry PEA:pea SBC:dpx INC:dpx SBC:indly SED:imp SBC:absy PLX:imp XCE:imp JSR:absindx SBC:absx INC:absx SBC:longx
"""
OPCODES = []
for _row in _ROWS.strip().splitlines():
    for _tok in _row.split():
        OPCODES.append(tuple(_tok.split(":")))
assert len(OPCODES) == 256

FIXED_LEN = {
    "imp": 1, "acc": 1, "imm8": 2, "dp": 2, "dpx": 2, "dpy": 2, "abs": 3,
    "absx": 3, "absy": 3, "long": 4, "longx": 4, "ind": 2, "indx": 2,
    "indy": 2, "indl": 2, "indly": 2, "absind": 3, "absindx": 3,
    "absindl": 3, "rel": 2, "rell": 3, "sr": 2, "sry": 2, "blk": 3,
    "pea": 3, "pei": 2, "per": 3,
}
CARRY_MODS = {"ADC", "SBC", "CMP", "CPX", "CPY", "ASL", "LSR", "ROL", "ROR"}
COND_BRANCH = {"BPL", "BMI", "BVC", "BVS", "BCC", "BCS", "BNE", "BEQ"}

# ------------------------------- I/O names ---------------------------------
IO_NAMES = {}
_vic = ["M0X", "M0Y", "M1X", "M1Y", "M2X", "M2Y", "M3X", "M3Y", "M4X", "M4Y",
        "M5X", "M5Y", "M6X", "M6Y", "M7X", "M7Y", "MSIGX", "CTRL1", "RASTER",
        "LPX", "LPY", "SPENA", "CTRL2", "YXPAND", "MEMPTR", "IRQFLAG", "IRQMASK",
        "SPBGPR", "SPMC", "XXPAND", "SPSPCL", "SPBGCL", "EXTCOL", "BGCOL0",
        "BGCOL1", "BGCOL2", "BGCOL3", "SPMC0", "SPMC1", "SP0COL", "SP1COL",
        "SP2COL", "SP3COL", "SP4COL", "SP5COL", "SP6COL", "SP7COL"]
for _i, _n in enumerate(_vic):
    IO_NAMES[0xD000 + _i] = "VIC_" + _n
_sid = []
for _v in (1, 2, 3):
    _sid += ["V%d_FREQ_LO" % _v, "V%d_FREQ_HI" % _v, "V%d_PW_LO" % _v,
             "V%d_PW_HI" % _v, "V%d_CTRL" % _v, "V%d_AD" % _v, "V%d_SR" % _v]
_sid += ["FC_LO", "FC_HI", "RES_FILT", "MODE_VOL", "POTX", "POTY", "OSC3", "ENV3"]
for _i, _n in enumerate(_sid):
    IO_NAMES[0xD400 + _i] = "SID_" + _n
_cia = ["PRA", "PRB", "DDRA", "DDRB", "TA_LO", "TA_HI", "TB_LO", "TB_HI",
        "TOD_10THS", "TOD_SEC", "TOD_MIN", "TOD_HR", "SDR", "ICR", "CRA", "CRB"]
for _i, _n in enumerate(_cia):
    IO_NAMES[0xDC00 + _i] = "CIA1_" + _n
    IO_NAMES[0xDD00 + _i] = "CIA2_" + _n
for _a in range(0xD070, 0xD080):
    IO_NAMES[_a] = "SCPU_%04X" % _a
for _a in range(0xD0B0, 0xD0C0):
    IO_NAMES[_a] = "SCPU_%04X" % _a
IO_NAMES[0xD07A] = "SCPU_D07A_ENABLE_SYSTEM_SPEED"
IO_NAMES[0xD07B] = "SCPU_D07B_ENABLE_TURBO_SPEED"
IO_NAMES[0xD07E] = "SCPU_D07E_ENABLE_HW_REGS"
IO_NAMES[0xD07F] = "SCPU_D07F_DISABLE_HW_REGS"


def io_name(a):
    if a in IO_NAMES:
        return IO_NAMES[a]
    if 0xD800 <= a <= 0xDBFF:
        return "COLOR_RAM+$%03X" % (a - 0xD800)
    if 0xDE00 <= a <= 0xDEFF:
        return "IO1+$%02X" % (a - 0xDE00)
    if 0xDF00 <= a <= 0xDFFF:
        return "IO2+$%02X" % (a - 0xDF00)
    return None


# ------------------------------- state --------------------------------------
# state = (e, m, x, c, p); each 0, 1 or None (unknown).
#   c = carry (needed for XCE); p = abstract push stack (only with track_php):
#   a tuple of items, each None (non-PHP push) or (e, m, x) of a PHP; None = unknown.

def norm(e, m, x, c=None, p=None):
    if e == 1:
        m = x = 1
    elif (m == 0 or x == 0) and e is None:
        e = 0
    return (e, m, x, c, p)


def join_p(p, q):
    if p == q:
        return p
    if p is None or q is None or len(p) != len(q):
        return None
    out = []
    for u, v in zip(p, q):
        if u is None and v is None:
            out.append(None)
        elif u is None or v is None:
            return None
        else:
            out.append(tuple(a if a == b else None for a, b in zip(u, v)))
    return tuple(out)


def join_state(a, b):
    j = [u if u == v else None for u, v in zip(a[:4], b[:4])]
    return norm(j[0], j[1], j[2], j[3], join_p(a[4], b[4]))


def parse_state(spec):
    """'e=1,m=8,x=16' -> normalized state tuple."""
    d = {"e": None, "m": None, "x": None}
    for part in spec.split(","):
        part = part.strip()
        if not part:
            continue
        k, v = part.split("=")
        k = k.strip().lower()
        v = int(v.strip())
        if k == "e":
            d["e"] = v
        elif k in ("m", "x"):
            if v not in (8, 16):
                raise ValueError("%s must be 8 or 16" % k)
            d[k] = 1 if v == 8 else 0
        else:
            raise ValueError("unknown state key " + k)
    return norm(d["e"], d["m"], d["x"], None, None)


def state_str(st):
    e, m, x = st[0], st[1], st[2]
    f = lambda v, n: n + ("?" if v is None else ("8" if v else "16"))
    return "e%s %s %s" % ("?" if e is None else e, f(m, "m"), f(x, "x"))


def state_dict(st):
    return {"e": st[0], "m": None if st[1] is None else (8 if st[1] else 16),
            "x": None if st[2] is None else (8 if st[2] else 16)}


def xce_state(e, m, x, c, p):
    ne, nc = c, e
    if ne == 1:
        nm = nx = 1
    elif ne == 0:
        if e == 1:
            nm = nx = 1
        elif e == 0:
            nm, nx = m, x
        else:
            nm = 1 if m == 1 else None
            nx = 1 if x == 1 else None
    else:
        if e == 1:
            nm = nx = 1
        else:
            nm = 1 if m == 1 else None
            nx = 1 if x == 1 else None
    return norm(ne, nm, nx, nc, p)


PUSHES = {"PHA", "PHX", "PHY", "PHB", "PHD", "PHK", "PEA", "PEI", "PER"}
PULLS = {"PLA", "PLX", "PLY", "PLB", "PLD"}


def apply_state(mn, operand, st, track=False):
    """State after executing mnemonic. track=True enables PHP/PLP pairing."""
    e, m, x, c, p = st
    if not track:
        p = None
    if mn == "CLC":
        c = 0
    elif mn == "SEC":
        c = 1
    elif mn == "XCE":
        return xce_state(e, m, x, c, p)
    elif mn in ("REP", "SEP"):
        setting = 1 if mn == "SEP" else 0
        if operand & 1:
            c = setting
        if e != 1:
            for bit, name in ((0x20, "m"), (0x10, "x")):
                if operand & bit:
                    cur = m if name == "m" else x
                    if e == 0:
                        new = setting
                    else:  # e unknown
                        new = 1 if setting == 1 else (0 if cur == 0 else None)
                    if name == "m":
                        m = new
                    else:
                        x = new
    elif mn == "PHP":
        if track and p is not None:
            p = p + ((e, m, x),)
    elif mn in PUSHES:
        if track and p is not None:
            p = p + (None,)
    elif mn in PULLS:
        if track and p is not None:
            p = p[:-1] if p else None
    elif mn in ("TXS", "TCS"):
        p = None
    elif mn in ("PLP", "RTI"):
        c = None
        item = None
        if mn == "PLP" and track and p:
            item = p[-1]
            p = p[:-1]
        elif track:
            p = None
        if item is not None and e is not None and item[0] == e:
            m, x = item[1], item[2]
            if e == 1:
                m = x = 1
        elif e != 1:
            m = x = None
    elif mn in CARRY_MODS:
        c = None
    return norm(e, m, x, c, p)


# ------------------------------- image ---------------------------------------
class Image:
    def __init__(self, data, load, bank=0, aliases=None):
        self.data = data
        self.load = load
        self.bank = bank
        self.aliases = aliases or []   # list of (virt, phys, length), 16-bit addrs

    def phys_of(self, a):
        """24-bit address -> offset into data, or None."""
        if (a >> 16) != self.bank:
            return None
        a16 = a & 0xFFFF
        for virt, phys, ln in self.aliases:
            if virt <= a16 < virt + ln:
                off = phys - self.load + (a16 - virt)
                return off if 0 <= off < len(self.data) else None
        off = a16 - self.load
        return off if 0 <= off < len(self.data) else None

    def byte_at(self, a):
        o = self.phys_of(a)
        return None if o is None else self.data[o]

    def is_virtual(self, a):
        a16 = a & 0xFFFF
        return any(v <= a16 < v + ln for v, p, ln in self.aliases)


# ------------------------------- decoding ------------------------------------
class Stop(Exception):
    pass


class Insn:
    __slots__ = ("addr", "phys", "length", "bytes", "mnemonic", "mode",
                 "operand", "state_in", "state_out", "flow", "targets",
                 "ext_targets", "unresolved", "dest_bank", "src_bank",
                 "operand_target")

    def to_json(self, labels):
        d = {
            "addr": self.addr, "addr_hex": addr_hex(self.addr),
            "bytes": " ".join("%02X" % b for b in self.bytes),
            "length": self.length,
            "mnemonic": self.mnemonic, "mode": self.mode,
            "operand": self.operand,
            "text": fmt_insn(self, labels),
            "state_in": state_dict(self.state_in),
            "state_out": state_dict(self.state_out),
            "flow": self.flow,
            "targets": [{"addr": t, "addr_hex": addr_hex(t), "kind": k}
                        for t, k in self.targets],
        }
        if self.ext_targets:
            d["external_targets"] = [{"addr": t, "addr_hex": addr_hex(t), "kind": k}
                                     for t, k in self.ext_targets]
        if self.unresolved:
            d["unresolved_indirect"] = True
        if self.mode == "blk":
            d["dest_bank"] = self.dest_bank
            d["src_bank"] = self.src_bank
        return d


def addr_hex(a):
    return "%04X" % a if a < 0x10000 else "%06X" % a


def label_name(a):
    return "L_" + addr_hex(a)


def fmt_insn(i, labels, with_io=False):
    mn, mode, op = i.mnemonic, i.mode, i.operand
    h2 = lambda v: "$%02X" % v
    h4 = lambda v: "$%04X" % v
    h6 = lambda v: "$%06X" % v
    tgt = None
    if mode in ("rel", "rell", "per") or (mn in ("JMP", "JSR") and mode == "abs") \
            or (mn in ("JML", "JSL") and mode == "long"):
        tgt = i.operand_target
    if mode == "imp" or mode == "acc":
        return mn + (" A" if mode == "acc" else "")
    if mode == "imm":
        return "%s #%s" % (mn, h2(op) if i.length == 2 else h4(op))
    if mode == "immx":
        return "%s #%s" % (mn, h2(op) if i.length == 2 else h4(op))
    if mode == "imm8":
        return "%s #%s" % (mn, h2(op))
    if tgt is not None:
        name = label_name(tgt) if tgt in labels else (h4(tgt) if tgt < 0x10000 else h6(tgt))
        return "%s %s" % (mn, name)
    fm = {
        "dp": "%s %s", "dpx": "%s %s,X", "dpy": "%s %s,Y",
        "ind": "%s (%s)", "indx": "%s (%s,X)", "indy": "%s (%s),Y",
        "indl": "%s [%s]", "indly": "%s [%s],Y",
        "sr": "%s %s,S", "sry": "%s (%s,S),Y", "pei": "%s (%s)",
    }
    if mode in fm:
        return fm[mode] % (mn, h2(op))
    fm16 = {"abs": "%s %s", "absx": "%s %s,X", "absy": "%s %s,Y",
            "absind": "%s (%s)", "absindx": "%s (%s,X)", "absindl": "%s [%s]",
            "pea": "%s %s"}
    if mode in fm16:
        return fm16[mode] % (mn, h4(op))
    if mode == "long":
        return "%s %s" % (mn, h6(op))
    if mode == "longx":
        return "%s %s,X" % (mn, h6(op))
    if mode == "blk":
        # assembler syntax: MVN/MVP srcbank,destbank; byte1=dest, byte2=src
        return "%s %s,%s" % (mn, h2(i.src_bank), h2(i.dest_bank))
    return mn


def decode_one(img, addr, st, track=False):
    """Decode instruction at 24-bit addr under state st. Raises Stop."""
    b0 = img.byte_at(addr)
    if b0 is None:
        raise Stop("address outside image")
    mn, mode = OPCODES[b0]
    e, m, x = st[0], st[1], st[2]
    if mode == "imm":
        if m is None:
            raise Stop("unknown immediate width (M unknown) for %s" % mn)
        length = 2 if m == 1 else 3
    elif mode == "immx":
        if x is None:
            raise Stop("unknown immediate width (X unknown) for %s" % mn)
        length = 2 if x == 1 else 3
    else:
        length = FIXED_LEN[mode]
    bs = [b0]
    phys0 = img.phys_of(addr)
    for k in range(1, length):
        a = (addr & ~0xFFFF) | ((addr + k) & 0xFFFF)
        b = img.byte_at(a)
        if b is None:
            raise Stop("instruction runs past end of image")
        if img.phys_of(a) != phys0 + k:
            raise Stop("instruction straddles a relocation boundary")
        bs.append(b)
    i = Insn()
    i.addr, i.phys, i.length, i.bytes = addr, phys0, length, bs
    i.mnemonic, i.mode = mn, mode
    i.state_in = st
    i.targets, i.ext_targets = [], []
    i.unresolved = False
    i.operand_target = None
    i.dest_bank = i.src_bank = None
    raw = 0
    for k in range(1, length):
        raw |= bs[k] << (8 * (k - 1))
    i.operand = raw if length > 1 else None
    bank = addr >> 16
    pc = addr & 0xFFFF
    nxt = (bank << 16) | ((pc + length) & 0xFFFF)

    def tgt16(v):
        return (bank << 16) | (v & 0xFFFF)

    flow = "seq"
    if mode == "rel":
        off = raw - 256 if raw >= 128 else raw
        t = tgt16(pc + 2 + off)
        i.operand_target = t
        i.targets.append((t, "branch"))
        flow = "branch" if mn in COND_BRANCH else "jump"
    elif mode == "rell":
        off = raw - 65536 if raw >= 32768 else raw
        t = tgt16(pc + 3 + off)
        i.operand_target = t
        i.targets.append((t, "branch"))
        flow = "jump"
    elif mode == "per":
        off = raw - 65536 if raw >= 32768 else raw
        t = tgt16(pc + 3 + off)
        i.operand_target = t
        i.targets.append((t, "per"))      # pushed address, not followed
    elif mn in ("JMP", "JSR") and mode == "abs":
        t = tgt16(raw)
        i.operand_target = t
        i.targets.append((t, "call" if mn == "JSR" else "jump"))
        flow = "call" if mn == "JSR" else "jump"
    elif mn in ("JML", "JSL") and mode == "long":
        t = raw
        i.operand_target = t
        i.targets.append((t, "call" if mn == "JSL" else "jump"))
        flow = "call" if mn == "JSL" else "jump"
    elif mode in ("absind", "absindx", "absindl"):
        i.unresolved = True
        flow = "call" if mn == "JSR" else "end"
    elif mn in ("RTS", "RTL", "RTI", "BRK", "COP", "STP"):
        flow = "end"
    elif mode == "blk":
        i.dest_bank, i.src_bank = bs[1], bs[2]
    i.flow = flow
    i.state_out = apply_state(mn, i.operand, st, track)
    return i, nxt


# ------------------------------- analysis ------------------------------------
class Disassembler:
    def __init__(self, img, entries, forced=None, track_php=False, stops=None):
        self.declared_stops = set(stops or [])
        self.declared_stops_hit = []
        self.img = img
        self.track = track_php
        self.entries = entries            # list of (addr24, state)
        self.forced = forced or {}        # addr24 -> state
        self.insns = {}                   # addr24 -> Insn
        self.owner = {}                   # phys offset -> addr24
        self.conflicts = []
        self.benign = []
        self.overlaps = []
        self.stops = []                   # unknown-width etc.
        self.out_of_image = []
        self.forced_used = []
        self.xrefs = {}                   # target -> list of dict
        self.external = {}                # target -> list
        self.unresolved = []
        self.labels = set()

    def _xref(self, tgt, frm, kind):
        self.xrefs.setdefault(tgt, []).append({"from": frm, "kind": kind})

    def run(self):
        work = []
        for a, st in reversed(self.entries):
            work.append((a, norm(st[0], st[1], st[2], None, () if self.track else None), None))
        while work:
            addr, st, src = work.pop()
            while True:
                nxt = self._arrive(addr, st, src, work)
                if nxt is None:
                    break
                addr, st, src = nxt
        self._collect()
        return self

    def _arrive(self, addr, st, src, work):
        """Process one instruction arrival. Returns next (addr, st, src) for
        straight-line continuation, or None."""
        if addr in self.forced:
            f = self.forced[addr]
            if (f[0], f[1], f[2]) != (st[0], st[1], st[2]):
                self.forced_used.append({"addr": addr, "incoming": state_dict(st),
                                         "forced": state_dict(f), "from": src})
            st = norm(f[0], f[1], f[2], st[3], st[4])
        if addr in self.declared_stops:
            self.declared_stops_hit.append({"addr": addr, "from": src})
            return None
        old = self.insns.get(addr)
        if old is not None:
            if tuple(old.state_in) == tuple(st):
                return None
            joined = join_state(old.state_in, st)
            if tuple(joined[:3]) != tuple(old.state_in[:3]):
                # two different M/X/E states reach one instruction
                width_dep = (old.mode == "imm" and joined[1] is None) or \
                            (old.mode == "immx" and joined[2] is None)
                o_a = apply_state(old.mnemonic, old.operand, old.state_in, self.track)
                o_b = apply_state(old.mnemonic, old.operand, st, self.track)
                same = tuple(o_a[:3]) == tuple(o_b[:3]) or \
                    (old.flow == "end" and not old.unresolved)
                rec = {"addr": addr, "first": state_dict(old.state_in),
                       "other": state_dict(st), "joined": state_dict(joined),
                       "from": src}
                if width_dep:
                    rec["kind"] = "width_dependent"
                    self.conflicts.append(rec)
                    return None
                if same:
                    self.benign.append(rec)
                else:
                    rec["kind"] = "joined_to_unknown"
                    self.conflicts.append(rec)
            old.state_in = joined
            new_out = apply_state(old.mnemonic, old.operand, joined, self.track)
            if tuple(new_out) == tuple(old.state_out):
                return None
            old.state_out = new_out
            return self._propagate(old, work, requeue=True)
        try:
            ins, nxt = decode_one(self.img, addr, st, self.track)
        except Stop as ex:
            if self.img.phys_of(addr) is None and str(ex) == "address outside image":
                self.out_of_image.append({"addr": addr, "from": src})
            else:
                self.stops.append({"addr": addr, "state": state_dict(st),
                                   "reason": str(ex), "from": src})
            return None
        for k in range(ins.length):
            o = self.owner.get(ins.phys + k)
            if o is not None:
                self.overlaps.append({"addr": addr, "overlaps": o,
                                      "from": src, "state": state_dict(st)})
                return None
        for k in range(ins.length):
            self.owner[ins.phys + k] = addr
        self.insns[addr] = ins
        return self._propagate(ins, work, nxt=nxt)

    def _propagate(self, ins, work, nxt=None, requeue=False):
        if nxt is None:
            nxt = (ins.addr & ~0xFFFF) | ((ins.addr + ins.length) & 0xFFFF)
        if not requeue:
            for t, kind in ins.targets:
                self._xref(t, ins.addr, kind)
        out = ins.state_out
        fresh = () if self.track else None
        for t, kind in ins.targets:
            if kind == "per":
                continue
            if self.img.phys_of(t) is None:
                if not requeue:
                    self.external.setdefault(t, []).append(ins.addr)
                continue
            if ins.flow == "call":   # callee sees caller e/m/x, fresh push stack
                tst = norm(ins.state_in[0], ins.state_in[1], ins.state_in[2],
                           ins.state_in[3], fresh)
            else:
                tst = out
            work.append((t, tst, ins.addr))
        if ins.unresolved and not requeue and ins.addr not in self.unresolved:
            self.unresolved.append(ins.addr)
        if ins.flow in ("seq", "branch"):
            return (nxt, out, ins.addr)
        if ins.flow == "call":
            # assumption: callee returns with the caller's e/m/x and balanced stack
            return (nxt, norm(ins.state_in[0], ins.state_in[1], ins.state_in[2],
                              None, ins.state_in[4]), ins.addr)
        return None

    def _collect(self):
        self.labels = set()
        for t, refs in self.xrefs.items():
            if self.img.phys_of(t) is not None:
                self.labels.add(t)
        # data operand xrefs
        for a, i in self.insns.items():
            if i.mode in ("abs", "absx", "absy", "absind", "absindx", "absindl") \
                    and i.targets == []:
                t = (a & ~0xFFFF) | i.operand
                if self.img.phys_of(t) is not None:
                    self._xref(t, a, "operand")
            elif i.mode in ("long", "longx") and i.targets == []:
                if self.img.phys_of(i.operand) is not None:
                    self._xref(i.operand, a, "operand")
        self.call_notes = self._call_state_notes()
        self.selfmod = []
        writers = {"STA", "STX", "STY", "STZ", "INC", "DEC", "ASL", "LSR",
                   "ROL", "ROR", "TSB", "TRB"}
        for a, i in self.insns.items():
            if i.mnemonic in writers and i.mode in ("abs", "absx", "absy"):
                t = (a & ~0xFFFF) | i.operand
                p = self.img.phys_of(t)
                o = self.owner.get(p) if p is not None else None
                if o is not None and i.mode == "abs":
                    # operand_byte 0 = opcode byte, >0 = operand byte
                    self.selfmod.append({"writer": a, "target": t,
                                         "instruction": o,
                                         "operand_byte": t - o})
        self.gaps = []
        n = len(self.img.data)
        p = 0
        while p < n:
            if p in self.owner:
                p += self.insns[self.owner[p]].length if p == self.insns[self.owner[p]].phys else 1
                continue
            q = p
            while q < n and q not in self.owner:
                q += 1
            self.gaps.append((self.img.load + p, q - p))
            p = q

    def _call_state_notes(self):
        notes = []
        callees = {}
        for a, i in self.insns.items():
            if i.flow == "call":
                for t, k in i.targets:
                    if t in self.insns:
                        callees.setdefault(t, []).append(a)
        for t in sorted(callees):
            entry = self.insns[t].state_in[:3]
            seen, stack = set(), [t]
            while stack:
                a = stack.pop()
                if a in seen or a not in self.insns:
                    continue
                seen.add(a)
                i = self.insns[a]
                if i.mnemonic in ("RTS", "RTL"):
                    if tuple(i.state_in[:3]) != tuple(entry):
                        changed = any(p is not None and q is not None and p != q
                                      for p, q in zip(i.state_in[:3], entry))
                        notes.append({
                            "kind": "changed" if changed else "unknown_at_return",
                            "callee": t, "return_at": a,
                            "entry_state": state_dict(entry),
                            "state_at_return": state_dict(i.state_in),
                            "callers": callees[t]})
                    continue
                if i.flow == "end":
                    continue
                for tt, k in i.targets:
                    if k in ("branch", "jump"):
                        stack.append(tt)
                if i.flow in ("seq", "branch", "call"):
                    stack.append((a & ~0xFFFF) | ((a + i.length) & 0xFFFF))
        return notes

    # ------------------------------ outputs ---------------------------------
    def summary(self):
        code = sum(i.length for i in self.insns.values())
        total = len(self.img.data)
        return {
            "total_bytes": total,
            "code_bytes": code,
            "data_bytes": total - code,
            "instructions": len(self.insns),
            "unresolved_indirect_jumps": [
                {"addr": a, "addr_hex": addr_hex(a),
                 "text": fmt_insn(self.insns[a], self.labels)}
                for a in sorted(self.unresolved)],
            "state_conflicts": self.conflicts,
            "overlap_conflicts": self.overlaps,
            "unknown_width_stops": self.stops,
            "forced_state_overrides": self.forced_used,
            "external_targets": [{"target": t, "target_hex": addr_hex(t),
                                  "from": sorted(set(f))}
                                 for t, f in sorted(self.external.items())],
            "entries_outside_image": self.out_of_image,
            "declared_data_stops_hit": self.declared_stops_hit,
            "benign_state_merges": self.benign,
            "call_state_notes": self.call_notes,
            "self_modifying_writes": self.selfmod,
            "unreached_ranges": [{"start": a, "start_hex": addr_hex(a), "length": l}
                                 for a, l in self.gaps],
        }

    def to_json(self, meta):
        ins = [self.insns[a].to_json(self.labels) for a in
               sorted(self.insns, key=lambda a: self.insns[a].phys)]
        xr = {addr_hex(t): v for t, v in sorted(self.xrefs.items())}
        return {"meta": meta, "summary": self.summary(),
                "instructions": ins, "xrefs": xr}

    def to_asm(self, name):
        img = self.img
        data = img.data
        lines = []
        by_phys = {i.phys: i for i in self.insns.values()}
        inline, top = {}, []
        for t in sorted(self.labels):
            p = img.phys_of(t)
            if img.is_virtual(t) or p is None or (img.load + p) != (t & 0xFFFF) or (t >> 16):
                top.append(t)
            else:
                inline.setdefault(p, []).append(t)
        # inline labels that fall mid-instruction cannot be placed -> top
        for p in list(inline):
            if p in self.owner and p not in by_phys:
                top.extend(inline.pop(p))
        lines.append("// %s: generated by tools/dis65816/dis65816.py; every byte is emitted as .byte." % name)
        lines.append("// Mnemonics in comments are 65816; state = e (emulation) / m,x width in bits (? = unknown).")
        for t in sorted(top):
            lines.append(".label %s = $%04X%s" % (
                label_name(t), t & 0xFFFF,
                "  // relocated (virtual) address" if img.is_virtual(t) else ""))
        lines.append("*=$%04X" % img.load)
        p = 0
        n = len(data)
        while p < n:
            for t in inline.get(p, []):
                lines.append("%s:" % label_name(t))
            i = by_phys.get(p)
            if i is not None:
                bs = ",".join("$%02X" % b for b in i.bytes)
                txt = fmt_insn(i, self.labels)
                where = addr_hex(i.addr)
                if img.is_virtual(i.addr):
                    where += " (virt, file $%04X)" % (img.load + i.phys)
                io = ""
                if i.mode in ("abs", "absx", "absy", "absind", "absindx", "absindl") \
                        and not i.targets:
                    nm = io_name(i.operand)
                    if nm:
                        io = "  ; " + nm
                elif i.mode in ("long", "longx") and not i.targets and (i.operand >> 16) == 0:
                    nm = io_name(i.operand & 0xFFFF)
                    if nm:
                        io = "  ; " + nm
                lines.append("    .byte %-17s // %s: %-24s [%s]%s%s" % (
                    bs, where, txt, state_str(i.state_in),
                    "  ; UNRESOLVED INDIRECT" if i.unresolved else "", io))
                p += i.length
                continue
            q = p
            while q < n and q - p < 16 and (q == p or (q not in by_phys and q not in inline)):
                if q in self.owner:
                    break
                q += 1
            if q == p:      # defensive: byte owned by an instruction not starting here
                q = p + 1
            lines.append("    .byte %-47s // %04X: data" % (
                ",".join("$%02X" % b for b in data[p:q]), img.load + p))
            p = q
        return "\n".join(lines) + "\n"


# ------------------------------- driver --------------------------------------
def parse_addr(s):
    s = s.strip().lstrip("$")
    if s.lower().startswith("0x"):
        s = s[2:]
    return int(s, 16)


def load_prg(path):
    with open(path, "rb") as f:
        raw = f.read()
    if len(raw) < 2:
        raise ValueError("file too short for PRG")
    return raw[2:], raw[0] | (raw[1] << 8), raw


def find_kickass(explicit=None):
    cands = [explicit] if explicit else []
    cands += [os.environ.get("KICKASS_JAR"), "/Applications/KickAssembler/KickAss.jar"]
    for c in cands:
        if c and os.path.exists(c):
            return c
    return None


def rebuild_check(asm_path, prg_path, jar):
    """Assemble asm with KickAssembler; compare with the original PRG (whole file)."""
    sym = os.path.splitext(asm_path)[0] + ".sym"
    had_sym = os.path.exists(sym)
    with tempfile.TemporaryDirectory() as td:
        out = os.path.join(td, "rebuilt.prg")
        r = subprocess.run(["java", "-jar", jar, asm_path, "-o", out],
                           capture_output=True, text=True)
        if not had_sym and os.path.exists(sym):
            os.remove(sym)
        if r.returncode != 0 or not os.path.exists(out):
            return False, "KickAssembler failed:\n" + r.stdout + r.stderr
        a = open(out, "rb").read()
        b = open(prg_path, "rb").read()
        if a == b:
            return True, "byte-identical (%d bytes)" % len(b)
        n = next((k for k in range(min(len(a), len(b))) if a[k] != b[k]), min(len(a), len(b)))
        return False, "MISMATCH rebuilt=%d bytes original=%d bytes first diff at file offset %d" % (
            len(a), len(b), n)


def summary_text(name, s, entries, rebuild=None):
    L = ["Summary for %s" % name]
    L.append("entries: " + ", ".join("%s(%s)" % (addr_hex(a), state_str(st)) for a, st in entries))
    L.append("total bytes: %d  code bytes: %d  data bytes: %d  instructions: %d" % (
        s["total_bytes"], s["code_bytes"], s["data_bytes"], s["instructions"]))
    L.append("unresolved indirect jumps: %d" % len(s["unresolved_indirect_jumps"]))
    for u in s["unresolved_indirect_jumps"]:
        L.append("  %s  %s" % (u["addr_hex"], u["text"]))
    L.append("state conflicts: %d" % len(s["state_conflicts"]))
    for c in s["state_conflicts"]:
        L.append("  %s [%s] first=%s other=%s joined=%s from=%s" % (
            addr_hex(c["addr"]), c["kind"], c["first"], c["other"], c["joined"],
            None if c["from"] is None else addr_hex(c["from"])))
    L.append("overlap conflicts: %d" % len(s["overlap_conflicts"]))
    for c in s["overlap_conflicts"]:
        L.append("  %s overlaps insn at %s from=%s" % (
            addr_hex(c["addr"]), addr_hex(c["overlaps"]),
            None if c["from"] is None else addr_hex(c["from"])))
    L.append("unknown-width/other stops: %d" % len(s["unknown_width_stops"]))
    for c in s["unknown_width_stops"]:
        L.append("  %s %s (%s) from=%s" % (
            addr_hex(c["addr"]), c["reason"], c["state"],
            None if c["from"] is None else addr_hex(c["from"])))
    L.append("forced-state overrides: %d" % len(s["forced_state_overrides"]))
    L.append("declared data stops hit (flow refused by config 'stop'): %d" % len(s["declared_data_stops_hit"]))
    for c in s["declared_data_stops_hit"]:
        L.append("  %s from=%s" % (addr_hex(c["addr"]), None if c["from"] is None else addr_hex(c["from"])))
    L.append("external targets (not followed): %d" % len(s["external_targets"]))
    for c in s["external_targets"]:
        L.append("  %s from %s" % (c["target_hex"], ",".join(addr_hex(f) for f in c["from"])))
    L.append("benign state merges (differing states, identical result): %d" % len(s["benign_state_merges"]))
    for c in s["benign_state_merges"]:
        L.append("  %s first=%s other=%s" % (addr_hex(c["addr"]), c["first"], c["other"]))
    L.append("self-modifying writes into decoded operand bytes: %d" % len(s["self_modifying_writes"]))
    for c in s["self_modifying_writes"][:10]:
        L.append("  writer %s -> %s (insn %s +%d)" % (
            addr_hex(c["writer"]), addr_hex(c["target"]), addr_hex(c["instruction"]), c["operand_byte"]))
    if len(s["self_modifying_writes"]) > 10:
        L.append("  ... (all in JSON)")
    gaps = s["unreached_ranges"]
    L.append("unreached ranges (data): %d, largest: %s" % (
        len(gaps), ", ".join("%s+%d" % (g["start_hex"], g["length"])
                             for g in sorted(gaps, key=lambda g: -g["length"])[:5])))
    ch = [c for c in s["call_state_notes"] if c["kind"] == "changed"]
    L.append("call-return state notes: %d changed (known->different known), %d unknown-at-return"
             % (len(ch), len(s["call_state_notes"]) - len(ch)))
    for c in ch:
        L.append("  callee %s RTS/RTL at %s entry=%s return=%s" % (
            addr_hex(c["callee"]), addr_hex(c["return_at"]),
            c["entry_state"], c["state_at_return"]))
    if rebuild is not None:
        L.append("rebuild: %s" % rebuild)
    return "\n".join(L) + "\n"


def project_root():
    return os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def resolve(p):
    return p if os.path.isabs(p) else os.path.join(project_root(), p)


def main(argv=None):
    ap = argparse.ArgumentParser(description="W65C816 flow-following disassembler")
    ap.add_argument("input", nargs="?", help="PRG file (2-byte load address)")
    ap.add_argument("--config", help="JSON config (see README)")
    ap.add_argument("--entry", action="append", default=[],
                    help="ADDR[:e=1,m=8,x=16]  (hex address, 24-bit allowed)")
    ap.add_argument("--force", action="append", default=[],
                    help="ADDR:e=0,m=16,x=16  declare state at an address")
    ap.add_argument("--alias", action="append", default=[],
                    help="VIRT=PHYS:LEN  code runs at VIRT but lives at PHYS in the file (hex)")
    ap.add_argument("--stop", action="append", default=[],
                    help="ADDR  declare data: flow is not followed into this address")
    ap.add_argument("--bank", type=lambda s: int(s, 16), default=0)
    ap.add_argument("--out", help="output directory")
    ap.add_argument("--name", help="output file stem")
    ap.add_argument("--track-php", action="store_true",
                    help="pair PHP/PLP on an abstract stack (default: PLP -> unknown)")
    ap.add_argument("--verify", action="store_true", help="rebuild with KickAssembler and compare")
    ap.add_argument("--kickass", help="path to KickAss.jar")
    a = ap.parse_args(argv)

    cfg = {}
    if a.config:
        with open(resolve(a.config)) as f:
            cfg = json.load(f)
    inp = a.input or cfg.get("input")
    if not inp:
        ap.error("no input")
    inp = resolve(inp)
    name = a.name or cfg.get("name") or os.path.basename(inp)
    out = resolve(a.out or cfg.get("out") or os.path.join("analysis", "dis65816", name))
    bank = a.bank if a.bank else int(cfg.get("bank", 0))

    data, load, raw = load_prg(inp)
    aliases = []
    for al in cfg.get("alias", []):
        aliases.append((parse_addr(al["virt"]), parse_addr(al["phys"]), parse_addr(al["len"])))
    for s in a.alias:
        v, rest = s.split("=")
        p, ln = rest.split(":")
        aliases.append((parse_addr(v), parse_addr(p), parse_addr(ln)))
    img = Image(data, load, bank, aliases)

    entries = []
    forced = {}
    def parse_entry(s):
        if ":" in s:
            ad, st = s.split(":", 1)
        else:
            ad, st = s, ""
        return parse_addr(ad), parse_state(st)
    for e in cfg.get("entries", []):
        st = "e=%d" % e["e"] if "e" in e else ""
        for k in ("m", "x"):
            if k in e:
                st += ",%s=%d" % (k, e[k])
        entries.append((parse_addr(e["addr"]) | (bank << 16) if parse_addr(e["addr"]) < 0x10000 else parse_addr(e["addr"]), parse_state(st)))
    for s in a.entry:
        ad, st = parse_entry(s)
        entries.append((ad | (bank << 16) if ad < 0x10000 else ad, st))
    for e in cfg.get("force", []):
        st = "e=%d" % e["e"] if "e" in e else ""
        for k in ("m", "x"):
            if k in e:
                st += ",%s=%d" % (k, e[k])
        ad = parse_addr(e["addr"])
        forced[ad | (bank << 16) if ad < 0x10000 else ad] = parse_state(st)
    for s in a.force:
        ad, st = parse_entry(s)
        forced[ad | (bank << 16) if ad < 0x10000 else ad] = st
    if not entries:
        ap.error("no entries")

    track = bool(a.track_php or cfg.get("track_php", False))
    stops = [parse_addr(x) for x in cfg.get("stop", [])] + [parse_addr(x) for x in a.stop]
    stops = [x | (bank << 16) if x < 0x10000 else x for x in stops]
    d = Disassembler(img, entries, forced, track, stops).run()
    os.makedirs(out, exist_ok=True)
    stem = os.path.splitext(name)[0] if name.endswith(".prg") else name
    asm_path = os.path.join(out, stem + ".asm")
    with open(asm_path, "w") as f:
        f.write(d.to_asm(name))
    meta = {"input": os.path.basename(inp), "load": load, "load_hex": "%04X" % load,
            "size": len(data),
            "entries": [{"addr": a_, "addr_hex": addr_hex(a_), "state": state_dict(s)}
                        for a_, s in entries],
            "forced": [{"addr": a_, "addr_hex": addr_hex(a_), "state": state_dict(s)}
                       for a_, s in forced.items()],
            "track_php": track,
            "declared_stops": [addr_hex(x) for x in stops],
            "aliases": [{"virt": v, "phys": p, "len": l} for v, p, l in aliases]}
    rb = None
    if a.verify:
        jar = find_kickass(a.kickass)
        if not jar:
            rb = "KickAss.jar not found"
            ok = False
        else:
            ok, rb = rebuild_check(asm_path, inp, jar)
        meta["rebuild"] = {"ok": ok, "detail": rb}
    with open(os.path.join(out, stem + ".json"), "w") as f:
        json.dump(d.to_json(meta), f, indent=1)
    st_txt = summary_text(name, d.summary(), entries, rb)
    with open(os.path.join(out, "summary.txt"), "w") as f:
        f.write(st_txt)
    sys.stdout.write(st_txt)
    if a.verify and not meta["rebuild"]["ok"]:
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
