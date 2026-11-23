#!/usr/bin/env python3
"""Host test for tools/cfw_core.py (T113 Q12, T115): the .cfw of a monolithic core is what the host's parser takes, and the
descriptor key `delivery` of tools/core_toml.py. Controls: every test named test_control_* is a case that must be refused,
and the last one of Check turns the design rename off to show the check can fail on the very file that passes.

  python3 tools/test_cfw_core.py        (also collected by python3 -m pytest tools/test_cfw_core.py)
"""
import contextlib
import io
import os
import re
import struct
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import cfw_core as F  # noqa: E402
import core_toml as T  # noqa: E402
import trx_pkg as P  # noqa: E402
import trx_pkg_check as K  # noqa: E402

CODE = bytes((i * 7) & 0xFF for i in range(0x2000))          # stands in for the updater
RAW = bytes((i * 5) & 0xFF for i in range(0x30000 - 7))      # a core app image, not a multiple of 4


def t1w(reg, val):
    return struct.pack(">II", 0x30000000 | (2 << 27) | (reg << 13) | 1, val)


def bit_file(design="top;UserID=0XFFFFFFFF;Version=2024.1", idcode=F.CFW_IDCODE_XC7A50T, axss=0x553525D3, body=6000):
    """A .bit as Vivado writes it: header a-e, dummy, bus width words, sync, IDCODE, the USR_ACCESS write, filler."""
    hdr = F.BIT_MAGIC
    for key, txt in zip(b"abcd", (design, "7a50tfgg484", "2026/10/23", "12:00:00")):
        t = txt.encode() + b"\0"
        hdr += bytes([key]) + struct.pack(">H", len(t)) + t
    data = b"\xFF" * 16 + bytes([0, 0, 0, 0xBB, 0x11, 0x22, 0, 0x44]) + b"\xFF" * 8 + struct.pack(">I", P.SYNC_WORD)
    data += t1w(F.REG_IDCODE, idcode)
    if axss is not None:
        data += t1w(P.REG_AXSS, axss)
    data += t1w(P.REG_CMD, P.CMD_DESYNC)
    data += bytes(((i * 31) & 0xFF) | 1 for i in range(body * 4))
    return hdr + b"e" + struct.pack(">I", len(data)) + data


def cfw_file(bit, app, gap=0):
    """The layout tools/make_update.sh gives: pre-header, one record holding the updater code, the manifest, the bitstream and
    the application; `gap` bytes of zeros between the two blobs."""
    blobs = P.build_blobs(bit, app, "main-deadbeef", "deadbeef", "cafebabe", flags=P.FLAG_RESTORE)
    if gap:
        v = P.parse_manifest(blobs[:P.MANIFEST_LEN])
        blobs = blobs[:v["app_off"]] + b"\0" * gap + blobs[v["app_off"]:]
    return wrap(blobs)


def wrap(blobs):
    rec = struct.pack("<III", P.RECORD_LOAD, len(CODE) + len(blobs), P.RECORD_LOAD) + CODE + blobs
    return P.PRE_FINGERPRINT + b"U64MK2" + b"\0" * 26 + rec


def prepared(core="amiga1200", **kw):
    return F.rename_design(bit_file(**kw), core), F.app_record(RAW)


class RenameAndRecord(unittest.TestCase):
    def test_rename_keeps_the_configuration_data(self):
        bit = bit_file()
        out = F.rename_design(bit, "amiga1200")
        f0, f1 = F.bit_fields(bit), F.bit_fields(out)
        self.assertEqual(f1[0][0], "amiga1200;UserID=0XFFFFFFFF;Version=2024.1")
        self.assertEqual(f1[0][1:], f0[0][1:])
        self.assertEqual(out[f1[1]:], bit[f0[1]:])
        self.assertEqual(f1[2], f0[2])
        self.assertEqual(P.bit_length_from_header(out), len(out))     # the updater's own header check

    def test_rename_cuts_vivados_long_header_to_what_the_host_reads(self):
        # Vivado's field a with `top` is "top;COMPRESS=TRUE;UserID=0XFFFFFFFF;Version=2024.1" (50 characters): with a longer name it passes the 47 the host
        # copies (strncpy); the name before the first ';' and the configuration data are what count
        bit = bit_file(design="top;COMPRESS=TRUE;UserID=0XFFFFFFFF;Version=2024.1")
        out = F.rename_design(bit, "amiga1200")
        f0, f1 = F.bit_fields(bit), F.bit_fields(out)
        self.assertTrue(f1[0][0].startswith("amiga1200;COMPRESS=TRUE;"))
        self.assertLess(len(f1[0][0]), F.FIELD_MAX)
        self.assertEqual(out[f1[1]:], bit[f0[1]:])
        self.assertEqual(P.bit_length_from_header(out), len(out))

    def test_control_rename_refuses_names_that_pick_another_logo(self):
        for name in ("top", "u64_mk2_artix", "u64_mk2_artix_x", "C128", "1x", "", "a" * 21):
            with self.assertRaises(F.CfwError, msg=name):
                F.rename_design(bit_file(), name)

    def test_control_rename_refuses_a_non_bit(self):
        with self.assertRaises(F.CfwError):
            F.rename_design(b"\0" * 400, "amiga1200")

    def test_app_record(self):
        app = F.app_record(RAW)
        self.assertEqual(struct.unpack_from("<III", app), (0x30000, len(RAW) + 3, 0x30000))
        self.assertEqual(app[12:12 + len(RAW)], RAW)
        self.assertEqual(app[12 + len(RAW):], b"\0" * 3)
        P.check_app_record(app)                                        # what the boot ROM and the updater accept


class Check(unittest.TestCase):
    def test_a_core_cfw_passes(self):
        bit, app = prepared()
        cfw = cfw_file(bit, app)
        info, notes = F.check(cfw, "amiga1200")
        self.assertEqual((info["map"], info["design"], info["app_len"]), ("identity", "amiga1200", len(app) - 12))
        self.assertEqual(cfw[info["bit_off"]:info["bit_off"] + info["bit_len"]], bit)
        self.assertEqual(struct.unpack_from("<III", cfw, info["app_off"]), (0x30000, len(app) - 12, 0x30000))
        self.assertEqual(info["name"], "amiga1200 2026/10/23 12:00:00")
        self.assertTrue(any("identity" in n for n in notes))

    def test_the_packer_checker_agrees(self):
        bit, app = prepared()
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "amiga1200.cfw")
            open(p, "wb").write(cfw_file(bit, app))
            errs, _ = K.check_file(p, bit, app, None)
            self.assertEqual(errs, [])

    def test_the_gap_of_alignment_is_taken(self):
        bit, app = prepared()
        F.check(cfw_file(bit, app, gap=12), "amiga1200")

    def test_version_nibble_is_ignored(self):
        bit, app = prepared(idcode=0x1362C093)                          # the same part, another silicon version
        F.check(cfw_file(bit, app))

    # ---- the controls: each of these must be refused ----
    def test_control_100t_bitstream(self):
        bit, app = prepared(idcode=0x03631093)
        with self.assertRaisesRegex(F.CfwError, "XC7A50T"):
            F.check(cfw_file(bit, app))

    def test_control_no_axss(self):
        bit, app = prepared(axss=None)
        with self.assertRaisesRegex(F.CfwError, "AXSS"):
            F.check(wrap(bit + b"\0" * (-len(bit) % 4) + app))

    def test_control_design_top(self):
        with self.assertRaisesRegex(F.CfwError, "logo"):
            F.check(cfw_file(bit_file(), F.app_record(RAW)))

    def test_control_wrong_design_name(self):
        bit, app = prepared()
        with self.assertRaisesRegex(F.CfwError, "expected"):
            F.check(cfw_file(bit, app), "c64")

    def test_control_application_elsewhere(self):
        bit, _ = prepared()
        app = struct.pack("<III", 0x220000, len(RAW) + 3, 0x220000) + RAW + b"\0" * 3
        with self.assertRaisesRegex(F.CfwError, "0x30000"):
            F.check(wrap(bit + b"\0" * (-len(bit) % 4) + app))

    def test_control_gap_too_large(self):
        bit, app = prepared()
        with self.assertRaisesRegex(F.CfwError, "application record"):
            F.check(cfw_file(bit, app, gap=20))

    def test_control_truncated(self):
        bit, app = prepared()
        cfw = cfw_file(bit, app)
        for cut in (len(cfw) - 100, len(CODE) + 36 + 12 + 400):
            with self.assertRaises(F.CfwError):
                F.check(cfw[:cut])

    def test_control_not_a_cfw(self):
        bit, app = prepared()
        with self.assertRaisesRegex(F.CfwError, "pre-header"):
            F.check(cfw_file(bit, app)[36:])

    def test_control_bitstream_over_the_updater_slot(self):
        bit, app = prepared(body=(P.BIT_MAX // 4) + 10)
        with self.assertRaisesRegex(F.CfwError, "slot"):
            F.check(wrap(bit + b"\0" * (-len(bit) % 4) + app))

    def test_control_rename_off_fails_the_same_file(self):
        """The check can fail on the file that passes: the same core without the rename is refused."""
        bit, app = prepared()
        F.check(cfw_file(bit, app), "amiga1200")
        with self.assertRaises(F.CfwError):
            F.check(cfw_file(bit_file(), app), "amiga1200")


STUB = '''#!%(py)s
# stands in for tools/make_update.sh: puts the inputs it is given into a .cfw (%(mode)s)
import os, sys
sys.path.insert(0, %(here)r)
import test_cfw_core as T
a = sys.argv[1:]
out, pid = a[a.index("--out") + 1], a[a.index("--id") + 1]
bit, app = open(a[-2], "rb").read(), open(a[-1], "rb").read()
if %(mode)r == "unrenamed":
    bit = T.bit_file()
os.makedirs(os.path.join(out, "trx64-" + pid))
open(os.path.join(out, "trx64-" + pid, os.environ["TRX_UPDATE_NAME"] + ".cfw"), "wb").write(T.cfw_file(bit, app))
'''


class Pack(unittest.TestCase):
    """tools/pack_cfw_mc.sh with a stub for the container build."""

    def setUp(self):
        self.d = tempfile.TemporaryDirectory()
        self.bit = os.path.join(self.d.name, "amiga1200_mono.bit")
        open(self.bit, "wb").write(bit_file())
        self.bin = os.path.join(self.d.name, "coreapp.bin")
        open(self.bin, "wb").write(RAW)
        self.out = os.path.join(self.d.name, "out")

    def tearDown(self):
        self.d.cleanup()

    def run_it(self, mode="ok", core="amiga1200", bit=None):
        stub = os.path.join(self.d.name, "stub_%s.py" % mode)
        open(stub, "w").write(STUB % dict(py=sys.executable, here=HERE, mode=mode))
        os.chmod(stub, 0o755)
        env = dict(os.environ, MAKE_UPDATE=stub, TRX_CORE_DESC="cores/amiga1200/mono/core.toml")
        import subprocess
        return subprocess.run(["bash", os.path.join(HERE, "pack_cfw_mc.sh"), "--core", core, "--bit", bit or self.bit,
                               "--app-bin", self.bin, "--id", "main-deadbeef", "--bit-commit", "deadbeef",
                               "--app-commit", "cafebabe", "--out", self.out], capture_output=True, text=True, env=env)

    def test_packs_a_cfw_the_host_takes(self):
        r = self.run_it()
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        info, _ = F.check(open(os.path.join(self.out, "amiga1200.cfw"), "rb").read(), "amiga1200")
        self.assertEqual((info["design"], info["map"]), ("amiga1200", "identity"))
        self.assertFalse(os.path.exists(os.path.join(self.out, "amiga1200.ue2")))     # the .ue2 would install over the host

    def test_control_unrenamed_design_stops_the_packer(self):
        r = self.run_it("unrenamed")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("FAIL", r.stderr)
        self.assertFalse(os.path.exists(os.path.join(self.out, "amiga1200.cfw")))

    def test_control_core_without_cfw_delivery(self):
        r = self.run_it(core="trx64")
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("delivery", r.stderr)

    def test_control_not_a_bitstream(self):
        bad = os.path.join(self.d.name, "bad.bit")
        open(bad, "wb").write(b"\0" * 5000)
        r = self.run_it(bit=bad)
        self.assertNotEqual(r.returncode, 0)
        self.assertFalse(os.path.exists(os.path.join(self.out, "amiga1200.cfw")))


class Limits(unittest.TestCase):
    def test_constants_are_the_host_apps(self):
        h = open(os.path.join(ROOT, "host-app/software/hostapp/cfw.h")).read()
        for name in ("CFW_APP_ADDR", "CFW_APP_MAX", "CFW_BIT_MAX", "CFW_SCAN_MAX", "CFW_IDCODE_XC7A50T", "CFW_IDCODE_MASK"):
            m = re.search(r"#define\s+%s\s+(0x[0-9A-Fa-f]+)u?" % name, h)
            self.assertIsNotNone(m, name)
            self.assertEqual(int(m.group(1), 16), getattr(F, name), name)

    def test_real_amiga1200_cfw_if_given(self):
        p = os.environ.get("TRX_REAL_C128_CFW")
        if not p:
            self.skipTest("TRX_REAL_C128_CFW not set")
        info, _ = F.check(open(p, "rb").read(), "amiga1200")
        self.assertEqual(info["map"], "identity")


class Descriptor(unittest.TestCase):
    """tools/core_toml.py: delivery / mono_env (checked against a scratch cores/ directory)."""

    def setUp(self):
        self.d = tempfile.TemporaryDirectory()
        self.old = T.CORES
        T.CORES = self.d.name
        os.makedirs(os.path.join(self.d.name, "x", "mono"))
        open(os.path.join(self.d.name, "x", "mono", "build_mono.tcl"), "w").write("# x\n")

    def tearDown(self):
        T.CORES = self.old
        self.d.cleanup()

    def toml(self, extra="", app="blueprint"):
        cv = open(os.path.join(ROOT, "contract", "VERSION")).read().strip()
        open(os.path.join(self.d.name, "x", "core.toml"), "w").write(
            'id = "x"\nname = "X"\nversion = "0.1.0"\ncontract = %s\napp = "%s"\norder = 1\nexperimental = true\n'
            'sources = ["cores/x"]\n%s' % (cv, app, extra))

    def check(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            rc = T.check()
        return rc, out.getvalue()

    def test_cfw_core_is_accepted(self):
        self.toml('delivery = "cfw"\nmono_env = ["TRX_Z80=1", "TRX_DEFS=A B C"]\n')
        self.assertEqual(self.check()[0], 0)
        self.assertEqual(T.delivery_of(T.descriptors()[0][2]), "cfw")

    def test_default_is_cor(self):
        self.toml()
        self.assertEqual(self.check()[0], 0)
        self.assertEqual(T.delivery_of(T.descriptors()[0][2]), "cor")

    def test_control_unknown_delivery(self):
        self.toml('delivery = "zip"\n')
        rc, out = self.check()
        self.assertEqual(rc, 1)
        self.assertIn("delivery", out)

    def test_control_cfw_without_flat_build(self):
        self.toml('delivery = "cfw"\n')
        os.remove(os.path.join(self.d.name, "x", "mono", "build_mono.tcl"))
        self.assertIn("build_mono.tcl", self.check()[1])

    def test_control_cfw_with_another_app(self):
        self.toml('delivery = "cfw"\n', app="ultimate")
        self.assertIn("blueprint", self.check()[1])

    def test_cfw_core_without_application(self):
        self.toml('delivery = "cfw"\n', app="none")
        self.assertEqual(self.check()[0], 0)

    def test_control_mono_env_on_a_cor_core(self):
        self.toml('mono_env = ["TRX_Z80=1"]\n')
        self.assertIn("mono_env", self.check()[1])

    def test_control_bad_mono_env_entry(self):
        self.toml('delivery = "cfw"\nmono_env = ["trx z80"]\n')
        self.assertIn("NAME=value", self.check()[1])

    def test_layer2_extra_in_a_large_descriptor(self):
        self.toml()
        os.remove(os.path.join(self.d.name, "x", "core.toml"))
        cv = open(os.path.join(ROOT, "contract", "VERSION")).read().strip()
        open(os.path.join(self.d.name, "x", "mono", "core.toml"), "w").write(
            'id = "x"\nname = "X"\nversion = "0.1.0"\ncontract = %s\napp = "blueprint"\norder = 1\ndelivery = "cfw"\nexperimental = true\n'
            'sources = ["cores/x"]\nlayer2_extra = ["IEC_SRQ_I"]\n' % cv)
        self.assertEqual(self.check()[0], 0)

    def test_control_layer2_extra_in_a_partial_descriptor(self):
        self.toml('layer2_extra = ["IEC_SRQ_I"]\n')
        rc, out = self.check()
        self.assertEqual(rc, 1)
        self.assertIn("layer2_extra", out)

    def test_contract_ports_extra_only_without_partial_descriptor(self):
        import contract_ports as C
        old = C.ROOT
        C.ROOT = self.d.name
        try:
            os.makedirs(os.path.join(self.d.name, "cores", "y", "mono"))
            open(os.path.join(self.d.name, "cores", "y", "mono", "core.toml"), "w").write('id = "y"\nlayer2_extra = ["P_I"]\n')
            self.assertEqual(C.large_extra("y"), ["P_I"])
            open(os.path.join(self.d.name, "cores", "y", "core.toml"), "w").write('id = "y"\n')   # a partial descriptor too: no extra ports
            self.assertEqual(C.large_extra("y"), [])
        finally:
            C.ROOT = old

    def test_the_real_amiga1200(self):
        t = T.parse(os.path.join(ROOT, "cores", "amiga1200", "mono", "core.toml"))
        self.assertEqual((t["delivery"], t["app"], t["experimental"]), ("cfw", "blueprint", True))


if __name__ == "__main__":
    unittest.main(verbosity=1)
