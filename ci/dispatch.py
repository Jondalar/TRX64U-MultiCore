#!/usr/bin/env python3
"""T239.5 D9: the dispatcher of the matrix - what this push has to do. It runs after ci/plan.sh (plan.json: what the push changed) in the first job of
.github/workflows/matrix.yml and decides the lanes and their steps (D8): the core lanes are the cores that moved to the SDK (a TOML B of their own,
tools/core_toml.py list-own-app); every lane starts, and a lane whose inputs did not change is served from its content-keyed caches (the images stage
key, the app test units, the bitstream releases); the sims of the bitstream lanes run on demand only (ci/select.sh sims-tier > 0: "[sims]" or the
dispatch input), for the owners host and core:<id>.

  ci/dispatch.py <plan.json> --tier N
      prints `key=value` lines for $GITHUB_OUTPUT: cores, lint_groups, cor_targets, sim_owners (JSON lists), sims_tier; and appends the matrix of this
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
    out = subprocess.run([sys.executable, "-I", os.path.join(ROOT, "tools", "core_toml.py"), "list-own-app"], capture_output=True, text=True, check=True).stdout
    return [c for c in out.split() if c]


def sim_owners(plan_path, tier, cores):
    if tier <= 0:
        return []
    out = subprocess.run([sys.executable, os.path.join(HERE, "orchestrate.py"), "outputs", plan_path, "--tier", str(tier), "--update", "false"],
                         capture_output=True, text=True, check=True).stdout
    owners = []
    for line in out.splitlines():
        if line.startswith("sim_owners="):
            owners = json.loads(line.split("=", 1)[1])
    keep = {"host"} | {"core:" + c for c in cores}
    return [o for o in owners if o in keep]


def matrix(cores, owners):
    """The rows of this run: (lane, {step number: what runs there})."""
    sims = lambda o: "sims" if o in owners else "sims on demand"
    rows = [("Every push", {1: "tree checks", 3: "dispatcher"}),
            ("SDK", {1: "lint", 2: "unit tests", 6: "build", 7: "test", 11: "package", 13: "deploy"}),
            ("Bitstream host", {2: sims("host"), 3: "key", 6: "Vivado (builder)", 7: "report", 11: "keep", 13: "to the lanes"})]
    for c in cores:
        rows.append(("Bitstream " + c, {2: sims("core:" + c), 3: "key", 6: "Vivado (builder)", 7: "report", 11: "keep", 13: "to the lanes"}))
    rows.append(("Host menu", {1: "lint", 2: "unit tests", 6: "build", 12: "emulator", 13: "deliver"}))
    for c in cores:
        rows.append((c, {i + 1: s.lower() for i, s in enumerate(STEPS)}))
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
    cores = moved_cores()
    owners = sim_owners(plan_path, tier, cores)
    out = {"cores": cores, "lint_groups": ["tree", "sdk", "host"] + ["core:" + c for c in cores],
           "cor_targets": ["core:" + c for c in cores], "sim_owners": owners}
    for k, v in out.items():
        print("%s=%s" % (k, json.dumps(v, separators=(",", ":"))))
    print("sims_tier=%d" % tier)
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        lines = ["### The matrix of this push (T239.5)", "",
                 "Changed: %s code files, branch %s, base %s" % (plan.get("changed"), plan.get("branch"), (plan.get("base") or {}).get("commit") or "none"), "",
                 "| lane | " + " | ".join("%d %s" % (i + 1, s) for i, s in enumerate(STEPS)) + " |", "|---" * (len(STEPS) + 1) + "|"]
        for lane, cells in matrix(cores, owners):
            lines.append("| %s | " % lane + " | ".join(cells.get(i + 1, "") for i in range(len(STEPS))) + " |")
        with open(summary, "a") as fh:
            fh.write("\n".join(lines) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
