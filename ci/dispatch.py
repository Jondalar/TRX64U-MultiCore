#!/usr/bin/env python3
"""T239.5 D9: the dispatcher of the matrix - what this push has to do. It runs after ci/plan.sh (plan.json: what the push changed) in the first job of
.github/workflows/matrix.yml and decides the lanes and their steps (D8): the core lanes are the cores that moved to the SDK (a TOML B of their own,
tools/core_toml.py lanes: a partial core, or a large core with its flat bitstream, T213.3 P5). A lane that runs and whose inputs did not change is served
from its content-keyed caches (the images stage key, the app test units, the bitstream releases); the sims run on demand only (ci/select.sh sims-tier > 0:
integration, integ-*, a tag, "[sims]" or the dispatch input), one leg per bench owner the plan names (host, core:<id>, large:<id>).

T239.5 D22: this is the only pipeline. The matrix owns the work order and the vivado click (T265 P5); the host gateware is its own job (host-bits: key, fetch,
report) whenever the whole package is made or the plan changes host/ or the aux designs.

The whole package (D14) is made where the plan's scope is full (main, integration, integ-*, release/*, v* tags) or the update files are asked for (--update yes):
every lane runs. On any other branch only the lanes the push touched start (the D2 addendum: an app change drags no core in): the core lanes of ci/orchestrate.py
matrix_lanes (a core the plan names, its sources or its app; every core lane when the application tests changed or the plan has no base - the same rule names their
bitstreams in the work order), the SDK lane when the shared app changed, a core lane runs or a .cor leg needs the shared image, the host menu lane when the host app changed or the lane of the core whose .cor it
runs (ci/orchestrate.py HOST_EMU_CORE) runs. The cores that have no lane of their own get their .cor from a cor leg on such a branch (cor_legs, ci/orchestrate.py order_cors).
Nothing touched: no lane, no matrix work. A documentation-only plan (D2.1): no lane, no lint leg, no package.

  ci/dispatch.py <plan.json> --tier N [--update yes|no]
      prints `key=value` lines for $GITHUB_OUTPUT: cores, lint_groups, lane_targets (host, core:<id> or large:<id>: the bitstream targets of this run's lanes), cor_legs (the
      targets of the cor jobs), order_targets (the work order's targets: ci/orchestrate.py order_cors), sim_owners (JSON lists), sims_tier, host_emu_core (the core the host
      menu lane's emulator test runs), and sdk, host, package, host_bits, update, apptests (true or false; host_bits: the matrix job host-bits builds and keeps the host
      gateware, T265 P5; update: the update files are built; apptests: the job apptests runs); and appends tables 1 and 2 of the run's summary (T239.5 D23: what changed,
      where this run goes; env CI_TESTS false = a run without tests) to $GITHUB_STEP_SUMMARY when set
"""
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import orchestrate as O  # noqa: E402

def moved_cores():
    """(name, kind) of the cores that moved to the SDK: kind dfx (a partial on the host bitstream) or large (its own flat bitstream)."""
    out = subprocess.run([sys.executable, "-I", os.path.join(ROOT, "tools", "core_toml.py"), "lanes"], capture_output=True, text=True, check=True).stdout
    lanes = [tuple(row.split()) for row in out.splitlines() if row.strip()]
    bad = [e for e in lanes if len(e) != 2 or e[1] not in ("dfx", "large")]
    if bad:
        raise SystemExit("dispatch: tools/core_toml.py lanes printed %r" % (bad[:2],))
    return lanes


target = O.lane_target     # the bitstream target of a core lane: core:<id> or large:<id>


def sim_owners(plan, tier):
    """every bench owner the plan names a bench of in this tier (ci/orchestrate.py sim_benches); none without a tier"""
    return sorted(O.sim_benches(plan, tier)) if tier > 0 else []


ON, OFF = "\U0001F7E2", "\u26AA"      # T239.5 D23: green = changed / runs, white = unchanged / not in this run


def overview(plan, lanes, run, owners, sdk, host, package, host_bits, cor_legs, docs, tests):
    """T239.5 D23: tables 1 (what changed) and 2 (where this run goes) of the run's summary; the summary job adds table 3 (the result)."""
    mark = lambda b: ON if b else OFF
    apps = plan.get("apps") or {}
    named = set(plan.get("dfx", [])) | set(plan.get("large", []))
    started = {c for c, _ in run}
    out = ["## This run (T239.5 D23)", "",
           "Branch %s, base %s, %s changed code files%s." % (plan.get("branch"), ((plan.get("base") or {}).get("commit") or "none")[:8],
                                                            plan.get("changed"), ", documentation only" if docs else ""), "",
           "### 1 What changed", "", "| | part | |", "|---|---|---|",
           "| %s | host gateware | |" % mark(plan.get("host")),
           "| %s | aux designs (ddr2_test, hdmi_test) | |" % mark(plan.get("aux")),
           "| %s | host menu app | |" % mark(apps.get("host")),
           "| %s | core app (every core's .cor carries it) | |" % mark(apps.get("core")),
           "| %s | application tests | every core lane runs when they change |" % mark(plan.get("tests"))]
    for c, k in lanes:
        why = "its own sources" if c in named else ""
        if not why and c in started:
            if plan.get("all"):
                why = "no base: everything"
            elif plan.get("tests"):
                why = "the application tests changed"
            elif apps.get("core"):
                why = "the core app changed"
            elif k == "dfx" and plan.get("host"):
                why = "the host changed (its partial is placed on it)"
            elif apps.get("host") and c == O.HOST_EMU_CORE:
                why = "the host menu's emulator test runs with its .cor"
            elif c not in named and not any(c in (plan.get("lanes") or {}).get(x, []) for x in ("dfx", "large")):
                why = "its .cor is not in the base's package (D24)"
        out.append("| %s | %s (%s) | %s |" % (mark(c in named or c in started), c, k, why))
    out += ["", "### 2 Where this run goes", "",
            "| lane | lint | tests | build | bitstream | emulator | package |", "|---|---|---|---|---|---|---|"]
    t = lambda b: mark(b and tests)
    out.append("| tree checks, key tests | %s | %s | | | | |" % (t(not docs), t(not docs)))
    out.append("| SDK | %s | %s | %s | | %s | %s |" % (t(sdk), t(sdk), mark(sdk), t(sdk), mark(sdk)))
    out.append("| host menu | %s | %s | %s | | %s | %s |" % (t(host), t(host), mark(host), t(host), mark(host)))
    out.append("| host gateware | | %s | | %s | | |" % (t(host_bits and "host" in owners), mark(host_bits)))
    for c, k in lanes:
        r = c in started
        note = "" if r else (" from the base's package (D24)" if package else "")
        out.append("| %s | %s | %s | %s | %s | %s | %s%s |" % (c, t(r), t(r), mark(r), mark(r), t(r), mark(r), note))
    if cor_legs:
        out.append("| .cor without a lane: %s | | | %s | | | %s |" % (" ".join(cor_legs), ON, ON))
    out.append("| whole package | | | | | | %s |" % mark(package))
    sims = [o for o in owners if o != "host"]
    if sims:
        out.append("| benches of %s | | %s | | | | |" % (" ".join(sims), t(True)))
    if not tests:
        out += ["", "No tests on this run (a push to main or a v* tag, T239.1 D51)."]
    return "\n".join(out)


def touched(plan, lanes):
    """On a feature branch: the core lanes the push touched (ci/orchestrate.py matrix_lanes, the rule the work order uses too), and
    whether the SDK and the host menu lane run (the host app changed, or the lane of the core that lane runs with starts)."""
    run = O.matrix_lanes(plan, lanes)
    apps = plan.get("apps") or {}
    return run, bool(run or apps.get("core")), bool(apps.get("host") or O.HOST_EMU_CORE in [c for c, _ in run])


def main(argv):
    o = {"--tier": None, "--update": "no"}
    rest = list(argv[1:])
    while rest:
        k = rest.pop(0)
        if k in o and rest:
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
    docs = bool(plan.get("docs_only"))
    package = not docs and (plan.get("scope", "full") == "full" or update)
    # the cor jobs: the .cor of a core without a lane of its own, on a branch that makes no package (the package packs every core)
    cor_legs = [] if docs or package else O.order_cors(plan, matrix=False)
    if docs:
        run, sdk, host = [], False, False
    elif package:
        run, sdk, host = lanes, True, True
        # T239.5 D24: a lane the plan does not start runs only when the base's package has no .cor of it (ci/base_cor.py filled CI_BASE_COR)
        base = os.environ.get("CI_BASE_COR", "")
        if base and os.path.isdir(base):
            started = {c for c, _ in touched(plan, lanes)[0]}
            run = [(c, k) for c, k in lanes if c in started or not os.path.isfile(os.path.join(base, c + ".cor"))]
    else:
        run, sdk, host = touched(plan, lanes)
        sdk = sdk or bool(cor_legs)           # the cor jobs take the shared image of the SDK lane
    cores = [c for c, _ in run]
    targets = [target(c, k) for c, k in run]
    # T265 P5: the host gateware (job host-bits): whenever the whole package is made (it carries the host) or the plan changes the host or aux designs; the target
    # `host` names it in the work order (ci/order.py), and in the order of this run
    host_bits = not docs and (package or bool(plan.get("host") or plan.get("aux")))
    if host_bits:
        targets = ["host"] + targets
    owners = [] if docs else sim_owners(plan, tier)
    from_base = [c for c, _ in lanes if package and c not in cores]
    out = {"from_base": from_base, "cores": cores, "lint_groups": [] if docs else ["tree", "sdk", "host"] + ["core:" + c for c in cores] + (["rtlchk"] if plan.get("rtlchk") else []),
           "lane_targets": targets, "cor_legs": cor_legs, "order_targets": [] if docs else O.order_cors(plan, True), "sim_owners": owners}
    for k, v in out.items():
        print("%s=%s" % (k, json.dumps(v, separators=(",", ":"))))
    print("sims_tier=%d" % tier)
    print("host_emu_core=%s" % O.HOST_EMU_CORE)
    for k, v in (("sdk", sdk), ("host", host), ("package", package), ("host_bits", host_bits), ("update", not docs and update),
                 ("apptests", not docs and O.wants_apptests(plan))):
        print("%s=%s" % (k, "true" if v else "false"))
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a") as fh:
            fh.write(overview(plan, lanes, run, owners, sdk, host, package, host_bits, cor_legs, docs,
                              os.environ.get("CI_TESTS", "true") != "false") + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
