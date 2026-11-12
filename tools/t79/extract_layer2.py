#!/usr/bin/env python3
"""Move the layer-2 instances of top.v into layer2.v (T79 trial B).

Text-based: the instance blocks move verbatim; every identifier they use that is
also used elsewhere in top.v becomes a port of layer2, the rest becomes an
internal wire. Port directions come from the instantiated modules (yosys JSON).
"""
import json, re, sys

TOP, DIRS, OUT_TOP, OUT_L2 = sys.argv[1:5]
# DIRS: yosys JSON of the instantiated modules (read_verilog -lib ...; write_json), with
# blackbox stubs for VHDL entities from tools/t79/vhdl_stub.py.
# T79.1 P1/P2: peripherals (kb bkb bio cled ai2c i2s ain), the video chain (scaler ovl pxl hdmi_enc,
# txt for now), Ethernet (rmii) and the stream mux (smux) belong to the host
L2 = "c64 drv eng tsys ddbg win tio cfill iec".split()

src = open(TOP).read()
dirs = {m: {p: v["direction"] for p, v in d["ports"].items()} for m, d in json.load(open(DIRS))["modules"].items()}


def strip_comments(t):
    t = re.sub(r"/\*.*?\*/", lambda m: " " * len(m.group(0)), t, flags=re.S)
    return re.sub(r"//[^\n]*", lambda m: " " * len(m.group(0)), t)


code = strip_comments(src)


def match_paren(t, i):
    """t[i] == '(' -> index of the matching ')'."""
    d = 0
    for j in range(i, len(t)):
        if t[j] == "(":
            d += 1
        elif t[j] == ")":
            d -= 1
            if d == 0:
                return j
    raise ValueError("unbalanced")


# --- the instance blocks -------------------------------------------------------
blocks = []  # (start, end, module, inst, params_text, ports_text)
for inst in L2:
    pat = re.compile(r"^[ \t]*(\w+)(?=[ \t\n#(])(?:\s*(#\s*\())?", re.M)
    found = None
    for m in pat.finditer(code):
        mod = m.group(1)
        if mod in ("wire", "reg", "assign", "always", "input", "output", "inout", "localparam", "parameter", "if", "else", "begin", "end", "case"):
            continue
        k = m.end()
        ptxt = ""
        if m.group(2):
            e = match_paren(code, k - 1)
            ptxt = code[k - 1:e + 1]
            k = e + 1
        m2 = re.compile(r"\s*(\w+)\s*\(").match(code, k)
        if not m2 or m2.group(1) != inst:
            continue
        p0 = m2.end() - 1
        p1 = match_paren(code, p0)
        semi = code.index(";", p1)
        start = code.rfind("\n", 0, m.start()) + 1
        end = code.index("\n", semi) + 1
        found = (start, end, mod, inst, ptxt, code[p0 + 1:p1])
        break
    if not found:
        sys.exit(f"instance {inst} not found")
    blocks.append(found)

blocks.sort()
in_block = lambda pos: any(s <= pos < e for s, e, *_ in blocks)

# --- declarations in top.v -------------------------------------------------------
KEYW = set("""module endmodule input output inout wire reg integer genvar localparam parameter assign always
begin end if else case endcase default posedge negedge or and not signed unsigned initial for generate endgenerate
function endfunction task endtask""".split())
decl = {}  # name -> (kind, width text)
hdr_end = code.index(");", code.index("module top")) + 2
for m in re.finditer(r"\b(input|output|inout)\s+(?:wire\s+|reg\s+)?(signed\s+)?(\[[^\]]+\])?\s*([\w\s,]+?)(?=,\s*(?:input|output|inout)\b|\s*\)\s*;|$)", code[:hdr_end], re.S):
    for n in m.group(4).split(","):
        n = n.strip()
        if n:
            decl[n] = (m.group(1), (m.group(2) or "") + (m.group(3) or ""))
params = set()
for m in re.finditer(r"\b(localparam|parameter)\b([^;]*);", code):
    for n in re.findall(r"(\w+)\s*=", m.group(2)):
        params.add(n)
driven_top = set()
scan = list(code)  # code with declarations reduced to their initialisers (for the use scan)
for m in re.finditer(r"^[ \t]*(?:\(\*[^*]*\*\)\s*)?(wire|reg)\s+(signed\s+)?(\[[^\]]+\])?\s*([^;]+);", code[hdr_end:], re.M):
    body = m.group(4)
    a, b = hdr_end + m.start(), hdr_end + m.end()
    inits = " ".join(re.findall(r"=([^,]*(?:\{[^}]*\}[^,]*)*)", body))
    scan[a:b] = list((" " + inits).ljust(b - a)[: b - a])
    # names: identifiers at depth 0 before '=' or ',' (skip initialiser expressions and unpacked dims)
    depth, cur, names, skip = 0, "", [], False
    for ch in body + ",":
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        if depth == 0 and ch == ",":
            nm = re.match(r"\s*(\w+)", cur)
            if nm and "=" in cur:
                driven_top.add(nm.group(1))
            if nm:
                names.append((nm.group(1), "[" in cur.split("=")[0][len(nm.group(0)):]))
            cur = ""
            continue
        cur += ch
    for n, unpacked in names:
        decl[n] = (m.group(1) + (" unpacked" if unpacked else ""), (m.group(2) or "") + (m.group(3) or ""))

# --- identifiers used by the blocks, and outside them ------------------------------
ident = re.compile(r"(?<![\w'$`.])([A-Za-z_]\w*)")


def idents(t):
    return {i for i in ident.findall(re.sub(r"\d*'[sS]?[bBhHdDoO][0-9a-fA-FxXzZ_?]+", " ", t)) if i not in KEYW}


scan = "".join(scan)
outside = set()
pos = hdr_end
for s, e, *_ in blocks:
    outside |= idents(scan[pos:s])
    pos = e
outside |= idents(scan[pos:])
pads = {n for n, (k, _) in decl.items() if k in ("input", "output", "inout")}
outside |= pads  # pins are always ports of layer 2
outside |= driven_top  # wires with an initialiser are driven in layer 1

used, l2out, l2in = set(), set(), set()
problems = []
for s, e, mod, inst, ptxt, ports in blocks:
    used |= idents(ptxt)
    md = dirs.get(mod)
    if md is None:
        sys.exit(f"no port directions for module {mod}")
    for m in re.finditer(r"\.(\w+)\s*\(", ports):
        p = m.group(1)
        e2 = match_paren(ports, m.end() - 1)
        expr = ports[m.end():e2]
        ids = idents(expr) - params
        used |= ids
        d = md.get(p)
        if d is None:
            problems.append(f"{inst}.{p}: no such port on {mod}")
            continue
        if d == "output":
            if re.fullmatch(r"\s*\w+\s*", expr) or re.fullmatch(r"\s*\{[\w\s,]*\}\s*", expr):
                l2out |= ids
            elif ids:
                # a part-select: fine if the whole vector is driven inside layer 2
                l2out |= ids
                problems.append(f"note {inst}.{p} -> {expr.strip()}")
        elif d == "inout":
            problems.append(f"{inst}.{p} is inout ({expr.strip()})")
        else:
            l2in |= ids

boundary = sorted(i for i in used if i in outside and i not in params)
internal = sorted(i for i in used if i not in outside and i not in params)
undeclared = [i for i in used if i not in decl and i not in params]
pused = sorted(i for i in used if i in params)

port_dir = {}
for n in boundary:
    if n in l2out and n in l2in:
        port_dir[n] = "output"  # driven inside, also read inside
    elif n in l2out:
        port_dir[n] = "output"
    else:
        port_dir[n] = "input"
    if decl.get(n, ("",))[0] == "input" and port_dir[n] == "output":
        problems.append(f"pin {n} is an input of top but driven by layer 2")
for n in internal:
    if n not in l2out:
        problems.append(f"internal {n} has no driver in layer 2")

print(f"blocks {len(blocks)}, ports {len(boundary)} ({sum(1 for d in port_dir.values() if d=='input')} in), internal {len(internal)}, params {pused}")
for p in problems:
    print("PROBLEM", p)
if undeclared:
    print("UNDECLARED", undeclared)

# --- layer2.v ----------------------------------------------------------------------
def w(n):
    return decl.get(n, ("wire", ""))[1]


pd = []
for n in boundary:
    pd.append(f"    {port_dir[n]:6} wire {w(n)+' ' if w(n) else ''}{n}")
L = ["// T79 trial B: layer 2, the reconfigurable partition. Generated from top.v by",
     "// tools/t79/extract_layer2.py: the instances below are top.v's, moved verbatim;",
     "// every signal they share with layer 1 is a port.",
     "`timescale 1ns / 1ps",
     "module layer2 #("]
def pdecl(p):
    m = re.search(r"\b(?:localparam|parameter)\s*(\[[^\]]*\])?\s*" + p + r"\s*=\s*([^,;]+)", code)
    return f"    parameter {(m.group(1) + ' ') if m and m.group(1) else ''}{p} = {m.group(2).strip() if m else 0}"
L.append(",\n".join(pdecl(p) for p in pused) + "\n) (")
L.append(",\n".join(pd))
L.append(");")
for n in internal:
    L.append(f"    wire {w(n)+' ' if w(n) else ''}{n};")
L.append("")
for s, e, *_ in blocks:
    L.append(src[s:e].rstrip("\n"))
    L.append("")
L.append("endmodule")
open(OUT_L2, "w").write("\n".join(L) + "\n")

# --- top.v without the blocks, with the layer2 instance -------------------------------
out, pos = [], 0
for s, e, *_ in blocks:
    out.append(src[pos:s])
    pos = e
rest = src[pos:]
inst = ["    // T79 trial B: layer 2 (layer2.v), the reconfigurable partition",
        "    layer2 l2 (   // parameters: layer2.v carries top.v's values (an out-of-context partition takes none)"]
inst.append(",\n".join(f"        .{n}({n})" for n in boundary))
inst.append("    );\n")
k = rest.rindex("endmodule")
# the last module in top.v may be a helper (rst_sync): put the instance before top's endmodule
k = (("".join(out)) + rest).find("endmodule", hdr_end)
full = "".join(out) + rest
full = full[:k] + "\n".join(inst) + "\n" + full[k:]
open(OUT_TOP, "w").write(full)
