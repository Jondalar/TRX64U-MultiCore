#!/usr/bin/env python3
"""T175 co-check: the result file the application wrote (diag_co replay) against what the model of the peripherals must have produced.
  check.py <result.txt> <test.expect>
The expect file: `# comment`, `path = <json>` (equal) or `path ~ <text>` (the string starts with text); path = dotted keys of the result TOML.
Exit 0 when every line holds; each failing line is printed."""
import json
import sys
import tomllib


def main():
    res, exp = sys.argv[1], sys.argv[2]
    text = open(res).read()
    if "co-check" in text:
        text = text[:text.rindex("co-check")]
    try:
        d = tomllib.loads(text)
    except Exception as e:                              # a result the application wrote that is no TOML is a defect of its own
        print("FAIL check: %s is no TOML: %s" % (res, e))
        return 1
    bad = 0
    n = 0
    for raw in open(exp):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        n += 1
        if " ~ " in line:
            path, want = line.split(" ~ ", 1)
            op = "~"
        else:
            path, want = line.split(" = ", 1)
            op = "="
        v = d
        for k in path.strip().split("."):
            v = v.get(k) if isinstance(v, dict) else None
        ok = (isinstance(v, str) and v.startswith(want.strip())) if op == "~" else v == json.loads(want)
        if not ok:
            bad += 1
            print("FAIL check %s: %s is %r, wanted %s %s" % (exp.split("/")[-1], path.strip(), v, op, want.strip()))
    print("check %s: %d lines, %d wrong" % (exp.split("/")[-1], n, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
