#!/usr/bin/env python3
"""The library table of an app (T239.3 D31, P3-1): reads the TOML B of a core's app, which names the libraries the app is linked with.

  libtab.py table TOML_B OUT_DIR     writes libtab_table.cc (the table libtab.cc walks, the export slots g_libslot), libslots.h (LIBSLOT_<name>) and libtab.mk (LIB_SRCS_CC, LIB_SRCS_C: the sources to compile),
                                     each only when its content changes
  libtab.py sources TOML_B           the source file names of every library, one per line
  libtab.py lint TOML_B ROOT         no generic source (coreapp/src, uicore) includes a header of a library or names an entry symbol of one
  libtab.py symcheck TOML_B OBJDIR ROOT
                                     nm over the objects of a build: no generic object has an undefined symbol that a library object defines (only libtab_table's entries may)
  libtab.py samelib TOML_B_1 TOML_B_2 NAME [MAP ...]
                                     S12: the library NAME is the same (sources, headers, entries) in the two TOML B files, and every source of it is an input of each link map given
  libtab.py tests TOML_B [KIND]      T239.5 D4: the test units the TOML B's libraries name (`tests`), one per line in the file's order; KIND unit|service|ui|api
                                     gives the units of that step of the matrix only (2 unit tests, 8 mock bitstream -> service, 9 service -> UI, 10 service -> API)
  libtab.py mapcheck TOML_B MAP ROOT [REF_TOML_B]
                                     the link map of an app (T239.3 V-4, S11, S12): every source of the TOML B's libraries is an input of the link, no source of a library of REF_TOML_B
                                     (default ROOT/coreapp/libs/shared.toml) that this TOML B does not name is, and no source of the cores' code (coreapp/cores, cores/*/app/lib) is except
                                     the libraries' and the [app] base_sources of this TOML B. The link holds what the TOML B names and the base

A library: name, sources, headers (the headers that name its functions), and the entries it hands the generic application: bind, register_ops (with capability: a name, or a list of names: it runs for a core that names any of them), init, start,
freeze_request, core, register (base 4.0: a library from outside this repository, `int fn(const LibApi *, LibRecord *)`, the file interface of lib_register: the base calls it at
start where it loaded a library file and takes its record with the same checks, libload.h LibLoader::link). `exports` (base 4.0, libslot.h) lists the tables the library exports,
each { name, major, table }: `table` names `const void *fn(void)`; a library with `register` leaves it out and delivers the table in LibRecord::export_list at start. `table` writes
libslots.h (LIBSLOT_<name>, LIBSLOT_<name>_MAJOR, LIBSLOT_COUNT) beside libtab_table.cc (g_libtab, g_libslot); `lint` refuses a source that names LIBSLOT_<x> of a library its `needs` do not list.
`menu` (a library with `register`) is the path of its menu record (an app.toml-style TOML, relative to the TOML B), which tools/cor_pack.py packs into the .cor (record 7).
`needs` names other libraries of the file. `heap_keep` and `heap_peak` (bytes, optional) are the heap the linked library holds when idle and at most at once
(the figures of a .u2a budget, P4-1): tools/cor_pack.py sums the first against the core's heap and reports both; the base checks both for a library with `register`. `tests` (T239.5 D4, optional) is an inline table that
names the library's tests by the step of the matrix that runs them: unit, service, ui, api, each a list of units of tests/coreapp/run.sh (a unit is named once in the file).
See coreapp/src/libtab.h for what each entry is."""
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import descrec  # noqa: E402

ENTRIES = {  # key -> (return, parameters) of the function the entry names
    "bind": ("void", "CoreCtx *, LibLoader &"),
    "register_ops": ("void", "OpRegistry &"),
    "init": ("void", "const BoardProfile &"),
    "start": ("void", "CoreCtx *"),
    "freeze_request": ("bool", "void"),
    "core": ("void", "CoreCtx *, const CoreDesc *, const DescRecord *"),
    "register": ("int", "const LibApi *, LibRecord *"),
}
KEYS = {"name", "sources", "headers", "needs", "capability", "heap_keep", "heap_peak", "tests", "exports", "menu"} | set(ENTRIES)
EXPORT_NAME = re.compile(r"^[a-z0-9_]{1,15}$")
TEST_KINDS = ("unit", "service", "ui", "api")   # T239.5 D4, D8: the steps 2, 8, 9 and 10 of a core lane
SRC_DIRS = ("coreapp/src", "coreapp/cores", "app/loaders", "app/lib/cbm1541")
GENERIC_DIRS = ("coreapp/src", "uicore", "app/lib/cbm1541", "app/loaders", "app/view", "app/edit", "app/video")   # every directory the build compiles generic code from; a source of TOML B or of the TRX64-Ultimate files is not generic
IDENT = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
FILE = re.compile(r"^[A-Za-z0-9_]+\.(cc|c|h)$")


class LibTabError(Exception):
    pass


def load(path):
    try:
        with open(path) as fh:
            doc = descrec.toml_loads(fh.read())
    except (descrec.DescError, OSError) as e:
        raise LibTabError("%s: %s" % (path, e))
    for k in doc:
        if k not in ("app", "library", "sizes"):             # [sizes]: the maxima of the app (tools/app_build.py checks them)
            raise LibTabError("%s: unknown table '%s'" % (path, k))
    libs = doc.get("library", [])
    names, srcs = [], {}
    for lib in libs:
        for k in lib:
            if k == "table":
                raise LibTabError("%s: library '%s': the key table is gone (base 4.0, libslot.h): name each table under exports = [{ name, major, table }]" % (path, lib.get("name", "?")))
            if k not in KEYS:
                raise LibTabError("%s: library '%s': unknown key '%s'" % (path, lib.get("name", "?"), k))
        n = lib.get("name")
        if not isinstance(n, str) or not re.match(r"^[a-z0-9_]{1,15}$", n):
            raise LibTabError("%s: a library needs a name of 1 to 15 characters of [a-z0-9_]" % path)
        if n in names:
            raise LibTabError("%s: library '%s' is listed twice" % (path, n))
        names.append(n)
        for key in ("sources", "headers", "needs"):
            v = lib.get(key, [])
            if not isinstance(v, list) or not all(isinstance(x, str) for x in v):
                raise LibTabError("%s: library '%s': %s is a list of strings" % (path, n, key))
        if not lib.get("sources"):
            raise LibTabError("%s: library '%s' has no sources" % (path, n))
        for f in lib["sources"] + lib.get("headers", []):
            if not FILE.match(f):
                raise LibTabError("%s: library '%s': '%s' is no file name" % (path, n, f))
        for f in lib["sources"]:
            if f in srcs:
                raise LibTabError("%s: source %s is in library '%s' and in '%s'" % (path, f, srcs[f], n))
            srcs[f] = n
        for key in ENTRIES:
            if key in lib and not (isinstance(lib[key], str) and IDENT.match(lib[key])):
                raise LibTabError("%s: library '%s': %s is a function name" % (path, n, key))
        for key in ("heap_keep", "heap_peak"):
            if key in lib and (isinstance(lib[key], bool) or not isinstance(lib[key], int) or lib[key] < 0):
                raise LibTabError("%s: library '%s': %s is a number of bytes" % (path, n, key))
        if "heap_keep" in lib and "heap_peak" in lib and lib["heap_keep"] > lib["heap_peak"]:
            raise LibTabError("%s: library '%s': heap_keep %d is above heap_peak %d" % (path, n, lib["heap_keep"], lib["heap_peak"]))
        if "capability" in lib and "register_ops" not in lib:
            raise LibTabError("%s: library '%s': a capability belongs to register_ops" % (path, n))
        if "capability" in lib:
            caps = lib["capability"] if isinstance(lib["capability"], list) else [lib["capability"]]
            if not caps or not all(isinstance(c, str) and re.match(r"^[a-z0-9_]+$", c) for c in caps) or len(set(caps)) != len(caps):
                raise LibTabError("%s: library '%s': capability is a name, or a list of different names" % (path, n))
        if "menu" in lib and not (isinstance(lib["menu"], str) and lib["menu"] and "register" in lib):
            raise LibTabError("%s: library '%s': menu is the path of a menu record, for a library with register" % (path, n))
        ex = lib.get("exports", [])
        if not isinstance(ex, list) or not all(isinstance(e, dict) for e in ex):
            raise LibTabError("%s: library '%s': exports is a list of inline tables { name, major, table }" % (path, n))
        for e in ex:
            for k in e:
                if k not in ("name", "major", "table"):
                    raise LibTabError("%s: library '%s': an export has no key '%s' (name, major, table)" % (path, n, k))
            if not isinstance(e.get("name"), str) or not EXPORT_NAME.match(e["name"]):
                raise LibTabError("%s: library '%s': an export needs a name of 1 to 15 characters of [a-z0-9_]" % (path, n))
            if isinstance(e.get("major"), bool) or not isinstance(e.get("major"), int) or e["major"] < 1:
                raise LibTabError("%s: library '%s': export %s needs a major from 1" % (path, n, e["name"]))
            if "table" in e and not (isinstance(e["table"], str) and IDENT.match(e["table"])):
                raise LibTabError("%s: library '%s': export %s: table is a function name" % (path, n, e["name"]))
            if "table" not in e and "register" not in lib:
                raise LibTabError("%s: library '%s': export %s names no table function (only a library with register delivers its tables at start)" % (path, n, e["name"]))
            if "table" in e and "register" in lib:
                raise LibTabError("%s: library '%s': export %s names a table function; a library with register delivers its tables at start (LibRecord::export_list), table is for a library without register" % (path, n, e["name"]))
    seen = {}
    for lib in libs:
        for e in lib.get("exports", []):
            if e["name"] in seen:
                raise LibTabError("%s: the export %s is in library '%s' and in '%s'" % (path, e["name"], seen[e["name"]], lib["name"]))
            seen[e["name"]] = lib["name"]
    units = {}
    for lib in libs:
        t = lib.get("tests")
        if t is None:
            continue
        if not isinstance(t, dict) or not t:
            raise LibTabError("%s: library '%s': tests is an inline table of %s" % (path, lib["name"], ", ".join(TEST_KINDS)))
        for kind, kind_units in t.items():
            if kind not in TEST_KINDS:
                raise LibTabError("%s: library '%s': tests has no kind '%s' (the kinds: %s)" % (path, lib["name"], kind, ", ".join(TEST_KINDS)))
            if not isinstance(kind_units, list) or not kind_units or not all(isinstance(x, str) and re.match(r"^[a-z0-9_]+$", x) for x in kind_units):
                raise LibTabError("%s: library '%s': tests.%s is a list of unit names" % (path, lib["name"], kind))
            for x in kind_units:
                if x in units:
                    raise LibTabError("%s: the test unit %s is named twice (library '%s' and '%s')" % (path, x, units[x], lib["name"]))
                units[x] = lib["name"]
    for lib in libs:
        for need in lib.get("needs", []):
            if need not in names:
                raise LibTabError("%s: library '%s' needs '%s', which the file does not list" % (path, lib["name"], need))
    try:
        link_order(libs)
    except LibTabError as e:
        raise LibTabError("%s: %s" % (path, e))
    return libs


def link_order(libs):
    """Base 4.0: the order in which the base links the libraries (LibLoader::link for those with `register`): every library after the libraries its `needs` name, a need may
    point to a library later in the file, ties keep the order of the file. A cycle is refused with two of its names. Returns the libraries in that order."""
    by = {lib["name"]: lib for lib in libs}
    state, out = {}, []

    def visit(lib, via):
        n = lib["name"]
        if state.get(n) == 2:
            return
        if state.get(n) == 1:
            raise LibTabError("library '%s' and library '%s' need each other (a cycle of needs)" % (via, n))
        state[n] = 1
        for need in lib.get("needs", []):
            visit(by[need], n)
        state[n] = 2
        out.append(lib)

    for lib in libs:
        visit(lib, lib["name"])
    return out


def find_file(root, name):
    for d in SRC_DIRS:
        p = os.path.join(root, d, name)
        if os.path.isfile(p):
            return p
    return None


def check_files(libs, root):
    for lib in libs:
        for f in lib["sources"] + lib.get("headers", []):
            if not find_file(root, f):
                raise LibTabError("library '%s': %s is in none of %s" % (lib["name"], f, ", ".join(SRC_DIRS)))


def slots(libs):
    """The exports of the file in its order (libslot.h): [(slot, library, export)]."""
    out = []
    for lib in libs:
        for e in lib.get("exports", []):
            out.append((len(out), lib, e))
    return out


def table_text(libs, source="TOML B"):
    decl = []
    rows = []
    caps = []
    for lib in libs:
        for key, (ret, par) in ENTRIES.items():
            if key in lib:
                decl.append('extern "C" %s %s(%s);' % (ret, lib[key], par) if key == "register" else "%s %s(%s);" % (ret, lib[key], par))   # a register library may be C (LIB_SRCS_C)
        cell = []
        for key in ("bind", "register_ops"):
            cell.append(lib[key] if key in lib else "0")
        if "capability" in lib:
            names = lib["capability"] if isinstance(lib["capability"], list) else [lib["capability"]]
            caps.append("static const char *const cap_%s[] = { %s, 0 };" % (lib["name"], ", ".join('"%s"' % c for c in names)))
            cell.append("cap_%s" % lib["name"])
        else:
            cell.append("0")
        for key in ("init", "start", "freeze_request", "core", "register"):
            cell.append(lib[key] if key in lib else "0")
        for key in ("heap_keep", "heap_peak"):
            cell.append("%du" % lib[key] if key in lib else "U2A3_BUDGET_NONE")
        rows.append('    { "%s", %s },' % (lib["name"], ", ".join(cell)))
    srows, sfn = [], []
    for i, lib, e in slots(libs):
        fn = e.get("table")
        if fn:
            decl.append("const void *%s(void);" % fn)
        else:                                          # a register library delivers the table at start (LibRecord::export_list); the base writes it into g_libslot_bound
            fn = "slot_bound_%d" % i
            sfn.append("static const void *%s(void) { return g_libslot_bound[%d]; }" % (fn, i))
        srows.append('    { "%s", "%s", %du, %s, %du },' % (lib["name"], e["name"], e["major"], fn, 0 if e.get("table") else 1))
    out = ["// Generated by tools/libtab.py from %s (T239.3 D31): the libraries the app is linked with and their exports (libslot.h). Do not edit." % source,
           '#include "libtab.h"', '#include "libslot.h"', ""]
    out += sorted(set(decl)) + [""] + caps + ["", "const LibEntry g_libtab[] = {"] + rows
    if not rows:
        out.append("    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },")
    out += ["};", "const unsigned g_libtab_n = %d;" % len(rows), ""]
    order = [str(libs.index(l)) for l in link_order(libs) if "register" in l]   # the libraries with `register`, after what they need (libtab_link)
    out += ["const unsigned g_libtab_link_order[] = { %s };" % (", ".join(order) or "0"), "const unsigned g_libtab_link_n = %d;" % len(order), ""]
    out += ["const void *g_libslot_bound[%d];" % max(1, len(srows))] + sfn + ["", "const LibSlot g_libslot[] = {"] + srows
    if not srows:
        out.append("    { 0, 0, 0, 0, 0 },")
    out += ["};", "const unsigned g_libslot_n = %d;" % len(srows), ""]
    return "\n".join(out)


def slots_text(libs, source="TOML B"):
    """libslots.h: the slot of every export of the file, for the sources the app compiles (libslot.h)."""
    out = ["// Generated by tools/libtab.py from %s (T239.3 D31, base 4.0): the slots of the exports of the libraries the app is linked with (libslot.h). Do not edit." % source,
           "#ifndef LIBSLOTS_H", "#define LIBSLOTS_H", ""]
    n = 0
    for i, lib, e in slots(libs):
        out.append("#define LIBSLOT_%s %d                    /* library %s */" % (e["name"], i, lib["name"]))
        out.append("#define LIBSLOT_%s_MAJOR %d" % (e["name"], e["major"]))
        n = i + 1
    out += ["#define LIBSLOT_COUNT %d" % n, "", "#endif", ""]
    return "\n".join(out)


def mk_text(libs):
    """libtab.mk: the C++ sources (LIB_SRCS_CC) and the C sources (LIB_SRCS_C) of the libraries, for the two compile rules of the build."""
    srcs = [f for lib in libs for f in lib["sources"]]
    return "LIB_SRCS_CC = %s\nLIB_SRCS_C = %s\n" % (" ".join(f for f in srcs if not f.endswith(".c")), " ".join(f for f in srcs if f.endswith(".c")))


def write_if_changed(path, text):
    if not os.path.exists(path) or open(path).read() != text:
        open(path, "w").write(text)


def lint(libs, root):
    """The generic sources name no library: no include of a library header, no entry symbol. Returns the list of findings."""
    import check_d24                                       # D12_TRX64U: the files that go to TRX64-Ultimate; they are a core's code, not the generic application
    headers = {h: lib["name"] for lib in libs for h in lib.get("headers", []) if not h.endswith("_api.h")}   # a table's header (online_api.h) is the seam the generic code reads through
    entry_syms = {lib[k]: lib["name"] for lib in libs for k in ENTRIES if k in lib}
    entry_syms.update({e["table"]: lib["name"] for lib in libs for e in lib.get("exports", []) if "table" in e})
    lib_sources = {f for lib in libs for f in lib["sources"]}
    inc = re.compile(r'^\s*#\s*include\s*"([^"]+)"')
    found = []
    for d in GENERIC_DIRS:
        base = os.path.join(root, d)
        for fn in sorted(os.listdir(base) if os.path.isdir(base) else []):
            if not fn.endswith((".cc", ".c", ".h")) or fn in lib_sources or fn in headers:
                continue
            if any(re.fullmatch(rx, "%s/%s" % (d, fn)) for rx in check_d24.D12_TRX64U):
                continue
            with open(os.path.join(base, fn), errors="replace") as fh:
                lines = fh.readlines()
            for no, line in enumerate(lines, 1):
                m = inc.match(line)
                if m and os.path.basename(m.group(1)) in headers:
                    found.append("%s/%s:%d includes %s (library %s)" % (d, fn, no, m.group(1), headers[os.path.basename(m.group(1))]))
                for sym, ln in entry_syms.items():
                    if re.search(r"\b%s\s*\(" % re.escape(sym), line) and not line.lstrip().startswith("//"):
                        found.append("%s/%s:%d calls %s (library %s)" % (d, fn, no, sym, ln))
    return found


def slot_needs(libs, root):
    """Base 4.0 (libslot.h): a library whose source names the slot of an export (LIBSLOT_<name>) lists the library that exports it in `needs` (or is it).
    Returns the findings."""
    exporter = {e["name"]: lib["name"] for lib in libs for e in lib.get("exports", [])}
    use = re.compile(r"\bLIBSLOT_([a-z0-9_]+?)(?:_MAJOR)?\b")
    found = []
    for lib in libs:
        own = set(lib.get("needs", [])) | {lib["name"]}
        for f in lib["sources"] + lib.get("headers", []):
            p = find_file(root, f)
            if not p:
                continue
            with open(p, errors="replace") as fh:
                for no, line in enumerate(fh, 1):
                    for x in use.findall(line):
                        if x == "COUNT":
                            continue
                        if x not in exporter:
                            found.append("%s:%d (library %s) names LIBSLOT_%s, which no library of the file exports" % (f, no, lib["name"], x))
                        elif exporter[x] not in own:
                            found.append("%s:%d (library %s) takes the export %s of library %s, which its needs do not list" % (f, no, lib["name"], x, exporter[x]))
    return found


def nm_symbols(obj):
    nm = os.environ.get("NM", "nm")                                      # the build passes the toolchain's riscv nm
    try:
        r = subprocess.run([nm, "-P", obj], capture_output=True, text=True)
    except OSError as e:
        raise LibTabError("%s: %s" % (nm, e))
    if r.returncode != 0:
        raise LibTabError("nm %s: %s" % (obj, r.stderr.strip()))
    defd, und = set(), set()
    for line in r.stdout.splitlines():
        f = line.split()
        if len(f) < 2:
            continue
        if f[1] == "U":
            und.add(f[0])
        elif f[1] not in ("w", "v", "-"):                                 # a weak undefined symbol needs no definition; everything else is defined here
            defd.add(f[0])
    return defd, und


def symcheck(libs, objdir, root):
    """nm over the objects: a generic object (a source of a GENERIC_DIRS directory that is no library's) must not use a symbol a library object defines."""
    import check_d24                                       # the files that go to TRX64-Ultimate are a core's code (they name their libraries in their own TOML B)
    lib_stems = {os.path.splitext(f)[0]: lib["name"] for lib in libs for f in lib["sources"]}
    generic_stems = set()
    for d in GENERIC_DIRS:
        for fn in (os.listdir(os.path.join(root, d)) if os.path.isdir(os.path.join(root, d)) else []):
            if fn.endswith((".cc", ".c")) and not any(re.fullmatch(rx, "%s/%s" % (d, fn)) for rx in check_d24.D12_TRX64U):
                generic_stems.add(os.path.splitext(fn)[0])
    objs = {}
    for fn in os.listdir(objdir):
        if fn.endswith(".o"):
            objs[os.path.splitext(fn)[0]] = os.path.join(objdir, fn)
    missing = [f for lib in libs for f in lib["sources"] if os.path.splitext(f)[0] not in objs]
    owner = {}
    for stem, name in lib_stems.items():
        if stem in objs:
            for s in nm_symbols(objs[stem])[0]:
                owner.setdefault(s, name)
    found = []
    for stem in sorted(generic_stems):
        if stem in lib_stems or stem not in objs or stem == "libtab_table":
            continue
        for s in sorted(nm_symbols(objs[stem])[1]):
            if s in owner:
                found.append("%s.o uses %s (library %s)" % (stem, s, owner[s]))
    return missing, found


def map_stems(text):
    """The stems of the object files that are inputs of a link: every `name.o` the map names (a plain object, or a member of an archive, `lib.a(name.o)`)."""
    return set(re.findall(r"(?:^|[\s(/])([A-Za-z0-9_+\-]+)\.o\b", text))


def core_code_stems(root):
    """The stems of the sources that are a core's code: the files of coreapp/cores and of every cores/<id>/app/lib."""
    out = {}
    dirs = [os.path.join(root, "coreapp", "cores")]
    cores = os.path.join(root, "cores")
    if os.path.isdir(cores):
        dirs += [os.path.join(cores, c, "app", "lib") for c in sorted(os.listdir(cores))]
    for d in dirs:
        for fn in sorted(os.listdir(d)) if os.path.isdir(d) else []:
            if fn.endswith((".cc", ".c")):
                out.setdefault(os.path.splitext(fn)[0], os.path.relpath(os.path.join(d, fn), root))
    return out


def mapcheck(libs, ref_libs, map_text, root, base_sources=()):
    """Returns the findings: sources of the TOML B's libraries that are no input of the link, inputs that are the sources of a library the TOML B does not name, and inputs
    that are a core's code no library and no base_sources entry of the TOML B names."""
    have = map_stems(map_text)
    own = {os.path.splitext(f)[0]: lib["name"] for lib in libs for f in lib["sources"]}
    allowed = set(own) | {os.path.splitext(f)[0] for f in base_sources}
    found = []
    for stem, name in sorted(own.items()):
        if stem not in have:
            found.append("the source %s of library %s is no input of the link" % (stem, name))
    ref = {os.path.splitext(f)[0]: lib["name"] for lib in ref_libs for f in lib["sources"]}
    for stem in sorted(set(ref) - allowed):
        if stem in have:
            found.append("%s.o (library %s) is in the link, the TOML B does not name it" % (stem, ref[stem]))
    for stem, where in sorted(core_code_stems(root).items()):
        if stem in have and stem not in allowed and stem not in ref:
            found.append("%s.o (%s) is in the link, the TOML B names no library or base source for it" % (stem, where))
    return found


def samelib(libs_a, libs_b, name, maps=(), text=lambda p: open(p, errors="replace").read()):
    """S12: the library `name` is the same in two TOML B files (the same source and header files, the same entries) and, for each link map given, every one of its sources is an input
    of that link. Returns the findings."""
    pick = lambda libs: next((l for l in libs if l["name"] == name), None)
    a, b = pick(libs_a), pick(libs_b)
    if a is None or b is None:
        return ["the library %s is not in both TOML B files" % name]
    found = []
    for key in ("sources", "headers") + tuple(ENTRIES) + ("capability", "needs", "exports", "menu"):
        if a.get(key) != b.get(key):
            found.append("library %s: %s is %s in one TOML B and %s in the other" % (name, key, a.get(key), b.get(key)))
    for m in maps:
        have = map_stems(text(m))
        for f in a["sources"]:
            if os.path.splitext(f)[0] not in have:
                found.append("the source %s of library %s is no input of the link %s" % (f, name, m))
    return found


def main(argv):
    try:
        if len(argv) == 3 and argv[0] == "table":
            libs = load(argv[1])
            os.makedirs(argv[2], exist_ok=True)
            write_if_changed(os.path.join(argv[2], "libtab_table.cc"), table_text(libs, os.path.basename(argv[1])))
            write_if_changed(os.path.join(argv[2], "libtab.mk"), mk_text(libs))
            write_if_changed(os.path.join(argv[2], "libslots.h"), slots_text(libs, os.path.basename(argv[1])))
            return 0
        if len(argv) in (2, 3) and argv[0] == "tests":
            if len(argv) == 3 and argv[2] not in TEST_KINDS:
                raise LibTabError("tests: the kind is one of %s, not %s" % (", ".join(TEST_KINDS), argv[2]))
            for lib in load(argv[1]):
                for kind in TEST_KINDS:
                    if len(argv) == 2 or kind == argv[2]:
                        for x in lib.get("tests", {}).get(kind, []):
                            print(x)
            return 0
        if len(argv) == 2 and argv[0] == "sources":
            for lib in load(argv[1]):
                for f in lib["sources"]:
                    print(f)
            return 0
        if len(argv) == 3 and argv[0] == "lint":
            libs = load(argv[1])
            check_files(libs, argv[2])
            found = lint(libs, argv[2]) + slot_needs(libs, argv[2])
            for f in found:
                print("libtab lint: %s" % f)
            return 1 if found else 0
        if len(argv) == 4 and argv[0] == "symcheck":
            libs = load(argv[1])
            missing, found = symcheck(libs, argv[2], argv[3])
            for f in missing:
                print("libtab symcheck: the source %s of TOML B has no object in %s" % (f, argv[2]))
            for f in found:
                print("libtab symcheck: %s" % f)
            return 1 if (missing or found) else 0
        if len(argv) >= 4 and argv[0] == "samelib":
            found = samelib(load(argv[1]), load(argv[2]), argv[3], argv[4:])
            for f in found:
                print("libtab samelib: %s" % f)
            return 1 if found else 0
        if len(argv) in (4, 5) and argv[0] == "mapcheck":
            libs = load(argv[1])
            ref = load(argv[4] if len(argv) == 5 else os.path.join(argv[3], "coreapp", "libs", "shared.toml"))
            app = descrec.toml_loads(open(argv[1]).read()).get("app", {})
            with open(argv[2], errors="replace") as fh:
                found = mapcheck(libs, ref, fh.read(), argv[3], app.get("base_sources", []))
            for f in found:
                print("libtab mapcheck: %s" % f)
            return 1 if found else 0
    except LibTabError as e:
        print("libtab: %s" % e, file=sys.stderr)
        return 1
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
