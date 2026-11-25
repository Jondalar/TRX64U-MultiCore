#!/usr/bin/env python3
"""T239.5 D9: the dispatcher of the matrix - what this push has to do. It runs after ci/plan.sh (plan.json: what the push changed) in the first job of
.github/workflows/matrix.yml and decides the lanes and their steps (D8): the core lanes are the cores that moved to the SDK (a TOML B of their own,
tools/core_toml.py lanes: a partial core, or a large core with its flat bitstream, T213.3 P5). A lane that runs and whose inputs did not change is served
from its content-keyed caches (the images stage key, the app test units, the bitstream releases); the sims of the bitstream lanes run on demand only
(ci/select.sh sims-tier > 0: "[sims]" or the dispatch input), for the owners host, core:<id> and large:<id>.

Alone (the T239 branch, D3): every lane runs, with its lint legs and sims, and the whole package is made (D14), so the work order names every entry.
Beside the old pipeline (--beside: every other push of a branch or v* tag, D11, D13): the run's plan, work order, approval, lint and sims are the old
pipeline's jobs (its job lint runs every group of ci/lint.sh), so this run has no lint legs and no sims of its own. Where the old pipeline makes a package
(scope full: main, integration, integ-*, release/*, v* tags; or --update yes) the matrix makes the whole package instead, so every lane runs; on any other
branch only the lanes the push touched start (the D2 addendum: an app change drags no core in): a core lane when the plan names the core (its sources, its
app included), every core lane when the application tests changed or the plan has no base, the SDK lane when the shared app changed or a core lane runs,
the host menu lane when the host app changed or a core lane runs (its emulator tests take the host app). Nothing touched: no lane, no matrix work.

  ci/dispatch.py <plan.json> --tier N [--beside] [--update yes|no]
      prints `key=value` lines for $GITHUB_OUTPUT: cores, lint_groups, cor_targets (core:<id> or large:<id>), sim_owners (JSON lists), sims_tier, and sdk, host,
      package (true or false); and appends the matrix of this run (lanes from top to bottom, the steps from left to right) to $GITHUB_STEP_SUMMARY when set
"""
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
STEPS = ["Lint", "Unit tests", "What has changed?", "Comparison", "What must be in it", "Build", "Complete and fits", "Tests: mock bitstream -> service",
         "Tests: service -> UI", "Tests: service -> API", "Package", "Emulator tests", "Deliver"]


def moved_cores():
    """(name, kind) of the cores that moved to the SDK: kind dfx (a partial on the host bitstream) or large (its own flat bitstream)."""
    out = subprocess.run([sys.executable, "-I", os.path.join(ROOT, "tools", "core_toml.py"), "lanes"], capture_output=True, text=True, check=True).stdout
    lanes = [tuple(row.split()) for row in out.splitlines() if row.strip()]
    bad = [e for e in lanes if len(e) != 2 or e[1] not in ("dfx", "large")]
    if bad:
        raise SystemExit("dispatch: tools/core_toml.py lanes printed %r" % (bad[:2],))
    return lanes


def target(name, kind):
    """The bitstream target of a core lane (ci/fetch_bits.sh, ci/order.py): core:<id> for a partial, large:<id> for a flat bitstream."""
    return ("large:" if kind == "large" else "core:") + name


def sim_owners(plan_path, tier, targets):
    if tier <= 0:
        return []
    out = subprocess.run([sys.executable, os.path.join(HERE, "orchestrate.py"), "outputs", plan_path, "--tier", str(tier), "--update", "false"],
                         capture_output=True, text=True, check=True).stdout
    owners = []
    for line in out.splitlines():
        if line.startswith("sim_owners="):
            owners = json.loads(line.split("=", 1)[1])
    keep = {"host"} | set(targets)
    return [o for o in owners if o in keep]


def matrix(lanes, owners, beside, sdk, host, package):
    """The rows of this run: (lane, {step number: what runs there})."""
    sims = lambda o: "sims" if o in owners else ("sims: the old job sim" if beside else "sims on demand")
    lint = "the old job lint" if beside else "lint"
    off = {6: "not touched"}
    rows = [("Every push", {1: "the old job lint" if beside else "tree checks", 3: "dispatcher"}),
            ("SDK", {1: lint, 2: "unit tests", 6: "build", 7: "test", 11: "package", 13: "deploy"} if sdk else off)]
    if lanes:
        rows.append(("Bitstream host", {2: sims("host"), 3: "key", 6: "Vivado (builder)", 7: "report", 11: "keep", 13: "to the lanes"}))
    for c, k in lanes:
        rows.append(("Bitstream " + c, {2: sims(target(c, k)), 3: "key", 6: "Vivado flat (builder)" if k == "large" else "Vivado (builder)", 7: "report",
                                        11: "keep", 13: "to the lanes"}))
    rows.append(("Host menu", {1: lint, 2: "unit tests", 6: "build", 12: "emulator", 13: "deliver"} if host else off))
    for c, k in lanes:
        cells = {i + 1: s.lower() for i, s in enumerate(STEPS)}
        cells[1] = lint
        rows.append((c + (" (large)" if k == "large" else ""), cells))
    if package:
        rows.append(("Whole package (D14)", {11: "collect (every lane's bitstreams), package (every .cor)", 13: "deliver (the artifact package)"}))
    return rows


def touched(plan, lanes):
    """Beside the old pipeline on a feature branch: the core lanes the push touched, and whether the SDK and the host menu lane run."""
    L = plan.get("lanes") or {}
    named = set(plan.get("dfx", [])) | set(plan.get("large", [])) | set(L.get("dfx", [])) | set(L.get("large", []))
    every = bool(plan.get("all") or plan.get("tests"))
    run = [(c, k) for c, k in lanes if every or c in named]
    apps = plan.get("apps") or {}
    return run, bool(run or apps.get("core")), bool(run or apps.get("host"))


def main(argv):
    o = {"--tier": None, "--update": "no"}
    beside, rest = False, list(argv[1:])
    while rest:
        k = rest.pop(0)
        if k == "--beside":
            beside = True
        elif k in o and rest:
            o[k] = rest.pop(0)
        else:
            o["--tier"] = None
            break
    if not argv or o["--tier"] is None or o["--update"] not in ("yes", "no"):
        print(__doc__, file=sys.stderr)
        return 2
    plan_path, tier, update = argv[0], int(o["--tier"]), o["--update"] == "yes"
    plan = json.load(open(plan_path))
    if plan.get("version") != 1:
        print("dispatch: %s is no plan.json of version 1" % plan_path, file=sys.stderr)
        return 1
    lanes = moved_cores()
    package = not beside or plan.get("scope", "full") == "full" or update
    if package:
        run, sdk, host = lanes, True, True
    else:
        run, sdk, host = touched(plan, lanes)
    cores = [c for c, _ in run]
    targets = [target(c, k) for c, k in run]
    owners = [] if beside else sim_owners(plan_path, tier, targets)
    out = {"cores": cores, "lint_groups": [] if beside else ["tree", "sdk", "host"] + ["core:" + c for c in cores],
           "cor_targets": targets, "sim_owners": owners}
    for k, v in out.items():
        print("%s=%s" % (k, json.dumps(v, separators=(",", ":"))))
    print("sims_tier=%d" % tier)
    for k, v in (("sdk", sdk), ("host", host), ("package", package)):
        print("%s=%s" % (k, "true" if v else "false"))
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        how = ("Beside the old pipeline (D11): the plan, the work order, the approval, the lint and the sims are its jobs; " +
               ("the package of record is this run's whole package" if package else "no package on this push (scope %s); the lanes this push touched: %s"
                % (plan.get("scope"), " ".join(cores) or "none")) if beside else "Alone (D3): this run's dispatcher uploads the plan and the work order")
        lines = ["### The matrix of this push (T239.5)", "",
                 "Changed: %s code files, branch %s, base %s" % (plan.get("changed"), plan.get("branch"), (plan.get("base") or {}).get("commit") or "none"), "",
                 how, "",
                 "| lane | " + " | ".join("%d %s" % (i + 1, s) for i, s in enumerate(STEPS)) + " |", "|---" * (len(STEPS) + 1) + "|"]
        for lane, cells in matrix(run, owners, beside, sdk, host, package):
            lines.append("| %s | " % lane + " | ".join(cells.get(i + 1, "") for i in range(len(STEPS))) + " |")
        with open(summary, "a") as fh:
            fh.write("\n".join(lines) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
