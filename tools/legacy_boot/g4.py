"""Gideon's USER4 chain over our USB-Blaster, the way recover.py drives it: IR USER4 is loaded again
before every data scan (without that, writes to the chain do not take)."""
import sys, struct, time
sys.path.insert(0, "/path/to/Development/1541ultimate-tools/tooling")
import u64ii_jtag as jt
from u64ii_bscan import Blaster

class G4:
    def __init__(self):
        self.cab = Blaster.open(); self.tap = jt.Tap(self.cab); self.tap.reset()
    def close(self):
        self.tap.reset(); self.cab.release()
    def scan(self, reg, tdi, nbits, read=False):
        t = self.tap
        t.ir(jt.IR_USER4); t.dr(1 | (reg << 1), 5); t.ir(jt.IR_USER4)
        return t.dr(tdi << 1, nbits + 1, read) >> 1
    def user_id(self): return self.scan(0, 0, 32, True)
    def outputs(self, v): self.scan(2, v, 8)
    def _cmd(self, words):
        tdi = 0
        for i, w in enumerate(words): tdi |= (w & 0xFFFF) << (16 * i)
        self.scan(5, tdi, 16 * len(words))
    @staticmethod
    def _addr(a): return [((0x4 | i) << 8) | ((a >> (8 * i)) & 0xFF) for i in range(4)]
    def write(self, a, data):
        self._cmd(self._addr(a) + [0x0180])
        data = data + bytes(8)                                   # recover.py pads every write
        self.scan(6, int.from_bytes(data, "little"), 8 * len(data))
    def drain(self, reg, pops):
        n = 8 * (pops + 1)
        v = self.scan(reg, 1 << (n - 1), n, True)
        return v & 0xFF, (v >> 8).to_bytes(pops, "little") if pops else b""
    def read(self, a, length, words_per=64):
        out = bytearray()
        while len(out) < length:
            w = min((length - len(out)) // 4, words_per)
            self._cmd(self._addr(a + len(out)) + [0x0300 | (w - 1)])
            block, known, stalls = bytearray(), 0, 0
            while len(block) < 4 * w:
                pops = min(known, 4 * w - len(block))
                level, d = self.drain(4, pops); block += d
                known = max(0, level - pops)
                if not pops and not level:
                    stalls += 1
                    if stalls > 50: raise RuntimeError("read stalled at %08X" % (a + len(out)))
                else: stalls = 0
            out += block
        return bytes(out)
    def console(self):
        out = b""
        for _ in range(400):
            level, _d = self.drain(0xA, 0)
            if not level: break
            _l, d = self.drain(0xA, min(level, 32)); out += d
        return out.decode(errors="replace")

if __name__ == "__main__":
    g = G4(); print("id %08X" % g.user_id())
    g.write(0x03F00000, bytes(range(16)))
    print("readback", g.read(0x03F00000, 16).hex(" "))
    g.close()
