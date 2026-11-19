#!/usr/bin/env python3
"""What the updater left after a ue2emu run (T135 D8/D9, T132): the flash image, the medium images, Flash:/boot.cfg. Exit 0 = every check passed.

  emu_check.py <rundir> installed --bit B --app A --medium IMG [--medium IMG ..] --cores-dir /SD --cores-src DIR --cores a.cor,b.cor
  emu_check.py <rundir> unchanged                 flash_after.bin byte-identical to flash_before.bin (read back, SHA-256 shown)
  emu_check.py <rundir> written --bit B --app A   only the bitstream and application are checked (a package that sets nothing up)
  emu_check.py <rundir> t240 --medium IMG .. --src DIR --n-cor N [--n-app N] [--n-cfw N] --expect-lines L --folders yes|no [--keep REL=TEXT]
                         [--log-has TEXT] [--menu-only]   T240: the boot menu holds L core lines, every .cor has its folder with roms/saves/configs
  emu_check.py <rundir> noboot                    no boot.cfg on the flash drive and no cores folder on the media (--medium)

<rundir> holds flash_before.bin and flash_after.bin. The backup .cor is compared with what tools/cor_pack.py (the T137 packer, `--cor-pack`)
builds from the same flash image: byte for byte (pack_large with origin/flash_id exists on branch t137-updater; until that is merged pass
--cor-pack <t137 worktree>/tools/cor_pack.py)."""
import argparse
import hashlib
import os
import re
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from fatread import Fat  # noqa: E402

fails = 0


def ok(c, msg):
    global fails
    print(("  ok   " if c else "  FAIL ") + msg)
    if not c:
        fails += 1


def old_parts(before):
    p = 13
    for _ in b"abcd":
        p += 3 + struct.unpack(">H", before[p + 1:p + 3])[0]
    n = struct.unpack(">I", before[p + 1:p + 5])[0]
    dlen = struct.unpack_from("<I", before, 0x220004)[0]
    return before[:p + 5 + n], before[0x220000 + 12:0x220000 + 12 + dlen]


class Medium:
    """A raw disk image with a FAT volume (SD card, or the work image of a --usb-dir stick), attached read-only through hdiutil."""

    def __init__(self, img):
        r = subprocess.run(["hdiutil", "attach", "-imagekey", "diskimage-class=CRawDiskImage", "-nobrowse", "-readonly", img], capture_output=True, text=True)
        vol = re.findall(r"(/Volumes/.+)$", r.stdout, re.M)
        dev = re.findall(r"^/dev/(disk\d+)\s", r.stdout, re.M)
        if not vol:
            sys.exit("cannot attach %s: %s" % (img, r.stdout + r.stderr))
        self.root, self.dev = vol[0].strip(), dev[0]

    def close(self):
        subprocess.run(["hdiutil", "detach", "/dev/" + self.dev, "-quiet"])


def flash_boot_cfg(after):
    f = Fat(after[0x400000:0xFE8000])
    e = f.find("boot.cfg")
    return f.read(e).decode() if e else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("run")
    ap.add_argument("mode", choices=("installed", "unchanged", "written", "noboot", "nobackup", "hasbackup", "t240"))
    ap.add_argument("--bit")
    ap.add_argument("--app")
    ap.add_argument("--medium", action="append", default=[])
    ap.add_argument("--cores-dir", default="")
    ap.add_argument("--cores-src")
    ap.add_argument("--cores", default="")
    ap.add_argument("--cor-pack", default=os.path.join(HERE, "..", "..", "tools", "cor_pack.py"))
    ap.add_argument("--default-name", default="Commodore 1.1.0")
    ap.add_argument("--src", help="t240: the folder the dummy files were made in")
    ap.add_argument("--n-cor", type=int, default=0)
    ap.add_argument("--n-app", type=int, default=0)
    ap.add_argument("--n-cfw", type=int, default=0)
    ap.add_argument("--expect-lines", type=int, default=-1)
    ap.add_argument("--folders", choices=("yes", "no"), default="yes")
    ap.add_argument("--keep", action="append", default=[], help="t240: <path below cores>=<text>: a file that was on the medium before and must be as it was")
    ap.add_argument("--log-has", action="append", default=[], help="t240: text the run's screen dump (emu.log) must hold")
    ap.add_argument("--menu-only", action="store_true", help="t240: check only boot.cfg and the folders, not the copied files (the control updater copies fewer)")
    a = ap.parse_args()
    before = open(os.path.join(a.run, "flash_before.bin"), "rb").read()
    after = open(os.path.join(a.run, "flash_after.bin"), "rb").read()

    if a.mode == "unchanged":
        ok(before[:0x220000] == after[:0x220000], "0x000000-0x21FFFF (bitstream) read back equal, SHA-256 %s" % hashlib.sha256(after[:0x220000]).hexdigest())
        ok(before[0x220000:0x400000] == after[0x220000:0x400000], "0x220000-0x3FFFFF (application) read back equal, SHA-256 %s" % hashlib.sha256(after[0x220000:0x400000]).hexdigest())
        print("FAILS:", fails)
        return 1 if fails else 0

    if a.mode in ("installed", "written", "nobackup", "hasbackup"):
        bit, app = open(a.bit, "rb").read(), open(a.app, "rb").read()
        ok(after[:len(bit)] == bit, "bitstream at 0x000000 equals the package (%d bytes)" % len(bit))
        ok(after[0x220000:0x220000 + len(app)] == app, "application at 0x220000 equals the package (%d bytes)" % len(app))

    mediums = [Medium(m) for m in a.medium]
    try:
        trees = []
        for m in mediums:
            trees.append(m.root)
            print("medium", m.root, sorted(os.listdir(m.root)))
        if a.mode == "noboot":
            ok(flash_boot_cfg(after) is None, "no boot.cfg on the flash drive")
            for t in trees:
                ok(not os.path.exists(os.path.join(t, "cores")), "no cores folder on %s" % t)
        if a.mode in ("nobackup", "hasbackup"):
            # T135 B2: a backup of the flash (TRXBK*) is made over our own firmware when the updater runs on the machine, and not when a host started it
            found = sorted(os.path.join(r, n) for t in trees for r, _, fs in os.walk(t) for n in fs if n.upper().startswith("TRXBK"))
            print("  backup files:", [os.path.relpath(f, "/") for f in found])
            if a.mode == "nobackup":
                ok(not found, "no backup of the flash on any medium")
            else:
                ok(found, "the backup of the flash is on a medium")
            ok(flash_boot_cfg(after) is None, "no boot.cfg on the flash drive")
        if a.mode == "t240":
            holders = [t for t in trees if os.path.isdir(os.path.join(t, "cores"))]
            ok(len(holders) == 1, "exactly one medium holds a cores folder (%s)" % holders)
            cdir = os.path.join(holders[0], "cores")
            cors = ["dummy%02d" % i for i in range(1, a.n_cor + 1)]
            cfws = ["commodore_x%d" % i for i in range(1, a.n_cfw + 1)]
            apps = ["app%d" % i for i in range(1, a.n_app + 1)]
            if not a.menu_only:
                for c in cors + cfws:
                    ext = ".cor" if c in cors else ".cfw"
                    p = os.path.join(cdir, c + ext)
                    ok(os.path.isfile(p) and open(p, "rb").read() == open(os.path.join(a.src, c + ext), "rb").read(), "%s%s copied byte-equal" % (c, ext))
                for c in apps:
                    p = os.path.join(cdir, "apps", c + ".u2a")
                    ok(os.path.isfile(p) and open(p, "rb").read() == open(os.path.join(a.src, c + ".u2a"), "rb").read(), "apps/%s.u2a copied byte-equal" % c)
            cfg = flash_boot_cfg(after)
            ok(cfg is not None, "Flash:/boot.cfg exists")
            if cfg:
                lines = [l for l in cfg.splitlines() if l.startswith("core=")]
                print("    boot.cfg: %d core lines" % len(lines))
                ok(len(lines) == a.expect_lines, "boot.cfg holds %d core lines (has %d)" % (a.expect_lines, len(lines)))
                paths = [l[5:].split("|")[0] for l in lines]
                ok(bool(paths) and paths[0].endswith("/cores/commodore-110.cor"), "the first entry is the backup .cor")
                ok(len(set(paths)) == len(paths), "no path twice")
                if not a.menu_only and a.expect_lines >= 1 + len(cors) + len(cfws):
                    for c in cors:
                        ok(any(p.endswith("/cores/%s.cor" % c) for p in paths), "boot.cfg has %s.cor" % c)
                    for c in cfws:
                        ok(any(p.endswith("/cores/%s.cfw" % c) for p in paths), "boot.cfg has %s.cfw" % c)
                ok(not any("/apps/" in p or p.endswith(".u2a") for p in paths), "no application is a boot entry")
            for c in cors + ["commodore-110"]:
                d = os.path.join(cdir, c)
                if a.folders == "yes":
                    ok(all(os.path.isdir(os.path.join(d, sub)) for sub in ("", "roms", "saves", "configs")), "%s/ has roms/, saves/ and configs/" % c)
                else:
                    ok(not os.path.exists(d), "no folder %s/" % c)
            for c in cfws:
                ok(not os.path.exists(os.path.join(cdir, c)), "no core folder for the vendor file %s.cfw" % c)
            for k in a.keep:
                rel, text = k.split("=", 1)
                p = os.path.join(cdir, rel)
                ok(os.path.isfile(p) and open(p, "rb").read() == text.encode(), "%s stays as it was" % rel)
            log = open(os.path.join(a.run, "emu.log"), errors="replace").read()
            for t in a.log_has:
                ok(t in log, "the screen dump says %r" % t)
        if a.mode == "installed":
            # the cores folder is /<medium>/cores: the medium is the image whose name matches --cores-dir (SD, USB0), the first one that has it
            holders = [t for t in trees if os.path.isdir(os.path.join(t, "cores"))]
            ok(len(holders) == 1, "exactly one medium holds a cores folder (%s)" % holders)
            cdir = os.path.join(holders[0], "cores")
            names = sorted(os.listdir(cdir))
            bks = [n for n in names if n.startswith("TRXBK")]
            want = sorted(["commodore-110.cor"] + [c for c in a.cores.split(",") if c])
            ok([n for n in names if n not in bks] == want, "cores folder holds %s" % names)
            ok(any(n.endswith(".BIN") for n in bks) and any(n.endswith(".CFW") for n in bks), "the raw backup and the restore bundle are in the cores folder: %s" % bks)
            ok(not [n for t in trees for n in os.listdir(t) if n.startswith("TRXBK")], "no backup file anywhere else on the media")
            sys.path.insert(0, os.path.dirname(os.path.abspath(a.cor_pack)))
            import cor_pack as C
            got = open(os.path.join(cdir, "commodore-110.cor"), "rb").read()
            m, recs = C.parse(got)
            print("  manifest: id %s, name %s, version %s, origin %s, flash_id %s, map %s, idcode %s" %
                  (m["id"], m["name"], m["version"], m.get("origin"), m.get("flash_id"), m["map"], m["idcode"]))
            ok(m.get("origin") == "backup" and m.get("flash_bit") == 0 and m.get("flash_app") == 0x220000, "manifest origin backup, flash_bit 0, flash_app 0x220000")
            oldbit, oldapp = old_parts(before)
            icon = open(os.path.join(HERE, "..", "..", "tools", "core_logos", "recovery", "icon.rgb565"), "rb").read()
            ref = C.pack_large("commodore-110", "Commodore 1.1.0", "1.1.0", oldbit, oldapp, icon=icon, origin="backup", flash_id=m["flash_id"])
            ok(recs[C.T_ICON] == icon, "the recovery icon (2048 bytes) is record 4")
            ok(got == ref, "backup .cor byte-equal to cor_pack.py pack-large of the old flash (%d bytes)" % len(got))
            ok(recs[C.T_BIT] == oldbit and recs[C.T_APP] == oldapp, "its records hold the old bitstream and application")
            for c in [c for c in a.cores.split(",") if c]:
                ok(open(os.path.join(cdir, c), "rb").read() == open(os.path.join(a.cores_src, c), "rb").read(), "%s copied byte-equal" % c)
            cfg = flash_boot_cfg(after)
            ok(cfg is not None, "Flash:/boot.cfg exists")
            if cfg:
                print("".join("    | " + l + "\n" for l in cfg.splitlines()))
                lines = cfg.splitlines()
                ok("direct=1" in lines and "default=0" in lines, "boot.cfg: direct=1, default=0")
                cores = [l for l in lines if l.startswith("core=")]
                ok(len(cores) == 1 + len([c for c in a.cores.split(",") if c]), "boot.cfg: %d core entries" % len(cores))
                ok(cores[0].split("|")[1] == a.default_name and cores[0].split("|")[0].endswith("/cores/commodore-110.cor"), "boot.cfg: the default entry is the backup .cor, named %s" % a.default_name)
    finally:
        for m in mediums:
            m.close()
    print("FAILS:", fails)
    return 1 if fails else 0


sys.exit(main())
