#!/usr/bin/env python3
"""Host test for tools/app_build.py (T239.3 P3-2): the [sizes] table of a TOML B (defaults read from the headers, the range of each key, the coupling of two keys), the appsizes.h that
carries what a TOML B names, the record checked against the same table (the compiler and the reader refuse the same record), the split of the sources into the ones that depend on the
core and the ones that do not (from the compiler's own .d file, else from the text), and the flag list of the SDK against the core app Makefile. Unit and happy path.

  python3 tools/test_app_build.py        (also collected by python3 -m pytest tools/test_app_build.py)
"""
import os
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import app_build as A  # noqa: E402
import descrec as D  # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, ".."))
INC = [os.path.join(ROOT, "coreapp", "src")]
MAKEFILE = os.path.join(ROOT, "host-app", "target", "u64ii", "riscv", "coreapp", "Makefile")
APPFLAGS = os.path.join(ROOT, "app", "lib", "appflags.mk")
EXAMPLE = os.path.join(ROOT, "app", "sdkpkg", "example", "app")


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, "w").write(text)


def toml_b(body):
    d = tempfile.mkdtemp()
    p = os.path.join(d, "b.toml")
    write(p, '[app]\nname = "t"\n' + body)
    return p


class Sizes(unittest.TestCase):
    def test_every_default_is_the_headers(self):
        eff = A.effective({}, INC)
        self.assertEqual(eff["ops"], 400)
        self.assertEqual(eff["report_bytes"], 28672)
        self.assertEqual(eff["record_bytes"], 0x20000)
        self.assertEqual(eff["rec_values"], 8192)
        self.assertEqual(len(eff), len(A.SIZES))

    def test_the_shared_app_names_no_size(self):
        self.assertEqual(A.load_sizes(os.path.join(ROOT, "coreapp", "libs", "shared.toml"), INC), {})

    def test_appsizes_carries_what_is_named(self):
        p = toml_b("[sizes]\nops = 500\nrecord_bytes = 65536\nrec_ftypes = 20\n")
        out = tempfile.mkdtemp()
        named = A.cmd_sizes(p, out, INC)
        self.assertEqual(named, {"ops": 500, "record_bytes": 65536, "rec_ftypes": 20})
        text = open(os.path.join(out, "appsizes.h")).read()
        self.assertIn("#define OPS_MAX 500\n", text)
        self.assertIn("#define DESCREC_MAX_SIZE 65536U\n", text)
        self.assertIn("#define DR_FTYPES 20\n", text)
        self.assertNotIn("STATES_MAX", text)
        self.assertFalse([l for l in text.split("\n") if l.lstrip().startswith("//")])        # -include of assembler files too: no // comment line

    def test_every_key_names_a_macro_the_headers_define(self):
        for key, (macro, header, _, _) in A.SIZES.items():
            text = open(os.path.join(INC[0], header)).read()
            self.assertRegex(text, r"(?m)^#ifndef %s\b" % macro, key)         # the header lets the build override it


class Record(unittest.TestCase):
    def setUp(self):
        self.limits = dict(D.LIMITS)
        self.mods = {k: getattr(D, k) for k in ("MAX_SIZE", "KEYS_MAX", "WINDOWS_MAX", "BLOCKS_MAX", "PROPS_MAX", "FIELDS_MAX", "STATES_MAX")}

    def tearDown(self):
        D.LIMITS.clear()
        D.LIMITS.update(self.limits)
        for k, v in self.mods.items():
            setattr(D, k, v)

    def test_the_example_record_fits_the_example_sizes(self):
        rec = os.path.join(tempfile.mkdtemp(), "r.bin")
        D.apply_sizes(A.load_sizes(os.path.join(EXAMPLE, "toml_b.toml"), INC))
        open(rec, "wb").write(D.compile_checked(open(os.path.join(EXAMPLE, "desc.toml")).read(), None, what="desc.toml", maps={}))
        A.cmd_record(os.path.join(EXAMPLE, "toml_b.toml"), rec, INC)

    def test_apply_sizes_moves_the_compilers_limits(self):
        D.apply_sizes({"rec_binds": 900, "rec_keys": 80, "record_bytes": 0x40000, "ops": 5})
        self.assertEqual(D.LIMITS["bind"], 900)
        self.assertEqual(D.KEYS_MAX, 80)
        self.assertEqual(D.MAX_SIZE, 0x40000)


class Split(unittest.TestCase):
    def tree(self):
        d = tempfile.mkdtemp()
        inc = os.path.join(d, "inc")
        src = os.path.join(d, "src")
        write(os.path.join(inc, "ops.h"), "#define OPS_MAX 4\n")
        write(os.path.join(inc, "plain.h"), "int plain(void);\n")
        write(os.path.join(inc, "viaops.h"), '#include "ops.h"\n')
        write(os.path.join(src, "a.cc"), '#include "plain.h"\n')
        write(os.path.join(src, "b.cc"), '#include "viaops.h"\n')
        write(os.path.join(src, "c.c"), '#include <stdint.h>\n#include "plain.h"\n')
        listing = os.path.join(d, "incdirs.txt")
        write(listing, inc + "\n")
        return d, inc, src, listing

    def test_by_text(self):
        d, inc, src, listing = self.tree()
        got = A.sized([inc], [os.path.join(src, f) for f in ("a.cc", "b.cc", "c.c")])
        self.assertEqual([os.path.basename(x) for x in got], ["b.cc"])

    def test_split_writes_both_lists(self):
        d, inc, src, listing = self.tree()
        out = os.path.join(d, "out", "sdkbase")
        A.cmd_split([inc], out, [os.path.join(src, f) for f in ("a.cc", "b.cc")])
        self.assertEqual([os.path.basename(x.strip()) for x in open(os.path.join(out, "sized.real"))], ["b.cc"])
        self.assertEqual([os.path.basename(x.strip()) for x in open(os.path.join(out, "unsized.real"))], ["a.cc"])

    def test_split_leaves_the_code_of_trx64_ultimate_out(self):
        # V-4: the ring, the monitor, trx64u_ops, u64cfg and trxmon go to TRX64-Ultimate (check_d24.D12_TRX64U); the package's base holds none of them, in either list
        d, inc, src, listing = self.tree()
        top = os.path.join(d, "top")
        for rel in ("coreapp/src/ring.cc", "coreapp/src/trxmon/tm_x.c", "coreapp/src/a.cc"):
            write(os.path.join(top, rel), "int x;\n")
        out = os.path.join(d, "out2")
        os.makedirs(out)
        write(os.path.join(out, "root.txt"), top + "\n")
        A.cmd_split([inc], out, [os.path.join(top, rel) for rel in ("coreapp/src/ring.cc", "coreapp/src/trxmon/tm_x.c", "coreapp/src/a.cc")])
        got = [os.path.relpath(x.strip(), os.path.realpath(top)) for f in ("sized.real", "unsized.real") for x in open(os.path.join(out, f))]
        self.assertEqual(got, ["coreapp/src/a.cc"])
        gone = [os.path.relpath(x.strip(), os.path.realpath(top)) for x in open(os.path.join(out, "trx64u_code.real"))]
        self.assertEqual(sorted(gone), ["coreapp/src/ring.cc", "coreapp/src/trxmon/tm_x.c"])

    def test_the_core_apps_sources_split_into_both_groups(self):
        # the real include path: what the closure of the text says about the tree's own sources
        incs = [os.path.join(ROOT, d) for d in ("coreapp/src", "app/sdk", "app/loaders", "app/lib/cbm1541", "uicore", "coreapp/cores")]
        got = {os.path.basename(x) for x in A.sized(incs, [os.path.join(ROOT, "coreapp/src", f) for f in ("ops.cc", "descrec.cc", "diag.cc", "sha256.c", "heapcfg.c")])}
        self.assertEqual(got, {"ops.cc", "descrec.cc", "diag.cc"})


class CoreAppToml(unittest.TestCase):
    """V-4: the VIC-20's app is linked against its own TOML A and the blocks of the base's TOML A that its TOML B names (descrec.merge_toml_a)."""

    def test_the_vic20_driver_table_holds_the_blocks_its_two_files_name(self):
        app = os.path.join(ROOT, "cores", "vc20", "app")
        core = D.toml_loads(open(os.path.join(app, "gateware.toml")).read())
        base = D.toml_loads(open(os.path.join(ROOT, "coreapp", "gateware", "shared.toml")).read())
        names = D.toml_loads(open(os.path.join(app, "sdk.toml")).read())["app"]["base_blocks"]
        doc = D.resolve_maps(D.merge_toml_a(core, base, names, "t"), D.load_maps([os.path.join(ROOT, "app", "sdkpkg", "maps")]))
        head, _ = D.drivers_files(doc, "t")
        self.assertEqual(sorted(b["name"] for b in doc["block"]), sorted(names + [b["name"] for b in core["block"]]))
        for macro in ("DRV_HAS_cart_regs", "DRV_HAS_drive_a", "DRV_HAS_icap", "DRV_ident_caps_RD()", "DRV_cart_regs_mode_WR(v)", "DRV_machine_ram_ext_ADDR"):
            self.assertIn(macro, head)
        self.assertNotIn("DRV_HAS_cmd_if", head)                 # the command interface is no block of this app: the probe of hw.cc is gone with it
        self.assertEqual(doc["identity"]["id"], "VC20")          # the core's identity, not the shared app's
        D.ldfrag_doc(doc, "t")


class Flags(unittest.TestCase):
    def test_the_sdk_flags_are_the_makefiles(self):
        A.cmd_flags(MAKEFILE, APPFLAGS)



if __name__ == "__main__":
    unittest.main()
