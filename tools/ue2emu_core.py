#!/usr/bin/env python3
"""T239.5 D13 (step 12 of the matrix): one core in ue2emu under the host app, from the core's own files - no script names a core.

A core's emulator profile is cores/<core>/app/ue2emu.toml (the ID word and the register values its app reads at start; S44 of the emulator). The rest comes from
the core's .cor (its manifest) and its data description cores/<core>/app/desc.toml.

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
    top = re.split(r"^\[", open(prof).read(), maxsplit=1, flags=re.M)[0]
    ident = re.search(r'^id\s*=\s*"(.{4})"', top, re.M)
    if not ident:
        raise Error("%s has no four-character id" % prof)
    d = desc_of(core)
    mods = d.get("hotkeys", {}).get("mod", [])
    names = [KEYS.get(k) for k in mods if k != HK_NONE]
    if None in names:
        raise Error("the core %s: a hotkey modifier %s is none of %s" % (core, mods, sorted(KEYS)))
    large = m.get("kind") == "large"
    out = {"kind": "large" if large else "dfx", "cname": m["name"], "prof": name, "ident": "%s %s" % (ident.group(1), d["core"]["name"]), "hold": "+".join(names),
           "magic": "0x%08x" % m.get("app_entry", 0) if large else "", "start_ms": "60000" if large else "30000"}
    for k, v in out.items():
        print("%s=%s" % (k, shlex.quote(v)))


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
        else:
            print(__doc__, file=sys.stderr)
            return 2
    except (Error, OSError, ValueError, KeyError, IndexError, descrec.DescError) as e:
        print("ue2emu_core: %s" % e, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
