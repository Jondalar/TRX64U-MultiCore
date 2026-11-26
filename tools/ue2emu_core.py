#!/usr/bin/env python3
"""A core in ue2emu from the core's own files - no script names a core: its app started alone (step 12 of a core lane, tests/coreapp/emu/core_round.sh) or
under the host app (the host menu lane, tests/hostapp/emu_host.sh).

A core's emulator profile is cores/<core>/app/ue2emu.toml (the ID word and the register values its app reads at start, the hand-over page; S44 of the
emulator). The rest comes from the core's .cor (its manifest and its app) and its data description cores/<core>/app/desc.toml.

  tools/ue2emu_core.py profile <core> <cor> <dir>   writes the profile for --core-dir: <dir>/<key>.toml, the core's ue2emu.toml with symbols = "coreapp.elf" in front
                                                    (the ELF of the program the core runs once the host started it, beside the profile). <key> is the name the
                                                    emulator looks the profile up by: the manifest's id (a large core), else its name (a DFX core). Prints <key>.toml.
  tools/ue2emu_core.py facts <core> <cor> <dir>     the round host -> core -> menu -> host as shell assignments: kind (dfx|large), cname (the manifest's name), prof
                                                    (the profile's file in <dir>, named as by `profile`), ident (the profile's ID word and the description's name, as
                                                    the core app's "[core]" line says them), hold (the menu hotkey's modifier as ue2emu key names, empty when the core
                                                    has no [hotkeys]: its menu opens with the menu button), magic (a large core: its entry, the boot magic the emulator
                                                    logs), start_ms (the time the start takes)
  tools/ue2emu_core.py roms <core> <dir>            zero-filled stand-ins of the ROM files the core needs to start (no ROM is in the repository) in <dir>: each [[rom]]
                                                    that is required or part of a set, and the [romset]'s example, at their first size. Prints the files.
  tools/ue2emu_core.py direct <core> <cor>          the core's app started alone (--core with the profile, no host) as shell assignments: prof (the profile's path),
                                                    ident and hold (as for facts), start (running: the machine runs once the ROMs are in; program: the core waits for a
                                                    program, its menu opens by itself and stays open until one runs; menu_first: the menu is its screen and does not
                                                    close), stick_cor (the .cor's path on the USB stick, from the profile's [handover] path /Usb0/...), rom_dir (the
                                                    .cor's core folder's roms/ on the stick, where the app looks for ROM files). Refused: a .cor that is not the
                                                    core's, a hand-over that names another core than the .cor's manifest or a .cor that is not on the stick.
  tools/ue2emu_core.py firmware <cor> <elf> <out>   <out>: the ELF of the app's link with the bytes of the .cor's app record in its loaded segments, so the emulator
                                                    runs the .cor's own app with the link's symbols. The packer sets the heap word of the image (cor_pack.py app_heap),
                                                    so the two may differ in that word and nowhere else; refused: another load address, entry or size, a difference
                                                    outside the heap word, or a byte of the record in no segment that is not 0. Prints the record's size and SHA-256.
"""
import json
import os
import re
import shlex
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import descrec  # noqa: E402  (toml_loads: tomllib, or its subset reader on Python 3.10)
import cor_pack  # noqa: E402  (the record numbers and the heap record)

# the modifiers of the menu hotkey (coreapp/src/hotkeys.cc g_pre: K_LSHIFT 15, K_CTRL 58, K_CBM 61, K_STOP 63) by the emulator's key names (ue2emu keymap.rs);
# 255 is no key
KEYS = {15: "lshift", 58: "ctrl", 61: "cbm", 63: "runstop"}
HK_NONE = 255


class Error(Exception):
    pass


def manifest(path):
    """The manifest of a .cor as the emulator reads it (multicore.rs read_cor): "TRXCOR01", u32 length, record 1 the JSON."""
    b = open(path, "rb").read()
    if len(b) < 20 or b[:8] != b"TRXCOR01":
        raise Error("%s: not a .cor bundle" % path)
    kind, n = struct.unpack("<II", b[12:20])
    if kind != 1 or 20 + n > len(b):
        raise Error("%s: the first record is not the manifest" % path)
    m = json.loads(b[20:20 + n])
    if not isinstance(m, dict) or not m.get("name"):
        raise Error("%s: the manifest names no core" % path)
    return m


def desc_of(core):
    p = os.path.join(ROOT, "cores", core, "app", "desc.toml")
    if not os.path.isfile(p):
        raise Error("the core %s has no data description %s" % (core, os.path.relpath(p, ROOT)))
    return descrec.toml_loads(open(p).read())


def checked(core, cor):
    """The manifest of `cor`, refused when the bundle is not this core's: a large core's id is the core, a DFX core's name the description's."""
    m = manifest(cor)
    name = desc_of(core).get("core", {}).get("name", "")
    if m.get("kind") == "large" and m.get("id") != core:
        raise Error("%s is the large core \"%s\", not %s" % (cor, m.get("id"), core))
    if m.get("kind") != "large" and m["name"] != name:
        raise Error("%s is the DFX core \"%s\", not %s (\"%s\")" % (cor, m["name"], core, name))
    return m


def key_of(m):
    return m.get("id") or m["name"]


def profile(core, cor, out):
    src = os.path.join(ROOT, "cores", core, "app", "ue2emu.toml")
    if not os.path.isfile(src):
        raise Error("the core %s has no emulator profile %s" % (core, os.path.relpath(src, ROOT)))
    text = open(src).read()
    top = re.split(r"^\[", text, maxsplit=1, flags=re.M)[0]
    if re.search(r"^symbols\s*=", top, re.M):
        raise Error("%s names symbols itself; the kit puts the line in front" % os.path.relpath(src, ROOT))
    name = key_of(checked(core, cor)) + ".toml"
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, name), "w") as f:
        f.write('symbols = "coreapp.elf"\n' + text)
    print(name)


def facts(core, cor, pdir):
    m = checked(core, cor)
    name = key_of(m) + ".toml"
    prof = os.path.join(pdir, name)
    if not os.path.isfile(prof):
        raise Error("no profile %s for the core %s" % (prof, core))
    d = desc_of(core)
    large = m.get("kind") == "large"
    out = {"kind": "large" if large else "dfx", "cname": m["name"], "prof": name, "ident": ident_of(open(prof).read(), prof, d), "hold": hold_of(core, d),
           "magic": "0x%08x" % m.get("app_entry", 0) if large else "", "start_ms": "60000" if large else "30000"}
    assignments(out)


def ident_of(text, where, d):
    """the ID word of the profile's top and the description's name, as the core app's "[core]" line says them"""
    top = re.split(r"^\[", text, maxsplit=1, flags=re.M)[0]
    ident = re.search(r'^id\s*=\s*"(.{4})"', top, re.M)
    if not ident:
        raise Error("%s has no four-character id" % where)
    return "%s %s" % (ident.group(1), d["core"]["name"])


def hold_of(core, d):
    """the menu hotkey's modifier as ue2emu key names joined by +, empty when the core has no [hotkeys]"""
    mods = d.get("hotkeys", {}).get("mod", [])
    names = [KEYS.get(k) for k in mods if k != HK_NONE]
    if None in names:
        raise Error("the core %s: a hotkey modifier %s is none of %s" % (core, mods, sorted(KEYS)))
    return "+".join(names)


def assignments(out):
    for k, v in out.items():
        print("%s=%s" % (k, shlex.quote(v)))


def direct(core, cor):
    m = checked(core, cor)
    prof = os.path.join(ROOT, "cores", core, "app", "ue2emu.toml")
    if not os.path.isfile(prof):
        raise Error("the core %s has no emulator profile %s" % (core, os.path.relpath(prof, ROOT)))
    text = open(prof).read()
    sec = re.search(r"^\[handover\][ \t]*$(.*?)(?=^\[|\Z)", text, re.M | re.S)
    if not sec:
        raise Error("%s has no [handover]: the app started alone reads its .cor's path there" % os.path.relpath(prof, ROOT))
    h = descrec.toml_loads(sec.group(1))
    if h.get("core") != m["name"]:
        raise Error("%s hands over the core \"%s\", the .cor is \"%s\"" % (os.path.relpath(prof, ROOT), h.get("core"), m["name"]))
    on_stick = re.match(r"^/usb0/(.+\.cor)$", h.get("path", ""), re.I)
    if not on_stick or ".." in on_stick.group(1).split("/"):
        raise Error("%s: [handover] path \"%s\" is no .cor on the USB stick (/Usb0/...)" % (os.path.relpath(prof, ROOT), h.get("path", "")))
    stick_cor = on_stick.group(1)
    d = desc_of(core)
    c = d.get("core", {})
    start = "menu_first" if c.get("menu_first") else "program" if c.get("needs_program") else "running"
    assignments({"prof": prof, "ident": ident_of(text, prof, d), "hold": hold_of(core, d), "start": start, "stick_cor": stick_cor,
                 "rom_dir": stick_cor[:-len(".cor")] + "/roms"})


def records(cor):
    b = open(cor, "rb").read()
    if b[:8] != cor_pack.MAGIC:
        raise Error("%s: not a .cor bundle" % cor)
    out, pos = {}, 12
    while pos + 8 <= len(b):
        t, n = struct.unpack_from("<II", b, pos)
        if pos + 8 + n > len(b):
            raise Error("%s: record %d runs past the end" % (cor, t))
        out.setdefault(t, b[pos + 8:pos + 8 + n])
        pos += 8 + n
    return out


def elf_loads(b, where):
    """(entry, [(file offset, address, file size)]) of the PT_LOAD segments of a 32-bit little-endian ELF"""
    if b[:4] != b"\x7fELF" or b[4] != 1 or b[5] != 1:
        raise Error("%s: not a 32-bit little-endian ELF" % where)
    entry, phoff = struct.unpack_from("<II", b, 24)
    phentsize, phnum = struct.unpack_from("<HH", b, 42)
    segs = []
    for i in range(phnum):
        t, off, _va, pa, fs = struct.unpack_from("<5I", b, phoff + i * phentsize)
        if t == 1 and fs:
            segs.append((off, pa, fs))
    if not segs:
        raise Error("%s: no loaded segment" % where)
    return entry, segs


def firmware(cor, elf, out):
    m = manifest(cor)
    app = records(cor).get(cor_pack.T_APP)
    if app is None or len(app) != m.get("app_size") or cor_pack.sha(app) != m.get("app_sha256"):
        raise Error("%s: the app record is missing or does not match the manifest's size and SHA-256" % cor)
    load, entry = m.get("app_load"), m.get("app_entry")
    b = bytearray(open(elf, "rb").read())
    e_entry, segs = elf_loads(b, elf)
    lo = min(pa for _, pa, _ in segs)
    hi = max(pa + fs for _, pa, fs in segs)
    if (lo, hi - lo, e_entry) != (load, len(app), entry):
        raise Error("%s loads 0x%x bytes at 0x%x, entry 0x%x; the .cor's app 0x%x bytes at 0x%x, entry 0x%x: not the link of this app"
                    % (elf, hi - lo, lo, e_entry, len(app), load or 0, entry or 0))
    image = bytearray(len(app))                  # the image of the link as objcopy makes it: the segments at their addresses, 0 between them
    for off, pa, fs in segs:
        image[pa - lo:pa - lo + fs] = b[off:off + fs]
    at = image.find(cor_pack.HEAP_MAGIC)
    if at >= 0 and image.find(cor_pack.HEAP_MAGIC, at + 1) < 0:      # the one heap record: its first word is the packer's
        w = at + len(cor_pack.HEAP_MAGIC)
        image[w:w + 4] = app[w:w + 4]
    if image != app:
        i = next(i for i in range(len(app)) if image[i] != app[i])
        raise Error("%s and the .cor's app differ at 0x%x (address 0x%x), outside the heap word: not the link of this app" % (elf, i, lo + i))
    for off, pa, fs in segs:
        b[off:off + fs] = app[pa - lo:pa - lo + fs]
    with open(out, "wb") as f:
        f.write(b)
    print("%d bytes, SHA-256 %s" % (len(app), m["app_sha256"]))


def roms(core, out):
    d = desc_of(core)
    want = [(r["names"][0], r["sizes"][0]) for r in d.get("rom", []) if r.get("required") or "set" in r]
    if "romset" in d:
        want.append((d["romset"]["example"], d["romset"]["sizes"][0]))
    os.makedirs(out, exist_ok=True)
    for n, size in want:
        with open(os.path.join(out, n), "wb") as f:
            f.write(bytes(size))
        print("%s (%d bytes)" % (n, size))


def main(argv):
    try:
        if len(argv) == 4 and argv[0] == "profile":
            profile(*argv[1:])
        elif len(argv) == 4 and argv[0] == "facts":
            facts(*argv[1:])
        elif len(argv) == 3 and argv[0] == "roms":
            roms(*argv[1:])
        elif len(argv) == 3 and argv[0] == "direct":
            direct(*argv[1:])
        elif len(argv) == 4 and argv[0] == "firmware":
            firmware(*argv[1:])
        else:
            print(__doc__, file=sys.stderr)
            return 2
    except (Error, OSError, ValueError, KeyError, IndexError, struct.error, descrec.DescError) as e:
        print("ue2emu_core: %s" % e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
