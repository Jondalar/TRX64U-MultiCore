"""Offline reference for the SID suite: a cycle-counting 6502 (documented opcodes) in front of TRX64's vendored
reSID (VICE 3.10 resid 1.0-pre2). The test programs run with their real bus timing, every SID access clocks the
chip up to its cycle first. This is where --selftest takes the expected values from (6581 and 8580).

Not modelled: VIC (only a PAL raster counter for $D011/$D012, 63 cycles per line, no badlines, no sprites), CIA,
interrupts (the programs are SEI), banking ($01 ignored, I/O always visible). TRX64's own CPU read path for
$D41B/$D41C is a fastsid shadow, not reSID, so it is not used here.
"""
import ctypes, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
CYC_LINE, LINES = 63, 312
SAMPLE_RESAMPLE = 2   # resid_ffi.rs SAMPLE_RESAMPLE


def _lib():
    ext = "dylib" if sys.platform == "darwin" else "so"
    p = os.path.join(HERE, ".cache", "libresid." + ext)
    if not os.path.exists(p):
        raise SystemExit("libresid missing: run tests/sid/build_resid.sh (needs the TRX64 checkout)")
    L = ctypes.CDLL(p)
    L.resid_new.restype = ctypes.c_void_p
    for f in ("resid_reset_h", "resid_delete"):
        getattr(L, f).argtypes = [ctypes.c_void_p]
    L.resid_set_chip_model_h.argtypes = [ctypes.c_void_p, ctypes.c_int]
    L.resid_write_h.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_int]
    L.resid_read_h.argtypes = [ctypes.c_void_p, ctypes.c_int]
    L.resid_clock_silent_h.argtypes = [ctypes.c_void_p, ctypes.c_int]
    L.resid_clock_h.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_int]
    L.resid_clock_remaining_h.argtypes = [ctypes.c_void_p]
    for f in ("resid_reinit_h",):
        getattr(L, f).argtypes = [ctypes.c_void_p]
    L.resid_set_voice_mask_h.argtypes = [ctypes.c_void_p, ctypes.c_int]
    L.resid_enable_filter_h.argtypes = [ctypes.c_void_p, ctypes.c_int]
    L.resid_enable_external_filter_h.argtypes = [ctypes.c_void_p, ctypes.c_int]
    L.resid_adjust_filter_bias_h.argtypes = [ctypes.c_void_p, ctypes.c_double]
    L.resid_set_sampling_h.argtypes = [ctypes.c_void_p, ctypes.c_double, ctypes.c_double, ctypes.c_int, ctypes.c_double, ctypes.c_double]
    return L


class Sid:
    def __init__(self, chip):
        self.L = _lib()
        self.h = self.L.resid_new()
        L = self.L   # TRX64's own order (resid_ffi.rs configure): the filter is OFF, sampling is set
        pass
        L.resid_set_chip_model_h(self.h, 0 if chip == "6581" else 1)
        L.resid_set_voice_mask_h(self.h, 7)
        L.resid_enable_filter_h(self.h, 0)
        L.resid_adjust_filter_bias_h(self.h, 0.5)
        L.resid_enable_external_filter_h(self.h, 1)
        L.resid_set_sampling_h(self.h, 985248.0, 44100.0, SAMPLE_RESAMPLE, 44100 * 90.0 / 200.0, 0.97)
        self.t = 0
        self.buf = (ctypes.c_short * 4096)()

    def to(self, cyc):
        # Single-cycle clocking through the sampler, like VICE/TRX64 do for audio: the pipelined ENV3 / OSC3
        # (env3, the 8580 delay) are only modelled there, the bulk clock(delta) does not update them.
        d = cyc - self.t
        self.t = max(self.t, cyc)
        while d > 0:
            self.L.resid_clock_h(self.h, d, self.buf, 4096)
            d = self.L.resid_clock_remaining_h(self.h)

    def write(self, cyc, reg, v):
        self.to(cyc)
        self.L.resid_write_h(self.h, reg, v)

    def read(self, cyc, reg):
        self.to(cyc)
        return self.L.resid_read_h(self.h, reg) & 255


class Cpu:
    def __init__(self, chip, sid_base=0xD400):
        self.m = bytearray(65536)
        self.sid = Sid(chip)
        self.base = sid_base
        self.a = self.x = self.y = 0
        self.sp = 0xFD
        self.p = 0x24
        self.pc = 0
        self.cyc = 0
        self.jam = False
        self.exit = False
        self.m[0x1FE], self.m[0x1FF] = 0xFD, 0xFF   # RTS of the entry routine lands on $FFFE = done

    # --- bus (the access happens on the last cycle of the instruction: self.cyc already points there)
    def rd(self, a):
        if self.base <= a < self.base + 0x20:
            return self.sid.read(self.cyc, a - self.base)
        if a == 0xD012:
            return ((self.cyc // CYC_LINE) % LINES) & 255
        if a == 0xD011:
            return (self.m[0xD011] & 0x7F) | ((((self.cyc // CYC_LINE) % LINES) >> 8) << 7)
        return self.m[a]

    def wr(self, a, v):
        if self.base <= a < self.base + 0x20:
            self.sid.write(self.cyc, a - self.base, v)
        self.m[a] = v
        if a == 0xD7FF:      # the VICE testprogs' exit protocol: $D7FF = 0 pass / $FF fail ends the program
            self.exit = True

    def load_prg(self, data):
        la = data[0] | data[1] << 8
        self.m[la:la + len(data) - 2] = data[2:]
        return la

    # --- helpers
    def nz(self, v):
        v &= 255
        self.p = (self.p & 0x7D) | (v & 0x80) | (2 if v == 0 else 0)
        return v

    def push(self, v):
        self.m[0x100 + self.sp] = v & 255
        self.sp = (self.sp - 1) & 255

    def pull(self):
        self.sp = (self.sp + 1) & 255
        return self.m[0x100 + self.sp]

    def run(self, until_pc=None, max_cycles=50_000_000):
        """Run until PC == until_pc or a JMP-to-self / RTS with empty stack."""
        m = self.m
        while self.cyc < max_cycles:
            pc = self.pc
            if pc == until_pc:
                return "pc"
            op = m[pc]
            n = self.step(op)
            if self.exit:
                return "exit"
            if n is None:
                return "stop"
        return "limit"

    def step(self, op):
        m = self.m
        pc = self.pc
        b1, b2 = m[(pc + 1) & 0xFFFF], m[(pc + 2) & 0xFFFF]
        abs_ = b1 | b2 << 8
        start = self.cyc

        def finish(length, cycles):
            self.pc = (pc + length) & 0xFFFF
            self.cyc = start + cycles

        def at(cycles):          # the bus access cycle: the last one
            self.cyc = start + cycles - 1

        # addressing resolvers: (effective address, length, cycles, page-cross-extra)
        def ea(mode):
            if mode == "zp": return b1, 2, 3, 0
            if mode == "zpx": return (b1 + self.x) & 255, 2, 4, 0
            if mode == "zpy": return (b1 + self.y) & 255, 2, 4, 0
            if mode == "abs": return abs_, 3, 4, 0
            if mode == "abx": return (abs_ + self.x) & 0xFFFF, 3, 4, int((abs_ & 255) + self.x > 255)
            if mode == "aby": return (abs_ + self.y) & 0xFFFF, 3, 4, int((abs_ & 255) + self.y > 255)
            if mode == "inx":
                z = (b1 + self.x) & 255
                return m[z] | m[(z + 1) & 255] << 8, 2, 6, 0
            if mode == "iny":
                lo = m[b1] | m[(b1 + 1) & 255] << 8
                return (lo + self.y) & 0xFFFF, 2, 5, int((lo & 255) + self.y > 255)
            raise ValueError(mode)

        MODES = {  # opcode group c=01
            0x00: "inx", 0x04: "zp", 0x08: "imm", 0x0C: "abs", 0x10: "iny", 0x14: "zpx", 0x18: "aby", 0x1C: "abx"}
        # --- implied / single byte
        simple = {
            0xEA: None, 0x18: ("p", 0xFE, 0), 0x38: ("p", 0xFF, 1), 0x58: ("p", 0xFB, 0), 0x78: ("p", 0xFF, 4),
            0xB8: ("p", 0xBF, 0), 0xD8: ("p", 0xF7, 0), 0xF8: ("p", 0xFF, 8)}
        if op in simple:
            v = simple[op]
            if v:
                _, mask, setb = v
                self.p = (self.p & mask) | (setb if mask == 0xFF else 0)
                if mask == 0xFF: self.p = self.p | setb
            finish(1, 2); return 1
        if op == 0xAA: self.x = self.nz(self.a); finish(1, 2); return 1
        if op == 0xA8: self.y = self.nz(self.a); finish(1, 2); return 1
        if op == 0x8A: self.a = self.nz(self.x); finish(1, 2); return 1
        if op == 0x98: self.a = self.nz(self.y); finish(1, 2); return 1
        if op == 0xBA: self.x = self.nz(self.sp); finish(1, 2); return 1
        if op == 0x9A: self.sp = self.x; finish(1, 2); return 1
        if op == 0xE8: self.x = self.nz(self.x + 1); finish(1, 2); return 1
        if op == 0xC8: self.y = self.nz(self.y + 1); finish(1, 2); return 1
        if op == 0xCA: self.x = self.nz(self.x - 1); finish(1, 2); return 1
        if op == 0x88: self.y = self.nz(self.y - 1); finish(1, 2); return 1
        if op == 0x48: self.push(self.a); finish(1, 3); return 1
        if op == 0x68: self.a = self.nz(self.pull()); finish(1, 4); return 1
        if op == 0x08: self.push(self.p | 0x30); finish(1, 3); return 1
        if op == 0x28: self.p = (self.pull() & 0xCF) | 0x20; finish(1, 4); return 1
        if op == 0x4A:
            self.p = (self.p & 0xFE) | (self.a & 1); self.a = self.nz(self.a >> 1); finish(1, 2); return 1
        if op == 0x0A:
            c = self.a >> 7; self.a = self.nz(self.a << 1); self.p = (self.p & 0xFE) | c; finish(1, 2); return 1
        if op == 0x2A:
            c = self.a >> 7; self.a = self.nz(self.a << 1 | (self.p & 1)); self.p = (self.p & 0xFE) | c; finish(1, 2); return 1
        if op == 0x6A:
            c = self.a & 1; self.a = self.nz(self.a >> 1 | (self.p & 1) << 7); self.p = (self.p & 0xFE) | c; finish(1, 2); return 1
        # --- jumps
        if op == 0x4C:
            if abs_ == pc:
                return None
            finish(3, 3); self.pc = abs_; return 3
        if op == 0x20:
            self.push((pc + 2) >> 8); self.push((pc + 2) & 255); finish(3, 6); self.pc = abs_; return 3
        if op == 0x60:
            lo = self.pull(); hi = self.pull()
            tgt = ((lo | hi << 8) + 1) & 0xFFFF
            if tgt == 0xFFFE:   # the sentinel staged below: the program returned to its caller
                return None
            finish(1, 6); self.pc = tgt; return 1
        # --- branches
        br = {0x10: (0x80, 0), 0x30: (0x80, 1), 0x50: (0x40, 0), 0x70: (0x40, 1), 0x90: (1, 0), 0xB0: (1, 1), 0xD0: (2, 0), 0xF0: (2, 1)}
        if op in br:
            flag, want = br[op]
            take = bool(self.p & flag) == bool(want)
            off = b1 - 256 if b1 > 127 else b1
            tgt = (pc + 2 + off) & 0xFFFF
            if take:
                cyc = 3 + int((pc + 2) >> 8 != tgt >> 8)
                finish(2, cyc); self.pc = tgt
            else:
                finish(2, 2)
            return 2
        # --- group c=01 ALU (ora and eor adc sta lda cmp sbc)
        c = op & 3; hi3 = (op >> 5) & 7; mb = op & 0x1C
        if c == 1:
            mode = MODES[mb]
            names = ["ora", "and", "eor", "adc", "sta", "lda", "cmp", "sbc"]
            nme = names[hi3]
            if mode == "imm":
                val = b1; ln, cy, ex = 2, 2, 0
            else:
                addr, ln, cy, ex = ea(mode)
                if nme == "sta" and mode in ("abx", "aby", "iny"):
                    ex = 1
                at(cy + ex)
                if nme == "sta": self.wr(addr, self.a); finish(ln, cy + ex); return ln
                val = self.rd(addr)
            if nme == "lda": self.a = self.nz(val)
            elif nme == "ora": self.a = self.nz(self.a | val)
            elif nme == "and": self.a = self.nz(self.a & val)
            elif nme == "eor": self.a = self.nz(self.a ^ val)
            elif nme == "cmp":
                r = self.a - val
                self.p = (self.p & 0xFE) | (1 if r >= 0 else 0); self.nz(r)
            elif nme in ("adc", "sbc"):
                v = val if nme == "adc" else val ^ 255
                r = self.a + v + (self.p & 1)
                ov = (~(self.a ^ v) & (self.a ^ r) & 0x80) != 0
                self.p = (self.p & 0xBE) | (1 if r > 255 else 0) | (0x40 if ov else 0)
                self.a = self.nz(r)
            finish(ln, cy + ex); return ln
        # --- group c=10 / c=00 loads, stores, inc/dec, bit, cpx/cpy
        def rmw(addr, ln, cy, fn):
            at(cy + 2)   # read at cy, write at the end (approx.)
            v = self.rd(addr); self.wr(addr, v); nv = fn(v); self.wr(addr, nv)
            finish(ln, cy + 2)
            return ln
        if op in (0xA2, 0xA0):       # ldx/ldy imm
            if op == 0xA2: self.x = self.nz(b1)
            else: self.y = self.nz(b1)
            finish(2, 2); return 2
        if op in (0xC9, 0xE0, 0xC0):
            pass
        ldst = {0xA6: ("ldx", "zp"), 0xB6: ("ldx", "zpy"), 0xAE: ("ldx", "abs"), 0xBE: ("ldx", "aby"),
                0xA4: ("ldy", "zp"), 0xB4: ("ldy", "zpx"), 0xAC: ("ldy", "abs"), 0xBC: ("ldy", "abx"),
                0x86: ("stx", "zp"), 0x96: ("stx", "zpy"), 0x8E: ("stx", "abs"),
                0x84: ("sty", "zp"), 0x94: ("sty", "zpx"), 0x8C: ("sty", "abs"),
                0x24: ("bit", "zp"), 0x2C: ("bit", "abs"),
                0xE4: ("cpx", "zp"), 0xEC: ("cpx", "abs"), 0xC4: ("cpy", "zp"), 0xCC: ("cpy", "abs")}
        if op in (0xE0, 0xC0):
            r = (self.x if op == 0xE0 else self.y) - b1
            self.p = (self.p & 0xFE) | (1 if r >= 0 else 0); self.nz(r); finish(2, 2); return 2
        if op in ldst:
            nme, mode = ldst[op]
            addr, ln, cy, ex = ea(mode)
            at(cy + ex)
            if nme == "stx": self.wr(addr, self.x)
            elif nme == "sty": self.wr(addr, self.y)
            else:
                v = self.rd(addr)
                if nme == "ldx": self.x = self.nz(v)
                elif nme == "ldy": self.y = self.nz(v)
                elif nme == "bit": self.p = (self.p & 0x3D) | (v & 0xC0) | (2 if (v & self.a) == 0 else 0)
                else:
                    r = (self.x if nme == "cpx" else self.y) - v
                    self.p = (self.p & 0xFE) | (1 if r >= 0 else 0); self.nz(r)
            finish(ln, cy + ex); return ln
        rm = {0xE6: "zp", 0xF6: "zpx", 0xEE: "abs", 0xFE: "abx", 0xC6: "zp", 0xD6: "zpx", 0xCE: "abs", 0xDE: "abx"}
        if op in rm:
            addr, ln, cy, _ = ea(rm[op])
            inc = op in (0xE6, 0xF6, 0xEE, 0xFE)
            return rmw(addr, ln, cy + (1 if rm[op] == "abx" else 0), lambda v: self.nz(v + (1 if inc else -1)))
        raise RuntimeError("opcode %02x at %04x not in the offline CPU" % (op, pc))
