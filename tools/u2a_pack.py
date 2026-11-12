#!/usr/bin/env python3
"""Packs a linked RISC-V image into a .u2a of format 3 (T104.1 D10; layout in coreapp/src/libfmt.h).

The ELF is read directly (no binutils, no pyelftools): the loadable segments become the flat image, the relocations become the table.
Two ways of getting position-independent words are understood:

  * ET_EXEC linked at the link base (default 0) with `ld --emit-relocs` (app/lib/lib.mk): every R_RISCV_32 in an allocated section is
    an absolute address that is already right for the link base, and goes into the table. The word must point into the image.
  * ET_DYN with a .rela.dyn of R_RISCV_RELATIVE (a real -static-pie): the packer writes the addend into the word and lists it, so the
    loader's "word += base" is the whole relocation.

Everything else that would need a fix-up at run time is refused by name: absolute HI20/LO12 pairs, GOT and PLT entries, TLS, 64-bit.

    u2a_pack.py image.elf -o x.u2a --kind lib --name cbm1541 --version 1 --needs base>=3.1 --capability drive_a
    u2a_pack.py image.elf --dump-image image.bin     # only the flat image, for tests
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import descrec  # noqa: E402

MAGIC = 0x50413255
HEADER_SIZE = 104
NEED_SIZE = 20
NONE = 0xFFFFFFFF
NAME_MAX = 16
IMAGE_MAX = 0x100000

EM_RISCV = 243
ET_EXEC, ET_DYN = 2, 3
PT_LOAD = 1
SHT_SYMTAB, SHT_RELA, SHT_DYNSYM = 2, 4, 11
SHF_ALLOC = 2
SHN_UNDEF = 0

R_NONE, R_32, R_RELATIVE = 0, 1, 3
# types that need nothing at run time: pc-relative, label differences, linker hints
R_OK = {16: "BRANCH", 17: "JAL", 18: "CALL", 19: "CALL_PLT", 23: "PCREL_HI20", 24: "PCREL_LO12_I", 25: "PCREL_LO12_S",
        33: "ADD8", 34: "ADD16", 35: "ADD32", 36: "ADD64", 37: "SUB8", 38: "SUB16", 39: "SUB32", 40: "SUB64",
        43: "ALIGN", 44: "RVC_BRANCH", 45: "RVC_JUMP", 51: "RELAX", 52: "SUB6", 53: "SET6", 54: "SET8", 55: "SET16",
        56: "SET32", 57: "32_PCREL", 60: "SET_ULEB128", 61: "SUB_ULEB128"}
R_NAMES = {2: "64", 20: "GOT_HI20", 21: "TLS_GOT_HI20", 22: "TLS_GD_HI20", 26: "HI20", 27: "LO12_I", 28: "LO12_S", 59: "PLT32"}


class PackError(Exception):
    pass


def rname(t):
    return "R_RISCV_" + R_OK.get(t) if t in R_OK else "R_RISCV_" + R_NAMES.get(t, str(t))


class Elf:
    def __init__(self, data):
        self.d = data
        if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
            raise PackError("not a little-endian ELF32 file")
        (self.type, machine, _v, self.entry, self.phoff, self.shoff, _fl, _eh, self.phentsize, self.phnum, self.shentsize,
         self.shnum, self.shstrndx) = struct.unpack_from("<HHIIIIIHHHHHH", data, 16)
        if machine != EM_RISCV:
            raise PackError("not a RISC-V ELF (machine %d)" % machine)
        if self.type not in (ET_EXEC, ET_DYN):
            raise PackError("neither ET_EXEC nor ET_DYN (type %d)" % self.type)
        self.segments = []
        for i in range(self.phnum):
            p_type, off, vaddr, _pa, filesz, memsz, _fl, _al = struct.unpack_from("<IIIIIIII", data, self.phoff + i * self.phentsize)
            if p_type == PT_LOAD:
                self.segments.append((vaddr, off, filesz, memsz))
        self.sections = []
        for i in range(self.shnum):
            n, t, fl, addr, off, size, link, info, _al, entsize = struct.unpack_from("<IIIIIIIIII", data, self.shoff + i * self.shentsize)
            self.sections.append(dict(name_off=n, type=t, flags=fl, addr=addr, off=off, size=size, link=link, info=info, entsize=entsize))
        if self.shnum:
            stroff = self.sections[self.shstrndx]["off"]
            for s in self.sections:
                s["name"] = self.cstr(stroff + s["name_off"])

    def cstr(self, off):
        e = self.d.index(b"\0", off)
        return self.d[off:e].decode("ascii", "replace")

    def symbols(self):
        out = {}
        for s in self.sections:
            if s["type"] in (SHT_SYMTAB, SHT_DYNSYM):
                strtab = self.sections[s["link"]]["off"]
                for k in range(s["size"] // 16):
                    name, value, size, info, other, shndx = struct.unpack_from("<IIIBBH", self.d, s["off"] + k * 16)
                    if shndx != SHN_UNDEF and name:
                        out.setdefault(self.cstr(strtab + name), value)
        return out

    def relocs(self, sec):
        for k in range(sec["size"] // 12):
            off, info, add = struct.unpack_from("<IIi", self.d, sec["off"] + k * 12)
            yield off, info & 0xFF, info >> 8, add


def build_image(elf, link_base):
    """The flat image (payload bytes) and the bss size, from the PT_LOAD segments; addresses become offsets from link_base."""
    if not elf.segments:
        raise PackError("no loadable segment")
    segs = sorted(elf.segments)
    lo = segs[0][0]
    if lo != link_base:
        raise PackError("the image starts at 0x%X, expected the link base 0x%X (--link-base)" % (lo, link_base))
    end_file = max(v + f for v, _o, f, _m in segs)
    end_mem = max(v + m for v, _o, _f, m in segs)
    # only the tail may be bss: a segment with memsz > filesz must end the image
    for v, _o, f, m in segs:
        if m > f and v + m != end_mem:
            raise PackError("bss in the middle of the image")
    img = bytearray(end_file - lo)
    for v, o, f, _m in segs:
        img[v - lo:v - lo + f] = elf.d[o:o + f]
    bss = end_mem - end_file
    if len(img) == 0 or len(img) + bss > IMAGE_MAX:
        raise PackError("the image is empty or larger than %d bytes" % IMAGE_MAX)
    return img, bss


def collect(elf, img, bss, link_base):
    """The relocation table (sorted image offsets). Rewrites img in place for RELATIVE entries."""
    table = set()
    total = len(img) + bss
    syms = elf.symbols()
    names = {v: k for k, v in syms.items()}

    def where(o):
        return "offset 0x%X" % o

    for sec in elf.sections:
        if sec["type"] != SHT_RELA:
            continue
        is_dyn = sec["name"] == ".rela.dyn" or (elf.type == ET_DYN and sec["flags"] & SHF_ALLOC)
        if not is_dyn:
            # an --emit-relocs section applies to the section in sh_info; only allocated ones are part of the image
            tgt = elf.sections[sec["info"]] if sec["info"] < len(elf.sections) else None
            if tgt is None or not tgt["flags"] & SHF_ALLOC:
                continue
        for off, typ, symi, add in elf.relocs(sec):
            o = off - link_base
            if typ == R_NONE:
                continue
            if is_dyn:
                if typ != R_RELATIVE:
                    raise PackError("%s at %s: a dynamic relocation other than R_RISCV_RELATIVE" % (rname(typ), where(o)))
                if o < 0 or o + 4 > len(img) or o & 3:
                    raise PackError("R_RISCV_RELATIVE at %s lies outside the payload or is not word aligned" % where(o))
                value = add - link_base
                if value < 0 or value > total:
                    raise PackError("R_RISCV_RELATIVE at %s points to 0x%X, outside the image" % (where(o), add))
                struct.pack_into("<I", img, o, value)
                table.add(o)
            elif typ == R_32:
                if o < 0 or o + 4 > len(img) or o & 3:
                    raise PackError("R_RISCV_32 at %s lies outside the payload or is not word aligned" % where(o))
                value = struct.unpack_from("<I", img, o)[0] - link_base
                if value < 0 or value > total:
                    raise PackError("R_RISCV_32 at %s holds 0x%X, which is not an address inside the image (link base 0x%X)"
                                    % (where(o), value + link_base, link_base))
                struct.pack_into("<I", img, o, value)         # the image is stored linked at 0
                table.add(o)
            elif typ in R_OK:
                continue
            else:
                raise PackError("%s at %s needs a fix-up the loader does not do (%s): build with -fPIE -mcmodel=medany -mno-relax and no "
                                "GOT" % (rname(typ), where(o), names.get(o, "?")))
    for s in elf.sections:
        if s["flags"] & SHF_ALLOC and s["size"] and s["name"] in (".got", ".got.plt", ".plt", ".dynamic"):
            raise PackError("the image has a non-empty %s: a global the code reaches through a table the loader does not fill" % s["name"])
    return sorted(table)


def parse_version(text):
    major, _, minor = text.partition(".")
    return (int(major) << 16) | int(minor or 0)


def parse_need(text):
    for op in (">=", "="):
        if op in text:
            n, v = text.split(op, 1)
            return n.strip(), parse_version(v.strip())
    raise PackError("a dependency reads name>=version, got %r" % text)


def field(text, what, may_be_empty=False):
    b = text.encode("ascii")
    ok = all(c in b"abcdefghijklmnopqrstuvwxyz0123456789_" for c in b)
    if not ok or len(b) >= NAME_MAX or (not b and not may_be_empty):
        raise PackError("%s %r: 1..15 characters of [a-z0-9_]" % (what, text))
    return b.ljust(NAME_MAX, b"\0")


def pack(elf_bytes, kind, name, version, needs=(), capability="", residence=None, align=16, link_base=0, manifest=b"",
         register_sym="lib_register", main_sym="app_main", abi=(3, 3), menu=b""):
    elf = Elf(elf_bytes)
    img, bss = build_image(elf, link_base)
    table = collect(elf, img, bss, link_base)
    syms = elf.symbols()

    def entry(sym):
        if sym not in syms:
            return NONE
        o = syms[sym] - link_base
        if o < 0 or o >= len(img) or o & 3:
            raise PackError("%s at 0x%X lies outside the payload or is not 4-aligned (the CPU has no C extension)" % (sym, syms[sym]))
        return o

    reg, main = entry(register_sym), entry(main_sym)
    if kind == "lib":
        if reg == NONE:
            raise PackError("a library needs a lib_register symbol")
        main = NONE
        residence = residence or "resident"
        if residence != "resident":
            raise PackError("a library is resident")
    else:
        if main == NONE:
            raise PackError("an app needs an app_main symbol")
        residence = residence or "transient"          # an app with screens (T104.3 D0) has lib_register too: it registers what they bind to while it is open
    if len(needs) > 4:
        raise PackError("at most 4 dependencies")
    if align < 4 or align > 4096 or align & (align - 1):
        raise PackError("alignment: a power of two from 4 to 4096")

    def pad4(b):
        return b + b"\0" * (-len(b) % 4)

    needs_blob = b"".join(field(n, "dependency name") + struct.pack("<I", v) for n, v in needs)
    img_blob = pad4(bytes(img))
    reloc_blob = b"".join(struct.pack("<I", o) for o in table)
    needs_off = HEADER_SIZE
    payload_off = needs_off + len(needs_blob)
    reloc_off = payload_off + len(img_blob)
    manifest_off = reloc_off + len(reloc_blob) if manifest else 0
    menu_off, menu_pad = 0, b""
    if menu:
        # T104.1 (a library's own menu record): behind the manifest, 4-aligned; header flag bit 0 and the `reserved` word (its file offset)
        end = reloc_off + len(reloc_blob) + len(manifest)
        menu_pad = b"\0" * (-end % 4)
        menu_off = end + len(menu_pad)
    hdr = struct.pack("<IHHHHBBBBI16sI16sIIIIIIIIIIII", MAGIC, HEADER_SIZE, 3, abi[0], abi[1],
                      1 if kind == "lib" else 2, 1 if residence == "resident" else 0, 2, len(needs), 1 if menu else 0,
                      field(name, "name"), version, field(capability, "capability", True),
                      payload_off, len(img), bss, align, reg, main, reloc_off, len(table), needs_off, manifest_off, len(manifest), menu_off)
    assert len(hdr) == HEADER_SIZE, len(hdr)
    return hdr + needs_blob + img_blob + reloc_blob + manifest + menu_pad + menu, bytes(img), table


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("elf")
    ap.add_argument("-o", "--out")
    ap.add_argument("--dump-image", help="write only the flat image (linked at 0 form) here")
    ap.add_argument("--raw-image", help="write the flat image exactly as linked (for a test that compares two link bases)")
    ap.add_argument("--kind", choices=("lib", "app"), default="lib")
    ap.add_argument("--residence", choices=("resident", "transient"))
    ap.add_argument("--name")
    ap.add_argument("--version", default="1")
    ap.add_argument("--needs", action="append", default=[], help="name>=major[.minor], e.g. base>=3.1")
    ap.add_argument("--capability", default="")
    ap.add_argument("--align", type=int, default=16)
    ap.add_argument("--link-base", type=lambda s: int(s, 0), default=0)
    ap.add_argument("--manifest", help="JSON manifest of an app, carried unchanged")
    ap.add_argument("--menu", help="the nodes, file actions, settings, tabs and poll of this file as TOML (tools/descrec.py): carried as a menu record")
    a = ap.parse_args(argv)
    try:
        data = open(a.elf, "rb").read()
        manifest = open(a.manifest, "rb").read() + b"\0" if a.manifest else b""
        menu = b""
        if a.menu:
            doc = descrec.toml_loads(open(a.menu).read())
            if "hide" in doc or "rename" in doc or ("area" in doc and (a.kind != "app" or any(ar.get("key") != "APP" for ar in doc["area"]))):
                raise PackError("a menu record gives no route of the core's and hides or renames nothing; that is a core's description (an application's own screens are the area APP)")
            menu = descrec.compile_desc(doc)
        if a.raw_image:
            img, _bss = build_image(Elf(data), a.link_base)
            open(a.raw_image, "wb").write(bytes(img))
            if not a.out and not a.dump_image:
                return 0
        if a.dump_image and not a.out:
            elf = Elf(data)
            img, bss = build_image(elf, a.link_base)
            collect(elf, img, bss, a.link_base)
            open(a.dump_image, "wb").write(bytes(img))
            return 0
        if not a.name or not a.out:
            ap.error("--name and -o are required")
        blob, img, table = pack(data, a.kind, a.name, parse_version(a.version), [parse_need(n) for n in a.needs], a.capability,
                                a.residence, a.align, a.link_base, manifest, menu=menu)
        open(a.out, "wb").write(blob)
        if a.dump_image:
            open(a.dump_image, "wb").write(img)
        print("%s: %s %s, image %d bytes, %d relocation(s), %d byte file" % (a.out, a.kind, a.name, len(img), len(table), len(blob)))
        return 0
    except (PackError, OSError, ValueError, struct.error, descrec.DescError) as e:
        print("u2a_pack: %s" % e, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
