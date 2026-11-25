#!/usr/bin/env python3
"""T100 D9, build by change: from the files a push changed to the targets the pipeline builds.

  ci/plan.py make --base <commit>|none [--head HEAD] [--branch B] [--source <text>] [--out plan.json] [--files-out f] [--pretty]
                                    the plan of the diff base..head (base none = no base known: everything is named; --branch B or
                                    GITHUB_REF_NAME / the checkout decides the scope: main, integration, integ-*, release/* and tags build everything
                                    that changed (D2.6-8), any other branch only the cores it touches plus host, reference core and null (D2 addendum))
  ci/plan.py has <plan.json> <target>      exit 0 when the plan names the target ("host", "dfx:vc20", "large:amiga1200", "aux",
                                           "app:core", ...), the lane runs ("lane:host", "lane:dfx:vc20", "lane:large:amiga1200") or "fundamental" (full benches);
                                           a missing plan file names everything
  ci/plan.py limit <kind> [plan.json]      minutes one Vivado run of kind host|core|large may take (plan, else ci/targets.txt)
  ci/plan.py show <plan.json>              the human summary
  ci/plan.py lanes                         the lanes of this tree, one per line: host, core:<id> (DFX), large:<id>

The diff is evaluated by git itself (pathspecs with :(exclude), exactly as ci/lib.sh hashes them), in the repository this file
sits in, or in the checkout CI_DATA_ROOT names (ci/keys.sh --data): that tree is only read; code_hash.py, simreads.py and
tools/core_toml.py always come from this file's own tree (core_toml.py runs with python3 -I).
Paths -> targets: host and the descriptors' sources as ci/lib.sh keys them, the static rows of ci/targets.txt,
consumers by the `expand` rows. Sims: a bench of ci/sims.txt with a line in ci/sims.deps runs when its declared inputs
changed (or its own lines in sims.txt / sims.deps / the owner row) AND the lane that owns it (ci/sims.lane, T100 D10) fires - the host lane,
a DFX core's lane, a large core's lane - or its own bench source (simscope) changed. Benches owned by `none` (source trees that are never
built: cores/c64u) are never planned.

plan.json (version 1):
  head, base{commit, source}, all (no base: everything), changed (number of code files), branch, scope ("full" | "branch"),
  fundamental (bool: the full benches run, D2.6), tests (bool: the application tests changed, D4), docs_only (bool: only documentation changed, D2.1),
  host, aux (bool), dfx[], large[] (named cores), apps{host, core, ultimate},
  lanes{host, dfx[], large[]} (T100 D10: the lanes whose triggers fired, see lanes_of),
  package{cor[], cfw[] (everything the package carries; it is published whole), fresh[] (those whose content moves)},
  sims[] (bench names), universe{dfx[], large[], cor[], reference, own[]}, limits{host, core, large}, notes[]
  universe.own (T239.5 D13): the cores with an app of their own (tools/core_toml.py lanes); their .cor comes from their matrix lane, not from the child cor
"""
import json, os, re, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import code_hash

HERE = os.path.dirname(os.path.abspath(__file__))      # the code: this file's folder (code_hash.py, simreads.py)
CODE = os.path.dirname(HERE)                           # its tree (tools/core_toml.py)
ROOT = os.environ.get("CI_DATA_ROOT") or CODE          # the tree that is read: git, host/layers.txt, the cores, ci/targets.txt, ci/sims.*
EXC = ":(exclude)"


def git(*a, check=True):
    r = subprocess.run(["git", "-C", ROOT] + list(a), capture_output=True, text=True)
    if check and r.returncode:
        raise SystemExit("git %s: %s" % (" ".join(a), r.stderr.strip()))
    return r.stdout


def core_toml(*a):
    return subprocess.run([sys.executable, "-I", os.path.join(CODE, "tools", "core_toml.py")] + list(a),
                          capture_output=True, text=True, check=True, env=dict(os.environ, CI_DATA_ROOT=ROOT)).stdout.strip()


def changed_in(base, head, specs):
    """Files that differ between base and head under the pathspecs (git decides, :(exclude) narrows)."""
    if not any(not s.startswith(EXC) for s in specs):
        return set()
    out = git("diff", "--name-only", "--no-renames", "-z", base, head, "--", *specs)
    return set(p for p in out.split("\0") if p and code_differs(base, head, p))


def code_differs(base, head, path):
    """T215 D1 (Alex: "The hash should only concern code. Always."): a file counts as changed only when its normalised content (ci/code_hash.py,
    the very function ci/lib.sh keys with) differs; a comment-only edit changes nothing."""
    def blob(rev):
        r = subprocess.run(["git", "-C", ROOT, "show", "%s:%s" % (rev, path)], capture_output=True)
        return r.stdout if r.returncode == 0 else None
    return code_hash.differs(blob(base), blob(head), path)


# D2.1 (Alex, 2026-11-07: "Only real code triggers anything"): documentation, specs, TODO and notes move no key and name no target. A file of
# this kind inside a code tree (host/README.md, mono/host/FROM) is no part of any content key either (ci/code_hash.py is_doc, 2026-11-15); the
# plan does not name it and code_differs says "unchanged" for it.
def is_noncode(p):
    base = os.path.basename(p)
    return p.startswith(("docs/", ".claude/")) or p.endswith(".md") or base.startswith("TODO") or base in ("LICENSE", "LICENSE.txt")


def changed_names(base, head, specs):
    """Files under the pathspecs that differ by a single byte (no code normalisation): what a .cor installs on the card is the file as it is."""
    if not specs:
        return set()
    return set(p for p in git("diff", "--name-only", "--no-renames", "-z", base, head, "--", *specs).split("\0") if p)


def changed_code(base, head, specs):
    return set(f for f in changed_in(base, head, specs) if not is_noncode(f))


def fundamental_specs(kind="benches"):
    """The pathspecs of ci/fundamental.txt, the one list (T174 D2.6): section [all] plus [benches] (touching one runs the full benches) or
    [apptests] (ci/apptests.sh escalates its tier to integration). A path outside every section is an error, so no list can hide a second copy."""
    out, sec = [], None
    for line in open(os.path.join(ROOT, "ci", "fundamental.txt"), encoding="utf-8").read().split("\n"):
        line = re.sub(r"\s+#.*$", "", line).strip()
        if not line or line.startswith("#"):
            continue
        m = re.match(r"^\[(\w+)\]$", line)
        if m:
            sec = m.group(1)
            if sec not in ("all", "benches", "apptests"):
                raise SystemExit("ci/fundamental.txt: unknown section [%s]" % sec)
        elif sec is None:
            raise SystemExit("ci/fundamental.txt: path %s before any section" % line)
        elif sec in ("all", kind):
            out.append(line)
    return out


def full_scope(branch):
    """main, integration, integration branches, release branches and tags build and test everything that changed; any other branch only its own core(s)
    and app(s). t239.1 counts as main while T239.1 D9 lasts (its S8 proof needs the package)."""
    return branch in ("main", "integration", "t239.1") or branch.startswith(("integ-", "release/")) or os.environ.get("GITHUB_REF_TYPE") == "tag"


# ---- ci/targets.txt -------------------------------------------------------------------------------------------------

def read_targets(text=None):
    limits, rows, expand = {}, [], []
    for line in (text if text is not None else open(os.path.join(ROOT, "ci", "targets.txt"), encoding="utf-8").read()).split("\n"):
        line = re.sub(r"\s+#.*$", "", line).strip()
        if not line or line.startswith("#"):
            continue
        f = line.split()
        if f[0] == "limit" and len(f) == 3:
            limits[f[1]] = int(f[2])
        elif f[0] == "expand" and f[2] == "->":
            expand.append((f[1], f[3:]))
        elif "->" in f:
            i = f.index("->")
            rows.append((f[:i], f[i + 1:]))
        else:
            raise SystemExit("ci/targets.txt: cannot read %r" % line)
    for k in ("host", "core", "large"):
        if k not in limits:
            raise SystemExit("ci/targets.txt: no limit for %s" % k)
    return limits, rows, expand


# ---- host/layers.txt ------------------------------------------------------------------------------------------------

def layer_specs(section):
    """The pathspecs of one section of host/layers.txt (the same reading as ci/lib.sh layer_specs)."""
    sec, out = None, []
    for line in open(os.path.join(ROOT, "host", "layers.txt"), encoding="utf-8").read().split("\n"):
        if re.match(r"^\s*(#|$)", line):
            continue
        m = re.match(r"^(host|shared|ignore):\s*$", line)
        if m:
            sec = m.group(1)
            continue
        if line.startswith("reference:"):
            sec = "reference"
            continue
        if sec == section:
            out.append(line.rstrip())
    return out


def reference_core():
    for line in open(os.path.join(ROOT, "host", "layers.txt"), encoding="utf-8"):
        if line.startswith("reference:"):
            return line.split(":", 1)[1].strip()
    return ""


# ---- ci/sims.deps / ci/sims.txt ------------------------------------------------------------------------------------

def parse_reads(text):
    """bench -> its record in ci/sims.reads (T174 D5): the files, the looked-for paths (?p) and listed directories (@d) it read."""
    sys.path.insert(0, HERE)
    import simreads
    return simreads.parse(text)


def reads_specs(lines):
    """The pathspecs the plan watches for a record: its files, the paths it looked for (a file added there) and the directories it listed."""
    return sorted(set(l.lstrip("?@") for l in lines))


def parse_deps(text, reads=None):
    """name -> list of pathspecs (macros resolved), as ci/sims.sh deps_of reads the file. `@reads` stands for the bench's record (T174 D5,
    reads = parse_reads of ci/sims.reads); a bench with `@reads` and no record has no deps at all, which every check reports."""
    entries, cur = {}, None
    for line in text.split("\n"):
        if re.match(r"^[ \t]*(#|$)", line):
            continue
        if line[0] in " \t":
            if cur is not None:
                entries[cur] += " " + line
            continue
        key = line.split()[0]
        if key.startswith("@"):
            body = line[line.index("=") + 1:]
        else:
            key = key.rstrip(":")
            body = line[line.index(":") + 1:]
        entries[key], cur = body, key

    def res(tok, seen=()):
        if not tok.startswith("@"):
            return [tok]
        if tok not in entries or tok in seen:
            return []
        return [x for t in entries[tok].split() for x in res(t, seen + (tok,))]
    def expand(k, v):
        out = []
        for t in v.split():
            if t == "@reads":
                out += reads_specs((reads or {}).get(k, []))
            else:
                out += res(t)
        return out
    return {k: expand(k, v) for k, v in entries.items() if not k.startswith("@")}


def read_reads(root=None):
    p = os.path.join(root or ROOT, "ci", "sims.reads")
    return parse_reads(open(p, encoding="utf-8").read()) if os.path.exists(p) else {}


def parse_owners(text):
    """bench -> owner lane (host | core:<id> | large:<id> | none) from ci/sims.lane (T100 D10); the last row of a bench wins here,
    ci/orchestrate.py check refuses a bench with two."""
    out = {}
    for line in text.split("\n"):
        if line.strip() and not line.lstrip().startswith("#") and ":" in line:
            k, _, v = line.partition(":")
            out[k.strip()] = v.strip()
    return out


def parse_sims(text):
    out = {}
    for line in text.split("\n"):
        if not line.strip() or line.startswith("#"):
            continue
        f = line.split("|", 3)
        if len(f) == 4:
            out[f[1]] = line
    return out


def lanes_of(everything, host, aux, apps, named_dfx, named_large, dfx_all, large_all, scoped=False):
    """T100 D10, Alex 2026-10-25: 'jeder Core 1 Bahn. Aber NUR laufen, wenn sich eines von diesen aendert'.
    host lane   <- host sources (incl. shared and the reference core), the aux designs' files, the host app
    DFX lane    <- the host, any app, the core's own sources (shared ones it reads included: they are in core.toml sources)
    large lane  <- any app, the core's own sources (its copy of the host files included) and the shared files it reads in place; NOT the host (T164 3a Q1: copies)
    scoped (a feature branch, D2 addendum): a core's lane runs only when the core itself is named (null comes with the host), whatever else changed.
    A lane that is not in this list is still a job (this Gitea cannot drop one) and hands over from the cache."""
    app_any = any(apps.values())
    if scoped and not everything:
        return {"host": host or aux or apps["host"], "dfx": sorted(named_dfx), "large": sorted(named_large)}
    return {"host": everything or host or aux or apps["host"],
            "dfx": sorted(dfx_all if everything or host or app_any else named_dfx),
            "large": sorted(large_all if everything or app_any else named_large)}


def show(rev, path):
    r = subprocess.run(["git", "-C", ROOT, "show", "%s:%s" % (rev, path)], capture_output=True, text=True)
    return r.stdout if r.returncode == 0 else ""


def bench_cores(specs):
    return sorted(set(m.group(1) for s in specs for m in [re.match(r"^cores/([^/]+)", s)] if m))


# ---- the plan -------------------------------------------------------------------------------------------------------

def universe(root=None):
    """(reference core, DFX cores, large cores) of the tree: the lanes of the pipeline (T100 D10) - the host lane, one lane per DFX core
    (a descriptor with a core_sources.tcl, not the reference core, not a large core), one per large core (mono/core.toml)."""
    ref = reference_core()
    allcores = core_toml("list").split()
    large_all = [c for c in core_toml("list", "cfw").split() if c]
    dfx_all = [c for c in allcores if c not in large_all and c != ref
               and os.path.exists(os.path.join(ROOT, "cores", c, "core_sources.tcl"))]
    return ref, dfx_all, large_all


def make(base, head, source, files_out=None, branch=None):
    limits, rows, expand = read_targets()
    ref, dfx_all, large_all = universe()
    cor_all = [c for c in core_toml("list-cor").split() if c]
    everything = base == "none"
    changed = set()
    notes = []
    branch = branch or os.environ.get("CI_BRANCH") or os.environ.get("GITHUB_REF_NAME") or git("rev-parse", "--abbrev-ref", "HEAD").strip()
    scoped = not full_scope(branch)
    docs_only = False
    if not everything:
        all_changed = set(p for p in git("diff", "--name-only", "-z", base, head).split("\0") if p)
        changed = set(p for p in all_changed if not is_noncode(p))
        docs_only = bool(all_changed) and not changed
        if docs_only:
            notes.append("only documentation changed (docs/, specs, TODO, *.md): no stage is triggered (D2.1)")
    targets, reason = set(), {}
    packed = set()                  # large cores whose record 8 changed (their .cor is repacked, no Vivado)

    def add(t, why):
        targets.add(t)
        reason.setdefault(t, []).append(why)

    if everything:
        add("host", "no diff base")
        add("aux", "no diff base")
        for c in dfx_all:
            add("dfx:" + c, "no diff base")
        for c in large_all:
            add("large:" + c, "no diff base")
        for t in ("app:host", "app:core", "app:ultimate"):
            add(t, "no diff base")
    else:
        hostspecs = layer_specs("host")
        shared = layer_specs("shared")
        refsrc = core_toml("sources", ref).split("\n") if ref else []
        for f in sorted(changed_code(base, head, hostspecs) | changed_code(base, head, shared) | changed_code(base, head, refsrc)):
            add("host", f)
        for c in dfx_all:
            for f in sorted(changed_code(base, head, core_toml("sources", c).split("\n"))):
                add("dfx:" + c, f)
        # T164 3a Q1 ("Copies instead."): a large core builds from its own copy of the host files (cores/<id>/mono/host, in its sources), so host/ is no
        # input of it; the shared files it reads in place (vendor/icepi/hdmi, cores/common, cores/common/trx_*, tools/vivado_lib.tcl, ...) are (ci/lib.sh mono_key)
        shared_large = [x for x in shared if not re.match(r"^(%s)?host/" % re.escape(EXC), x)]
        for c in large_all:
            specs = core_toml("sources", c).split("\n") + shared_large + [EXC + "cores/%s/sim" % c, EXC + "cores/%s/mono/reports" % c]
            for f in sorted(changed_code(base, head, specs)):
                add("large:" + c, f)
        # record 8 of a large core's .cor (core.toml install_dir / install_old) is no input of its bitstream (not in its sources): a change there repacks the
        # .cor from the cached bitstream (package:cfw:<id>, its lane hands the cached bit over) and builds nothing
        for c in large_all:
            fdirs = ["cores/%s/%s" % (c, d) for d in (core_toml("get", c, "install_dir"), core_toml("get", c, "install_old")) if d]
            for f in sorted(p for p in changed_names(base, head, fdirs) if not is_noncode(p)):
                add("package:cfw:" + c, f)
                packed.add(c)
        for specs, tg in rows:
            for f in sorted(changed_code(base, head, specs)):
                for t in tg:
                    add(t, f)
        for c in dfx_all + large_all:        # a core's own bench source: its bench runs, nothing is built for it
            for f in sorted(changed_code(base, head, ["cores/%s/sim" % c])):
                add("simscope:" + c, f)

    direct = set(targets)          # what the files themselves named, before the consumers are added

    # consumers
    def instances(pattern):
        kind = pattern[:-1]            # "dfx:" of "dfx:*"
        return {"dfx:": dfx_all, "large:": large_all, "package:cor:": cor_all,
                "package:cfw:": large_all}.get(kind, [])
    grown = True
    while grown:
        grown = False
        for left, right in expand:
            if left.endswith("*"):
                src = [t[len(left) - 1:] for t in targets if t.startswith(left[:-1])]
                for s in src:
                    for r in right:
                        if r.endswith("*"):
                            n = r[:-1] + s
                            if s in instances(r) and n not in targets:
                                add(n, "consumer of " + left[:-1] + s)
                                grown = True
            elif left in targets:
                for r in right:
                    if r.endswith("*"):
                        for s in instances(r):
                            if r[:-1] + s not in targets:
                                add(r[:-1] + s, "consumer of " + left)
                                grown = True
                    elif r not in targets:
                        add(r, "consumer of " + left)
                        grown = True

    # D2 addendum (Alex, 2026-11-07: "on a feature branch ... only for the one core and the one app; host + ref core + null core"): a branch that is
    # not main builds the cores it touches itself. A host or app change does not drag every other core in; they come from the cache / the main package.
    # The host lane (host + reference core) is the host's own; null comes along with the host (the host update file is built from its image).
    if scoped and not everything:
        keep = set(t for t in direct if t.startswith(("dfx:", "large:"))) | set("large:" + c for c in packed)
        if "host" in targets and "null" in dfx_all:
            keep.add("dfx:null")
        for t in [t for t in targets if t.startswith(("dfx:", "large:")) and t not in keep]:
            targets.discard(t)
        for t in [t for t in targets if t.startswith("package:")]:
            c = t.split(":")[-1]
            if ("dfx:" + c) not in keep and ("large:" + c) not in keep:
                targets.discard(t)
        notes.append("feature branch %s: only the cores it touches are built (%s), plus host, reference core and null; every other core comes from the cache (D2 addendum)"
                     % (branch, " ".join(sorted(t.split(":", 1)[1] for t in keep)) or "none"))
    named_dfx = sorted(t[4:] for t in targets if t.startswith("dfx:"))
    named_large = sorted(t[6:] for t in targets if t.startswith("large:"))
    host = "host" in targets
    fresh = sorted(t for t in targets if t.startswith("package:"))

    # sims
    sims = []
    nodeps = []
    if everything:
        pass
    deps_now = parse_deps(open(os.path.join(ROOT, "ci", "sims.deps"), encoding="utf-8").read(), read_reads())
    sims_now = parse_sims(open(os.path.join(ROOT, "ci", "sims.txt"), encoding="utf-8").read())
    deps_then = parse_deps(show(base, "ci/sims.deps"), parse_reads(show(base, "ci/sims.reads"))) if not everything else {}
    sims_then = parse_sims(show(base, "ci/sims.txt")) if not everything else {}
    owners = parse_owners(open(os.path.join(ROOT, "ci", "sims.lane"), encoding="utf-8").read())
    lanes = lanes_of(everything, host, "aux" in targets, {"host": "app:host" in targets, "core": "app:core" in targets,
                                                          "ultimate": "app:ultimate" in targets}, named_dfx, named_large, dfx_all, large_all, scoped)
    # a bench runs when its inputs changed AND the lane that owns it fires (D10: host lane, a core's lane) or only its own bench source
    # changed (simscope: the bench of a core runs when its bench changed, no build needed)
    simscope = set(t[len("simscope:"):] for t in targets if t.startswith("simscope:"))
    for name in sims_now:
        if name not in deps_now:
            nodeps.append(name)
            continue
        specs = deps_now[name]
        hit = everything or name not in sims_then or sims_now[name] != sims_then.get(name) or specs != deps_then.get(name) \
            or bool(changed_code(base, head, specs + [EXC + "cores/*/logo/*", EXC + "cores/*/app/*"]))   # a core's logo and its app part are never a bench input
        if not hit:
            continue
        o = owners.get(name, "none")
        if o == "host":
            belongs = lanes["host"] or "host" in simscope
        elif o.startswith("core:"):
            belongs = o[5:] in lanes["dfx"] or o[5:] in simscope
        elif o.startswith("large:"):
            belongs = o[6:] in lanes["large"] or o[6:] in simscope
        else:
            belongs = False
        if belongs:
            sims.append(name)
        elif name not in owners:
            nodeps.append(name + " (no owner in ci/sims.lane)")
    if nodeps:
        notes.append("benches without a ci/sims.deps line are never planned: " + " ".join(sorted(nodeps)))

    # D2.6: the full benches (render benches, file benches, negative controls) run on main and its kind, and on a branch only when a fundamental file is
    # touched (ci/fundamental.txt); the rest runs at night on main
    fundamental = everything or not scoped or bool(changed_code(base, head, fundamental_specs()))
    # T174 D4: the application tests themselves (the child `apptests` runs on an app change, on this, or on a fundamental file)
    tests = everything or bool(changed_code(base, head, ["tests/hostapp", "tests/coreapp", "tests/loader", "tests/view", "tests/ctlcache.sh",
                                                           "tests/ctlcache.py", "ci/apptests.sh", "ci/render.sh"]))
    plan = {
        "version": 1, "branch": branch, "scope": "branch" if scoped else "full", "fundamental": fundamental, "tests": tests, "docs_only": docs_only, "head": head if re.match(r"^[0-9a-f]{40}$", head) else git("rev-parse", head).strip(),
        "base": {"commit": base, "source": source}, "all": everything, "changed": len(changed),
        "host": host, "aux": "aux" in targets, "dfx": named_dfx, "large": named_large,
        "apps": {"host": "app:host" in targets, "core": "app:core" in targets, "ultimate": "app:ultimate" in targets},
        "package": {"cor": cor_all, "cfw": large_all, "fresh": [t[len("package:"):] for t in fresh]},
        "lanes": lanes_of(everything, host, "aux" in targets, {"host": "app:host" in targets, "core": "app:core" in targets,
                                                               "ultimate": "app:ultimate" in targets}, named_dfx, sorted(set(named_large) | packed), dfx_all, large_all, scoped),
        "sims": sorted(sims),
        "universe": {"dfx": dfx_all, "large": large_all, "cor": cor_all, "reference": ref,
                     "own": [row.split()[0] for row in core_toml("lanes").splitlines() if row.strip()]},
        "limits": limits, "notes": notes,
    }
    if files_out:
        with open(files_out, "w", encoding="utf-8") as fh:
            for t in sorted(targets):
                fh.write("%s <- %s\n" % (t, ", ".join(reason[t][:5]) + (" ..." if len(reason[t]) > 5 else "")))
            fh.write("# changed files\n")
            fh.write("\n".join(sorted(changed)) + "\n")
    return plan


def summary(p):
    b = p["base"]
    out = ["plan for %s, diff base %s (%s)%s, %d file(s) changed" % (p["head"][:8], b["commit"][:8] if b["commit"] != "none" else "none",
                                                                  b["source"], " - NOTHING KNOWN, everything is named" if p["all"] else "", p["changed"])]
    out.append("  scope:            %s (%s), full benches: %s%s" % (p.get("scope", "full"), p.get("branch", "?"), "yes" if p.get("fundamental", True) else "no (nothing fundamental touched)",
                                                                ", DOCS ONLY: nothing runs" if p.get("docs_only") else ""))
    out.append("  host + reference: %s" % ("BUILD" if p["host"] else "from the cache"))
    out.append("  DFX cores:        %s (of %d)" % (" ".join(p["dfx"]) or "none", len(p["universe"]["dfx"])))
    out.append("  large cores:      %s (of %d)" % (" ".join(p["large"]) or "none", len(p["universe"]["large"])))
    out.append("  aux designs:      %s" % ("BUILD" if p["aux"] else "from the cache"))
    if "lanes" in p:
        L = p["lanes"]
        out.append("  lanes that run:   host %s; DFX %s; large %s" % ("yes" if L["host"] else "no", " ".join(L["dfx"]) or "none", " ".join(L["large"]) or "none"))
    out.append("  apps:             %s" % (" ".join(k for k, v in p["apps"].items() if v) or "none"))
    out.append("  package, fresh:   %s" % (" ".join(p["package"]["fresh"]) or "none"))
    out.append("  sims:             %d%s" % (len(p["sims"]), (": " + " ".join(p["sims"])) if p["sims"] else ""))
    out.append("  limits (min):     host %(host)d, core %(core)d, large %(large)d" % p["limits"])
    for n in p["notes"]:
        out.append("  note: " + n)
    return "\n".join(out)


def named(plan, target):
    if target == "host":
        return plan["host"]
    if target == "aux":
        return plan["aux"]
    if target == "fundamental":
        return plan.get("fundamental", True)
    if target.startswith("lane:"):
        L = plan.get("lanes") or {"host": plan["host"], "dfx": plan["dfx"], "large": plan["large"]}   # a plan without lanes: the named targets
        k = target[5:]
        return L["host"] if k == "host" else (k[4:] in L["dfx"] if k.startswith("dfx:") else k[6:] in L["large"])
    if target.startswith("dfx:"):
        return target[4:] in plan["dfx"]
    if target.startswith("large:"):
        return target[6:] in plan["large"]
    if target.startswith("app:"):
        return plan["apps"].get(target[4:], False)
    raise SystemExit("unknown target %s" % target)


def main():
    a = sys.argv[1:]
    if a[:1] == ["make"]:
        pretty = "--pretty" in a
        o = dict(zip([x for x in a[1:] if x != "--pretty"][0::2], [x for x in a[1:] if x != "--pretty"][1::2]))
        base = o.get("--base") or sys.exit("make needs --base")
        p = make(base, o.get("--head", "HEAD"), o.get("--source", ""), o.get("--files-out"), o.get("--branch"))
        txt = json.dumps(p, indent=1) if pretty else json.dumps(p, separators=(",", ":"))
        if o.get("--out"):
            open(o["--out"], "w").write(txt + "\n")
        else:
            print(txt)
        print(summary(p), file=sys.stderr)
    elif a[:1] == ["has"] and len(a) == 3:
        if not os.path.exists(a[1]):
            sys.exit(0)
        sys.exit(0 if named(json.load(open(a[1])), a[2]) else 1)
    elif a[:1] == ["limit"] and len(a) in (2, 3):
        lim = read_targets()[0]
        if len(a) == 3 and os.path.exists(a[2]):
            lim = json.load(open(a[2])).get("limits", lim)
        print(lim[a[1]])
    elif a[:1] == ["lanes"] and len(a) == 1:
        ref, dfx, large = universe()
        print("\n".join(["host"] + ["core:" + c for c in dfx] + ["large:" + c for c in large]))
    elif a[:1] == ["fundamental"] and len(a) == 2 and a[1] in ("benches", "apptests"):
        print("\n".join(fundamental_specs(a[1])))
    elif a[:1] == ["show"] and len(a) == 2:
        print(summary(json.load(open(a[1]))))
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
