#!/usr/bin/env python3
"""The app of a core, linked from the SDK (T239.3 D31, P3-2): what a core's TOML A and TOML B turn into, and the link.

  app_build.py sizes TOML_B OUT_DIR [--inc DIR ...]        writes OUT_DIR/appsizes.h: the maxima the [sizes] table of the TOML B names, as the macros of the headers
                                                           (every object that includes ops.h, descrec.h, contrib.h or corereport.h is compiled with it, -include)
  app_build.py table                                       the [sizes] keys: macro, header, default, range (the defaults are read from the headers)
  app_build.py record TOML_B RECORD.bin                    the core's menu record against the [sizes] table of its TOML B (the numbers the compiled app refuses at start)
  app_build.py sized INCDIRS SOURCE...                     the sources among SOURCE that depend on the core: their include closure holds a header whose layout the TOML A
                                                           or the TOML B decides (ops.h, descrec.h, contrib.h, corereport.h, drvtab.h, basetab.h). INCDIRS: a file of
                                                           directories, one per line (the include path of the build). tools/sdk_pack.sh ships these as sources, the rest as an archive
  app_build.py split INCDIRS OUT_DIR SOURCE...             the same, written to OUT_DIR/sized.real and OUT_DIR/unsized.real (real paths); the Makefile's target sdkbase calls it. The headers of a
                                                           source are the ones its compile listed (the .d file in the directory above OUT_DIR, which the conditions of the preprocessor decided);
                                                           a source with none is read as text
  app_build.py pack-base BASE_DIR PKG_DIR SHARED_TOML_B   what tools/sdk_pack.sh takes from the build's `make sdkbase` directory into the package (base/, lib/)
  app_build.py flags MAKEFILE APPFLAGS                     every -D of the core app Makefile's OPTIONS is in app/lib/appflags.mk (the package compiles with that file)
  app_build.py build --sdk DIR --toml-a A.toml [--base-toml-a BASE.toml] --toml-b B.toml --out DIR [--record REC.bin] [--src DIR ...] [--maps DIR_OR_FILE ...] [--dry-run]
                                                           generates appsizes.h, the driver table (drvtab.h/.c), the table of the base blocks (basetab.h), the library table (libtab_table.cc), the I/O linker fragment and
                                                           checks the record (a window may not cover a block of the base of the TOML A); then (not --dry-run) links the app with mk/appbuild.mk of the package: the base archives, the sources
                                                           that depend on the core, the libraries of the TOML B. --maps adds the register maps of the core's own blocks to the package's maps/.
                                                           [app] desc names the descriptor symbol of a core compiled into the app; a core described by its record (D17) has none
                                                           A TOML B whose [app] has `base_blocks = [names]` links the core's TOML A with those blocks of the base's TOML A
                                                           (--base-toml-a, default the package's gateware/<[app] base_toml_a>, which is shared.toml when the key is absent;
                                                           the VIC-20's is host.toml: it runs on the host bitstream); out/blocks.txt lists the blocks

The [sizes] table of a TOML B (every key is optional; a key that is not there keeps the default of the header). The registry, the contributions and the report are sized per core;
a core that needs more registry than the shared app has says so here and pays for it in its own image only. A number outside its range, or a number its neighbours cannot hold, is refused."""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import descrec  # noqa: E402

IDENT = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
COUNT = (1, 65535)
BYTES = (256, 1 << 22)
# key -> (macro, header, (lowest, highest), what it is)
SIZES = {
    "ops": ("OPS_MAX", "ops.h", COUNT, "operations of the registry, over the base and every library"),
    "states": ("STATES_MAX", "ops.h", COUNT, "state values of the registry"),
    "events": ("EVENTS_MAX", "ops.h", COUNT, "events of the registry"),
    "report_bytes": ("CORE_REPORT_MAX", "corereport.h", (4096, 1 << 20), "the text of the bug report (the REST answer is this and 4 KiB of header)"),
    "contrib_settings": ("CONTRIB_SETTINGS_MAX", "contrib.h", COUNT, "settings the libraries contribute together"),
    "contrib_values": ("CONTRIB_VALUES_MAX", "contrib.h", (2, 4096), "values of one contributed setting"),
    "contrib_filetypes": ("CONTRIB_FILETYPES_MAX", "contrib.h", COUNT, "extensions of the merged descriptor"),
    "contrib_actions": ("CONTRIB_ACTIONS_MAX", "contrib.h", COUNT, "file actions over all of those"),
    "contrib_pool": ("CONTRIB_POOL_MAX", "contrib.h", BYTES, "characters of the labels made for contributed range settings"),
    "contrib_errors": ("CONTRIB_ERR_MAX", "contrib.h", (1, 1024), "refusals the merge keeps to report"),
    "contrib_polls": ("CONTRIB_POLLS_MAX", "contrib.h", COUNT, "polls of the libraries"),
    "contrib_slotpool": ("CONTRIB_SLOTPOOL", "contrib.h", BYTES, "characters of the strings made by $slot expansion"),
    "contrib_bindpool": ("CONTRIB_BINDPOOL", "contrib.h", COUNT, "bindings made by contributions"),
    "record_bytes": ("DESCREC_MAX_SIZE", "descrec.h", (256, 1 << 22), "bytes of the menu record"),
    "rec_binds": ("DR_BINDS", "descrec.h", COUNT, "bindings of the record"),
    "rec_values": ("DR_VALUES", "descrec.h", COUNT, "values of the record's settings, all together"),
    "rec_csettings": ("DR_CSETS", "descrec.h", COUNT, "settings of the core's own menu"),
    "rec_facts": ("DR_FACTS", "descrec.h", COUNT, "file actions of the core's own menu"),
    "rec_tasks": ("DR_TASKS", "descrec.h", COUNT, "tasks of the core's own menu"),
    "rec_exts": ("DR_EXTS", "descrec.h", COUNT, "extensions of the record's file actions"),
    "rec_ftypes": ("DR_FTYPES", "descrec.h", COUNT, "file types of the core's own menu"),
    "rec_keys": ("DR_KEYS", "descrec.h", COUNT, "rows of the key table"),
    "rec_windows": ("DR_WINDOWS", "descrec.h", COUNT, "register windows"),
    "rec_blocks": ("DR_BLOCKS", "descrec.h", COUNT, "blocks of TOML A"),
    "rec_props": ("DR_PROPS", "descrec.h", COUNT, "properties of TOML A, all together"),
    "rec_fields": ("DR_FIELDS", "descrec.h", COUNT, "fields of TOML A, all together"),
    "rec_states": ("DR_STATES", "descrec.h", COUNT, "states of TOML A, all together"),
    "elements": ("OVL_ELEMENTS_MAX", "overlay.h", (2, 64), "elements of the merged UI TOML, the base's own (2: still, paused, coreapp/base_ui.toml) and the core's (T256 D8)"),
    "overlay_text": ("OVL_TEXT_MAX", "overlay.h", (1, 256), "text calls of the overlay service that wait for the next frame (T256 D10)"),
}
# the headers whose contents decide an object's layout or code: an object that includes one of them, directly or not, is compiled for the core
SIZING_HEADERS = ("ops.h", "descrec.h", "contrib.h", "corereport.h", "drvtab.h", "basetab.h", "overlay.h")        # appsizes.h is -include of every object, so it says nothing
INCLUDE = re.compile(r'^[ \t]*#[ \t]*include[ \t]+(["<])([^">]+)[">]', re.M)


class BuildError(Exception):
    pass


def default_inc():
    """Where the headers are: the repository's coreapp/src, or the package's base/inc/coreapp/src (the same file at the same repository path)."""
    for p in (os.path.join(HERE, "..", "coreapp", "src"), os.path.join(HERE, "..", "base", "inc", "coreapp", "src")):
        if os.path.isfile(os.path.join(p, "ops.h")):
            return [os.path.normpath(p)]
    raise BuildError("no directory with ops.h beside this tool (--inc DIR names one)")


def header_default(incs, macro, header):
    """The value the header gives a macro: its own #define, read from the file (no copy of the number lives here)."""
    for d in incs:
        p = os.path.join(d, header)
        if os.path.isfile(p):
            m = re.search(r"^#define[ \t]+%s[ \t]+(0[xX][0-9a-fA-F]+|[0-9]+)[uUlL]*\b" % re.escape(macro), open(p, errors="replace").read(), re.M)
            if m:
                return int(m.group(1), 0)
            raise BuildError("%s has no number for %s" % (p, macro))
    raise BuildError("no %s in %s" % (header, ", ".join(incs)))


def load_sizes(tomlb, incs=None):
    """The [sizes] table of a TOML B, checked: {key: value} of what it names. The values of the keys it leaves out are the headers' (effective())."""
    try:
        doc = descrec.toml_loads(open(tomlb).read())
    except (descrec.DescError, OSError) as e:
        raise BuildError("%s: %s" % (tomlb, e))
    sz = doc.get("sizes", {})
    if not isinstance(sz, dict):
        raise BuildError("%s: [sizes] is a table" % tomlb)
    out = {}
    for k, v in sz.items():
        if k not in SIZES:
            raise BuildError("%s: [sizes] has no key '%s' (the keys: %s)" % (tomlb, k, ", ".join(SIZES)))
        lo, hi = SIZES[k][2]
        if isinstance(v, bool) or not isinstance(v, int) or not lo <= v <= hi:
            raise BuildError("%s: [sizes] %s = %r: an integer from %d to %d" % (tomlb, k, v, lo, hi))
        out[k] = v
    eff = effective(out, incs or default_inc())
    if eff["contrib_filetypes"] < eff["rec_ftypes"]:
        raise BuildError("%s: contrib_filetypes (%d) is below rec_ftypes (%d): the merged descriptor holds the record's own types and the new ones" % (tomlb, eff["contrib_filetypes"], eff["rec_ftypes"]))
    if eff["rec_props"] < eff["rec_blocks"]:
        raise BuildError("%s: rec_props (%d) is below rec_blocks (%d): a block has a property at least" % (tomlb, eff["rec_props"], eff["rec_blocks"]))
    return out


def effective(named, incs):
    return {k: named[k] if k in named else header_default(incs, m, h) for k, (m, h, _, _) in SIZES.items()}


def sizes_text(named, source):
    lines = ["/* Generated by tools/app_build.py from %s (T239.3 D31): the maxima of this core's app, -include of every object (C, C++ and assembler: no // comment here). */" % source,
             "#ifndef TRX_APPSIZES_H", "#define TRX_APPSIZES_H"]
    for k in SIZES:
        if k in named:
            m = SIZES[k][0]
            lines.append("#define %s %d%s" % (m, named[k], "U" if m == "DESCREC_MAX_SIZE" else ""))
    lines += ["#endif", ""]
    return "\n".join(lines)


def write_if_changed(path, text):
    if not os.path.exists(path) or open(path).read() != text:
        with open(path, "w") as f:
            f.write(text)


def cmd_sizes(tomlb, out, incs):
    named = load_sizes(tomlb, incs)
    os.makedirs(out, exist_ok=True)
    write_if_changed(os.path.join(out, "appsizes.h"), sizes_text(named, os.path.basename(tomlb)))
    return named


def cmd_record(tomlb, rec, incs):
    named = load_sizes(tomlb, incs)
    descrec.apply_sizes(named)
    try:
        descrec.parse_desc(open(rec, "rb").read())
    except (descrec.DescError, OSError) as e:
        raise BuildError("%s against the [sizes] of %s: %s" % (rec, tomlb, e))


def reach(path, incdirs, seen):
    """Adds to `seen` the real path of every header that `path` includes, directly or not, as the compiler finds it: a quote include next to the including file first, then the
    include path in order; an angle include in the include path only (what is in none of its directories is the toolchain's and is not followed). The conditions are not evaluated."""
    try:
        text = open(path, errors="replace").read()
    except OSError:
        return
    here = os.path.dirname(path)
    for quote, name in INCLUDE.findall(text):
        for d in ([here] if quote == '"' else []) + incdirs:
            p = os.path.realpath(os.path.join(d, name))
            if os.path.isfile(p):
                if p not in seen:
                    seen.add(p)
                    reach(p, incdirs, seen)
                break


def headers_of(path, incdirs):
    """What a source includes: the closure of its text (reach); the conditions are not evaluated."""
    seen = set()
    reach(os.path.realpath(path), incdirs, seen)
    return seen


def sized(incdirs, sources):
    return [s for s in sources if {os.path.basename(h) for h in headers_of(s, incdirs)} & set(SIZING_HEADERS)]


def cmd_pack_base(base_dir, out, shared_tomlb):
    """What the package carries of the base (tools/sdk_pack.sh): the archives of the build's `make sdkbase` directory, the sources that depend on the core and the sources of the
    shared libraries (TOML B of the shared app) at their repository paths under base/src/, and every header they reach under base/inc/. The include path (base/incdirs.txt) is the
    build's, as repository paths, in the build's order."""
    import shutil
    import libtab
    root = os.path.realpath(open(os.path.join(base_dir, "root.txt")).read().strip())
    incdirs = read_incdirs(os.path.join(base_dir, "incdirs.txt"))
    sized_real = [l.strip() for l in open(os.path.join(base_dir, "sized.real")) if l.strip()]
    libsrc = []
    for lib in libtab.load(shared_tomlb):
        for f in lib["sources"]:
            p = libtab.find_file(root, f)
            if not p:
                raise BuildError("the library source %s of %s is in none of %s" % (f, shared_tomlb, ", ".join(libtab.SRC_DIRS)))
            libsrc.append(os.path.realpath(p))

    def rel(p):
        r = os.path.relpath(os.path.realpath(p), root)
        return None if r.startswith("..") or "/output/" in "/" + r + "/" or r.startswith("output/") else r

    hdrs, srcs = set(), []
    for s in sized_real + libsrc:
        if s not in srcs:
            srcs.append(s)
            hdrs |= headers_of(s, incdirs)
    for d in ("src", "inc", "lib"):
        shutil.rmtree(os.path.join(out, "base" if d != "lib" else "", d), ignore_errors=True)
    names = []
    for s in srcs:
        r = rel(s)
        if r is None:
            raise BuildError("the source %s is outside the tree %s or in a build output directory" % (s, root))
        dst = os.path.join(out, "base", "src", r)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy(s, dst)
        if s in sized_real:
            names.append("src/" + r)
    ngen = 0
    for h in sorted(hdrs):
        r = rel(h)
        if r is None:
            ngen += 1                                              # generated for the core (drvtab.h) or per commit (gitinfo.h): not part of the package's headers
            continue
        dst = os.path.join(out, "base", "inc", r)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy(h, dst)
    gen = os.path.join(base_dir, "gen", "gitinfo.h")
    dirs = [rel(d) for d in incdirs]
    dirs = [d for d in dirs if d is not None]
    if os.path.isfile(gen):
        os.makedirs(os.path.join(out, "base", "inc", "_gen"), exist_ok=True)
        shutil.copy(gen, os.path.join(out, "base", "inc", "_gen", "gitinfo.h"))
        dirs.append("_gen")
    with open(os.path.join(out, "base", "SIZED.txt"), "w") as f:
        f.write("".join(n + "\n" for n in sorted(names)))
    with open(os.path.join(out, "base", "incdirs.txt"), "w") as f:
        f.write("".join(d + "\n" for d in dirs))
    shutil.copy(os.path.join(base_dir, "linker.x"), os.path.join(out, "base", "linker.x"))
    os.makedirs(os.path.join(out, "lib"), exist_ok=True)
    for f in sorted(os.listdir(os.path.join(base_dir, "lib"))):
        if f.endswith((".a", ".o")):
            shutil.copy(os.path.join(base_dir, "lib", f), os.path.join(out, "lib", f))
    for need in ("libtrxbase.a", "liblwip.a", "liblvgl.a", "crt0.o", "port_asm.o"):
        if not os.path.isfile(os.path.join(out, "lib", need)):
            raise BuildError("the build's sdkbase directory has no lib/%s" % need)
    print("app_build: base: %d sources compiled for the core, %d library sources, %d headers (%d generated ones left out), %d include directories" %
          (len(names), len(libsrc), len(hdrs) - ngen, ngen, len(dirs)))


def cmd_split(incdirs, out, sources):
    """Writes OUT/sized.real and OUT/unsized.real: the real paths of the sources that depend on the core and of the ones that do not."""
    os.makedirs(out, exist_ok=True)
    srcs = [os.path.realpath(s) for s in sources]
    hit = set(sized(incdirs, srcs))
    for name, lst in (("sized.real", [s for s in srcs if s in hit]), ("unsized.real", [s for s in srcs if s not in hit])):
        write_if_changed(os.path.join(out, name), "".join(s + "\n" for s in lst))


def read_incdirs(f):
    return [l.strip() for l in open(f) if l.strip() and os.path.isdir(l.strip())]


def make_flags(path):
    """The -D options of the makefile's OPTIONS lines and the --wrap options of its LFLAGS, as the set of the tokens."""
    toks = set()
    for line in open(path, errors="replace"):
        m = re.match(r"^(?:OPTIONS|LFLAGS)\s*\+?=\s*(.*)$", line.rstrip())
        if m:
            toks |= set(re.findall(r"(?:-D[A-Za-z_][A-Za-z0-9_]*(?:=\S+)?|--wrap=\S+)", m.group(1).split("#")[0]))
    return toks


def cmd_flags(makefile, appflags):
    want = make_flags(makefile)
    have = set(re.findall(r"(?:-D[A-Za-z_][A-Za-z0-9_]*(?:=\S+)?|--wrap=\S+)", open(appflags).read()))
    missing = sorted(want - have)
    if missing:
        raise BuildError("%s compiles with %s, which %s does not carry: the SDK would build another app than the tree" % (makefile, " ".join(missing), appflags))


def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if r.returncode != 0:
        raise BuildError("%s: %s" % (" ".join(cmd[:3]), (r.stderr or r.stdout).strip()))
    return r.stdout


def cmd_build(a):
    sdk, out = os.path.abspath(a["sdk"]), os.path.abspath(a["out"])
    tools = os.path.join(sdk, "tools")
    incs = [os.path.join(sdk, "base", "inc", "coreapp", "src")]
    maps = [os.path.join(sdk, "maps")] + [os.path.abspath(m) for m in a.get("maps", [])]
    mapargs = [x for m in maps for x in ("--maps", m)]
    os.makedirs(out, exist_ok=True)
    named = cmd_sizes(a["toml-b"], out, incs)
    py = [sys.executable, "-I"]
    app = descrec.toml_loads(open(a["toml-b"]).read()).get("app", {})
    base_blocks = app.get("base_blocks", [])
    if not isinstance(base_blocks, list) or not all(isinstance(x, str) and IDENT.match(x) for x in base_blocks):
        raise BuildError("%s: [app] base_blocks is a list of block names of the base's TOML A" % a["toml-b"])
    if base_blocks:
        merge_a(a, base_blocks, maps, out)
    else:
        run(py + [os.path.join(tools, "descrec.py"), "basetab", a["toml-a"], os.path.join(out, "drv")])
        run(py + [os.path.join(tools, "descrec.py"), "drivers", a["toml-a"], os.path.join(out, "drv")] + mapargs)
        run(py + [os.path.join(tools, "descrec.py"), "ldfrag", a["toml-a"], os.path.join(out, "io.ld")] + mapargs)
    if not base_blocks:
        try:
            descrec.use_base_toml_a(a["toml-a"])                   # a window of the record may not cover a block of the base of this TOML A (D30 point 1)
        except descrec.DescError as e:
            raise BuildError(str(e))
    if a.get("record"):
        cmd_record(a["toml-b"], a["record"], incs)
    run(py + [os.path.join(tools, "libtab.py"), "table", a["toml-b"], os.path.join(out, "lib")])
    libsrcs = run(py + [os.path.join(tools, "libtab.py"), "sources", a["toml-b"]]).split()
    srcdirs = [os.path.abspath(d) for d in a.get("src", [])]
    for f in libsrcs:                                          # a library's source is in the package's tree or in a directory the core names
        if not find_source(sdk, srcdirs, f):
            raise BuildError("the source %s of a library of %s is in the package's base/ and in none of %s" % (f, a["toml-b"], ", ".join(srcdirs) or "(no --src)"))
    sized_list = open(os.path.join(sdk, "base", "SIZED.txt")).read().split()
    desc = app.get("desc", "")
    if desc and not re.match(r"^[A-Za-z_][A-Za-z0-9_]*$", str(desc)):
        raise BuildError("%s: [app] desc is the name of the core's descriptor symbol" % a["toml-b"])
    if not desc and not a.get("record"):
        raise BuildError("%s: [app] desc is empty and no --record is given: a core without a description holds every start" % a["toml-b"])
    own = app.get("sources", [])
    if not isinstance(own, list) or not all(isinstance(x, str) and re.match(r"^[A-Za-z0-9_]+\.(cc|c)$", x) for x in own):
        raise BuildError("%s: [app] sources is a list of C or C++ file names" % a["toml-b"])
    bsrc = app.get("base_sources", [])
    if not isinstance(bsrc, list) or not all(isinstance(x, str) and re.match(r"^[A-Za-z0-9_]+\.(cc|c)$", x) for x in bsrc):
        raise BuildError("%s: [app] base_sources is a list of file names of the package's coreapp/cores" % a["toml-b"])
    for f in own:
        if not any(os.path.isfile(os.path.join(d, f)) for d in srcdirs):
            raise BuildError("the source %s of [app] in %s is in none of %s" % (f, a["toml-b"], ", ".join(srcdirs) or "(no --src)"))
    # T239.3 D50: a source of a core's own library (found in a --src directory) and the core's [app] sources are compiled with -DDRV_BASE_READ_ONLY; the SDK's shared libraries and the base keep every driver macro
    core_own = [f for f in libsrcs if any(os.path.isfile(os.path.join(d, f)) for d in srcdirs)] + own
    write_if_changed(os.path.join(out, "build.mk"),
                     "# generated by tools/app_build.py\nSDK := %s\nSIZED_SRCS := %s\nSRC_DIRS := %s\nLIB_SRCS := %s\nOWN_SRCS := %s\nBASE_SOURCES := %s\nCORE_DESC := %s\nCORE_OWN_NAMES := %s\n"
                     % (sdk, " ".join(sized_list), " ".join(srcdirs), " ".join(libsrcs), " ".join(own), " ".join(bsrc), desc, " ".join(sorted(set(os.path.splitext(f)[0] for f in core_own)))))
    print("app_build: %d size%s named in %s, %d sources compiled for the core, %d library sources" %
          (len(named), "" if len(named) == 1 else "s", os.path.basename(a["toml-b"]), len(sized_list), len(libsrcs)))
    if a.get("dry-run"):
        return
    cmd = ["make", "-C", out, "-f", os.path.join(sdk, "mk", "appbuild.mk"), "SDK=" + sdk]
    r = subprocess.run(cmd)
    if r.returncode != 0:
        raise BuildError("the link of the app failed")


def merge_a(a, base_blocks, maps, out):
    """The app's TOML A is the core's own (--toml-a) with the blocks of the base's TOML A that [app] base_blocks names (descrec.merge_toml_a); the base's is --base-toml-a, the
    package's gateware/<[app] base_toml_a> (shared.toml by default) when none is given. The driver table and the I/O fragment are made of that one document, as `descrec.py drivers` and `ldfrag` do for a file."""
    named = descrec.toml_loads(open(a["toml-b"]).read()).get("app", {}).get("base_toml_a", "shared.toml")
    if not isinstance(named, str) or not re.match(r"^[A-Za-z0-9_]+\.toml$", named):
        raise BuildError("%s: [app] base_toml_a is the file name of a TOML A in the package's gateware/" % a["toml-b"])
    base = a.get("base-toml-a") or os.path.join(a["sdk"], "gateware", named)
    try:
        doc = descrec.merge_toml_a(descrec.toml_loads(open(a["toml-a"]).read()), descrec.toml_loads(open(base).read()), base_blocks, a["toml-b"])
        doc = descrec.resolve_maps(doc, descrec.load_maps(maps))
        what = "%s + %s of %s" % (os.path.basename(a["toml-a"]), ", ".join(base_blocks), os.path.basename(base))
        head, src = descrec.drivers_files(doc, what)
        frag = descrec.ldfrag_doc(doc, what)
        io = (doc["io"]["iobase"], doc["io"]["u2p_io_base"])
    except (KeyError, TypeError) as e:
        raise BuildError("%s: the TOML A of the app has no [io] with %s" % (a["toml-a"], e))
    except (descrec.DescError, OSError) as e:
        raise BuildError("%s: the TOML A of the app: %s" % (a["toml-b"], e))
    os.makedirs(os.path.join(out, "drv"), exist_ok=True)
    rows = [(b["name"], b["address"], b["size"]) for b in doc["block"] if b.get("owner") == "base"]
    descrec._BASE_BLOCKS["path"], descrec._BASE_BLOCKS["rows"], descrec._BASE_BLOCKS["io"] = what, rows, io    # the base blocks and bases of this one document decide what a window may cover
    write_if_changed(os.path.join(out, "drv", "basetab.h"), descrec.basetab_text(rows, what, io))
    write_if_changed(os.path.join(out, "drv", "drvtab.h"), head)
    write_if_changed(os.path.join(out, "drv", "drvtab.c"), src)
    write_if_changed(os.path.join(out, "io.ld"), frag)
    with open(os.path.join(out, "blocks.txt"), "w") as f:            # what the TOML A names, for the check of the link (libtab.py mapcheck)
        f.write("".join("%s\n" % b["name"] for b in doc["block"]))


def find_source(sdk, srcdirs, name):
    for d in srcdirs:
        if os.path.isfile(os.path.join(d, name)):
            return os.path.join(d, name)
    for root, _, files in os.walk(os.path.join(sdk, "base", "src")):          # base/src/<repository path>/<name>
        if name in files:
            return os.path.join(root, name)
    return None


def main(argv):
    try:
        if argv[:1] == ["table"]:
            incs = default_inc()
            for k, (m, h, (lo, hi), what) in SIZES.items():
                print("%-18s %-22s %-13s %-8d %d..%d  %s" % (k, m, h, header_default(incs, m, h), lo, hi, what))
            return 0
        if argv[:1] == ["sizes"] and len(argv) >= 3:
            incs = []
            rest = argv[3:]
            while rest[:1] == ["--inc"] and len(rest) >= 2:
                incs.append(rest[1])
                rest = rest[2:]
            cmd_sizes(argv[1], argv[2], incs or default_inc())
            return 0
        if argv[:1] == ["record"] and len(argv) == 3:
            cmd_record(argv[1], argv[2], default_inc())
            return 0
        if argv[:1] == ["sized"] and len(argv) >= 3:
            for s in sized(read_incdirs(argv[1]), argv[2:]):
                print(s)
            return 0
        if argv[:1] == ["split"] and len(argv) >= 4:
            cmd_split(read_incdirs(argv[1]), argv[2], argv[3:])
            return 0
        if argv[:1] == ["pack-base"] and len(argv) == 4:
            cmd_pack_base(argv[1], argv[2], argv[3])
            return 0
        if argv[:1] == ["flags"] and len(argv) == 3:
            cmd_flags(argv[1], argv[2])
            return 0
        if argv[:1] == ["build"]:
            a, i = {"src": [], "maps": []}, 1
            while i < len(argv):
                k = argv[i]
                if k == "--dry-run":
                    a["dry-run"] = True
                    i += 1
                elif k in ("--sdk", "--toml-a", "--base-toml-a", "--toml-b", "--out", "--record", "--src", "--maps") and i + 1 < len(argv):
                    if k in ("--src", "--maps"):
                        a[k[2:]].append(argv[i + 1])
                    else:
                        a[k[2:]] = argv[i + 1]
                    i += 2
                else:
                    break
            else:
                if "toml-a" not in a and "toml-b" in a and os.path.isfile(a["toml-b"]):         # [app] toml_a of the TOML B, a file beside it
                    ta = descrec.toml_loads(open(a["toml-b"]).read()).get("app", {}).get("toml_a")
                    if isinstance(ta, str):
                        a["toml-a"] = os.path.join(os.path.dirname(os.path.abspath(a["toml-b"])), ta)
                if all(x in a for x in ("sdk", "toml-a", "toml-b", "out")):
                    cmd_build(a)
                    return 0
    except (BuildError, descrec.DescError) as e:
        print("app_build: %s" % e, file=sys.stderr)
        return 1
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
