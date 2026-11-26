#!/usr/bin/env python3
"""Tests of tools/u2a_pack.py (T104.1 D10): the header, the relocation table, the refusals.

The real-compiler checks use two checked-in ELF files, the same objects of app/lib/testlib linked at 0 and at 0x1000 by a real
riscv64-elf-ld (`make -C app/lib/testlib fixtures`), and a rebuild from source when that toolchain is installed. The ET_DYN /
R_RISCV_RELATIVE path has no real-linker input: no installed binutils builds -pie for a bare-metal RISC-V target. It is tested on ELF
files this test writes itself, so it proves the reader, not the linker's output.
"""
import contextlib
import io
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


def build_elf(etype, image, bss=0, relas=(), dyn=True, symbols=None, got=0, base=0, syms=None, nlocal=0, extra=(), extra_relas=()):
    """A minimal RISC-V ELF32: one PT_LOAD at `base`, one .text, one .rela.dyn / .rela.text, a symbol table.

    syms: a symbol table of (name, value, size, info, shndx) behind the null symbol, locals first (nlocal of them), instead of `symbols`.
    extra: more allocated sections, (name, size); extra_relas: relocation sections of a section by name, (section, [(offset, type, addend, symbol index)])."""
    symbols = symbols or {}
    names = [".text", ".rela.dyn", ".rela.text", ".symtab", ".strtab", ".shstrtab", ".got"] + [n for n, _sz in extra] + [".rela" + sec for sec, _r in extra_relas]
    shstr = b"\0" + b"".join(n.encode() + b"\0" for n in names)
    nm = lambda n: shstr.index(b"\0" + n.encode() + b"\0") + 1  # noqa: E731
    strtab = b"\0"
    symtab = b"\0" * 16
    if syms is None:
        syms = [(name, val, 0, 0x12, 1) for name, val in symbols.items()]
        nlocal = 0
    for name, val, size, info, shndx in syms:
        symtab += struct.pack("<IIIBBH", len(strtab) if name else 0, val, size, info, 0, shndx)
        strtab += (name.encode() + b"\0") if name else b""
    rela = b"".join(struct.pack("<IIi", o, (0 << 8) | t, a) for o, t, a in relas)
    off_img = 52 + 32
    body = bytes(image)
    pos = off_img + len(body)
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
          (nm(".symtab"), 2, 0, 0, o_sym, len(symtab), 4, 1 + nlocal, 4, 16),
          (nm(".strtab"), 3, 0, 0, o_str, len(strtab), 0, 0, 1, 0),
          (nm(".shstrtab"), 3, 0, 0, o_shs, len(shstr), 0, 0, 1, 0)]
    if got:
        sh.append((nm(".got"), 1, 3, base, off_img, got, 0, 0, 4, 0))
    for n, size in extra:
        sh.append((nm(n), 1, 3, base, off_img, size, 0, 0, 4, 0))
    for sec, rl in extra_relas:
        data = b"".join(struct.pack("<IIi", o, (si << 8) | t, a) for o, t, a, si in rl)
        target = 1 if sec == ".text" else [n for n, _sz in extra].index(sec) + (7 if got else 6)
        sh.append((nm(".rela" + sec), 4, 0x40, 0, put(data), len(data), 3, target, 4, 12))
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
    check(h["abi_min"] == 4 and h["abi_max"] == 4 and h["needs"] == 1 and h["cap"] == "" and h["main"] == P.NONE, "header abi, needs")
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


def no_constructors():
    """D19: the loader runs no constructor and no destructor; the packer refuses what would hold one, naming the section or symbol and, where a
    relocation tells it, the function and the object."""
    FILE, FUNC_L, FUNC_G, UND = 0x04, 0x02, 0x12, 0x10
    ABS = 0xFFF1
    plain = [("lib_register", 0, 0, FUNC_G, 1)]
    ok = pk(build_elf(P.ET_EXEC, bytes(64), dyn=False, syms=plain))
    check(ok[0][:4] == struct.pack("<I", P.MAGIC), "a library with no constructor and no atexit is packed")
    check(pk(build_elf(P.ET_EXEC, bytes(64), dyn=False, syms=plain, extra=[(".init_array", 0)]))[2] == [], "an empty .init_array is no constructor")
    for sec in (".init_array", ".fini_array", ".ctors", ".dtors", ".init_array.00100"):
        refused("section " + sec, lambda sec=sec: pk(build_elf(P.ET_EXEC, bytes(64), dyn=False, syms=plain, extra=[(sec, 4)])), sec)
        refused("section " + sec + " says D19", lambda sec=sec: pk(build_elf(P.ET_EXEC, bytes(64), dyn=False, syms=plain, extra=[(sec, 4)])), "no constructor or destructor (D19)")
    # the relocation of the entry names the function, and a local one the object it came from
    objsyms = [("ctor_obj.o", 0, 0, FILE, ABS), ("_GLOBAL__sub_I_cache", 8, 12, FUNC_L, 1), ("lib_register", 0, 0, FUNC_G, 1)]
    elf = build_elf(P.ET_EXEC, bytes(64), dyn=False, syms=objsyms, nlocal=2, extra=[(".init_array", 4)], extra_relas=[(".init_array", [(0, 1, 0, 2)])])
    refused("init_array names the function", lambda: pk(elf), "_GLOBAL__sub_I_cache")
    refused("init_array names the object", lambda: pk(elf), "from ctor_obj.o")
    elf = build_elf(P.ET_EXEC, bytes(64), dyn=False, syms=[("lib_register", 0, 0, FUNC_G, 1), ("init_global", 8, 12, FUNC_G, 1)], extra=[(".init_array", 4)],
                    extra_relas=[(".init_array", [(0, 1, 0, 2)])])
    refused("a global function has no object to name", lambda: pk(elf), "it holds init_global)")
    # an undefined atexit or __cxa_atexit: the call names the function and the object
    for sym in ("__cxa_atexit", "atexit"):
        syms = [("dtor_obj.o", 0, 0, FILE, ABS), ("__static_initialization", 8, 16, FUNC_L, 1), ("lib_register", 0, 0, FUNC_G, 1), (sym, 0, 0, UND, 0)]
        elf = build_elf(P.ET_EXEC, bytes(64), dyn=False, syms=syms, nlocal=2, extra_relas=[(".text", [(12, 18, 0, 4)])])
        refused(sym, lambda elf=elf: pk(elf), sym)
        refused(sym + " names the caller", lambda elf=elf: pk(elf), "called from __static_initialization in dtor_obj.o")
        refused(sym + " says D19", lambda elf=elf: pk(elf), "no constructor or destructor (D19)")
        # the other tool paths read the same check: --dump-image refuses it as well
        import tempfile
        with tempfile.TemporaryDirectory() as d:
            e, out = os.path.join(d, "x.elf"), os.path.join(d, "x.bin")
            open(e, "wb").write(elf)
            check(P.main([e, "--dump-image", out]) == 1 and not os.path.exists(out), sym + " is refused on the command line too, and nothing is written")
    # the check comes before build_image: an image that build_image refuses (empty) and holds a constructor says D19, not "empty"
    refused("D19 before the image rules", lambda: pk(build_elf(P.ET_EXEC, b"", dyn=False, syms=plain, extra=[(".init_array", 4)])), "(D19)")
    # --check-objects: the same check on relocatable objects, in front of ld (an object is ET_REL, which the image reader refuses)
    import tempfile
    with tempfile.TemporaryDirectory() as d:
        rel = lambda elf: elf[:16] + struct.pack("<H", P.ET_REL) + elf[18:]  # noqa: E731
        good, bad, ctor = (os.path.join(d, n) for n in ("good.o", "bad.o", "ctor.o"))
        open(good, "wb").write(rel(build_elf(P.ET_EXEC, bytes(64), dyn=False, syms=plain)))
        open(bad, "wb").write(rel(build_elf(P.ET_EXEC, bytes(64), dyn=False, extra_relas=[(".text", [(12, 18, 0, 4)])],
                                            syms=[("dtor_obj.o", 0, 0, FILE, ABS), ("__static_init", 8, 16, FUNC_L, 1), ("lib_register", 0, 0, FUNC_G, 1), ("__cxa_atexit", 0, 0, UND, 0)], nlocal=2)))
        open(ctor, "wb").write(rel(build_elf(P.ET_EXEC, bytes(64), dyn=False, syms=plain, extra=[(".init_array", 4)])))
        check(P.main(["--check-objects", good]) == 0, "--check-objects passes an object with no constructor")
        for o, needle in ((bad, "__cxa_atexit"), (ctor, ".init_array")):
            err = io.StringIO()
            with contextlib.redirect_stderr(err):
                rc = P.main(["--check-objects", good, o])
            check(rc == 1 and needle in err.getvalue() and os.path.basename(o) in err.getvalue() and "(D19)" in err.getvalue(),
                  "--check-objects refuses %s, naming the file and %s: %s" % (os.path.basename(o), needle, err.getvalue()))
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            rc = P.main(["--check-objects", os.path.join(FIX, "testlib.at0.elf")])
        check(rc == 1 and "relocatable" in err.getvalue(), "--check-objects takes objects, not a linked image: %s" % err.getvalue())
    # a defined atexit (a libc that brings it) or another undefined symbol is not this refusal
    check(pk(build_elf(P.ET_EXEC, bytes(64), dyn=False, syms=plain + [("atexit", 4, 4, FUNC_G, 1)]))[0][:4] == struct.pack("<I", P.MAGIC), "a defined atexit is not refused here")


def real_constructors():
    """D19 with the real compiler and linker (tests/loader/d19): a global with a constructor and one with a destructor, built through app/lib/lib.mk. The build
    must stop in the packer's check of the objects, with the D19 message; a destructor must not reach ld, which would stop on the undefined __cxa_atexit."""
    prefix = next((p for p in ("riscv32-unknown-elf-", "riscv64-elf-") if shutil.which(p + "g++")), None)
    if not prefix:
        print("(no RISC-V g++ installed: the D19 fixtures of tests/loader/d19 are not built)")
        return
    d19 = os.path.join(ROOT, "tests", "loader", "d19")
    try:
        for src, needle in (("ctor.cc", "_GLOBAL__sub_I"), ("dtor.cc", "__cxa_atexit")):
            r = subprocess.run(["make", "-C", d19, "SRC=" + src, "RV_PREFIX=" + prefix], capture_output=True, text=True)
            check(r.returncode != 0 and "no constructor or destructor (D19)" in r.stderr and needle in r.stderr and "undefined reference" not in r.stderr,
                  "lib.mk stops on %s in the packer's D19 check, not in ld: rc %d, %s" % (src, r.returncode, r.stderr[-300:]))
            check(not os.path.exists(os.path.join(d19, "d19.elf")) and not os.path.exists(os.path.join(d19, "d19.u2a")), "%s leaves no image behind" % src)
            subprocess.run(["make", "-C", d19, "SRC=" + src, "clean"], capture_output=True)
    finally:
        subprocess.run(["make", "-C", d19, "clean"], capture_output=True)


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
        # T262 D4 finding 11: a library's record carries [[key]] rows; windows and an application's key table stay a core's
        keyrow = '[[key]]\nnode = "lib"\nkey = "x"\nop = "test.echo"\nlegend = "Echo"\n'
        open(t, "w").write(toml + keyrow)
        os.path.exists(out) and os.remove(out)
        check(P.main([os.path.join(FIX, "testlib.at0.elf"), "-o", out, "--name", "testlib", "--menu", t]) == 0 and os.path.exists(out), "a library's menu record with a [[key]] row packs")
        os.remove(out)
        check(P.main([os.path.join(FIX, "testlib.at0.elf"), "-o", out, "--name", "testlib", "--kind", "app", "--menu", t]) == 1 and not os.path.exists(out), "an application's menu record with a key table is refused")
        open(t, "w").write('[[window]]\nname = "video"\naddress = 0xA0000A00\nsize = 0x14\n')
        check(P.main([os.path.join(FIX, "testlib.at0.elf"), "-o", out, "--name", "testlib", "--menu", t]) == 1 and not os.path.exists(out), "a menu record with windows is refused")


def budget_record():
    """T239.3 S13: --budget writes the record behind the header, sets flag 2, raises the base dependency; the packer refuses an image over its figure."""
    data = open(os.path.join(FIX, "testlib.at0.elf"), "rb").read()
    base30 = [("base", P.parse_version("3.0"))]
    plain = P.pack(data, "lib", "testlib", 1, base30)[0]
    check(hdr(plain)["flags"] == 0 and hdr(plain)["needs_off"] == P.HEADER_SIZE, "a file without a budget has flags 0 and its needs behind the header, as before")
    bud = P.parse_budget(["image=4096", "heap_keep=0", "heap_peak=0", "ops=0x1", "polls=0"])
    check(bud == {"image": 4096, "heap_keep": 0, "heap_peak": 0, "ops": 1, "polls": 0}, "parse_budget: decimal, hex and zero")
    blob, img, _ = P.pack(data, "lib", "testlib", 1, base30, budget=bud)
    h = hdr(blob)
    rec = struct.unpack_from("<12I", blob, P.HEADER_SIZE)
    check(h["flags"] == 2 and h["header_size"] == 104, "flag 2 says a budget record follows the header")
    check(rec[0] == 48 and rec[1] == 4096 and rec[2] == 0 and rec[3] == 0 and rec[4] == 1 and rec[11] == 0, "the record: size, image, heap_keep, heap_peak, ops, polls")
    check(all(v == P.BUDGET_NONE for v in rec[5:11]), "a key left out is U2A3_BUDGET_NONE")
    check(h["needs_off"] == P.HEADER_SIZE + 48 and h["payload_off"] == h["needs_off"] + 20, "the needs and the image follow the record")
    need = struct.unpack_from("<16sI", blob, h["needs_off"])
    check(need[0].rstrip(b"\0") == b"base" and need[1] == (3 << 16 | 2), "a base dependency below 3.2 is raised to it")
    check(image_of(blob) == img and image_of(plain) == image_of(blob), "the image is the same with and without the budget")
    b2 = P.pack(data, "lib", "testlib", 1, [("base", P.parse_version("3.5"))], budget=bud)[0]
    check(struct.unpack_from("<16sI", b2, hdr(b2)["needs_off"])[1] == (3 << 16 | 5), "a base dependency above 3.2 stays")
    b3 = P.pack(data, "lib", "testlib", 1, [], budget=bud)[0]
    check(hdr(b3)["needs"] == 1 and struct.unpack_from("<16sI", b3, hdr(b3)["needs_off"])[1] == (3 << 16 | 2), "without a base dependency the packer adds one")
    refused("four needs and no base", lambda: P.pack(data, "lib", "testlib", 1, [("a%d" % i, 0) for i in range(4)], budget=bud), "at most 4")
    refused("a keep over the peak", lambda: P.parse_budget(["heap_keep=100", "heap_peak=99"]), "keeps 100 bytes of heap and allows a peak of 99")
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


def exports_record():
    """T239.3 D20: the names a library exports stand in the file (header flag 4, a count, then name and major), behind the budget record when there is one."""
    data = open(os.path.join(FIX, "testlib.at0.elf"), "rb").read()
    base30 = [("base", P.parse_version("3.0"))]
    check(P.parse_export("ring=1") == ("ring", 1) and P.parse_export("trx>=2") == ("trx", 2), "parse_export: name=major and name>=major")
    blob = P.pack(data, "lib", "testlib", 1, base30, exports=[("ring", 1), ("trx", 2)])[0]
    h = hdr(blob)
    check(h["flags"] == 4 and h["header_size"] == 104, "flag 4 says a list of exported names follows the header")
    count = struct.unpack_from("<I", blob, P.HEADER_SIZE)[0]
    e0 = struct.unpack_from("<16sI", blob, P.HEADER_SIZE + 4)
    e1 = struct.unpack_from("<16sI", blob, P.HEADER_SIZE + 24)
    check(count == 2 and e0[0].rstrip(b"\0") == b"ring" and e0[1] == 1 and e1[0].rstrip(b"\0") == b"trx" and e1[1] == 2, "the list: the count, then name and major of each")
    check(h["needs_off"] == P.HEADER_SIZE + 4 + 40 and h["payload_off"] == h["needs_off"] + 20, "the needs and the image follow the list")
    need = struct.unpack_from("<16sI", blob, h["needs_off"])
    check(need[0].rstrip(b"\0") == b"base" and need[1] == (3 << 16 | 2), "a base dependency below 3.2 is raised to it")
    b2 = P.pack(data, "lib", "testlib", 1, [], exports=[("ring", 1)])[0]
    check(hdr(b2)["needs"] == 1 and struct.unpack_from("<16sI", b2, hdr(b2)["needs_off"])[1] == (3 << 16 | 2), "no base dependency: the packer adds base 3.2")
    # with a budget in front, the list stands behind it
    bud = P.parse_budget(["image=4096"])
    b3 = P.pack(data, "lib", "testlib", 1, base30, budget=bud, exports=[("ring", 1)])[0]
    h3 = hdr(b3)
    check(h3["flags"] == 6 and struct.unpack_from("<I", b3, P.HEADER_SIZE + 48)[0] == 1, "budget and exports: flags 6, the list behind the 48 bytes of the budget")
    check(h3["needs_off"] == P.HEADER_SIZE + 48 + 4 + 20, "the needs follow both records")
    plain = P.pack(data, "lib", "testlib", 1, base30)[0]
    check(image_of(blob) == image_of(plain) and hdr(plain)["flags"] == 0, "the image is the same with and without the list; a file without it has flags 0")
    refused("an app that exports", lambda: P.pack(data, "app", "testlib", 1, base30, exports=[("ring", 1)], main_sym="lib_register"), "only a library")
    refused("nine exports", lambda: P.pack(data, "lib", "testlib", 1, base30, exports=[("n%d" % i, 1) for i in range(9)]), "one to 8")
    refused("an export twice", lambda: P.pack(data, "lib", "testlib", 1, base30, exports=[("ring", 1), ("ring", 1)]), "twice")
    refused("an export with a capital", lambda: P.pack(data, "lib", "testlib", 1, base30, exports=[("Ring", 1)]), "1..15")
    refused("export major 0", lambda: P.parse_export("ring=0"), "outside")
    refused("export without major", lambda: P.parse_export("ring"), "name=major")
    refused("export major no number", lambda: P.parse_export("ring=x"), "no number")
    import tempfile
    with tempfile.TemporaryDirectory() as d:
        out = os.path.join(d, "x.u2a")
        elf = os.path.join(FIX, "testlib.at0.elf")
        check(P.main([elf, "-o", out, "--name", "testlib", "--export", "ring=1", "--export", "trx=2"]) == 0 and hdr(open(out, "rb").read())["flags"] == 4, "--export packs")
        check(P.main([elf, "-o", out + "2", "--name", "testlib", "--export", "ring"]) == 1 and not os.path.exists(out + "2"), "--export without a major is refused and writes no file")


def main():
    packed = real_fixtures()
    menu_record()
    budget_record()
    exports_record()
    rebuild_check(packed)
    synthetic()
    no_constructors()
    real_constructors()
    print("test_u2a_pack: %d checks, %d failed" % (checks, fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
