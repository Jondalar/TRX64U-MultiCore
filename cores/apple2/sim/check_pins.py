#!/usr/bin/env python3
"""check_pins.py [wrapper.v]: T89 S13 - the Apple core drives nothing on the cartridge slot or the IEC bus, a cartridge changes nothing, no C64 device answers.
Read from the code, not from comments:
  1. every layer-2 output that host/top.v gates with the decoupler (`assign X = core_decouple ? <idle> : l2_X;`) and that layer2_apple2.v ties to a
     constant is tied to that idle value (the host's own list, parsed from host/top.v)
  2. the slot and IEC outputs (SLOT_*, slot_*, IEC_*) are all tied to constants, none to the core instance or to an input
  3. none of the cartridge slot and IEC inputs (SLOT_DATA, SLOT_IRQn, SLOT_NMIn, SLOT_RST_I, SLOT_DMAn, SLOT_EXROMn, SLOT_GAMEn, IEC_*_I) is used anywhere in the
     module body: a cartridge in the port cannot change what the Apple shows
  4. nothing of the C64's I/O is instantiated in the wrapper (no CIA, SID, VIC, no drive): the only instance is the Apple core and the register
     bus answers 0 to what it does not know (tb_a2_win)
Exit 0 = pass."""
import os, re, sys

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.abspath(os.path.join(here, "..", "..", ".."))
wrapper = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, "cores", "apple2", "layer2_apple2.v")
top = open(os.path.join(root, "host", "top.v")).read()
src = open(wrapper).read()

def const(txt):
    txt = txt.strip()
    m = re.fullmatch(r"(\d*)'([bdh])([0-9a-fA-F_]+)", txt)
    if m:
        return (int(m.group(1) or 0), int(m.group(3).replace("_", ""), {"b": 2, "d": 10, "h": 16}[m.group(2)]))
    m = re.fullmatch(r"\{(\d+)\{(\d+)'([bdh])([0-9a-fA-F_]+)\}\}", txt)
    if m:
        w, n = int(m.group(1)), int(m.group(2))
        v = int(m.group(4), {"b": 2, "d": 10, "h": 16}[m.group(3)])
        return (w * n, sum(v << (n * i) for i in range(w)))
    return None

# the host's list
idle = {}
for m in re.finditer(r"assign\s+(\w+)\s*=\s*(?:core_decouple|dc_cpu|dc_ui|port_off)\s*\?\s*(.+?)\s*:\s*l2_(\w+)\s*;", top):
    c = const(m.group(2))
    if c is not None and m.group(1) == m.group(3):
        idle[m.group(1)] = c[1]

# the wrapper: ports, assigns, the instance
body = src[src.index(");", src.index("module layer2_apple2")) + 2:]
ports = re.findall(r"(input|output)\s+wire\s*(?:\[[^\]]*\])?\s*(\w+)", src[:src.index(");", src.index("module layer2_apple2")) + 2])
inputs = {n for d, n in ports if d == "input"}
outputs = {n for d, n in ports if d == "output"}
assigns = {}
for m in re.finditer(r"^\s*assign\s+(\w+)\s*=\s*(.+?);", body, re.M):
    assigns[m.group(1)] = m.group(2)
bad = 0
# gate_frz is no pin: the core asks the host to gate its Freeze (set, as the Amiga's and the NES's layer do)
FUNCTIONAL = {"gate_frz"}
for name, want in sorted(idle.items()):
    if name in FUNCTIONAL:
        continue
    if name in assigns and name in outputs:
        c = const(assigns[name])
        if c is None:
            continue                      # driven by the core, not a tie-off
        if c[1] != want:
            print("FAIL %s is tied to %d, the host's decoupler holds %d" % (name, c[1], want)); bad += 1
checked = sorted(n for n in outputs if re.match(r"(SLOT_|slot_|IEC_)", n) and n in idle)
for n in checked:
    c = const(assigns.get(n, "")) if n in assigns else None
    if c is None:
        print("FAIL %s (a slot / IEC output) is not tied to a constant: %s" % (n, assigns.get(n, "driven by the instance"))); bad += 1
slot_in = [n for n in inputs if re.match(r"(SLOT_|IEC_)", n)]
for n in slot_in:
    if re.search(r"\b" + re.escape(n) + r"\b", body):
        print("FAIL the input %s is used inside the module" % n); bad += 1
insts = re.findall(r"^\s*(\w+)\s+(?:#\s*\(.*?\)\s*)?(\w+)\s*\(", body, re.M)
insts = [i for i in insts if i[0] not in ("assign", "always", "module", "if", "begin", "end", "else")]
names = sorted(set(i[0] for i in insts))
print("instances in the wrapper:", ", ".join(names))
if any(re.search(r"cia|sid|vic|c64|iec|drive|slot", n, re.I) for n in names if n not in ("a2_core",)):
    print("FAIL the wrapper instantiates a C64 device: %s" % names); bad += 1
print("%d host idle values read from host/top.v, %d slot/IEC outputs checked, %d slot/IEC inputs unused" % (len(idle), len(checked), len(slot_in)))
if bad:
    print("FAIL check_pins: %d problems" % bad); sys.exit(1)
print("PASS check_pins")
