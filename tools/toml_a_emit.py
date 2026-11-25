#!/usr/bin/env python3
"""T239.3 D55: a TOML A skeleton from the RTL decode (the `emit` command of tools/rtl_check.py).

  rtl_check.py emit --design DIR --toml-a-out FILE --core NAME [--list FILE]... [--define NAME]... [--top top]
                    [--bus MODULE:write=P,read=P,address=P,data=P,response=P --window BASE:SIZE[:full]]...
                    [--header FILE]... [--ident-id ABCD] [--module NAME] [--scan LO:SIZE | --pages first[:N]] [-j N] [--cache FILE] [--no-verify] [--maps DIR]...

Takes the decode engine of `check` and asks it the other way round: for every byte of the window the host hands to the core (or of --scan), is it written, is it
read? (the same two questions `check` asks per property: Checker.stim). The bytes with the same answer form runs; a run is cut into registers (width 8, 16 or 32,
naturally aligned) at the offsets the core's C headers name (--header: `#define NAME 0x...`, the offset in the core window; a name that is a bit mask of an earlier
name is not an offset), and a run of 16 or more bytes that no header cuts is one run property (count). dir is exactly what the RTL does at every byte of the register.
Registers are grouped into blocks by holes (4 bytes or more) and the standard block (the first 16 bytes). What is left for a person: the meaning (name placeholders
`r_<offset>` carry a `# TODO meaning` marker), the grouping into capabilities, the `module` of each block, the four characters of [identity]. The result is checked with
`check` against the same RTL before the command succeeds (--no-verify skips it).

--pages first[:N] limits the scan to the first N bytes of the window (default 0x1000) and the 256-byte pages that a header names (the dry run's mode: minutes became seconds; a register
elsewhere that no header names is missed, which the full scan, the default, does not).

The scan is one stimulus per byte and direction (about 4 ms each: a 64 KiB window takes minutes), spread over -j processes (default 2); --cache FILE keeps the bytes' answers
so a second run with the same RTL is instant (delete it when the RTL changes).
"""
import json
import multiprocessing
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rtl_check as R

BLOCK_GAP = 4          # a hole of this many bytes starts a new block
RUN_MIN = 16           # an unnamed run of this many bytes is one run property
STD_SIZE = 0x10        # the standard block of the core window: id, caps, build


# ---------------------------------------------------------------- headers

def header_offsets(paths, wsize):
    """{offset: name} from `#define NAME value` lines of C headers (object-like and function-like macros, evaluated; names that are masks of an earlier name are left out)."""
    objs, funcs, order = {}, {}, []
    for p in paths:
        text = re.sub(r"/\*.*?\*/", " ", open(p).read(), flags=re.S)
        text = re.sub(r"//[^\n]*", "", text)
        for line in text.split("\n"):
            m = re.match(r"\s*#\s*define\s+([A-Za-z_]\w*)\(([\w, ]*)\)\s+(.*)", line)
            if m:
                funcs[m.group(1)] = ([a.strip() for a in m.group(2).split(",")], m.group(3).strip())
                continue
            m = re.match(r"\s*#\s*define\s+([A-Za-z_]\w*)\s+(.+)", line)
            if m:
                objs[m.group(1)] = m.group(2).strip()
                order.append(m.group(1))

    def ev(expr, depth=0):
        if depth > 8:
            raise ValueError
        for _ in range(8):
            m = re.search(r"([A-Za-z_]\w*)\(([^()]*)\)", expr)
            if not m or m.group(1) not in funcs:
                break
            params, body = funcs[m.group(1)]
            args = [a.strip() for a in m.group(2).split(",")]
            for pn, av in zip(params, args):
                body = re.sub(r"\b%s\b" % re.escape(pn), "(" + av + ")", body)
            expr = expr[:m.start()] + "(" + body + ")" + expr[m.end():]

        def sub(mm):
            n = mm.group(0)
            if n in objs:
                return "(" + str(ev(objs[n], depth + 1)) + ")"
            raise ValueError
        expr = re.sub(r"\b[A-Za-z_]\w*\b", lambda mm: mm.group(0) if re.fullmatch(r"0[xX][0-9a-fA-F]+[uUlL]*|\d+[uUlL]*", mm.group(0)) else sub(mm), expr)
        expr = re.sub(r"\b(0[xX][0-9a-fA-F]+|\d+)[uUlL]+\b", r"\1", expr)
        if not re.fullmatch(r"[0-9a-fA-FxX+\-*()| <>&~\s]*", expr):
            raise ValueError
        return int(eval(expr, {"__builtins__": {}}, {}))

    out, seen, last = {}, [], None
    for n in order:
        try:
            v = ev(objs[n])
        except (ValueError, SyntaxError, TypeError, ZeroDivisionError):
            continue
        if not 0 <= v < wsize:
            continue
        if any(n.startswith(s + "_") for s in seen) or re.search(r"(_MASK|_BIT\d*|_SHIFT|_ST_\w+)$", n):
            continue                              # a mask or a field of an earlier name
        if v < 0x100 and v & (v - 1) == 0 and last is not None and last > v:
            continue                              # a single bit after a register with a larger offset: the bit of that register
        seen.append(n)
        last = v
        out.setdefault(v, n)
    return out


def short_names(offs):
    """Lower-case names without the prefix that most of the names share (`AM_`, `C128_`)."""
    if not offs:
        return {}
    pref = {}
    for n in offs.values():
        if "_" in n:
            pref[n.split("_", 1)[0]] = pref.get(n.split("_", 1)[0], 0) + 1
    best = max(pref, key=pref.get) if pref else None
    cut = best + "_" if best and pref[best] * 2 >= len(offs) else ""
    res = {}
    for o, n in offs.items():
        s = n[len(cut):] if cut and n.startswith(cut) else n
        s = re.sub(r"[^a-z0-9_]", "_", s.lower()).strip("_")[:28] or "r"
        if s[0].isdigit():
            s = "r_" + s
        res[o] = s
    return res


# ---------------------------------------------------------------- scan

_W = {}


def _mk(args):
    design, lists, defines, top, bus, window, full = args
    for d in defines:
        R.DEFINES.add(d)
    des = R.Design(design, lists or "build.tcl")
    doc = {"io": {"iobase": 0x10000000}, "block": []}
    wnd = None
    if ":" in bus:
        wb, _, rest = window.partition(":")
        ws, _, fl = rest.partition(":")
        wnd = (int(wb, 0) - doc["io"]["iobase"], int(ws, 0))
        full = fl == "full"
    return R.Checker(des, top, bus, doc, wnd, full)


def _scan_chunk(job):
    args, lo, n = job
    key = args
    if _W.get("key") != key:
        _W["key"], _W["ck"] = key, _mk(args)
    ck = _W["ck"]
    out = []
    for a in range(lo, lo + n):
        try:
            w = bool(ck.stim(a, True)["write"])
            r = bool(ck.stim(a, False)["read"])
        except R.Unsupported as e:
            out.append((a, "U", str(e)))
            continue
        if w or r:
            out.append((a, w, r))
    return out


def scan_window(args, ranges, jobs, label, cache, cache_file):
    """[(rtl address, write, read)] for the bytes decoded in the ranges [(lo, size)] (rtl addresses = offsets from iobase); the answers of a 256-byte chunk are cached by (window, chunk)."""
    step = 256
    chunks = [(a, min(step, lo + size - a)) for lo, size in ranges for a in range(lo, lo + size, step)]
    for c in list(cache):          # an answer that was Unsupported is not kept: the checker may know the pattern by the next run
        if any(x[1] == "U" for x in cache[c]):
            del cache[c]
    todo = [c for c in chunks if "%s|%d|%d" % (args[5], c[0], c[1]) not in cache]
    done = 0
    if todo:
        jl = [(args, a, n) for a, n in todo]
        if jobs > 1:
            it = multiprocessing.get_context("fork").Pool(jobs).imap(_scan_chunk, jl)
        else:
            it = map(_scan_chunk, jl)
        for i, part in enumerate(it):
            cache["%s|%d|%d" % (args[5], todo[i][0], todo[i][1])] = part
            done += todo[i][1]
            if i % 16 == 15 or i == len(jl) - 1:
                sys.stderr.write("\remit: %s scanned %d/%d bytes" % (label, done, sum(n for _, n in todo)))
                sys.stderr.flush()
        sys.stderr.write("\n")
        if cache_file:
            json.dump(cache, open(cache_file, "w"))
    res = []
    for a, n in chunks:
        res += [tuple(x) for x in cache["%s|%d|%d" % (args[5], a, n)]]
    return res


# ---------------------------------------------------------------- registers and blocks

def build_registers(bytes_, splits):
    """bytes_ {off: (w, r)}; splits: set of offsets a register starts at (the header's). -> [dict(off, width, count, dir)] sorted by off."""
    offs = sorted(bytes_)
    runs, i = [], 0
    while i < len(offs):
        j = i
        while j + 1 < len(offs) and offs[j + 1] == offs[j] + 1 and bytes_[offs[j + 1]] == bytes_[offs[i]]:
            j += 1
        runs.append((offs[i], offs[j] + 1, bytes_[offs[i]]))
        i = j + 1
    regs = []
    for s, e, (w, r) in runs:
        d = ("r" if r else "") + ("w" if w else "")
        d = "rw" if (w and r) else ("w" if w else "r")
        inner = [q for q in splits if s < q < e]
        if e - s >= RUN_MIN and not inner:
            nb = 4 if s % 4 == 0 and (e - s) % 4 == 0 else 1
            regs.append(dict(off=s, width=nb * 8, count=(e - s) // nb, dir=d))
            continue
        p = s
        while p < e:
            lim = min([q for q in splits if p < q < e] + [e])
            nb = 4
            while nb > 1 and (p % nb or p + nb > lim):
                nb //= 2
            regs.append(dict(off=p, width=nb * 8, count=1, dir=d))
            p += nb
    return regs


def group_blocks(regs):
    blocks, cur = [], None
    for r in regs:
        end = r["off"] + r["width"] // 8 * r["count"]
        if cur is not None and (r["off"] - cur["end"] < BLOCK_GAP) and not (cur["first"] == 0 and cur["end"] <= STD_SIZE and r["off"] >= STD_SIZE):
            cur["regs"].append(r)
            cur["end"] = end
        else:
            cur = dict(first=r["off"], end=end, regs=[r])
            blocks.append(cur)
    return blocks


def emit_text(core, ident_id, bases, blocks, names, module, notes, ioc):
    L = []
    L.append("# TOML A of the %s core: a SKELETON that tools/rtl_check.py emit wrote from what the RTL decodes (T239.3 D55). Addresses, sizes, widths and directions are the RTL's;" % core)
    L.append("# the names come from the core's C headers by offset where one matches. Left for a person: every `# TODO` below (meaning of an unnamed register, the block grouping and names,")
    L.append("# the module of each block, the four characters of [identity]). Proved against the RTL by `rtl_check.py check` before this file was written.")
    for n in notes:
        L.append("# " + n)
    L.append("[io]")
    L.append("iobase = 0x%08X" % ioc[0])
    L.append("u2p_io_base = 0x%08X" % ioc[1])
    L.append("")
    L.append("[identity]")
    L.append('id = "%s"        # TODO the four characters the gateware answers at the identity register' % ident_id)
    L.append('block = "ident"')
    for bi, b in enumerate(blocks):
        base = b["base"]
        bname = "ident" if b.get("ident") else "blk_%x" % b["first"]
        L.append("")
        if not b.get("ident"):
            L.append("# TODO name the block and split it into capabilities")
        L.append("[[block]]")
        L.append('name = "%s"' % bname)
        L.append('module = "%s"%s' % (module, "" if b.get("ident") else "        # TODO the gateware module that owns these registers"))
        L.append('owner = "base"')
        L.append("address = 0x%08X" % (base + b["first"]))
        L.append("size = 0x%X" % (b["end"] - b["first"]))
        used = set()
        for r in b["regs"]:
            nm = names.get(r["off"])
            todo = nm is None
            if b.get("ident") and r["off"] == 0 and r["width"] == 32:
                nm, todo = "id", False
            elif b.get("ident") and r["off"] == 8 and r["width"] == 32:
                nm, todo = "build", False
            if nm is None:
                nm = "r_%x" % r["off"]
            if nm in used:
                nm = "%s_%x" % (nm, r["off"])
            used.add(nm)
            L.append("  [[block.property]]%s" % ("        # TODO meaning" if todo else ""))
            L.append('  name = "%s"' % nm)
            L.append("  offset = 0x%X" % (r["off"] - b["first"]))
            L.append("  width = %d" % r["width"])
            if r["count"] > 1:
                L.append("  count = %d" % r["count"])
            L.append('  dir = "%s"' % r["dir"])
    return "\n".join(L) + "\n"


def _regs(path, maps):
    doc = R.parse_toml_a(path, maps)
    out = []
    for b in doc["block"]:
        for p in b.get("property", []):
            nb = p["width"] // 8
            out.append(dict(addr=b["address"] + p["offset"], width=p["width"], dir=p["dir"], count=p.get("count", 1), stride=p.get("stride", nb), name=p["name"], block=b["name"]))
    return out


def _bytes(regs):
    m = {}
    for r in regs:
        nb = r["width"] // 8
        for e in range(r["count"]):
            for k in range(nb):
                m.setdefault(r["addr"] + e * r["stride"] + k, set()).update(r["dir"])
    return m


def compare(emitted, hand, maps):
    """What differs between the emitted TOML A and a hand-written one, against the RTL (the emitted file is the RTL's decode, the hand-written one passes `check`: it is a subset of it)."""
    E, H = _regs(emitted, maps), _regs(hand, maps)
    eb, hb = _bytes(E), _bytes(H)
    ek = {(r["addr"], r["width"], r["count"]): r for r in E}
    same = [r for r in H if (r["addr"], r["width"], r["count"]) in ek and ek[(r["addr"], r["width"], r["count"])]["dir"] == r["dir"]]
    print("compare: hand-written %d propert(ies), emitted %d; identical (address, width, count, dir): %d" % (len(H), len(E), len(same)))
    names = [(r, ek[(r["addr"], r["width"], r["count"])]) for r in same if ek[(r["addr"], r["width"], r["count"])]["name"] != r["name"]]
    print("compare: of those, %d are named differently (hand-written name / header name)" % len(names))
    for h, e in names[:80]:
        print("  name  0x%08X %s.%s  emitted %s" % (h["addr"], h["block"], h["name"], e["name"]))
    sk = {id(r) for r in same}
    for h in H:
        if id(h) in sk:
            continue
        hbytes = {h["addr"] + e * h["stride"] + k for e in range(h["count"]) for k in range(h["width"] // 8)}
        inside = sorted({id(x) for x in E if any(b in hbytes for b in _bytes([x]))})
        cover = [x for x in E if id(x) in inside]
        kind = "dir" if (h["addr"], h["width"], h["count"]) in ek else ("width" if cover else "missing")
        if kind == "dir":
            e = ek[(h["addr"], h["width"], h["count"])]
            why = "hand-written dir %s is part of the RTL's %s (the check allows it): the RTL does more" % (h["dir"], e["dir"])
        elif kind == "width":
            why = "hand-written register of %d bit%s, the RTL's bytes are cut as %s" % (
                h["width"], " x%d" % h["count"] if h["count"] > 1 else "", ", ".join("%s@%+d/%d%s" % (x["dir"], x["addr"] - h["addr"], x["width"], "x%d" % x["count"] if x["count"] > 1 else "") for x in cover[:6]))
        else:
            why = "not in the emitted file: outside the windows or bytes that were scanned (a hand-written file that `check` passes is in the RTL)"
        print("  %-7s 0x%08X %s.%s %d bit %s: %s" % (kind, h["addr"], h["block"], h["name"], h["width"], h["dir"], why))
    extra = sorted(a for a in eb if a not in hb)
    runs = []
    for a in extra:
        if runs and a == runs[-1][1]:
            runs[-1][1] = a + 1
        else:
            runs.append([a, a + 1])
    print("compare: bytes the RTL decodes that the hand-written file does not describe: %d bytes in %d run(s)" % (len(extra), len(runs)))
    for lo, hi in runs[:60]:
        print("  omitted 0x%08X..0x%08X (%d bytes) dir %s" % (lo, hi - 1, hi - lo, "".join(sorted(eb[lo]))))
    print("compare: blocks: hand-written %d, emitted %d" % (len({r["block"] for r in H}), len({r["block"] for r in E})))


def cmd_emit(args):
    opts = {"--design": None, "--toml-a-out": None, "--core": None, "--top": "top", "--ident-id": None, "--module": None, "--scan": None, "--compare": None, "--pages": None, "-j": "2", "--cache": None}
    lists, buses, windows, headers, maps, defines, flags = [], [], [], [], [], [], set()
    i = 0
    while i < len(args):
        a = args[i]
        if a == "--list":
            lists.append(args[i + 1]); i += 2
        elif a == "--bus":
            buses.append(args[i + 1]); i += 2
        elif a == "--window":
            windows.append(args[i + 1]); i += 2
        elif a == "--header":
            headers.append(args[i + 1]); i += 2
        elif a == "--maps":
            maps.append(args[i + 1]); i += 2
        elif a == "--define":
            defines.append(args[i + 1]); i += 2
        elif a == "--no-verify":
            flags.add(a); i += 1
        elif a in opts:
            opts[a] = args[i + 1]; i += 2
        else:
            raise R.CheckError("unknown option %s" % a)
    if not (opts["--design"] and opts["--toml-a-out"] and opts["--core"] and buses and len(buses) == len(windows)):
        raise R.CheckError("usage: rtl_check.py emit --design DIR --toml-a-out FILE --core NAME --bus MODULE:... --window BASE:SIZE[:full] [--list F]... [--header H]... (see tools/toml_a_emit.py)")
    iobase = 0x10000000
    jobs = int(opts["-j"])
    cache = json.load(open(opts["--cache"])) if opts["--cache"] and os.path.exists(opts["--cache"]) else {}
    blocks_all, notes, names_all = [], [], {}
    first_window = True
    for bus, window in zip(buses, windows):
        wb, _, rest = window.partition(":")
        ws = int(rest.partition(":")[0], 0)
        wb = int(wb, 0)
        full = rest.partition(":")[2] == "full"
        args_ = (opts["--design"], tuple(lists), tuple(defines), opts["--top"], bus, window, full)
        base_rt = wb - iobase
        lo = base_rt
        hdr = header_offsets(headers, ws) if first_window else {}
        if opts["--pages"]:
            n = int(opts["--pages"].partition(":")[2] or "0x1000", 0)
            pg = sorted({q // 256 for q in range(0, min(n, ws), 256)} | {o // 256 for o in hdr})
            ranges = [(lo + p * 256, 256) for p in pg if p * 256 < ws]
        elif opts["--scan"]:
            so, _, sz = opts["--scan"].partition(":")
            ranges = [(lo + int(so, 0), int(sz, 0))]
        else:
            ranges = [(lo, ws)]
        res = scan_window(args_, ranges, jobs, bus.split(":")[0], cache, opts["--cache"])
        bad = [x for x in res if x[1] == "U"]
        for a, _, msg in bad[:3]:
            notes.append("UNSUPPORTED at +0x%X: %s" % (a - base_rt, msg))
        if bad:
            notes.append("%d byte(s) the decode does not follow (Unsupported) are not in this file" % len(bad))
        bytes_ = {a - base_rt: (w, r) for a, w, r in res if w != "U"}
        if bad and not bytes_:
            raise R.Unsupported(bad[0][2])
        names = short_names(hdr)
        splits = set(hdr)
        if first_window:        # the standard block: id (+0) and build (+8) are 32 bit registers whatever the headers name inside them (identity, 6.14.4)
            for lo_ in (0, 8):
                if all(bytes_.get(lo_ + k) == (False, True) for k in range(4)):
                    splits = {q for q in splits if not lo_ < q < lo_ + 4}
            if bytes_.get(4) == (False, True):      # hw.cc's trx_caps() reads ident.caps (+4); the VIC-20's and the A1200's have caps2 at +5
                splits |= {4, 5}
                names = dict(names)
                names[4] = "caps"
                if bytes_.get(5) == (False, True):
                    names[5] = "caps2"
        regs = build_registers(bytes_, splits)
        mkck = []
        for r in regs:                                   # a write run longer than the RAM that takes it aliases: keep the part the RAM holds
            if "w" in r["dir"]:
                cap = None
                if r["count"] > 1:
                    try:
                        mkck = mkck or [_mk(args_)]
                        cap = mkck[0].stim(base_rt + r["off"], True)["memcap"]
                    except R.Unsupported:
                        cap = None
                nb = r["width"] // 8
                if cap is not None and r["count"] * nb > cap:
                    notes.append("run at +0x%X clipped from %d to %d bytes (RAM behind it)" % (r["off"], r["count"] * nb, cap))
                    r["count"] = max(1, cap // nb)
        blks = group_blocks(regs)
        for k, b in enumerate(blks):
            b["base"] = wb
            b["ident"] = first_window and k == 0 and b["first"] == 0 and b["end"] <= STD_SIZE
        if first_window:
            unused = sorted(o for o in hdr if not any(r["off"] == o for r in regs))
            if unused:
                notes.append("%d header name(s) with no register at that offset in the RTL: %s" % (len(unused), " ".join(hdr[o] for o in unused[:12]) + (" ..." if len(unused) > 12 else "")))
            names_all.update(names)
        else:
            names = {}
        blocks_all += blks
        first_window = False
    # a dense region (more than 64 registers in a block: a hand-over to another module, a RAM) and what is over the record's limits are not emitted: a person describes them as a window
    split = []
    for b in blocks_all:                # a block of 65..256 registers is cut into blocks of 64 (the record holds 64 properties per block); more is a hand-over or a RAM
        if 64 < len(b["regs"]) <= 256:
            for k in range(0, len(b["regs"]), 64):
                part = b["regs"][k:k + 64]
                split.append(dict(first=part[0]["off"], end=part[-1]["off"] + part[-1]["width"] // 8 * part[-1]["count"], regs=part, base=b["base"], ident=False))
            notes.append("block +0x%X is cut into blocks of 64 registers (TODO group them)" % b["first"])
        else:
            split.append(b)
    blocks_all = split
    while True:
        dense = [b for b in blocks_all if len(b["regs"]) > 256]
        over = len(blocks_all) > 64 or sum(len(b["regs"]) for b in blocks_all) > 512
        if not dense and not over:
            break
        b = max(blocks_all, key=lambda x: len(x["regs"]))
        blocks_all.remove(b)
        notes.append("NOT EMITTED: +0x%X..+0x%X (%d registers, %s): describe it by hand as a window" % (
            b["first"], b["end"] - 1, len(b["regs"]), "more than 256: a hand-over or a RAM" if len(b["regs"]) > 256 else "over the record's limits of 64 blocks, 512 properties"))
    if not any(b.get("ident") for b in blocks_all):
        raise R.CheckError("no standard block: the RTL does not decode the first 16 bytes of the window as read-only registers (id at +0, build at +8; 6.14.4)%s" % (
            "; " + "; ".join(n for n in notes if n.startswith("NOT EMITTED"))[:160] if any(n.startswith("NOT EMITTED") for n in notes) else ""))
    if len(blocks_all) > 64:
        notes.append("%d blocks: the record holds 64 (6.14.10); merge blocks" % len(blocks_all))
    nprop = sum(len(b["regs"]) for b in blocks_all)
    if nprop > 512:
        notes.append("%d properties: the record holds 512 (6.14.10)" % nprop)
    core = opts["--core"]
    ident = opts["--ident-id"] or (core.upper() + "____")[:4]
    module = opts["--module"] or re.sub(r"[^a-z0-9_]", "_", re.split(r"[:.]", buses[0])[0].lower())[:31]
    named = sum(1 for b in blocks_all for r in b["regs"] if r["off"] in names_all or (b.get("ident") and r["off"] in (0, 8)))
    text = emit_text(core, ident, None, blocks_all, names_all, module, notes, (iobase, 0x10100000))
    open(opts["--toml-a-out"], "w").write(text)
    print("emit: %s: %d block(s), %d propert%s (%d named from the headers, %d `# TODO meaning`)" % (
        opts["--toml-a-out"], len(blocks_all), nprop, "y" if nprop == 1 else "ies", named, nprop - named))
    if opts["--compare"]:
        compare(opts["--toml-a-out"], opts["--compare"], maps)
    if "--no-verify" in flags:
        return 0
    chk = ["--design", opts["--design"], "--toml-a", opts["--toml-a-out"], "--top", opts["--top"]]
    for l in lists:
        chk += ["--list", l]
    for b, w in zip(buses, windows):
        chk += ["--bus", b, "--window", w]
    for m in maps:
        chk += ["--maps", m]
    for d in defines:
        chk += ["--define", d]
    return R.cmd_check(chk)
