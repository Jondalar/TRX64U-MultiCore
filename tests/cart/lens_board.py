#!/usr/bin/env python3
"""Memory lenses on the board (T21 §3.4.1): REST machine:readmem with lens=cpu|cart|io|rom
on an emulated cartridge, running and with the freeze menu up, against the bytes the
simulation's test CRTs hold (host/sim/cart/carts.py: every ROM chip is filled
with P(bank, half, offset), one value per 256-byte page).

    carts.py --emit DIR easyflash easyflash_xl     (once, in host/sim/cart)
    lens_board.py DIR [name ...]                   (default: easyflash easyflash_xl)

Per cartridge: the CRT is started with PUT /v1/runners:run_crt (carts_board.run), then the
EasyFlash registers are set by REST writemem (mode 16K, a bank), and
  - lens=cart $8000/$A000/$E000 equal P(bank, half, i)
  - lens=cpu $8000/$A000 equal the cartridge (16K mode, $01 = $37), $E000 equals lens=rom
  - lens=cpu equals the default readmem (Gideon's DMA bus read) where the PLA maps the cartridge
  - the default readmem without lens is unchanged ($8000 shows the cartridge in 16K mode)
and all of it again with the freeze menu open (PUT /v1/machine:menu_button). TRX64_HOST as in
carts_board.py. Needs no JTAG.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import carts_board as cb  # noqa: E402

HOST = cb.HOST


def P(bank, half, off):
    return (bank * 2 + half + (off >> 8) * 37 + 0x11) & 0xFF


def exp(bank, half, n=0x200):
    return bytes(P(bank, half, o) for o in range(n))


def rd(addr, n, lens=None):
    q = "/v1/machine:readmem?address=%04X&length=%d" % (addr, n)
    if lens:
        q += "&lens=" + lens
    return cb.http("GET", q)


def wr(addr, v):
    cb.http("PUT", "/v1/machine:writemem?address=%04X&data=%02X" % (addr, v))


fails = 0


def check(what, got, want):
    global fails
    ok = got == want
    if not ok:
        fails += 1
    print("  %-46s %s%s" % (what, "ok" if ok else "FAILED", "" if ok else "\n    got  %s\n    want %s" % (got[:24].hex(), want[:24].hex())))


def checks(tag, bank):
    n = 0x200
    check(tag + "lens=cart $8000 (ROML)", rd(0x8000, n, "cart"), exp(bank, 0, n))
    check(tag + "lens=cart $A000 (ROMH)", rd(0xA000, n, "cart"), exp(bank, 1, n))
    check(tag + "lens=cart $E000 (ROMH)", rd(0xE000, n, "cart"), exp(bank, 1, n))
    check(tag + "lens=cpu $8000 (16K: ROML)", rd(0x8000, n, "cpu"), exp(bank, 0, n))
    check(tag + "lens=cpu $A000 (16K: ROMH)", rd(0xA000, n, "cpu"), exp(bank, 1, n))
    check(tag + "lens=cpu $E000 = lens=rom (KERNAL)", rd(0xE000, n, "cpu"), rd(0xE000, n, "rom"))
    # the default read is a bus cycle: a cartridge line not cached yet reads stale the first time
    # (cart_slot.v holds only the CPU), so it is read twice and the second answer is compared
    rd(0x8000, n)
    check(tag + "lens=cpu $8000 = default readmem (2nd)", rd(0x8000, n, "cpu"), rd(0x8000, n))
    rd(0xA000, n)
    check(tag + "lens=cpu $A000 = default readmem (2nd)", rd(0xA000, n, "cpu"), rd(0xA000, n))
    check(tag + "default readmem $8000 unchanged", rd(0x8000, n), exp(bank, 0, n))


def one(d, name, bank):
    print("%s, bank %d" % (name, bank))
    if not cb.run(d, name):
        global fails
        fails += 1
        return
    wr(0xDE02, 0x07)                    # 16K, $01 is $37 under BASIC
    wr(0xDE00, bank & 0xFF)
    time.sleep(0.2)
    checks("running: ", bank)
    cb.http("PUT", "/v1/machine:menu_button")
    time.sleep(2)
    try:
        checks("frozen:  ", bank)
    finally:
        cb.http("PUT", "/v1/machine:menu_button")
        time.sleep(1)
    wr(0xDE00, 0)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    d = sys.argv[1]
    names = sys.argv[2:] or ["easyflash", "easyflash_xl"]
    banks = {"easyflash": 2, "easyflash_xl": 100}
    for n in names:
        one(d, n, banks.get(n, 1))
    print("all passed" if not fails else "%d failed" % fails)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
