#!/usr/bin/env python3
"""Makes the fixtures the description and bundle tests read, with the real packer (tools/cor_pack.py, tools/descrec.py), so that the C++ readers of the
board are tested against what the tools write (T104.1 D9, D12).

    gen_bundle.py OUTDIR      writes desc_test.bin, desc_default.bin, new.cor (description + two libraries), old.cor (neither: the layer-2 bundle
                              of before), large.cor (a large core with the description), other.cor (another core's name),
                              merged.cor (the description with the menu of the library gamma merged in), desc_trx64u.bin (the menu of TRX64U),
                              diag.cor (the diag core's .cor as the packer makes it: record 8 holds cores/diag/sd, earlier versions from cores/diag/superseded),
                              nofiles.cor (the same core without record 8), app/index.txt, app/<id>.bin, app/<id>.cor, app/<id>_old.cor (the
                              data description of every cores/*/app/desc.toml, its bundle and the same bundle without the record, T239.3 D17),
                              desc_sections.bin and desc_sections_extra.bin (the tables of the sections 7 .. 17), desc_keys.bin (base 3.3: key rows, windows, job tasks), desc_toml_a.bin (P1-1: TOML A, record version 7), desc_places.bin (D32: routes and places)
"""
import glob
import os
import struct
import sys

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, "..", "..", "tools"))
import cor_pack as C  # noqa: E402
import descrec  # noqa: E402


def bit_file():
    hdr = C.BIT_MAGIC
    for key in b"abcd":
        txt = b"x" * 5 + b"\0"
        hdr += bytes([key]) + struct.pack(">H", len(txt)) + txt
    data = b"\xFF" * 8 + C.SYNC + bytes(range(256)) * 4
    return hdr + b"e" + struct.pack(">I", len(data)) + data


def u2a(name, needs=(), kind=1, version=(1, 0), menu=b""):
    """A format-3 file as small as the loader takes one: header, needs, 64 bytes of payload (tests/coreapp/test_libscan.cc build())."""
    needs_off = 104
    pay_off = needs_off + 20 * len(needs)
    reg = 0xFFFFFFFF if kind == 2 else 0
    main = 0 if kind == 2 else 0xFFFFFFFF
    menu_off = pay_off + 64 if menu else 0                     # the record follows the (empty) relocation table: payload is 64 bytes, a multiple of 4
    h = struct.pack("<IHHHHBBBBI16sI16sIIIIIIIIIIII", 0x50413255, 104, 3, 3, 3, kind, 1 if kind == 1 else 0, 2, len(needs), 1 if menu else 0,
                    name.encode(), (version[0] << 16) | version[1], b"", pay_off, 64, 16, 16, reg, main, pay_off + 64, 0, needs_off, 0, 0, menu_off)
    assert len(h) == 104, len(h)
    for n, v in needs:
        h += struct.pack("<16sI", n.encode(), v)
    return h + bytes(64) + menu


def main(out):
    os.makedirs(out, exist_ok=True)
    gold = os.path.join(here, "golden")
    desc = descrec.compile_text(open(os.path.join(gold, "desc_test.toml")).read())
    open(os.path.join(out, "desc_test.bin"), "wb").write(desc)
    # the menu of TRX64U as menu.toml compiles (T104.2 D10): the record with the core's own tables
    open(os.path.join(out, "desc_trx64u.bin"), "wb").write(descrec.compile_text(open(os.path.join(here, "..", "..", "coreapp", "cores", "trx64u", "menu.toml")).read()))
    # base 3.3: a key table, register windows and job tasks (tests/coreapp/test_deckeys.cc)
    open(os.path.join(out, "desc_keys.bin"), "wb").write(descrec.compile_text(open(os.path.join(gold, "desc_keys.toml")).read()))
    # P1-1: TOML A (record version 7), its module maps from the SDK's (tests/coreapp/test_desctoml.cc)
    open(os.path.join(out, "desc_toml_a.bin"), "wb").write(descrec.compile_checked(open(os.path.join(gold, "desc_toml_a.toml")).read(), maps=descrec.load_maps([os.path.join(here, "..", "..", "app", "sdkpkg", "maps")])))
    # D32: a core's routes and places over the base's (tests/coreapp/test_uiplace.cc)
    open(os.path.join(out, "desc_places.bin"), "wb").write(descrec.compile_text(open(os.path.join(gold, "desc_places.toml")).read()))
    open(os.path.join(out, "desc_pick.bin"), "wb").write(descrec.compile_text(open(os.path.join(gold, "desc_pick.toml")).read()))   # a core row with a pick and one value (T239.3 D32)
    open(os.path.join(out, "desc_default.bin"), "wb").write(descrec.compile_text(open(os.path.join(gold, "desc_default.toml")).read()))
    app = bytes(0x60 for _ in range(64))
    kw = dict(version="0.1", partial=bit_file(), app=app, load=0x30000, entry=0x30000, host_key="820eded2")
    menu_alpha = descrec.compile_text(open(os.path.join(gold, "menu_alpha.toml")).read())
    alpha, beta = u2a("alpha", version=(1, 2), menu=menu_alpha), u2a("beta", needs=[("alpha", (1 << 16) | 0)])
    open(os.path.join(out, "new.cor"), "wb").write(C.pack(name="Test Machine", desc=desc, libs=[beta, alpha], **kw))
    # T104.2 D10: gamma has a menu without a poll, so the packer merges it into the description; the record names it
    menu_gamma = descrec.compile_text(open(os.path.join(gold, "menu_gamma.toml")).read())
    open(os.path.join(out, "merged.cor"), "wb").write(C.pack(name="Test Machine", desc=desc, libs=[u2a("gamma", menu=menu_gamma)], **kw))
    open(os.path.join(out, "old.cor"), "wb").write(C.pack(name="Test Machine", **kw))
    open(os.path.join(out, "other.cor"), "wb").write(C.pack(name="Another Machine", desc=desc, libs=[alpha], **kw))
    # a large core: the full bitstream is whatever scan_config accepts (an IDCODE write and a sync word)
    sync = struct.pack(">I", C.AXSS_SYNC)
    idcode = struct.pack(">II", (1 << 29) | (2 << 27) | (C.REG_IDCODE << 13) | 1, C.IDCODE_XC7A50T)
    data = b"\xFF" * 8 + sync + idcode + struct.pack(">I", 0x20000000) * 20
    bit = data + bytes(-len(data) % 4)
    open(os.path.join(out, "large.cor"), "wb").write(C.pack_large("test", "Test Machine", "0.1", bit, bytes(0x60 for _ in range(64)), desc=desc, libs=[alpha]))
    # T175: the Port Diagnostics core carries its example tests as record 8 (cores/diag/core.toml install_dir / install_old)
    diag = os.path.join(here, "..", "..", "cores", "diag")
    files = C.collect_files(os.path.join(diag, "sd"), os.path.join(diag, "superseded"))
    open(os.path.join(out, "diag.cor"), "wb").write(C.pack_large("diag", "Port Diagnostics", "0.1", bit, bytes(0x60 for _ in range(64)), files=files))
    open(os.path.join(out, "nofiles.cor"), "wb").write(C.pack_large("diag", "Port Diagnostics", "0.1", bit, bytes(0x60 for _ in range(64))))
    # T239.3 D17: the data description of every core that has one (cores/*/app/desc.toml) as the packer makes it, and the bundle that carries it; index.txt names them
    app = os.path.join(out, "app")
    os.makedirs(app, exist_ok=True)
    lines = []
    for toml in sorted(glob.glob(os.path.join(here, "..", "..", "cores", "*", "app", "desc.toml"))):
        cid = os.path.basename(os.path.dirname(os.path.dirname(toml)))
        blob = descrec.compile_text(open(toml).read(), toml)
        name = descrec.parse_desc(blob)["core"]["name"]
        open(os.path.join(app, cid + ".bin"), "wb").write(blob)
        open(os.path.join(app, cid + ".cor"), "wb").write(C.pack(name=name, desc=blob, **kw))
        open(os.path.join(app, cid + "_old.cor"), "wb").write(C.pack(name=name, **kw))
        lines.append("%s %s\n" % (cid, name))
    open(os.path.join(app, "index.txt"), "w").write("".join(lines))
    # T239.3 D17 part 2: the tables of sections 7 .. 17 (tests/coreapp/test_descsec.cc): the Amiga 1200's, and what no descriptor has together
    for n in ("desc_sections", "desc_sections_extra"):
        open(os.path.join(out, n + ".bin"), "wb").write(descrec.compile_text(open(os.path.join(gold, n + ".toml")).read()))


if __name__ == "__main__":
    main(sys.argv[1])
