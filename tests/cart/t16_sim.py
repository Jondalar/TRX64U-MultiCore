"""Simulated board and cartridges for t16_session.py (--dry-run and `selftest`).

Not a hardware model: it implements exactly what the session script touches (REST readmem/writemem with and
without lens, pause/reset/config, JTAG slot status, SLOT_CTRL, CPU/raster samples) in front of behavioural
cartridges: plain 16K ROM, EasyFlash 1 (2 x AM29F040), MegaByter (MX29F800 bottom boot), GMod2 (AM29F040 +
M93C86). The flash state machine is the textbook AMD one (unlock, autoselect, program with DQ7 polling,
sector/chip erase); ROML/ROMH writes reach it only with ROM_WR_EN = 1 and the cart's own write mode.

Fault injection (--sim-fault, comma separated): flaky (second dump pass differs), zeros (lens returns 0),
nobankswitch (IO1 writes do nothing), inhibit_broken (ROM_WR_EN ignored), noprogram (program never completes),
fullcart (no blank sector), latched (SLOT_STATUS fault set), hang (CPU stops after 30 pause cycles), stalebus (the state
bus never snaps again after the first read), oldapp (trex_crt without validate-on-open: Alex says "no popup"), erasebad
(the tool erases the cart for the empty CRT), norefuse / alexno (Alex answers No), notours (not our core after the power cycle),
jtaglost (the JTAG handle cannot be reopened), rest_exc (one REST error in the middle of the first dump), kbint (Ctrl-C in a
stage), kbint2 (and a second one within 3 s), sigterm (SIGTERM in a stage).
"""
from __future__ import annotations

import hashlib
import random
import struct
import sys
import tempfile
import time as _time
from pathlib import Path


class FakeTime:
    """sleep() advances a virtual clock: the soak and the pause matrix take no real time."""

    def __init__(self):
        self.now = 1_000_000.0

    arm_kb2 = False

    def sleep(self, s):
        self.now += s
        if self.arm_kb2 and s == 3:                 # run_stages' "second Ctrl-C within 3 s" window
            self.arm_kb2 = False
            raise KeyboardInterrupt

    def monotonic(self):
        return self.now

    def time(self):
        return self.now

    def __getattr__(self, name):
        return getattr(_time, name)


class AmdFlash:
    def __init__(self, size, u1, u2, mask, ids, sector, dead=False):
        self.mem = bytearray(b"\xff" * size)
        self.u1, self.u2, self.mask, self.ids, self.sector, self.dead = u1, u2, mask, ids, sector, dead
        self.state, self.busy, self.busy_val = "read", 0, None
        self.writes_seen = 0

    def read(self, a):
        if self.busy > 0:
            self.busy -= 1
            return 0x00 if self.busy_val is None else (~self.busy_val) & 0x80
        if self.state == "autosel":
            return self.ids.get(a & 0xFF, 0x00)
        return self.mem[a]

    def write(self, a, v):
        self.writes_seen += 1
        m, st = a & self.mask, self.state
        if st == "read":
            if m == self.u1 and v == 0xAA:
                self.state = "s1"
        elif st == "autosel":
            if v == 0xF0:
                self.state = "read"
            elif m == self.u1 and v == 0xAA:
                self.state = "s1"
        elif st == "s1":
            self.state = "s2" if (m == self.u2 and v == 0x55) else "read"
        elif st == "s2":
            self.state = {0xA0: "prog", 0x80: "e0", 0x90: "autosel"}.get(v, "read") if m == self.u1 else "read"
        elif st == "prog":
            if self.dead:
                self.busy, self.busy_val = 10 ** 9, v
            else:
                self.mem[a] &= v
                self.busy, self.busy_val = 2, self.mem[a]
            self.state = "read"
        elif st == "e0":
            self.state = "e1" if (m == self.u1 and v == 0xAA) else "read"
        elif st == "e1":
            self.state = "e2" if (m == self.u2 and v == 0x55) else "read"
        elif st == "e2":
            if v == 0x30:
                s, e = self.sector(a)
                self.mem[s:e] = b"\xff" * (e - s)
                self.busy, self.busy_val = 2, None
            elif v == 0x10 and m == self.u1:
                self.mem[:] = b"\xff" * len(self.mem)
                self.busy, self.busy_val = 2, None
            self.state = "read"


def uniform64(a):
    return a & ~0xFFFF, (a | 0xFFFF) + 1


def bottom_boot(a):
    for s, e in ((0, 0x4000), (0x4000, 0x6000), (0x6000, 0x8000), (0x8000, 0x10000)):
        if s <= a < e:
            return s, e
    return a & ~0xFFFF, (a | 0xFFFF) + 1


def fill(flash, banks, rnd, blank_from, full):
    """Random data per 8 KiB bank, the banks from blank_from on stay erased (unless `full`)."""
    for b in range(banks):
        if b >= blank_from and not full:
            continue
        flash.mem[b * 0x2000:(b + 1) * 0x2000] = rnd.randbytes(0x2000)
        flash.mem[b * 0x2000] = b            # every bank differs for sure


class Rom16:
    kind = ""

    def __init__(self, rnd, f):
        self.d = bytearray(rnd.randbytes(0x4000))
        self.d[0x4:0x9] = b"CBM80"

    def pins(self): return (0, 0)
    def reset(self): pass
    def boot_game(self): pass
    def io_write(self, a, v): pass
    def io_read(self, a): return 0
    def lens(self, a):
        if 0x8000 <= a < 0xA000: return self.d[a - 0x8000]
        if 0xA000 <= a < 0xC000: return self.d[a - 0xA000 + 0x2000]
        return 0xFF
    def dma_read(self, a): return self.lens(a)
    def dma_write(self, a, v): pass


class EF:
    kind = "ef"

    def __init__(self, rnd, f):
        mk = lambda: AmdFlash(512 * 1024, 0x555, 0x2AA, 0x7FF, {0: 0x01, 1: 0xA4}, uniform64, dead="noprogram" in f)
        self.f = [mk(), mk()]
        for c in self.f:
            fill(c, 64, rnd, 40, "fullcart" in f)
        self.bank, self.mode, self.ram = 0, 0, bytearray(256)
        self.nobank = "nobankswitch" in f

    def pins(self):
        if not self.mode & 4:
            return (0, 1)
        return (0 if self.mode & 1 else 1, 0 if self.mode & 2 else 1)

    def reset(self): self.bank, self.mode = 0, 0
    def boot_game(self): self.mode = 0x07
    def _off(self, a): return self.bank * 0x2000 + (a & 0x1FFF)

    def io_write(self, a, v):
        if a == 0xDE00 and not self.nobank: self.bank = v & 0x3F
        elif a == 0xDE02: self.mode = v & 0x87
        elif 0xDF00 <= a < 0xE000: self.ram[a & 0xFF] = v

    def io_read(self, a): return self.ram[a & 0xFF] if a >= 0xDF00 else 0

    def lens(self, a):
        if 0x8000 <= a < 0xA000: return self.f[0].read(self._off(a))
        if 0xA000 <= a < 0xC000 or a >= 0xE000: return self.f[1].read(self._off(a))
        return 0xFF

    def dma_read(self, a):
        g, e = self.pins()
        if (g, e) == (0, 1):
            if 0x8000 <= a < 0xA000: return self.f[0].read(self._off(a))
            if a >= 0xE000: return self.f[1].read(self._off(a))
        elif (g, e) == (0, 0):
            if 0x8000 <= a < 0xA000: return self.f[0].read(self._off(a))
            if 0xA000 <= a < 0xC000: return self.f[1].read(self._off(a))
        return 0

    def dma_write(self, a, v):
        if self.pins() != (0, 1): return
        if 0x8000 <= a < 0xA000: self.f[0].write(self._off(a), v)
        elif a >= 0xE000: self.f[1].write(self._off(a), v)

    def load_image(self, banks):
        for b, d in banks.items():
            self.f[0].mem[b * 0x2000:(b + 1) * 0x2000] = d[:0x2000]
            self.f[1].mem[b * 0x2000:(b + 1) * 0x2000] = d[0x2000:0x4000]

    def snapshot(self):
        return hashlib.sha256(bytes(self.f[0].mem) + bytes(self.f[1].mem)).hexdigest()

    def save_in_game(self):
        f = self.f[0]
        f.write(0x555, 0xAA); f.write(0x2AA, 0x55); f.write(0x555, 0xA0)
        f.write(40 * 0x2000 + 0x10, 0x5A)
        f.busy = 0


class MB:
    kind = "mb"

    def __init__(self, rnd, f):
        self.f = AmdFlash(1024 * 1024, 0xAAA, 0x555, 0xFFF, {0: 0xC2, 2: 0x58}, bottom_boot, dead="noprogram" in f)
        fill(self.f, 128, rnd, 100, "fullcart" in f)
        self.bank, self.mode = 0, 0
        self.nobank = "nobankswitch" in f

    def pins(self): return (0, 1) if self.mode & 3 == 3 else (1, 0)
    def reset(self): self.bank, self.mode = 0, 0
    def boot_game(self): pass
    def _off(self, a): return self.bank * 0x2000 + (a & 0x1FFF)

    def io_write(self, a, v):
        if a == 0xDE00 and not self.nobank: self.bank = v & 0x7F
        elif a == 0xDE02: self.mode = v & 3

    def io_read(self, a): return 0
    def lens(self, a): return self.f.read(self._off(a)) if 0x8000 <= a < 0xA000 else 0xFF
    def dma_read(self, a): return self.lens(a)

    def dma_write(self, a, v):
        if self.mode & 3 == 3 and 0x8000 <= a < 0xA000: self.f.write(self._off(a), v)

    def load_image(self, banks):
        for b, d in banks.items():
            self.f.mem[b * 0x2000:(b + 1) * 0x2000] = d[:0x2000]

    def snapshot(self): return hashlib.sha256(bytes(self.f.mem)).hexdigest()


class Eeprom:
    def __init__(self, rnd):
        self.words = [rnd.getrandbits(16) for _ in range(1024)]
        self.cs = self.clk = 0
        self.bits, self.out_mode, self.i, self.word = [], False, 0, 0
        self.wen, self.busy, self.pending, self.do = False, 0, None, 0

    def reg(self, v):
        cs, clk, di = v >> 6 & 1, v >> 5 & 1, v >> 4 & 1
        if cs and not self.cs:
            self.bits, self.out_mode, self.do = [], False, 0
        if not cs and self.cs:
            if self.pending and self.wen:
                self.words[self.pending[0]] = self.pending[1]
                self.busy = 1
            self.pending, self.bits, self.out_mode = None, [], False
        if cs and clk and not self.clk:
            self.rising(di)
        self.cs, self.clk = cs, clk

    def rising(self, di):
        if self.out_mode:
            self.do = self.word >> (15 - self.i) & 1
            self.i += 1
            return
        self.bits.append(di)
        b = self.bits
        if not b[0]:
            self.bits = []
            return
        if len(b) == 13:
            op, addr = b[1] * 2 + b[2], int("".join(map(str, b[3:])), 2)
            if op == 2:
                self.out_mode, self.word, self.i, self.do = True, self.words[addr], 0, 0
            elif op == 0:
                sub = addr >> 8
                if sub == 3: self.wen = True
                elif sub == 0: self.wen = False
        elif len(b) == 29 and b[1] * 2 + b[2] == 1:
            self.pending = (int("".join(map(str, b[3:13])), 2), int("".join(map(str, b[13:29])), 2))

    def read_do(self):
        if self.cs and self.busy:
            self.busy -= 1
            return 0
        return 1 if (self.cs and not self.out_mode and not self.bits) else self.do


class GM:
    kind = "gm"

    def __init__(self, rnd, f):
        self.f = AmdFlash(512 * 1024, 0x555, 0x2AA, 0x7FF, {0: 0x01, 1: 0xA4}, uniform64, dead="noprogram" in f)
        fill(self.f, 64, rnd, 48, "fullcart" in f)
        self.reg, self.e = 0, Eeprom(rnd)
        self.nobank = "nobankswitch" in f

    def pins(self): return (0, 1) if self.reg & 0xC0 == 0xC0 else (1, 0)
    def reset(self): self.reg = 0
    def boot_game(self): pass
    def _off(self, a): return (self.reg & 0x3F) * 0x2000 + (a & 0x1FFF)

    def io_write(self, a, v):
        if a == 0xDE00:
            if not self.nobank: self.reg = v
            self.e.reg(v)

    def io_read(self, a): return (self.e.read_do() << 7) if a == 0xDE00 else 0
    def lens(self, a): return self.f.read(self._off(a)) if 0x8000 <= a < 0xA000 else 0xFF
    def dma_read(self, a): return self.lens(a)

    def dma_write(self, a, v):
        if self.reg & 0xC0 == 0xC0 and a >= 0xE000: self.f.write(self._off(a), v)

    def load_image(self, banks):
        for b, d in banks.items():
            self.f.mem[b * 0x2000:(b + 1) * 0x2000] = d[:0x2000]

    def snapshot(self):
        return hashlib.sha256(bytes(self.f.mem) + b"".join(w.to_bytes(2, "big") for w in self.e.words)).hexdigest()


MODELS = {"rom16": Rom16, "ef": EF, "mb": MB, "gm": GM}


class SimFtp:
    def __init__(self):
        self.files = {}
        self.dir = "/USB1/t16"

    def path(self, name): return f"{self.dir}/{name}"
    def upload(self, name, data, stale=()): self.files[self.path(name)] = bytes(data)
    def download(self, name): return self.files.get(self.path(name))


class SimRest:
    def __init__(self, b):
        self.b, self.calls, self.host = b, 0, "sim"
        self.cfg = {"Cartridge Preference": "Auto", "Bus Sharing - ROMs": "Both", "Bus Sharing - I/O1": "Both",
                    "Bus Sharing - I/O2": "Both"}

    def version(self): self.calls += 1; return '{"version":"sim"}'

    def readmem(self, addr, n, lens=None):
        self.calls += 1
        b, m = self.b, self.b.model
        if "rest_exc" in b.faults and self.calls > 400 and not b.fired.get("rest_exc"):
            b.fired["rest_exc"] = True
            from t16_session import RestError
            raise RestError("GET /v1/machine:readmem: simulated connection reset")
        if lens:
            if "zeros" in b.faults:
                return bytes(n)
            data = bytearray(m.lens(addr + i) for i in range(n))
            if "flaky" in b.faults and b.pauses >= 2 and addr == 0x8000:
                b.flaky_n += 1
                if b.flaky_n == 5:
                    data[0] ^= 0x01
            return bytes(data)
        out = bytearray()
        for i in range(n):
            a = addr + i
            if 0xDE00 <= a < 0xE000:
                out.append(m.io_read(a) if b.io_visible() else 0xFF)
            elif b.rom_visible(a):
                out.append(m.dma_read(a))
            else:
                out.append(0x55)                                   # RAM under the cartridge
        return bytes(out)

    def poke(self, addr, val):
        self.calls += 1
        b, m = self.b, self.b.model
        if addr == 0x00 or addr == 0x01:
            if b.held() and "noq8" not in b.faults:
                b.tmp_port = list(b.tmp_port or b.prog_port)
                b.tmp_port[addr] = val
            return
        if 0xDE00 <= addr < 0xE000:
            if b.io_visible() and "nodmaw" not in b.faults:
                m.io_write(addr, val)
        elif b.wr_en or "inhibit_broken" in b.faults:
            m.dma_write(addr, val)

    def pause(self): self.calls += 1; self.b.pauses += 1; self.b.rest_paused = True
    def resume(self): self.calls += 1; self.b.rest_paused = False; self.b.tmp_port = None
    def reset(self): self.calls += 1; self.b.reset()
    def menu(self): self.calls += 1; self.b.frozen = not self.b.frozen
    def run_crt(self, path): self.calls += 1
    def apps(self): return ["trex_crt"]
    def cfg_get(self, item, cat=None):
        if item not in self.cfg:
            from t16_session import RestError
            raise RestError(f"no config {item!r}")      # like the firmware without the removed item
        return self.cfg[item]

    def cfg_set(self, item, v, cat=None):
        self.cfg[item] = v
        if item == "Cartridge Preference" or item.startswith("Bus Sharing"):
            pass                                                   # takes effect at the next reset


class SimJtag:
    """The CPU words of the state bus come from a shadow: loaded at every read while the machine runs (a capture snap) or is
    halted by the machine pause, never under REST's CPU pause alone (like c64_core.v sb_capture) or with the stalebus fault."""

    def __init__(self, b):
        self.b, self.tick, self.gen, self.shadow, self.reads = b, 0, 0, None, 0

    def close(self): pass

    def reopen(self):
        if "jtaglost" in self.b.faults:
            raise RuntimeError("simulated: no FTDI device")
        return self

    def status(self):
        b = self.b
        g, e = b.model.pins()
        v = (0 if b.not_ours else 1 << 31) | g | e << 1 | 1 << 2 | 1 << 3 | 1 << 4
        if b.armed:
            v |= 0x7 << 8 | 1 << 16
        if b.wr_en:
            v |= 1 << 18
        if "latched" in b.faults:
            v |= 1 << 17 | 3 << 20
        return v

    def ctrl(self): return 2 if self.b.wr_en else 0
    def set_ctrl(self, v): self.b.wr_en = bool(v & 2)
    def gate(self): return self.b.gate
    def set_gate(self, v): self.b.gate = v
    def machine_pause(self, on):
        self.b.mpause = on
        if on:
            self.b.pause_cycles += 1
        else:
            self.b.tmp_port = None
    def paused(self): return self.b.mpause

    def glue(self):
        return dict(halted=self.b.mpause, gen=self.gen)

    def cpu(self):
        b = self.b
        self.reads += 1
        if self.reads > 60 and not b.fired.get("kb"):
            for f in ("kbint", "kbint2", "sigterm"):
                if f in b.faults:
                    b.fired["kb"] = True
                    if f == "sigterm":
                        import t16_session as S
                        S.TERMINATE["now"] = True
                    if f == "kbint2":
                        import t16_session as S
                        S.time.arm_kb2 = True
                    raise KeyboardInterrupt
        hang = "hang" in b.faults and b.pause_cycles > 30
        if not (b.mpause or b.rest_paused or hang):
            self.tick += 1
        halted = b.mpause
        stale = b.rest_paused or ("stalebus" in b.faults and self.shadow is not None)
        if self.shadow is None or halted or not stale:
            t = self.tick
            ddr, port = b.eff_port()
            self.shadow = dict(pc=0x0800 + (t * 37) % 0x400, a=t & 255, x=0, y=0, s=0xFD, p=0x24, port01=port, ddr=ddr,
                               raster=(t * 13) % 312)
            if not halted:
                self.gen = (self.gen + 1) & 0xFFFF
        return dict(self.shadow, halted=halted, gen=self.gen)

    def screen(self): return "SIMULATED SCREEN\n" + self.b.model.__class__.__name__


class SimBoard:
    last = None

    def __init__(self, cart, seed=1, faults=""):
        self.faults = set(f for f in (faults or "").split(",") if f)
        self.rnd = random.Random(seed)
        self.model = MODELS[cart.flash or "rom16"](self.rnd, self.faults)
        self.cart = cart
        self.fired = {}
        self.not_ours = False
        self.wr_en = self.mpause = self.rest_paused = self.frozen = False
        self.gate = self.pauses = self.pause_cycles = self.flaky_n = 0
        self.armed = False
        # the program's CPU port: Lykia and Sam's Journey hide ROML ($35), the others leave $37
        self.prog_port = [0x2F, 0x35 if cart.key in ("megabyter", "gmod2") else 0x37]
        self.tmp_port = None
        self.rest, self.jtag, self.ftp = SimRest(self), SimJtag(self), SimFtp()
        self.reset()
        self.wr_start = self.wr_en
        SimBoard.last = self
        self.initial = self.model.snapshot() if hasattr(self.model, "snapshot") else None

    def reset(self):
        self.model.reset()
        self.model.boot_game()
        c = self.rest.cfg
        pref = c["Cartridge Preference"]
        self.armed = pref == "Auto" or (pref == "Manual" and c["Bus Sharing - ROMs"] == "External")
        self.tmp_port = None
        self.apply_policy()

    def apply_policy(self):
        """T57 Decision 2026-10-05 in the firmware: ROM_WR_EN is always 1 (trx_slot_apply_policy), armed or not."""
        self.wr_en = True

    # the CPU port and what the PLA shows of the cartridge
    def held(self): return self.mpause or self.rest_paused

    def eff_port(self):
        p = self.tmp_port or self.prog_port
        return p[0], p[1]

    def io_visible(self):
        g, e = self.model.pins()
        p = self.eff_port()[1]
        return (g, e) == (0, 1) or bool(p & 4 and p & 3)

    def rom_visible(self, a):
        g, e = self.model.pins()
        p = self.eff_port()[1]
        if (g, e) == (0, 1):
            return 0x8000 <= a < 0xA000 or a >= 0xE000
        return 0x8000 <= a < 0xC000 and (p & 3) == 3

    def install_clock(self, module):
        module.time = FakeTime()

    # trex_crt by Alex's keys (the hooks keys_dump / keys_write): what the tool does to the cartridge
    def image_banks(self):
        m = self.model
        if m.kind == "":
            return {0: bytes(m.d)}
        if m.kind == "ef":
            return {b: bytes(m.f[0].mem[b * 0x2000:(b + 1) * 0x2000]) + bytes(m.f[1].mem[b * 0x2000:(b + 1) * 0x2000]) for b in range(64)}
        mem = m.f.mem
        return {b: bytes(mem[b * 0x2000:(b + 1) * 0x2000]) for b in range(self.cart.banks)}

    def eeprom_bytes(self):
        return b"".join(w.to_bytes(2, "big") for w in self.model.e.words) if self.model.kind == "gm" else None

    def keys_dump(self, path):
        """Alex's keys in the dumper: F1, F3, a file name; the tool touches the cart, the C64 resets after it."""
        import t16_session as S
        if not self.armed or self.faults & {"latched"}:
            return
        self.ftp.files[path] = S.build_crt(self.cart, self.image_banks(), "T16 app", self.model.kind in ("ef", ""), self.eeprom_bytes())
        self.reset()

    def keys_write(self, path):
        """Alex's keys in the writer: F3 (the app raises the gate: wr0 comes back), F5; the C64 resets after it."""
        import t16_session as S
        m = self.model
        data = self.ftp.files.get(path)
        if data is None or not self.armed or not m.kind:
            return
        try:
            _, banks = S.crt_banks(data)
        except Exception:
            banks = {}
        if banks and "noprogram" not in self.faults:
            if hasattr(m, "f") and isinstance(m.f, list):
                for ch in m.f:
                    ch.mem[:] = b"\xff" * len(ch.mem)
            else:
                m.f.mem[:] = b"\xff" * len(m.f.mem)
            m.load_image(banks)
            ee = self._crt_eeprom(data)
            if ee and m.kind == "gm":
                m.e.words[:] = [int.from_bytes(ee[i:i + 2], "big") for i in range(0, 2048, 2)]
        self.reset()

    @staticmethod
    def _crt_eeprom(data):
        pos = struct.unpack(">I", data[0x10:0x14])[0]
        while pos + 16 <= len(data) and data[pos:pos + 4] == b"CHIP":
            ln, ct, bank, load, size = struct.unpack(">IHHHH", data[pos + 4:pos + 16])
            if ct == 1:
                return data[pos + 16:pos + 16 + size]
            pos += ln
        return None

    def alex_ask(self, tag, msg, default):
        """What the simulated Alex answers to a yes/no question."""
        f = self.faults
        if tag == "confirm_write":
            return "confirmyes" in f
        if tag == "popup":
            return "oldapp" not in f
        if tag == "refused" and "norefuse" in f:
            return False
        if tag in ("verified", "dumper", "popup_pref", "refused", "picture") and "alexno" in f:
            return False
        return default

    def alex(self, tag, arg, msg):
        """What Alex's key presses do while the script waits."""
        m = self.model
        if tag == "power":
            self.reset()
            if "notours" in self.faults:
                self.not_ours = True
        elif tag == "dump":
            self.keys_dump(self.ftp.path(arg + ".crt"))
        elif tag == "write":
            self.keys_write(arg)
        elif tag == "verify":
            self.reset()
        elif tag == "badcrt":
            if "erasebad" in self.faults and m.kind:
                if hasattr(m, "f") and isinstance(m.f, list):
                    for c in m.f: c.mem[:] = b"\xff" * len(c.mem)
                else:
                    m.f.mem[:] = b"\xff" * len(m.f.mem)
            self.reset()
        elif tag == "writefile":
            data = self.ftp.files[arg]
            import t16_session as S
            _, banks = S.crt_banks(data)
            if not self.wr_en and "inhibit_broken" not in self.faults:
                return                                 # nothing reaches the chip: writer verify fails
            if hasattr(m, "f") and isinstance(m.f, list):
                for c in m.f: c.mem[:] = b"\xff" * len(c.mem)
            else:
                m.f.mem[:] = b"\xff" * len(m.f.mem)
            m.load_image(banks)
        elif tag == "ingame":
            if self.wr_en and m.kind == "ef":
                m.save_in_game()
            elif m.kind == "gm":
                m.e.words[3] ^= 0x00FF


def _run(argv):
    import t16_session as S
    return S.main(argv)


def selftest() -> int:
    import contextlib
    import io
    import t16_session as S
    tmp = Path(tempfile.mkdtemp(prefix="t16-selftest-"))
    S.OUT = tmp
    u2a = tmp / "trex_crt.u2a"
    u2a.write_bytes(b"U2A-sim")
    real_time = S.time
    fails = []

    counter = [0]

    def case(name, argv, want_rc, check=None, quiet=True, out=None):
        counter[0] += 1
        S.OUT = tmp / (out or f"case{counter[0]}")          # every case has its own output dir (and so its own first backup)
        S.time = real_time
        buf = io.StringIO()
        err = io.StringIO()
        with contextlib.redirect_stdout(buf), contextlib.redirect_stderr(err):
            try:
                rc = S.main(["--dry-run"] + argv)
            except SystemExit as e:
                rc = e.code
        out = buf.getvalue() + err.getvalue()
        sim = SimBoard.last
        ok = (rc == want_rc) and (check(out, sim) if check else True) and sim.wr_en == sim.wr_start
        print(f"{'ok  ' if ok else 'FAIL'} {name} (rc {rc}, want {want_rc})")
        if not ok:
            fails.append(name)
            print(out[-2500:], err.getvalue()[-500:])
        return ok

    base = ["run", "--yes", "--quick", "--app-u2a", str(u2a)]
    for cart in ("rom16", "ef1", "megabyter", "gmod2"):
        case(f"{cart} read-only stages", base + ["--cart", cart], 0,
             lambda o, s: "PASS" in o and "FAIL" not in o and (s.initial is None or s.model.snapshot() == s.initial))
    for cart, stages in (("ef1", "dump,write,writer,ingame"), ("megabyter", "dump,write,writer"), ("gmod2", "dump,write,eeprom,app_up,app_dump,app_write,ingame")):
        case(f"{cart} write stages", base + ["--cart", cart, "--write", "--yes-write", "--stages", stages], 0,
             lambda o, s: "ABORT" not in o and "FAIL" not in o)
    # the writer stage and the in-game save leave the cart as the backup / as the save: compare by stage
    case("ef1 in-game save changed flash only after the writer restored it",
         base + ["--cart", "ef1", "--write", "--yes-write", "--stages", "dump,ingame"], 0,
         lambda o, s: "in-game save changed the flash" in o and "PASS" in o)
    # abort paths: every one ends rc 1, nothing written (snapshot unchanged), ROM_WR_EN back at its start value (checked in case())
    for fault, cart, stages, why in (("flaky", "ef1", "dump", "second pass differs"),
                                     ("zeros", "ef1", "dump", "constant dump"),
                                     ("nobankswitch", "ef1", "dump", "bank switching ineffective"),
                                     ("latched", "ef1", "pre", "fault latched"),
                                     ("inhibit_broken", "ef1", "dump,write", "write inhibit not effective"),
                                     ("noprogram", "ef1", "dump,write", "program never completes"),
                                     ("noprogram", "megabyter", "dump,write", "program never completes"),
                                     ("noprogram", "gmod2", "dump,write", "program never completes"),
                                     ("fullcart", "megabyter", "dump,write", "no blank sector")):
        extra = ["--write", "--yes-write"] if "write" in stages else []
        case(f"abort {cart}/{fault}: {why}", base + ["--cart", cart, "--stages", stages, "--sim-fault", fault] + extra, 1,
             lambda o, s: "ABORT" in o and "nothing further was written" in o)
    case("write stage without --write", base + ["--cart", "ef1", "--stages", "dump,write"], 1, lambda o, s: "need --write" in o)
    case("write stage without a dump in the run", base + ["--cart", "ef1", "--stages", "write", "--write", "--yes-write"], 1,
         lambda o, s: "no verified backup" in o)
    # flash content must be unharmed by the aborted write tests
    for cart, fault in (("ef1", "noprogram"), ("megabyter", "noprogram"), ("gmod2", "noprogram")):
        def intact(o, s):
            return s.model.snapshot() == s.initial
        case(f"{cart} flash intact after aborted {fault} write", base + ["--cart", cart, "--stages", "dump,write",
             "--write", "--yes-write", "--sim-fault", fault], 1, intact)
    case("hung CPU after pause cycles is a FAIL, not silence", base + ["--cart", "ef1", "--stages", "pause100", "--sim-fault", "hang"], 1,
         lambda o, s: "FAIL" in o)
    # the final session's plan: every cart, the full stage list, writes included (the in-game save, the power cycle)
    for cart in ("rom16", "ef1", "megabyter", "gmod2"):
        case(f"{cart} full plan", base + ["--cart", cart, "--stages", "full", "--write", "--yes-write"], 0,
             lambda o, s: "ABORT" not in o and "FAIL" not in o and "PASS" in o)
    case("full plan without --write stays read-only", base + ["--cart", "ef1", "--stages", "full"], 0,
         lambda o, s: "ABORT" not in o and "FAIL" not in o and "app_write" not in o and s.model.snapshot() == s.initial)
    # the new checks fail where they have to
    case("Q8 not built: the port stage fails", base + ["--cart", "megabyter", "--stages", "port", "--sim-fault", "noq8"], 1,
         lambda o, s: "FAIL" in o)
    case("DMA write lost (no hold tick): the dump fails", base + ["--cart", "megabyter", "--stages", "dump", "--sim-fault", "nodmaw"], 1,
         lambda o, s: "ABORT" in o)
    case("gate stays 1 with the port not armed", base + ["--cart", "ef1", "--stages", "pre,gate"], 0,
         lambda o, s: "ROM_WR_EN still 1" in o and "FAIL" not in o)
    dumpdir = lambda cart, o: tmp / o / "dryrun" / cart / "dump"          # noqa: E731
    # --- T16 hardening 2026-10-10 -----------------------------------------------------------------------------------------
    # 1. the first backup is written once and never replaced; a changed cart goes to a file of its own; restores use the first
    for cart in ("ef1", "megabyter", "rom16"):
        o = f"first-{cart}"
        case(f"{cart} first backup written once", base + ["--cart", cart, "--stages", "dump"], 0,
             lambda o_, s_: "FIRST backup written" in o_, out=o)
        f = dumpdir(cart, o) / f"t16_backup_{cart}.crt"
        h0 = f.read_bytes()
        case(f"{cart} second dump of the same cart keeps the file", base + ["--cart", cart, "--stages", "dump"], 0,
             lambda o_, s_: "== the FIRST backup" in o_ and "FIRST backup written" not in o_, out=o)
        ok = f.read_bytes() == h0 and not (f.stat().st_mode & 0o222)
        print(f"{'ok  ' if ok else 'FAIL'} {cart} first backup unchanged and read-only")
        if not ok:
            fails.append(f"{cart} first backup file")
    case("ef1 changed cart: first backup kept, WARN, the restore writes the FIRST backup",
         base + ["--cart", "ef1", "--stages", "dump,app_up,app_dump,write,app_write,app_err", "--write", "--yes-write", "--sim-seed", "7"], 0,
         lambda o_, s_: "differs from the FIRST backup" in o_ and "FAIL" not in o_ and "restore image" in o_ and "(first backup)" in o_,
         out="first-ef1")
    fb = S.crt_banks((dumpdir("ef1", "first-ef1") / "t16_backup_ef1.crt").read_bytes())[1]
    last = SimBoard.last
    ok = last.image_banks() == fb
    print(f"{'ok  ' if ok else 'FAIL'} ef1 after the restore the cart holds the FIRST backup (not the seed-7 content)")
    if not ok:
        fails.append("ef1 restore to first backup")
    (dumpdir("ef1", "first-ef1") / "t16_backup_ef1.crt").chmod(0o644)
    (dumpdir("ef1", "first-ef1") / "t16_backup_ef1.crt").write_bytes(b"garbage")
    case("ef1 unusable first backup file: FAIL line, file left alone",
         base + ["--cart", "ef1", "--stages", "dump"], 1, lambda o_, s_: "not usable" in o_, out="first-ef1")
    ok = (dumpdir("ef1", "first-ef1") / "t16_backup_ef1.crt").read_bytes() == b"garbage"
    print(f"{'ok  ' if ok else 'FAIL'} ef1 unusable first backup not overwritten")
    if not ok:
        fails.append("ef1 garbage backup overwritten")
    # 2. app_err: new app (popup, nothing erased) and old app both pass; --app-validates-open fails the old one; a tool that erases fails hard
    for cart in ("ef1", "megabyter", "gmod2"):
        case(f"{cart} app_err: popup (validate-on-open), sampled check", base + ["--cart", cart, "--write", "--yes-write", "--stages",
             "dump,app_up,app_dump,app_err"], 0, lambda o_, s_: "validates the CRT when it is opened" in o_ and "sector edges" in o_ and "FAIL" not in o_)
        case(f"{cart} app_err: old app (writer screen), full dump", base + ["--cart", cart, "--write", "--yes-write", "--stages",
             "dump,app_up,app_dump,app_err", "--sim-fault", "oldapp"], 0,
             lambda o_, s_: "older trex_crt, accepted" in o_ and "sector edges" not in o_ and "FAIL" not in o_)
    case("ef1 app_err: old app with --app-validates-open fails", base + ["--cart", "ef1", "--write", "--yes-write", "--app-validates-open",
         "--stages", "dump,app_err", "--sim-fault", "oldapp"], 1, lambda o_, s_: "the popup is required" in o_)
    case("ef1 app_err: the tool erases the cart: hard FAIL with the restore hint", base + ["--cart", "ef1", "--write", "--yes-write",
         "--stages", "dump,app_err", "--sim-fault", "erasebad,oldapp"], 1, lambda o_, s_: "Wiederherstellen" in o_ and "FAIL" in o_)
    case("ef1 app_err: tool erases but Alex saw a popup: the sampled check still catches it", base + ["--cart", "ef1", "--write",
         "--yes-write", "--stages", "dump,app_err", "--sim-fault", "erasebad"], 1, lambda o_, s_: "FAIL" in o_)
    case("ef1 app_err: Alex says not refused", base + ["--cart", "ef1", "--write", "--yes-write", "--stages", "dump,app_err",
         "--sim-fault", "norefuse"], 1, lambda o_, s_: "rejected with a message" in o_ and "FAIL" in o_)
    case("write confirmation is asked once per run and a No stops the writes", base + ["--cart", "ef1", "--write", "--stages",
         "dump,write,app_write"], 1, lambda o_, s_: o_.count("GESCHRIEBEN") == 1 and o_.count("write not confirmed") >= 1)
    case("write confirmation answered Yes: asked once for write, app_write, app_err, ingame", base + ["--cart", "ef1", "--write",
         "--stages", "dump,app_up,app_dump,write,app_write,app_err,ingame", "--sim-fault", "confirmyes"], 0,
         lambda o_, s_: o_.count("GESCHRIEBEN") == 1 and "FAIL" not in o_)
    # 3. state bus validity under a REST CPU pause
    case("stale state bus: the port stage fails instead of confirming the old value", base + ["--cart", "megabyter", "--stages", "port",
         "--sim-fault", "stalebus"], 1, lambda o_, s_: "state bus stale" in o_)
    case("state bus fresh: port stage on both holds", base + ["--cart", "megabyter", "--stages", "port"], 0,
         lambda o_, s_: "program's port is back after the resume" in o_ and "FAIL" not in o_)
    # 4. power cycle
    for cart in ("ef1", "gmod2"):
        case(f"{cart} power cycle with --sram", base + ["--cart", cart, "--write", "--yes-write", "--sram", "--stages",
             "dump,app_up,app_dump,ingame,powercycle,persist"], 0, lambda o_, s_: "our core runs after the power cycle" in o_ and "FAIL" not in o_)
    case("ef1 power cycle: not our core without --sram: clear message, later stages still run", base + ["--cart", "ef1", "--write",
         "--yes-write", "--stages", "dump,powercycle,soak", "--sim-fault", "notours"], 1,
         lambda o_, s_: "mit --sram wiederholen" in o_ and "running with the cart" in o_)
    case("ef1 power cycle: JTAG cannot be reopened: later stages skipped as FAIL, no traceback", base + ["--cart", "ef1", "--write",
         "--yes-write", "--stages", "dump,powercycle,persist,soak", "--sim-fault", "jtaglost"], 1,
         lambda o_, s_: "JTAG handle lost" in o_ and "Traceback" not in o_)
    # 5. nothing leaves a traceback: REST error mid-dump, Ctrl-C, second Ctrl-C, SIGTERM
    case("REST error in the middle of the dump: FAIL line, the next stages run", base + ["--cart", "ef1", "--stages", "dump,soak,matrix",
         "--sim-fault", "rest_exc"], 1, lambda o_, s_: "stage failed" in o_ and "running with the cart" in o_ and "Traceback" not in o_)
    case("Ctrl-C in a stage: that stage fails, the run goes on, report written", base + ["--cart", "ef1", "--stages", "pause100,soak",
         "--sim-fault", "kbint"], 1, lambda o_, s_: "interrupted by Ctrl-C" in o_ and "running with the cart" in o_ and "report" in o_)
    case("second Ctrl-C ends the run, report written", base + ["--cart", "ef1", "--stages", "pause100,soak", "--sim-fault", "kbint2"], 130,
         lambda o_, s_: "Lauf beendet" in o_ and "report" in o_ and "running with the cart" not in o_)
    case("SIGTERM ends the run at once, report written", base + ["--cart", "ef1", "--stages", "pause100,soak", "--sim-fault", "sigterm"], 130,
         lambda o_, s_: "Lauf beendet" in o_ and "report" in o_ and "running with the cart" not in o_)
    case("Alex answers No everywhere: FAIL lines, no crash", base + ["--cart", "gmod2", "--write", "--yes-write", "--stages",
         "boot,dump,app_up,app_dump,app_write,app_err", "--sim-fault", "alexno"], 1, lambda o_, s_: "FAIL" in o_ and "Traceback" not in o_)
    print("\nselftest:", "all passed" if not fails else f"{len(fails)} failed: {fails}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    sys.exit(selftest())
