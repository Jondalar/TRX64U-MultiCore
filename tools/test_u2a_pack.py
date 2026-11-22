#!/usr/bin/env python3
"""Tests of tools/u2a_pack.py (T104.1 D10): the header, the relocation table, the refusals.

The real-compiler checks use two checked-in ELF files, the same objects of app/lib/testlib linked at 0 and at 0x1000 by a real
riscv64-elf-ld (`make -C app/lib/testlib fixtures`), and a rebuild from source when that toolchain is installed. The ET_DYN /
R_RISCV_RELATIVE path has no real-linker input: no installed binutils builds -pie for a bare-metal RISC-V target. It is tested on ELF
files this test writes itself, so it proves the reader, not the linker's output.
"""
import os
import shutil
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import u2a_pack as P  # noqa: E402

FIX = os.path.join(ROOT, "tests", "loader", "fixtures")
fails = 0
checks = 0


def check(cond, what):
    global fails, checks
    checks += 1
    if not cond:
        fails += 1
        print("FAIL %s" % what)


def refused(what, fn, needle):
    """A control: the packer has to refuse, and say why."""
    global fails, checks
    checks += 1
    try:
        fn()
    except P.PackError as e:
        if needle in str(e):
            return
        fails += 1
        print("FAIL %s: refused, but the reason %r lacks %r" % (what, str(e), needle))
        return
    fails += 1
    print("FAIL %s: was not refused" % what)


def hdr(blob):
    f = struct.unpack_from("<IHHHHBBBBI16sI16sIIIIIIIIIIII", blob, 0)
    keys = ("magic header_size fmt abi_min abi_max kind res arch needs flags name version cap payload_off payload_size bss align "
            "reg main reloc_off reloc_count needs_off manifest_off manifest_size reserved").split()
    d = dict(zip(keys, f))
    d["name"] = d["name"].rstrip(b"\0").decode()
    d["cap"] = d["cap"].rstrip(b"\0").decode()
    return d


def table_of(blob):
    h = hdr(blob)
    return list(struct.unpack_from("<%dI" % h["reloc_count"], blob, h["reloc_off"]))


def image_of(blob):
    h = hdr(blob)
    return blob[h["payload_off"]:h["payload_off"] + h["payload_size"]]


def relocate(img, table, base):
    b = bytearray(img)
    for o in table:
        struct.pack_into("<I", b, o, (struct.unpack_from("<I", b, o)[0] + base) & 0xFFFFFFFF)
    return bytes(b)


def build_elf(etype, image, bss=0, relas=(), dyn=True, symbols=None, got=0, base=0):
    """A minimal RISC-V ELF32: one PT_LOAD at `base`, one .text, one .rela.dyn / .rela.text, a symbol table."""
    symbols = symbols or {}
    shstr = b"\0.text\0.rela.dyn\0.rela.text\0.symtab\0.strtab\0.shstrtab\0.got\0"
    nm = lambda n: shstr.index(b"\0" + n.encode() + b"\0") + 1  # noqa: E731
    strtab = b"\0"
    symtab = b"\0" * 16
    for name, val in symbols.items():
        symtab += struct.pack("<IIIBBH", len(strtab), val, 0, 0x12, 0, 1)
        strtab += name.encode() + b"\0"
    rela = b"".join(struct.pack("<IIi", o, (0 << 8) | t, a) for o, t, a in relas)
    off_img = 52 + 32
    body = bytes(image)
    pos = off_img + len(body)
    secs = []   # (name, type, flags, addr, off, size, link, info, entsize, data)
    blobs = []

    def put(data, align=4):
        nonlocal pos
        pos += -pos % align
        o = pos
        blobs.append((o, data))
        pos += len(data)
        return o
    rela_name = ".rela.dyn" if dyn else ".rela.text"
    o_rela = put(rela)
    o_sym = put(symtab)
    o_str = put(strtab, 1)
    o_shs = put(shstr, 1)
    sh = [(0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
          (nm(".text"), 1, 6, base, off_img, len(body), 0, 0, 0, 0),
          (nm(rela_name), 4, 2 if dyn else 0x40, 0, o_rela, len(rela), 3, 1, 4, 12),
          (nm(".symtab"), 2, 0, 0, o_sym, len(symtab), 4, 1, 4, 16),
          (nm(".strtab"), 3, 0, 0, o_str, len(strtab), 0, 0, 1, 0),
          (nm(".shstrtab"), 3, 0, 0, o_shs, len(shstr), 0, 0, 1, 0)]
    if got:
        sh.append((nm(".got"), 1, 3, base, off_img, got, 0, 0, 4, 0))
    pos += -pos % 4
    shoff = pos
    out = bytearray(shoff + 40 * len(sh))
    out[0:16] = b"\x7fELF\x01\x01\x01" + b"\0" * 9
    struct.pack_into("<HHIIIIIHHHHHH", out, 16, etype, 243, 1, 0, 52, shoff, 0, 52, 32, 1, 40, len(sh), 5)
    struct.pack_into("<IIIIIIII", out, 52, 1, off_img, base, base, len(body), len(body) + bss, 7, 4)
    out[off_img:off_img + len(body)] = body
    for o, data in blobs:
        out[o:o + len(data)] = data
    for i, s in enumerate(sh):
        struct.pack_into("<IIIIIIIIII", out, shoff + 40 * i, *s)
    return bytes(out)


def pk(elf, **kw):
    a = dict(kind="lib", name="lib1", version=1 << 16)
    a.update(kw)
    return P.pack(elf, **a)


def real_fixtures():
    a = open(os.path.join(FIX, "testlib.at0.elf"), "rb").read()
    b = open(os.path.join(FIX, "testlib.at1000.elf"), "rb").read()
    needs = [("base", P.parse_version("3.0"))]
    ba, ia, ta = P.pack(a, "lib", "testlib", 1 << 16, needs)
    bb, ib, tb = P.pack(b, "lib", "testlib", 1 << 16, needs, link_base=0x1000)
    h = hdr(ba)
    check(h["magic"] == P.MAGIC and h["header_size"] == 104 and h["fmt"] == 3, "header magic, size, format")
    check(h["kind"] == 1 and h["res"] == 1 and h["arch"] == 2 and h["name"] == "testlib" and h["version"] == 1 << 16, "header identity")
    check(h["abi_min"] == 3 and h["abi_max"] == 3 and h["needs"] == 1 and h["cap"] == "" and h["main"] == P.NONE, "header abi, needs")
    check(h["reg"] == 0 and h["reloc_count"] == 6, "lib_register is at offset 0 and six words are listed")
    check(ta == [0xb4, 0xb8, 0xc0, 0xc4, 0xc8, 0xcc], "the table holds the six R_RISCV_32 words of the data.rel.ro section: %s" % ta)
    check(table_of(ba) == ta and image_of(ba) == ia, "the file holds the table and the image it was built from")
    check(struct.unpack_from("<20s", ba, h["needs_off"])[0][:4] == b"base", "the dependency is in the file")
    check(ba == bb, "the same objects linked at 0 and at 0x1000 pack to the same bytes")
    # the proof the table is complete: the image at 0, relocated by 0x1000, is what the linker made at 0x1000
    raw = P.build_image(P.Elf(b), 0x1000)[0]
    check(relocate(ia, ta, 0x1000) == bytes(raw), "relocating the image of the link at 0 by 0x1000 gives the link at 0x1000, byte for byte")
    # controls: a table that lost an entry, or has a wrong one, must not give it
    check(relocate(ia, ta[:-1], 0x1000) != bytes(raw), "control: a table without its last entry does not reproduce the link at 0x1000")
    check(relocate(ia, ta + [0x10], 0x1000) != bytes(raw), "control: a table with an extra word does not reproduce it either")
    check(relocate(ia, ta, 0x2000) != bytes(raw), "control: another base does not reproduce it")
    # the readelf view of the same file, when the tool is installed
    rd = shutil.which("riscv64-elf-readelf")
    if rd:
        txt = subprocess.run([rd, "-r", os.path.join(FIX, "testlib.at0.elf")], capture_output=True, text=True).stdout
        offs = sorted(int(l.split()[0], 16) for l in txt.splitlines() if "R_RISCV_32 " in l)
        check(offs == ta, "readelf lists the same R_RISCV_32 offsets as the table: %s" % offs)
    return ba


def rebuild_check(packed):
    cxx = shutil.which("riscv64-elf-g++")
    if not cxx:
        print("(riscv64-elf-g++ not installed: the checked-in fixtures are not compared with a fresh build of app/lib/testlib)")
        return
    if True:
        r = subprocess.run(["make", "-C", os.path.join(ROOT, "app", "lib", "testlib"), "testlib.u2a"], capture_output=True, text=True)
        out = os.path.join(ROOT, "app", "lib", "testlib", "testlib.u2a")
        ok = r.returncode == 0 and os.path.exists(out)
        check(ok, "app/lib/testlib builds with the installed toolchain: %s" % r.stderr[-200:])
        if ok:
            check(open(out, "rb").read() == packed, "a fresh build of app/lib/testlib packs to the bytes of the checked-in fixture")
        for f in ("testlib.o", "testlib.elf", "testlib.u2a"):
            p = os.path.join(ROOT, "app", "lib", "testlib", f)
            if os.path.exists(p):
                os.remove(p)


def synthetic():
    img = bytearray(64)
    struct.pack_into("<I", img, 16, 0xAAAA)          # an addend sits in the word: the packer rewrites it from the entry
    struct.pack_into("<I", img, 32, 0)
    elf = build_elf(P.ET_DYN, img, bss=16, relas=[(16, 3, 40), (32, 3, 8)], symbols={"lib_register": 4})
    blob, im, table = pk(elf)
    check(table == [16, 32], "RELATIVE entries become the table")
    check(struct.unpack_from("<II", im, 16)[0] == 40 and struct.unpack_from("<I", im, 32)[0] == 8, "RELATIVE: the addend is written into the word")
    check(hdr(blob)["bss"] == 16 and hdr(blob)["payload_size"] == 64, "bss follows the payload")
    # ET_EXEC with --emit-relocs at a base: R_RISCV_32 words are rewritten to link-at-0 form
    img2 = bytearray(64)
    struct.pack_into("<I", img2, 8, 0x2010)
    elf2 = build_elf(P.ET_EXEC, img2, relas=[(0x2008, 1, 0), (0x2000, 17, 0), (0x2000, 51, 0)], dyn=False,
                     symbols={"lib_register": 0x2000}, base=0x2000)
    blob2, im2, t2 = pk(elf2, link_base=0x2000)
    check(t2 == [8] and struct.unpack_from("<I", im2, 8)[0] == 0x10, "R_RISCV_32 at the link base: listed, rewritten to an image offset")
    # an app
    elf3 = build_elf(P.ET_DYN, bytes(64), symbols={"app_main": 8, "lib_register": 4})
    b3, _i, _t = pk(elf3, kind="app", name="mon", manifest=b'{"id":"x"}\0')
    h3 = hdr(b3)
    check(h3["kind"] == 2 and h3["res"] == 0 and h3["reg"] == 4 and h3["main"] == 8, "a transient app: app_main, and the registration of the screens it binds to (T104.3 D0)")
    check(b3[h3["manifest_off"]:h3["manifest_off"] + h3["manifest_size"]] == b'{"id":"x"}\0', "the manifest is carried unchanged")
    b4, _i, _t = pk(elf3, kind="app", name="mon", residence="resident")
    check(hdr(b4)["reg"] == 4 and hdr(b4)["res"] == 1, "a resident app keeps lib_register")
    b5, _i, _t = pk(elf3, needs=[("base", 3 << 16 | 1), ("cbm1541", 1 << 16)], capability="drive_a")
    check(hdr(b5)["needs"] == 2 and hdr(b5)["cap"] == "drive_a", "needs and capability")

    # the refusals
    refused("absolute HI20", lambda: pk(build_elf(P.ET_EXEC, bytes(64), relas=[(4, 26, 0)], dyn=False, symbols={"lib_register": 0})), "HI20")
    refused("got reloc", lambda: pk(build_elf(P.ET_EXEC, bytes(64), relas=[(4, 20, 0)], dyn=False, symbols={"lib_register": 0})), "GOT_HI20")
    refused("dyn reloc type", lambda: pk(build_elf(P.ET_DYN, bytes(64), relas=[(4, 1, 0)], symbols={"lib_register": 0})), "other than R_RISCV_RELATIVE")
    refused("relative outside payload", lambda: pk(build_elf(P.ET_DYN, bytes(64), relas=[(64, 3, 0)], symbols={"lib_register": 0})), "outside the payload")
    refused("relative points outside", lambda: pk(build_elf(P.ET_DYN, bytes(64), relas=[(8, 3, 0x5000)], symbols={"lib_register": 0})), "outside the image")
    refused("r32 points outside", lambda: pk(build_elf(P.ET_EXEC, struct.pack("<16I", *([0x9000] * 16)), relas=[(8, 1, 0)], dyn=False,
                                                       symbols={"lib_register": 0})), "not an address inside the image")
    refused("unaligned reloc", lambda: pk(build_elf(P.ET_DYN, bytes(64), relas=[(6, 3, 0)], symbols={"lib_register": 0})), "not word aligned")
    refused("got section", lambda: pk(build_elf(P.ET_DYN, bytes(64), symbols={"lib_register": 0}, got=8)), ".got")
    refused("no lib_register", lambda: pk(build_elf(P.ET_DYN, bytes(64), symbols={})), "lib_register")
    refused("odd entry", lambda: pk(build_elf(P.ET_DYN, bytes(64), symbols={"lib_register": 3})), "4-aligned")
    refused("2-aligned entry", lambda: pk(build_elf(P.ET_DYN, bytes(64), symbols={"lib_register": 2})), "4-aligned")
    refused("entry outside", lambda: pk(build_elf(P.ET_DYN, bytes(64), symbols={"lib_register": 64})), "outside the payload")
    refused("no app_main", lambda: pk(build_elf(P.ET_DYN, bytes(64), symbols={"lib_register": 0}), kind="app"), "app_main")
    refused("bad name", lambda: pk(build_elf(P.ET_DYN, bytes(64), symbols={"lib_register": 0}), name="Bad-Name"), "1..15")
    refused("long name", lambda: pk(build_elf(P.ET_DYN, bytes(64), symbols={"lib_register": 0}), name="a" * 16), "1..15")
    refused("link base", lambda: pk(build_elf(P.ET_DYN, bytes(64), symbols={"lib_register": 0}), link_base=0x100), "link base")
    refused("transient lib", lambda: pk(build_elf(P.ET_DYN, bytes(64), symbols={"lib_register": 0}), residence="transient"), "resident")
    refused("align", lambda: pk(build_elf(P.ET_DYN, bytes(64), symbols={"lib_register": 0}), align=24), "alignment")
    refused("not riscv", lambda: P.Elf(b"\x7fELF\x01\x01\x01" + b"\0" * 9 + struct.pack("<HH", 2, 3) + b"\0" * 40), "RISC-V")
    refused("not elf", lambda: P.Elf(b"hello"), "ELF")
    refused("need syntax", lambda: P.parse_need("base3"), "name>=version")


def menu_record():
    """T104.1: a library's own menu record, behind the manifest (header flag bit 0, the `reserved` word is its offset)."""
    import descrec
    elf = os.path.join(FIX, "testlib.at0.elf")
    if not os.path.exists(elf):
        return
    data = open(elf, "rb").read()
    toml = open(os.path.join(ROOT, "tests", "loader", "menu_lib.toml")).read()
    rec = descrec.compile_text(toml)
    plain = P.pack(data, "lib", "testlib", 1)[0]
    blob = P.pack(data, "lib", "testlib", 1, menu=rec)[0]
    f = struct.unpack_from("<IHHHHBBBBI16sI16sIIIIIIIIIIII", blob, 0)
    flags, off = f[9], f[24]
    check(flags == 1 and off % 4 == 0 and blob[off:] == rec, "a menu record: flag bit 0, a 4-aligned offset, the record at the end")
    check(descrec.parse_desc(blob[off:])["node"][0]["label"] == "Pulse", "the record in the file reads back")
    g = struct.unpack_from("<IHHHHBBBBI16sI16sIIIIIIIIIIII", plain, 0)
    check(g[9] == 0 and g[24] == 0 and len(blob) > len(plain), "a file without the record has flags 0 and reserved 0, as before")
    # with a manifest in front the record still starts on a word
    blob2 = P.pack(data, "lib", "testlib", 1, manifest=b"{}\0", menu=rec)[0]
    f2 = struct.unpack_from("<IHHHHBBBBI16sI16sIIIIIIIIIIII", blob2, 0)
    check(f2[24] % 4 == 0 and blob2[f2[24]:] == rec and f2[23] == 3, "the manifest keeps its size; the record follows on a word")
    # the command line refuses a route and a hide
    import tempfile
    with tempfile.TemporaryDirectory() as d:
        t = os.path.join(d, "m.toml")
        out = os.path.join(d, "x.u2a")
        for body in ('[[area]]\nkey = "F5"\n  [[area.tab]]\n  id = "a"\n  name = "A"\n  widget = "nodes"\n', 'hide = ["x"]\n'):
            open(t, "w").write(body)
            rc = P.main([os.path.join(FIX, "testlib.at0.elf"), "-o", out, "--name", "testlib", "--menu", t])
            check(rc == 1 and not os.path.exists(out), "a menu record with a route or a hide list is refused")
        open(t, "w").write(toml)
        check(P.main([os.path.join(FIX, "testlib.at0.elf"), "-o", out, "--name", "testlib", "--menu", t]) == 0 and os.path.exists(out), "--menu packs")


def budget_record():
    """T239.3 S13: --budget writes the record behind the header, sets flag 2, raises the base dependency; the packer refuses an image over its figure."""
    data = open(os.path.join(FIX, "testlib.at0.elf"), "rb").read()
    base30 = [("base", P.parse_version("3.0"))]
    plain = P.pack(data, "lib", "testlib", 1, base30)[0]
    check(hdr(plain)["flags"] == 0 and hdr(plain)["needs_off"] == P.HEADER_SIZE, "a file without a budget has flags 0 and its needs behind the header, as before")
    bud = P.parse_budget(["image=4096", "heap=0", "ops=0x1", "polls=0"])
    check(bud == {"image": 4096, "heap": 0, "ops": 1, "polls": 0}, "parse_budget: decimal, hex and zero")
    blob, img, _ = P.pack(data, "lib", "testlib", 1, base30, budget=bud)
    h = hdr(blob)
    rec = struct.unpack_from("<11I", blob, P.HEADER_SIZE)
    check(h["flags"] == 2 and h["header_size"] == 104, "flag 2 says a budget record follows the header")
    check(rec[0] == 44 and rec[1] == 4096 and rec[2] == 0 and rec[3] == 1 and rec[10] == 0, "the record: size, image, heap, ops, polls")
    check(all(v == P.BUDGET_NONE for v in rec[4:10]), "a key left out is U2A3_BUDGET_NONE")
    check(h["needs_off"] == P.HEADER_SIZE + 44 and h["payload_off"] == h["needs_off"] + 20, "the needs and the image follow the record")
    need = struct.unpack_from("<16sI", blob, h["needs_off"])
    check(need[0].rstrip(b"\0") == b"base" and need[1] == (3 << 16 | 3), "a base dependency below 3.3 is raised to it")
    check(image_of(blob) == img and image_of(plain) == image_of(blob), "the image is the same with and without the budget")
    b2 = P.pack(data, "lib", "testlib", 1, [("base", P.parse_version("3.5"))], budget=bud)[0]
    check(struct.unpack_from("<16sI", b2, hdr(b2)["needs_off"])[1] == (3 << 16 | 5), "a base dependency above 3.3 stays")
    b3 = P.pack(data, "lib", "testlib", 1, [], budget=bud)[0]
    check(hdr(b3)["needs"] == 1 and struct.unpack_from("<16sI", b3, hdr(b3)["needs_off"])[1] == (3 << 16 | 3), "without a base dependency the packer adds one")
    refused("four needs and no base", lambda: P.pack(data, "lib", "testlib", 1, [("a%d" % i, 0) for i in range(4)], budget=bud), "at most 4")
    refused("an image over its figure", lambda: P.pack(data, "lib", "testlib", 1, base30, budget={"image": 8}), "over its declared budget of 8 bytes")
    # the figure counts payload and bss: one byte less than that is refused, exactly that packs
    full = len(P.build_image(P.Elf(data), 0)[0]) + P.build_image(P.Elf(data), 0)[1]
    check(hdr(P.pack(data, "lib", "testlib", 1, base30, budget={"image": full})[0])["flags"] == 2, "an image exactly at its figure packs")
    refused("one byte under the image", lambda: P.pack(data, "lib", "testlib", 1, base30, budget={"image": full - 1}), "over its declared budget")
    for bad, why in ((["colour=3"], "key"), (["ops"], "key=number"), (["ops=x"], "no number"), (["ops=-1"], "outside"), (["ops=4294967295"], "outside"), (["ops=1", "ops=2"], "twice")):
        refused("budget " + " ".join(bad), lambda b=bad: P.parse_budget(b), why)
    import tempfile
    with tempfile.TemporaryDirectory() as d:
        out = os.path.join(d, "x.u2a")
        elf = os.path.join(FIX, "testlib.at0.elf")
        check(P.main([elf, "-o", out, "--name", "testlib", "--budget", "image=8"]) == 1 and not os.path.exists(out), "--budget image=8 is refused on the command line and writes no file")
        check(P.main([elf, "-o", out, "--name", "testlib", "--budget", "image=4096", "--budget", "ops=1"]) == 0 and hdr(open(out, "rb").read())["flags"] == 2, "--budget packs")


def main():
    packed = real_fixtures()
    menu_record()
    budget_record()
    rebuild_check(packed)
    synthetic()
    print("test_u2a_pack: %d checks, %d failed" % (checks, fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
