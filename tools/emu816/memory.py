"""Sparse 16 MiB memory with region hooks.

Hooks: Memory.add_hook(lo, hi, read=None, write=None). Hooks are consulted in registration order.
  read(addr)  -> int (0..255) to answer, or None to fall through to RAM
  write(addr, value) -> True if the write is consumed (RAM untouched), falsy to also store in RAM
Hooks see every CPU access (and every REU DMA access that goes through read()/write()).
No timing, no bus side effects other than what hooks implement.
"""


class Memory:
    def __init__(self, fill=0x00):
        self.banks = {}           # bank -> bytearray(65536)
        self.fill = fill & 0xFF
        self._hooks = {}          # page (addr>>8) -> [(lo, hi, read, write)]
        self.post_step = []       # callbacks run after each CPU instruction (REU transfers)
        self.write_log = None     # None = off, list = log every write as (addr, value)
        self.read_log = None

    # ---- raw RAM (no hooks) ----
    def _bank(self, b, create=False):
        bk = self.banks.get(b)
        if bk is None and create:
            bk = bytearray([self.fill]) * 65536
            self.banks[b] = bk
        return bk

    def peek(self, addr):
        addr &= 0xFFFFFF
        bk = self.banks.get(addr >> 16)
        return self.fill if bk is None else bk[addr & 0xFFFF]

    def poke(self, addr, value):
        addr &= 0xFFFFFF
        self._bank(addr >> 16, True)[addr & 0xFFFF] = value & 0xFF

    def load(self, addr, data):
        for i, b in enumerate(data):
            self.poke(addr + i, b)

    def dump(self, addr, n):
        return bytes(self.peek(addr + i) for i in range(n))

    # ---- hooks ----
    def add_hook(self, lo, hi, read=None, write=None):
        h = (lo, hi, read, write)
        for p in range(lo >> 8, (hi >> 8) + 1):
            self._hooks.setdefault(p, []).append(h)

    # ---- CPU-visible access ----
    def read(self, addr):
        addr &= 0xFFFFFF
        hk = self._hooks
        if hk:
            lst = hk.get(addr >> 8)
            if lst:
                for lo, hi, rd, _ in lst:
                    if rd is not None and lo <= addr <= hi:
                        v = rd(addr)
                        if v is not None:
                            v &= 0xFF
                            if self.read_log is not None:
                                self.read_log.append((addr, v))
                            return v
        bk = self.banks.get(addr >> 16)
        v = self.fill if bk is None else bk[addr & 0xFFFF]
        if self.read_log is not None:
            self.read_log.append((addr, v))
        return v

    def write(self, addr, value):
        addr &= 0xFFFFFF
        value &= 0xFF
        if self.write_log is not None:
            self.write_log.append((addr, value))
        hk = self._hooks
        if hk:
            lst = hk.get(addr >> 8)
            if lst:
                consumed = False
                for lo, hi, _, wr in lst:
                    if wr is not None and lo <= addr <= hi:
                        if wr(addr, value):
                            consumed = True
                if consumed:
                    return
        self._bank(addr >> 16, True)[addr & 0xFFFF] = value


class IORegion:
    """Fixture-driven I/O window: reads return scripted values, writes are logged and consumed.

    reads: {addr: int | [v0, v1, ...]}  a list is consumed in order, the last value repeats.
    default: value for addresses not in `reads`.
    """

    def __init__(self, lo, hi, reads=None, default=0x00):
        self.lo, self.hi = lo, hi
        self.default = default & 0xFF
        self.script = {}
        for a, v in (reads or {}).items():
            self.script[int(a)] = list(v) if isinstance(v, (list, tuple)) else [v]
        self.pos = {}
        self.writes = []          # (addr, value) in order
        self.reads = []           # (addr, value) in order

    def attach(self, mem):
        mem.add_hook(self.lo, self.hi, self.read, self.write)
        return self

    def read(self, addr):
        s = self.script.get(addr)
        if s is None:
            v = self.default
        else:
            i = self.pos.get(addr, 0)
            v = s[min(i, len(s) - 1)]
            self.pos[addr] = i + 1
        self.reads.append((addr, v & 0xFF))
        return v & 0xFF

    def write(self, addr, value):
        self.writes.append((addr, value & 0xFF))
        return True
