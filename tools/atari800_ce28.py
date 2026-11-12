#!/usr/bin/env python3
"""T88 D3: give every clocked process of Mark Watson's Atari core the core's global clock enable.

The core runs on sys_clk with a clock enable (28.37516 MHz = sys_clk x 9/10, spec D3); a clock buffer inside the
partition is not allowed, so there is no gated clock and every flip-flop takes the enable itself. This script reads
the (already patched) VHDL files of a directory and writes the same files with

  * a port   CE28 : in std_logic := '1'   as first port of every entity that has a CLK/clock port,
  * the same port in every `component` declaration of such an entity,
  * `CE28 => CE28,` first in every `port map` that instantiates such an entity,
  * `and CE28 = '1'` on every clock edge: rising_edge(x) and  x'event and x = '1'.

Entities of ours that are named like Watson's (trx/*.vhd: pokeymax, fifo_*, stubs) have the CE28 port by hand and are
listed in OURS. The 6502 adapter (cores/atari800/cpu.vhd) has the enable under the name CE. Nothing else is touched:
the result differs from the input only in these places (diff -r shows exactly them).

Usage: atari800_ce28.py <input dir> <output dir>      (input: files *.vhd / *.vhdl in the subdirectories)
"""
import os
import re
import sys

OURS = {"pokeymax", "fifo_receive", "fifo_transmit", "ds1305redux", "large_bram", "vbxe"}
CPU = {"cpu": "CE"}                      # entity -> name of its enable port (instantiated, never edited here)
CLKNAME = re.compile(r"\b(clk|clock)\b\s*(,\s*\w+\s*)*:\s*in\b", re.I)


def read(path):
    with open(path, encoding="utf-8", errors="replace", newline="") as f:
        return f.read()


def strip_comments(s):
    return re.sub(r"--[^\n]*", "", s)


def entities(text):
    """(name, start of its port list '(' , end) for every entity declaration."""
    out = []
    for m in re.finditer(r"^\s*entity\s+(\w+)\s+is\b", text, re.I | re.M):
        pm = re.compile(r"\bport\s*\(", re.I).search(text, m.end())
        em = re.compile(r"\bend\b", re.I).search(text, m.end())
        if pm and em and pm.start() < em.start():
            out.append((m.group(1).lower(), pm.end()))
    return out


def has_clock_port(text, name):
    for n, pos in entities(text):
        if n == name:
            # port list = up to the matching close paren
            depth, i = 1, pos
            while depth and i < len(text):
                c = text[i]
                depth += (c == "(") - (c == ")")
                i += 1
            return bool(CLKNAME.search(strip_comments(text[pos:i])))
    return False


def balanced_end(text, i):
    """index just after the ')' matching the '(' that ends at i-1 (i points behind the '(')"""
    depth = 1
    while depth and i < len(text):
        c = text[i]
        depth += (c == "(") - (c == ")")
        i += 1
    return i


def main(src, dst):
    files = []
    for d, _, fs in os.walk(src):
        for f in sorted(fs):
            if f.lower().endswith((".vhd", ".vhdl")):
                files.append(os.path.join(d, f))
    texts = {p: read(p) for p in files}
    patched = set(OURS)
    for p, t in texts.items():
        for name, _ in entities(strip_comments(t)):
            if has_clock_port(strip_comments(t), name):
                patched.add(name)
    count = {"ports": 0, "components": 0, "instances": 0, "edges": 0}
    for p, t in texts.items():
        out = t
        # 1. entity ports and component declarations: insert the port behind `port (`
        edits = []
        for m in re.finditer(r"^\s*entity\s+(\w+)\s+is\b", out, re.I | re.M):
            name = m.group(1).lower()
            if name in patched and name not in OURS:
                pm = re.compile(r"\bport\s*\(", re.I).search(out, m.end())
                edits.append((pm.end(), "\n\t\tCE28 : in std_logic := '1';", "ports"))
        for m in re.finditer(r"^\s*component\s+(\w+)\b", out, re.I | re.M):
            name = m.group(1).lower()
            if name in patched:
                pm = re.compile(r"\bport\s*\(", re.I).search(out, m.end())
                edits.append((pm.end(), "\n\t\tCE28 : in std_logic := '1';", "components"))
        # 2. instances: header = text since the last ';' up to `port map (`
        for m in re.finditer(r"\bport\s+map\s*\(", out, re.I):
            semi = max(out.rfind(";", 0, m.start()), 0)
            head = strip_comments(out[semi:m.start()])
            # drop a generic map (balanced)
            gm = re.search(r"\bgeneric\s+map\s*\(", head, re.I)
            if gm:
                head = head[:gm.start()]
            ids = re.findall(r"\w+", head)
            if not ids:
                continue
            name = ids[-1].lower()
            if name in CPU:
                edits.append((m.end(), "%s => CE28, " % CPU[name], "instances"))
            elif name in patched:
                edits.append((m.end(), "CE28 => CE28, ", "instances"))
        for pos, txt, kind in sorted(edits, reverse=True):
            out = out[:pos] + txt + out[pos:]
            count[kind] += 1
        # 3. clock edges
        out, n1 = re.subn(r"\brising_edge\s*\(\s*(\w+)\s*\)", r"(rising_edge(\1) and CE28 = '1')", out, flags=re.I)
        out, n2 = re.subn(r"(\w+)'event\s+and\s+\1\s*=\s*'1'", r"\1'event and \1 = '1' and CE28 = '1'", out, flags=re.I)
        count["edges"] += n1 + n2
        q = os.path.join(dst, os.path.relpath(p, src))
        os.makedirs(os.path.dirname(q), exist_ok=True)
        with open(q, "w", encoding="utf-8", newline="") as f:
            f.write(out)
    print("ce28: %d entity ports, %d component ports, %d instances, %d clock edges" %
          (count["ports"], count["components"], count["instances"], count["edges"]))
    print("clocked entities:", " ".join(sorted(patched - OURS)))


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2])
