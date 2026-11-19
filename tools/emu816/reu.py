"""Commodore 1764/1750-style REU (REC) register model at $DF00-$DF0A, instruction-set unit-test grade.

Register semantics are a line-by-line port of the TRX64 implementation, which itself ports VICE reu.c:
  TRX64 crates/trx64-core/src/reu.rs   (cited below as reu.rs:LINE)
  TRX64 crates/trx64-core/tests/reu_gate.rs (behaviours: reu_gate.rs:LINE)
No timing, no BA/VIC stealing. A transfer runs at the instruction boundary after the write that
starts it (reu.rs:486-489 and the DmaBus contract), via Memory.post_step.

Host (C64) side goes through Memory.read/write of bank 0, i.e. through I/O hooks (so a FETCH
into $D800 shows up in an IORegion write log).
"""

STATUS_256K, STATUS_VERIFY, STATUS_EOB, STATUS_IRQ = 0x10, 0x20, 0x40, 0x80     # reu.rs:36-39
CMD_MASK, CMD_STASH, CMD_FETCH, CMD_SWAP, CMD_VERIFY = 0x03, 0, 1, 2, 3          # reu.rs:42-46
CMD_FF00_DISABLED, CMD_AUTOLOAD, CMD_EXECUTE = 0x10, 0x20, 0x80                  # reu.rs:47-49
AC_FIX_REC, AC_FIX_C64 = 0x40, 0x80                                              # reu.rs:62-63
INT_VERIFY, INT_EOB, INT_ENABLE = 0x20, 0x40, 0x80                               # reu.rs:56-58
FIRST_UNUSED = 0x0B                                                              # reu.rs:33


class REU:
    def __init__(self, mem, size_kb=16384, base=0xDF00, host_read=None, host_write=None):
        """size_kb: 128 (1700), 256 (1764), 512 (1750) or 1024..16384 (reu.rs:109-137).
        Default 16384 so REU address == 24-bit address of the original (no mapping needed)."""
        if size_kb not in (128, 256, 512, 1024, 2048, 4096, 8192, 16384):
            raise ValueError("unknown REU size %r" % size_kb)    # reu.rs:134 returns None
        self.mem = mem
        self.size_kb = size_kb
        self.host_read = host_read or (lambda a: mem.read(a & 0xFFFF))
        self.host_write = host_write or (lambda a, v: mem.write(a & 0xFFFF, v))
        # per-model options, reu.rs:109-137
        self.wrap_around = 0x80000
        self.dram_wrap = 0x80000
        self.not_backed = size_kb << 10
        self.wrap_mask_store = 0x80000 - 1
        self.bank_unused = 0xF8
        self.status_preset = STATUS_256K
        if size_kb == 128:
            self.status_preset = 0
            self.wrap_around = self.dram_wrap = 0x20000
        elif size_kb >= 1024:
            self.bank_unused = 0
            self.dram_wrap = size_kb * 1024
            self.wrap_mask_store = size_kb * 1024 - 1
            # wrap_around stays 512K: the "wraparound bug" (reu.rs:127-131)
        self.pages = {}                 # 64K REU pages, sparse; absent = 0
        self.floating = 0xFF            # reu.rs:178
        self.irq = False
        self.pending = None             # None | "now" | "ff00"
        self.transfers = []             # log of executed transfers (dict)
        self.reset()
        # register file window $DF00-$DFFF (mirrors every $20) and $FF00 snoop (reu.rs:776-780, 801-821)
        mem.add_hook(base, base + 0xFF, self._read, self._write)
        mem.add_hook(0xFF00, 0xFF00, None, self._snoop_ff00)
        mem.post_step.append(self._boundary)

    # ---- REU RAM ----
    def ram_read(self, a):
        pg = self.pages.get(a >> 16)
        return 0 if pg is None else pg[a & 0xFFFF]

    def ram_write(self, a, v):
        pg = self.pages.get(a >> 16)
        if pg is None:
            pg = self.pages[a >> 16] = bytearray(65536)
        pg[a & 0xFFFF] = v & 0xFF

    def load(self, addr, data):
        for i, b in enumerate(data):
            self.ram_write(addr + i, b)

    def dump(self, addr, n):
        return bytes(self.ram_read(addr + i) for i in range(n))

    def _store_to_reu(self, a, v):        # reu.rs:327-334 : above fitted DRAM -> dropped
        a &= self.dram_wrap - 1
        if a < self.not_backed:
            self.ram_write(a, v)

    def _read_from_reu(self, a):          # reu.rs:337-345 : no DRAM -> bus latch
        a &= self.dram_wrap - 1
        return self.ram_read(a) if a < self.not_backed else self.floating

    def _inc_reu(self, a, step):          # reu.rs:348-354 : only the low 19 bits belong to the REC
        nxt = (a & 0x7FFFF) + step
        if nxt == self.wrap_around:
            nxt = 0
        return (a & 0xF80000) | nxt

    # ---- registers ----
    def reset(self):                      # reu.rs:308-322; RAM untouched, FF00 trigger disabled
        self.status = self.status_preset
        self.command = CMD_FF00_DISABLED
        self.base_c64 = self.base_reu = 0
        self.bank = self.bank_unused
        self.length = 0xFFFF
        self.int_mask = 0x1F
        self.addr_ctl = 0x3F
        self.sh_c64 = self.sh_reu = 0
        self.sh_bank = self.bank_unused
        self.sh_len = 0xFFFF
        self.irq = False
        self.pending = None

    def _peek_reg(self, r):               # reu.rs:359-374
        if r == 0: return self.status
        if r == 1: return self.command
        if r == 2: return self.base_c64 & 0xFF
        if r == 3: return self.base_c64 >> 8
        if r == 4: return self.base_reu & 0xFF
        if r == 5: return self.base_reu >> 8
        if r == 6: return self.bank | self.bank_unused
        if r == 7: return self.length & 0xFF
        if r == 8: return self.length >> 8
        if r == 9: return self.int_mask
        if r == 10: return self.addr_ctl
        return 0xFF

    def _read(self, addr):                # reu.rs:423-442
        r = addr & 0x1F
        if r >= FIRST_UNUSED:
            return 0xFF
        v = self._peek_reg(r)
        if r == 0:
            self.status &= ~(STATUS_VERIFY | STATUS_EOB | STATUS_IRQ) & 0xFF
            self.irq = False
        elif r == 6:
            v |= 0xF8
        return v

    def _store_reg(self, r, b):           # reu.rs:379-419 (shadow registers = Half-Autoload-Bug)
        if r == 1: self.command = b
        elif r == 2: self.sh_c64 = (self.sh_c64 & 0xFF00) | b; self.base_c64 = self.sh_c64
        elif r == 3: self.sh_c64 = (self.sh_c64 & 0xFF) | (b << 8); self.base_c64 = self.sh_c64
        elif r == 4: self.sh_reu = (self.sh_reu & 0xFF00) | b; self.base_reu = self.sh_reu
        elif r == 5: self.sh_reu = (self.sh_reu & 0xFF) | (b << 8); self.base_reu = self.sh_reu
        elif r == 6: self.sh_bank = b & ~self.bank_unused & 0xFF; self.bank = self.sh_bank
        elif r == 7: self.sh_len = (self.sh_len & 0xFF00) | b; self.length = self.sh_len
        elif r == 8: self.sh_len = (self.sh_len & 0xFF) | (b << 8); self.length = self.sh_len
        elif r == 9: self.int_mask = b | 0x1F
        elif r == 10: self.addr_ctl = b | 0x3F

    def _write(self, addr, b):            # reu.rs:445-481
        r = addr & 0x1F
        if r < FIRST_UNUSED:
            self._store_reg(r, b)
            if r == 1 and self.command & CMD_EXECUTE:
                self.pending = "now" if self.command & CMD_FF00_DISABLED else "ff00"
            elif r == 9:
                m = self.int_mask
                if (m & (INT_EOB | INT_ENABLE)) == (INT_EOB | INT_ENABLE) and self.status & STATUS_EOB:
                    self.status |= STATUS_IRQ; self.irq = True
                if (m & (INT_VERIFY | INT_ENABLE)) == (INT_VERIFY | INT_ENABLE) and self.status & STATUS_VERIFY:
                    self.status |= STATUS_IRQ; self.irq = True
        return True      # register window never reaches RAM

    def _snoop_ff00(self, addr, b):       # reu.rs:805-820 : armed -> run at boundary
        if self.pending == "ff00":
            self.pending = "now"
        return False     # the byte also lands in RAM

    def _boundary(self):
        if self.pending == "now":
            self.run()

    # ---- the transfer (reu.rs:516-544) ----
    def run(self):
        self.pending = None
        host = self.base_c64
        ra = self.base_reu | (self.bank << 16)
        ln = 0x10000 if self.length == 0 else self.length                   # length 0 = 64 KiB
        hs = 0 if self.addr_ctl & AC_FIX_C64 else 1
        rs = 0 if self.addr_ctl & AC_FIX_REC else 1
        t = self.command & CMD_MASK
        rec = {"type": ("stash", "fetch", "swap", "verify")[t], "c64": host, "reu": ra, "len": ln}
        self.transfers.append(rec)
        if t == CMD_STASH:
            host, ra, ln = self._stash(host, ra, hs, rs, ln)
        elif t == CMD_FETCH:
            host, ra, ln = self._fetch(host, ra, hs, rs, ln)
        elif t == CMD_SWAP:
            host, ra, ln = self._swap(host, ra, hs, rs, ln)
        else:
            host, ra, ln = self._verify(host, ra, hs, rs, ln)
        self.command = (self.command & ~CMD_EXECUTE & 0xFF) | CMD_FF00_DISABLED

    def _update(self, host, ra, ln, new_status):     # reu.rs:548-583
        ra &= self.wrap_mask_store
        self.status |= new_status
        if not self.command & CMD_AUTOLOAD:
            if not self.addr_ctl & AC_FIX_C64:
                self.base_c64 = host & 0xFFFF
            if not self.addr_ctl & AC_FIX_REC:
                self.base_reu = ra & 0xFFFF
                self.bank = (ra >> 16) & 0xFF
            self.length = ln & 0xFFFF
        else:
            self.base_c64, self.base_reu = self.sh_c64, self.sh_reu
            self.bank, self.length = self.sh_bank, self.sh_len
        m = self.int_mask
        if new_status & STATUS_EOB and (m & (INT_EOB | INT_ENABLE)) == (INT_EOB | INT_ENABLE):
            self.status |= STATUS_IRQ; self.irq = True
        if new_status & STATUS_VERIFY and (m & (INT_VERIFY | INT_ENABLE)) == (INT_VERIFY | INT_ENABLE):
            self.status |= STATUS_IRQ; self.irq = True

    def _stash(self, host, ra, hs, rs, ln):          # reu.rs:586-607
        v = 0
        while ln:
            v = self.host_read(host)
            self._store_to_reu(ra, v)
            host = (host + hs) & 0xFFFF
            ra = self._inc_reu(ra, rs)
            ln -= 1
        self._update(host, ra, ln + 1, STATUS_EOB)
        self.floating = v
        return host, ra, ln

    def _fetch(self, host, ra, hs, rs, ln):          # reu.rs:610-634
        while ln:
            v = self._read_from_reu(ra)
            self.floating = v
            self.host_write(host, v)
            host = (host + hs) & 0xFFFF
            ra = self._inc_reu(ra, rs)
            ln -= 1
        self._update(host, ra, ln + 1, STATUS_EOB)
        self.floating = self._read_from_reu(ra)
        return host, ra, ln

    def _swap(self, host, ra, hs, rs, ln):           # reu.rs:637-661
        while ln:
            fr = self._read_from_reu(ra)
            fc = self.host_read(host)
            self._store_to_reu(ra, fc)
            self.host_write(host, fr)
            host = (host + hs) & 0xFFFF
            ra = self._inc_reu(ra, rs)
            ln -= 1
        self._update(host, ra, ln + 1, STATUS_EOB)
        return host, ra, ln

    def _verify(self, host, ra, hs, rs, ln):         # reu.rs:666-710
        ns = 0
        while ln:
            fr = self._read_from_reu(ra)
            fc = self.host_read(host)
            ra = self._inc_reu(ra, rs)
            host = (host + hs) & 0xFFFF
            ln -= 1
            if fr != fc:
                ns |= STATUS_VERIFY
                break
        if ln == 0:
            ln += 1
            ns |= STATUS_EOB            # weirdness 2: last byte failed (or all equal) -> EOB too
        elif ln == 1:                   # weirdness 3
            if self._read_from_reu(ra) == self.host_read(host):
                ns |= STATUS_EOB
        self._update(host, ra, ln, ns)
        return host, ra, ln
