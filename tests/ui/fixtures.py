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


def make_stick(stick, roms_dir, core="trx64u"):
    """The USB stick of the test machine: games/ with two disks and a program, roms/c64/ with the three ROMs of the emulator's firmware tree."""
    shutil.rmtree(stick, ignore_errors=True)
    os.makedirs(os.path.join(stick, "games"))
    if core == "amiga1200":
        # the core folder (a blank Kickstart image stands in: no ROM is in the repository), two floppies and a program
        os.makedirs(os.path.join(stick, "cores", "amiga1200", "roms"))
        with open(os.path.join(stick, "cores", "amiga1200", "roms", "kick31.rom"), "wb") as f:
            f.write(bytes(524288))
        for n, b in (("alpha.adf", 1), ("beta.adf", 2)):
            with open(os.path.join(stick, "games", n), "wb") as f:
                f.write(bytes([b]) * 901120)
        with open(os.path.join(stick, "games", "hello.prg"), "wb") as f:
            f.write(HELLO_PRG)
        return
    os.makedirs(os.path.join(stick, "roms", "c64"))
    with open(os.path.join(stick, "games", "alpha.d64"), "wb") as f:
        f.write(make_d64("ALPHA", "HELLO", HELLO_PRG))
    with open(os.path.join(stick, "games", "beta.d64"), "wb") as f:
        f.write(make_d64("BETA", "WORLD", HELLO_PRG))
    with open(os.path.join(stick, "games", "hello.prg"), "wb") as f:
        f.write(HELLO_PRG)
    for dst, src in (("basic.bin", "basic.901226-01.bin"), ("kernal.bin", "kernal.901227-03.bin"), ("chars.bin", "characters.901225-01.bin")):
        shutil.copy(os.path.join(roms_dir, src), os.path.join(stick, "roms", "c64", dst))


def make_sd_dir(sd):
    """The SD card of the test machine: one program (nothing third-party)."""
    shutil.rmtree(sd, ignore_errors=True)
    os.makedirs(os.path.join(sd, "cards"))
    with open(os.path.join(sd, "cards", "sdgame.prg"), "wb") as f:
        f.write(HELLO_PRG)
    with open(os.path.join(sd, "sdhello.prg"), "wb") as f:
        f.write(HELLO_PRG)
