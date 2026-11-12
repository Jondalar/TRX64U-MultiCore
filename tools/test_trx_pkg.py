#!/usr/bin/env python3
"""Host test for tools/trx_pkg.py and tools/trx_pkg_check.py: a normal package and a restore bundle
(T49, ADR-0050) built from a synthetic flash, the way update_trx.cc backup_cfw builds it on the board
(trx_pkg.restore_blobs is its host twin), then damaged in the ways the checker has to catch.

  python3 tools/test_trx_pkg.py
"""
import os
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trx_pkg as P  # noqa: E402
import trx_pkg_check as C  # noqa: E402

PKG_REAL = os.environ.get("TRX_REAL_PKG", "/path/to/.claude/jobs/259f244b/tmp/upd/trx64-integ-m1-49847451/trx64_update.cfw")
CODE = bytes((i * 7) & 0xFF for i in range(0x1000))       # stands in for the updater: it is only copied


def bit_file(axss=None, body_words=2000):
    """A .bit file: the header fields a-e, then sync, optionally the USR_ACCESS write, then filler."""
    hdr = bytes([0x00, 0x09, 0x0F, 0xF0, 0x0F, 0xF0, 0x0F, 0xF0, 0x0F, 0xF0, 0x00, 0x00, 0x01])
    for key in b"abcd":
        txt = b"x" * 5 + b"\0"
        hdr += bytes([key]) + struct.pack(">H", len(txt)) + txt
    data = struct.pack(">I", 0xFFFFFFFF) + struct.pack(">I", P.SYNC_WORD)
    if axss is not None:
        data += struct.pack(">I", 0x30000000 | (2 << 27) | (P.REG_AXSS << 13) | 1) + struct.pack(">I", axss)
    data += struct.pack(">I", 0x30000000 | (2 << 27) | (P.REG_CMD << 13) | 1) + struct.pack(">I", P.CMD_DESYNC)
    data += bytes((i * 31) & 0xFF for i in range(body_words * 4))
    return hdr + b"e" + struct.pack(">I", len(data)) + data


def raw_image(axss=None, body_words=2000):
    """A raw 7-series image as openFPGALoader writes it: FF padding, bus-width words, sync, config packets, no .bit header."""
    data = b"\xFF" * 16 + bytes([0, 0, 0, 0xBB, 0x11, 0x22, 0, 0x44]) + b"\xFF" * 8 + struct.pack(">I", P.SYNC_WORD)
    if axss is not None:
        data += struct.pack(">I", 0x30000000 | (2 << 27) | (P.REG_AXSS << 13) | 1) + struct.pack(">I", axss)
    data += struct.pack(">I", 0x30000000 | (2 << 27) | (P.REG_CMD << 13) | 1) + struct.pack(">I", P.CMD_DESYNC)
    return data + bytes(((i * 31) & 0xFF) | 1 for i in range(body_words * 4)) + struct.pack(">I", 0x20000000) * 4


def app_file(dlen=0x20000):
    data = bytes((i * 13) & 0xFF for i in range(dlen))
    return struct.pack("<III", 0x30000, dlen, 0x30000) + data


def package(blobs, pre=True):
    rec = struct.pack("<III", P.RECORD_LOAD, len(CODE) + len(blobs), P.RECORD_LOAD) + CODE + blobs
    return (P.PRE_FINGERPRINT + b"U64MK2" + b"\0" * 26 + rec) if pre else rec


def softboot_scan(pkg, flash_bit):
    """apps/softboot/src/softboot.c: find_probe (the flash's first 4096 bytes, 4-aligned), then find_app
    (the app record within 16 bytes behind the bitstream the flash's .bit header describes).
    Returns (bitstream offset, app offset) or None."""
    probe = flash_bit[:4096]
    blen = P.bit_length_from_header(flash_bit[:512])
    at = next((i for i in range(0, len(pkg) - 4096, 4) if pkg[i:i + 4096] == probe), -1)
    if at < 0 or not blen:
        return None
    end = at + blen
    for a in range((end + 3) & ~3, end + 17, 4):
        load, ln, start = struct.unpack_from("<III", pkg, a)
        if load == 0x30000 and 0x10000 <= ln <= 0xA00000 and load <= start < load + ln and a + 12 + ln <= len(pkg):
            return at, a
    return None


def fake_flash(bit, app):
    f = bytearray(b"\xFF" * 0x1000000)
    f[:len(bit)] = bit
    f[P.FLASH_APPL:P.FLASH_APPL + len(app)] = app
    f[P.CFG_ADDR:P.CFG_ADDR + P.CFG_LEN] = bytes((i * 3 + 1) & 0xFF for i in range(P.CFG_LEN))
    return bytes(f)


class T(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.TemporaryDirectory()

    def tearDown(self):
        self.d.cleanup()

    def check(self, data, name="x.cfw", **kw):
        p = os.path.join(self.d.name, name)
        open(p, "wb").write(data)
        return C.check_file(p, kw.get("bit"), kw.get("app"), kw.get("code"))

    def test_normal_package(self):
        bit, app = bit_file(0x12345678), app_file()
        pkg = package(P.build_blobs(bit, app, "main-abcd1234", "b" * 15, "a" * 15))
        errs, _ = self.check(pkg, bit=bit, app=app)
        self.assertEqual(errs, [])
        errs, _ = self.check(package(P.build_blobs(bit, app, "main-abcd1234", "b", "a"), pre=False), "x.ue2")
        self.assertEqual(errs, [])

    def test_softboot_still_finds_bitstream_and_app(self):
        bit, app = bit_file(0x12345678), app_file()
        rec = package(P.build_blobs(bit, app, "main-abcd1234", "b", "a"), pre=False)
        found = softboot_scan(rec[12:], bit)
        self.assertIsNotNone(found)
        self.assertEqual(rec[12:][found[1]:found[1] + len(app)], app)
        flash = fake_flash(bit_file(None), app)          # a restore bundle of the flash is softbootable too
        rec = package(P.restore_blobs(flash, "TRXBK000"), pre=False)
        self.assertIsNotNone(softboot_scan(rec[12:], flash[:P.bit_length_from_header(flash[:512])]))

    def test_normal_package_needs_axss(self):
        with self.assertRaises(ValueError):
            P.build_blobs(bit_file(None), app_file(), "id", "b", "a")

    def test_restore_bundle_of_a_stock_bitstream(self):
        bit, app = bit_file(None), app_file(0x30004)
        flash = fake_flash(bit, app)
        blobs = P.restore_blobs(flash, "TRXBK003")
        self.assertIsNotNone(blobs)
        errs, notes = self.check(package(blobs), code=CODE)
        self.assertEqual(errs, [])
        self.assertTrue(any("restore bundle" in n for n in notes))
        v = P.parse_manifest(blobs[:P.MANIFEST_LEN])
        self.assertEqual((v["flags"], v["bit_axss"], v["cfg_len"], v["id"]), (P.FLAG_RESTORE, 0, P.CFG_LEN, "TRXBK003"))
        end = P.layout(v["bit_len"], v["app_len"], v["cfg_len"])[3]
        self.assertEqual(len(blobs), end)
        self.assertEqual(blobs[v["bit_off"]:v["bit_off"] + v["bit_len"]], bit)
        self.assertEqual(blobs[v["app_off"]:v["app_off"] + v["app_len"]], app)
        self.assertEqual(blobs[v["cfg_off"]:], flash[P.CFG_ADDR:])

    def test_restore_bundle_of_a_stamped_bitstream(self):
        bit, app = bit_file(0xCAFE0001, 777), app_file()
        blobs = P.restore_blobs(fake_flash(bit, app), "TRXBK000")
        self.assertEqual(P.parse_manifest(blobs[:P.MANIFEST_LEN])["bit_axss"], 0xCAFE0001)
        self.assertEqual(self.check(package(blobs))[0], [])

    def test_restore_bundle_of_a_raw_bitstream(self):
        for ax in (None, 0xCAFE0002):
            raw, app = raw_image(ax), app_file(0x30004)
            self.assertEqual(P.bit_length_from_header(raw[:512]), 0)
            self.assertTrue(P.raw_detect(raw[:256]))
            flash = fake_flash(raw, app)
            blobs = P.restore_blobs(flash, "TRXBK004")
            self.assertIsNotNone(blobs)
            errs, notes = self.check(package(blobs), code=CODE)
            self.assertEqual(errs, [])
            v = P.parse_manifest(blobs[:P.MANIFEST_LEN])
            self.assertEqual(v["flags"], P.FLAG_RESTORE | P.FLAG_RAWBIT)
            self.assertEqual(v["bit_axss"], ax or 0)
            bit = blobs[v["bit_off"]:v["bit_off"] + v["bit_len"]]
            self.assertEqual(P.bit_length_from_header(bit), len(bit))          # the existing manifest check accepts it
            self.assertEqual(bit[P.RAW_HDR:], raw)                              # exactly the bytes the flash held
            self.assertEqual(len(raw) % 4, 0)

    def test_raw_length_is_the_last_non_ff_byte(self):
        raw = raw_image(None)
        slot = raw + b"\xFF" * (P.BIT_MAX - len(raw))
        self.assertEqual(P.raw_length(slot), len(raw))
        slot = bytearray(slot)
        slot[len(raw) + 100] = 0x12                                             # a stale tail byte is kept
        self.assertEqual(P.raw_length(bytes(slot)), len(raw) + 104)
        self.assertEqual(P.raw_length(b"\xFF" * 64), 0)

    def test_raw_detection_needs_both_markers(self):
        raw = raw_image(None)
        self.assertFalse(P.raw_detect(b"\xFF" * 256))
        self.assertFalse(P.raw_detect(raw[:24]))                                # bus width but no sync inside
        self.assertFalse(P.raw_detect(raw[:16] + raw[24:]))                     # sync but no bus width
        self.assertEqual(P.restore_blobs(fake_flash(b"\xFF" * 100, app_file()), "TRXBK000"), None)

    def test_raw_flag_only_with_a_matching_header(self):
        raw = raw_image(5)
        with self.assertRaises(ValueError):
            P.build_blobs(P.raw_header(len(raw)) + raw, app_file(), "id", "b", "a", flags=P.FLAG_RAWBIT)         # not restore
        with self.assertRaises(ValueError):
            P.build_blobs(P.raw_header(len(raw) + 4) + raw, app_file(), "id", "b", "a", flags=P.FLAG_RESTORE | P.FLAG_RAWBIT)

    @unittest.skipUnless(os.path.exists(PKG_REAL), "package integ-m1-49847451 not on this machine")
    def test_raw_bundle_of_the_real_bitstream(self):
        """The .bit of the real package with its header stripped, as openFPGALoader leaves it in the flash."""
        d = open(PKG_REAL, "rb").read()
        img = d[(36 if d[:4] == P.PRE_FINGERPRINT else 0) + 12:]
        m_off, v, why = C.find_manifest(img)
        self.assertGreater(m_off, 0, why)
        bit = img[m_off + v["bit_off"]:m_off + v["bit_off"] + v["bit_len"]]
        p = 13
        for _ in b"abcd":
            p += 3 + ((bit[p + 1] << 8) | bit[p + 2])
        raw = bit[p + 5:]
        self.assertEqual(len(raw), struct.unpack_from(">I", bit, p + 1)[0])
        self.assertTrue(P.raw_detect(raw[:256]))
        slot = raw + b"\xFF" * (P.BIT_MAX - len(raw))
        self.assertEqual(P.raw_length(slot), len(raw))
        flash = fake_flash(slot, app_file(0x30004))
        blobs = P.restore_blobs(flash, "TRXBK001")
        self.assertEqual(self.check(package(blobs))[0], [])
        w = P.parse_manifest(blobs[:P.MANIFEST_LEN])
        self.assertEqual(blobs[w["bit_off"] + P.RAW_HDR:w["bit_off"] + w["bit_len"]], raw)
        self.assertEqual(w["bit_axss"], v["bit_axss"])

    def test_nothing_to_restore(self):
        flash = bytearray(fake_flash(bit_file(1), app_file()))
        flash[P.FLASH_APPL:P.FLASH_APPL + 12] = b"\xFF" * 12
        self.assertIsNone(P.restore_blobs(bytes(flash), "TRXBK000"))
        self.assertIsNone(P.restore_blobs(b"\xFF" * 0x1000000, "TRXBK000"))

    def test_damage_is_caught(self):
        bit, app = bit_file(None), app_file()
        blobs = bytearray(P.restore_blobs(fake_flash(bit, app), "TRXBK000"))
        v = P.parse_manifest(bytes(blobs[:P.MANIFEST_LEN]))
        for what, off in (("config", v["cfg_off"] + 100), ("app", v["app_off"] + 500), ("bitstream", v["bit_off"] + 300)):
            bad = bytearray(blobs)
            bad[off] ^= 1
            self.assertNotEqual(self.check(package(bytes(bad)))[0], [], what)
        self.assertNotEqual(self.check(package(bytes(blobs[:-4])))[0], [], "truncated")
        self.assertNotEqual(self.check(package(bytes(blobs) + b"\0\0\0\0"))[0], [], "trailing bytes")
        other = bytearray(CODE)
        other[5] ^= 1
        self.assertNotEqual(self.check(package(bytes(blobs)), code=bytes(other))[0], [], "different updater code")

    def test_config_only_in_a_restore_bundle(self):
        bit, app = bit_file(7), app_file()
        with self.assertRaises(ValueError):
            P.build_blobs(bit, app, "id", "b", "a", cfg=b"\0" * P.CFG_LEN)
        with self.assertRaises(ValueError):
            P.build_blobs(bit, app, "id", "b", "a", cfg=b"\0" * 100, flags=P.FLAG_RESTORE)

    def test_a_full_sha_does_not_fit_a_commit_field(self):
        # The manifest keeps 16 bytes NUL-terminated per commit (FIELDS), so a caller has to shorten.
        # ci/lib.sh build_update_host passed the whole sha once and the update stage of run 51 died on it.
        bit, app = bit_file(7), app_file()
        full = "696d5cdd20d7304764d8cc13304c4c983130b89f"
        with self.assertRaises(ValueError):
            P.build_blobs(bit, app, "id", full, "a")
        with self.assertRaises(ValueError):
            P.build_blobs(bit, app, "id", "b", full)
        P.build_blobs(bit, app, "id", full[:8], full[:8])           # eight digits, as every caller sends
        P.build_blobs(bit, app, "id", "f" * 15, "f" * 15)           # the last length that still fits

    def test_flags_other_than_restore_are_refused(self):
        blobs = P.build_blobs(bit_file(7), app_file(), "id", "b", "a", flags=P.FLAG_CONFIG_RESET)
        self.assertNotEqual(self.check(package(blobs))[0], [])


if __name__ == "__main__":
    unittest.main()
