#!/usr/bin/env python3
"""The operations and states the real application image exports, as the golden list tests/coreapp/golden/ops_amiga1200.txt holds them (the ABI between a
menu TOML and the C++: tools/descrec.py checks every name and binding against that list).

  export.py              print the list from a ue2emu run of the image (GET /v1/ops, GET /v1/state)
  export.py --update     write tests/coreapp/golden/ops_amiga1200.txt
  export.py --core NAME [--update]    the same for another core of emu.CORES (the golden is written for the Amiga 1200 only)

Line format (the same as tests/coreapp/ops_export.cc): `op<TAB>name<TAB>arg:type:lo:hi:maxlen:label|label ...` and `state<TAB>name`.
"""
import json
import os
import sys
import urllib.request

import emu

GOLDEN_A1200 = os.path.join(emu.ROOT, "tests", "coreapp", "golden", "ops_amiga1200.txt")


def _get(e, path):
    with urllib.request.urlopen("http://127.0.0.1:%d%s%s" % (emu.REST_PORT, path, ("&t=" + e.token) if e.token else ""), timeout=20) as r:
        return json.loads(r.read().decode("utf-8"))


def _all(e, path, key):
    out, frm = [], 0
    while frm is not None:
        d = _get(e, "%s?from=%d" % (path, frm))
        out += d[key]
        frm = d["next"]
    return out


def arg_text(a):
    t = a["type"]
    lo = hi = ml = 0
    labels = ""
    if t == "int":
        lo, hi = a["min"], a["max"]
    elif t == "bool":
        hi = 1
    elif t == "enum":
        hi = len(a["labels"]) - 1
        labels = "|".join(a["labels"])
    elif t in ("string", "path", "slot"):
        ml = a["maxlen"]
    return "%s:%s:%d:%d:%d:%s" % (a["name"], t, lo, hi, ml, labels)


def export_text(e):
    """The list of the image the emulator `e` runs (it has to be up)."""
    e.state()                                              # the REST server answers
    lines = ["\t".join(["op", o["name"]] + [arg_text(a) for a in o["args"]]) for o in _all(e, "/v1/ops", "ops")]
    lines += ["state\t%s" % s["name"] for s in _all(e, "/v1/state", "state")]
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    core = sys.argv[sys.argv.index("--core") + 1] if "--core" in sys.argv else "amiga1200"
    e = emu.Emu(core=core)
    e.start()
    try:
        text = export_text(e)
    finally:
        e.stop()
    if "--update" in sys.argv:
        if core != "amiga1200":
            raise SystemExit("export.py: --update writes the golden of the Amiga 1200 only")
        g = GOLDEN_A1200
        open(g, "w").write(text)
        print("wrote %s (%d lines)" % (g, text.count("\n")))
    else:
        sys.stdout.write(text)
