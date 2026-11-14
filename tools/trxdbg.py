#!/usr/bin/env python3
"""Debug access to a running TRX64 design over the USB-Blaster (JTAG USER1).

    tools/trxdbg.sh regs                 status registers of c64_basic
    tools/trxdbg.sh peek 0400 [len]      read C64 RAM
    tools/trxdbg.sh poke 0400 01 02 ..   write C64 RAM
    tools/trxdbg.sh screen               the 40x25 text screen at $0400 as text
    tools/trxdbg.sh ddrpeek 1000 [len]   read DDR2 (16-bit words, word address)
    tools/trxdbg.sh ddrpoke 1000 ABCD .. write DDR2 words
    tools/trxdbg.sh ddrtest [count]      write a pattern to spread-out words, read back
    tools/trxdbg.sh load file.prg [run]  put a .prg into C64 RAM at its load address;
                                         "run" types RUN (BASIC) or SYS <addr>
    tools/trxdbg.sh console [follow]     the RISC-V console (last 4 KB); "follow" keeps
                                         printing new text until Ctrl-C
    tools/trxdbg.sh riscv file.bin [addr] [follow]
                                         RISC-V: hold it in reset, load file.bin into
                                         DDR2 at addr (default 0x30000), set the boot
                                         ROM's magic (jump to addr), release the reset;
                                         "follow" then prints the console from the start
    tools/trxdbg.sh riscv reset|run      hold the RISC-V in reset / let it run
    tools/trxdbg.sh dcache [on|off|check|clear|stat|perf [clear]|mask <bits>]
                                         T133 host data cache: switch (default off after power-up) and counters
    tools/trxdbg.sh boot check [--dump flash.bin]
                                         boot ROM request (T15 §7): read the app image
                                         in flash (0x220000) and sum it, no jump; with
                                         --dump, compare with that flash dump
    tools/trxdbg.sh boot staged file [addr] [follow]
                                         put a flash-format image (.app; a raw .bin is
                                         wrapped as {0x30000, size, 0x30000}) into DDR2
                                         at addr (default 0x03000000) and let the boot
                                         ROM load and start it like the flash image
    tools/trxdbg.sh boot flash [follow]  boot from flash past the build stamp check
    tools/trxdbg.sh boot result          the boot ROM's result block (0xFFC0)
    tools/trxdbg.sh ddrload file addr    raw file into DDR2 at byte address addr (32-bit writes)
    tools/trxdbg.sh ddrdump addr len     DDR2 bytes (32-bit reads) as hex
    tools/trxdbg.sh mdio phy reg [value] Ethernet PHY register over MDIO (phy 0 or 3,
                                         as the app addresses it), read or write;
                                         takes MDC/MDIO from the app meanwhile
    tools/trxdbg.sh net                  Ethernet and app counters (registers 12-15)
    tools/trxdbg.sh decouple [on|off]    T79 DFX build: cut the core off for a swap
                                         (0x100029 bit 0, T79.1 section 7), or show it
                                         (bit 1 = effective, JTAG or app bit)
    tools/trxdbg.sh pause [on|off]       machine pause (the whole C64 and drive A stop
                                         on one cycle; RAM peek/poke still work), or
                                         show whether it is paused
    tools/trxdbg.sh trx OFF [VALUE]      32-bit register at byte offset OFF (hex) of the
                                         IO block 0x101A0000, read or write; the pages in
                                         the C64 clock only (0 from 0x100, 1, 5: trx_sys.v)
    tools/trxdbg.sh sb                   the state bus: 6510, VIC, CIA 1/2, cartridge as
                                         the shadows hold them (side-effect free; while
                                         paused they follow the machine)
    tools/trxdbg.sh mode                 System Mode (T40, mode_ctl.v): the mode in use and
                                         requested, the last switch, and the VIC's frame as
                                         it runs (lines, dot4x clocks per line and frame)
    tools/trxdbg.sh turbo [clear]        T29 turbo: TURBO_STAT (engine on, CPU cycles per PHI2,
                                         badline timing, enable word, speed index, hold) and
                                         the state words; "clear" clears the sticky "ran" bit
    tools/trxdbg.sh rvpc [clear] [file.elf]
                                         the RISC-V's state (rv_dbg.v): the PC of its
                                         last instruction, what it waits for (I/O or
                                         memory, with the address), how long the PC has
                                         stood still (two snapshots, so a hang shows as a
                                         run that grows), the longest stall since reset;
                                         with the app's ELF the PCs get function names;
                                         "clear" resets the longest-stall record
    tools/trxdbg.sh ddr2                 the DDR2 calibration of this boot (ddr2_ctrl.v, OPT-ddr2 §9):
                                         done / ok, bitslip position, read delay, IDELAY taps, the
                                         window width per lane (the margin), whether the scan range
                                         cut it; exit status 1 when the calibration failed or has
                                         not ended (JTAG 0x100050-0x100052, the app reads the same
                                         words at 0x101A0020)
    tools/trxdbg.sh freeze [on|off]      GATE_CTRL b0: the app's C64_STOP pauses the whole
                                         machine (on) or only the CPU (off, reset value);
                                         on a core with the freeze session also its counters
                                         (the slim core has none: they read 0)

Talks to jtag_dbg.v in the design: one 64-bit USER1 data register, a command
shifted in, the result of the previous command shifted out. Only USER1, IDCODE
and BYPASS are ever loaded into the instruction register.

The USB-Blaster driver comes from the 1541ultimate-tools checkout
(tooling/u64ii_bscan.py); set TRX_TOOLING if it is not in the default place.
"""
import os
import sys

TOOLING = os.environ.get("TRX_TOOLING",
                         os.path.expanduser("~/Development/1541ultimate-tools/tooling"))
sys.path.insert(0, TOOLING)
import u64ii_jtag as jt          # noqa: E402
from u64ii_bscan import Blaster  # noqa: E402

IR_USER1 = 0x02
ALLOWED = {IR_USER1, jt.IR_IDCODE, jt.IR_BYPASS}
REG_BASE = 0x100000
REG_NAMES = ["magic", "x_text", "kb_status", "keys_lo", "keys_hi", "pins", "con_count"]
DDR_BASE = 0x800000
DDR32_BASE = 0x400000               # 32-bit words, 22 address bits per window
DDR_WINDOW = REG_BASE + 8           # 16 MB (8M words) per window value
MACHINE = REG_BASE + 9              # bit 0 C64 reset, bit 1 C64 CPU pause, bit 2 RISC-V reset,
                                    # bit 3 machine pause, bit 4 app's C64_STOP = CPU pause only;
                                    # read bit 8 = machine paused
RISCV_RESET = 4
MACHINE_PAUSE = 8
MACHINE_PAUSED = 0x100
CON_COUNT = REG_BASE + 6            # bytes the RISC-V wrote to its console
TRX_PAGE_WIN = 0x101000             # the IO block's page JTAG_PAGE, 32-bit words
TRX_JTAG_PAGE = 0x101400            # JTAG_PAGE itself
GATE_CTRL, SB_ADDR, SB_DATA = 0x0100, 0x1000, 0x1004
SH_STATUS, SH_ACCESSES, SH_REAL, SH_GCLK, SH_UNMAPPED = 0x5004, 0x5008, 0x500C, 0x5010, 0x5014
CON_BASE = 0x200000                 # the last 4 KB of them, 4 bytes per read
CON_SIZE = 4096
BOOT_MAGIC = 0x1571BABE             # boot ROM: jump to [0xFFF8] when [0xFFFC] holds it
BOOT_REQ = 0x7158B007               # boot ROM request at 0xFFF4: mode 0xFFF0, address 0xFFEC
BOOT_RES = 0xFFC0                   # its result block (docs/specs/T15-flash-boot.md §5)
BOOT_STATUS = {0: "running", 1: "booting", 2: "checked", 3: "empty (erased header)",
               4: "header invalid", 5: "identity mismatch: no flash boot", 6: "locked (button)",
               7: "bad request", 8: "staged image overlaps its destination"}
FLASH_APPL = 0x220000
MDIO_REG = REG_BASE + 7             # bit 31 take over, bit 0 MDC, bit 1 MDIO; read bit 2 = pin


class DebugTap(jt.Tap):
    def ir(self, instruction, read=False):
        if instruction not in ALLOWED:
            raise jt.JtagError(f"instruction 0x{instruction:02X} is not allowed here")
        return super().ir(instruction, read)


class Dbg:
    def __init__(self):
        self.cable = Blaster.open()
        self.tap = DebugTap(self.cable)
        self.tap.reset()
        idcode = self.tap.idcode()
        # XC7A50T (C64 Ultimate) or XC7A100T (Ultimate 64 Elite II, T53): same board, same design
        if idcode & 0x0FFFFFFF not in (0x0362C093, 0x03631093):
            raise jt.JtagError(f"IDCODE 0x{idcode:08X}: not an XC7A50T or XC7A100T")
        self.tap.ir(IR_USER1)

    def close(self):
        self.tap.reset()
        self.cable.release()

    # Designs built before the TDO fix return every result one bit to the
    # left (the top bit is lost, which read results do not need);
    # TRXDBG_SHIFT=1 undoes that.
    SHIFT = int(os.environ.get("TRXDBG_SHIFT", "0"))

    def _scan(self, op, addr, data=0):
        word = (op << 62) | ((addr & 0xFFFFFF) << 32) | (data & 0xFFFFFFFF)
        return self.tap.dr(word, 64, read=True) >> self.SHIFT

    def read(self, addr):
        self._scan(1, addr)
        res = self._scan(0, 0)
        if (res >> 62) != 1 or ((res >> 32) & 0xFFFFFF) != (addr & 0xFFFFFF):
            raise jt.JtagError(f"read of 0x{addr:06X} got 0x{res:016X}; is a TRX64 design loaded?")
        return res & 0xFFFFFFFF

    def write(self, addr, data):
        self._scan(2, addr, data)

    def write_bytes(self, addr, data):
        """Bulk write to a byte target (C64 RAM): four bytes per scan (op 3),
        all scans queued and sent at once, no read back."""
        m = self.cable
        for off in range(0, len(data), 4):
            chunk = bytes(data[off:off + 4]).ljust(4, b"\0")
            n = min(4, len(data) - off)
            if n == 4:
                word = (3 << 62) | (((addr + off) & 0xFFFFFF) << 32) | int.from_bytes(chunk, "little")
                self._queue(word)
            else:
                for i in range(n):
                    self._queue((2 << 62) | (((addr + off + i) & 0xFFFFFF) << 32) | chunk[i])
        m.flush()

    def _queue(self, word):
        # Tap.dr without its flush: Select-DR, Capture-DR, Shift-DR, 63 bits as
        # bytes and bits, the last with TMS to Exit1, then Update and Idle.
        m = self.cable
        m.tms(0b001, 3)
        m.bytes_out((word & ((1 << 56) - 1)).to_bytes(7, "little"))
        m.bits_out(word >> 56, 7)
        m.tms(0b1, 1, tdi=word >> 63)
        m.tms(0b01, 2)

    # DDR2: word address 0 .. 64M-1 through the 8M-word window
    def _window(self, word):
        win = word >> 23
        if getattr(self, "win", None) != win:
            self.write(DDR_WINDOW, win)
            self.win = win
        return DDR_BASE + (word & 0x7FFFFF)

    def ddr_read(self, word):
        return self.read(self._window(word)) & 0xFFFF

    def ddr_write(self, word, value):
        self.write(self._window(word), value & 0xFFFF)

    # DDR2 by byte address, 32-bit words (address bits 1:0 ignored)
    def _window32(self, addr):
        win = addr >> 24
        if getattr(self, "win", None) != win:
            self.write(DDR_WINDOW, win)
            self.win = win
        return DDR32_BASE + ((addr >> 2) & 0x3FFFFF)

    def ddr32_read(self, addr):
        return self.read(self._window32(addr))

    def ddr32_write(self, addr, value):
        self.write(self._window32(addr), value)

    def ddr32_load(self, addr, data):
        """Bulk write, four bytes per scan, queued without read back. The design
        keeps one DDR2 write waiting; a scan takes far longer than an access."""
        data = bytes(data) + b"\0" * (-len(data) % 4)
        for off in range(0, len(data), 4):
            a = addr + off
            if off == 0 or (a & 0xFFFFFF) == 0:
                self.cable.flush()
                self._window32(a)
            word = int.from_bytes(data[off:off + 4], "little")
            self._queue((2 << 62) | ((DDR32_BASE + ((a >> 2) & 0x3FFFFF)) << 32) | word)
            if (off >> 2) % 4096 == 4095:
                self.cable.flush()
        self.cable.flush()

    def read_many(self, addrs):
        """Reads of fast targets (registers, C64 RAM, console ring) pipelined:
        each scan carries the next command and brings the result of the last."""
        m = self.cable
        words = [(1 << 62) | ((a & 0xFFFFFF) << 32) for a in addrs] + [0]
        for w in words:
            m.tms(0b001, 3)
            m.bytes_out((w & ((1 << 56) - 1)).to_bytes(7, "little"), True)
            m.bits_out((w >> 56) & 0x7F, 7, True)
            m.tms(0b1, 1, tdi=w >> 63, read=True)
            m.tms(0b01, 2)
        raw = m.flush()
        out = []
        for i in range(len(words)):
            r = raw[9 * i:9 * i + 9]
            v = int.from_bytes(r[:7], "little") | ((r[7] >> 1) << 56) | (((r[8] >> 7) & 1) << 63)
            out.append(v >> self.SHIFT)
        res = []
        for a, v in zip(addrs, out[1:]):
            if (v >> 62) != 1 or ((v >> 32) & 0xFFFFFF) != (a & 0xFFFFFF):
                raise jt.JtagError(f"read of 0x{a:06X} got 0x{v:016X}")
            res.append(v & 0xFFFFFFFF)
        return res

    def console(self, since):
        """Console bytes from count `since` up to now (at most the last 4 KB);
        returns (bytes, new count)."""
        count = self.read(CON_COUNT)
        start = max(since, count - CON_SIZE)
        if (count - start) & 0xFFFFFFFF > CON_SIZE:      # count wrapped or reader ahead
            start = count - min(count, CON_SIZE)
        first, last = start & ~3, (count + 3) & ~3
        words = self.read_many([CON_BASE + (a % CON_SIZE) for a in range(first, last, 4)]) if last > first else []
        raw = b"".join(w.to_bytes(4, "little") for w in words)
        return raw[start - first:count - first], count

    # MDIO frames as the app's mdio.c clocks them (Gideon's bit order and
    # turnaround), one debug-register write per pin change
    def _mdio_bit(self, val):
        b = 0x80000000 | ((1 if val else 0) << 1)
        self._queue((2 << 62) | (MDIO_REG << 32) | b)
        self._queue((2 << 62) | (MDIO_REG << 32) | b | 1)
        self._queue((2 << 62) | (MDIO_REG << 32) | b)

    def _mdio_head(self, op_read, phy, reg):
        for _ in range(33):
            self._mdio_bit(1)
        for bit in (0, 1) + ((1, 0) if op_read else (0, 1)) + (0, 0, 0, phy, phy):
            self._mdio_bit(bit)
        for i in range(5):
            self._mdio_bit(reg & (0x10 >> i))

    def mdio_read(self, phy, reg):
        self._mdio_head(True, 1 if phy else 0, reg)
        self._mdio_bit(1)
        self.cable.flush()
        value = 0
        for _ in range(16):
            self._mdio_bit(1)
            self.cable.flush()
            value = (value << 1) | ((self.read(MDIO_REG) >> 2) & 1)
        self.write(MDIO_REG, 0)
        return value

    def mdio_write(self, phy, reg, value):
        self._mdio_head(False, 1 if phy else 0, reg)
        self._mdio_bit(1)
        self._mdio_bit(0)
        for i in range(16):
            self._mdio_bit(value & (0x8000 >> i))
        self._mdio_bit(1)
        self.cable.flush()
        self.write(MDIO_REG, 0)

    def riscv_reset(self, hold):
        ctl = self.read(MACHINE)
        self.write(MACHINE, (ctl | RISCV_RESET) if hold else (ctl & ~RISCV_RESET))

    # our IO block (trx_sys.v): byte offset off, through the JTAG page
    def _trx_page(self, off):
        page = (off >> 12) & 0xF
        if getattr(self, "trx_page", None) != page:
            self.write(TRX_JTAG_PAGE, page)
            self.trx_page = page
        return TRX_PAGE_WIN + ((off & 0xFFF) >> 2)

    def trx_read(self, off):
        return self.read(self._trx_page(off))

    def trx_write(self, off, value):
        self.write(self._trx_page(off), value)

    def sb_words(self, chip, count):
        self.trx_write(SB_ADDR, chip << 7)
        return [self.trx_read(SB_DATA) for _ in range(count)]   # SB_DATA increments SB_ADDR

    def machine_pause(self, on):
        ctl = self.read(MACHINE) & 0x1F
        self.write(MACHINE, (ctl | MACHINE_PAUSE) if on else (ctl & ~MACHINE_PAUSE))


def app_sums(body, length):
    """The boot ROM's sums over ceil(length / 4) little-endian words."""
    s = x = 0
    body = body + b"\0" * 3
    for i in range(0, (length + 3) & ~3, 4):
        w = int.from_bytes(body[i:i + 4], "little")
        s = (s + w) & 0xFFFFFFFF
        x = (((x << 1) | (x >> 31)) & 0xFFFFFFFF) ^ w
    return s, x


def app_image(path):
    """A flash-format image ({dest, length, run} LE + data): a .bin is wrapped
    as hex2bin -r does for the app's single record at 0x30000."""
    data = open(path, "rb").read()
    if path.endswith(".bin"):
        data = (0x30000).to_bytes(4, "little") + len(data).to_bytes(4, "little") + \
               (0x30000).to_bytes(4, "little") + data + b"\0" * (-len(data) % 4)
    length = int.from_bytes(data[4:8], "little")
    if len(data) < 12 or not 0 < length <= len(data) - 12:
        raise SystemExit(f"{path}: not a flash-format image (length {length:#x}, size {len(data):#x})")
    return data


def boot_result(d, quiet=False):
    r = [d.ddr32_read(BOOT_RES + 4 * i) for i in range(11)]
    if not quiet:
        if r[0] != 0x54313552:
            print(f"no result block (0x{r[0]:08X})")
        else:
            src = {1: "flash", 2: "DDR2"}.get(r[2], "-")
            print(f"status {r[1]} {BOOT_STATUS.get(r[1], '?')}, source {src}")
            print(f"  header dest {r[3]:08X} length {r[4]:08X} run {r[5]:08X}")
            print(f"  sum {r[6]:08X} xrot {r[7]:08X}")
            print(f"  build {r[8]:08X}, flash bitstream "
                  + (f"{r[9]:08X}" if r[10] & 1 else "without a stamp")
                  + ("" if r[10] & 2 else " (BUILD register missing)"))
    return r


def boot_request(d, mode, addr=0, image=None):
    """CPU in reset, clear the JTAG magic and the result block, stage the
    image, write the request, let the boot ROM run; returns the console count
    at the release."""
    d.riscv_reset(True)
    d.ddr32_write(0xFFFC, 0)
    for i in range(16):
        d.ddr32_write(BOOT_RES + 4 * i, 0)
    if image is not None:
        d.ddr32_load(addr, image)
        probes = sorted(set(list(range(0, len(image) - 3, 0x10000)) + [(len(image) - 4) & ~3]))
        bad = [o for o in probes if d.ddr32_read(addr + o) != int.from_bytes(image[o:o + 4], "little")]
        print(f"staged {len(image)} bytes at 0x{addr:X}, {len(probes) - len(bad)}/{len(probes)} probes ok")
        if bad:
            raise SystemExit("staging failed")
    d.ddr32_write(0xFFEC, addr)
    d.ddr32_write(0xFFF0, mode)
    d.ddr32_write(0xFFF4, BOOT_REQ)
    count = d.read(CON_COUNT)
    d.riscv_reset(False)
    return count


def boot_wait(d, count, timeout=30.0):
    """Console text until the result block has a status (or the time is up)."""
    import time
    t = time.time()
    while True:
        text, count = d.console(count)
        sys.stdout.write(text.decode("latin-1").replace("\r", ""))
        sys.stdout.flush()
        r = boot_result(d, quiet=True)
        if r[0] == 0x54313552 and r[1] != 0:
            time.sleep(0.3)
            text, count = d.console(count)
            sys.stdout.write(text.decode("latin-1").replace("\r", ""))
            print(f"(boot ROM done after {time.time() - t:.1f} s)")
            return r, count
        if time.time() - t > timeout:
            print("timeout: no result from the boot ROM")
            return r, count
        time.sleep(0.2)


PETSCII_SCREEN = "@ABCDEFGHIJKLMNOPQRSTUVWXYZ[£]^_ !\"#$%&'()*+,-./0123456789:;<=>?"


def screen_char(code):
    c = code & 0x7F
    return PETSCII_SCREEN[c] if c < 64 else "."


def elf_symbols(path):
    """(address, size, name) of the function symbols of a 32-bit little-endian ELF."""
    import struct
    data = open(path, "rb").read()
    shoff, = struct.unpack_from("<I", data, 0x20)
    shentsize, shnum = struct.unpack_from("<HH", data, 0x2E)
    sections = [struct.unpack_from("<IIIIIIIIII", data, shoff + i * shentsize) for i in range(shnum)]
    out = []
    for sh in sections:
        if sh[1] != 2:                      # SHT_SYMTAB
            continue
        strtab = sections[sh[6]]
        for i in range(sh[5] // 16):
            name, value, size, info, _o, _shndx = struct.unpack_from("<IIIBBH", data, sh[4] + i * 16)
            if info & 0xF in (0, 2) and value and name:
                end = data.index(b"\0", strtab[4] + name)
                out.append((value, size, data[strtab[4] + name:end].decode("latin-1")))
    return sorted(out)


def symbolize(syms, addr):
    best = None
    for value, size, name in syms:
        if value <= addr and (best is None or value >= best[0]):
            best = (value, size, name)
    if best is None:
        return ""
    return f" {best[2]}+0x{addr - best[0]:X}" + ("" if addr - best[0] < max(best[1], 1) else "?")


def follow(d, count):
    """Print console text as it comes, until Ctrl-C. The UART runs at 115200
    baud (11.5 KB/s), so polling every 0.2 s stays well inside the 4 KB ring."""
    import time
    try:
        while True:
            text, count = d.console(count)
            sys.stdout.write(text.decode("latin-1").replace("\r", ""))
            sys.stdout.flush()
            time.sleep(0.2)
    except KeyboardInterrupt:
        pass


DDR2_CAL_ID = 0xDD2C0001            # JTAG 0x100050; 0x100051 CAL0, 0x100052 CAL1 (ddr2_ctrl.v)


def ddr2_report(d, quiet=False):
    """Print the DDR2 calibration of this boot; returns 0 when it ended ok, 1 otherwise."""
    ident = d.read(REG_BASE + 0x50)
    if ident != DDR2_CAL_ID:
        print(f"ID 0x{ident:08X} at 0x{REG_BASE + 0x50:06X}: no DDR2 calibration block "
              f"(a bitstream with the MIG, or not c64_basic); expected 0x{DDR2_CAL_ID:08X}")
        return 1
    c0, c1 = d.read(REG_BASE + 0x51), d.read(REG_BASE + 0x52)
    init, done, ok, up = (c0 >> 28) & 1, (c0 >> 29) & 1, (c0 >> 30) & 1, (c0 >> 31) & 1
    w0, w1 = c1 & 63, (c1 >> 8) & 63
    if not quiet:
        print(f"CAL0 0x{c0:08X}  CAL1 0x{c1:08X}")
    print(f"init {'done' if init else 'running'}, calibration "
          f"{'ok' if done and ok else 'FAILED (no window found)' if done else 'running'}, "
          f"controller {'up' if up else 'not up'}")
    if done and ok:
        print(f"bitslip position {(c0 >> 24) & 15}, read delay {(c0 >> 16) & 7}, "
              f"IDELAY taps lane 0 {c0 & 31}, lane 1 {(c0 >> 8) & 31} (78 ps each)")
        print(f"window width lane 0 {w0} taps ({w0 * 0.078:.2f} ns)"
              f"{', cut by the 32-tap scan range' if (c1 >> 16) & 1 else ''}, "
              f"lane 1 {w1} taps ({w1 * 0.078:.2f} ns)"
              f"{', cut by the 32-tap scan range' if (c1 >> 17) & 1 else ''}; "
              f"bitslip positions with a window for lane 0: {(c1 >> 20) & 15}")
    elif done:
        print(f"stopped after bitslip position {(c0 >> 24) & 15}; positions with a window for lane 0: "
              f"{(c1 >> 20) & 15}. The boot ROM waits for DDR2 for ever (console shows 'DDR2: ');"
              f" power-cycle to retry")
    return 0 if done and ok and up else 1


def main(argv):
    if not argv:
        print(__doc__)
        return 1
    cmd, args = argv[0], argv[1:]
    d = Dbg()
    try:
        if cmd == "regs":
            for n, name in enumerate(REG_NAMES):
                print(f"{name:10s} 0x{d.read(REG_BASE + n):08X}")
            if d.read(REG_BASE + 0x50) == DDR2_CAL_ID:
                c0, c1 = d.read(REG_BASE + 0x51), d.read(REG_BASE + 0x52)
                print(f"{'ddr2 cal':10s} 0x{c0:08X} 0x{c1:08X}  "
                      f"{'ok' if c0 >> 30 & 1 else 'FAILED' if c0 >> 29 & 1 else 'running'}, "
                      f"window {c1 & 63} / {(c1 >> 8) & 63} taps")
        elif cmd == "peek":
            addr = int(args[0], 16)
            length = int(args[1], 0) if len(args) > 1 else 16
            data = [d.read(addr + i) & 0xFF for i in range(length)]
            for row in range(0, length, 16):
                chunk = data[row:row + 16]
                print(f"{addr + row:04X}: " + " ".join(f"{b:02X}" for b in chunk))
        elif cmd == "poke":
            addr = int(args[0], 16)
            for i, v in enumerate(args[1:]):
                d.write(addr + i, int(v, 16))
        elif cmd == "ddrpeek":
            addr = int(args[0], 16)
            length = int(args[1], 0) if len(args) > 1 else 8
            words = [d.ddr_read(addr + i) for i in range(length)]
            for row in range(0, length, 8):
                print(f"{addr + row:07X}: " + " ".join(f"{w:04X}" for w in words[row:row + 8]))
        elif cmd == "ddrpoke":
            addr = int(args[0], 16)
            for i, v in enumerate(args[1:]):
                d.ddr_write(addr + i, int(v, 16))
        elif cmd == "ddrtest":
            count = int(args[0], 0) if args else 256
            step = (1 << 26) // count
            addrs = [i * step + (i % 8) for i in range(count)]
            val = lambda a: ((a * 2654435761) >> 7) & 0xFFFF
            for a in addrs:
                d.ddr_write(a, val(a))
            bad = [(a, d.ddr_read(a)) for a in addrs]
            bad = [(a, v) for a, v in bad if v != val(a)]
            print(f"{count} words over 128 MB: {len(bad)} wrong")
            for a, v in bad[:10]:
                print(f"  {a:07X}: {v:04X}, expected {val(a):04X}")
            return 1 if bad else 0
        elif cmd == "load":
            data = open(args[0], "rb").read()
            load = data[0] | data[1] << 8
            body = data[2:]
            import time
            t = time.time()
            d.write_bytes(load, body)
            dt = time.time() - t
            back = bytes(d.read(load + i) & 0xFF for i in range(min(64, len(body))))
            ok = back == body[:len(back)]
            end = load + len(body)
            print(f"{args[0]}: ${load:04X}-${end - 1:04X}, {len(body)} bytes in {dt:.2f} s"
                  f" ({len(body) / dt / 1024:.1f} KB/s), check {'ok' if ok else 'FAILED'}")
            if load == 0x0801:          # BASIC: fix the end-of-program pointers
                for zp in (0x2D, 0x2F, 0x31):
                    d.write(zp, end & 0xFF)
                    d.write(zp + 1, end >> 8)
            if len(args) > 1 and args[1] == "run":
                text = "RUN\r" if load == 0x0801 else f"SYS{load}\r"
                for i, ch in enumerate(text):
                    d.write(0x0277 + i, ord(ch))
                d.write(0x00C6, len(text))
            return 0 if ok else 1
        elif cmd == "console":
            text, count = d.console(0)
            sys.stdout.write(text.decode("latin-1").replace("\r", ""))
            sys.stdout.flush()
            if args and args[0] == "follow":
                follow(d, count)
        elif cmd == "dcache":
            # T133: host data cache debug (host/top.v 0x100060 control, 0x100061 selected counter; docs/todo/T133.md)
            import time
            sub = args[0] if args else "stat"
            if sub == "perf":
                clear = len(args) > 1 and args[1] == "clear"
                ctl = d.read(0x100060) & 0xF
                if clear:
                    d.write(0x100060, ctl | 0x10)
                    print("perf counters cleared (armed)")
                    return 0
                def word(n):
                    d.write(0x100060, ctl | n << 12)
                    time.sleep(0.02)
                    return d.read(0x100061)
                clk, ist, drd, dot, imiss, dmiss, isame, dsame, dhit = (word(n) for n in (5, 6, 7, 8, 9, 10, 11, 12, 13))
                d.write(0x100060, ctl)
                pc = lambda v: f"{100.0 * v / clk:6.2f} %" if clk else "     -"
                print(f"cpu clocks          {clk:12d}")
                print(f"icache stall clocks {ist:12d} {pc(ist)}")
                print(f"dcache read-miss st {drd:12d} {pc(drd)}   (cacheable reads waiting for DDR2)")
                print(f"other data stalls   {dot:12d} {pc(dot)}   (stores, I/O, uncached)")
                print(f"icache misses       {imiss:12d}   same 16 B line as the previous miss: {isame} ({100.0 * isame / imiss if imiss else 0:.1f} %)")
                print(f"dcache read misses  {dmiss:12d}   same 16 B line as the previous miss: {dsame} ({100.0 * dsame / dmiss if dmiss else 0:.1f} %)")
                print(f"dcache hits         {dhit:12d}")
                if imiss:
                    print(f"clocks per icache miss {ist / imiss:.1f}; per dcache read miss {drd / dmiss if dmiss else 0:.1f}")
                return 0
            ctl = d.read(0x100060) & 0xFFFF
            en = ctl & 0xF
            if sub == "off":
                d.write(0x100060, 0)
            elif sub == "on":                       # cache + snoop + ext walk
                d.write(0x100060, 0x7)
            elif sub == "check":                    # same, plus check mode (hits are verified against DDR2)
                d.write(0x100060, 0xF)
            elif sub == "mask":                     # dcache mask <bits>: raw bits 3:0 (bit0 cache, 1 snoop, 2 ext, 3 check)
                d.write(0x100060, int(args[1], 0) & 0xF)
            elif sub == "clear":
                d.write(0x100060, en | 0x10)
            elif sub != "stat":
                print("dcache [on|off|check|clear|stat|mask <bits>]")
                return 1
            if sub != "stat":
                time.sleep(0.05)
            ctl = d.read(0x100060) & 0xFFFF
            names = ["check mismatches", "mismatch addr", "mismatch cached", "mismatch ddr2", "status"]
            print(f"ctl 0x{ctl:04X}: cache {ctl & 1}, snoop {ctl >> 1 & 1}, ext {ctl >> 2 & 1}, check {ctl >> 3 & 1}")
            for n, name in enumerate(names):
                d.write(0x100060, (ctl & 0xF) | n << 12)     # bit 4 = 0: no clear
                time.sleep(0.02)
                v = d.read(0x100061)
                if n == 4:
                    print(f"{name:16s} 0x{v:08X}  (walk busy {v >> 4 & 1}, mismatch latched {v >> 5 & 1}, state {v >> 6 & 3})")
                else:
                    print(f"{name:16s} {v:10d}  0x{v:08X}")
            d.write(0x100060, ctl & 0x000F)
        elif cmd == "riscv":
            if args[0] in ("reset", "run"):
                d.riscv_reset(args[0] == "reset")
                return 0
            import time
            want_follow = args[-1] == "follow"
            if want_follow:
                args = args[:-1]
            data = open(args[0], "rb").read()
            addr = int(args[1], 0) if len(args) > 1 else 0x30000
            d.riscv_reset(True)
            t = time.time()
            d.ddr32_load(addr, data)
            dt = time.time() - t
            # spot checks: start, end and every 64 KB
            probes = sorted(set(list(range(0, len(data), 0x10000)) + [len(data) & ~3]))
            bad = 0
            for off in probes:
                if off >= len(data):
                    continue
                want = int.from_bytes(data[off:off + 4].ljust(4, b"\0"), "little")
                got = d.ddr32_read(addr + off)
                if got != want:
                    bad += 1
                    print(f"  {addr + off:07X}: {got:08X}, expected {want:08X}")
            print(f"{args[0]}: {len(data)} bytes at 0x{addr:X} in {dt:.1f} s "
                  f"({len(data) / dt / 1024:.1f} KB/s), {len(probes) - bad}/{len(probes)} probes ok")
            if bad:
                return 1
            d.ddr32_write(0xFFF8, addr)
            d.ddr32_write(0xFFFC, BOOT_MAGIC)
            count = d.read(CON_COUNT)
            d.riscv_reset(False)
            if want_follow:         # from the first byte on: the ring holds only 4 KB
                follow(d, count)
        elif cmd == "boot":
            sub = args[0] if args else "result"
            want_follow = args[-1] == "follow"
            if want_follow:
                args = args[:-1]
            if sub == "result":
                boot_result(d)
                return 0
            expect = None
            if sub == "check":
                count = boot_request(d, 0x101)
                if "--dump" in args:
                    dump = open(args[args.index("--dump") + 1], "rb").read()
                    hdr = [int.from_bytes(dump[FLASH_APPL + 4 * i:FLASH_APPL + 4 * i + 4], "little")
                           for i in range(3)]
                    expect = hdr + list(app_sums(dump[FLASH_APPL + 12:FLASH_APPL + 12 + hdr[1] + 3], hdr[1]))
            elif sub == "staged":
                image = app_image(args[1])
                addr = int(args[2], 0) if len(args) > 2 else 0x03000000
                length = int.from_bytes(image[4:8], "little")
                expect = [int.from_bytes(image[4 * i:4 * i + 4], "little") for i in range(3)] + \
                    list(app_sums(image[12:], length))
                count = boot_request(d, 2, addr, image)
            elif sub == "flash":
                count = boot_request(d, 1)
            else:
                print(__doc__)
                return 1
            r, count = boot_wait(d, count)
            boot_result(d)
            ok = r[0] == 0x54313552 and r[1] in (1, 2)
            if expect is not None:
                same = r[3:8] == expect
                print(f"header and sums {'equal' if same else 'DIFFER from'} the file's: "
                      + " ".join(f"{v:08X}" for v in expect))
                ok = ok and same
            if want_follow and r[1] == 1:
                follow(d, count)
            return 0 if ok else 1
        elif cmd == "ddrload":
            data = open(args[0], "rb").read()
            d.ddr32_load(int(args[1], 0), data)
        elif cmd == "ddrdump":
            addr = int(args[0], 0) & ~3
            length = int(args[1], 0) if len(args) > 1 else 64
            for row in range(0, length, 16):
                words = [d.ddr32_read(addr + row + i) for i in range(0, min(16, length - row), 4)]
                b = b"".join(w.to_bytes(4, "little") for w in words)
                print(f"{addr + row:07X}: " + " ".join(f"{x:02X}" for x in b))
        elif cmd == "mdio":
            phy, reg = int(args[0], 0), int(args[1], 0)
            if len(args) > 2:
                d.mdio_write(phy, reg, int(args[2], 0))
            else:
                print(f"PHY {phy} reg 0x{reg:02X} = 0x{d.mdio_read(phy, reg):04X}")
        elif cmd == "net":
            v = d.read_many([REG_BASE + n for n in (12, 13, 14, 15)])
            print(f"Ethernet frames in: {v[0] & 0xFFFF} ({v[0] >> 16} CRC ok), "
                  f"out: {v[1] & 0xFFFF} from the MAC, {v[1] >> 16} video frames")
            print(f"PHY carrier edges: {v[2] & 0xFFFF}, app DMA bytes into C64 RAM: {v[2] >> 16}, "
                  f"stream enable 0x{v[3] & 0xFF:02X}")
        elif cmd == "decouple":
            if args:
                d.write(0x100029, 1 if args[0] == "on" else 0)
            v = d.read(0x100029)
            print(f"core {'decoupled' if v & 2 else 'connected'} (effective); "
                  f"JTAG bit {v & 1}, app bit {1 if (v & 2) and not (v & 1) else 0 if not (v & 2) else '?'}")
        elif cmd == "pause":
            if args:
                d.machine_pause(args[0] == "on")
            ctl = d.read(MACHINE)
            print(f"machine pause {'requested' if ctl & MACHINE_PAUSE else 'off'}, "
                  f"{'paused' if ctl & MACHINE_PAUSED else 'running'}")
        elif cmd == "mode":
            st, st2, geo, fclk = (d.read(0x100030 + i) for i in range(4))
            names = ["PAL", "NTSC", "PAL-60", "NTSC-50"]
            tmo = (st >> 8) & 7
            print(f"mode {names[(st >> 14) & 3]} (requested {names[(st >> 16) & 3]}), "
                  f"switches {(st >> 22) & 0xFF}, state {(st >> 18) & 0xF}, "
                  f"synthesizers {'60' if st & (1 << 13) else '50'} Hz, sys {'on' if st & (1 << 12) else 'stopped'}")
            print(f"last switch {(st2 & 0xFFFF) * 1024 / 50e3:.1f} ms; timeouts "
                  f"{','.join(n for b, n in ((1, 'halt'), (2, 'buses'), (4, 'lock/I2C')) if tmo & b) or 'none'}")
            if st & (1 << 11):
                print(f"DRP read-back differed: register 0x{st & 0x7F:02X} = 0x{st2 >> 16:04X}")
            lines, lclk = (geo >> 12) & 0x1FF, geo & 0xFFF
            print(f"VIC: {lines} lines, {lclk} clocks per line ({lclk / 32:.2f} cycles), {fclk} clocks per frame")
            hm = d.read(0x100034)
            print(f"HDMI: GTP {'ready' if hm >> 31 & 1 else 'down'}, pixel MMCM {'locked' if hm >> 30 & 1 else 'unlocked'}, "
                  f"scaler {'locked' if hm >> 29 & 1 else 'unlocked'}, {(hm >> 16) & 0xFFF} frames/s, "
                  f"{(hm & 0xFFFF) * 256} pixel clocks per frame")
            rq = d.read(0x100035)
            print(f"HDMI request (A1200 large core; other builds read 0): last byte at 0x4F00 = {rq >> 24}, {(rq >> 16) & 0xFF} request(s) seen, "
                  f"vm_mode {(rq >> 12) & 15}, vm_p {(rq >> 8) & 15}, hdmi_timing runs row {(rq >> 4) & 15}")
        elif cmd == "trx":
            off = int(args[0], 16)
            if len(args) > 1:
                d.trx_write(off, int(args[1], 16))
            print(f"0x{0x101A0000 + off:08X}: 0x{d.trx_read(off):08X}")
        elif cmd == "sb":
            glue = d.sb_words(0, 1)[0]
            cpu = d.sb_words(1, 3)
            vic = d.sb_words(2, 12)
            cia = [d.sb_words(3, 5), d.sb_words(4, 5)]
            cart = d.sb_words(7, 1)[0]
            print(f"glue  CAP_GEN {glue >> 16}, halted {glue & 1}, GAME {glue >> 1 & 1} EXROM {glue >> 2 & 1}"
                  f" cartridge {glue >> 3 & 1}, CPU held {glue >> 4 & 1}"
                  + ("" if glue & 1 else "  (not halted: the words below are the last capture, not live; a CPU pause"
                     " alone does not refresh them, use 'freeze on' first)"))
            print(f"6510  PC {cpu[1] & 0xFFFF:04X} A {cpu[0] & 0xFF:02X} X {cpu[0] >> 8 & 0xFF:02X}"
                  f" Y {cpu[0] >> 16 & 0xFF:02X} S {cpu[0] >> 24:02X} P {cpu[1] >> 16 & 0xFF:02X}"
                  f" fetch {cpu[1] >> 24 & 1}  $00 {cpu[2] & 0xFF:02X} $01 {cpu[2] >> 8 & 0xFF:02X}"
                  f" (reads {cpu[2] >> 16 & 0xFF:02X})")
            vb = b"".join(w.to_bytes(4, "little") for w in vic)
            for r in range(0, 0x30, 16):
                print(f"VIC   $D0{r:02X}: " + " ".join(f"{b:02X}" for b in vb[r:r + 16]))
            for n in (0, 1):
                cb = b"".join(w.to_bytes(4, "little") for w in cia[n][:4])
                p = cia[n][4]
                print(f"CIA{n + 1}  $D{'C' if n == 0 else 'D'}00: " + " ".join(f"{b:02X}" for b in cb) +
                      f"   pins out PA {p & 0xFF:02X} PB {p >> 8 & 0xFF:02X} DDR {p >> 16 & 0xFF:02X}"
                      f" {p >> 24:02X}")
            print(f"cart  reg_0 {cart & 0xFF:02X} reg_1 {cart >> 8 & 0xFF:02X}")
        elif cmd == "turbo":
            if args and args[0] == "clear":
                d.trx_write(0x144, 1)
            st = d.trx_read(0x140)
            n = (st >> 16) & 0x7F
            print(f"TURBO_STAT 0x{st:08X}: engine {'ON' if st >> 30 & 1 else 'off'}, {n} CPU cycles per PHI2"
                  f" ({n * 0.985248:.2f} MHz), badline timing {'on' if st >> 29 & 1 else 'off'},"
                  f" index {st & 15}, enable word {st >> 8 & 7} $D030 bit {st >> 7 & 1},"
                  f" hold {'running' if st >> 31 & 1 else 'over'}, ran since cleared {st >> 28 & 1},"
                  f" stops force 1 MHz {st >> 27 & 1}")
            w = [0, d.trx_read(0x148), d.trx_read(0x14C)]
            print(f"state words: TURBO_W1 0x{w[1]:08X} (prefer {w[1] & 0xFF:02X}, enable {w[1] >> 8 & 7},"
                  f" $D030 {w[1] >> 11 & 1}, $D031 {w[1] >> 24:02X}), TURBO_W2 hold {w[2] & 0x7FFFFF}")
        elif cmd == "rvpc":
            import time
            elf = [a for a in args if a != "clear"]
            syms = elf_symbols(elf[0]) if elf else []
            if "clear" in args:
                d.write(0x100041, 1)
            snaps = []
            for _ in range(2):
                d.write(0x100040, 1)
                time.sleep(0.05)
                snaps.append(d.read_many([0x100040 + i for i in range(8)]))
                time.sleep(0.2)
            for n, w in enumerate(snaps):
                tag = (w[1] >> 4) & 0xFF
                memaddr = ((w[2] & 0x3F) << 20) | (w[1] >> 12)
                ioaddr = (w[2] >> 6) & 0xFFFFFF
                print(f"[{n}] PC 0x{w[0]:08X}{symbolize(syms, w[0])}  still for {w[4]} clocks "
                      f"({w[4] / 100e6 * 1000:.1f} ms)  IRQ edges {w[3] & 0xFFFFFF}  IRQ line {w[1] >> 3 & 1}")
                print(f"    waits: I/O {'yes' if w[1] & 1 else 'no'} (last access 0x{0x10000000 + ioaddr:08X}, "
                      f"{'write' if w[2] >> 30 & 1 else 'read'}), memory request "
                      f"{'pending' if w[1] >> 1 & 1 else 'none'} (0x{memaddr:07X}, tag 0x{tag:02X}, "
                      f"{'read' if w[1] >> 2 & 1 else 'write'})")
            w = snaps[-1]
            ctx = w[7]
            print(f"longest stall since reset/clear: {w[5]} clocks ({w[5] / 100e6 * 1000:.1f} ms) at "
                  f"PC 0x{w[6]:08X}{symbolize(syms, w[6])}; then I/O "
                  f"{'busy' if ctx >> 24 & 1 else 'idle'} 0x{0x10000000 + (ctx & 0xFFFFFF):08X} "
                  f"{'write' if ctx >> 27 & 1 else 'read'}, memory {'pending' if ctx >> 25 & 1 else 'none'}")
            if w[3] >> 24:
                print(f"app DMA bytes the C64 side never answered (given up after 84 ms): {w[3] >> 24}")
            if snaps[1][4] > snaps[0][4] > 0 and snaps[1][0] == snaps[0][0]:
                print("the PC has not moved between the snapshots: hung here")
        elif cmd == "freeze":
            if args:
                d.trx_write(GATE_CTRL, 1 if args[0] == "on" else 0)
            g = d.trx_read(GATE_CTRL)
            st = d.trx_read(SH_STATUS)
            print(f"app's C64_STOP: {'machine pause' if g & 1 else 'CPU pause only'};"
                  f" machine {'paused' if g & 0x100 else 'running'}")
            ctr = [st, d.trx_read(SH_ACCESSES), d.trx_read(SH_REAL), d.trx_read(SH_GCLK), d.trx_read(SH_UNMAPPED)]
            if any(ctr):                    # cores before SLIM: the freeze session's counters
                print(f"sessions {st >> 16}{' (one open)' if st & 1 else ''}, accesses {ctr[1]},"
                      f" real bus cycles {ctr[2]}, C64 clock edges {ctr[3]}, unmapped {ctr[4]}")
            else:
                print("no freeze session on this core (slim: the app's accesses are bus cycles)")
        elif cmd == "ddr2":
            return ddr2_report(d)
        elif cmd == "screen":
            data = [d.read(0x0400 + i) & 0xFF for i in range(1000)]
            for row in range(25):
                print("".join(screen_char(c) for c in data[row * 40:row * 40 + 40]).rstrip())
        else:
            print(__doc__)
            return 1
    finally:
        d.close()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
