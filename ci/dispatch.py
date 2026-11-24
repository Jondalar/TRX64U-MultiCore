#!/usr/bin/env python3
"""T239.5 D9: the dispatcher of the matrix - what this push has to do. It runs after ci/plan.sh (plan.json: what the push changed) in the first job of
.github/workflows/matrix.yml and decides the lanes and their steps (D8): the core lanes are the cores that moved to the SDK (a TOML B of their own,
tools/core_toml.py lanes: a partial core, or a large core with its flat bitstream, T213.3 P5); every lane starts, and a lane whose inputs did not change is served from its content-keyed caches (the images stage
key, the app test units, the bitstream releases); the sims of the bitstream lanes run on demand only (ci/select.sh sims-tier > 0: "[sims]" or the
dispatch input), for the owners host, core:<id> and large:<id>.

  ci/dispatch.py <plan.json> --tier N
      prints `key=value` lines for $GITHUB_OUTPUT: cores, lint_groups, cor_targets (core:<id> or large:<id>), sim_owners (JSON lists), sims_tier; and appends the matrix of this
      run (lanes from top to bottom, the steps from left to right) to $GITHUB_STEP_SUMMARY when that is set
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


def matrix(lanes, owners):
    """The rows of this run: (lane, {step number: what runs there})."""
    sims = lambda o: "sims" if o in owners else "sims on demand"
    rows = [("Every push", {1: "tree checks", 3: "dispatcher"}),
            ("SDK", {1: "lint", 2: "unit tests", 6: "build", 7: "test", 11: "package", 13: "deploy"}),
            ("Bitstream host", {2: sims("host"), 3: "key", 6: "Vivado (builder)", 7: "report", 11: "keep", 13: "to the lanes"})]
    for c, k in lanes:
        rows.append(("Bitstream " + c, {2: sims(target(c, k)), 3: "key", 6: "Vivado flat (builder)" if k == "large" else "Vivado (builder)", 7: "report",
                                        11: "keep", 13: "to the lanes"}))
    rows.append(("Host menu", {1: "lint", 2: "unit tests", 6: "build", 12: "emulator", 13: "deliver"}))
    for c, k in lanes:
        rows.append((c + (" (large)" if k == "large" else ""), {i + 1: s.lower() for i, s in enumerate(STEPS)}))
    return rows


def main(argv):
    if len(argv) != 3 or argv[1] != "--tier":
        print(__doc__, file=sys.stderr)
        return 2
    plan_path, tier = argv[0], int(argv[2])
    plan = json.load(open(plan_path))
    if plan.get("version") != 1:
        print("dispatch: %s is no plan.json of version 1" % plan_path, file=sys.stderr)
        return 1
    lanes = moved_cores()
    cores = [c for c, _ in lanes]
    targets = [target(c, k) for c, k in lanes]
    owners = sim_owners(plan_path, tier, targets)
    out = {"cores": cores, "lint_groups": ["tree", "sdk", "host"] + ["core:" + c for c in cores],
           "cor_targets": targets, "sim_owners": owners}
    for k, v in out.items():
        print("%s=%s" % (k, json.dumps(v, separators=(",", ":"))))
    print("sims_tier=%d" % tier)
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        lines = ["### The matrix of this push (T239.5)", "",
                 "Changed: %s code files, branch %s, base %s" % (plan.get("changed"), plan.get("branch"), (plan.get("base") or {}).get("commit") or "none"), "",
                 "| lane | " + " | ".join("%d %s" % (i + 1, s) for i, s in enumerate(STEPS)) + " |", "|---" * (len(STEPS) + 1) + "|"]
        for lane, cells in matrix(lanes, owners):
            lines.append("| %s | " % lane + " | ".join(cells.get(i + 1, "") for i in range(len(STEPS))) + " |")
        with open(summary, "a") as fh:
            fh.write("\n".join(lines) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
