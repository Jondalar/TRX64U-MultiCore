"""Test fixtures made on the spot: nothing third-party is checked in. A D64 is built here (35 tracks, one PRG), a PRG is 29 bytes of BASIC."""
import os
import shutil

SECTORS = [21] * 17 + [19] * 7 + [18] * 6 + [17] * 5


def _off(track, sector):
    return (sum(SECTORS[:track - 1]) + sector) * 256


def make_d64(name, prg_name, prg_bytes):
    """A formatted 35 track image holding one closed PRG (track 17, sector 0 on, the BAM and the directory on track 18)."""
    img = bytearray(sum(SECTORS) * 256)
    bam = _off(18, 0)
    img[bam:bam + 2] = bytes([18, 1])
    img[bam + 2] = 0x41
    for t in range(1, 36):
        free = SECTORS[t - 1]
        e = bam + 4 * t
        img[e] = free
        img[e + 1:e + 4] = bytes([0xFF, 0xFF, (1 << (free - 16)) - 1 if free > 16 else 0])
    nm = name.upper().encode("latin-1")[:16].ljust(16, b"\xa0")
    img[bam + 0x90:bam + 0xA0] = nm
    img[bam + 0xA0:bam + 0xA2] = b"\xa0\xa0"
    img[bam + 0xA2:bam + 0xA4] = b"T1"
    img[bam + 0xA4:bam + 0xA9] = b"\xa02A\xa0\xa0"
    # the PRG: one sector at track 17 sector 0
    t, s = 17, 0
    body = prg_bytes[:254]
    o = _off(t, s)
    img[o:o + 2] = bytes([0, len(body) + 1])
    img[o + 2:o + 2 + len(body)] = body
    for (tt, ss) in ((17, 0),):
        e = bam + 4 * tt
        img[e] -= 1
        img[e + 1 + ss // 8] &= ~(1 << (ss % 8)) & 0xFF
    d = _off(18, 1)
    img[d:d + 2] = bytes([0, 0xFF])
    img[d + 2] = 0x82
    img[d + 3:d + 5] = bytes([t, s])
    img[d + 5:d + 21] = prg_name.upper().encode("latin-1")[:16].ljust(16, b"\xa0")
    img[d + 30:d + 32] = bytes([1, 0])
    return bytes(img)


HELLO_PRG = bytes([0x01, 0x08, 0x0B, 0x08, 0x0A, 0x00, 0x99, 0x22, 0x48, 0x49, 0x22, 0x00, 0x00, 0x00])


SCPU_ROM = bytes([0xEA]) * 0xFFFC + bytes([0x00, 0x80, 0x00, 0x00])        # T217 pictures: 64 KiB, reset vector $8000, not empty (what scpu_rom_fault asks); no real SuperCPU ROM is in the repository

ROMFILES = {"c64scpu": (("scpu64.rom", SCPU_ROM), ("basic.bin", 8192), ("kernal.bin", 8192), ("chars.bin", 4096)), "vc20": (("basic.bin", 8192), ("kernal.bin", 8192), ("char.bin", 4096)), "c128": (("c128.rom", 73728),),
            "atari800": (("ATARIXL.ROM", 16384), ("BASIC.ROM", 8192)),
            "cpc": tuple((n, 16384) for n in ("os464.rom", "basic464.rom", "os664.rom", "basic664.rom", "os6128.rom", "basic6128.rom", "amsdos.rom")),
            "pet": (("basic.bin", 12288), ("edit.bin", 2048), ("kernal.bin", 4096), ("chargen.bin", 4096), ("dos4040.bin", 12288), ("ctl4040.bin", 2048))}


def _big(stick, ext):
    """T209: a long list with long real names (the Library's rows, the cut, the scroll margin)."""
    os.makedirs(os.path.join(stick, "big"))
    for n in ("Turrican_II_The_Final_Fight_Director_Cut_Disk_1_of_3_Fixed", "Last_Ninja_Remix_(1990)(System_3)[cr Fairlight][t +3]",
              "Impossible_Mission_II_(1988)(Epyx)(Side_A)", "Wizball", "Zak_McKracken_and_the_Alien_Mindbenders_Disk_1_Side_A"):
        with open(os.path.join(stick, "big", n + "." + ext), "wb") as f:
            f.write(make_d64("DISK", "HELLO", HELLO_PRG) if ext == "d64" else bytes(901120))
    for i in range(1, 25):
        with open(os.path.join(stick, "big", "game%02d.prg" % i), "wb") as f:
            f.write(HELLO_PRG)


def make_stick(stick, roms_dir, core="trx64u", ovtest=False):
    """The USB stick of the test machine: games/ with two disks and a program, roms/c64/ with the three ROMs of the emulator's firmware tree."""
    shutil.rmtree(stick, ignore_errors=True)
    os.makedirs(os.path.join(stick, "games"))
    if core in ("amiga1200", "vc20", "c128", "atari800", "pet", "c64scpu", "cpc"):
        # the core folder (a blank Kickstart image stands in: no ROM is in the repository), two floppies and a program
        os.makedirs(os.path.join(stick, "cores", core, "roms"))
        with open(os.path.join(stick, "cores", core, "roms", "kick31.rom"), "wb") as f:
            f.write(bytes(524288))
        if core == "pet":                            # T216 P6: a model's own VICE-named files are required, the generic names no longer count; the files come from the
            # local VICE data dir (PET_ROMS_DIR) and the Waterloo set in cores/pet/sim/media; UI_PET_DROP="a.bin,b.bin" leaves files out (the missing-ROM picture)
            drop = [x for x in os.environ.get("UI_PET_DROP", "").split(",") if x]
            corrupt = [x for x in os.environ.get("UI_PET_CORRUPT", "").split(",") if x]
            vice = os.environ.get("PET_ROMS_DIR", os.path.expanduser("~/Development/C64/Tools/vice/vice/data/PET"))
            media = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "cores", "pet", "sim", "media")
            for src in (vice, media):
                for n in (sorted(os.listdir(src)) if os.path.isdir(src) else ()):
                    if n.endswith(".bin") and n not in drop and (src == media or n.startswith(("basic-", "edit-", "kernal-", "characters"))):
                        shutil.copy(os.path.join(src, n), os.path.join(stick, "cores", core, "roms", n))
                        if n in corrupt:                                # T216 D15: a copy with one byte changed (the wrong-content picture)
                            with open(os.path.join(stick, "cores", core, "roms", n), "r+b") as cf:
                                cf.seek(100); b0 = cf.read(1); cf.seek(100); cf.write(bytes([b0[0] ^ 0xFF]))
        if core == "pet" and os.environ.get("UI_PET_CFG"):      # T216 D14: a saved settings file (Model=SuperPET), so the boot itself meets the missing Waterloo files
            shutil.copy(os.environ["UI_PET_CFG"], os.path.join(stick, "cores", core, "pet.cfg"))
        for n, b in ROMFILES.get(core, ()) if core != "pet" else (("dos4040.bin", 12288), ("ctl4040.bin", 2048)):          # the ROMs the core wants (blank images stand in: no ROM is in the repository), else it waits for them and the UI service stays down
            with open(os.path.join(stick, "cores", core, "roms", n), "wb") as f:
                f.write(b if isinstance(b, bytes) else bytes([0xA5]) * b if core == "pet" else bytes(b))       # PET: a pattern, so the read-back of the ROM aperture proves something
        for n, b in (("alpha.adf", 1), ("beta.adf", 2), ("Turrican_II_The_Final_Fight_Director_Cut_Disk_1_of_3_Fixed.adf", 3)):
            with open(os.path.join(stick, "games", n), "wb") as f:
                f.write(bytes([b]) * 901120)
        with open(os.path.join(stick, "games", "hello.prg"), "wb") as f:
            f.write(HELLO_PRG)
        if core in ("pet", "vc20", "c128", "c64scpu"):  # T206: a disk to run (the PET core mounts .d64); T212: the CBM cores mount one from their drive list; T217: a program inside a disk
            with open(os.path.join(stick, "games", "alpha.d64"), "wb") as f:
                f.write(make_d64("ALPHA", "hello", HELLO_PRG))
        if core == "cpc":                            # T92 #17: two disks for the F1 drive list (a blank CPCEMU image, nothing third-party)
            for n in ("alpha.dsk", "beta.dsk"):
                with open(os.path.join(stick, "games", n), "wb") as f:
                    f.write(b"MV - CPCEMU Disk-File\r\nDisk-Info\r\n".ljust(256, b"\0") + bytes(40000))
        _big(stick, "adf")
        if ovtest:
            make_ovtest_cor(stick, core)
        return
    if core == "snes":
        # a core with no drives: one synthetic 1 MiB LoROM image (header at 0x7FC0: title, LoROM mode, 1 MiB, complement + checksum, reset vector), nothing third-party
        os.makedirs(os.path.join(stick, "cores", "snes"))
        rom = bytearray(1 << 20)
        h = 0x7FC0
        rom[h:h + 21] = b"TRX TEST CARTRIDGE".ljust(21)
        rom[h + 0x15] = 0x20
        rom[h + 0x17] = 0x0A
        rom[h + 0x1C:h + 0x20] = bytes([0xFF, 0xFF, 0x00, 0x00])
        rom[h + 0x3C:h + 0x3E] = bytes([0x00, 0x80])
        with open(os.path.join(stick, "games", "test.sfc"), "wb") as f:
            f.write(rom)
        return
    if core == "neogeo":
        os.makedirs(os.path.join(stick, "cores", "neogeo", "roms"))
        for n, b in (("sp-s2.sp1", 0x20000), ("000-lo.lo", 0x10000), ("sfix.sfix", 0x20000)):     # blank images stand in: no ROM is in the repository
            with open(os.path.join(stick, "cores", "neogeo", "roms", n), "wb") as f:
                f.write(bytes(b))
        return
    if core == "diag":                               # T196 S7: the diag core has a folder and nothing else (it writes its example tests there)
        os.makedirs(os.path.join(stick, "cores", "diag"))
        return
    if core == "nes":
        # a core with no drives and no ROM set: the core folder holds nothing, the games folder one cartridge image (iNES header, 16 KiB PRG, 8 KiB CHR of zeros)
        os.makedirs(os.path.join(stick, "cores", "nes"))
        with open(os.path.join(stick, "games", "blank.nes"), "wb") as f:
            f.write(b"NES\x1a\x01\x01" + bytes(10) + bytes(16384 + 8192))
        return
    os.makedirs(os.path.join(stick, "roms", "c64"))
    _big(stick, "d64")
    with open(os.path.join(stick, "games", "alpha.d64"), "wb") as f:
        f.write(make_d64("ALPHA", "HELLO", HELLO_PRG))
    with open(os.path.join(stick, "games", "beta.d64"), "wb") as f:
        f.write(make_d64("BETA", "WORLD", HELLO_PRG))
    with open(os.path.join(stick, "games", "hello.prg"), "wb") as f:
        f.write(HELLO_PRG)
    for dst, src in (("basic.bin", "basic.901226-01.bin"), ("kernal.bin", "kernal.901227-03.bin"), ("chars.bin", "characters.901225-01.bin")):
        shutil.copy(os.path.join(roms_dir, src), os.path.join(stick, "roms", "c64", dst))
    if ovtest:
        make_ovtest_cor(stick, core)


# T256: the core the overlay features run on (S3, S5, S10) is the same core with a .cor that carries the test library and the UI TOML of the test element (tests/ui/ovtest/). The .cor is
# made by the real packer on a stand-in bitstream (the tests of the packer's readers do the same: tests/coreapp/gen_bundle.py); the application reads the description and the library
# from it at the hand-over path of the core's profile. The names are the core names the profiles hand over (tests/coreapp/emu/profiles/*.toml, `core`).
OVTEST_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "ovtest")
OVTEST_U2A = os.environ.get("OVTEST_U2A", os.path.join(os.path.dirname(os.path.abspath(__file__)), "build", "ovtest.u2a"))
OVTEST_NAMES = {"vc20": "VIC-20", "c64scpu": "C64 with SuperCPU", "trx64u": "TRX64U"}


def _stand_in_bit():
    import struct
    import cor_pack
    hdr = cor_pack.BIT_MAGIC
    for key in b"abcd":
        txt = b"x" * 5 + b"\0"
        hdr += bytes([key]) + struct.pack(">H", len(txt)) + txt
    data = b"\xFF" * 8 + cor_pack.SYNC + bytes(range(256)) * 4
    return hdr + b"e" + struct.pack(">I", len(data)) + data


def make_ovtest_cor(stick, core):
    """stick/cores/<core>.cor: the description of tests/ui/ovtest/ovtest.toml and the library ovtest.u2a (built by tests/ui/ovtest/build.sh)."""
    import sys
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools"))
    import cor_pack
    import descrec
    if not os.path.exists(OVTEST_U2A):
        raise RuntimeError("no test library %s (tests/ui/ovtest/build.sh, or OVTEST_U2A=)" % OVTEST_U2A)
    desc = descrec.compile_text(open(os.path.join(OVTEST_DIR, "ovtest.toml")).read())
    blob = cor_pack.pack(name=OVTEST_NAMES[core], desc=desc, libs=[open(OVTEST_U2A, "rb").read()], version="0.1", partial=_stand_in_bit(), app=bytes(0x60 for _ in range(64)),
                         load=0x30000, entry=0x30000, host_key="820eded2")
    os.makedirs(os.path.join(stick, "cores"), exist_ok=True)
    with open(os.path.join(stick, "cores", core + ".cor"), "wb") as f:
        f.write(blob)


def make_sd_dir(sd):
    """The SD card of the test machine: one program (nothing third-party)."""
    shutil.rmtree(sd, ignore_errors=True)
    os.makedirs(os.path.join(sd, "cards"))
    with open(os.path.join(sd, "cards", "sdgame.prg"), "wb") as f:
        f.write(HELLO_PRG)
    with open(os.path.join(sd, "sdhello.prg"), "wb") as f:
        f.write(HELLO_PRG)
