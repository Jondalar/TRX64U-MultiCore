#!/usr/bin/env python3
"""T44 board test of the flash write gate (docs/specs/done/T44-flash-write-gate.md §8).

    tools/flashgate_test.py stage0 [--out FILE]
        Read only; today's bitstream or the gate's: SR1/SR2/SR3, JEDEC ID,
        unique ID, the gate's registers if there is a gate, and the scratch
        sector 0xFE8000 (4 KB) into FILE.
    tools/flashgate_test.py stage1 [--stage0 FILE] [--control]
        Needs the gate built with g_wren_pass = false (checked first). Every
        forbidden command is sent only right after WRDI and an SR1 read that
        showed WEL = 0, in the same list, on the RISC-V without a break.
        --control adds step 5 (an erase at the scratch sector while WEL = 0, the
        chip ignores it): only with the stage-0 dump of that sector, all 0xFF.
    tools/flashgate_test.py stage0|stage1 --dry-run
        Print the access lists and stop; no JTAG.
    tools/flashgate_test.py stage0|stage1 --sim DIR [--sim-generics ...]
        No JTAG: the same lists on Gideon's rvlite, the gate and a flash model
        under ghdl (host/sim/flash_gate/run.sh elaborates tb_cpu
        in DIR and calls this).

    tools/flashgate_test.py stage2 [--out FILE] [--wel1-probe]
        Needs the gate built with g_wren_pass = true (checked first). Real
        writes, only inside the scratch sector 0xFE8000-0xFE8FFF, and only if
        it reads all 0xFF first (saved to FILE): erase, verify, program one
        page at 0xFE8000, verify, erase again, verify. Then stage 1 step 2
        again with WEL forced off (the release build still blocks).
        --wel1-probe adds one forbidden erase at 0x100000 with WEL = 1, where
        the gate is the only barrier; the spec (§8) rules this out, so it is
        never part of the default run.

How: tools/trxdbg.py's JTAG access holds the RISC-V in reset, loads
tools/flashgate/mailbox.S (assembled here with riscv64-elf-as) to DDR2 0x30000
and starts it through the boot ROM's magic, as "trxdbg.sh riscv" does. The
helper carries out lists of byte accesses to the flash SPI window 0x10060200
that this script puts into DDR2. The app is not running meanwhile; load it
again afterwards as usual.
"""
import os
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

LOAD = 0x30000
MBOX = 0x38000
SEQ, DONE, READY, STATUS, NRES = MBOX, MBOX + 4, MBOX + 8, MBOX + 12, MBOX + 16
OPS, RES = MBOX + 0x100, MBOX + 0x2000
MAX_OPS = (RES - OPS) // 4 - 1
READY_MAGIC = 0x464C4741
BOOT_MAGIC = 0x1571BABE

DATA, CTRL, GATE = 0x00, 0x08, 0x10      # window offsets
SCRATCH = 0xFE8000
PROBE = 0x10                            # --wel1-probe: erase at 0x100000 (bitstream area)
PATTERN = bytes((i * 37 + 0x5A) & 0xFF for i in range(256))

# opcodes that only read (or clear WEL): sent at the start of a transfer freely
READ_OPS = {0x03, 0x05, 0x35, 0x15, 0x9F, 0x4B, 0xFF, 0x04}
CHECKED = {0x02, 0x32, 0x20, 0x52, 0xD8, 0x12, 0x34, 0x21, 0x53, 0xDC}


class Refused(Exception):
    pass


class Seq:
    """One list of accesses for the helper. Tracks the CS framing as the gate
    does and refuses, before anything is sent, every opcode that could write
    unless WRDI and a WEL = 0 check came first with no WREN since."""

    def __init__(self, title):
        self.title = title
        self.ops = []
        self.names = []                 # (name, bytes) for each read, in order
        self.guards = []
        self.force, self.level = 0, 1
        self.idx = 0
        self.wel_checked = False
        self.gate_checked = False
        self.held = None                # program/erase opcode, waiting for A23..A16
        self.real_ok = False            # control step: one allowed erase while WEL = 0
        self.stage2 = False             # gate_check(wren=True) seen: stage-2 rules
        self.armed = False              # stage 2: WREN sent, one program/erase may follow
        self.probe_ok = False           # stage 2 --wel1-probe: one erase at 0x10....
        self.addr = []                  # stage 2: address bytes of the held opcode

    def _cs_low(self):
        return self.force == 0 or self.level == 0

    def ctrl(self, v):
        self.ops.append((1 << 24) | (CTRL << 8) | v)
        new_low = (v & 1) == 1 and (v & 2) == 0
        if not (new_low and self.force == 1 and self.level == 0) or self.held is not None:
            self.idx = 0                # as the gate: a dropped opcode was never clocked
        self.force, self.level = v & 1, (v >> 1) & 1
        self.held = None

    def _byte(self, v, is_read):
        if not self._cs_low():
            return
        if self.force == 0:
            idx, self.idx = 0, 0
        else:
            idx = self.idx
            self.idx += 1
        if self.stage2:
            return self._byte2(idx, v, is_read)
        if idx == 0:
            if v == 0x06:
                if not self.gate_checked:
                    raise Refused(f"{self.title}: WREN without the gate check (g_wren_pass = 0)")
                self.wel_checked = False
            elif v not in READ_OPS:
                if not self.wel_checked:
                    raise Refused(f"{self.title}: opcode 0x{v:02X} without WRDI + WEL = 0 first")
                if v in CHECKED and self.force == 1:
                    self.held = v
        elif idx == 1 and self.held is not None:
            if v >= 0x40:
                if not self.real_ok:
                    raise Refused(f"{self.title}: 0x{self.held:02X} at an allowed address "
                                  f"(0x{v:02X}....) is not part of stage 1")
                self.real_ok = False
            self.held = None

    def _byte2(self, idx, v, is_read):
        """Stage 2: WREN, then at most one 0x20/0x02 whose three address bytes
        are written and lie in 0xFE8000-0xFE8FFF; the one exception is the
        --wel1-probe erase at 0x10..... Anything else that could write: refused.
        Opcodes after WRDI + WEL = 0 follow the stage-1 rule (forbidden address only)."""
        if idx == 0:
            self.held, self.addr = None, []
            if v == 0x06:
                self.armed, self.wel_checked = True, False
            elif v not in READ_OPS:
                if self.armed and v in (0x20, 0x02) and self.force == 1 and not is_read:
                    self.armed, self.held = False, v
                elif self.wel_checked and not self.armed and v in CHECKED and self.force == 1:
                    self.held = v
                    self.addr = None    # stage-1 rule: must be below 0x40
                else:
                    raise Refused(f"{self.title}: opcode 0x{v:02X} not allowed in stage 2 here")
            return
        if self.held is None:
            return
        if self.addr is None:           # after WRDI + WEL = 0
            if idx == 1:
                if v >= 0x40 or is_read:
                    raise Refused(f"{self.title}: 0x{self.held:02X} at 0x{v:02X}.... after WRDI")
                self.held = None
            return
        if is_read:
            raise Refused(f"{self.title}: address byte of 0x{self.held:02X} as a DATA read")
        self.addr.append(v)
        if idx == 1 and v != SCRATCH >> 16:
            if self.probe_ok and self.held == 0x20 and v == PROBE:
                self.probe_ok, self.held = False, None
                return
            raise Refused(f"{self.title}: 0x{self.held:02X} at 0x{v:02X}.... outside 0xFE8000-0xFE8FFF")
        if idx == 2 and v >> 4 != (SCRATCH >> 8 & 0xFF) >> 4:
            raise Refused(f"{self.title}: 0x{self.held:02X} at 0xFE{v:02X}.. outside 0xFE8000-0xFE8FFF")
        if idx == 3:
            self.held = None

    def dat(self, v):
        self._byte(v, False)
        self.ops.append((1 << 24) | (DATA << 8) | v)

    def rd(self, name, count=1):
        for _ in range(count):
            self._byte(0xFF, True)
        self.names.append((name, count))
        while count:
            n = min(count, 256)
            self.ops.append((2 << 24) | ((n - 1) << 16) | (DATA << 8))
            count -= n

    def reg(self, off, name):
        """A gate register (offset 0x10-0x17), or CTRL. Not a SPI byte."""
        self.ops.append((2 << 24) | (off << 8))
        self.names.append((name, 1))

    def reg_write(self, off, v):
        self.ops.append((1 << 24) | (off << 8) | v)

    def guard(self, mask, expect, why):
        self.ops.append((3 << 24) | (expect << 16) | (mask << 8))
        self.guards.append(why)

    # ---- building blocks ----
    def gate_check(self, wren=False):
        """CS high and one byte clocked (the gate keeps the pin high after a
        reset until then, as the boot ROM does it); the gate's status:
        signature 0101, g_wren_pass = 0 (stage 1) or 1 (wren, stage 2)."""
        self.ctrl(3)
        self.dat(0xFF)
        self.reg(GATE, "gate status")
        if wren:
            self.guard(0xF2, 0x52, "no gate with g_wren_pass = 1 in this bitstream")
            self.stage2 = True
        else:
            if self.stage2:
                raise Refused(f"{self.title}: mixed stage-1 and stage-2 gate checks")
            self.guard(0xF2, 0x50, "no gate with g_wren_pass = 0 in this bitstream")
        self.gate_checked = True

    def wel_off_verified(self):
        """WRDI, then SR1: WEL must read 0, or the list stops here."""
        self.ctrl(0)
        self.dat(0x04)
        self.ctrl(1)
        self.dat(0x05)
        self.rd("SR1 after WRDI")
        self.guard(0x02, 0x00, "WEL is 1 after WRDI")
        self.ctrl(3)
        self.wel_checked = True
        self.armed = False

    def counters(self, tag):
        for off, n in ((0x11, "blocked count"), (0x12, "last blocked opcode"),
                       (0x13, "last blocked A23..16"), (0x14, "last blocked A15..8"),
                       (0x15, "last blocked A7..0"), (0x16, "allowed count")):
            self.reg(off, f"{tag}: {n}")

    def describe(self):
        out = [f"--- {self.title} ({len(self.ops)} ops)"]
        for w in self.ops:
            kind, a, b8, v = w >> 24, (w >> 16) & 0xFF, (w >> 8) & 0x1F, w & 0xFF
            if kind == 1:
                out.append(f"  write +0x{b8:02X} = 0x{v:02X}")
            elif kind == 2:
                out.append(f"  read  +0x{b8:02X}" + (f" x{a + 1}" if a else ""))
            else:
                out.append(f"  guard (last & 0x{(w >> 8) & 0xFF:02X}) == 0x{a:02X} or stop")
        return "\n".join(out)


class Helper:
    def __init__(self, dry):
        self.dry = dry
        self.quiet = False
        self.d = None
        self.seq = 0
        self.collected = []             # --sim, first pass: the lists
        self.replay = None              # --sim, second pass: (status, bytes) per list

    def start(self):
        if self.dry or self.replay is not None:
            return
        binary = build_helper()
        from trxdbg import Dbg
        self.d = Dbg()
        d = self.d
        d.riscv_reset(True)
        d.ddr32_load(LOAD, binary)
        for a in (SEQ, DONE, READY, STATUS, NRES):
            d.ddr32_write(a, 0)
        for off in range(0, len(binary), 4):
            want = int.from_bytes(binary[off:off + 4].ljust(4, b"\0"), "little")
            if d.ddr32_read(LOAD + off) != want:
                raise RuntimeError(f"helper load: word at 0x{LOAD + off:X} differs")
        d.ddr32_write(0xFFF8, LOAD)
        d.ddr32_write(0xFFFC, BOOT_MAGIC)
        d.riscv_reset(False)
        t = time.time()
        while d.ddr32_read(READY) != READY_MAGIC:
            if time.time() - t > 3:
                raise RuntimeError("helper did not start (boot ROM waiting for the magic?)")
            time.sleep(0.05)

    def run(self, s):
        """Carry out one list; returns {name: bytes}. Stops the test on a guard."""
        if len(s.ops) > MAX_OPS:
            raise RuntimeError("list too long")
        if self.dry:
            if self.quiet:
                self.collected.append(s.ops)
            else:
                print(s.describe())
            return None
        print(f"--- {s.title}")
        if self.replay is not None:
            status, raw = self.replay.pop(0)
            n = len(raw)
        else:
            status, raw = self._run_jtag(s.ops)
            n = len(raw)
        if status != 0:
            raise SystemExit(f"STOPPED by a guard in '{s.title}' after {n} bytes read, one of: "
                             f"{s.guards}; nothing after it was sent")
        res, pos = {}, 0
        for name, k in s.names:
            res[name] = raw[pos:pos + k]
            pos += k
        return res

    def _run_jtag(self, ops):
        d = self.d
        words = ops + [0]
        d.ddr32_load(OPS, b"".join(w.to_bytes(4, "little") for w in words))
        for i in (0, len(words) - 1):
            if d.ddr32_read(OPS + 4 * i) != words[i]:
                raise RuntimeError("op list load failed")
        self.seq += 1
        d.ddr32_write(SEQ, self.seq)
        t = time.time()
        while d.ddr32_read(DONE) != self.seq:
            if time.time() - t > 5:
                raise RuntimeError("helper did not finish the list")
            time.sleep(0.01)
        status = d.ddr32_read(STATUS)
        n = d.ddr32_read(NRES)
        raw = b"".join(d.ddr32_read(RES + o).to_bytes(4, "little") for o in range(0, n, 4))[:n]
        return status, raw

    def stop(self):
        if self.d:
            self.d.close()


def build_helper():
    src = os.path.join(HERE, "flashgate", "mailbox.S")
    with tempfile.TemporaryDirectory() as tmp:
        o, elf, b = (os.path.join(tmp, n) for n in ("m.o", "m.elf", "m.bin"))
        for cmd in (["riscv64-elf-as", "-march=rv32i_zicsr", "-mabi=ilp32", src, "-o", o],
                    ["riscv64-elf-ld", "-m", "elf32lriscv", f"-Ttext=0x{LOAD:X}", "-e", "_start",
                     "-o", elf, o],
                    ["riscv64-elf-objcopy", "-O", "binary", elf, b]):
            subprocess.run(cmd, check=True)
        return open(b, "rb").read()


def sr_text(sr1, sr2, sr3):
    return (f"SR1 0x{sr1:02X}: BUSY {sr1 & 1} WEL {sr1 >> 1 & 1} BP2..0 {sr1 >> 2 & 7:03b} "
            f"TB {sr1 >> 5 & 1} SEC {sr1 >> 6 & 1} SRP {sr1 >> 7 & 1}\n"
            f"SR2 0x{sr2:02X}: SRL {sr2 & 1} QE {sr2 >> 1 & 1} LB3..1 {sr2 >> 3 & 7:03b} "
            f"CMP {sr2 >> 6 & 1} SUS {sr2 >> 7 & 1}\n"
            f"SR3 0x{sr3:02X}: WPS {sr3 >> 2 & 1} DRV {sr3 >> 5 & 3}")


def read_ids(s):
    """Status registers, JEDEC and unique ID: reads only."""
    s.ctrl(3); s.dat(0xFF)                      # as the boot ROM: a byte with CS high
    s.ctrl(1); s.dat(0x9F); s.rd("jedec", 3); s.ctrl(3)
    s.ctrl(1); s.dat(0x05); s.rd("sr1"); s.ctrl(3)
    s.ctrl(1); s.dat(0x35); s.rd("sr2"); s.ctrl(3)
    s.ctrl(1); s.dat(0x15); s.rd("sr3"); s.ctrl(3)
    s.ctrl(1); s.dat(0x4B)
    for _ in range(4):
        s.dat(0x00)
    s.rd("unique id", 8); s.ctrl(3)


def read_sector(h, addr, size=4096):
    out = b""
    for off in range(0, size, 1024):
        s = Seq(f"read 0x{addr + off:06X}")
        s.ctrl(1); s.dat(0x03)
        a = addr + off
        s.dat(a >> 16 & 0xFF); s.dat(a >> 8 & 0xFF); s.dat(a & 0xFF)
        s.rd("data", 1024)
        s.ctrl(3)
        r = h.run(s)
        if r is not None:
            out += r["data"]
    return out


def stage0(h, out):
    s = Seq("stage 0: IDs and status registers")
    read_ids(s)
    s.reg(GATE, "gate status (with CS high)")
    r = h.run(s)
    data = read_sector(h, SCRATCH)
    if r is None:
        return 0
    j = r["jedec"]
    print(f"JEDEC ID {j.hex(' ')}" + ("  (W25Q128)" if j == b"\xef\x40\x18" else "  NOT EF 40 18"))
    print(sr_text(r["sr1"][0], r["sr2"][0], r["sr3"][0]))
    print(f"unique ID {r['unique id'].hex()}")
    g = r["gate status (with CS high)"][0]
    print(f"offset 0x10: 0x{g:02X} " + ("(gate present, g_wren_pass %d, g_4byte %d)" %
                                        (g >> 1 & 1, g >> 2 & 1) if g >> 4 == 5 else "(no gate)"))
    with open(out, "wb") as f:
        f.write(data)
    erased = data == b"\xff" * len(data)
    print(f"0x{SCRATCH:06X}..0x{SCRATCH + len(data) - 1:06X} -> {out}: "
          + ("all 0xFF" if erased else f"{sum(1 for x in data if x != 0xFF)} bytes not 0xFF"))
    return 0


def expect(label, got, want):
    def show(v):
        if isinstance(v, bytes):
            return v.hex(" ")
        return f"0x{v:02X}" if isinstance(v, int) and not isinstance(v, bool) else str(v)
    ok = got == want
    print(f"  {'ok  ' if ok else 'FAIL'} {label}: {show(got)}" + ("" if ok else f" (expected {show(want)})"))
    return ok


def stage1(h, stage0_file, control):
    """All lists are built (and checked by Seq) before the first one is sent."""
    ref = None
    if control:
        if not stage0_file:
            raise Refused("--control needs --stage0 with the dump of 0xFE8000")
        ref = open(stage0_file, "rb").read()
        if ref != b"\xff" * 4096:
            raise Refused("--control: the stage-0 dump of 0xFE8000 is not all 0xFF (Q2)")
    steps = []

    def addr_of(r, tag):
        return r[f"{tag}: last blocked A23..16"] + r[f"{tag}: last blocked A15..8"] + \
            r[f"{tag}: last blocked A7..0"]

    # step 0: the gate is there with g_wren_pass = 0; status registers; baseline
    s = Seq("step 0: gate check, status registers, counters")
    s.gate_check()
    s.reg_write(GATE, 0x01)                     # clear the sticky bit
    s.ctrl(1); s.dat(0x05); s.rd("sr1"); s.ctrl(3)
    s.ctrl(1); s.dat(0x35); s.rd("sr2"); s.ctrl(3)
    s.ctrl(1); s.dat(0x15); s.rd("sr3"); s.ctrl(3)
    s.counters("before")

    def check0(r, st):
        st["sr"] = r["sr1"] + r["sr2"] + r["sr3"]
        st["blk"], st["ok"] = r["before: blocked count"][0], r["before: allowed count"][0]
        print(sr_text(*st["sr"]))
        print(f"  blocked count {st['blk']}, allowed count {st['ok']}")
        return True
    steps.append((s, check0))

    # step 1: WREN goes out as WRDI
    s = Seq("step 1: WREN -> WRDI")
    s.gate_check()
    s.ctrl(0); s.dat(0x06)
    s.ctrl(1); s.dat(0x05); s.rd("sr1 after WREN"); s.ctrl(3)
    s.guard(0x02, 0x00, "WREN set WEL: this bitstream does not turn it into WRDI")
    steps.append((s, lambda r, st: expect("WEL after WREN", r["sr1 after WREN"][0] & 2, 0)))

    # step 2: erase at 0x00.... -> the chip got 0x9F
    s = Seq("step 2: erase 0x00....")
    s.gate_check()
    s.wel_off_verified()
    s.ctrl(1); s.dat(0x20); s.dat(0x00); s.rd("id bytes", 2); s.ctrl(3)
    s.counters("after step 2")

    def check2(r, st):
        t = "after step 2"
        return all([expect("MISO after 20 00", r["id bytes"], b"\x40\x18"),
                    expect("blocked count", r[f"{t}: blocked count"][0], (st["blk"] + 1) & 0xFF),
                    expect("last blocked opcode", r[f"{t}: last blocked opcode"][0], 0x20),
                    expect("last blocked address", addr_of(r, t), b"\x00\xff\xff")])
    steps.append((s, check2))

    # step 3: program at 0x22...., 64K erase at 0x3F...., 0x12 with 00 00
    for title, op, addr, n, want in (("program 0x22....", 0x02, [0x22], 2, b"\x40\x18"),
                                     ("64K erase 0x3F....", 0xD8, [0x3F], 2, b"\x40\x18"),
                                     ("4-byte program 00 00..", 0x12, [0x00, 0x00], 1, b"\x18")):
        s = Seq(f"step 3: {title}")
        s.gate_check()
        s.wel_off_verified()
        s.ctrl(1); s.dat(op)
        for b in addr:
            s.dat(b)
        s.rd("id bytes", n); s.ctrl(3)
        s.reg(0x12, "last blocked opcode")
        label = f"MISO after {op:02X} " + " ".join(f"{b:02X}" for b in addr)
        steps.append((s, lambda r, st, label=label, want=want, op=op:
                      expect(label, r["id bytes"], want) &
                      expect("last blocked opcode", r["last blocked opcode"][0], op)))

    # step 4: chip erase, status writes, volatile SR enable, each in its own CS pulse
    s = Seq("step 4: C7 60 01 31 50")
    s.gate_check()
    s.wel_off_verified()
    for op in (0xC7, 0x60, 0x01, 0x31, 0x50):
        s.ctrl(0); s.dat(op)
    s.ctrl(3)
    s.ctrl(1); s.dat(0x05); s.rd("sr1"); s.ctrl(3)
    s.ctrl(1); s.dat(0x35); s.rd("sr2"); s.ctrl(3)
    s.ctrl(1); s.dat(0x15); s.rd("sr3"); s.ctrl(3)
    s.counters("after step 4")

    def check4(r, st):
        t = "after step 4"
        return all([expect("SR1-3 unchanged", r["sr1"] + r["sr2"] + r["sr3"], st["sr"]),
                    expect("blocked count", r[f"{t}: blocked count"][0], (st["blk"] + 9) & 0xFF),
                    expect("last blocked opcode", r[f"{t}: last blocked opcode"][0], 0x50),
                    expect("allowed count", r[f"{t}: allowed count"][0], st["ok"])])
    steps.append((s, check4))

    # step 5 (--control): a real erase command for sector 0xFE8000 (address
    # 0xFE80FF, the last byte is a read) while WEL = 0; the chip ignores it
    if control:
        s = Seq("step 5: erase 0xFE80FF with WEL = 0 (control)")
        s.gate_check()
        s.wel_off_verified()
        s.real_ok = True
        s.ctrl(1); s.dat(0x20); s.dat(0xFE); s.dat(0x80); s.rd("miso", 1); s.ctrl(3)
        s.ctrl(1); s.dat(0x05); s.rd("sr1"); s.ctrl(3)
        s.counters("after step 5")

        def check5(r, st):
            t = "after step 5"
            ok = r["miso"] != b"\x18"
            print(f"  {'ok  ' if ok else 'FAIL'} MISO after 20 FE 80: {r['miso'].hex()} (0x18 = blocked)")
            return ok & all([
                expect("allowed count", r[f"{t}: allowed count"][0], (st["ok"] + 1) & 0xFF),
                expect("blocked count", r[f"{t}: blocked count"][0], (st["blk"] + 9) & 0xFF),
                expect("WEL", r["sr1"][0] & 2, 0)])
        steps.append((s, check5))

    good, st = True, {}
    for s, check in steps:
        r = h.run(s)
        if r is not None:
            good &= bool(check(r, st))
    if control:
        data = read_sector(h, SCRATCH)
        if not h.dry:
            good &= expect("scratch sector unchanged", data == ref, True)
    if not h.dry:
        print("STAGE 1 PASS" if good else "STAGE 1 FAIL")
        print("Step 6 by hand: load the app again (normal boot), check settings and the flash drive.")
    return 0 if good else 1


def poll_ready(h, what, timeout):
    """SR1 until BUSY = 0; returns SR1 (None without a board). A list per poll,
    so the host can give up: then WRDI and stop (§8 stage 2: BUSY stuck)."""
    t = time.time()
    while True:
        s = Seq(f"poll BUSY after {what}")
        s.ctrl(1); s.dat(0x05); s.rd("sr1"); s.ctrl(3)
        r = h.run(s)
        if r is None:
            return None
        sr1 = r["sr1"][0]
        if not sr1 & 1:
            return sr1
        if time.time() - t > timeout:
            wrdi_stop(h, f"BUSY still 1 after {timeout} s ({what}), SR1 0x{sr1:02X}")
        time.sleep(0.005)


def wrdi_stop(h, why):
    s = Seq("stop: WRDI, SR1")
    s.ctrl(0); s.dat(0x04)
    s.ctrl(1); s.dat(0x05); s.rd("sr1"); s.ctrl(3)
    r = h.run(s)
    sr1 = f", SR1 after WRDI 0x{r['sr1'][0]:02X}" if r else ""
    raise SystemExit(f"STOPPED: {why}; WRDI sent{sr1}; nothing further done")


def write_cmd(title, op, addr, data=b""):
    """Gate check (g_wren_pass 1), BUSY 0, WREN, WEL = 1, then op at addr
    (+ data); the helper stops the list (WRDI, CS high) if a guard fails."""
    s = Seq(title)
    s.gate_check(wren=True)
    s.ctrl(1); s.dat(0x05); s.rd("sr1 before"); s.ctrl(3)
    s.guard(0x03, 0x00, "BUSY or WEL set before WREN")
    s.ctrl(0); s.dat(0x06)
    s.ctrl(1); s.dat(0x05); s.rd("sr1 after WREN"); s.ctrl(3)
    s.guard(0x03, 0x02, "WEL not 1 (or BUSY) after WREN")
    s.ctrl(1); s.dat(op)
    s.dat(addr >> 16 & 0xFF); s.dat(addr >> 8 & 0xFF); s.dat(addr & 0xFF)
    for b in data:
        s.dat(b)
    s.ctrl(3)
    return s


def stage2(h, out, probe):
    """Real program/erase, only in 0xFE8000-0xFE8FFF; leaves it all 0xFF as found."""
    good, ff = True, b"\xff" * 4096

    def must(label, got, want, stop=True):
        nonlocal good
        if h.dry:
            return
        if not expect(label, got, want):
            good = False
            if stop:
                wrdi_stop(h, f"{label} mismatch")

    s = Seq("stage 2 step 0: gate check (g_wren_pass 1), status registers, counters")
    s.gate_check(wren=True)
    s.reg_write(GATE, 0x01)
    s.ctrl(1); s.dat(0x05); s.rd("sr1"); s.ctrl(3)
    s.ctrl(1); s.dat(0x35); s.rd("sr2"); s.ctrl(3)
    s.ctrl(1); s.dat(0x15); s.rd("sr3"); s.ctrl(3)
    s.counters("before")
    r = h.run(s)
    st = {}
    if r is not None:
        st["sr"] = r["sr1"] + r["sr2"] + r["sr3"]
        st["blk"], st["ok"] = r["before: blocked count"][0], r["before: allowed count"][0]
        print(sr_text(*st["sr"]))
        print(f"  blocked count {st['blk']}, allowed count {st['ok']}")
        must("SR1 (BUSY 0, WEL 0, BP 000)", r["sr1"][0], 0x00)

    data = read_sector(h, SCRATCH)
    if not h.dry:
        with open(out, "wb") as f:
            f.write(data)
        print(f"0x{SCRATCH:06X} saved to {out}")
        if data != ff:
            raise Refused(f"0x{SCRATCH:06X} is not all 0xFF ({sum(1 for x in data if x != 0xFF)} "
                          "bytes): stage 2 would not leave it as found")

    steps = (("erase 0xFE8000", 0x20, b"", 2.0, ff),
             ("program page 0xFE8000", 0x02, PATTERN, 0.1, PATTERN + ff[256:]),
             ("erase 0xFE8000 again", 0x20, b"", 2.0, ff))
    for title, op, payload, tmo, want in steps:
        r = h.run(write_cmd(f"stage 2: {title}", op, SCRATCH, payload))
        if r is not None:
            print(f"  SR1 before 0x{r['sr1 before'][0]:02X}, after WREN 0x{r['sr1 after WREN'][0]:02X}")
        sr1 = poll_ready(h, title, tmo)
        if sr1 is not None:
            must(f"WEL after {title}", sr1 & 2, 0)
        must(f"0x{SCRATCH:06X} after {title}", read_sector(h, SCRATCH) == want, True)

    s = Seq("stage 2: counters after the scratch writes")
    s.counters("after")
    r = h.run(s)
    if r is not None:
        must("allowed count", r["after: allowed count"][0], (st["ok"] + 3) & 0xFF, stop=False)
        must("blocked count", r["after: blocked count"][0], st["blk"], stop=False)

    # stage 1 step 2 on the release build, WEL forced off first (spec §8)
    s = Seq("stage 2: erase 0x00.... with WEL = 0")
    s.gate_check(wren=True)
    s.wel_off_verified()
    s.ctrl(1); s.dat(0x20); s.dat(0x00); s.rd("id bytes", 2); s.ctrl(3)
    s.ctrl(1); s.dat(0x05); s.rd("sr1"); s.ctrl(3)
    s.counters("blocked 0")
    r = h.run(s)
    if r is not None:
        t = "blocked 0"
        must("MISO after 20 00", r["id bytes"], b"\x40\x18", stop=False)
        must("blocked count", r[f"{t}: blocked count"][0], (st["blk"] + 1) & 0xFF, stop=False)
        must("last blocked opcode", r[f"{t}: last blocked opcode"][0], 0x20, stop=False)
        must("WEL", r["sr1"][0] & 2, 0)
    nblk = 1

    if probe:
        s = Seq("stage 2: erase 0x10.... with WEL = 1 (--wel1-probe)")
        s.gate_check(wren=True)
        s.probe_ok = True
        s.ctrl(1); s.dat(0x05); s.rd("sr1 before"); s.ctrl(3)
        s.guard(0x03, 0x00, "BUSY or WEL set before WREN")
        s.ctrl(0); s.dat(0x06)
        s.ctrl(1); s.dat(0x05); s.rd("sr1 after WREN"); s.ctrl(3)
        s.guard(0x03, 0x02, "WEL not 1 after WREN")
        s.ctrl(1); s.dat(0x20); s.dat(PROBE); s.rd("id bytes", 2); s.ctrl(3)
        s.ctrl(1); s.dat(0x05); s.rd("sr1 after"); s.ctrl(3)
        s.ctrl(0); s.dat(0x04)
        s.ctrl(1); s.dat(0x05); s.rd("sr1 after WRDI"); s.ctrl(3)
        s.counters("probe")
        r = h.run(s)
        if r is not None:
            t = "probe"
            print(f"  SR1 after WREN 0x{r['sr1 after WREN'][0]:02X}, after the probe "
                  f"0x{r['sr1 after'][0]:02X}, after WRDI 0x{r['sr1 after WRDI'][0]:02X}")
            must("MISO after 20 10 (WEL 1)", r["id bytes"], b"\x40\x18", stop=False)
            must("BUSY after the probe", r["sr1 after"][0] & 1, 0, stop=False)
            must("blocked count", r[f"{t}: blocked count"][0], (st["blk"] + 2) & 0xFF, stop=False)
            must("last blocked opcode", r[f"{t}: last blocked opcode"][0], 0x20, stop=False)
            must("WEL after WRDI", r["sr1 after WRDI"][0] & 2, 0)
        nblk = 2

    s = Seq("stage 2: final status registers")
    s.ctrl(1); s.dat(0x05); s.rd("sr1"); s.ctrl(3)
    s.ctrl(1); s.dat(0x35); s.rd("sr2"); s.ctrl(3)
    s.ctrl(1); s.dat(0x15); s.rd("sr3"); s.ctrl(3)
    s.counters("end")
    r = h.run(s)
    if r is not None:
        must("SR1-3 as at the start", r["sr1"] + r["sr2"] + r["sr3"], st["sr"], stop=False)
        print(f"  end: blocked count {r['end: blocked count'][0]} (start {st['blk']}, +{nblk}), "
              f"allowed count {r['end: allowed count'][0]} (start {st['ok']}, +3)")
    if not h.dry:
        print("STAGE 2 PASS" if good else "STAGE 2 FAIL")
    return 0 if good else 1


def run_sim(stage_fn, simdir, generics):
    """Two passes over the stage: collect the lists, run them on tb_cpu
    (rvlite + gate + flash model) under ghdl, then check the results."""
    h = Helper(True)
    h.quiet = True
    stage_fn(h)
    binary = build_helper()
    with open(os.path.join(simdir, "prog.hex"), "w") as f:
        for off in range(0, len(binary), 4):
            f.write(f"{int.from_bytes(binary[off:off + 4].ljust(4, bytes(1)), 'little'):08X}\n")
    with open(os.path.join(simdir, "lists.txt"), "w") as f:
        for ops in h.collected:
            f.write(f"L {len(ops)}\n")
            for w in ops:
                f.write(f"{w:08X}\n")
    subprocess.run(["nice", "-n", "15", "ghdl", "-r", "--std=08", "-frelaxed", "-fsynopsys",
                    "tb_cpu", "-gg_prog=prog.hex", "-gg_lists=lists.txt", "-gg_out=results.txt",
                    *generics, "--assert-level=failure", "--ieee-asserts=disable"],
                   cwd=simdir, check=True, stdout=subprocess.DEVNULL)
    replay = []
    with open(os.path.join(simdir, "results.txt")) as f:
        lines = f.read().split()
    i = 0
    while i < len(lines):
        assert lines[i] == "R"
        status, n = int(lines[i + 1]), int(lines[i + 2])
        replay.append((status, bytes(int(x, 16) for x in lines[i + 3:i + 3 + n])))
        i += 3 + n
    h2 = Helper(False)
    h2.replay = replay
    return stage_fn(h2)


def main(argv):
    if not argv or argv[0] not in ("stage0", "stage1", "stage2"):
        print(__doc__)
        return 1
    stage, args = argv[0], argv[1:]
    dry = "--dry-run" in args

    def opt(name):
        return args[args.index(name) + 1] if name in args else None

    out = opt("--out") or time.strftime("flashgate_fe8000_%Y%m%d-%H%M%S.bin")

    def stage_fn(hh):
        if stage == "stage0":
            return stage0(hh, out)
        if stage == "stage2":
            return stage2(hh, out, "--wel1-probe" in args)
        return stage1(hh, opt("--stage0"), "--control" in args)

    h = Helper(dry)
    try:
        if opt("--sim"):
            gen = [a for a in args if a.startswith("-gg_")]
            return run_sim(stage_fn, opt("--sim"), gen)
        h.start()
        return stage_fn(h)
    except Refused as e:
        print(f"REFUSED: {e}")
        return 2
    finally:
        h.stop()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
