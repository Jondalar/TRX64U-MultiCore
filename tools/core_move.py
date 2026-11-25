#!/usr/bin/env python3
"""T239.3 D55: moving a core to its own app (cores/<id>/app) - the part a check can prove, as a script. The rest is the runbook .claude/skills/core-move/SKILL.md.

  core_move.py plan CORE                  what a move would do, nothing written
  core_move.py apply CORE [--full-scan]   the mechanical part: git mv the core's sources to cores/<id>/app/lib, the TOML A (rtl_check.py emit), sdk.toml and the rtl_check case
                                          from the templates, the core's row out of table.cc and cores.h; then it lists what needs judgement and runs the gates
  core_move.py gates CORE                 rtl_check against cores/<id>/app/gateware.toml, the app's sources compiled natively against its driver table, check_d24, core_toml lanes
  core_move.py dryrun [CORE...] [--out DIR] [--full-scan] [-j N]
                                          D55's dry run: for each core (default: every core of coreapp/src/table.cc) the TOML A, its rtl_check, the driver table of the
                                          merged TOML A and the sources compiled against it, in DIR (default: a scratch folder). NOTHING in the repository is changed. One table at the end.

What it knows (all read from the repository, none from a comment): the core's code is the include closure of coreapp/cores/<id>.cc inside coreapp/cores (a file that the closure
of another core reaches too is shared: it stays and is listed); the shared libraries it uses are the libraries of coreapp/libs/shared.toml whose headers the closure includes (and
their `needs`); the registers are what rtl_check.py emit decodes from cores/<id>/layer2_<id>.v with the core's list of sources (cores/<id>/core_sources.tcl, or
cores/<id>/app/rtl_check_sources.tcl where the core has one: a list the checker can read, as the A1200's), named from the register headers of the closure by offset.
"""
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import descrec  # noqa: E402

CORES_SRC = os.path.join(ROOT, "coreapp", "cores")
SRC = os.path.join(ROOT, "coreapp", "src")
WINDOW = "0x101A0000:0x10000"
BUS = "layer2_%s:write=cw_we,read=cw_re,address=cw_addr,data=cw_wdata,response=cw_rdata"
BASE_BLOCKS = ["cart_regs", "icap_id", "icap_data", "icap_enable", "icap_count", "partition", "audio_master"]    # the A1200's and the VIC-20's common set (sdk.toml)
MIN_A = """[io]
iobase = 0x10000000
u2p_io_base = 0x10100000
[identity]
id = "MINI"
block = "ident"
[[block]]
name = "ident"
module = "mini"
owner = "base"
address = 0x101A0000
size = 0x10
  [[block.property]]
  name = "id"
  offset = 0x0
  width = 32
  dir = "r"
  [[block.property]]
  name = "caps"
  offset = 0x4
  width = 8
  dir = "r"
  [[block.property]]
  name = "caps2"
  offset = 0x5
  width = 8
  dir = "r"
  [[block.property]]
  name = "build"
  offset = 0x8
  width = 32
  dir = "r"
"""
DRIVE_A = """
# TODO the drive window (drv_io_*, 0x10020000): the shipped map cbm1541 is the drive of the C64U gateware with every register of it; the core's own list (the VIC-20's: no drivetype, no WD177x) is
# written from the RTL with a second `--bus layer2_<id>:write=drv_io_wr,read=drv_io_rd,address=drv_io_addr,data=drv_io_wdata,response=drv_io_rdata --window 0x10020000:0x4000:full`
[[block]]
name = "drive_a"
module = "cbm1541"
owner = "core"
address = 0x10020000
size = 0x1800
map = "cbm1541"
"""
CXX = os.environ.get("CXX", "g++-16")


def rd(p):
    with open(p) as f:
        return f.read()


def sh(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


# ---------------------------------------------------------------- what a core is

def table_cores():
    return re.findall(r"&(\w+)_desc,", rd(os.path.join(SRC, "table.cc")))


def includes(path):
    return re.findall(r'^\s*#\s*include\s+"([^"]+)"', rd(path), flags=re.M)


def shared_libs():
    doc = descrec.toml_loads(rd(os.path.join(ROOT, "coreapp", "libs", "shared.toml")))
    return doc.get("library", [])


def closure(core):
    """(own files, libs, notes): the files of coreapp/cores reached from <core>.cc, the shared libraries they include, the files that are not shared libraries' own."""
    libs = shared_libs()
    lib_files = {}
    for l in libs:
        for f in l.get("sources", []) + l.get("headers", []):
            lib_files[f] = l["name"]
    start = os.path.join(CORES_SRC, core + ".cc")
    if not os.path.exists(start):
        return [], [], ["coreapp/cores/%s.cc does not exist" % core]
    seen, todo, used = [], [core + ".cc"], set()
    while todo:
        f = todo.pop()
        if f in seen or f == "cores.h":
            continue
        if f in lib_files:
            used.add(lib_files[f])
            continue
        p = os.path.join(CORES_SRC, f)
        if not os.path.exists(p):
            continue
        seen.append(f)
        for i in includes(p):
            todo.append(i)
            if i.endswith(".h"):
                todo.append(i[:-2] + ".cc")
    for l in libs:                      # the closure of needs
        pass
    changed = True
    byname = {l["name"]: l for l in libs}
    while changed:
        changed = False
        for n in list(used):
            for m in byname[n].get("needs", []):
                if m not in used:
                    used.add(m)
                    changed = True
    return sorted(seen), [l["name"] for l in libs if l["name"] in used], []


_OWNERS = {}


def owners():
    if not _OWNERS:
        for c in table_cores() + ["portdiag"]:
            _OWNERS[c] = set(closure(c)[0]) if c != "portdiag" else set()
    return _OWNERS


def info(core):
    own, libs, notes = closure(core)
    sh_with = {c: sorted(set(own) & fs) for c, fs in owners().items() if c != core and set(own) & fs}
    shared = sorted({f for fl in sh_with.values() for f in fl})
    cdir = os.path.join(ROOT, "cores", core)
    kind = "dfx" if os.path.exists(os.path.join(cdir, "core.toml")) else "large"
    rts = os.path.join(cdir, "app", "rtl_check_sources.tcl")
    lists = []
    if os.path.exists(rts):
        lists = [os.path.relpath(rts, os.path.join(ROOT, "host"))]
    else:
        cs = os.path.join(cdir, "core_sources.tcl")
        if os.path.exists(cs):
            lists = [os.path.relpath(cs, os.path.join(ROOT, "host"))]
            if "cores/c1541" in rd(os.path.join(cdir, "core.toml")) if kind == "dfx" else False:
                lists.append("gideon_vhdl.tcl")
    regs_h = [os.path.join(CORES_SRC, f) for f in own if f.endswith("_regs.h")] or [os.path.join(CORES_SRC, core + ".cc")]
    return dict(core=core, kind=kind, own=[f for f in own if f not in shared], shared=shared, shared_with=sh_with, libs=libs, notes=notes, lists=lists,
                headers=regs_h, layer2=os.path.exists(os.path.join(cdir, "layer2_%s.v" % core)), has_rts=os.path.exists(rts))


# ---------------------------------------------------------------- the templates

def toml_val(v):
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, int):
        return str(v)
    if isinstance(v, list):
        return "[" + ", ".join(toml_val(x) for x in v) + "]"
    if isinstance(v, dict):
        return "{ " + ", ".join("%s = %s" % (k, toml_val(x)) for k, x in v.items()) + " }"
    return '"%s"' % v


def sdk_toml(inf):
    c = inf["core"]
    own_cc = [f for f in inf["own"] if f.endswith(".cc")]
    hdr = [f for f in inf["own"] if f.endswith(".h")]
    L = ["# TOML B of the %s's app (T239.3 D55, written by tools/core_move.py from the templates of the VIC-20's and the A1200's): what its own app is linked from." % c,
         "# TODO every comment below the values; the values are the mechanical guess (the include closure of coreapp/cores/%s.cc, the shared libraries it includes)." % c,
         "[app]", 'name = "%s"' % c, "desc_record = true", 'desc = "%s_desc"' % c,
         '# TOML A: the core\'s own (gateware.toml: emitted from the RTL, tools/rtl_check.py emit; TODO the names and blocks) and the blocks of the host\'s TOML A that the shared drivers (hw.cc) name',
         'toml_a = "gateware.toml"', 'base_toml_a = "host.toml"', "base_blocks = %s" % toml_val(BASE_BLOCKS + (["iec_status"] if "cbm1541" in inf["libs"] else [])),
         'base_sources = ["portdiag.cc"]', "",
         "[[library]]", 'name = "%s"' % c, "sources = %s" % toml_val(own_cc), "headers = %s" % toml_val(hdr)]
    if inf["libs"]:
        L.append("needs = %s" % toml_val(inf["libs"]))
    L.append("# TODO tests = { unit = [...], service = [...], ui = [...], api = [...] }: the units of tests/coreapp/run.sh that name this core (T239.5 D4)")
    byname = {l["name"]: l for l in shared_libs()}
    for n in inf["libs"]:
        l = byname[n]
        L += ["", "[[library]]"]
        for k, v in l.items():
            if k != "tests":
                L.append("%s = %s" % (k, toml_val(v)))
    return "\n".join(L) + "\n"


def defines(core):
    """The `define symbols of the core's flat build (mono_env TRX_DEFS of `core_toml.py env`): the checker decides an `ifdef with them (layer2_c128.v: RAM_DDR2 chooses the module of an instance)."""
    p = sh([sys.executable, "tools/core_toml.py", "env", core], cwd=ROOT)
    for l in p.stdout.split("\n"):
        if l.startswith("TRX_DEFS="):
            return l.split("=", 1)[1].split()
    return []


def candidate_list(core, path):
    """A source list for the checker from the core's own pathspecs (cores/<id>/core.toml or mono/core.toml `sources`: what the core's key hashes), for a core that has no
    core_sources.tcl. Written beside the other generated files; a person turns it into rtl_check_sources.tcl (the A1200's) where files have to be left out."""
    spec = sh([sys.executable, "tools/core_toml.py", "sources", core], cwd=ROOT).stdout.split()
    files = sh(["git", "ls-files", "--"] + spec, cwd=ROOT).stdout.split()
    files = [f for f in files if f.endswith((".v", ".sv", ".vhd", ".vhdl")) and "/sim/" not in f]
    with open(path, "w") as f:
        f.write("# generated by tools/core_move.py: the .v/.sv/.vhd files of the pathspecs of `core_toml.py sources %s`\n" % core)
        f.write("\n".join(os.path.relpath(os.path.join(ROOT, x), os.path.join(ROOT, "host")) for x in sorted(files)) + "\n")
    return [os.path.relpath(path, os.path.join(ROOT, "host"))]


def emit_args(inf, out_toml, full=False, jobs=2, cache=None):
    c = inf["core"]
    a = ["--design", "host", "--top", "layer2_" + c, "--bus", BUS % c, "--window", WINDOW, "--core", c, "--toml-a-out", out_toml, "--maps", "app/sdkpkg/maps", "-j", str(jobs)]
    for l in inf["lists"]:
        a += ["--list", l]
    for h in inf["headers"]:
        a += ["--header", os.path.relpath(h, ROOT)]
    for d in defines(c):
        a += ["--define", d]
    if not full:
        a += ["--pages", "first"]
    if cache:
        a += ["--cache", cache]
    return a


def run_emit(inf, out_toml, full, jobs, cache):
    if not inf["lists"] and inf["layer2"]:
        inf["lists"] = candidate_list(inf["core"], os.path.join(os.path.dirname(out_toml), "rtl_check_sources.tcl"))
        inf["generated_list"] = True
    p = sh([sys.executable, "-I", "tools/rtl_check.py", "emit"] + emit_args(inf, out_toml, full, jobs, cache), cwd=ROOT)
    return p.returncode, (p.stdout + p.stderr).replace("\r", "\n")


# ---------------------------------------------------------------- compile

def includes_native(drv):
    S = ROOT
    HA = "host-app/software/hostapp"
    incs = ["app/view", "app/video", "coreapp/src", "uicore", "coreapp/cores", "app/loaders", "app/lib/cbm1541", "app/edit", "tests/coreapp", "tests", "host-app/software/io/c64",
            "host-app/software/lvgl_port", "host-app/software/filesystem", HA]
    return ["-I" + drv] + ["-I" + os.path.join(S, i) for i in incs]


def stub_incs(stubdir):
    """-I of the board headers that exist in the repository (hw.cc and its neighbours need FreeRTOS, the U64 headers); a header that is nowhere is stubbed empty on demand."""
    ps, vs = os.path.join(ROOT, "host-app", "software"), os.path.join(ROOT, "vendor", "gideon", "software")
    ds = [vs + "/FreeRTOS/Source", vs + "/FreeRTOS/Source/include", vs + "/FreeRTOS/Source/portable/risc-v",
          vs + "/FreeRTOS/Source/portable/risc-v/chip_specific_extensions/RV32I_CLINT_no_extensions", ps + "/portable/riscv", ps + "/u64ii", ps + "/u64", ps + "/u64ctrl/main",
          ps + "/io/uart", ps + "/io/usb", ps + "/api", ps + "/system", vs + "/io/audio", vs + "/appsdk/include", vs + "/network", vs + "/system", ROOT + "/coreapp/src/trxmon",
          ROOT + "/coreapp/ffconf", ps + "/lvgl"]
    return ["-I" + d for d in ds if os.path.isdir(d)] + ["-I" + stubdir]


def syntax(src, drv, work, onlydrv):
    """Compile src with its driver table first. Returns the error lines (file:line: message). A header that does not exist is stubbed empty and the compile repeated; with onlydrv
    only the errors that name a DRV_ macro count (a file that needs the board's headers, hw.cc)."""
    stub = os.path.join(work, "stub")
    os.makedirs(stub, exist_ok=True)
    wrap = os.path.join(work, "w_" + os.path.basename(src))
    with open(wrap, "w") as f:
        f.write('#include "%s"\n#include "%s"\n' % (os.path.join(drv, "drvtab.h"), src))
    cmd = [CXX, "-std=gnu++14", "-w", "-DVIDEOMODE_NO_BOARD=1", "-DLV_CONF_INCLUDE_SIMPLE", "-DTRX_HOSTAPP=1", "-DTRX_COREAPP=1", "-fsyntax-only", "-fmax-errors=0"] \
        + includes_native(drv) + stub_incs(stub) + [wrap]
    for _ in range(60):
        p = sh(["nice", "-n", "15"] + cmd)
        m = re.search(r"fatal error: ([A-Za-z0-9_./+-]+): No such file", p.stderr)
        if not m:
            break
        os.makedirs(os.path.dirname(os.path.join(stub, m.group(1))), exist_ok=True)
        open(os.path.join(stub, m.group(1)), "a").close()
    errs = [l for l in p.stderr.split("\n") if re.search(r": (fatal )?error:", l)]
    if onlydrv:
        errs = [l for l in errs if "DRV_" in l]
    return [l.replace(wrap, src) for l in errs]


def sdk_driver_sources(libs):
    """The SDK sources that read the driver table (grep DRV_/drvtab.h) and are in this app's link: the base's always, a library's when the app has the library."""
    inlib = {}
    for l in shared_libs():
        for f in l.get("sources", []):
            inlib[f] = l["name"]
    out = []
    for d in (SRC, os.path.join(ROOT, "app", "lib", "cbm1541")):
        for f in sorted(os.listdir(d)):
            if not f.endswith(".cc"):
                continue
            t = rd(os.path.join(d, f))
            if "DRV_" not in t and "drvtab.h" not in t:
                continue
            if f in inlib and inlib[f] not in libs:
                continue
            out.append(os.path.join(d, f))
    return out


def merge_drv(toml_a, sdk, out):
    import app_build
    app = descrec.toml_loads(rd(sdk))["app"]
    app_build.merge_a({"toml-a": toml_a, "toml-b": sdk, "base-toml-a": os.path.join(ROOT, "coreapp", "gateware", app.get("base_toml_a", "shared.toml"))},
                      list(app.get("base_blocks", [])), [os.path.join(ROOT, "app", "sdkpkg", "maps")], out)


_BASELINE = {}


def baseline_envlimited(src, work):
    """True when src does not compile natively even against the VIC-20's table: only its DRV_ errors say anything."""
    if src not in _BASELINE:
        d = os.path.join(work, "vc20drv")
        if not os.path.exists(os.path.join(d, "drv", "drvtab.h")):
            merge_drv(os.path.join(ROOT, "cores", "vc20", "app", "gateware.toml"), os.path.join(ROOT, "cores", "vc20", "app", "sdk.toml"), d)
        _BASELINE[src] = bool(syntax(src, os.path.join(d, "drv"), os.path.join(work, "b"), False))
    return _BASELINE[src]


def add_drive(inf, toml_a):
    """A core whose code uses the shared 1541 library needs the block drive_a in its TOML A (hw.cc and d1541_drive.cc name it): the map's block is appended."""
    if "cbm1541" in inf["libs"] and 'name = "drive_a"' not in rd(toml_a):
        with open(toml_a, "a") as f:
            f.write(DRIVE_A)
        return True
    return False


def compile_core(inf, toml_a, sdk, srcdir, work):
    """[(source, first error, [all errors])] for the core's own sources (in srcdir) and the SDK's driver sources against the app's driver table."""
    drvroot = os.path.join(work, "drv")
    merge_drv(toml_a, sdk, drvroot)
    drv = os.path.join(drvroot, "drv")
    res = []
    for f in inf["own"]:
        if f.endswith(".cc"):
            e = syntax(os.path.join(srcdir, f), drv, work, False)
            if e:
                res.append((f, e[0], e))
    for s in sdk_driver_sources(inf["libs"]):
        e = syntax(s, drv, work, baseline_envlimited(s, work))
        if e:
            res.append((os.path.relpath(s, ROOT), e[0], e))
    return res


# ---------------------------------------------------------------- commands

def plan(core, out=None):
    inf = info(core)
    w = lambda s="": print(s, file=out or sys.stdout)
    w("core_move plan: %s (%s core)" % (core, inf["kind"]))
    w("  descriptor        %s_desc: row of coreapp/src/table.cc, extern of coreapp/cores/cores.h" % core)
    w("  move to cores/%s/app/lib (git mv from coreapp/cores): %s" % (core, " ".join(inf["own"]) or "(nothing)"))
    if inf["shared"]:
        w("  SHARED, stays (judgement: split or copy): " + " ".join("%s [%s]" % (f, ",".join(c for c, fl in inf["shared_with"].items() if f in fl)) for f in inf["shared"]))
    w("  shared libraries  %s (sdk.toml [[library]] copied from coreapp/libs/shared.toml, `needs`)" % (" ".join(inf["libs"]) or "none"))
    w("  new files         cores/%s/app/sdk.toml (template), gateware.toml (rtl_check.py emit%s), %s" % (
        core, " from " + ", ".join(os.path.relpath(h, ROOT) for h in inf["headers"]),
        "rtl_check_sources.tcl exists" if inf["has_rts"] else "rtl_check_sources.tcl only if core_sources.tcl has a filter the checker cannot read"))
    w("  rtl_check source list: %s; top layer2_%s%s" % (" ".join(inf["lists"]) or "NONE FOUND", core, "" if inf["layer2"] else "  (cores/%s/layer2_%s.v is missing)" % (core, core)))
    w("  test case         tests/rtl_check/test_rtl_check.py (appended with the counts of the first run)")
    units = sh(["grep", "-n", "-E", r"\b(%s)" % "|".join(re.escape(f[:-3]) for f in inf["own"] if f.endswith(".cc")), "tests/coreapp/run.sh"], cwd=ROOT).stdout.strip().split("\n")
    w("  tests/coreapp/run.sh: %d line(s) name the core's sources (regroup into the TOML B's `tests`: judgement)" % len([u for u in units if u]))
    w("  lanes             core_toml.py lanes will list \"%s %s\" once sdk.toml names [app] desc" % (core, inf["kind"]))
    for n in inf["notes"]:
        w("  NOTE " + n)
    return inf


def lanes_ok(core):
    p = sh([sys.executable, "tools/core_toml.py", "lanes"], cwd=ROOT)
    return p.returncode == 0 and re.search(r"^%s " % core, p.stdout, flags=re.M) is not None, (p.stdout + p.stderr).strip()


def gates(core, appdir=None, work=None):
    appdir = appdir or os.path.join(ROOT, "cores", core, "app")
    inf = info(core) if not os.path.isdir(os.path.join(appdir, "lib")) else None
    ok = True
    print("gates: rtl_check")
    l = ["--list", os.path.relpath(os.path.join(appdir, "rtl_check_sources.tcl"), os.path.join(ROOT, "host"))] if os.path.exists(os.path.join(appdir, "rtl_check_sources.tcl")) else \
        sum([["--list", x] for x in info(core)["lists"]], [])
    p = sh([sys.executable, "-I", "tools/rtl_check.py", "check", "--design", "host", *l, "--top", "layer2_" + core, "--bus", BUS % core, "--window", WINDOW,
            "--toml-a", os.path.join(appdir, "gateware.toml"), "--maps", "app/sdkpkg/maps", *sum([["--define", d] for d in defines(core)], [])], cwd=ROOT)
    print("  " + (p.stdout + p.stderr).strip().replace("\n", "\n  "))
    ok &= p.returncode == 0
    print("gates: compile the app's sources against its driver table (g++ -fsyntax-only)")
    work = work or tempfile.mkdtemp(prefix="coremove.")
    inf = info(core)
    inf["own"] = [f for f in os.listdir(os.path.join(appdir, "lib")) if f.endswith((".cc", ".h"))] if os.path.isdir(os.path.join(appdir, "lib")) else inf["own"]
    res = compile_core(inf, os.path.join(appdir, "gateware.toml"), os.path.join(appdir, "sdk.toml"), os.path.join(appdir, "lib") if os.path.isdir(os.path.join(appdir, "lib")) else CORES_SRC, work)
    for f, first, _ in res:
        print("  %s: %s" % (f, first))
    print("  %s" % ("%d source(s) with errors" % len(res) if res else "no errors"))
    ok &= not res
    print("gates: tools/check_d24.py")
    p = sh([sys.executable, "tools/check_d24.py"], cwd=ROOT)
    print("  " + (p.stdout + p.stderr).strip().split("\n")[-1])
    ok &= p.returncode == 0
    print("gates: tools/core_toml.py lanes")
    k, out = lanes_ok(core)
    print("  " + (out.replace("\n", "; ") if out else "(empty)") + ("" if k else "   <- the core is not listed"))
    ok &= k
    return ok


def apply(core, full):
    inf = plan(core)
    app = os.path.join(ROOT, "cores", core, "app")
    if os.path.exists(os.path.join(app, "sdk.toml")):
        print("core_move: cores/%s/app/sdk.toml exists: the core has moved" % core)
        return 1
    if not inf["layer2"]:
        print("core_move: no layer2 RTL to emit from (see the plan): stop")
        return 1
    os.makedirs(os.path.join(app, "lib"), exist_ok=True)
    for f in inf["own"]:
        os.makedirs(os.path.dirname(os.path.join(app, "lib", f)), exist_ok=True)
        subprocess.check_call(["git", "mv", os.path.join("coreapp", "cores", f), os.path.join("cores", core, "app", "lib", f)], cwd=ROOT)
    rc, out = run_emit(inf, os.path.join(app, "gateware.toml"), full, 2, os.path.join(tempfile.gettempdir(), "emit_%s.json" % core))
    print(out.strip().split("\n")[-1])
    add_drive(inf, os.path.join(app, "gateware.toml"))
    with open(os.path.join(app, "sdk.toml"), "w") as f:
        f.write(sdk_toml(inf))
    t = rd(os.path.join(SRC, "table.cc"))
    open(os.path.join(SRC, "table.cc"), "w").write(re.sub(r"^\s*&%s_desc,\n" % core, "", t, flags=re.M))
    h = rd(os.path.join(CORES_SRC, "cores.h"))
    open(os.path.join(CORES_SRC, "cores.h"), "w").write(re.sub(r"^extern const CoreDesc %s_desc;\n" % core, "", h, flags=re.M))
    print("core_move: written: cores/%s/app/{lib/*, sdk.toml, gateware.toml}; table.cc and cores.h lose %s_desc" % (core, core))
    print("JUDGEMENT (the runbook, .claude/skills/core-move/SKILL.md): the moved sources include headers of coreapp/cores by name and name registers by number (use drvtab.h, "
          "the way amiga1200_regs.h does); shared files %s; tests/coreapp/run.sh units; the core's rtl_check case in tests/rtl_check/test_rtl_check.py; desc.toml; names and blocks of gateware.toml; comments" % (
              ", ".join(inf["shared"]) or "(none)"))
    return 0 if gates(core) else 1


def dryrun(cores, out, full, jobs):
    os.makedirs(out, exist_ok=True)
    rows = []
    for c in cores:
        inf = info(c)
        d = os.path.join(out, c)
        os.makedirs(d, exist_ok=True)
        row = dict(core=c, kind=inf["kind"], blocks="-", regs="-", unnamed="-", rtl="-", comp=[], notes=list(inf["notes"]))
        if inf["shared"]:
            row["notes"].append("shared: " + " ".join(inf["shared"]))
        if not inf["layer2"]:
            row["rtl"] = "no layer2_%s.v" % c
        else:
            toml = os.path.join(d, "gateware.toml")
            rc, o = run_emit(inf, toml, full, jobs, os.path.join(out, c + ".scan.json"))
            open(os.path.join(d, "emit.log"), "w").write(o)
            if inf.get("generated_list"):
                row["notes"].append("no core_sources.tcl: the checker was given the files of `core_toml.py sources` (%s)" % os.path.relpath(os.path.join(d, "rtl_check_sources.tcl"), out))
            m = re.search(r"(\d+) block\(s\), (\d+) propert\w+ \((\d+) named from the headers, (\d+)", o)
            if m:
                row["blocks"], row["regs"], row["unnamed"] = m.group(1), m.group(2), m.group(4)
            if rc == 0:
                row["rtl"] = "agrees"
            else:
                err = [l for l in o.split("\n") if l.startswith("rtl_check") or "Error" in l or "Unsupported" in l]
                row["rtl"] = ("FAIL: " + (err[-1] if err else o.strip().split("\n")[-1]))[:200]
        if not os.path.exists(os.path.join(d, "gateware.toml")) or row["rtl"] != "agrees" and row["blocks"] == "-":
            # no TOML A from the RTL: the sources are still compiled, against the standard block alone (what every TOML A has), so the SDK's and the core's own breaks show
            with open(os.path.join(d, "gateware.toml"), "w") as f:
                f.write(MIN_A)
            row["notes"].append("TOML A not emitted: its sources were compiled against the standard block alone (ident: id, caps, caps2, build)")
        if os.path.exists(os.path.join(d, "gateware.toml")) and add_drive(inf, os.path.join(d, "gateware.toml")):
            row["notes"].append("drive_a added as the map cbm1541 (the core uses the shared 1541 library): not from the RTL, see the TODO in gateware.toml")
        if os.path.exists(os.path.join(d, "gateware.toml")):
            with open(os.path.join(d, "sdk.toml"), "w") as f:
                f.write(sdk_toml(inf))
            try:
                row["comp"] = compile_core(inf, os.path.join(d, "gateware.toml"), os.path.join(d, "sdk.toml"), CORES_SRC, d)
            except Exception as e:      # the TOML A does not merge with the host's: that is a break of its own
                row["comp"] = [("(merge of the TOML A with the host's)", str(e)[:200], [])]
        rows.append(row)
        print("dryrun: %s: blocks %s, registers %s, unnamed %s, rtl_check %s, %d source(s) with errors" % (c, row["blocks"], row["regs"], row["unnamed"], row["rtl"][:60], len(row["comp"])), flush=True)
    with open(os.path.join(out, "table.md"), "w") as f:
        f.write("| core | kind | blocks | registers | unnamed | rtl_check | first compile error per source |\n|---|---|---|---|---|---|---|\n")
        for r in rows:
            f.write("| %s | %s | %s | %s | %s | %s | %s |\n" % (r["core"], r["kind"], r["blocks"], r["regs"], r["unnamed"], r["rtl"].replace("|", "/"),
                    "<br>".join("%s: %s" % (s, e.replace("|", "/").replace(ROOT + "/", "")) for s, e, _ in r["comp"]) or "none"))
        f.write("\n")
        for r in rows:
            for n in r["notes"]:
                f.write("- %s: %s\n" % (r["core"], n))
    print(rd(os.path.join(out, "table.md")))
    return 0


def sdk_variants(toml_a, out):
    """The SDK's base sources (the ones that read the driver table and are in no library) compiled against the table with one optional base block taken out at a time, and with
    all of them out: every error that names a DRV_ macro is a driver that serves a block only some bitstreams have and does not test #ifdef DRV_HAS_<block> (api.md 6.17.5)."""
    os.makedirs(out, exist_ok=True)
    base = [s for s in sdk_driver_sources([]) if not any(os.path.basename(s) in l.get("sources", []) for l in shared_libs())]
    optional = BASE_BLOCKS + ["iec_status"]
    rows = []
    text = rd(toml_a)
    nocaps = re.sub(r'  \[\[block\.property\]\][^\n]*\n  name = "caps"\n(?:  [a-z]+ = [^\n]*\n)+', "", text, count=1)
    variants = [("without " + b, [b], toml_a) for b in optional] + [("without all of them", optional, toml_a)]
    if nocaps != text:
        nc = os.path.join(out, "nocaps.toml")
        open(nc, "w").write(nocaps)
        variants.append(("without ident.caps", [], nc))
    for name, drop, ta in variants:
        d = os.path.join(out, name.replace(" ", "_"))
        os.makedirs(d, exist_ok=True)
        sdk = os.path.join(d, "sdk.toml")
        with open(sdk, "w") as f:
            f.write('[app]\nname = "x"\ndesc = "x_desc"\ntoml_a = "gateware.toml"\nbase_toml_a = "host.toml"\nbase_blocks = %s\n' % toml_val([b for b in optional if b not in drop]))
        merge_drv(ta, sdk, d)
        for s in base:
            e = [x for x in syntax(s, os.path.join(d, "drv"), d, True)]
            if e:
                rows.append((name, os.path.relpath(s, ROOT), e[0].replace(ROOT + "/", "")))
    return rows


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    cmd, rest = argv[1], argv[2:]
    full = "--full-scan" in rest
    jobs = int(rest[rest.index("-j") + 1]) if "-j" in rest else 2
    out = rest[rest.index("--out") + 1] if "--out" in rest else None
    cores = [a for i, a in enumerate(rest) if not a.startswith("-") and (i == 0 or rest[i - 1] not in ("--out", "-j"))]
    if cmd == "plan" and cores:
        plan(cores[0])
        return 0
    if cmd == "apply" and cores:
        return apply(cores[0], full)
    if cmd == "gates" and cores:
        return 0 if gates(cores[0]) else 1
    if cmd == "sdkgaps":
        rows = sdk_variants(os.path.join(ROOT, rest[0]) if rest else os.path.join(ROOT, "cores", "vc20", "app", "gateware.toml"), out or tempfile.mkdtemp(prefix="coremove_gaps."))
        for r in rows:
            print("%s: %s: %s" % r)
        print("sdkgaps: %d break(s)" % len(rows))
        return 0
    if cmd == "dryrun":
        return dryrun(cores or table_cores(), out or tempfile.mkdtemp(prefix="coremove_dry."), full, jobs)
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
