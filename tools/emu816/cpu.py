"""W65C816S instruction-set interpreter (no timing, no machine).

Sources used for behaviour (stated where a choice was made):
  [DS]  WDC W65C816S datasheet (opcode matrix, addressing-mode table, flag/register tables,
        emulation-mode notes) and the W65C816S Programming Manual.
  [PM]  Programming manual instruction pages (decimal-mode flags, MVN/MVP, stack behaviour).
  [NMOS] MOS 6502/6510 behaviour as documented by Bruce Clark ("Decimal Mode") and the
        visual6502 / 64doc work, used only by 6502 mode (nmos=True).
Decimal ADC/SBC on the 65816 follow the algorithm that reproduces the W65C816S results (valid N,V,Z,
result Z from the *decimal* result); see _adc/_sbc. 16-bit decimal is the same chain over 4 nibbles.
"""
from collections import deque

from .opcodes import TABLE, LEGAL_6502
from .memory import Memory

K24, K16, KDP = 0, 1, 2   # wrap kind of the 2nd byte of a 16-bit access: 24-bit, within-bank, direct page

VEC_NATIVE = {"COP": 0xFFE4, "BRK": 0xFFE6, "ABORT": 0xFFE8, "NMI": 0xFFEA, "IRQ": 0xFFEE}
VEC_EMU = {"COP": 0xFFF4, "ABORT": 0xFFF8, "NMI": 0xFFFA, "RESET": 0xFFFC, "IRQ": 0xFFFE, "BRK": 0xFFFE}


class IllegalOpcode6502(Exception):
    pass


class JmpIndirectBug(Exception):
    """JMP ($xxFF) executed in 6502 mode (page-wrap bug site)."""


class Halt(Exception):
    pass


class RunResult:
    def __init__(self, reason, steps, pc):
        self.reason, self.steps, self.pc = reason, steps, pc

    def __repr__(self):
        return "RunResult(%s, steps=%d, pc=%06X)" % (self.reason, self.steps, self.pc)


def _s8(v):
    return v - 256 if v & 0x80 else v


def _s16(v):
    return v - 65536 if v & 0x8000 else v


class CPU:
    def __init__(self, mem=None, mode6502=False, trace=64, raise_on_jmp_bug=True):
        self.mem = mem if mem is not None else Memory()
        self.nmos = bool(mode6502)
        self.raise_on_jmp_bug = raise_on_jmp_bug
        self.events = []            # e.g. ("jmp_ind_page_bug", pc, ptr)
        self.trace = deque(maxlen=trace) if trace else None
        self.steps = 0
        self.halted = None          # None | "WAI" | "STP"
        # registers
        self.C = 0                  # 16-bit accumulator (A = low byte, B = high byte)
        self.X = self.Y = 0
        self.S = 0x01FF
        self.D = 0
        self.DB = 0
        self.PB = 0
        self.PC = 0
        # flags
        self.fN = self.fV = self.fD = self.fI = self.fZ = self.fC = 0
        self.fM = self.fX = 1
        self.E = 1
        self._build_dispatch()

    # ------------------------------------------------------------ state helpers
    @property
    def P(self):
        return (self.fN << 7 | self.fV << 6 | self.fM << 5 | self.fX << 4 | self.fD << 3
                | self.fI << 2 | self.fZ << 1 | self.fC)

    @P.setter
    def P(self, v):
        self.fN, self.fV = (v >> 7) & 1, (v >> 6) & 1
        self.fD, self.fI, self.fZ, self.fC = (v >> 3) & 1, (v >> 2) & 1, (v >> 1) & 1, v & 1
        self.fM, self.fX = (v >> 5) & 1, (v >> 4) & 1
        self._fix_mx()

    def _fix_mx(self):
        if self.E:                       # [DS] emulation: M and X forced to 1
            self.fM = self.fX = 1
            self.S = 0x0100 | (self.S & 0xFF)
        if self.fX:                      # [DS] X=1 : index high bytes are zero
            self.X &= 0xFF
            self.Y &= 0xFF

    @property
    def A(self):
        return self.C & 0xFF

    @A.setter
    def A(self, v):
        self.C = (self.C & 0xFF00) | (v & 0xFF)

    @property
    def B(self):
        return self.C >> 8

    def pc24(self):
        return (self.PB << 16) | self.PC

    def set_state(self, **kw):
        """Set registers/flags: A,B,C,X,Y,S,D,DB,PB,PC,P,E,N,V,M,X_flag... e.g. set_state(E=0, M=0, X=0)."""
        for k, v in kw.items():
            if k == "A": self.A = v
            elif k == "B": self.C = (self.C & 0xFF) | ((v & 0xFF) << 8)
            elif k in ("N", "V", "M", "D_flag", "I", "Z", "CF"):
                setattr(self, {"N": "fN", "V": "fV", "M": "fM", "D_flag": "fD", "I": "fI",
                               "Z": "fZ", "CF": "fC"}[k], v & 1)
            elif k == "XF": self.fX = v & 1
            elif k == "P": self.P = v
            else: setattr(self, k, v)
        self._fix_mx()

    def reset_6502(self):
        """Power-on state for 6502 mode: E=1, S=$01FF, D=DB=PB=0."""
        self.E = 1
        self.S, self.D, self.DB, self.PB = 0x01FF, 0, 0, 0
        self.fM = self.fX = 1
        self._fix_mx()

    # ------------------------------------------------------------ memory helpers
    def _rd(self, addr):
        return self.mem.read(addr)

    def _wr(self, addr, v):
        self.mem.write(addr, v)

    def fetch8(self):
        v = self.mem.read((self.PB << 16) | self.PC)
        self.PC = (self.PC + 1) & 0xFFFF
        return v

    def fetch16(self):
        lo = self.fetch8()
        return lo | (self.fetch8() << 8)

    def fetch24(self):
        lo = self.fetch16()
        return lo | (self.fetch8() << 16)

    def _next(self, ea, i):
        """Address of byte i of a multi-byte access at ea=(addr, kind)."""
        addr, kind = ea
        if kind == K24:
            return (addr + i) & 0xFFFFFF
        if kind == K16:
            return (addr & 0xFF0000) | ((addr + i) & 0xFFFF)
        # KDP: [DS] emulation + DL=0 : page wrap, otherwise 16-bit wrap in bank 0
        if self.E and (self.D & 0xFF) == 0:
            return (addr & 0xFFFF00) | ((addr + i) & 0xFF)
        return (addr + i) & 0xFFFF

    def rd(self, ea, n):
        v = self._rd(ea[0])
        if n == 2:
            v |= self._rd(self._next(ea, 1)) << 8
        return v

    def wr(self, ea, n, v):
        self._wr(ea[0], v & 0xFF)
        if n == 2:
            self._wr(self._next(ea, 1), (v >> 8) & 0xFF)

    # ------------------------------------------------------------ stack
    def push_old(self, v):               # 6502-style push: wraps in page 1 when E=1
        self._wr(self.S, v)
        self.S = (0x0100 | ((self.S - 1) & 0xFF)) if self.E else ((self.S - 1) & 0xFFFF)

    def pull_old(self):
        self.S = (0x0100 | ((self.S + 1) & 0xFF)) if self.E else ((self.S + 1) & 0xFFFF)
        return self._rd(self.S)

    def push_new(self, v):               # 65816-new stack ops: full 16-bit S; E=1 S.h restored after the op
        self._wr(self.S, v)
        self.S = (self.S - 1) & 0xFFFF

    def pull_new(self):
        self.S = (self.S + 1) & 0xFFFF
        return self._rd(self.S)

    # ------------------------------------------------------------ effective addresses
    def _dp(self, off, idx=0):
        """Direct-page EA. [DS] E=1 & DL=0: index wraps inside the page; else 16-bit wrap in bank 0."""
        if self.E and (self.D & 0xFF) == 0:
            return (self.D + ((off + idx) & 0xFF)) & 0xFFFF
        return (self.D + off + idx) & 0xFFFF

    def _idx(self, v):
        return v & (0xFF if self.fX else 0xFFFF)

    def _abs24(self, base, idx):
        a = (base + idx) & 0xFFFFFF
        return a & 0xFFFF if self.nmos else a      # NMOS: 64 KiB wrap, no bank carry

    def ea(self, mode):
        if mode == "dp":
            return (self._dp(self.fetch8()), KDP)
        if mode == "dpx":
            return (self._dp(self.fetch8(), self._idx(self.X)), KDP)
        if mode == "dpy":
            return (self._dp(self.fetch8(), self._idx(self.Y)), KDP)
        if mode == "abs":
            return ((self.DB << 16) | self.fetch16(), K24)
        if mode == "absx":
            return (self._abs24((self.DB << 16) | self.fetch16(), self._idx(self.X)), K24)
        if mode == "absy":
            return (self._abs24((self.DB << 16) | self.fetch16(), self._idx(self.Y)), K24)
        if mode == "long":
            return (self.fetch24(), K24)
        if mode == "longx":
            return ((self.fetch24() + self._idx(self.X)) & 0xFFFFFF, K24)
        if mode == "ind":
            p = self.rd((self._dp(self.fetch8()), KDP), 2)
            return ((self.DB << 16) | p, K24)
        if mode == "indx":
            p = self.rd((self._dp(self.fetch8(), self._idx(self.X)), KDP), 2)
            return ((self.DB << 16) | p, K24)
        if mode == "indy":
            p = self.rd((self._dp(self.fetch8()), KDP), 2)
            return (self._abs24((self.DB << 16) | p, self._idx(self.Y)), K24)
        if mode == "ldp":
            a = self._dp(self.fetch8())
            return (self._ptr24(a), K24)
        if mode == "ldpy":
            a = self._dp(self.fetch8())
            return ((self._ptr24(a) + self._idx(self.Y)) & 0xFFFFFF, K24)
        if mode == "sr":
            return ((self.S + self.fetch8()) & 0xFFFF, K16)
        if mode == "sry":
            p = self.rd(((self.S + self.fetch8()) & 0xFFFF, K16), 2)
            return (((self.DB << 16) + p + self._idx(self.Y)) & 0xFFFFFF, K24)
        raise AssertionError(mode)

    def _ptr24(self, dpaddr):
        ea = (dpaddr, KDP)
        return self.rd(ea, 2) | (self._rd(self._next(ea, 2)) << 16)

    # ------------------------------------------------------------ flags / ALU
    def _nz(self, v, n):
        if n == 1:
            self.fN, self.fZ = (v >> 7) & 1, int(v & 0xFF == 0)
        else:
            self.fN, self.fZ = (v >> 15) & 1, int(v & 0xFFFF == 0)

    def _acc_n(self):
        return 1 if self.fM else 2

    def _x_n(self):
        return 1 if self.fX else 2

    def getA(self, n):
        return self.C & (0xFF if n == 1 else 0xFFFF)

    def setA(self, v, n):
        if n == 1:
            self.C = (self.C & 0xFF00) | (v & 0xFF)
        else:
            self.C = v & 0xFFFF

    def _adc(self, d, n):
        a = self.getA(n)
        c = self.fC
        sign = 0x80 if n == 1 else 0x8000
        mask = 0xFF if n == 1 else 0xFFFF
        if not self.fD:
            r = a + d + c
            self.fV = int(bool(~(a ^ d) & (a ^ r) & sign))
            self.fC = int(r > mask)
            r &= mask
            self._nz(r, n)
            self.setA(r, n)
            return
        if self.nmos:
            return self._adc_nmos(d, c)
        nn = 2 * n                                   # nibbles
        carry = c
        res = 0
        for i in range(nn):
            sh = 4 * i
            t = ((a >> sh) & 0xF) + ((d >> sh) & 0xF) + carry
            if i == nn - 1:                          # V from the sum with the top nibble not yet corrected
                self.fV = int(bool(~(a ^ d) & (a ^ (res | (t << sh))) & sign))
            carry = int(t > 9)
            if carry:
                t += 6
            res |= (t & 0xF) << sh
        self.fC = carry
        self._nz(res, n)                              # 65816: N,Z from the decimal result [PM]
        self.setA(res, n)

    def _adc_nmos(self, b, c):
        a = self.C & 0xFF
        # Bruce Clark, "Decimal Mode", 6502 ADC sequence 1/2
        z_bin = (a + b + c) & 0xFF == 0
        al = (a & 0xF) + (b & 0xF) + c
        if al >= 0x0A:
            al = ((al + 6) & 0xF) + 0x10
        sa = _s8(a & 0xF0) + _s8(b & 0xF0) + al
        self.fN = 1 if (sa & 0x80) else 0
        self.fV = int(sa < -128 or sa > 127)
        r = (a & 0xF0) + (b & 0xF0) + al
        if r >= 0xA0:
            r += 0x60
        self.fC = int(r >= 0x100)
        self.fZ = int(z_bin)
        self.A = r & 0xFF

    def _sbc(self, d, n):
        a = self.getA(n)
        c = self.fC
        sign = 0x80 if n == 1 else 0x8000
        mask = 0xFF if n == 1 else 0xFFFF
        inv = (~d) & mask
        if not self.fD:
            r = a + inv + c
            self.fV = int(bool(~(a ^ inv) & (a ^ r) & sign))
            self.fC = int(r > mask)
            r &= mask
            self._nz(r, n)
            self.setA(r, n)
            return
        if self.nmos:
            return self._sbc_nmos(d, c)
        nn = 2 * n
        carry = c
        res = 0
        for i in range(nn):
            sh = 4 * i
            t = ((a >> sh) & 0xF) + ((inv >> sh) & 0xF) + carry
            if i == nn - 1:
                self.fV = int(bool(~(a ^ inv) & (a ^ (res | (t << sh))) & sign))
            carry = int(t > 0xF)
            if not carry:
                t -= 6
            res |= (t & 0xF) << sh
        self.fC = carry
        self._nz(res, n)
        self.setA(res, n)

    def _sbc_nmos(self, b, c):
        a = self.C & 0xFF
        # All flags as in binary mode [NMOS]; only A is decimal-adjusted (Clark, SBC sequence)
        rb = a + ((~b) & 0xFF) + c
        self.fV = int(bool(~(a ^ (~b & 0xFF)) & (a ^ rb) & 0x80))
        self.fC = int(rb > 0xFF)
        self._nz(rb & 0xFF, 1)
        al = (a & 0xF) - (b & 0xF) + c - 1
        if al < 0:
            al = ((al - 6) & 0xF) - 0x10
        r = (a & 0xF0) - (b & 0xF0) + al
        if r < 0:
            r -= 0x60
        self.A = r & 0xFF

    def _cmp(self, reg, d, n):
        mask = 0xFF if n == 1 else 0xFFFF
        r = (reg - d) & mask
        self.fC = int((reg & mask) >= d)
        self._nz(r, n)

    # ------------------------------------------------------------ dispatch table
    def _build_dispatch(self):
        self.disp = []
        for op in range(256):
            mn, mode = TABLE[op]
            fn = getattr(self, "op_" + mn)
            self.disp.append((fn, mode))

    # operand access for the ALU group; returns (value) for reads
    def _src(self, mode, n):
        if mode == "immM" or mode == "immX":
            v = self.fetch8()
            if n == 2:
                v |= self.fetch8() << 8
            return v
        return self.rd(self.ea(mode), n)

    # ------------------------------------------------------------ execution
    def step(self):
        if self.halted:
            raise Halt(self.halted)
        pc24 = self.pc24()
        op = self.mem.read(pc24)
        if self.nmos and op not in LEGAL_6502:
            raise IllegalOpcode6502("opcode %02X at %06X is not a legal NMOS 6502 opcode" % (op, pc24))
        if self.trace is not None:
            self.trace.append((pc24, op, self.C, self.X, self.Y, self.S, self.P, self.E, self.D, self.DB))
        self.PC = (self.PC + 1) & 0xFFFF
        fn, mode = self.disp[op]
        fn(mode, op)
        if self.E:                                   # [DS] S high forced to 1 in emulation mode
            self.S = 0x0100 | (self.S & 0xFF)
        self.steps += 1
        if self.mem.post_step:
            for cb in self.mem.post_step:
                cb()

    def run(self, entry=None, stop_on="rts", sentinel=0x00FFF0, addresses=(), max_steps=1_000_000, skip_entry_stop=False):
        """Run from `entry` (24-bit or 16-bit PC within PB).

        stop_on: "rts"  - push `sentinel` as a JSR return address and stop when PC reaches it
                 "rtl"  - push `sentinel` as a JSL return (3 bytes), stop at sentinel
                 "address" - stop when PB:PC is in `addresses`
                 None   - only max_steps / STP / WAI
        max_steps always applies. Returns RunResult(reason in {sentinel,address,max_steps,STP,WAI}).
        skip_entry_stop: an `addresses` hit before the first instruction (entry itself is a stop address) does not stop (loop-body calls).
        """
        if entry is not None:
            if entry > 0xFFFF:
                self.PB = (entry >> 16) & 0xFF
            self.PC = entry & 0xFFFF
        stops = set(addresses)
        sent24 = None
        if stop_on == "rts":
            sent24 = (self.PB << 16) | (sentinel & 0xFFFF)
            ret = (sent24 - 1) & 0xFFFF
            self.push_old(ret >> 8)
            self.push_old(ret & 0xFF)
        elif stop_on == "rtl":
            sent24 = sentinel & 0xFFFFFF
            ret = (sent24 - 1) & 0xFFFF
            self.push_new((sent24 >> 16) & 0xFF)
            self.push_new(ret >> 8)
            self.push_new(ret & 0xFF)
            if self.E:
                self.S = 0x0100 | (self.S & 0xFF)
        if sent24 is not None:
            stops.add(sent24)
        n = 0
        while True:
            if self.pc24() in stops and not (skip_entry_stop and n == 0 and self.pc24() != sent24):
                return RunResult("sentinel" if self.pc24() == sent24 else "address",
                                 n, self.pc24())
            if n >= max_steps:
                return RunResult("max_steps", n, self.pc24())
            try:
                self.step()
            except Halt:
                return RunResult(self.halted, n, self.pc24())
            n += 1
            if self.halted:
                return RunResult(self.halted, n, self.pc24())

    # ------------------------------------------------------------ ALU-group ops
    def op_ORA(self, mode, op):
        n = self._acc_n(); v = self.getA(n) | self._src(mode, n); self.setA(v, n); self._nz(v, n)

    def op_AND(self, mode, op):
        n = self._acc_n(); v = self.getA(n) & self._src(mode, n); self.setA(v, n); self._nz(v, n)

    def op_EOR(self, mode, op):
        n = self._acc_n(); v = self.getA(n) ^ self._src(mode, n); self.setA(v, n); self._nz(v, n)

    def op_ADC(self, mode, op):
        n = self._acc_n(); self._adc(self._src(mode, n), n)

    def op_SBC(self, mode, op):
        n = self._acc_n(); self._sbc(self._src(mode, n), n)

    def op_CMP(self, mode, op):
        n = self._acc_n(); self._cmp(self.getA(n), self._src(mode, n), n)

    def op_LDA(self, mode, op):
        n = self._acc_n(); v = self._src(mode, n); self.setA(v, n); self._nz(v, n)

    def op_STA(self, mode, op):
        n = self._acc_n(); self.wr(self.ea(mode), n, self.getA(n))

    def op_STZ(self, mode, op):
        n = self._acc_n(); self.wr(self.ea(mode), n, 0)

    def op_BIT(self, mode, op):
        n = self._acc_n()
        v = self._src(mode, n)
        self.fZ = int((self.getA(n) & v) == 0)
        if mode != "immM":                      # [PM] BIT #imm affects Z only
            self.fN = (v >> (7 if n == 1 else 15)) & 1
            self.fV = (v >> (6 if n == 1 else 14)) & 1

    def _set_index(self, reg, v, n):
        setattr(self, reg, v & (0xFF if n == 1 else 0xFFFF))
        self._nz(v, n)

    def op_LDX(self, mode, op):
        n = self._x_n(); self._set_index("X", self._src(mode, n), n)

    def op_LDY(self, mode, op):
        n = self._x_n(); self._set_index("Y", self._src(mode, n), n)

    def op_STX(self, mode, op):
        n = self._x_n(); self.wr(self.ea(mode), n, self.X)

    def op_STY(self, mode, op):
        n = self._x_n(); self.wr(self.ea(mode), n, self.Y)

    def op_CPX(self, mode, op):
        n = self._x_n(); self._cmp(self.X, self._src(mode, n), n)

    def op_CPY(self, mode, op):
        n = self._x_n(); self._cmp(self.Y, self._src(mode, n), n)

    # ------------------------------------------------------------ read-modify-write
    def _rmw(self, mode, fn):
        n = self._acc_n()
        if mode == "acc":
            self.setA(fn(self.getA(n), n), n)
            return
        ea = self.ea(mode)
        old = self.rd(ea, n)
        new = fn(old, n) & (0xFF if n == 1 else 0xFFFF)
        if n == 1:
            if self.nmos:
                self._wr(ea[0], old)               # [NMOS] RMW writes the old value back first
            self._wr(ea[0], new)
        else:                                      # [DS] 16-bit RMW writes high byte first, then low
            self._wr(self._next(ea, 1), new >> 8)
            self._wr(ea[0], new & 0xFF)

    def _asl(self, v, n):
        sh = 7 if n == 1 else 15
        self.fC = (v >> sh) & 1
        r = (v << 1) & (0xFF if n == 1 else 0xFFFF); self._nz(r, n); return r

    def _lsr(self, v, n):
        self.fC = v & 1
        r = v >> 1; self._nz(r, n); return r

    def _rol(self, v, n):
        sh = 7 if n == 1 else 15
        c = self.fC; self.fC = (v >> sh) & 1
        r = ((v << 1) | c) & (0xFF if n == 1 else 0xFFFF); self._nz(r, n); return r

    def _ror(self, v, n):
        sh = 7 if n == 1 else 15
        c = self.fC; self.fC = v & 1
        r = (v >> 1) | (c << sh); self._nz(r, n); return r

    def _inc(self, v, n):
        r = (v + 1) & (0xFF if n == 1 else 0xFFFF); self._nz(r, n); return r

    def _dec(self, v, n):
        r = (v - 1) & (0xFF if n == 1 else 0xFFFF); self._nz(r, n); return r

    def op_ASL(self, mode, op): self._rmw(mode, self._asl)
    def op_LSR(self, mode, op): self._rmw(mode, self._lsr)
    def op_ROL(self, mode, op): self._rmw(mode, self._rol)
    def op_ROR(self, mode, op): self._rmw(mode, self._ror)
    def op_INC(self, mode, op): self._rmw(mode, self._inc)
    def op_DEC(self, mode, op): self._rmw(mode, self._dec)

    def op_TSB(self, mode, op):
        a = self.getA(self._acc_n())
        def f(v, n):
            self.fZ = int((a & v) == 0)
            return v | a
        self._rmw(mode, f)

    def op_TRB(self, mode, op):
        a = self.getA(self._acc_n())
        def f(v, n):
            self.fZ = int((a & v) == 0)
            return v & ~a
        self._rmw(mode, f)

    # ------------------------------------------------------------ implied / registers
    def op_INX(self, m, o): n = self._x_n(); self._set_index("X", self.X + 1, n)
    def op_INY(self, m, o): n = self._x_n(); self._set_index("Y", self.Y + 1, n)
    def op_DEX(self, m, o): n = self._x_n(); self._set_index("X", self.X - 1, n)
    def op_DEY(self, m, o): n = self._x_n(); self._set_index("Y", self.Y - 1, n)
    def op_NOP(self, m, o): pass

    def op_WDM(self, m, o): self.fetch8()

    def op_CLC(self, m, o): self.fC = 0
    def op_SEC(self, m, o): self.fC = 1
    def op_CLI(self, m, o): self.fI = 0
    def op_SEI(self, m, o): self.fI = 1
    def op_CLD(self, m, o): self.fD = 0
    def op_SED(self, m, o): self.fD = 1
    def op_CLV(self, m, o): self.fV = 0

    def op_TAX(self, m, o):                    # width of the destination: X [DS]
        n = self._x_n(); self._set_index("X", self.getA(n), n)

    def op_TAY(self, m, o):
        n = self._x_n(); self._set_index("Y", self.getA(n), n)

    def op_TXA(self, m, o):                    # width of the destination: A (m)
        n = self._acc_n(); v = self.X & (0xFF if n == 1 else 0xFFFF); self.setA(v, n); self._nz(v, n)

    def op_TYA(self, m, o):
        n = self._acc_n(); v = self.Y & (0xFF if n == 1 else 0xFFFF); self.setA(v, n); self._nz(v, n)

    def op_TXY(self, m, o): n = self._x_n(); self._set_index("Y", self.X, n)
    def op_TYX(self, m, o): n = self._x_n(); self._set_index("X", self.Y, n)

    def op_TSX(self, m, o): n = self._x_n(); self._set_index("X", self.S, n)

    def op_TXS(self, m, o):
        if self.E:
            self.S = 0x0100 | (self.X & 0xFF)           # no flags
        else:
            self.S = self.X & 0xFFFF

    def op_TCS(self, m, o):
        self.S = (0x0100 | (self.C & 0xFF)) if self.E else self.C   # no flags

    def op_TSC(self, m, o): self.C = self.S; self._nz(self.C, 2)
    def op_TCD(self, m, o): self.D = self.C; self._nz(self.D, 2)
    def op_TDC(self, m, o): self.C = self.D; self._nz(self.C, 2)

    def op_XBA(self, m, o):
        self.C = ((self.C & 0xFF) << 8) | (self.C >> 8)
        self._nz(self.C & 0xFF, 1)                      # N,Z from the new low byte

    def op_XCE(self, m, o):
        c, self.fC = self.fC, self.E
        self.E = c
        if self.E:
            self.fM = self.fX = 1
        self._fix_mx()

    def op_REP(self, m, o):
        v = self.fetch8()
        self.P = self.P & ~v & 0xFF             # P setter re-applies the E=1 rules

    def op_SEP(self, m, o):
        v = self.fetch8()
        self.P = (self.P | v) & 0xFF

    def op_WAI(self, m, o): self.halted = "WAI"
    def op_STP(self, m, o): self.halted = "STP"

    # ------------------------------------------------------------ stack ops
    def op_PHA(self, m, o):
        v = self.C
        if self.fM: self.push_old(v & 0xFF)
        else: self.push_old(v >> 8); self.push_old(v & 0xFF)

    def op_PLA(self, m, o):
        n = self._acc_n()
        v = self.pull_old()
        if n == 2: v |= self.pull_old() << 8
        self.setA(v, n); self._nz(v, n)

    def op_PHX(self, m, o): self._push_idx(self.X)
    def op_PHY(self, m, o): self._push_idx(self.Y)

    def _push_idx(self, v):
        if self.fX: self.push_old(v & 0xFF)
        else: self.push_old(v >> 8); self.push_old(v & 0xFF)

    def _pull_idx(self, reg):
        n = self._x_n()
        v = self.pull_old()
        if n == 2: v |= self.pull_old() << 8
        self._set_index(reg, v, n)

    def op_PLX(self, m, o): self._pull_idx("X")
    def op_PLY(self, m, o): self._pull_idx("Y")

    def op_PHP(self, m, o): self.push_old(self.P | (0x30 if self.E else 0))
    def op_PLP(self, m, o):
        v = self.pull_old()
        self.P = v | 0x30 if self.E else v

    def op_PHB(self, m, o): self.push_old(self.DB)
    def op_PLB(self, m, o): self.DB = self.pull_old(); self._nz(self.DB, 1)
    def op_PHK(self, m, o): self.push_old(self.PB)

    def op_PHD(self, m, o):
        self.push_new(self.D >> 8); self.push_new(self.D & 0xFF)

    def op_PLD(self, m, o):
        lo = self.pull_new(); self.D = lo | (self.pull_new() << 8); self._nz(self.D, 2)

    def op_PEA(self, m, o):
        v = self.fetch16(); self.push_new(v >> 8); self.push_new(v & 0xFF)

    def op_PEI(self, m, o):
        v = self.rd((self._dp(self.fetch8()), KDP), 2)
        self.push_new(v >> 8); self.push_new(v & 0xFF)

    def op_PER(self, m, o):
        disp = self.fetch16()
        v = (self.PC + disp) & 0xFFFF                   # PC already points after the operand
        self.push_new(v >> 8); self.push_new(v & 0xFF)

    # ------------------------------------------------------------ branches / jumps
    def _branch(self, cond):
        d = _s8(self.fetch8())
        if cond:
            self.PC = (self.PC + d) & 0xFFFF

    def op_BPL(self, m, o): self._branch(not self.fN)
    def op_BMI(self, m, o): self._branch(self.fN)
    def op_BVC(self, m, o): self._branch(not self.fV)
    def op_BVS(self, m, o): self._branch(self.fV)
    def op_BCC(self, m, o): self._branch(not self.fC)
    def op_BCS(self, m, o): self._branch(self.fC)
    def op_BNE(self, m, o): self._branch(not self.fZ)
    def op_BEQ(self, m, o): self._branch(self.fZ)
    def op_BRA(self, m, o): self._branch(True)

    def op_BRL(self, m, o):
        d = _s16(self.fetch16())
        self.PC = (self.PC + d) & 0xFFFF

    def op_JMP(self, mode, op):
        if mode == "abs":
            self.PC = self.fetch16()
        elif mode == "absind":
            p = self.fetch16()
            if self.nmos:
                if (p & 0xFF) == 0xFF:
                    self.events.append(("jmp_ind_page_bug", self.pc24() - 3, p))
                    if self.raise_on_jmp_bug:
                        raise JmpIndirectBug("JMP ($%04X) at %06X: NMOS fetches the high byte from $%04X"
                                             % (p, (self.pc24() - 3) & 0xFFFFFF, p & 0xFF00))
                hi_addr = (p & 0xFF00) | ((p + 1) & 0xFF)
                self.PC = self._rd(p) | (self._rd(hi_addr) << 8)
            else:                                        # bank 0, 16-bit wrap in bank 0 [DS]
                self.PC = self.rd((p, K16), 2)
        elif mode == "absindx":
            p = (self.fetch16() + self._idx(self.X)) & 0xFFFF
            self.PC = self.rd(((self.PB << 16) | p, K16), 2)

    def op_JML(self, mode, op):
        if mode == "long":
            t = self.fetch24()
        else:                                            # [abs] : 3 bytes from bank 0
            p = self.fetch16()
            ea = (p, K16)
            t = self.rd(ea, 2) | (self._rd(self._next(ea, 2)) << 16)
        self.PB, self.PC = (t >> 16) & 0xFF, t & 0xFFFF

    def op_JSR(self, mode, op):
        if mode == "abs":
            t = self.fetch16()
            r = (self.PC - 1) & 0xFFFF
            self.push_old(r >> 8); self.push_old(r & 0xFF)
            self.PC = t
        else:                                            # (abs,X): new stack ops [DS]
            lo = self.fetch8()
            r = self.PC                                  # points at the high operand byte
            self.push_new(r >> 8); self.push_new(r & 0xFF)
            hi = self.fetch8()
            p = ((hi << 8 | lo) + self._idx(self.X)) & 0xFFFF
            self.PC = self.rd(((self.PB << 16) | p, K16), 2)

    def op_JSL(self, mode, op):
        t = self.fetch24()
        r = (self.PC - 1) & 0xFFFF
        self.push_new(self.PB); self.push_new(r >> 8); self.push_new(r & 0xFF)
        self.PB, self.PC = (t >> 16) & 0xFF, t & 0xFFFF

    def op_RTS(self, mode, op):
        lo = self.pull_old(); hi = self.pull_old()
        self.PC = ((hi << 8 | lo) + 1) & 0xFFFF

    def op_RTL(self, mode, op):
        lo = self.pull_new(); hi = self.pull_new(); bk = self.pull_new()
        self.PC = ((hi << 8 | lo) + 1) & 0xFFFF
        self.PB = bk

    def _interrupt(self, kind, signature):
        # BRK/COP: PC+2 pushed (the signature byte is skipped) [PM]
        if signature:
            self.fetch8()
        if not self.E:
            self.push_old(self.PB)
        self.push_old(self.PC >> 8); self.push_old(self.PC & 0xFF)
        self.push_old(self.P | (0x30 if self.E else 0))   # E=1: B bit (4) set for BRK and COP
        if not self.nmos:
            self.fD = 0                                  # 65816 clears D on interrupt; NMOS does not [DS]/[NMOS]
        self.fI = 1
        vec = (VEC_EMU if self.E else VEC_NATIVE)[kind]
        self.PB = 0
        self.PC = self._rd(vec) | (self._rd(vec + 1) << 8)

    def op_BRK(self, m, o): self._interrupt("BRK", True)
    def op_COP(self, m, o): self._interrupt("COP", True)

    def op_RTI(self, mode, op):
        self.P = self.pull_old() | (0x30 if self.E else 0)
        lo = self.pull_old(); hi = self.pull_old()
        self.PC = hi << 8 | lo
        if not self.E:
            self.PB = self.pull_old()

    def _block(self, step):
        dst = self.fetch8()          # object code order: destination bank first, source bank second [DS]
        src = self.fetch8()
        self.DB = dst
        mask = 0xFF if self.fX else 0xFFFF
        while True:
            self._wr((dst << 16) | self.Y, self._rd((src << 16) | self.X))
            self.X = (self.X + step) & mask
            self.Y = (self.Y + step) & mask
            self.C = (self.C - 1) & 0xFFFF
            if self.C == 0xFFFF:
                break

    def op_MVN(self, m, o): self._block(+1)
    def op_MVP(self, m, o): self._block(-1)

    # ------------------------------------------------------------ disassembly (for traces)
    def format_trace(self, tail=16):
        out = []
        if self.trace is None:
            return out
        for (pc24, op, C, X, Y, S, P, E, D, DB) in list(self.trace)[-tail:]:
            m = bool(P & 0x20) or E
            x = bool(P & 0x10) or E
            txt = disasm_at(self.mem, pc24, m, x)
            out.append("%06X  %-26s C=%04X X=%04X Y=%04X S=%04X D=%04X DB=%02X P=%02X E=%d"
                       % (pc24, txt, C, X, Y, S, D, DB, P, E))
        return out


def disasm_at(mem, pc24, m=True, x=True):
    from .opcodes import TABLE, MODE_LEN
    op = mem.peek(pc24)
    mn, mode = TABLE[op]
    bank = pc24 & 0xFF0000
    def b(i): return mem.peek(bank | ((pc24 + i) & 0xFFFF))
    ln = MODE_LEN[mode]
    if ln is None:
        ln = 1 if (m if mode == "immM" else x) else 2
    ops = [b(1 + i) for i in range(ln)]
    v = 0
    for i, o in enumerate(ops):
        v |= o << (8 * i)
    raw = " ".join("%02X" % z for z in [op] + ops)
    f = {"imp": "", "acc": " A", "immM": " #$%0*X" % (2 * ln, v), "immX": " #$%0*X" % (2 * ln, v),
         "imm8": " #$%02X" % v, "dp": " $%02X" % v, "dpx": " $%02X,X" % v, "dpy": " $%02X,Y" % v,
         "abs": " $%04X" % v, "absx": " $%04X,X" % v, "absy": " $%04X,Y" % v, "long": " $%06X" % v,
         "longx": " $%06X,X" % v, "ind": " ($%02X)" % v, "indx": " ($%02X,X)" % v, "indy": " ($%02X),Y" % v,
         "ldp": " [$%02X]" % v, "ldpy": " [$%02X],Y" % v, "sr": " $%02X,S" % v, "sry": " ($%02X,S),Y" % v,
         "absind": " ($%04X)" % v, "absindx": " ($%04X,X)" % v, "absindl": " [$%04X]" % v,
         "rel": " +%d" % _s8(v), "rell": " +%d" % _s16(v), "blk": " $%02X,$%02X" % (ops[1], ops[0]) if ln == 2 else "",
         "pea": " #$%04X" % v, "pei": " ($%02X)" % v, "per": " +%d" % _s16(v)}[mode]
    return "%s%s [%s]" % (mn, f, raw)
