#!/usr/bin/env python3
"""The library table of an app (T239.3 D31, P3-1): reads the TOML B of a core's app, which names the libraries the app is linked with.

  libtab.py table TOML_B OUT_DIR     writes libtab_table.cc (the table libtab.cc walks) and libtab.mk (LIB_SRCS_CC, the sources to compile), each only when its content changes
  libtab.py sources TOML_B           the source file names of every library, one per line
  libtab.py lint TOML_B ROOT         no generic source (coreapp/src, uicore) includes a header of a library or names an entry symbol of one
  libtab.py symcheck TOML_B OBJDIR ROOT
                                     nm over the objects of a build: no generic object has an undefined symbol that a library object defines (only libtab_table's entries may)

A library: name, sources, headers (the headers that name its functions), and the entries it hands the generic application: bind, register_ops (with capability), init, start,
freeze_request, core, table (the library's call table, found by libtab_table(name) in libtab.h; a table's header, *_api.h, is the seam the generic code reads it through). `needs` names other libraries of the file. See coreapp/src/libtab.h for what each entry is."""
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
    "table": ("const void *", "void"),
    "core": ("void", "CoreCtx *, const CoreDesc *, const DescRecord *"),
}
KEYS = {"name", "sources", "headers", "needs", "capability"} | set(ENTRIES)
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
        if k not in ("app", "library"):
            raise LibTabError("%s: unknown table '%s'" % (path, k))
    libs = doc.get("library", [])
    names, srcs = [], {}
    for lib in libs:
        for k in lib:
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
        if "capability" in lib and "register_ops" not in lib:
            raise LibTabError("%s: library '%s': a capability belongs to register_ops" % (path, n))
        if "capability" in lib and not re.match(r"^[a-z0-9_]+$", str(lib["capability"])):
            raise LibTabError("%s: library '%s': capability is a name" % (path, n))
    for lib in libs:
        for need in lib.get("needs", []):
            if need not in names:
                raise LibTabError("%s: library '%s' needs '%s', which the file does not list" % (path, lib["name"], need))
    return libs


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


def table_text(libs, source="TOML B"):
    decl = []
    rows = []
    for lib in libs:
        for key, (ret, par) in ENTRIES.items():
            if key in lib:
                decl.append("%s %s(%s);" % (ret, lib[key], par))
        cell = []
        for key in ("bind", "register_ops"):
            cell.append(lib[key] if key in lib else "0")
        cell.append('"%s"' % lib["capability"] if "capability" in lib else "0")
        for key in ("init", "start", "freeze_request", "table", "core"):
            cell.append(lib[key] if key in lib else "0")
        rows.append('    { "%s", %s },' % (lib["name"], ", ".join(cell)))
    out = ["// Generated by tools/libtab.py from %s (T239.3 D31): the libraries the app is linked with. Do not edit." % source,
           '#include "libtab.h"', ""]
    out += sorted(set(decl)) + ["", "const LibEntry g_libtab[] = {"] + rows
    if not rows:
        out.append("    { 0, 0, 0, 0, 0, 0, 0, 0, 0 },")
    out += ["};", "const unsigned g_libtab_n = %d;" % len(rows), ""]
    return "\n".join(out)


def mk_text(libs):
    srcs = [f for lib in libs for f in lib["sources"]]
    return "LIB_SRCS_CC = %s\n" % " ".join(srcs)


def write_if_changed(path, text):
    if not os.path.exists(path) or open(path).read() != text:
        open(path, "w").write(text)


def lint(libs, root):
    """The generic sources name no library: no include of a library header, no entry symbol. Returns the list of findings."""
    import check_d24                                       # D12_TRX64U: the files that go to TRX64-Ultimate; they are a core's code, not the generic application
    headers = {h: lib["name"] for lib in libs for h in lib.get("headers", []) if not h.endswith("_api.h")}   # a table's header (online_api.h) is the seam the generic code reads through
    entry_syms = {lib[k]: lib["name"] for lib in libs for k in ENTRIES if k in lib}
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


def main(argv):
    try:
        if len(argv) == 3 and argv[0] == "table":
            libs = load(argv[1])
            os.makedirs(argv[2], exist_ok=True)
            write_if_changed(os.path.join(argv[2], "libtab_table.cc"), table_text(libs, os.path.basename(argv[1])))
            write_if_changed(os.path.join(argv[2], "libtab.mk"), mk_text(libs))
            return 0
        if len(argv) == 2 and argv[0] == "sources":
            for lib in load(argv[1]):
                for f in lib["sources"]:
                    print(f)
            return 0
        if len(argv) == 3 and argv[0] == "lint":
            libs = load(argv[1])
            check_files(libs, argv[2])
            found = lint(libs, argv[2])
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
    except LibTabError as e:
        print("libtab: %s" % e, file=sys.stderr)
        return 1
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
