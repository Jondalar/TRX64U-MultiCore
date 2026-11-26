#!/usr/bin/env python3
"""Host test for tools/libtab.py (T239.3 P3-1): the TOML B of the shared app loads and every file it names exists, its library table has one row per library with the entries
the TOML names, the build image's TOML reader (the subset) reads it as tomllib does, and the two checks (the generic sources name no library, the objects of a build use no
library symbol outside the table) pass a clean tree.

  python3 tools/test_libtab.py        (also collected by python3 -m pytest tools/test_libtab.py)
"""
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import descrec as D  # noqa: E402
import libtab as L  # noqa: E402
import sdk_api  # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, ".."))
SHARED = os.path.join(ROOT, "coreapp", "libs", "shared.toml")
HAVE_CC = shutil.which("cc") is not None and shutil.which("nm") is not None


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, "w").write(text)


def toml_b(body):
    d = tempfile.mkdtemp()
    p = os.path.join(d, "b.toml")
    write(p, '[app]\nname = "t"\n' + body)
    return p


class Tests(unittest.TestCase):
    """T239.5 D4: a library names its tests by the step of the matrix; `libtab.py tests` lists them per step, and the VIC-20's TOML B names its."""

    def test_the_tests_of_a_library_are_listed_by_step(self):
        p = toml_b('[[library]]\nname = "a"\nsources = ["a.cc"]\ntests = { unit = ["a1"], ui = ["a2", "a3"] }\n'
                   '[[library]]\nname = "b"\nsources = ["b.cc"]\ntests = { unit = ["b1"], api = ["b2"] }\n')
        out = lambda *k: subprocess.run([sys.executable, "-I", os.path.join(HERE, "libtab.py"), "tests", p] + list(k), capture_output=True, text=True, check=True).stdout.split()
        self.assertEqual(out(), ["a1", "a2", "a3", "b1", "b2"])
        self.assertEqual(out("unit"), ["a1", "b1"])
        self.assertEqual(out("ui"), ["a2", "a3"])
        self.assertEqual(out("service"), [])

    def test_a_wrong_kind_or_a_unit_named_twice_is_refused(self):
        for body, why in (('tests = { smoke = ["x"] }', "no kind 'smoke'"), ('tests = { unit = [] }', "list of unit names"),
                          ('tests = { unit = ["x"], ui = ["x"] }', "named twice")):
            p = toml_b('[[library]]\nname = "a"\nsources = ["a.cc"]\n' + body + "\n")
            with self.assertRaises(L.LibTabError) as e:
                L.load(p)
            self.assertIn(why, str(e.exception))

    def test_the_vc20_names_a_test_for_each_step(self):
        libs = L.load(os.path.join(ROOT, "cores", "vc20", "app", "sdk.toml"))
        kinds = {k for lib in libs for k in lib.get("tests", {})}
        self.assertEqual(kinds, set(L.TEST_KINDS))


class SharedApp(unittest.TestCase):
    def test_loads_and_files_exist(self):
        libs = L.load(SHARED)
        L.check_files(libs, ROOT)
        names = [x["name"] for x in libs]
        for need in ("c64cart", "cbm1541", "sid", "tempdisk", "cbmprg", "cbmrun", "dma", "t64", "mouse", "pad", "padkeys", "resample",
                     "newfile", "newdisk", "folderhd", "fastseek", "ftp"):
            self.assertIn(need, names)
        run = [x for x in libs if x["name"] == "cbmrun"][0]
        self.assertIn("c64_go.cc", run["sources"])                   # c64_go is part of cbmrun (P1-8)

    def test_the_new_file_and_folder_libraries_are_whole(self):
        libs = {x["name"]: x for x in L.load(SHARED)}
        self.assertEqual(libs["newdisk"]["needs"], ["newfile"])                  # the image builders place their file by newfile
        self.assertIn("newfile", libs["cbm1541"]["needs"])                       # so does the 1541's Create and Save
        self.assertEqual(sorted(libs["folderhd"]["sources"]), ["amiga_ffs.cc", "folderhd.cc", "folderhd_exports.cc", "folderhd_fold.cc"])
        self.assertEqual(libs["ftp"]["sources"], ["ftp_client.cc", "ftp_exports.cc"])
        srcs = [f for x in libs.values() for f in x["sources"]]
        for generic in ("textcopy.cc", "sectors.cc", "winslot.cc", "netfs.cc", "fm_fs.cc"):   # the files that generic code needs stay out of every library
            self.assertNotIn(generic, srcs)

    def test_table_has_a_row_per_library_with_its_entries(self):
        # the columns of a LibEntry row (libtab.h): name, bind, register_ops, capability, init, start, freeze_request, core, reg, heap_keep, heap_peak; no `table` column any more
        libs = L.load(SHARED)
        t = L.table_text(libs)
        N = "U2A3_BUDGET_NONE, U2A3_BUDGET_NONE"
        self.assertEqual(t.count('\n    { "'), len(libs) + len(L.slots(libs)))                  # one row per library, then one LibSlot row per export
        self.assertIn('{ "cbm1541", cbm1541_bind, cbm1541_register, cap_cbm1541, 0, 0, 0, 0, 0, %s },' % N, t)
        self.assertIn('static const char *const cap_cbm1541[] = { "drive_a", 0 };', t)
        self.assertNotIn('"uci"', t)                                                   # the command interface is TRX64-Ultimate's library (D49)
        self.assertIn('{ "dma", 0, 0, 0, 0, dmasvc_start, 0, 0, 0, %s },' % N, t)
        self.assertIn('{ "newfile", 0, 0, 0, 0, 0, 0, 0, 0, %s },' % N, t)              # a library without an entry: its code is called by the libraries that need it
        self.assertIn('{ "newdisk", 0, newdisk_register, cap_newdisk, 0, 0, 0, 0, 0, %s },' % N, t)
        self.assertIn('static const char *const cap_newdisk[] = { "floppy", "sio", 0 };', t)       # L-1b: an entry names several capabilities (a record-described atari800 names "sio")
        self.assertIn('{ "folderhd", folderhd_bind, 0, 0, 0, 0, 0, 0, 0, %s },' % N, t)
        self.assertIn('{ "fastseek", fastseek_bind, 0, 0, 0, 0, 0, 0, 0, %s },' % N, t)
        self.assertIn('{ "ftp", ftp_bind, 0, 0, 0, 0, 0, 0, 0, %s },' % N, t)
        self.assertIn('{ "mouse", 0, 0, 0, mouse_lib_init, 0, 0, 0, 0, %s },' % N, t)           # the gamepad, mouse and pad-key libraries (L-1): the USB hook at init, the XInput claim at the core
        self.assertIn('{ "pad", 0, 0, 0, pad_lib_init, 0, 0, pad_lib_core, 0, %s },' % N, t)
        self.assertIn('{ "padkeys", 0, 0, 0, 0, 0, 0, 0, 0, %s },' % N, t)
        self.assertIn('{ "resample", 0, 0, 0, 0, 0, 0, 0, 0, %s },' % N, t)
        self.assertIn('{ "miniz", 0, 0, 0, 0, 0, 0, 0, 0, %s },' % N, t)
        self.assertIn("const void *mouse_table(void);", t)                              # the table function of an export is declared once
        self.assertIn("void pad_lib_core(CoreCtx *, const CoreDesc *, const DescRecord *);", t)
        self.assertIn("void tempdisk_lib_init(const BoardProfile &);", t)
        self.assertIn("const unsigned g_libtab_n = %d;" % len(libs), t)
        self.assertEqual(L.mk_text(libs).split()[0:2], ["LIB_SRCS_CC", "="])

    def test_an_entry_names_one_capability_or_several(self):
        p = toml_b('[[library]]\nname = "a"\nsources = ["newdisk.cc"]\nregister_ops = "newdisk_register"\ncapability = ["floppy", "sio"]\n'
                   '[[library]]\nname = "b"\nsources = ["newfile.cc"]\nregister_ops = "newfile_register"\ncapability = "iec"\n')
        t = L.table_text(L.load(p))
        self.assertIn('static const char *const cap_a[] = { "floppy", "sio", 0 };', t)
        self.assertIn('static const char *const cap_b[] = { "iec", 0 };', t)
        self.assertIn('{ "a", 0, newdisk_register, cap_a, 0, 0, 0, 0, 0, U2A3_BUDGET_NONE, U2A3_BUDGET_NONE },', t)

    def test_the_old_table_key_is_refused_for_exports(self):
        # base 4.0 (libslot.h): `table = "x_table"` of a library is gone; the table is named under exports
        p = toml_b('[[library]]\nname = "a"\nsources = ["a.cc"]\ntable = "a_table"\n')
        with self.assertRaises(L.LibTabError) as e:
            L.load(p)
        self.assertIn("the key table is gone", str(e.exception))
        self.assertIn("exports", str(e.exception))

    def test_exports_are_written_into_g_libslot_and_libslots_h(self):
        p = toml_b('[[library]]\nname = "a"\nsources = ["a.cc"]\nexports = [{ name = "alpha", major = 1, table = "alpha_table" }, { name = "beta", major = 2, table = "beta_table" }]\n'
                   '[[library]]\nname = "b"\nsources = ["b.cc"]\nexports = [{ name = "gamma", major = 3, table = "gamma_table" }]\n')
        libs = L.load(p)
        t = L.table_text(libs)
        self.assertIn("const LibSlot g_libslot[] = {", t)
        self.assertIn('{ "a", "alpha", 1u, alpha_table, 0u },', t)
        self.assertIn('{ "a", "beta", 2u, beta_table, 0u },', t)
        self.assertIn('{ "b", "gamma", 3u, gamma_table, 0u },', t)
        self.assertIn("const unsigned g_libslot_n = 3;", t)
        self.assertIn("const void *g_libslot_bound[3];", t)
        self.assertIn("const void *alpha_table(void);", t)
        self.assertLess(t.index('"alpha"'), t.index('"beta"'))                          # the order of the file is the order of the slots
        h = L.slots_text(libs)
        for line in ("#define LIBSLOT_alpha 0", "#define LIBSLOT_alpha_MAJOR 1", "#define LIBSLOT_beta 1", "#define LIBSLOT_beta_MAJOR 2", "#define LIBSLOT_gamma 2",
                     "#define LIBSLOT_gamma_MAJOR 3", "#define LIBSLOT_COUNT 3"):
            self.assertIn(line, h)
        # no library exports anything: one placeholder row, so the table is not an empty array
        t0 = L.table_text(L.load(toml_b('[[library]]\nname = "a"\nsources = ["a.cc"]\n')))
        self.assertIn("const unsigned g_libslot_n = 0;", t0)
        self.assertIn("{ 0, 0, 0, 0, 0 },", t0)

    def test_a_register_librarys_export_needs_no_table(self):
        # a library with `register` delivers its table at start (LibRecord::export_list): the slot is at_start 1 and its function reads g_libslot_bound
        p = toml_b('[[library]]\nname = "a"\nsources = ["a.cc"]\nexports = [{ name = "alpha", major = 1, table = "alpha_table" }]\n'
                   '[[library]]\nname = "r"\nsources = ["r.cc"]\nregister = "r_register"\nexports = [{ name = "rho", major = 1 }]\n')
        libs = L.load(p)
        t = L.table_text(libs)
        self.assertIn("static const void *slot_bound_1(void) { return g_libslot_bound[1]; }", t)
        self.assertIn('{ "r", "rho", 1u, slot_bound_1, 1u },', t)
        self.assertIn('{ "a", "alpha", 1u, alpha_table, 0u },', t)                      # a library without register keeps at_start 0
        self.assertIn('{ "r", 0, 0, 0, 0, 0, 0, 0, r_register, U2A3_BUDGET_NONE, U2A3_BUDGET_NONE },', t)
        self.assertIn("const unsigned g_libtab_link_order[] = { 1 };", t)
        # without register an export has to name its table
        with self.assertRaises(L.LibTabError) as e:
            L.load(toml_b('[[library]]\nname = "a"\nsources = ["a.cc"]\nexports = [{ name = "alpha", major = 1 }]\n'))
        self.assertIn("names no table function", str(e.exception))
        # with register an export may not name one
        with self.assertRaises(L.LibTabError):
            L.load(toml_b('[[library]]\nname = "r"\nsources = ["r.cc"]\nregister = "r_register"\nexports = [{ name = "rho", major = 1, table = "rho_table" }]\n'))

    def test_libtab_mk_splits_c_and_cc_sources(self):
        p = toml_b('[[library]]\nname = "a"\nsources = ["a.cc", "a1.c"]\n[[library]]\nname = "b"\nsources = ["b.c", "b.cc"]\n')
        mk = L.mk_text(L.load(p)).split("\n")
        self.assertEqual(mk[0], "LIB_SRCS_CC = a.cc b.cc")
        self.assertEqual(mk[1], "LIB_SRCS_C = a1.c b.c")
        shared = dict(l.split(" = ", 1) for l in L.mk_text(L.load(SHARED)).strip().split("\n"))
        self.assertIn("miniz.c", shared["LIB_SRCS_C"].split())                          # miniz is plain C
        self.assertIn("miniz_lib.cc", shared["LIB_SRCS_CC"].split())
        self.assertNotIn(".c ", shared["LIB_SRCS_CC"] + " ")

    def test_the_four_libraries_of_l_1_are_listed_with_their_headers(self):
        libs = {x["name"]: x for x in L.load(SHARED)}
        for name in ("mouse", "pad", "padkeys", "resample"):
            self.assertIn(name, libs)
            self.assertTrue(libs[name]["sources"], name)
        for name, api in (("mouse", "mouse_api.h"), ("pad", "pad_api.h"), ("padkeys", "padkeys_api.h")):
            self.assertIn(api, libs[name]["headers"])
            self.assertIn(api, [os.path.basename(h) for h in sdk_api.headers(ROOT)], "%s is a public header of the SDK" % api)    # the package's include/ carries it (tools/sdk_pack.sh copies this list)

    def test_every_entry_is_defined_in_a_source_of_its_library(self):
        import re
        for lib in L.load(SHARED):
            text = ""
            for f in lib["sources"]:
                text += open(L.find_file(ROOT, f)).read()
            for key in L.ENTRIES:
                if key in lib:
                    self.assertRegex(text, r"(^|\n)[A-Za-z_][^\n;]*\b%s\(" % re.escape(lib[key]), "%s: %s = %s is defined in none of its sources" % (lib["name"], key, lib[key]))

    def test_the_mouse_pad_and_padkeys_libraries_give_the_generic_code_a_table(self):
        libs = {x["name"]: x for x in L.load(SHARED)}
        for name, table in (("mouse", "mouse_table"), ("pad", "pad_table"), ("padkeys", "padkeys_table")):
            self.assertNotIn("table", libs[name])                                                   # the old library key is gone: the table is an export
            self.assertEqual(libs[name]["exports"], [{"name": name, "major": 1, "table": table}])
            self.assertTrue(any(h.endswith("_api.h") for h in libs[name]["headers"]), name)          # the table's header: the seam the generic code reads through
        self.assertEqual(libs["pad"]["core"], "pad_lib_core")
        self.assertIn("u64_resample.cc", libs["resample"]["sources"])
        for gen in ("coreapp/src/engines.cc", "coreapp/src/baseset.cc", "coreapp/src/hotkeys.cc", "coreapp/src/ops_glue.cc", "coreapp/src/run.cc"):    # the generic users name the tables, no function of the libraries
            text = open(os.path.join(ROOT, gen)).read()
            self.assertNotRegex(text, r"\b(mouse_sink_\w+|pad_(pace|reset|register)|padkey_row|usb_xinput_set_sink)\(", gen)

    def test_heap_figures_of_a_library(self):
        ok = toml_b('[[library]]\nname = "a"\nsources = ["x.cc"]\nheap_keep = 100\nheap_peak = 200\n')
        self.assertEqual(L.load(ok)[0]["heap_keep"], 100)

    def test_subset_reader_agrees_with_tomllib(self):
        text = open(SHARED).read()
        try:
            import tomllib
        except ImportError:
            self.skipTest("no tomllib")
        self.assertEqual(D._Toml(text).parse(), tomllib.loads(text))

    def test_the_generic_sources_name_no_library(self):
        self.assertEqual(L.lint(L.load(SHARED), ROOT), [])


class Planted(unittest.TestCase):
    def tree(self):
        d = tempfile.mkdtemp()
        write(os.path.join(d, "coreapp/src/libfoo.cc"), "int foo_value(void) { return 7; }\nvoid foo_bind(void) {}\n")
        write(os.path.join(d, "coreapp/src/libfoo.h"), "int foo_value(void);\n")
        write(os.path.join(d, "uicore/ui.c"), "int ui_x(void) { return 1; }\n")
        return d

    def libs(self, d):
        p = os.path.join(d, "b.toml")
        write(p, '[app]\nname = "t"\n[[library]]\nname = "foo"\nsources = ["libfoo.cc"]\nheaders = ["libfoo.h"]\nbind = "foo_bind"\n')
        return L.load(p)

    def test_lint(self):
        d = self.tree()
        libs = self.libs(d)
        write(os.path.join(d, "coreapp/src/clean.cc"), '#include "other.h"\nint c(void) { return 1; }\n')
        self.assertEqual(L.lint(libs, d), [])

    @unittest.skipUnless(HAVE_CC, "needs cc and nm")
    def test_symcheck(self):
        d = self.tree()
        libs = self.libs(d)
        o = os.path.join(d, "obj")
        os.makedirs(o)
        write(os.path.join(d, "coreapp/src/plain.c"), "int p(void) { return 2; }\n")
        write(os.path.join(d, "libfoo.c"), "int foo_value(void) { return 7; }\nvoid foo_bind(void) {}\n")
        for src, obj in (("libfoo.c", "libfoo.o"), ("coreapp/src/plain.c", "plain.o")):
            subprocess.check_call(["cc", "-c", "-o", os.path.join(o, obj), os.path.join(d, src)])
        self.assertEqual(L.symcheck(libs, o, d), ([], []))


VC20_B = os.path.join(ROOT, "cores", "vc20", "app", "sdk.toml")


class Vic20App(unittest.TestCase):
    """T239.3 V-4 (S11, S12): the TOML B of the VIC-20's own app."""

    def test_the_toml_b_loads_and_its_files_exist(self):
        libs = L.load(VC20_B)
        self.assertEqual([lib["name"] for lib in libs], ["vc20", "cbm1541", "cbmrun", "cbmprg", "newfile", "miniz"])   # newfile: cbm1541 needs it (L-2, S12); miniz: unzip's need (D58)
        own = os.path.join(ROOT, "cores", "vc20", "app", "lib")
        for lib in libs:
            for f in lib["sources"] + lib.get("headers", []):
                self.assertTrue(os.path.isfile(os.path.join(own, f)) or L.find_file(ROOT, f), "%s of library %s is in none of the source directories" % (f, lib["name"]))
        app = D.toml_loads(open(VC20_B).read())["app"]
        self.assertEqual(app["desc"], "vc20_desc")
        self.assertIn("const CoreDesc vc20_desc", open(os.path.join(own, "vc20.cc")).read())
        self.assertTrue(os.path.isfile(os.path.join(ROOT, "cores", "vc20", "app", app["toml_a"])))
        for f in app["base_sources"]:
            self.assertTrue(os.path.isfile(os.path.join(ROOT, "coreapp", "cores", f)))

    def test_it_names_no_library_the_core_does_not_use(self):
        # S11: the shared app links every library; the VIC-20 has the drive, the PRG run and its own code, nothing of the cartridge, UCI, SID, DMA, the temp disk, T64 or online search
        names = {lib["name"] for lib in L.load(VC20_B)}
        self.assertEqual(names & {"c64cart", "uci", "sid", "dma", "tempdisk", "t64", "online"}, set())

    def test_the_drive_library_is_the_shared_one(self):
        # S12: the same sources and entries as the shared app's, which trx64u's own image links
        self.assertEqual(L.samelib(L.load(SHARED), L.load(VC20_B), "cbm1541"), [])

    def test_every_own_app_links_the_zip_library_of_the_shared_app(self):
        # T239.3 D58 (base 4.0 loads no library file): Unzip stays in every core with an app of its own; unzip's need miniz>=1.0 is met by the library its TOML B links
        own = sorted(d for d in os.listdir(os.path.join(ROOT, "cores")) if os.path.isfile(os.path.join(ROOT, "cores", d, "app", "sdk.toml")))
        self.assertGreater(len(own), 1)
        for core in own:
            self.assertEqual(L.samelib(L.load(SHARED), L.load(os.path.join(ROOT, "cores", core, "app", "sdk.toml")), "miniz"), [], core)

    def test_a_link_map_with_what_the_toml_b_names_passes(self):
        libs = L.load(VC20_B)
        objs = ["obj/%s.o" % os.path.splitext(f)[0] for lib in libs for f in lib["sources"]] + ["obj/portdiag.o", "obj/table.o", "lib/libtrxbase.a(ops.o)", "lib/libtrxbase.a(settings.o)"]
        text = "Linker script and memory map\n" + "".join(" .text 0x1000 0x20 %s\n" % o for o in objs)
        self.assertEqual(L.mapcheck(libs, L.load(SHARED), text, ROOT, ["portdiag.cc"]), [])
        self.assertEqual(L.samelib(L.load(SHARED), libs, "cbm1541", [], text=lambda p: text), [])


if __name__ == "__main__":
    unittest.main()


class LinkOrderTests(unittest.TestCase):
    """Base 4.0 (P2-6): the base links the libraries with `register` after the libraries their needs name; a cycle is refused with both names."""

    @staticmethod
    def lib(name, needs=(), register=True):
        d = {"name": name, "sources": [name + ".cc"], "needs": list(needs)}
        if register:
            d["register"] = name + "_register"
        return d

    def test_trx64u_toml_b_links_in_the_order_of_its_needs(self):
        base = [self.lib(n, register=False) for n in ("c64cart", "cbm1541", "uci", "tempdisk", "cbmprg")]
        theirs = [self.lib("trx64u", ["c64cart", "cbm1541", "uci", "tempdisk", "cbmprg", "ring"]), self.lib("monitor", ["ring"]),
                  self.lib("ring", ["c64cart", "cbm1541", "uci"]), self.lib("online")]
        order = [l["name"] for l in L.link_order(base + theirs)]
        self.assertLess(order.index("ring"), order.index("trx64u"))         # a need later in the file comes first
        self.assertLess(order.index("ring"), order.index("monitor"))
        self.assertLess(order.index("trx64u"), order.index("monitor"))        # ties keep the order of the file
        self.assertLess(order.index("monitor"), order.index("online"))
        t = L.table_text(base + theirs)
        regs = [i for i, l in enumerate(base + theirs) if "register" in l]
        self.assertIn("const unsigned g_libtab_link_order[] = { 7, 5, 6, 8 };", t)
        self.assertEqual(len(regs), 4)

    def test_a_cycle_of_needs_is_refused_with_both_names(self):
        with self.assertRaises(L.LibTabError) as e:
            L.link_order([self.lib("a", ["b"]), self.lib("b", ["a"])])
        self.assertIn("'a'", str(e.exception))
        self.assertIn("'b'", str(e.exception))
