#!/usr/bin/env python3
"""Read the cores' descriptors, cores/<name>/core.toml (T94 D1).

A descriptor says what a core is (id, version, the contract it was written for, which
application it runs, whether it is experimental) and which files are its layer: its `sources`
are the pathspecs that were the `core:<name>` section of host/layers.txt before, and they are
what ci/lib.sh hashes into that core's keys.

  tools/core_toml.py list                 core names, in build order (every descriptor, the flash filler too)
  tools/core_toml.py list [cor|cfw]       (with cor or cfw: only the cores of that delivery; cfw also names the large cores that
                                          have only cores/<name>/mono/core.toml)
  tools/core_toml.py list-cor             the cores a user starts: the same without the flash filler (T123) and the build-only cores (T115)
  (a core with `pipeline = false` is left out of all three lists: T92 D-17e)
  tools/core_toml.py sources <name>       the core's pathspecs, one per line
  tools/core_toml.py env <name>           the core's mono_env, one NAME=value per line (a value may hold spaces)
  tools/core_toml.py sdk <name> <key>     one value of cores/<name>/app/sdk.toml (T239.3 D25)
  tools/core_toml.py get <name> <key>     one value ("true"/"false" for a flag)
  tools/core_toml.py --check              every descriptor against the schema below

A core may also carry cores/<name>/mono/core.toml, the descriptor of a LARGE core (delivery = "cfw", flat build): `list` and the DFX
stages do not see it, `list cfw`, `sources`, `get` and `env` do (T100 D9: ci/plan.py, ci/bitstream_mono.sh), --check validates it, and
TRX_CORE_DESC may name another file for `get`/`env` (cores/amiga1200/mono/build.sh).

Only the TOML this format needs is read (one flat table, strings, integers, flags and arrays
of strings), so no toml library is required on any runner.
"""
import os, re, sys

# the tree whose descriptors are read: this file's own, or the checkout CI_DATA_ROOT names (ci/lib.sh core_toml_of, ci/plan.py, ci/keys.sh --data:
# this file of the trusted tree over another tree's cores/, which is only read; T239.1 D14)
ROOT = os.environ.get("CI_DATA_ROOT") or os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CORES = os.path.join(ROOT, "cores")
# name: (type, required)
SCHEMA = {
    "id":           ("str",  True),
    "name":         ("str",  True),
    # T209 D15: the boot menu's text tile for a core without a logo, 1 to 4 of A-Z a-z 0-9 + / . (cor_pack.py --short); the app falls back to
    # the first 4 letters of the name when a core says none; "name" is at most 30 characters (D17)
    "short":        ("str",  False),
    "version":      ("str",  True),
    "contract":     ("int",  True),
    "app":          ("str",  True),     # "blueprint" | "ultimate" | "none"
    "order":        ("int",  True),     # build order; the reference core is 0
    "experimental": ("bool", True),
    "sources":      ("list", True),
    "ooc":          ("list", False),    # extra options for the out-of-context synthesis
    "features":     ("list", False),    # "TRX_FEATURES=jtag", "TRX_SID=redip2"
    "roms":         ("list", False),    # names only, never files (clean room)
    # the partition top, when it is not this core's own layer2_<id>.v: a core that is another core's
    # gateware with other parameters (cores/amiga1200) names that file here, so the contract check
    # sees it too instead of silently skipping the core.
    "top":          ("str",  False),
    # Vivado directives for this core's place and route against the host (host/core.tcl reads T79_PLACE and
    # T79_ROUTE); ci/bitstream_layers.sh sets them for this core only, so the host key and the other cores stay.
    "place":        ("str",  False),
    "route":        ("str",  False),
    # T123: the core is not a machine but the filler of the core partition in the host update file's bitstream (cores/null). It is built
    # like any core and never packed as a .cor, listed in a release or shown in the boot menu: list-cor leaves it out.
    "flash_filler": ("bool", False),
    # T115: the core is built only so that something else can be built against it (cores/c64ref: the host's static is routed with it). It is
    # never shipped: no .cor, no release set, no boot menu entry, no app. list-cor leaves it out like the flash filler; the flash filler's own
    # checks (the host key in its image, the static diff) are not part of it.
    "build_only":   ("bool", False),
    # How the core reaches a board (T113 Q12, T115). "cor" (the default) is a partial bitstream for layer 2 and the
    # bundle of tools/cor_pack.py. "cfw" is a monolithic full bitstream - host and core flat in one design, built by
    # cores/<id>/mono/build_mono.tcl (ci/bitstream_mono.sh) - that the host starts as a .cfw (tools/pack_cfw_mc.sh). A
    # "cfw" core is not implemented against the host (ci/bitstream_layers.sh skips it) and has no .cor.
    "delivery":     ("str",  False),
    # The environment of that flat build, "NAME=value" each (the variant: TRX_DEFS, TRX_Z80, TRX_REU, ... of build_mono.tcl).
    "mono_env":     ("list", False),
    # Files the core installs on the SD (record 8 of its .cor, tools/cor_pack.py): every file below cores/<id>/<install_dir>/ is written to the core's
    # folder at the same relative path when it is missing; cores/<id>/<install_old>/<name>.v<N>.<ext> are earlier shipped texts of <name>.<ext>, which
    # the application replaces when the file on the card is byte for byte one of them. Only the core that names the folder carries the files.
    "install_dir":  ("str",  False),
    "install_old":  ("str",  False),
    # T92 D-17e: false = the core's code stays in the tree and no pipeline stage builds its gateware, packs, ships or benches it (list, list cfw, list-cor,
    # ci/plan.py, ci/sims.sh without a bench named, ci/orchestrate.py check, tools/gen_roms_txt.py leave it out). Default true. --check, get, sources and env
    # still read it; the core application keeps its code (table.cc) and its unit tests (tests/coreapp/run.sh) still run.
    "pipeline":     ("bool", False),
    # T239.3 D15 (D11, D13): the core's heap in bytes. The packer writes it into the heap word of the core's copy of the application image
    # (tools/cor_pack.py --heap, coreapp/src/heapcfg.c) and the application defines its FreeRTOS heap_5 region of that size at start; the board
    # parses no TOML. Absent = HEAP_DEFAULT, the heap every core had before. At least HEAP_MIN, a multiple of 16, at most HEAP_MAX; whether the image
    # fits with it is the packer's check.
    "heap":         ("int",  False),
}
# T239.3 D25: cores/<id>/app/sdk.toml, the switches of a core on its way onto the SDK. It lies in the core's app part (cores/*/app/** is no part of a bitstream key), not in
# core.toml (cores/<id> is). desc_record = true: the packers put cores/<id>/app/desc.toml into the .cor as record 6 and the application takes the core's data from it
# (coreapp/src/bundle.cc). Absent file, absent key or false: the file rests in the tree (test_descgen keeps it equal to the C tables), the .cor carries no record and the core runs on
# its compiled-in descriptor. True needs the file.
SDK_SCHEMA = {"desc_record": "bool"}
HEAP_DEFAULT = 0x7A0000
HEAP_MIN = 0x10000
HEAP_MAX = 0xE60000                           # the application region's length (host-app/target/u64ii/riscv/coreapp/linker.x)
APPS = ("blueprint", "ultimate", "none")
DELIVERIES = ("cor", "cfw")


def parse(path):
    """The flat table of a core.toml as a dict of str/int/bool/list values."""
    out, lines = {}, open(path, encoding="utf-8").read().split("\n")
    i = 0
    while i < len(lines):
        line = re.sub(r"\s+#.*$", "", lines[i]).strip()
        i += 1
        if not line or line.startswith("#"):
            continue
        if line.startswith("["):
            raise SystemExit("%s: tables are not part of the format (%s)" % (path, line))
        m = re.match(r"^(\w+)\s*=\s*(.*)$", line)
        if not m:
            raise SystemExit("%s: cannot read %r" % (path, line))
        key, val = m.group(1), m.group(2)
        while val.count("[") > val.count("]"):      # an array over several lines
            val += " " + re.sub(r"\s+#.*$", "", lines[i]).strip()
            i += 1
        if val.startswith("["):
            out[key] = [v.strip().strip('"') for v in val[1:val.rindex("]")].split(",") if v.strip()]
        elif val in ("true", "false"):
            out[key] = val == "true"
        elif re.match(r"^(-?\d+|0x[0-9a-fA-F]+)$", val):     # T239.3 D15: a size may be written 0x7A0000
            out[key] = int(val, 16) if val.startswith("0x") else int(val)
        elif val.startswith('"'):
            out[key] = val[1:val.rindex('"')]
        else:
            raise SystemExit("%s: %s has no value this format knows (%r)" % (path, key, val))
    return out


def in_pipeline(table):
    """T92 D-17e: a core with `pipeline = false` is out of the gateware build, packing and benches; the default is in."""
    return table.get("pipeline", True) is not False


def delivery_of(table):
    return table.get("delivery", "cor")


def descriptors():
    """(name, path, table) of every core, in build order."""
    out = []
    for name in sorted(os.listdir(CORES)):
        p = os.path.join(CORES, name, "core.toml")
        if os.path.exists(p):
            out.append((name, p, parse(p)))
    return sorted(out, key=lambda e: (e[2].get("order", 99), e[0]))


def large_descriptors():
    """(name, path, table) of the cores that carry a LARGE-core descriptor cores/<name>/mono/core.toml next to (or instead of) the core.toml.
    No pipeline stage reads these (the lists and sources above do not); the flat build of the core does, through TRX_CORE_DESC (below)."""
    out = []
    for name in sorted(os.listdir(CORES)):
        p = os.path.join(CORES, name, "mono", "core.toml")
        if os.path.exists(p):
            out.append((name, p, parse(p)))
    return out


def table_of(name):
    """The table `get` and `env` read: the file TRX_CORE_DESC names (the flat build of a core whose pipeline descriptor is a partial one,
    cores/amiga1200/mono/build.sh), else cores/<name>/core.toml."""
    f = os.environ.get("TRX_CORE_DESC", "")
    if f:
        t = parse(f if os.path.isabs(f) else os.path.join(ROOT, f))
        if t.get("id") == name:
            return t
    t = dict((n, t) for n, _, t in descriptors())
    if name in t:
        return t[name]
    # T100 D9: a LARGE core has only cores/<name>/mono/core.toml; the pipeline (ci/plan.py, ci/bitstream_mono.sh, ci/lib.sh mono_key) reads it from there
    return dict((n, t) for n, _, t in large_descriptors())[name]


def sdk_table(name):
    """The flat table of cores/<name>/app/sdk.toml ({} when there is none)."""
    p = os.path.join(CORES, name, "app", "sdk.toml")
    return parse(p) if os.path.isfile(p) else {}


def check_sdk():
    bad = 0
    for name in sorted(os.listdir(CORES)):
        p = os.path.join(CORES, name, "app", "sdk.toml")
        if not os.path.isfile(p):
            continue
        rel = p.replace(ROOT + "/", "")
        for key, v in parse(p).items():
            if key not in SDK_SCHEMA:
                print("%s: %s is not a field of the format" % (rel, key))
                bad += 1
            elif not isinstance(v, bool):
                print("%s: %s is not a bool" % (rel, key))
                bad += 1
        if parse(p).get("desc_record") is True and not os.path.isfile(os.path.join(CORES, name, "app", "desc.toml")):
            print("%s: desc_record = true, but cores/%s/app/desc.toml does not exist" % (rel, name))
            bad += 1
    return bad


def check():
    bad = check_sdk()
    seen = {}
    large = large_descriptors()
    for name, path, t in descriptors() + large:
        is_large = (name, path, t) in large
        def err(msg):
            print("%s: %s" % (path.replace(ROOT + "/", ""), msg))
            return 1
        for key, (typ, required) in SCHEMA.items():
            if key not in t:
                if required:
                    bad += err("%s is missing" % key)
                continue
            v = t[key]
            ok = {"str": isinstance(v, str), "int": isinstance(v, int) and not isinstance(v, bool),
                  "bool": isinstance(v, bool), "list": isinstance(v, list)}[typ]
            if not ok:
                bad += err("%s is not a %s" % (key, typ))
        for key in t:
            if key not in SCHEMA:
                bad += err("%s is not a field of the format" % key)
        if isinstance(t.get("name"), str) and len(t["name"]) > 30:
            bad += err("name %r is %d characters, at most 30 (T209 D17: it is the row's text in the boot menu)" % (t["name"], len(t["name"])))
        if "short" in t and not re.fullmatch(r"[A-Za-z0-9+/.]{1,4}", str(t["short"])):
            bad += err("short %r is not 1 to 4 of A-Z a-z 0-9 + / ." % (t["short"],))
        if isinstance(t.get("heap"), int) and not isinstance(t.get("heap"), bool) and not (HEAP_MIN <= t["heap"] <= HEAP_MAX and t["heap"] % 16 == 0):
            bad += err("heap 0x%x is not a multiple of 16 between 0x%x and 0x%x" % (t["heap"], HEAP_MIN, HEAP_MAX))
        if t.get("id") != name:
            bad += err("id %r is not the directory name %r" % (t.get("id"), name))
        if t.get("app") not in APPS and "app" in t:
            bad += err("app %r is none of %s" % (t.get("app"), ", ".join(APPS)))
        if "delivery" in t and t["delivery"] not in DELIVERIES:
            bad += err("delivery %r is none of %s" % (t["delivery"], ", ".join(DELIVERIES)))
        if is_large and t.get("delivery") != "cfw":
            bad += err("a large-core descriptor (mono/core.toml) says delivery = \"cfw\"")
        if t.get("delivery") == "cfw":
            if not os.path.exists(os.path.join(CORES, name, "mono", "build_mono.tcl")):
                bad += err("delivery = \"cfw\" needs cores/%s/mono/build_mono.tcl" % name)
            # T255 D6: a large core that is not a machine (cores/ddt_mandel: the demo and its HDMI, no CPU in the bitstream) runs no application: app = "none", the .cor
            # carries a placeholder record (ci/package.sh)
            if t.get("app") not in ("blueprint", "none"):
                bad += err("delivery = \"cfw\" runs the generic application (app = \"blueprint\") or none (app = \"none\")")
        elif "mono_env" in t:
            bad += err("mono_env belongs to delivery = \"cfw\"")
        for e in t.get("mono_env", []):
            if not re.match(r"^[A-Z][A-Z0-9_]*=", e):
                bad += err("mono_env entry %r is not NAME=value" % e)
        if not re.match(r"^\d+\.\d+\.\d+$", str(t.get("version", ""))):
            bad += err("version %r is not <major>.<minor>.<patch>" % t.get("version"))
        cv = open(os.path.join(ROOT, "contract", "VERSION"), encoding="utf-8").read().strip()
        if str(t.get("contract")) != cv:
            bad += err("contract %s is not the contract/VERSION %s" % (t.get("contract"), cv))
        if not is_large and t.get("order") in seen:
            bad += err("order %s is also %s's" % (t.get("order"), seen[t["order"]]))
        if not is_large:
            seen[t.get("order")] = name
        for s in t.get("sources", []):
            spec = s[len(":(exclude)"):] if s.startswith(":(exclude)") else s
            # Its own directory, a vendored tree, or ANOTHER CORE - the last one deliberately: two
            # cores can be one gateware with two power-up configurations (amiga and amiga1200), and
            # then the second one must have the first in its key, or it would serve a stale cache
            # entry after a change. It still may not edit it (T94 D12); a shared file that belongs
            # to no core goes to cores/common, which is in the host key, not here.
            other = spec.split("/")[1] if spec.startswith("cores/") and "/" in spec[6:] else spec[6:]
            if spec.startswith("cores/%s" % name) or spec.startswith("vendor/"):
                continue
            if spec.startswith("cores/") and os.path.isdir(os.path.join(CORES, other)):
                continue
            bad += err("sources holds %s, which is neither under cores/%s, another core nor vendor/" % (s, name))
    print("core.toml: %s" % ("ok (%d cores, %d large-core descriptors)" % (len(descriptors()), len(large)) if not bad else "%d problem(s)" % bad))
    return 1 if bad else 0


def main():
    a = sys.argv[1:]
    if a[:1] == ["--check"]:
        sys.exit(check())
    if a[:1] == ["list"] and len(a) in (1, 2):
        if len(a) == 2 and a[1] not in DELIVERIES:
            raise SystemExit("list: %r is none of %s" % (a[1], ", ".join(DELIVERIES)))
        names = [n for n, _, t in descriptors() if in_pipeline(t) and (len(a) == 1 or delivery_of(t) == a[1])]
        if a[1:] == ["cfw"]:        # T100 D9: the large cores (mono/core.toml only) are cores with delivery = "cfw" too
            names += [n for n, _, t in large_descriptors() if n not in names and in_pipeline(t)]
        print("\n".join(names))
    elif a[:1] == ["list-cor"]:
        print("\n".join(n for n, _, t in descriptors() if in_pipeline(t) and not t.get("flash_filler") and not t.get("build_only") and delivery_of(t) != "cfw"))
    elif a[:1] == ["sources"] and len(a) == 2:
        print("\n".join(table_of(a[1]).get("sources", [])))
    elif a[:1] == ["env"] and len(a) == 2:
        t = table_of(a[1])
        print("\n".join(t.get("mono_env", [])))
    elif a[:1] == ["sdk"] and len(a) == 3:          # one value of cores/<name>/app/sdk.toml ("" when there is none)
        v = sdk_table(a[1]).get(a[2], "")
        print("true" if v is True else "false" if v is False else v)
    elif a[:1] == ["get"] and len(a) == 3:
        t = table_of(a[1])
        v = t.get(a[2], "")
        print("true" if v is True else "false" if v is False else
              " ".join(v) if isinstance(v, list) else v)
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":        # importable: contract_ports.py reads the descriptors too
    main()
