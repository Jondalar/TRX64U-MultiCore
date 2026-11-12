#!/usr/bin/env python3
"""Host test for tools/cor_pack.py (T84 S3): round trip, corrupt hash refused, wrong contract refused,
wrong host key refused (T84 D5b).

  python3 tools/test_cor_pack.py        (also collected by python3 -m pytest tools/test_cor_pack.py)
"""
import os
import hashlib
import json
import struct
import sys
import subprocess
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cor_pack as C  # noqa: E402


def bit_file(words=300):
    hdr = C.BIT_MAGIC
    for key in b"abcd":
        txt = b"x" * 5 + b"\0"
        hdr += bytes([key]) + struct.pack(">H", len(txt)) + txt
    data = b"\xFF" * 8 + C.SYNC + bytes(range(256)) * (words // 64)
    data = data[: len(data) // 4 * 4]
    return hdr + b"e" + struct.pack(">I", len(data)) + data, data


DFX_GOLDEN = "e090bd832bf27cbabec0efcaece369932ae5a081d16f58ad91efa1dc48d5c2f8"
APP = bytes((i * 5) & 0xFF for i in range(4096))
ICON = bytes(2048)


def make(**kw):
    bit, data = bit_file()
    args = dict(name="VIC-20", version="0.1", partial=bit, app=APP, load=0x30000, entry=0x30000,
                host_key="820eded2")
    args.update(kw)
    return C.pack(**args), data


class CorTest(unittest.TestCase):
    def test_round_trip(self):
        blob, data = make(icon=ICON)
        m, r = C.parse(blob, C.CONTRACT)
        self.assertEqual(r[C.T_PARTIAL], data)       # .bit header stripped
        self.assertEqual(r[C.T_APP], APP)
        self.assertEqual(r[C.T_ICON], ICON)
        self.assertEqual((m["name"], m["contract"], m["app_load"]), ("VIC-20", C.CONTRACT, 0x30000))

    def test_raw_partial_accepted(self):
        _, data = bit_file()
        blob = C.pack("x", "1", data, APP, 0x30000, 0x30010, "820eded2")
        self.assertEqual(C.parse(blob)[1][C.T_PARTIAL], data)

    def test_corrupt_part_refused(self):
        blob, _ = make()
        for pos in (len(blob) - 1, len(blob) // 2):    # app / partial
            bad = bytearray(blob)
            bad[pos] ^= 1
            with self.assertRaises(C.CorError):
                C.parse(bytes(bad))

    def test_truncated_and_magic(self):
        blob, _ = make()
        with self.assertRaises(C.CorError):
            C.parse(blob[:-5])
        with self.assertRaises(C.CorError):
            C.parse(b"XXXXXXXX" + blob[8:])

    def test_wrong_contract_refused(self):
        blob, _ = make(contract=3)
        C.parse(blob)                                  # intact
        with self.assertRaises(C.CorError):
            C.parse(blob, C.CONTRACT)

    def test_wrong_host_key_refused(self):
        blob, _ = make()
        self.assertEqual(C.parse(blob)[0]["host_key"], "820eded2")
        C.parse(blob, C.CONTRACT, "820eded2")                      # the host it was built for
        C.parse(blob, C.CONTRACT, "0x820EDED2")                    # same number, written differently
        with self.assertRaises(C.CorError):
            C.parse(blob, C.CONTRACT, "9ae17385")                  # another host
        with self.assertRaises(C.CorError):
            make(host_key="820eded")                               # 7 digits
        with self.assertRaises(C.CorError):
            make(host_key="820eded2z")
        self.assertEqual(C.norm_host_key(0x820EDED2), "820eded2")

    def test_manifest_without_host_key_refused(self):
        # a bundle of the old packer, before T84 D5b: no grandfathering
        blob, _ = make()
        m, recs = C.parse(blob)
        del m["host_key"]
        man = json.dumps(m, sort_keys=True, separators=(",", ":")).encode()
        body = C.rec(C.T_MANIFEST, man) + C.rec(C.T_PARTIAL, recs[C.T_PARTIAL]) \
            + C.rec(C.T_APP, recs[C.T_APP])
        old = C.MAGIC + struct.pack("<I", len(body)) + body
        with self.assertRaises(C.CorError):
            C.parse(old)

    def test_partial_must_have_sync(self):
        with self.assertRaises(C.CorError):
            C.pack("x", "1", bytes(400), APP, 0x30000, 0x30000, "820eded2")

    def test_entry_outside_refused(self):
        with self.assertRaises(C.CorError):
            make(entry=0x20000)

    def test_app_with_header_refused(self):
        # What shipped in trx64mc-main-51e89821 (T93 5.12): the .app, which is the image behind a 12-byte
        # header, packed where the raw image belongs. The header then sits on the entry point.
        dotapp = struct.pack("<III", 0x30000, len(APP), 0x30000) + APP
        with self.assertRaises(C.CorError) as e:
            make(app=dotapp)
        self.assertIn("this is a .app", str(e.exception))

    def test_image_that_only_looks_like_a_header_passes(self):
        # The negative control of the control: three words that do not describe this file are data, not a header.
        other = struct.pack("<III", 0x30000, len(APP), 0x40000) + APP
        C.parse(make(app=other)[0], C.CONTRACT)

    def test_cli(self):
        bit, _ = bit_file()
        with tempfile.TemporaryDirectory() as d:
            p, a, o = (os.path.join(d, n) for n in ("p.bit", "a.bin", "c.cor"))
            open(p, "wb").write(bit)
            open(a, "wb").write(APP)
            self.assertEqual(C.main(["pack", "-o", o, "--name", "T", "--version", "1", "--partial", p,
                                      "--app", a, "--host-key", "820eded2"]), 0)
            self.assertEqual(C.main(["check", o, "--contract", "2"]), 0)
            self.assertEqual(C.main(["check", o, "--contract", "9"]), 1)
            self.assertEqual(C.main(["check", o, "--host-key", "820eded2"]), 0)
            self.assertEqual(C.main(["check", o, "--host-key", "9ae17385"]), 1)


# ---- T115 §7: the large .cor (a full bitstream started by IPROG) ------------------------------------------------------

def full_bit(idcode=0x0362C093, axss=0x553525D3, frames=2000, name=b"top;UserID=0XFFFFFFFF;Version=2024.1"):
    """A synthetic full 7-series .bit: header, then dummy words, sync, IDCODE, [AXSS], FDRI + frames, DESYNC."""
    def w(*v):
        return b"".join(struct.pack(">I", x) for x in v)
    hdr = C.BIT_MAGIC
    for key, txt in zip(b"abcd", (name + b"\0", b"7a50tfgg484\0", b"2026/10/25\0", b"12:00:00\0")):
        hdr += bytes([key]) + struct.pack(">H", len(txt)) + txt
    cfg = w(0xFFFFFFFF, 0xFFFFFFFF, 0x000000BB, 0x11220044, 0xFFFFFFFF, 0xFFFFFFFF, 0xAA995566, 0x20000000,
            0x30008001, 0, 0x30018001, idcode)
    if axss is not None:
        cfg += w(0x3001A001, axss)
    cfg += w(0x30004000, 0x50000000 | frames) + b"".join(struct.pack(">I", (i * 2654435761) & 0xFFFFFFFF) for i in range(frames))
    cfg += w(0x30008001, 0x0000000D, 0x20000000, 0x20000000)
    return hdr + b"e" + struct.pack(">I", len(cfg)) + cfg


def make_large(**kw):
    args = dict(cid="amiga1200", name="Amiga 1200", version="0.3.0", bit=full_bit(), app=APP, icon=ICON)
    args.update(kw)
    return C.pack_large(**args)


class OriginTest(unittest.TestCase):
    """T137 D1/D2: `origin` and the flash fields of a backup."""

    def test_trx_writes_no_key_and_keeps_the_bytes(self):
        a = make_large()
        b = make_large(origin="trx")
        self.assertEqual(a, b)
        m, _ = C.parse(a)
        self.assertNotIn("origin", m)

    def test_foreign_and_backup_round_trip(self):
        m, _ = C.parse(make_large(origin="foreign"))
        self.assertEqual(m["origin"], "foreign")
        self.assertNotIn("flash_id", m)
        m, _ = C.parse(make_large(origin="backup", flash_id="Winbond W25Q128"))
        self.assertEqual((m["origin"], m["flash_bit"], m["flash_app"], m["flash_id"]), ("backup", 0, 0x220000, "Winbond W25Q128"))

    def test_refusals(self):
        with self.assertRaises(C.CorError):
            make_large(origin="vendor")
        with self.assertRaises(C.CorError):
            make_large(origin="backup")                       # a backup without its chip
        with self.assertRaises(C.CorError):
            make_large(origin="foreign", flash_id="x")        # the chip belongs to a backup
        # a hand-made manifest: backup layout other than the fixed one
        blob = make_large(origin="backup", flash_id="Winbond W25Q128")
        m, r = C.parse(blob)
        m["flash_app"] = 0x300000
        body = C.rec(1, json.dumps(m, sort_keys=True, separators=(",", ":")).encode()) + C.rec(3, r[3]) + C.rec(4, r[4]) + C.rec(5, r[5])
        with self.assertRaises(C.CorError):
            C.parse(C.MAGIC + struct.pack("<I", len(body)) + body)


class LargeTest(unittest.TestCase):
    def test_round_trip(self):
        bit = full_bit()
        blob = make_large(bit=bit, bit_commit="aaaa1111", app_commit="bbbb2222")
        m, r = C.parse(blob)
        self.assertEqual(r[C.T_BIT], bit)                    # the .bit file as it is, header included
        self.assertEqual(r[C.T_APP], APP)
        self.assertEqual(r[C.T_ICON], ICON)
        self.assertEqual((m["kind"], m["format"], m["idcode"], m["map"]), ("large", 2, "0362c093", "identity"))
        self.assertEqual((m["app_load"], m["app_entry"], m["bit_commit"]), (0x30000, 0x30000, "aaaa1111"))
        self.assertNotIn("host_key", m)
        self.assertNotIn("contract", m)
        self.assertEqual([t for t in (1, 2, 3, 4, 5) if t in r], [1, 3, 4, 5])

    def test_map_follows_the_axss_write(self):
        self.assertEqual(C.parse(make_large(bit=full_bit(axss=None)))[0]["map"], "gideon_bl4")
        self.assertEqual(C.parse(make_large(bit=full_bit(axss=None), map_kind="identity"))[0]["map"], "identity")

    def test_wrong_idcode_refused(self):
        with self.assertRaises(C.CorError) as e:
            make_large(bit=full_bit(idcode=0x03631093))      # the 100T
        self.assertIn("not the XC7A50T", str(e.exception))
        # the version nibble does not count
        self.assertEqual(C.parse(make_large(bit=full_bit(idcode=0x1362C093)))[0]["idcode"], "1362c093")

    def test_wrong_idcode_in_a_made_file_refused(self):
        # a file whose manifest says 50T but whose bitstream is the 100T's: the check on adding finds it
        blob = make_large()
        bad = bytearray(blob)
        i = bytes(bad).find(struct.pack(">I", 0x0362C093))
        bad[i:i + 4] = struct.pack(">I", 0x03631093)
        with self.assertRaises(C.CorError):
            C.parse(bytes(bad))                              # SHA-256 first: the part is damaged

    def test_manifest_idcode_must_match_the_bitstream(self):
        m, r = C.parse(make_large())
        m = dict(m, idcode="03631093")
        body = C.rec(1, json.dumps(m, sort_keys=True, separators=(",", ":")).encode()) + C.rec(3, r[3]) + C.rec(4, r[4]) + C.rec(5, r[5])
        with self.assertRaises(C.CorError) as e:
            C.parse(C.MAGIC + struct.pack("<I", len(body)) + body)
        self.assertIn("IDCODE", str(e.exception))

    def test_truncated_refused(self):
        blob = make_large()
        for cut in (1, 100, 5000, len(blob) - 20, len(blob) - 12):
            with self.assertRaises(C.CorError):
                C.parse(blob[:-cut])

    def test_damaged_part_refused(self):
        blob = make_large()
        for pos in (len(blob) - 5, len(blob) // 2, 300):
            bad = bytearray(blob)
            bad[pos] ^= 1
            with self.assertRaises(C.CorError):
                C.parse(bytes(bad))

    def test_no_sync_or_not_a_bitstream_refused(self):
        with self.assertRaises(C.CorError):
            make_large(bit=bytes(4096))

    def test_app_rules(self):
        with self.assertRaises(C.CorError):
            make_large(app=b"")
        dotapp = struct.pack("<III", 0x30000, len(APP), 0x30000) + APP
        with self.assertRaises(C.CorError):
            make_large(app=dotapp)
        with self.assertRaises(C.CorError):
            make_large(app=bytes(C.LARGE_APP_MAX + 1))

    def test_dfx_flags_not_allowed_in_a_large_manifest(self):
        m, r = C.parse(make_large())
        for extra in ({"host_key": "820eded2"}, {"contract": 2}):
            mm = dict(m, **extra)
            body = C.rec(1, json.dumps(mm, sort_keys=True, separators=(",", ":")).encode()) + C.rec(3, r[3]) + C.rec(4, r[4]) + C.rec(5, r[5])
            with self.assertRaises(C.CorError):
                C.parse(C.MAGIC + struct.pack("<I", len(body)) + body)

    def test_record_5_in_a_dfx_bundle_refused(self):
        dfx, _ = make(icon=ICON)
        body = dfx[12:] + C.rec(5, full_bit())
        with self.assertRaises(C.CorError):
            C.parse(C.MAGIC + struct.pack("<I", len(body)) + body)

    def test_dfx_bundle_is_unchanged_byte_for_byte(self):
        # Golden: the SHA-256 of what tools/cor_pack.py of main (before the large mode) made on exactly these inputs. Negative control:
        # test_golden_detects_a_change.
        blob, _ = make(icon=ICON)
        self.assertEqual(hashlib.sha256(blob).hexdigest(), DFX_GOLDEN)
        self.assertEqual(len(blob), 7644)

    def test_golden_detects_a_change(self):
        blob, _ = make(icon=ICON, name="VIC-21")
        self.assertNotEqual(hashlib.sha256(blob).hexdigest(), DFX_GOLDEN)

    def test_cli(self):
        with tempfile.TemporaryDirectory() as d:
            b, a, o = (os.path.join(d, n) for n in ("x.bit", "a.bin", "c.cor"))
            open(b, "wb").write(full_bit())
            open(a, "wb").write(APP)
            self.assertEqual(C.main(["pack-large", "-o", o, "--id", "amiga1200", "--name", "A", "--version", "1", "--bit", b, "--app", a]), 0)
            self.assertEqual(C.main(["check", o]), 0)
            open(b, "wb").write(full_bit(idcode=0x03631093))
            self.assertEqual(C.main(["pack-large", "-o", o + "2", "--id", "x", "--name", "A", "--version", "1", "--bit", b, "--app", a]), 1)
            self.assertFalse(os.path.exists(o + "2"))


# ---- T104.1 D9, D12: the optional records 6 (description) and 7 (libraries) ----------------------------------------------------------

LARGE_GOLDEN = "311d02a9c4df134d91ef859fe427b234560ffb56b1269188671588b26c0f4bb5"
DESC_TOML = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tests", "coreapp", "golden", "desc_test.toml")


def u2a(name="alpha", fmt=3, hsz=104, magic=b"U2AP"):
    return magic + struct.pack("<HH", hsz, fmt) + bytes(max(0, hsz - 8)) + bytes(64)


def desc_record():
    import descrec
    return descrec.compile_text(open(DESC_TOML).read())


class RecordsTest(unittest.TestCase):
    def test_without_the_records_nothing_changes(self):
        # a bundle that asks for neither is the bundle of before, byte for byte: the golden hashes of both kinds, made by main's packer
        blob, _ = make(icon=ICON)
        self.assertEqual(hashlib.sha256(blob).hexdigest(), DFX_GOLDEN)
        large = C.pack_large(cid="amiga1200", name="Amiga 1200", version="0.3.0", bit=full_bit(), app=APP, icon=ICON)
        self.assertEqual(hashlib.sha256(large).hexdigest(), LARGE_GOLDEN)
        # the golden can fail: the same inputs with a record are another file
        self.assertNotEqual(hashlib.sha256(make(icon=ICON, desc=desc_record())[0]).hexdigest(), DFX_GOLDEN)
        # and the manifest has none of the new keys
        m, r = C.parse(blob)
        for k in ("records", "desc_size", "libs_size", "desc_sha256", "libs_sha256", "libs_count"):
            self.assertNotIn(k, m)
        self.assertNotIn(C.T_DESC, r)

    def test_identical_to_the_packer_of_main(self):
        # the same inputs through main's file (git), when this is a checkout that has a main
        try:
            src = subprocess.run(["git", "show", "main:tools/cor_pack.py"], cwd=os.path.dirname(os.path.abspath(__file__)),
                                 capture_output=True, check=True).stdout
        except (OSError, subprocess.CalledProcessError):
            self.skipTest("no main to compare with here: the golden hashes above carry the check")
        import importlib.util
        with tempfile.TemporaryDirectory() as d:
            f = os.path.join(d, "cor_pack_main.py")
            open(f, "wb").write(src)
            spec = importlib.util.spec_from_file_location("cor_pack_main", f)
            Mn = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(Mn)
        bit, _ = bit_file()
        for icon in (None, ICON):
            a = C.pack("T", "1", bit, APP, 0x30000, 0x30010, "820eded2", icon=icon)
            b = Mn.pack("T", "1", bit, APP, 0x30000, 0x30010, "820eded2", icon=icon)
            self.assertEqual(a, b)
            a = C.pack_large("amiga1200", "A", "1", full_bit(), APP, icon)
            b = Mn.pack_large("amiga1200", "A", "1", full_bit(), APP, icon)
            self.assertEqual(a, b)

    def test_round_trip_both_kinds(self):
        desc = desc_record()
        libs = [u2a(), u2a() + b"xy"]
        blob, _ = make(icon=ICON, desc=desc, libs=libs)
        m, r = C.parse(blob, C.CONTRACT)
        self.assertEqual(r[C.T_DESC], desc)
        self.assertEqual(C.parse_libs(r[C.T_LIBS]), libs)               # the odd length survives its padding
        self.assertEqual((m["records"], m["desc_size"], m["libs_count"]), (1, len(desc), 2))
        self.assertEqual([t for t in sorted(r)], [1, 2, 3, 4, 6, 7])
        big = make_large(desc=desc, libs=libs[:1])
        m, r = C.parse(big)
        self.assertEqual([t for t in sorted(r)], [1, 3, 4, 5, 6, 7])
        self.assertEqual(m["records"], 1)
        # one of them alone
        m, r = C.parse(make(desc=desc)[0])
        self.assertNotIn(C.T_LIBS, r)
        self.assertNotIn("libs_size", m)
        m, r = C.parse(make(libs=libs)[0])
        self.assertNotIn(C.T_DESC, r)
        self.assertNotIn("desc_size", m)

    def test_the_records_follow_the_icon_and_precede_nothing(self):
        # the order the reader of the board requires: increasing types, the manifest first
        blob, _ = make(icon=ICON, desc=desc_record(), libs=[u2a()])
        types, p = [], 12
        while p < len(blob):
            t, n = struct.unpack_from("<II", blob, p)
            types.append(t)
            p += 8 + n
        self.assertEqual(types, [1, 2, 3, 4, 6, 7])

    def test_damaged_records_refused(self):
        blob, _ = make(desc=desc_record(), libs=[u2a()])
        for t in (C.T_DESC, C.T_LIBS):
            off, p = None, 12
            while p < len(blob):
                tt, n = struct.unpack_from("<II", blob, p)
                if tt == t:
                    off = p + 8 + n // 2
                p += 8 + n
            bad = bytearray(blob)
            bad[off] ^= 1
            with self.assertRaises(C.CorError) as e:
                C.parse(bytes(bad))
            self.assertIn("SHA-256", str(e.exception))

    def test_manifest_and_records_must_agree(self):
        desc = desc_record()
        blob, _ = make(desc=desc, libs=[u2a()])
        m, r = C.parse(blob)

        def rebuild(mm, drop=(), keep_desc=True):
            body = C.rec(1, json.dumps(mm, sort_keys=True, separators=(",", ":")).encode())
            for t in (2, 3, 6, 7):
                if t in r and t not in drop:
                    body += C.rec(t, r[t])
            return C.MAGIC + struct.pack("<I", len(body)) + body
        C.parse(rebuild(m))                                             # the rebuild itself is sound
        # a record the manifest does not announce, and a manifest that announces a record that is not there
        m2 = {k: v for k, v in m.items() if k not in ("desc_size", "desc_sha256")}
        with self.assertRaises(C.CorError):
            C.parse(rebuild(m2))
        with self.assertRaises(C.CorError):
            C.parse(rebuild(m, drop=(6,)))
        # a version of the extension this tool does not know
        with self.assertRaises(C.CorError) as e:
            C.parse(rebuild(dict(m, records=2)))
        self.assertIn("records", str(e.exception))
        with self.assertRaises(C.CorError):
            C.parse(rebuild({k: v for k, v in m.items() if k != "records"}))
        # the count of libraries
        with self.assertRaises(C.CorError):
            C.parse(rebuild(dict(m, libs_count=3)))

    def test_a_description_that_is_not_one_is_refused_when_packing(self):
        with self.assertRaises(C.CorError):
            make(desc=b"TRXD" + bytes(200))
        with self.assertRaises(C.CorError):
            make(desc=desc_record()[:-1])

    def test_libraries_must_be_u2a_of_format_3(self):
        with self.assertRaises(C.CorError):
            make(libs=[u2a(magic=b"XXXX")])
        with self.assertRaises(C.CorError):
            make(libs=[u2a(fmt=2)])
        with self.assertRaises(C.CorError):
            make(libs=[u2a(hsz=64)])
        with self.assertRaises(C.CorError):
            make(libs=[b"U2AP"])
        with self.assertRaises(C.CorError):
            C.pack_libs([])
        # a library set that lies: a count that is not what follows, a length past the end, bytes behind the last
        good = C.pack_libs([u2a()])
        for bad in (good[:8] + struct.pack("<I", 2) + good[12:], good[:12] + struct.pack("<I", 9999) + good[16:], good + b"\0\0\0\0", good[:-1],
                    b"TRXM" + good[4:], good[:4] + struct.pack("<I", 2) + good[8:]):
            with self.assertRaises(C.CorError):
                C.parse_libs(bad)

    def test_a_record_twice_or_out_of_order_refused(self):
        blob, _ = make(desc=desc_record())
        m, r = C.parse(blob)
        body = blob[12:] + C.rec(6, r[6])
        with self.assertRaises(C.CorError):
            C.parse(C.MAGIC + struct.pack("<I", len(body)) + body)
        body = C.rec(1, blob[20:20 + struct.unpack_from("<I", blob, 16)[0]]) + C.rec(6, r[6]) + C.rec(2, r[2]) + C.rec(3, r[3])
        with self.assertRaises(C.CorError):
            C.parse(C.MAGIC + struct.pack("<I", len(body)) + body)

    def test_cli(self):
        bit, _ = bit_file()
        with tempfile.TemporaryDirectory() as d:
            p, a, o, l1, l2 = (os.path.join(d, n) for n in ("p.bit", "a.bin", "c.cor", "l1.u2a", "l2.u2a"))
            open(p, "wb").write(bit)
            open(a, "wb").write(APP)
            open(l1, "wb").write(u2a())
            open(l2, "wb").write(u2a() + b"abcd")
            self.assertEqual(C.main(["pack", "-o", o, "--name", "T", "--version", "1", "--partial", p, "--app", a, "--host-key", "820eded2",
                                      "--desc", DESC_TOML, "--lib", l1, "--lib", l2]), 0)
            self.assertEqual(C.main(["check", o]), 0)
            m, r = C.parse(open(o, "rb").read())
            self.assertEqual(m["libs_count"], 2)
            self.assertEqual(r[C.T_DESC], desc_record())
            # a description with a typo is refused with the file named, and nothing is written
            bad = os.path.join(d, "bad.toml")
            open(bad, "w").write(open(DESC_TOML).read().replace("[[node]]", "[[nodes]]", 1))
            self.assertEqual(C.main(["pack", "-o", o + "2", "--name", "T", "--version", "1", "--partial", p, "--app", a, "--host-key", "820eded2",
                                      "--desc", bad]), 1)
            self.assertFalse(os.path.exists(o + "2"))
            # the large form
            b = os.path.join(d, "x.bit")
            open(b, "wb").write(full_bit())
            self.assertEqual(C.main(["pack-large", "-o", o + "3", "--id", "x", "--name", "A", "--version", "1", "--bit", b, "--app", a,
                                      "--desc", DESC_TOML, "--lib", l1]), 0)
            self.assertEqual(C.main(["check", o + "3"]), 0)


def lib_with_menu(name, menu):
    """A format-3 library file with a menu record (tests/coreapp/gen_bundle.py u2a())."""
    off = 104 + 64
    h = struct.pack("<IHHHHBBBBI16sI16sIIIIIIIIIIII", 0x50413255, 104, 3, 3, 3, 1, 1, 2, 0, 1, name.encode(), 1 << 16, b"", 104, 64, 16, 16, 0, 0xFFFFFFFF,
                    off, 0, 104, 0, 0, off)
    return h + bytes(64) + menu


class MergedMenusTest(unittest.TestCase):
    """T104.2 D10: the libraries' menus are merged into the description when it is packed, and the record names them."""

    def setUp(self):
        import descrec
        self.d = descrec
        self.menu = descrec.compile_text('[[node]]\nanchor = "F7/quick"\nkind = "op"\nlabel = "Gamma"\nop = "gamma.go"\n')

    def test_the_menu_is_in_record_6_and_the_library_is_named(self):
        blob, _ = make(desc=desc_record(), libs=[lib_with_menu("gamma", self.menu)])
        m, r = C.parse(blob)
        rec = self.d.parse_desc(r[C.T_DESC])
        self.assertEqual(rec["merged"], ["gamma"])
        self.assertIn("Gamma", [n["label"] for n in rec["node"]])
        self.assertEqual(m["desc_size"], len(r[C.T_DESC]))

    def test_no_description_still_gets_one_for_a_library_with_a_menu(self):
        blob, _ = make(libs=[lib_with_menu("gamma", self.menu)])
        m, r = C.parse(blob)
        self.assertEqual(self.d.parse_desc(r[C.T_DESC])["merged"], ["gamma"])

    def test_a_library_without_a_menu_leaves_the_description_as_it_was(self):
        blob, _ = make(desc=desc_record(), libs=[u2a()])
        self.assertEqual(C.parse(blob)[1][C.T_DESC], desc_record())

    def test_a_library_with_a_poll_keeps_its_menu_for_the_run_time(self):
        poll = self.d.compile_text('[[node]]\nanchor = "F7/quick"\nkind = "op"\nlabel = "P"\nop = "p.go"\n[poll]\nop = "p.go"\nperiod_ms = 10\n')
        blob, _ = make(desc=desc_record(), libs=[lib_with_menu("poller", poll)])
        self.assertEqual(C.parse(blob)[1][C.T_DESC], desc_record())


if __name__ == "__main__":
    unittest.main()
