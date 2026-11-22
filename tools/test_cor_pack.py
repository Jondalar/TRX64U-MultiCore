#!/usr/bin/env python3
"""Host test for tools/cor_pack.py (T84 S3): round trip, corrupt hash refused, wrong contract refused,
wrong host key refused (T84 D5b).

  python3 tools/test_cor_pack.py        (also collected by python3 -m pytest tools/test_cor_pack.py)
"""
import contextlib
import io
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
import descrec  # noqa: E402


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


class ShortTest(unittest.TestCase):
    """T209 D15: the optional `short` of the boot menu's text tile."""

    def test_no_short_keeps_the_bytes_and_the_key_away(self):
        a, _ = make()
        b, _ = make(short=None)
        self.assertEqual(a, b)
        m, _ = C.parse(a, C.CONTRACT)
        self.assertNotIn("short", m)

    def test_short_round_trips_in_both_kinds(self):
        blob, _ = make(short="PET")
        m, _ = C.parse(blob, C.CONTRACT)
        self.assertEqual(m["short"], "PET")
        m, _ = C.parse(C.pack_large(cid="x", name="X", version="1", bit=full_bit(), app=APP, short="C128"))
        self.assertEqual(m["short"], "C128")

    def test_bad_short_refused(self):
        for bad in ("FIVE5", "SIXSIX", "A B", "VIC-", "\u00e4"):
            with self.assertRaises(C.CorError):
                make(short=bad)

    def test_check_refuses_a_bad_short_in_a_made_file(self):
        blob, _ = make(short="PET")
        bad = blob.replace(b'"short":"PET"', b'"short":"P T"')
        self.assertEqual(len(bad), len(blob))
        with self.assertRaises(C.CorError):
            C.parse(bad, C.CONTRACT)


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

    def test_a_description_with_the_cores_data_sections(self):
        # T239.3 D17: the VIC-20's own description (cores/vc20/app/desc.toml) is record 6 of its bundle; the core section names the manifest's core
        import descrec
        with open(os.path.join(os.path.dirname(DESC_TOML), "..", "..", "..", "cores", "vc20", "app", "desc.toml")) as f:
            desc = descrec.compile_text(f.read())
        blob, _ = make(desc=desc)
        m, r = C.parse(blob, C.CONTRACT)
        self.assertEqual(r[C.T_DESC], desc)
        C.check_optional(m, r)
        self.assertTrue(descrec.parse_desc(r[C.T_DESC])["sections"])
        with self.assertRaisesRegex(C.CorError, "core section names"):
            make(desc=desc, name="Another Machine")
        blob, _ = make(desc=desc, libs=[u2a()])                                     # the libraries' menus are merged in: the sections stay
        self.assertTrue(descrec.parse_desc(C.parse(blob, C.CONTRACT)[1][C.T_DESC])["sections"])

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

    def test_desc_more_than_once_compiles_the_files_into_one_record(self):
        """T239.3 base 3.3: a core's menu.toml (menu, key table) and desc.toml (core data, windows) are one record 6; a table or key in both files is refused naming both."""
        menu = 'key = [ { layout = "timeline", key = "esc", op = "tw.cancel", close = true } ]\n[[task]]\nlabel = "Dump"\nop = "ring.dump"\njob = true\n'
        desc = ('[core]\nid = "VC20"\nname = "T"\ntitle = "T"\nshort_title = "T"\nfolder = "t"\nstore = "t"\n'
                '[[window]]\nname = "audio_mixer"\naddress = 0xA0000500\nsize = 0x14\n')
        bit, _ = bit_file()
        with tempfile.TemporaryDirectory() as d:
            p, a, o, m_, d_ = (os.path.join(d, n) for n in ("p.bit", "a.bin", "c.cor", "menu.toml", "desc.toml"))
            open(p, "wb").write(bit)
            open(a, "wb").write(APP)
            open(m_, "w").write(menu)
            open(d_, "w").write(desc)
            args = ["pack", "-o", o, "--name", "T", "--version", "1", "--partial", p, "--app", a, "--host-key", "820eded2"]
            self.assertEqual(C.main(args + ["--desc", m_, "--desc", d_]), 0)
            self.assertEqual(C.main(["check", o]), 0)
            m, r = C.parse(open(o, "rb").read())
            rec = descrec.parse_desc(r[C.T_DESC])
            self.assertEqual((rec["core"]["name"], len(rec["key"]), rec["window"][0]["name"], rec["task"][0]["job"]), ("T", 1, "audio_mixer", True))
            self.assertEqual(r[C.T_DESC], descrec.compile_files([("a", menu), ("b", desc)]))
            self.assertEqual(r[C.T_DESC], descrec.compile_desc({**descrec.toml_loads(menu), **descrec.toml_loads(desc)}))
            # the order of the files does not change the record
            o2 = o + "2"
            self.assertEqual(C.main(["pack", "-o", o2, "--name", "T", "--version", "1", "--partial", p, "--app", a, "--host-key", "820eded2", "--desc", d_, "--desc", m_]), 0)
            self.assertEqual(C.parse(open(o2, "rb").read())[1][C.T_DESC], r[C.T_DESC])
            # one file is what it always was
            only = os.path.join(d, "only.toml")
            open(only, "w").write(menu)
            o3 = o + "3"
            self.assertEqual(C.main(args[:2] + [o3] + args[3:] + ["--desc", only]), 0)
            self.assertEqual(C.parse(open(o3, "rb").read())[1][C.T_DESC], descrec.compile_text(menu))
            # a table in both files: refused, both files named, nothing written
            twice = os.path.join(d, "twice.toml")
            open(twice, "w").write(menu)
            o4 = o + "4"
            buf = io.StringIO()
            with contextlib.redirect_stderr(buf):
                self.assertEqual(C.main(args[:2] + [o4] + args[3:] + ["--desc", m_, "--desc", twice]), 1)
            self.assertIn("menu.toml", buf.getvalue())
            self.assertIn("twice.toml", buf.getvalue())
            self.assertIn("'key'", buf.getvalue())
            self.assertFalse(os.path.exists(o4))
            # the large form takes it too
            b = os.path.join(d, "x.bit")
            open(b, "wb").write(full_bit())
            self.assertEqual(C.main(["pack-large", "-o", o + "5", "--id", "x", "--name", "T", "--version", "1", "--bit", b, "--app", a, "--desc", m_, "--desc", d_]), 0)
            self.assertEqual(C.parse(open(o + "5", "rb").read())[1][C.T_DESC], r[C.T_DESC])


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
        self.menu = descrec.compile_text('[[node]]\nanchor = "F7/settings"\nkind = "op"\nlabel = "Gamma"\nop = "gamma.go"\n')

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
        poll = self.d.compile_text('[[node]]\nanchor = "F7/settings"\nkind = "op"\nlabel = "P"\nop = "p.go"\n[poll]\nop = "p.go"\nperiod_ms = 10\n')
        blob, _ = make(desc=desc_record(), libs=[lib_with_menu("poller", poll)])
        self.assertEqual(C.parse(blob)[1][C.T_DESC], desc_record())


class FilesTest(unittest.TestCase):
    """Record 8, the files a core installs on the SD (T175)."""
    FILES = [("tests/a.toml", b"new a\n", [(3, 0x1234567890ABCDEF)]), ("b.txt", b"b" * 5, [])]

    def test_roundtrip_in_both_kinds(self):
        for blob in (make(files=self.FILES)[0], make_large(files=self.FILES)):
            m, r = C.parse(blob)
            self.assertEqual(C.parse_files(r[C.T_FILES]), self.FILES)
            self.assertEqual((m["records"], m["files_count"], m["files_size"]), (1, 2, len(r[C.T_FILES])))
            self.assertEqual(list(r)[-1], C.T_FILES)                       # after the others
        m, r = C.parse(make()[0])
        self.assertNotIn("files_size", m)                                  # a bundle without it is what it was

    def test_a_damaged_record_is_refused(self):
        blob = bytearray(make_large(files=self.FILES))
        blob[-9] ^= 1
        with self.assertRaises(C.CorError):
            C.parse(bytes(blob))

    def test_paths_are_relative_and_inside(self):
        for bad in ("/a", "../a", "a/../b", "a//b", "a\\b", ""):
            with self.assertRaises(C.CorError, msg=bad):
                C.pack_files([(bad, b"x", [])])

    def test_collect_pairs_earlier_versions_with_their_file(self):
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            os.makedirs(d + "/sd/tests"); os.makedirs(d + "/old")
            open(d + "/sd/tests/x.toml", "wb").write(b"v2")
            open(d + "/old/x.v1.toml", "wb").write(b"v1")
            (p, data, olds), = C.collect_files(d + "/sd", d + "/old")
            self.assertEqual((p, data, olds), ("tests/x.toml", b"v2", [(2, C.fnv64(b"v1"))]))
            open(d + "/old/y.v1.toml", "wb").write(b"?")
            with self.assertRaises(C.CorError):                             # an earlier version of a file the core does not install
                C.collect_files(d + "/sd", d + "/old")

    def test_the_diag_core_carries_its_tests_and_only_it(self):
        root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "cores")
        files = C.collect_files(os.path.join(root, "diag", "sd"), os.path.join(root, "diag", "superseded"))
        self.assertIn("tests/cd32.toml", [f[0] for f in files])
        self.assertTrue(all(o for f in files if f[0] in ("tests/cd32.toml", "tests/stick2.toml") for o in [f[2]]))


def heap_app(heap=0x7A0000, image_end=0x400000, region_end=0xE90000, reserve=0x20000, extra=b"\x5a" * 64):
    """An application image that carries the TRXHEAP record (coreapp/src/heapcfg.c)."""
    return APP + C.HEAP_MAGIC + struct.pack("<4I", heap, image_end, region_end, reserve) + extra


class HeapTest(unittest.TestCase):
    """T239.3 D15: the packer writes the core's heap into its copy of the app and refuses an image that does not fit."""

    def words(self, app):
        o = app.index(C.HEAP_MAGIC) + len(C.HEAP_MAGIC)
        return struct.unpack_from("<4I", app, o)

    def test_fill_sets_only_the_heap_word(self):
        app = heap_app()
        out = C.app_heap(app, 0x950000, "vic20")
        self.assertEqual(self.words(out), (0x950000, 0x400000, 0xE90000, 0x20000))
        self.assertEqual(len(out), len(app))
        o = app.index(C.HEAP_MAGIC) + len(C.HEAP_MAGIC)
        self.assertEqual(out[:o], app[:o])
        self.assertEqual(out[o + 4:], app[o + 4:])

    def test_no_heap_keeps_the_default(self):
        app = heap_app()
        self.assertEqual(C.app_heap(app, None, "vic20"), app)
        self.assertEqual(C.app_heap(APP, None, "trx64g"), APP)            # an app without the record (Gideon's) is left alone

    def test_pack_and_pack_large_carry_the_filled_copy(self):
        blob, _ = make(app=heap_app(), heap=0x600000)
        self.assertEqual(self.words(C.parse(blob, C.CONTRACT)[1][C.T_APP])[0], 0x600000)
        blob, _ = make(app=heap_app())
        self.assertEqual(self.words(C.parse(blob, C.CONTRACT)[1][C.T_APP])[0], 0x7A0000)

    def test_refusal_names_core_figure_and_limit(self):
        with self.assertRaises(C.CorError) as e:
            C.app_heap(heap_app(), 0xB00000, "vic20")
        msg = str(e.exception)
        self.assertIn("vic20", msg)
        self.assertIn("0xb00000", msg)
        self.assertIn("0xe90000", msg)
        with self.assertRaises(C.CorError) as e:
            make(app=heap_app(), heap=0xB00000)                           # through pack as well
        self.assertIn("0xe90000", str(e.exception))

    def test_default_that_does_not_fit_is_refused_too(self):
        with self.assertRaises(C.CorError):
            C.app_heap(heap_app(image_end=0xA00000), None, "big")

    def test_exact_fit_passes_one_more_does_not(self):
        C.app_heap(heap_app(), 0xE90000 - 0x20000 - 0x400000, "x")
        with self.assertRaises(C.CorError):
            C.app_heap(heap_app(), 0xE90000 - 0x20000 - 0x400000 + 16, "x")

    def test_heap_without_record_or_with_two_is_refused(self):
        with self.assertRaises(C.CorError):
            C.app_heap(APP, 0x100000, "x")
        with self.assertRaises(C.CorError):
            C.app_heap(heap_app() + heap_app(), 0x100000, "x")
        for bad in (0x1000, 0x100008 + 1):
            with self.assertRaises(C.CorError):
                C.app_heap(heap_app(), bad, "x")


class PackersCarryTheDescriptionTest(unittest.TestCase):
    """T239.3 D17 part 2: the packers put cores/<id>/app/desc.toml into record 6 of the .cor, and the bundle checks it."""

    ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")

    def desc(self):
        import descrec
        with open(os.path.join(self.ROOT, "cores", "vc20", "app", "desc.toml")) as f:
            return descrec.compile_text(f.read())

    def test_pack_cores_mc_carries_it(self):
        with tempfile.TemporaryDirectory() as d:
            bits, apps, out = (os.path.join(d, x) for x in ("bits", "apps", "out"))
            for x in (bits, apps, out):
                os.mkdir(x)
            open(os.path.join(bits, "c64_basic.bit"), "wb").write(full_bit())
            open(os.path.join(bits, "vc20_partial.bit"), "wb").write(bit_file()[0])
            open(os.path.join(apps, "vc20.app"), "wb").write(APP)
            r = subprocess.run([os.path.join(self.ROOT, "tools", "pack_cores_mc.sh"), "--bits", bits, "--app-dir", apps, "--out", out, "--core", "vc20"], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            m, rec = C.parse(open(os.path.join(out, "vc20.cor"), "rb").read())
            self.assertEqual(rec[C.T_DESC], self.desc())
            self.assertEqual(m["name"], "VIC-20")

    def test_pack_large_cor_carries_it_and_refuses_a_second_description(self):
        with tempfile.TemporaryDirectory() as d:
            bit, app, out = (os.path.join(d, x) for x in ("a.bit", "app.bin", "out"))
            open(bit, "wb").write(full_bit())
            open(app, "wb").write(APP)
            cmd = [os.path.join(self.ROOT, "tools", "pack_large_cor.sh"), "--id", "vc20", "--name", "VIC-20", "--version", "0.1", "--bit", bit, "--app-bin", app, "--out", out]
            r = subprocess.run(cmd, capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            self.assertEqual(C.parse(open(os.path.join(out, "vc20.cor"), "rb").read())[1][C.T_DESC], self.desc())
            r = subprocess.run(cmd + ["--desc", os.path.join(self.ROOT, "tests", "coreapp", "golden", "desc_sections.toml")], capture_output=True, text=True)
            self.assertNotEqual(r.returncode, 0)
            self.assertIn("one description per core", r.stderr)
            # a core without cores/<id>/app/desc.toml carries no record
            r = subprocess.run(cmd[:2] + ["nodesc"] + cmd[3:], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
            self.assertNotIn(C.T_DESC, C.parse(open(os.path.join(out, "nodesc.cor"), "rb").read())[1])


class CoreTomlHeapTest(unittest.TestCase):
    """T239.3 D15: the `heap` key of core.toml."""

    def test_hex_and_decimal_and_range(self):
        import core_toml as T
        with tempfile.TemporaryDirectory() as d:
            f = d + "/c.toml"
            open(f, "w").write('id = "x"\nheap = 0x950000\norder = 07\n')
            self.assertEqual(T.parse(f)["heap"], 0x950000)
            self.assertEqual(T.parse(f)["order"], 7)
        self.assertIn("heap", T.SCHEMA)
        self.assertEqual(T.SCHEMA["heap"], ("int", False))

    def test_check_refuses_a_bad_heap(self):
        import core_toml as T
        saved = T.ROOT, T.CORES
        with tempfile.TemporaryDirectory() as d:
            os.makedirs(d + "/cores/x"); os.makedirs(d + "/contract")
            open(d + "/contract/VERSION", "w").write(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "contract", "VERSION")).read())
            base = 'id = "x"\nname = "X"\nversion = "0.1.0"\ncontract = %s\napp = "blueprint"\norder = 1\nexperimental = true\nsources = ["cores/x"]\n' % open(d + "/contract/VERSION").read().strip()
            try:
                T.ROOT, T.CORES = d, d + "/cores"
                res = {}
                for heap in ("0x950000", "0x1000", "0x7A0001", "0xF00000"):
                    open(d + "/cores/x/core.toml", "w").write(base + "heap = %s\n" % heap)
                    import io, contextlib
                    buf = io.StringIO()
                    with contextlib.redirect_stdout(buf):
                        res[heap] = T.check()
                self.assertEqual(res, {"0x950000": 0, "0x1000": 1, "0x7A0001": 1, "0xF00000": 1})
            finally:
                T.ROOT, T.CORES = saved


if __name__ == "__main__":
    unittest.main()
