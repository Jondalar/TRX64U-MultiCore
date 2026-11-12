#!/usr/bin/env python3
"""Tests of tools/ef3_image.py on synthetic images (python3 tools/test_ef3_image.py)."""
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ef3_image as E

EAPI_SRC = os.environ.get("EF3_EAPI_SRC")


def pat(bank, half):
    return bytes((bank * 3 + half * 7 + i * 5) & 0xFF for i in range(E.CHIP))


def make_ef(path, banks=64, eapi=b"Am/M29F040 V1.4"):
    img = bytearray(b"\xFF" * (banks * 0x4000))
    for b in range(banks):
        for h in range(2):
            img[b * 0x4000 + h * E.CHIP:b * 0x4000 + (h + 1) * E.CHIP] = pat(b, h)
    o = E.CHIP + E.EAPI_OFFSET
    img[o:o + 4] = E.EAPI_SIG
    img[o + 4:o + 4 + len(eapi)] = eapi
    E.write_crt(path, 32, "TESTGAME", 1, 0, img)
    return img


class Ef3Image(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.mkdtemp()
        self.p = lambda n: os.path.join(self.d, n)

    def system(self):
        img = bytearray(b"\xFF" * (E.BANKS_PER_SLOT * 0x4000))
        for b in range(8):
            img[b * 0x4000:b * 0x4000 + E.CHIP] = pat(100 + b, 0)
        o = E.DIR_BANK * 0x4000
        img[o:o + 16] = E.DIR_SIG   # PETSCII, as the real directory.s has it
        for i in range(16):
            img[o + 16 + 16 * i:o + 32 + 16 * i] = E.petscii("Slot %d" % i).ljust(16, b"\0")
        E.write_crt(self.p("sys.crt"), 32, "SYSTEM", 1, 0, img)
        return img

    def test_new_and_layout(self):
        sysimg = self.system()
        E.main(["new", "-o", self.p("ef3.crt"), "--system", self.p("sys.crt")])
        raw = open(self.p("ef3.crt"), "rb").read()
        self.assertEqual(len(raw), 0x40 + 1024 * (16 + E.CHIP))
        hw, hdr, chips = E.read_crt(self.p("ef3.crt"))
        self.assertEqual(hw, 90)
        self.assertEqual((hdr[0x18], hdr[0x19]), (1, 0))
        banks = sorted({b for (b, l) in chips})
        self.assertEqual(banks, list(range(512)))
        # slot-major: the packets come in bank order, ROML then ROMH
        order = []
        pos = 0x40
        while pos < len(raw):
            _, _, bank, load, _ = E.struct.unpack(">IHHHH", raw[pos + 4:pos + 16])
            order.append((bank, load))
            pos += 16 + E.CHIP
        self.assertEqual(order, [(b, l) for b in range(512) for l in (0x8000, 0xA000)])
        img = E.load_ef3(self.p("ef3.crt"))
        self.assertEqual(bytes(img[:len(sysimg)]), bytes(sysimg))
        self.assertTrue(all(x == 0xFF for x in img[E.BANKS_PER_SLOT * 0x4000:]))
        self.assertEqual(E.slot_names(img)[3], "Slot 3")

    @unittest.skipUnless(EAPI_SRC or any(os.path.exists(os.path.join(os.path.dirname(os.path.abspath(__file__)), r)) for r in (
        "../../TRX64-Ultimate-app-wt-t51/apps/trex_crt/src/eapi_drivers.c", "../vendor/gideon/apps/trex_crt/src/eapi_drivers.c")), "the app's eapi_drivers.c is not next to this repo")
    def test_insert_extract(self):
        self.system()
        E.main(["new", "-o", self.p("ef3.crt"), "--system", self.p("sys.crt")])
        ef = make_ef(self.p("game.crt"))
        E.main(["insert", self.p("ef3.crt"), "4", self.p("game.crt"), "-o", self.p("out.crt"), "--name", "Timo's Castle"] +
               (["--eapi-src", EAPI_SRC] if EAPI_SRC else []))
        img = E.load_ef3(self.p("out.crt"))
        base = 4 * E.BANKS_PER_SLOT * 0x4000
        o = E.CHIP + E.EAPI_OFFSET
        self.assertEqual(bytes(img[base + o:base + o + E.EAPI_SIZE]), E.eapi_driver(EAPI_SRC))      # the EF3 driver, not the Am29F040's
        self.assertEqual(bytes(img[base + 0x4000 * 7:base + 0x4000 * 7 + E.CHIP]), pat(7, 0))
        self.assertEqual(E.slot_names(img)[4], "Timo's Castle")
        E.main(["extract", self.p("out.crt"), "4", self.p("back.crt")])
        hw, hdr, chips = E.read_crt(self.p("back.crt"))
        self.assertEqual(hw, 32)
        self.assertEqual(len(chips), 128)
        self.assertEqual(chips[(7, 0x8000)], pat(7, 0))
        self.assertEqual(chips[(0, 0xA000)][o - E.CHIP:o - E.CHIP + E.EAPI_SIZE], E.eapi_driver(EAPI_SRC))

    def test_chip_erase_recovery(self):
        """a chip erase leaves 8 MiB of $FF; the base image written back to slot 0 brings the system area back
        and keeps what else is in the image"""
        sysimg = self.system()
        E.main(["new", "-o", self.p("ef3.crt"), "--system", self.p("sys.crt")])
        full = E.load_ef3(self.p("ef3.crt"))
        full[5 * E.BANKS_PER_SLOT * 0x4000:5 * E.BANKS_PER_SLOT * 0x4000 + 0x4000] = bytes(range(256)) * 64   # a game in slot 5
        E.write_crt(self.p("full.crt"), 90, "EASYFLASH 3", 1, 0, full)
        E.write_crt(self.p("erased.crt"), 90, "EASYFLASH 3", 1, 0, bytearray(b"\xFF" * len(full)))
        self.assertEqual(E.main(["cmp", self.p("full.crt"), self.p("erased.crt")]), 1)
        E.main(["system", self.p("erased.crt"), self.p("sys.crt"), "-o", self.p("back.crt")])
        back = E.load_ef3(self.p("back.crt"))
        self.assertEqual(bytes(back[:len(sysimg)]), bytes(sysimg))
        self.assertTrue(all(x == 0xFF for x in back[E.BANKS_PER_SLOT * 0x4000:]))
        self.assertEqual(E.slot_names(back)[2], "Slot 2")
        # a type 90 as the base image: only its slot 0 counts; slots 1-7 of the target stay
        E.main(["system", self.p("full.crt"), self.p("back.crt"), "-o", self.p("again.crt")])
        self.assertEqual(E.main(["cmp", self.p("full.crt"), self.p("again.crt")]), 0)

    def test_real_directory(self):
        # the first bytes of EF3BootImage's directory.bin (acme !pet)
        self.assertEqual(E.DIR_SIG[:6].hex(), "c5c62dc44952")
        self.assertEqual(E.petscii("System Area").hex(), "d3595354454d20c1524541")

    def test_refuses(self):
        make_ef(self.p("game.crt"))
        with self.assertRaises(SystemExit):
            E.main(["info", self.p("game.crt")])   # type 32 is not an image


if __name__ == "__main__":
    unittest.main()
