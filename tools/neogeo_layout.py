#!/usr/bin/env python3
"""T143: what the Neo Geo core finds in the DDR2, from real ROM files, in two layouts.

  mister  the bytes MiSTer's loader (Main_MiSTer support/neogeo/neogeo_loader.cpp, GPL v3) puts into the core's memory: the layout the core
          (rtl/neogeo.sv, sdram_mux.sv, zmc2_dot.v) was written for. Transcribed from the code (spr_convert_skp / spr_convert_dbl / fix_convert / spr_bswap,
          load_crom_to_mem, load_rom_to_mem), not from its comments.
  app     what cores/neogeo/app/lib/neoset.cc wrote BEFORE the fix of 2026-10-29 (neo_load, neo_fix_rearrange; the NK_C / NK_CNEO / NK_CRAW / NK_S / NK_P paths, copied here): the layout of
          the board run of new2. Since the fix neoset.cc writes the mister layout; `app` is kept to tell what is on a board that still has the old one.

The DDR2 placement is the application's (neoset.cc neo_plan, neogeo_regs.h): rom_base = 0x01000000 + 0x300000, system ROM at +0, board fix ROM at +0x20000,
cartridge fix ROM S1 at +0x80000, P1 at +0x200000, P2 at +0x300000, the C ROM from rom_base + (c_start MB), the pair of C files with index 64 + 2 * n at + n MB
(c1/c2 of Metal Slug are 64/65, c3/c4 80/81).

  tools/neogeo_layout.py show  --roms DIR [--set mslug] [--layout mister|app|both] (--addr 0x01700000 | --what c|s|sfix|bios|p --offset N) [--len 64]
  tools/neogeo_layout.py addrs --roms DIR            the DDR2 byte addresses of the regions and the C start the plan gives
  tools/neogeo_layout.py compare --roms DIR          how many bytes of each region the two layouts have in common
  tools/neogeo_layout.py image --roms DIR --layout mister|app --out DIR   vspace.bin etc. for the bench (cores/neogeo/sim/run.sh real)
  tools/neogeo_layout.py png frame_000.txt ...       the bench's frame dumps as PNG
  tools/neogeo_layout.py selftest                    the transcription checked against itself (no ROM needed)

Needs numpy. The ROM files are the user's and never go into the repository.
"""
import argparse
import os
import sys

import numpy as np

ROM_BASE = 0x01000000 + 0x300000          # neogeo_regs.h NEO_DDR_BASE + NEO_W_ROM
MSLUG_FILES = {"p": "201-p1.p1", "s": "201-s1.s1", "m": "201-m1.m1", "v1": "201-v1.v1", "v2": "201-v2.v2",
               "c": [(64, "201-c1.c1"), (65, "201-c2.c2"), (80, "201-c3.c3"), (81, "201-c4.c4")]}


# ---------------------------------------------------------------- MiSTer's conversions, from neogeo_loader.cpp
def spr_perm_skp(n_words):
    """spr_convert_skp: out[i] = in[(i & ~0x1F) | ((i >> 1) & 0xF) | (((i & 1) ^ 1) << 4)]"""
    i = np.arange(n_words, dtype=np.int64)
    return (i & ~0x1F) | ((i >> 1) & 0xF) | (((i & 1) ^ 1) << 4)


def spr_perm_dbl(n_words):
    """spr_convert_dbl: out[i] = in[(i & ~0x3F) | ((i ^ 1) & 1) | ((i >> 1) & 0x1E) | (((i & 2) ^ 2) << 4)]"""
    i = np.arange(n_words, dtype=np.int64)
    return (i & ~0x3F) | ((i ^ 1) & 1) | ((i >> 1) & 0x1E) | (((i & 2) ^ 2) << 4)


def fix_perm(n):
    """fix_convert: out[i] = in[(i & ~0x1F) | ((i >> 2) & 7) | ((i & 1) << 3) | (((i & 2) << 3) ^ 0x10)]"""
    i = np.arange(n, dtype=np.int64)
    return (i & ~0x1F) | ((i >> 2) & 7) | ((i & 1) << 3) | (((i & 2) << 3) ^ 0x10)


def mister_fix(data, size=None):
    """fix_convert over the whole transfer: the file, zero padded to `size` (a multiple of 32)"""
    size = size or (len(data) + 31) // 32 * 32
    buf = np.zeros(size, dtype=np.uint8)
    buf[:len(data)] = np.frombuffer(data, dtype=np.uint8)
    return buf[fix_perm(size)].tobytes()


def mister_crom_pair_file(data, index):
    """load_crom_to_mem for one file: 2 * len bytes, the file's words (16 bit) spread to every second word of that region:
    out word 2 * i + ((index ^ 1) & 1) = in word perm_skp(i); c1 (even index) fills the odd words, c2 the even ones."""
    n = len(data) // 2
    words = np.frombuffer(data[:2 * n], dtype="<u2")
    out = np.zeros(2 * n, dtype="<u2")
    out[((index ^ 1) & 1)::2] = words[spr_perm_skp(n)]
    return out.tobytes()


def mister_neo_c(data, swap=1):
    """the C part of a .neo file (index 15): spr_bswap (middle two bytes of every 32 bit unit) and spr_convert_dbl, written as one run"""
    n = len(data) // 4 * 4
    b = np.frombuffer(data[:n], dtype=np.uint8).copy()
    if swap:
        b2 = b.reshape(-1, 4)
        b2[:, [1, 2]] = b2[:, [2, 1]]
    words = b.view("<u2")
    nw = len(words) // 64 * 64
    out = words[:nw][spr_perm_dbl(nw)]
    return out.tobytes()


# ---------------------------------------------------------------- what neoset.cc does today
def app_fix(data, size=None):
    """neo_fix_rearrange: per 32 bytes, out[line * 4 + k] = in[{2, 3, 0, 1}[k] * 8 + line]"""
    size = size or (len(data) + 31) // 32 * 32
    buf = np.zeros(size, dtype=np.uint8)
    buf[:len(data)] = np.frombuffer(data, dtype=np.uint8)
    t = buf.reshape(-1, 4, 8)                      # [tile][block][line]
    blk = [2, 3, 0, 1]
    res = np.zeros((t.shape[0], 8, 4), dtype=np.uint8)   # [tile][line][k]
    for k in range(4):
        res[:, :, k] = t[:, blk[k], :]
    return res.reshape(-1).tobytes()


def app_crom_file(data, index):
    """NK_C: the file's byte pairs go to every second pair of a 4 byte unit, parity = index & 1 (bytes 0,1 for an even index), in file order"""
    n = len(data) // 2
    words = np.frombuffer(data[:2 * n], dtype="<u2")
    out = np.zeros(2 * n, dtype="<u2")
    out[(index & 1)::2] = words
    return out.tobytes()


def app_neo_c(data):
    """NK_CNEO: per 4 bytes {b0, b1, b2, b3} -> {b0, b2, b1, b3}, in file order"""
    n = len(data) // 4 * 4
    b = np.frombuffer(data[:n], dtype=np.uint8).copy().reshape(-1, 4)
    b[:, [1, 2]] = b[:, [2, 1]]
    return b.reshape(-1).tobytes()


# ---------------------------------------------------------------- regions
def read(roms, name):
    p = os.path.join(roms, name)
    return open(p, "rb").read() if os.path.exists(p) else None


def build(roms, layout, files=MSLUG_FILES, c_start=None):
    """-> {region name: (offset from rom_base, bytes)}"""
    reg = {}
    bios = read(roms, "sp-s2.sp1") or read(roms, "uni-bios.rom")
    if bios:
        reg["bios"] = (0, bios[:0x20000])
    sfix = read(roms, "sfix.sfix")
    fixf = mister_fix if layout == "mister" else app_fix
    if sfix:
        reg["sfix"] = (0x20000, fixf(sfix[:0x20000]))
    gdir = os.path.join(roms, "mslug") if os.path.isdir(os.path.join(roms, "mslug")) else roms
    s = read(gdir, files["s"])
    if s:
        reg["s"] = (0x80000, fixf(s))
    p = read(gdir, files["p"])
    if p:
        reg["p"] = (0x200000, p)
        pend = 0x200000 + len(p)
    else:
        pend = 0x300000
    if c_start is None:
        c_start = max(3, (max(pend, 0x300000) + 0xFFFFF) >> 20)
    cf = [(i, read(gdir, n)) for i, n in files["c"]]
    cf = [(i, d) for i, d in cf if d]
    if cf:
        span = 0
        for i, d in cf:
            span = max(span, (((i - 64) >> 1) << 20) + 2 * len(d))
        buf = bytearray(span)
        for i, d in cf:
            img = mister_crom_pair_file(d, i) if layout == "mister" else app_crom_file(d, i)
            # both layouts write only the words of the file's own parity (the loader / the application writes every second word with a mask): c1 and c2 of a pair end up in one region
            o = ((i - 64) >> 1) << 20
            cur = np.frombuffer(bytes(buf[o:o + len(img)]), dtype="<u2").copy()
            new = np.frombuffer(img, dtype="<u2")
            par = ((i ^ 1) & 1) if layout == "mister" else (i & 1)
            m = (np.arange(len(new)) & 1) == par
            cur[m] = new[m]
            buf[o:o + len(img)] = cur.tobytes()
        reg["c"] = (c_start << 20, bytes(buf))
    return reg, c_start


# ---------------------------------------------------------------- command line
def hexrow(base, b):
    return "\n".join("%08X: %s" % (base + r, " ".join("%02X" % x for x in b[r:r + 16])) for r in range(0, len(b), 16))


def where(reg, addr):
    for n, (off, b) in reg.items():
        a = ROM_BASE + off
        if a <= addr < a + len(b):
            return n, addr - a
    return None, 0


def cmd_show(a):
    layouts = ["mister", "app"] if a.layout == "both" else [a.layout]
    for lay in layouts:
        reg, cs = build(a.roms, lay, c_start=a.c_start)
        if a.addr is not None:
            name, off = where(reg, a.addr)
            if name is None:
                print("%s: 0x%08X is outside every region (c_start %d)" % (lay, a.addr, cs)); continue
            start = a.addr
        else:
            base, b = reg[a.what]
            off = a.offset
            start = ROM_BASE + base + off
            name = a.what
        data = reg[name][1][off:off + a.len]
        print("== %s layout, region %s, DDR2 0x%08X (c_start %d MB):" % (lay, name, start, cs))
        print(hexrow(start, data))


def cmd_addrs(a):
    reg, cs = build(a.roms, "mister", c_start=a.c_start)
    print("rom_base 0x%08X, c_start %d MB" % (ROM_BASE, cs))
    for n, (off, b) in reg.items():
        print("  %-5s DDR2 0x%08X .. 0x%08X (%d bytes)" % (n, ROM_BASE + off, ROM_BASE + off + len(b) - 1, len(b)))


def cmd_compare(a):
    m, _ = build(a.roms, "mister", c_start=a.c_start)
    p, _ = build(a.roms, "app", c_start=a.c_start)
    for n in m:
        x = np.frombuffer(m[n][1], dtype=np.uint8)
        y = np.frombuffer(p[n][1], dtype=np.uint8)
        k = min(len(x), len(y))
        eq = int((x[:k] == y[:k]).sum())
        print("%-5s %9d bytes, equal in both layouts: %9d (%.2f %%)" % (n, k, eq, 100.0 * eq / k))


def cmd_overlay(a):
    """the real C ROM in one layout over a generated image (cores/neogeo/sim/mkimg.py): the sprites of the bench then show real art; c_start 3 as the bench has it"""
    reg, cs = build(a.roms, a.layout, c_start=3)
    vs = bytearray(open(os.path.join(a.out, "vspace.bin"), "rb").read())
    off, b = reg["c"]
    vs.extend(bytes(max(0, off + len(b) - len(vs))))
    vs[off:off + len(b)] = b
    if "sfix" in reg:                                    # the board fix ROM as the cartridge's fix ROM S1 (the bench's fix tiles come from there)
        o2, b2 = reg["sfix"]
        vs[o2:o2 + len(b2)] = b2                         # where the machine reads it with the system latch set (MVS, from reset until the BIOS hands over)
        vs[0x80000:0x80000 + len(b2)] = b2
    open(os.path.join(a.out, "vspace.bin"), "wb").write(bytes(vs))
    print("overlaid the C ROM (%s layout) at %d MB in %s/vspace.bin (%d bytes)" % (a.layout, cs, a.out, len(vs)))


def cmd_image(a):
    """vspace.bin of the bench: the ROM space from rom_base, padded to the size of the largest region; the other files as the bench wants them"""
    reg, cs = build(a.roms, a.layout, c_start=a.c_start)
    end = max(off + len(b) for off, b in reg.values())
    vs = bytearray(end)
    for n, (off, b) in reg.items():
        vs[off:off + len(b)] = b
    os.makedirs(a.out, exist_ok=True)
    open(os.path.join(a.out, "vspace.bin"), "wb").write(bytes(vs))
    gdir = os.path.join(a.roms, "mslug") if os.path.isdir(os.path.join(a.roms, "mslug")) else a.roms
    lo = read(a.roms, "000-lo.lo") or bytes(0x10000)
    open(os.path.join(a.out, "lo.bin"), "wb").write(lo[:0x10000])
    open(os.path.join(a.out, "m1.bin"), "wb").write((read(gdir, MSLUG_FILES["m"]) or bytes(0x20000)))
    open(os.path.join(a.out, "v1.bin"), "wb").write(bytes(0x10000))
    open(os.path.join(a.out, "expect.txt"), "w").write("backdrop_word 0000\np1_code_end 000000\n")
    with open(os.path.join(a.out, "cfg.txt"), "w") as f:
        f.write("c_start %d\n" % cs)
    print("wrote %s/vspace.bin (%d bytes), c_start %d" % (a.out, len(vs), cs))


def cmd_png(a):
    """frame_NNN.txt of the bench (one RGB565 hex word per line, 320 x 224) -> PNG next to it, times `--scale`"""
    import struct
    import zlib
    for src in a.files:
        px = np.array([int(l, 16) for l in open(src) if l.strip()], dtype=np.uint32)
        if len(px) != 320 * 224:
            print("%s: %d pixels, not 320 x 224" % (src, len(px))); continue
        r = ((px >> 11) & 31) * 255 // 31
        g = ((px >> 5) & 63) * 255 // 63
        b = (px & 31) * 255 // 31
        img = np.stack([r, g, b], axis=1).astype(np.uint8).reshape(224, 320, 3)
        if a.scale > 1:
            img = np.repeat(np.repeat(img, a.scale, axis=0), a.scale, axis=1)
        raw = b"".join(b"\0" + img[y].tobytes() for y in range(img.shape[0]))

        def ch(t, d):
            c = struct.pack(">I", len(d)) + t + d
            return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
        png = b"\x89PNG\r\n\x1a\n" + ch(b"IHDR", struct.pack(">IIBBBBB", img.shape[1], img.shape[0], 8, 2, 0, 0, 0)) + ch(b"IDAT", zlib.compress(raw)) + ch(b"IEND", b"")
        open(os.path.splitext(src)[0] + ".png", "wb").write(png)


def cmd_selftest(_):
    rng = np.random.default_rng(7)
    ok = True

    def check(c, what):
        nonlocal ok
        print("  %s %s" % ("ok  " if c else "FAIL", what))
        ok = ok and c
    # the fix permutation: MiSTer's formula equals the application's block order
    d = rng.integers(0, 256, 4096, dtype=np.uint8).tobytes()
    check(mister_fix(d) == app_fix(d), "S/sfix: fix_convert equals neo_fix_rearrange")
    # the C permutations are bijections
    for nm, p in (("skp", spr_perm_skp(4096)), ("dbl", spr_perm_dbl(4096))):
        check(len(set(p.tolist())) == 4096, "spr_convert_%s is a permutation" % nm)
    # the .neo path and the c1/c2 path of MiSTer's loader give the same bytes
    n = 4096
    c1 = rng.integers(0, 256, n, dtype=np.uint8).tobytes()
    c2 = rng.integers(0, 256, n, dtype=np.uint8).tobytes()
    neo = bytearray(2 * n)
    neo[0::2] = c1
    neo[1::2] = c2
    a1 = mister_crom_pair_file(c1, 64)
    a2 = mister_crom_pair_file(c2, 65)
    x1 = np.frombuffer(a1, dtype="<u2"); x2 = np.frombuffer(a2, dtype="<u2")
    comb = np.zeros(len(x1), dtype="<u2")
    comb[1::2] = x1[1::2]; comb[0::2] = x2[0::2]        # c1 owns the odd words, c2 the even ones
    check(comb.tobytes() == mister_neo_c(bytes(neo)), "MiSTer: the .neo C path (bswap + convert_dbl) equals the c1/c2 path (convert_skp, two files)")
    # the inverse mapping that coreapp's host test (tests/coreapp/test_neogeo.cc crom_addr) and neoset.cc use, against the forward transcription
    for idx in (64, 65, 80, 81):
        f = rng.integers(0, 256, 2048, dtype=np.uint8).tobytes()
        want = mister_crom_pair_file(f, idx)
        got = bytearray(len(want))
        for a in range(len(f)):
            j = a >> 1
            i = (j & ~31) | ((j & 15) << 1) | (((j >> 4) & 1) ^ 1)
            got[4 * i + 2 * ((idx & 1) ^ 1) + (a & 1)] = f[a]
        sel = np.zeros(len(want), dtype=bool)
        sel.reshape(-1, 2)[(np.arange(len(want) // 2) & 1) == ((idx ^ 1) & 1)] = True
        check(bytes(got) == want or bytes(np.frombuffer(bytes(got), dtype=np.uint8)[sel]) == bytes(np.frombuffer(want, dtype=np.uint8)[sel]), "index %d: the inverse mapping of neoset.cc/test equals loader.cpp's forward one" % idx)
    # the application's two paths agree with each other, and differ from MiSTer's
    app_xml = np.zeros(len(x1), dtype="<u2")
    y1 = np.frombuffer(app_crom_file(c1, 64), dtype="<u2"); y2 = np.frombuffer(app_crom_file(c2, 65), dtype="<u2")
    app_xml[0::2] = y1[0::2]; app_xml[1::2] = y2[1::2]
    check(app_xml.tobytes() == app_neo_c(bytes(neo)), "app: the .neo path equals the c1/c2 path")
    check(app_xml.tobytes() != comb.tobytes(), "app and MiSTer differ for C (the finding)")
    print("selftest", "PASS" if ok else "FAIL")
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    pp = sub.add_parser("png")
    pp.add_argument("files", nargs="+")
    pp.add_argument("--scale", type=int, default=2)
    po = sub.add_parser("overlay")
    po.add_argument("--roms", required=True)
    po.add_argument("--layout", required=True, choices=["mister", "app"])
    po.add_argument("--out", required=True)
    for name in ("show", "addrs", "compare", "image", "selftest"):
        p = sub.add_parser(name)
        if name != "selftest":
            p.add_argument("--roms", required=True)
            p.add_argument("--c-start", type=int, default=None)
        if name == "show":
            p.add_argument("--layout", default="both", choices=["mister", "app", "both"])
            p.add_argument("--addr", type=lambda s: int(s, 0))
            p.add_argument("--what", choices=["c", "s", "sfix", "bios", "p"])
            p.add_argument("--offset", type=lambda s: int(s, 0), default=0)
            p.add_argument("--len", type=lambda s: int(s, 0), default=64)
        if name == "image":
            p.add_argument("--layout", required=True, choices=["mister", "app"])
            p.add_argument("--out", required=True)
    a = ap.parse_args()
    if a.cmd == "selftest":
        sys.exit(cmd_selftest(a))
    if a.cmd == "png":
        return cmd_png(a)
    if a.cmd == "overlay":
        return cmd_overlay(a)
    {"show": cmd_show, "addrs": cmd_addrs, "compare": cmd_compare, "image": cmd_image}[a.cmd](a)


if __name__ == "__main__":
    main()
