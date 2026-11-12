#!/usr/bin/env python3
"""The layer-2 contract: the port list of the reconfigurable partition (T94 D1).

What the host offers a core is fixed: the ports of the layer-2 cell in host/top.v.
This reads them out of a layer2_<name>.v and prints one "<dir> <width> <name>" line per
port, sorted, so the list of two cores can be compared byte for byte.

  tools/contract_ports.py <file.v> [more.v ...]      print the list of each file
  tools/contract_ports.py --check                    every core against contract/layer2_ports.txt
"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONTRACT = os.path.join(ROOT, "contract", "layer2_ports.txt")


def ports(path):
    """The module header's ports as a list of "<dir> <width> <name>" lines."""
    src = open(path, encoding="utf-8").read()
    src = re.sub(r"//[^\n]*", "", src)
    src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    m = re.search(r"\bmodule\s+\w+\s*(#\s*\((?:[^()]|\([^()]*\))*\)\s*)?\((.*?)\)\s*;", src, re.S)
    if not m:
        raise SystemExit("%s: no module header found" % path)
    out = []
    for decl in m.group(2).split(","):
        d = re.match(r"\s*(input|output|inout)\s+(?:wire\s+|reg\s+)?(signed\s+|unsigned\s+)?(\[[^\]]+\]\s+)?(\w+)\s*$", decl)
        if not d:
            if decl.strip():
                raise SystemExit("%s: cannot read the port declaration %r" % (path, decl.strip()))
            continue
        width = re.sub(r"\s+", "", d.group(3) or "[0:0]")
        sign = (d.group(2) or "").strip()
        out.append("%-6s %-8s %-10s %s" % (d.group(1), sign, width, d.group(4)))
    return sorted(out)


def core_files():
    # A core's partition top is cores/<name>/layer2_<name>.v, unless its descriptor names another
    # one ("top", e.g. amiga1200, which is the Amiga core's gateware with other generics). Reading
    # the descriptor here keeps such a core in the check instead of skipping it for want of a file.
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import core_toml
    cores = os.path.join(ROOT, "cores")
    for name in sorted(os.listdir(cores)):
        f = os.path.join(cores, name, "layer2_%s.v" % name)
        d = os.path.join(cores, name, "core.toml")
        if os.path.exists(d):
            top = core_toml.parse(d).get("top")
            if top:
                f = os.path.join(ROOT, top)
        if os.path.exists(f):
            yield name, f


def main():
    if sys.argv[1:2] == ["--check"]:
        want = open(CONTRACT, encoding="utf-8").read().split("\n")
        want = [l for l in want if l and not l.startswith("#")]
        bad = 0
        for name, f in core_files():
            have = ports(f)
            if have == want:
                print("contract: %s matches (%d ports)" % (name, len(have)))
                continue
            bad += 1
            miss = [l for l in want if l not in have]
            extra = [l for l in have if l not in want]
            print("contract: %s DIFFERS from contract/layer2_ports.txt" % name)
            for l in miss:
                print("    missing: %s" % l)
            for l in extra:
                print("    extra:   %s" % l)
        sys.exit(1 if bad else 0)
    if not sys.argv[1:]:
        raise SystemExit(__doc__)
    for f in sys.argv[1:]:
        print("\n".join(ports(f)))


main()
