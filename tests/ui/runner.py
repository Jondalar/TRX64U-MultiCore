#!/usr/bin/env python3
"""A tiny Gherkin runner (Python 3 standard library only).

Subset: `Feature:`, `Background:`, `Scenario:`, `Given/When/Then/And/But <text>`, `#` comments. A step is matched against the regular expressions of
steps.py; the keyword does not matter for the match. One emulator session per feature file (unless the feature has `# no-emulator` on its first line).

  runner.py [--keep-going] [--only TEXT] feature...
"""
import os
import re
import sys
import time
import traceback

import emu
import steps
from steps import AssertionFailed

KEYWORDS = ("Given", "When", "Then", "And", "But")


class Feature:
    def __init__(self, path):
        self.path = path
        self.name = os.path.basename(path)
        self.background = []
        self.scenarios = []                  # (name, [(keyword, text, lineno)])
        self.needs_emulator = True
        self.core = "amiga1200"              # the Amiga 1200 profile of the emulator by default; `# core: <profile>` on a feature's first line picks another (T192.3 section 5)


def parse(path):
    f = Feature(path)
    cur = None
    target = None
    for no, raw in enumerate(open(path, encoding="utf-8"), 1):
        line = raw.strip()
        if no == 1 and line == "# no-emulator":
            f.needs_emulator = False
        if no == 1 and line.startswith("# core:"):
            f.core = line[7:].strip()
        if not line or line.startswith("#"):
            continue
        if line.startswith("Feature:"):
            f.name = line[8:].strip()
        elif line.startswith("Background:"):
            target = f.background
        elif line.startswith("Scenario:"):
            cur = (line[9:].strip(), [])
            f.scenarios.append(cur)
            target = cur[1]
        else:
            w = line.split(None, 1)
            if w[0] not in KEYWORDS or len(w) < 2 or target is None:
                raise SystemExit("%s:%d: not a step: %s" % (path, no, line))
            target.append((w[0], w[1].strip(), no))
    return f


def find_step(text):
    for rx, fn in steps.STEPS:
        m = rx.fullmatch(text)
        if m:
            return fn, m.groups()
    return None, None


def run_steps(ctx, lst, indent, out):
    for kw, text, no in lst:
        fn, groups = find_step(text)
        t0 = time.time()
        if fn is None:
            out.append("%s%-5s %s\n%s      no step matches this line" % (indent, "ERR", "%s %s" % (kw, text), indent))
            raise steps.StepError("no step matches: %s" % text)
        try:
            fn(ctx, *groups)
        except AssertionFailed as e:
            shot = ctx.attach_failure()
            out.append("%sFAIL  %s %s\n%s      %s%s" % (indent, kw, text, indent, e, "   [screenshot %s]" % shot if shot else ""))
            raise
        except Exception as e:
            out.append("%sERR   %s %s\n%s      %s: %s" % (indent, kw, text, indent, type(e).__name__, e))
            raise
        out.append("%sok    %s %s  (%.1fs)" % (indent, kw, text, time.time() - t0))


def run_feature(f, keep_going, only):
    ctx = steps.Ctx(f.needs_emulator, f.core)
    res = {"pass": 0, "fail": 0, "error": 0}
    print("Feature: %s   (%s, %s)" % (f.name, os.path.basename(f.path), f.core))
    try:
        ctx.start()
        for name, lst in f.scenarios:
            if only and only not in name:
                continue
            out = ["  Scenario: %s" % name]
            ctx.scenario = "%s__%s" % (os.path.splitext(os.path.basename(f.path))[0], re.sub(r"\W+", "_", name)[:50])
            kind = "pass"
            try:
                run_steps(ctx, f.background, "    ", out)
                run_steps(ctx, lst, "    ", out)
            except AssertionFailed as e:
                kind = "fail"
            except Exception:
                kind = "error"
                out.append("    " + traceback.format_exc().strip().replace("\n", "\n    "))
            res[kind] += 1
            print("\n".join(out))
            if kind != "pass" and not keep_going:
                break
    finally:
        ctx.stop()
    return res


def main(argv):
    keep = "--keep-going" in argv
    only = None
    if "--only" in argv:
        only = argv[argv.index("--only") + 1]
        del argv[argv.index("--only"):argv.index("--only") + 2]
    files = [a for a in argv if not a.startswith("--")]
    if not files:
        raise SystemExit(__doc__)
    t0 = time.time()
    total = {"pass": 0, "fail": 0, "error": 0}
    skipped = []
    for p in files:
        f = parse(p)
        core = f.core.partition("+")[0]
        if f.needs_emulator and not os.environ.get("COREAPP_" + core) and emu.own_app(core):
            # T239.5 D13: the core has left the shared image; its features need its own image (tools/dev.sh builds only the shared one)
            print("Feature: %s   (%s, %s)\n  SKIPPED out loud: the core has an app of its own - give its image as COREAPP_%s" % (f.name, os.path.basename(f.path), f.core, core))
            skipped.append(os.path.basename(f.path))
            continue
        r = run_feature(f, keep, only)
        for k in total:
            total[k] += r[k]
    secs = time.time() - t0
    print("\n%d passed, %d failed, %d errors (%.0f s)" % (total["pass"], total["fail"], total["error"], secs))
    if skipped:
        print("SKIPPED out loud: %d feature(s) of cores with an app of their own, no image given: %s" % (len(skipped), " ".join(skipped)))
    return 1 if total["fail"] or total["error"] else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
