#!/usr/bin/env python3
"""INSTALL.md of a multi-core release bundle (tools/make_release_mc.sh, T94 P5).

Everything about the bundle in the text is read from the files it describes: the update file's manifest,
the app record, the .bit header and every .cor's metadata. The per-core ROM and media facts are the CORES
and PARAS tables below, each CORES line names the source (file:line) it was read from;
tools/test_make_release_mc.py checks that every cores/*/core.toml has a line and that no link in the
text leaves LINK_HOSTS.

  tools/release_notes_mc.py --id <id> --name <base> --bit <bit> --app <app>
      --bit-commit <sha> --app-commit <sha> --cor-dir <dir> -o INSTALL.md
"""
import argparse, hashlib, json, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import trx_pkg                                            # noqa: E402  (the layout lives there)

# The only hosts a link in INSTALL.md may point to: legal sources of ROMs and Kickstarts (Alex, 2026-10-21).
# Never a download or archive site for ROM images. ultimate64.com: Gideon's firmware, linked, never shipped (T120 M1,
# Alex 2026-10-23: "His .cfw we never ship, it lies with him. Just a link to it").
LINK_HOSTS = ("vice-emu.sourceforge.io", "www.amigaforever.com", "www.c64forever.com", "www.virtualdub.org", "ultimate64.com")

VICE = "https://vice-emu.sourceforge.io/"
AMIGA_FOREVER = "https://www.amigaforever.com/"
C64_FOREVER = "https://www.c64forever.com/"
ALTIRRA = "https://www.virtualdub.org/altirra.html"


def bad_links(text):
    """Every URL in text whose host is not in LINK_HOSTS."""
    bad = []
    for m in re.finditer(r"https?://([^/\s)>`\]\"']+)[^\s)>`\]\"']*", text):
        if m.group(1).lower() not in LINK_HOSTS:
            bad.append(m.group(0))
    return bad


# One entry per core id (= cores/<id>/core.toml; trx64 and c64u have none; amiga1200 and c128 are large cores, T115 7). `match` is looked for in the lower-cased name the .cor
# carries (core.toml `name`). `sources` says where each fact was read.
CORES = [
    dict(id="ddt_mandel", match=("mandelbrot",), title="DDT Mandelbrot (mandelbrUlt)", folder="ddt_mandel",
         files="none", needed="nothing",
         sources="cores/ddt_mandel/mono/core.toml (a large core without an application: no ROM list)"),
    dict(id="ddt_novic", match=("no vic",), title="DDT Look Ma, No VIC", folder="ddt_novic",
         files="none", needed="nothing",
         sources="cores/ddt_novic/app/lib/ddt_novic.cc (no ROM list in the descriptor)"),
    dict(id="vc20", match=("vic-20", "vc20"), title="VIC-20", folder="vc20",
         files="`kernal.bin` 8,192, `basic.bin` 8,192, `char.bin` 4,096 bytes", needed="all three",
         sources="cores/vc20/app/lib/vc20.cc (roms[], names and sizes); cores/vc20/app/lib/vc20_regs.h"),
    dict(id="c264", match=("commodore 264", "c264", "c16"), title="Commodore 264 (C16, Plus/4)", folder="c16",
         files="`kernal.bin` and `basic.bin` 16,384 each; Plus/4: `3plus1lo.bin`, `3plus1hi.bin` 16,384 each",
         needed="Kernal and Basic; the two function ROMs for the Plus/4",
         sources="cores/c264/app/lib/c264.cc (roms[], names and sizes); cores/c264/app/lib/c264_regs.h"),
    dict(id="amiga1200", match=("amiga 1200",), title="Amiga 1200 (AGA, 68020)", folder="amiga",
         files="any `.rom` of 262,144 or 524,288 bytes (Kickstart); Auto takes 3.1", needed="one Kickstart (3.x for the hard disk)",
         sources="cores/amiga1200/app/lib/amiga1200.cc (KS_VER_31, folder amiga); coreapp/src/romset.cc"),
    dict(id="c64scpu", match=("supercpu",), title="C64 with SuperCPU", folder="c64scpu",
         files="the SuperCPU ROM of 131,072, 65,536 or 262,144 bytes (`scpu64.rom`, `scpu.rom`, `supercpu.rom` or `scpu64-v2.04.rom`); "
               "`basic-901226-01.bin` (8,192), `kernal-901227-03.bin` (8,192), `chargen-901225-01.bin` (4,096); optional: the 1541 ROM "
               "(16,384 or 32,768), the 1581 ROM (32,768), the JiffyDOS 1541 and 1581 ROMs",
         needed="the SuperCPU ROM, BASIC, KERNAL and the character ROM",
         sources="host-app/software/hostapp/romreq_table.inc:46-53 (the core's RomSpec rows)"),
    dict(id="c128", match=("commodore 128",), title="Commodore 128", folder="c128",
         files="one system ROM image of 73,728 bytes: `boot0.rom`, `c128.rom`, `c128-system.rom` or `c128_system.rom`",
         needed="the system ROM image",
         sources="cores/c128/app/lib/c128.cc (roms[]); cores/c128/app/lib/c128_regs.h (C128_ROM_BYTES)"),
    dict(id="pet", match=("commodore pet",), title="Commodore PET", folder="pet",
         files="per model: BASIC (8,192 or 12,288), Editor (2,048), KERNAL (4,096), Characters (2,048 or 4,096) bytes, VICE's file names (`basic-4.901465-23-20-21.bin`, `edit-4-40-n-60Hz.901499-01.bin`, `kernal-4.901465-22.bin`, `characters.901640-01.bin` and the 2 / 1 / 8032 variants) or `basic.bin`, `edit.bin`, `kernal.bin`, `chargen.bin`; optional: disk drive DOS and controller ROMs, SuperPET Waterloo ROMs",
         needed="BASIC, Editor, KERNAL and Characters of the chosen model; the drive and Waterloo ROMs only for those features",
         sources="cores/pet/app/lib/pet.cc (roms[], names and sizes)"),
    dict(id="amiga", match=("amiga (ocs", "amiga 500"), title="Amiga (OCS/ECS, 68000)", folder="amiga",
         files="any `.rom` of 262,144 or 524,288 bytes (Kickstart); Auto takes 1.3", needed="one Kickstart",
         sources="cores/amiga/app/lib/amiga.cc (variant table, Kickstart, folder); coreapp/src/romset.cc:39-126; app/loaders/kickstart.cc:11-58"),
    dict(id="atari800", match=("atari",), title="Atari 800 XL", folder="atari800",
         files="`ATARIXL.ROM` 16,384; `BASIC.ROM` or `ATARIBAS.ROM` 8,192 bytes", needed="both",
         sources="cores/atari800/app/lib/atari800.cc:72-75; cores/atari800/app/lib/atari800_regs.h:43-44; coreapp/src/romfind.cc:59-130"),
    dict(id="snes", match=("super nintendo",), title="Super Nintendo", folder="snes",
         files="none: the S-SMP's 64 byte boot ROM is embedded in the application",
         needed="nothing",
         sources="coreapp/cores/snes.cc (spc_ipl); cores/snes/rtl/NOTICE"),
    dict(id="nes", match=("nintendo",), title="Nintendo Entertainment System", folder="nes",
         files="none", needed="nothing",
         sources="cores/nes/app/lib/nes.cc:139-160 (no ROM list in the descriptor)"),
    dict(id="diag", match=("port diagnostics",), title="Port Diagnostics", folder="diag",
         files="none", needed="nothing",
         sources="coreapp/cores/portdiag.cc (the folders `tests/` and `results/` under the core folder; the files of record 8 of diag.cor, the example tests, are written when missing), cores/diag/mono/core.toml (no ROM, no media)"),
    dict(id="apple2", match=("apple",), title="Apple IIe", folder="apple2",
         files="`apple2e.rom` 16,384, `apple2e-video.rom` 8,192, `diskii.rom` 256 bytes",
         needed="the first two; the third for any floppy",
         sources="coreapp/cores/apple2.cc:88-91,122-126; coreapp/cores/apple2_regs.h:49-51"),
    dict(id="zxnext", match=("spectrum next",), title="ZX Spectrum Next", folder="zxnext",
         files="`next.img`: a whole SD card image (FAT, MBR) with the Next's system files on it (any size the machine reads; it is written back)",
         needed="the image; without it the machine's boot ROM shows its own error",
         sources="cores/zxnext/app/lib/zxnext.cc (mount_default, sd_card); cores/zxnext/app/lib/zxnext_regs.h"),
    dict(id="neogeo", match=("neo geo",), title="SNK Neo Geo (MVS / AES)", folder="neogeo",
         files="`sp-s2.sp1` (MVS) or `neo-epo.sp1` (AES) or `uni-bios.rom`, 131,072 bytes each; `000-lo.lo` 65,536 bytes (zoom table); `sfix.sfix` 131,072 bytes; `romsets.xml`",
         needed="one BIOS, the zoom table and the fix ROM; a game set for anything to run",
         sources="cores/neogeo/app/lib/neogeo.cc (roms); cores/neogeo/app/lib/neoset.cc"),
    dict(id="zx", match=("spectrum",), title="ZX Spectrum 48K / 128K", folder="zx",
         files="`48.rom` 16,384 bytes (48K); `128.rom` 32,768 bytes (128K)", needed="one of the two (each gives its model)",
         sources="cores/zx/app/lib/zx.cc (roms[]); cores/zx/app/lib/zx_regs.h (the sizes); coreapp/src/romfind.cc (sets)"),
    dict(id="cpc", match=("amstrad",), title="Amstrad CPC 464 / 664 / 6128", folder="cpc",
         files="464: `os464.rom`, `basic464.rom`; 664: `os664.rom`, `basic664.rom`, `amsdos.rom`; 6128: `os6128.rom`, `basic6128.rom`, `amsdos.rom` (16,384 bytes each)",
         needed="the files of one model (a disk drive needs `amsdos.rom`)",
         sources="cores/cpc/app/lib/cpc.cc (roms[]); cores/cpc/app/lib/cpc_regs.h (the sizes); coreapp/src/romfind.cc (sets)"),
]


# What lies in each roms/ folder (the exact names are in CORES.files).
TREE = {
    "c128": "boot0.rom (73,728 bytes, the system ROM image)",
    "vc20": "kernal.bin  basic.bin  char.bin",
    "c16": "kernal.bin  basic.bin  3plus1lo.bin  3plus1hi.bin",
    "pet": "basic*.bin  edit*.bin  kernal*.bin  characters*.bin (VICE names) or basic.bin edit.bin kernal.bin chargen.bin",
    "amiga": "any name.rom, 256 KB or 512 KB (Kickstart)",
    "atari800": "ATARIXL.ROM  BASIC.ROM",
    "apple2": "apple2e.rom  apple2e-video.rom  diskii.rom",
    "zx": "48.rom  128.rom",
    "cpc": "os464.rom basic464.rom  os664.rom basic664.rom  os6128.rom basic6128.rom  amsdos.rom",
    "zxnext": "next.img (a whole SD card image)",
    "neogeo": "sp-s2.sp1 neo-epo.sp1 uni-bios.rom  000-lo.lo  sfix.sfix  romsets.xml",
}


def core_of(name):
    n = name.lower()
    for c in CORES:
        if any(m in n for m in c["match"]):
            return c
    return None


PARAS = {
    "c64scpu": [
        "Needs the **SuperCPU ROM** in `cores/c64scpu/roms/`: `scpu64.rom` (131,072 bytes; 65,536 or 262,144 bytes and the names",
        "`scpu.rom`, `supercpu.rom`, `scpu64-v2.04.rom` work too), and the three C64 ROMs `basic-901226-01.bin`, `kernal-901227-03.bin`",
        "and `chargen-901225-01.bin`. The 1541 and 1581 ROMs and the JiffyDOS ROMs are optional: they are for the drives.",
    ],
    "amiga1200": [
        "Needs its own copy of a Kickstart in `cores/amiga1200/roms/` (not shared with the Amiga core): one `.rom` of 256 or 512 KB. Its **Auto**",
        "setting takes **Kickstart 3.1**. The hard disk (`.hdf`, or a folder mounted as HD0/HD1) needs a 3.x Kickstart.",
    ],
    "c128": [
        "Needs **one system ROM image** of exactly 73,728 bytes in `cores/c128/roms/`, named",
        "`boot0.rom`, `c128.rom`, `c128-system.rom` or `c128_system.rom`. Hold **C=** at the start for the C64 mode.",
    ],
    "vc20": [
        "Needs three files in `cores/vc20/roms/`: the **PAL** Kernal (8,192 bytes) as `kernal.bin`, Basic (8,192) as",
        "`basic.bin` and the character ROM (4,096) as `char.bin`. The core is PAL only. The application keeps trying while",
        "the stick mounts, and after about 10 seconds the screen says what is missing. The other names it knows: `chargen.bin`,",
        "`chargen-901460-03.bin`, `basic-901486-01.bin`, `kernal.901486-07.bin`.",
        "Where to get them: the VICE distribution (%s) holds the Commodore ROM images in `data/VIC20/`." % VICE,
        "Copy the PAL Kernal `901486-07`, Basic `901486-01` and the character ROM `901460-03` and rename them as",
        "above. If the core does not take a file, tell us the message it shows.",
    ],
    "pet": [
        "Needs four ROM files per model in `cores/pet/roms/`: BASIC, Editor, KERNAL and the character ROM, with VICE's file names (table below).",
        "Each model takes the files of its own row and no others: with one of them missing the model does not start and the screen names the missing file",
        "(no generic `basic.bin` or another model's file stands in).",
        "Optional: the disk drive's DOS and controller ROMs (`dos4040.bin`, `ctl4040.bin`, `dos1001` or `dos8250.bin`, `ctl8250.bin`) and the six SuperPET",
        "`waterloo-*.bin` ROMs. Where to get them: the VICE distribution (%s), folder `data/PET/`." % VICE,
        "",
        "**VICE files per model** (in `cores/pet/roms/`, the names are the ones the core looks for; the picture-rate setting chooses the 50 or 60 Hz editor):",
        "",
        "| Model | BASIC | Editor 60 Hz | Editor 50 Hz | KERNAL | Characters |",
        "|---|---|---|---|---|---|",
        "| 2001 | `basic-1.901439-09-05-02-06.bin` | `edit-1-n.901439-03.bin` | same | `kernal-1.901439-04-07.bin` | `characters-1.901447-08.bin` |",
        "| 3032 | `basic-2.901465-01-02.bin` | `edit-2-n.901447-24.bin` | same | `kernal-2.901465-03.bin` | `characters-2.901447-10.bin` |",
        "| 4032 | `basic-4.901465-23-20-21.bin` | `edit-4-40-n-60Hz.901499-01.bin` | `edit-4-40-n-50Hz.901498-01.bin` | `kernal-4.901465-22.bin` | `characters-2.901447-10.bin` |",
        "| 2001 + BASIC 2 | `basic-2.901465-01-02.bin` | `edit-2-n.901447-24.bin` | same | `kernal-2.901465-03.bin` | `characters-1.901447-08.bin` |",
        "| 8032, 8096 | `basic-4.901465-23-20-21.bin` | `edit-4-80-b-60Hz.901474-03.bin` | `edit-4-80-b-50Hz.901474-04.bin` (or `edit-4-80-b-50Hz.901474-04_.bin`) | `kernal-4.901465-22.bin` | `characters.901640-01.bin` (or `characters-2.901447-10.bin`) |",
        "| SuperPET | `basic-4.901465-23-20-21.bin` | `edit-4-80-b-60Hz.901474-03.bin` | `edit-4-80-b-50Hz.901474-04.bin` (or `edit-4-80-b-50Hz.901474-04_.bin`) | `kernal-4.901465-22.bin` | `characters.901640-01.bin` (4,096 bytes: its upper half is the Waterloo character set the 6809 uses) |",
        "",
        "The plain **2001** keeps BASIC 1 as the original has it, and BASIC 1 cannot use the disk drive (a Commodore bug, the core reproduces it). The model **2001 + BASIC 2** is the 2001",
        "with the 3032's BASIC 2, KERNAL 2 and editor 2, and does. The **SuperPET** has 96 KiB (32 KiB + 64 KiB of bank RAM behind `$9000`; the row **SuperPET RAM** takes the bank RAM away), and its",
        "6809 runs the six `waterloo-*.bin` files; the setting **SuperPET CPU** chooses the 6502 or the 6809. **Keyboard mapping**: *Symbolic* types the character on the key's legend (1 types 1, shift + 1 types !),",
        "*Positional* puts the PET key at the same place as the C64 key. A USB pad or a joystick presses the keys the rows **Pad / joystick up ... fire** name (default: 8 2 4 6 and SPACE).",
        "",
        "**The 4040 drive** (setting Drive = 4040) needs two files in the same folder, both of exactly these sizes (the core refuses any other size):",
        "",
        "- `dos4040.bin`, 12,288 bytes (also accepted as `dos4040-901468-14-16.bin`): the drive's DOS ROMs.",
        "- `ctl4040.bin`, 2,048 bytes: the controller ROM `901466-04.bin` written twice into one file, one copy after the other. The result must be 2,048 bytes.",
        "",
        "Join the controller ROM with the command of your system, in the folder that holds `901466-04.bin`, then check the size of `ctl4040.bin`:",
        "",
        "```",
        "macOS, Linux:   cat 901466-04.bin 901466-04.bin > ctl4040.bin",
        "Windows:        copy /b 901466-04.bin+901466-04.bin ctl4040.bin",
        "```",
    ],
    "c264": [
        "Needs `kernal.bin` and `basic.bin` (16,384 bytes each, PAL) in `cores/c264/roms/`.",
        "The Plus/4 model also needs `3plus1lo.bin` and `3plus1hi.bin` (16,384 each, the two halves of",
        "the built-in function ROM).",
        "Where to get them: the VICE distribution (%s), folder `data/PLUS4/`: Kernal `318004-05`, Basic" % VICE,
        "`318006-01`, function ROM `317053-01` (low) and `317054-01` (high). Copy and rename. The two function",
        "ROMs are optional: without them the Plus/4 model has its BASIC and no built-in programs.",
    ],
    "amiga": [
        "Needs **one Kickstart** as a `.rom` file of exactly 256 KB (262,144 bytes) or 512 KB (524,288) in",
        "`cores/amiga/roms/`. The file name does not matter. The core reads the version out of the image (header word",
        "`1111` or `1114`, version at offset 12), and its **Auto** setting takes **Kickstart 1.3**; if there is",
        "none, an older one, then any Kickstart. Up to eight files are listed, sorted by name, in the setting",
        "**Kickstart file (at reload)** (F5, Settings), so you can pick one yourself.",
        "A `.rom` of the wrong size is refused",
        "with a message. With none, the core stays held and the screen says \"Kickstart not found ... Put a 256K",
        "or 512K .rom file ... is missing in /SD/cores/amiga/roms/\" after about 10 seconds. A plain reset reloads the Kickstart when you",
        "picked another file; **Reload Kickstart, Reset** (top of F1) always does.",
        "Where to get it: Amiga Forever (%s), Cloanto's licensed package with Kickstart 1.3 and 3.1." % AMIGA_FOREVER,
        "The core needs a plain image: a file that does not begin with `1111` or `1114` is not recognised as a",
        "Kickstart, and Auto cannot tell what it is. A file that needs Cloanto's `rom.key` to be read has to be",
        "converted by you first.",
    ],
    "atari800": [
        "Needs two files in `cores/atari800/roms/`, both required: `ATARIXL.ROM` (the XL operating system, exactly 16,384",
        "bytes) and `BASIC.ROM` or `ATARIBAS.ROM` (BASIC, exactly 8,192 bytes). Upper or lower case does not",
        "matter. The core checks the size only, not the content, so any 16 KB XL OS and any 8 KB BASIC image is",
        "accepted. A file of the wrong size is reported at once with its size, a missing file after about 10",
        "seconds. It looks twice a second, so plugging the stick in late is enough.",
        "Where to get them: **AltirraOS**, the free replacement Atari OS, and **AltirraBASIC** from the Altirra",
        "project (%s). Use the 16 KB XL image of AltirraOS as `ATARIXL.ROM` and the 8 KB AltirraBASIC as" % ALTIRRA,
        "`BASIC.ROM`; check the sizes, the file names in the Altirra package are not ours. Atari's own ROMs are not",
        "offered here.",
    ],
    "ddt_mandel": [
        "Needs **no ROM**. A large core: DDT's Mandelbrot generator with 20 engines, HDMI output only, no menu. It starts by itself; the joystick in port 2 pans and zooms. The way back to the host is a power cycle.",
    ],
    "ddt_novic": [
        "Needs **no ROM**. DDT's demo starts by itself.",
    ],
    "nes": [
        "Needs **no ROM**. A `.nes` file carries its own program; start it from the core's file browser.",
    ],
    "apple2": [
        "Needs `apple2e.rom` (16,384 bytes; a file whose reset vector is not `$FA62` is refused) and",
        "`apple2e-video.rom` (8,192 bytes) in `cores/apple2/roms/`. `diskii.rom` (256 bytes, the Disk II controller ROM)",
        "is optional for starting, but **mounting any disk image needs it**: without it the mount is refused with a",
        "message, and the ROM is picked up on the next ROM reload.",
        "Where to get them: **there is no free legal source for the Apple IIe ROMs.** Dump them from your own",
        "machine (and your own Disk II card). We cannot point you to a download.",
    ],
    "zx": [
        "Needs the ROM of the model you want in `cores/zx/roms/`: `48.rom` (16,384 bytes, the 48K BASIC) for the 48K and",
        "`128.rom` (32,768 bytes, ROM 0 and ROM 1 one after the other) for the 128K. **One of the two is enough** to start:",
        "if the **Model** setting names the model whose file is missing, the other runs and the screen says so. A file of",
        "the wrong size is reported at once with its size, a missing one after about 10 seconds.",
        "Where to get them: **we cannot point you to a download.** Dump them from your own machine.",
    ],
    "cpc": [
        "Needs the files of **one model** in `cores/cpc/roms/` (16,384 bytes each): the 464 `os464.rom` and `basic464.rom`; the 664",
        "`os664.rom`, `basic664.rom` and `amsdos.rom`; the 6128 `os6128.rom`, `basic6128.rom` and `amsdos.rom`. One complete",
        "set is enough to start; if the **Model** setting names a model whose files are not all there, the highest model that",
        "is complete runs and the screen says so. Without `amsdos.rom` no disk can be mounted (the 464 has no drive).",
        "Where to get them: **we cannot point you to a download.** Dump them from your own machine.",
    ],
    "neogeo": [
        "Needs a **BIOS** (`sp-s2.sp1` for the arcade, `neo-epo.sp1` for the console, or `uni-bios.rom`), the zoom table `000-lo.lo` and the board fix ROM",
        "`sfix.sfix` in `cores/neogeo/roms/`, and `romsets.xml` (the list of the files of each game set). A game is a `.neo` file (choose it in the Files page) or a",
        "folder of a cartridge's files (**Load a ROM set from a folder**, the set's name from `romsets.xml`). Backup RAM and memory card are saved to",
        "`cores/neogeo/saves/<game>.sav`. Nothing of the Neo Geo's system or games is shipped here.",
    ],
    "diag": [
        "Needs no ROM file. The core has no machine: it measures the joystick and paddle ports from test files (`.toml`, in `cores/diag/tests/` on the stick or the card).",
        "The example tests are written there at the first start (a file that is there already is never overwritten, so an edited test stays).",
        "A test says what to plug in, what to press and what to measure; the result is written next to it in `cores/diag/results/` as a `.toml` and a readable `.txt`.",
    ],
    "zxnext": [
        "Needs **one SD card image** as `cores/zxnext/roms/next.img` on the stick or the card (or choose a `.img` in the Files page and pick",
        "**Mount SD image and reset**). The image is the whole card as the Next reads it (the partition table, the FAT with the operating",
        "system files); the machine's own boot ROM is in the core and loads `TBBLUE.FW` from it. Nothing of the Next's system is shipped here.",
        "What the machine writes to its card goes back into the file. Without an image the boot ROM stops with \"Error initializing SD card!\".",
    ],
    "snes": [
        "Needs no ROM file: the 64 byte boot ROM of the console's sound processor is embedded in the application. Games are `.sfc` / `.smc` files chosen in the F3 Library.",
        "Battery saves go to `saves/<game>.srm` in the core's folder. Cartridges with an SA-1, Super FX, Cx4, SPC7110, S-DD1 or ST010/ST011 are refused.",
        "**DSP-n cartridges** (DSP-1/1B, DSP-2, DSP-3, DSP-4: Super Mario Kart, Pilotwings, Dungeon Master, SD Gundam GX, Top Gear 3000) run, but the chip's firmware is not part of this release and never will be:",
        "you supply it as 8192 bytes. The core takes it from the end of the `.sfc` when the file is a power-of-two ROM size plus 8192 bytes; otherwise from a file in `cores/snes/roms/` or `roms/snes/` on any medium:",
        "`dsp1b.bin` (else `dsp1.bin`) for DSP-1, `dsp2.bin`, `dsp3.bin`, `dsp4.bin`. A firmware file with the words high byte first is recognised and turned around. The chip is chosen from the game's title;",
        "the setting **DSP firmware** (F5, takes effect when the game is run again from F3; a Reset alone keeps the chip) overrides it (Auto, DSP-1 ... DSP-4). Without a firmware file the game is refused with a message that names the file and the folder.",
    ],
}

MEDIA = {
    "c64scpu": "`.prg` `.p00` (Load, Load + run: into the 65816's own RAM), `.d64` `.g64` on drive 8 (the 1541), `.d81` on the 1581; `.rom` sets the 1541 ROM.",
    "amiga1200": "`.adf` (Mount DF0 ... DF3), `.hdf` (Mount HD0, HD1), or a folder as a hard disk (Mount folder as HD0/HD1).",
    "c128": "`.d64` `.g64` on drive 8 (the 1541), `.d81` on the 1581; `.rom` loads a system ROM image.",
    "vc20": "`.prg` `.p00` (Run PRG: reset and run; Load PRG: into the running machine, nothing typed). The RAM expansion is a setting in F5. No disk drive yet.",
    "pet": "`.prg` `.p00` (Run PRG, Load PRG), `.d64` on the 4040, `.d80` `.d82` on the 8250, `.tap` (Datasette).",
    "c264": "`.prg` `.p00` (Run PRG, Load PRG), `.bin` of 16 or 32 KB (Load as cartridge: the Plus/4 model). No disk drive yet.",
    "amiga": "`.adf` (Mount DF0 ... DF3) and `.hdf` (Mount HD0, HD1) from the **F3 Library**.",
    "atari800": "`.atr` `.xfd` (Mount D1: ... D4:; the first entry also cold-starts), `.xex` `.obx` `.com` (Run), `.car` `.rom` `.bin` (cartridge).",
    "nes": "`.nes` (Run).",
    "ddt_mandel": "none (no disk, no tape, no file).",
    "ddt_novic": "none (no disk, no tape, no file).",
    "diag": "`.toml` test files from `cores/diag/tests/` (a test says what to plug in and press); results go to `cores/diag/results/`.",
    "apple2": "`.dsk` `.do` `.po` `.nib` (Mount D1: and cold start, Mount D1:, Mount D2:).",
    "zx": "`.sna` `.z80` (Run snapshot: the machine starts where the snapshot was taken), `.tap` `.tzx` (Insert tape, then **Tape: Play** in the Tape row of F1; real speed).",
    "cpc": "`.dsk` (Mount A: and reset, Mount A:, Mount B: and reset, Mount B:; the F1 drive list mounts into an empty drive with the reset and replaces the disk of a filled drive without it, so a game of several disks keeps running; what the disk drive writes goes back into the file), `.cdt` (Insert tape, then **Tape: Play** in the Tape row of F1; real speed).",
    "snes": "`.sfc` `.smc` (Run; the battery RAM is saved as `.srm`); cartridges with a DSP-n chip need its firmware (section 5).",
    "neogeo": "`.neo` (Run); a folder set through the task Load a ROM set from a folder.",
    "zxnext": "`.img` (Mount SD image and reset, Mount SD image; the whole card, written back into the file).",
}

KNOWN = {
    "c64scpu": "**C64 with SuperCPU (large core, experimental):** *Run Disk* types `LOAD\"*\",8,1` and `RUN` through the keyboard matrix at READY (no board pass yet); *Load + run* of a `.prg` on the stick and *Mount & Run* of one inside a disk image place it into the 65816's own RAM and type RUN or SYS (Load + run: board pass 2026-11-15). *Metal Dust* scrolls without picture faults (board 2026-11-15). A large core replaces the whole FPGA: power off and on to come back.",
    "atari800": "**Atari 800 XL:** a mounted `.atr` boots. Some executables (the trainer demo) show a broken scroller and ignore input.",
    "apple2": "**Apple IIe:** swapping the disk in D1: while a game runs is not tested on the board yet.",
    "pet": "**Commodore PET (large core, experimental):** starts on the board and runs demos (2026-11-11); the models and the cassette are not checked yet. Needs your own PET ROMs. A large core replaces the whole FPGA: power off and on to come back.",
    "c264": "**Commodore 264:** runs on the board with sound (Plus/4). Cartridges are not tested. The 1541 disk drive is not there yet.",
    "nes": "**NES:** the picture does not fill as much of the screen as it could.",
    "zx": "**ZX Spectrum:** 128K shows a picture on the board; **48K stays black** (the fix is being built). Tapes with pulses shorter than 16 T-states (turbo loaders) are refused; there is no tape SAVE.",
    "amiga1200": "**Amiga 1200 (large core):** AmigaVision runs on the board with sound. A large core replaces the whole FPGA: to come back to the boot menu, switch the power off and on.",
    "c128": "**Commodore 128 (large core):** C128 and C64 mode with picture and sound on the board. A large core replaces the whole FPGA: power off and on to come back. *Busted Boulders* stops after the title screen (the left column is missing).",
    "cpc": "**Amstrad CPC:** on the board 2026-11-18: the 464, 664 and 6128 boot, disks mount with and without a reset, CAT, programs load and run, SAVE writes back to the .dsk. There is no tape SAVE and no snapshot.",
    "neogeo": "**SNK Neo Geo (large core):** never on the board. The machine is held while a sprite or fix word is late from the DDR2 (about 210,000 words in a 320 ms bench run); whether that is visible is open. A large core replaces the whole FPGA: power off and on to come back.",
    "zxnext": "**ZX Spectrum Next:** never on the board; the CPU runs from the DDR2 behind a cache and waits on a miss, so the speed of a program that does not fit the cache is below the real machine's. 128K, +3 and Pentagon timing run 1.5 to 2.7 % fast (frame lock); no Sega 6-button pad, no SD pass-through.",
    "snes": "**Super Nintendo (large core):** The ROM and RAM are in the DDR2 behind caches and the machine waits on a miss (about 1 % in the benches); no SA-1, Super FX, Cx4, SPC7110, S-DD1 or ST010/ST011 cartridges (a DSP-n game needs its firmware, section 5; Super Mario Kart with DSP-1 runs on the board); the keys for the buttons are a proposal.",
    "vc20": "**VIC-20:** the common application is never on the board for it; the board retest of the current version is still open.",
}


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def cor_meta(path):
    out = subprocess.run([sys.executable, os.path.join(HERE, "cor_pack.py"), "check", path],
                         capture_output=True, text=True, check=True).stdout
    return json.loads(out)


def render(a, cors, bit_len, alen, dest):
    """The text of INSTALL.md and the names of the cores it has no ROM notes for."""
    L = []
    w = L.append
    present, unknown = [], []
    for f, m in cors:
        c = core_of(m["name"])
        if c is None:
            unknown.append(m["name"])
        elif c not in present:
            present.append(c)

    w("# TRX64 Ultimate multi-core bundle %s" % a.id)
    w("")
    w("One host, %d cores. The update file installs the host into the flash and copies the cores that lie beside it;" % len(cors))
    w("your old firmware stays as a core and keeps starting first. This file is the whole guide: what to download, where")
    w("to put it, and what to press.")
    w("")
    w("This bundle contains **no ROM, Kickstart or disk image** that you have to supply or copy; you bring those yourself (section 5).")
    w("")
    trx = {os.path.basename(f) for f, m in cors}
    if "trx64g.cor" in trx:
        w("The exceptions are inside the TRX64 files: `trx64g.cor` carries the C64 BASIC ROM and the MOD player cartridge image that Gideon's")
        w("Ultimate application links, as his firmware does.")
        w("")
    w("## 1. What you need")
    w("")
    w("- A C64 Ultimate (C64U). It is the only board on which the firmware is known to work. A C64C Ultimate can contain")
    w("  the XC7A100T FPGA. The firmware does not work with the XC7A100T.")
    w("- A USB stick formatted **FAT32 or exFAT**. An SD card works for cores and ROMs too.")
    w("- An HDMI screen. The picture is HDMI only: this bitstream has no analog video pins, so composite")
    w("  and S-video stay black with every core.")
    w("- A PC to prepare the stick. Optional: a network cable, for the web page and FTP.")
    w("- A USB-Blaster, only for the way back if something goes wrong. You do not need it to install.")
    w("")
    w("## 2. What is in here")
    w("")
    w("| File | What it is |")
    w("|---|---|")
    w("| `%s.cfw` | the installer, for the file browser of a machine that runs our firmware |" % a.name)
    w("| `%s.ue2` | the same package without the pre-header, for a machine that still runs Gideon's |" % a.name)
    if getattr(a, "txu", ""):
        w("| `%s` | the same host as an update for a board that already runs a TRX64 MultiCore host (section 3a) |" % a.txu)
    for f, m in cors:
        if m.get("kind") == "large":     # T115 7: a whole bitstream, started on its own (the host reloads itself after it)
            w("| `%s` | %s %s, **large core**: a full bitstream of %s bytes + its application %s bytes |"
              % (f, m["name"], m["version"], format(m["bit_size"], ","), format(m["app_size"], ",")))
        else:
            w("| `%s` | %s %s, partial %s bytes + its application %s bytes |"
              % (f, m["name"], m["version"], format(m["partial_size"], ","), format(m["app_size"], ",")))
    u2as = sorted(f for f in os.listdir(a.cor_dir) if f.endswith(".u2a"))   # T180 D9: applications every core reads
    for f in u2as:
        w("| `%s` | an application for every core: **Unzip here** on a `.zip` in the F3 browser (section 4) |" % f)
    w("| `SHA256SUMS` | checksum of every file above |")
    w("")
    w("The installer writes two things, nothing else in the flash changes:")
    w("")
    w("- flash `0x000000`: the full bitstream, %s bytes as its own header declares. It holds **layer 1"
      % format(bit_len, ","))
    w("  (the host) and an empty core, the null core, in one image**: the null core is no machine, it only")
    w("  answers what the boot menu asks of the core partition (identity, host key, the picture layer, the")
    w("  menu's interrupt). Every machine, the TRX64 C64 included, starts from its own `.cor`.")
    w("- flash `0x220000`: the **host boot app**, %s bytes, loaded to and started at `0x%05X`. It is not the"
      % (format(alen, ","), dest))
    w("  Ultimate app: it is the boot menu that reads a `.cor` and starts that core.")
    w("")
    if re.match(r"^[0-9]+\.[0-9]+\.[0-9]+$", a.id):   # a release names its tag, no commit (Alex 2026-11-18: "with a tag it becomes the tag name, EVERYWHERE")
        w("Built from the tag `v%s`." % a.id)
    else:
        w("Commits: bitstream `%s`, app `%s`." % (a.bit_commit, a.app_commit))
    w("")
    w("## 3. Install the host")
    w("")
    w("1. Unzip the whole bundle into **one folder** on a USB stick or the SD card, e.g. `/TRX64-MC/`: the update file and")
    w("   the `.cor` files side by side. The installer copies every `.cor` and `.cfw` that lies beside it into `/cores`, and every `.u2a` (Unzip) into `/cores/apps`.")
    w("2. In the file browser of the firmware the machine runs now, open `%s.ue2` (stock firmware: Gideon's, or" % a.name)
    w("   Commodore's 1.1.0, which takes no `.cfw`) or `%s.cfw` (our firmware) and pick **Run Update**." % a.name)
    w("3. Two questions: you leave the warranty of Gideon and Commodore. Answer both with **Yes**; any **No** goes back to")
    w("   the menu with nothing written.")
    w("4. Pick the medium for the cores: **SD** or **USB**. On it the installer creates `/cores`, and writes:")
    w("   - `TRXBK_<date>_nnn.BIN` and `TRXBK_<date>_nnn.CFW`: what was in the flash before; the `.CFW` reinstalls it;")
    w("   - the old firmware as a core, e.g. `commodore-110.cor` or `gideon-315a.cor` (recovery icon in the menu);")
    w("   - the cores that lay beside the update file;")
    w("   - `/Flash/boot.cfg`: the old firmware as the default, the copied cores as further entries, direct start on.")
    w("   Then it writes the flash. Do not pull the medium or switch off meanwhile.")
    w("5. Unplug a JTAG adapter, then **power-cycle the board**. The first start copies the old firmware's bitstream into")
    w("   the flash once (shown on screen); then your old firmware starts straight away, as before.")
    w("6. **Hold C= (or fire on joystick port 1) while switching on** to get the boot menu with all cores instead.")
    w("   F8 Settings switches the direct start off if you want the menu every time.")
    w("")
    w("The way back: start the old firmware's `.cor` from the menu and run an update file it accepts with **Run Update**:")
    w("Gideon's firmware takes the backup `TRXBK_..CFW`; Commodore's 1.1.0 takes only a `.ue2`, so use Commodore's own")
    w("1.1.0 update file there. By cable: a USB-Blaster and `openFPGALoader -c usb-blaster --fpga-part xc7a50tfgg484 -f <bitstream>`.")
    w("")
    if getattr(a, "txu", ""):
        # T152: the host's own File Browser installs a .txu; a .cfw opened there starts as a core, it does not install
        w("### 3a. Already on a TRX64 MultiCore host (an earlier bundle)")
        w("")
        w("**Order matters: update the host first, then copy the cores.** A `.cor` of this bundle can carry a record an older host does not know (`diag.cor` has record 8, the files it installs on the card); an older host refuses it.")
        w("")
        w("1. Copy `%s` anywhere on the SD card or stick. Do not use the `.cfw` here: the host's File Browser starts a `.cfw` as a core." % a.txu)
        if u2as:   # the .txu writes the flash only; it copies no file (#43: the tester who updated this way had no Unzip)
            w("   Also copy `%s` into `/cores/apps/` (create the folder): the update does not copy it." % "`, `".join(u2as))
        w("2. Host menu, **F3**, open `%s`. It shows what it writes (only the parts that changed) and asks; answer **Yes**." % a.txu)
        w("3. When it reports the flash written and verified, power-cycle the board. A host from this bundle on restarts by itself.")
        w("4. Only now copy every `.cor` of this bundle into `/cores`, replacing the old ones.")
        w("")
    w("## 4. The stick")
    w("")
    w("Prepare the stick (or the SD card) on the PC like this:")
    w("")
    w("```")
    w("/cores/                      the .cor files of this bundle, and any .cfw full firmware")
    w("    trx64u.cor  trx64g.cor  vc20.cor  amiga.cor  ...")
    if u2as:
        w("    apps/                    applications every core reads (T180 D9), from this bundle; never a core's folder:")
        w("        unzip.u2a            Unzip here / Unzip to folder / Extract one file, on a .zip in the F3 browser")
        if "miniz.u2a" in u2as:
            w("        miniz.u2a            the zip library Unzip loads (T197); it has no menu entry, and any application that reads a .zip can use it")
    w("    <name>/                  one folder per core, made by the core at its first start; never part of a release:")
    w("        <name>.cfg           its settings")
    w("        roms/                its ROM files (section 5)")
    w("        saves/               what it saves")
    w("anywhere                     your media: .adf .hdf .atr .xex .d64 .prg .nes .dsk ...")
    w("```")
    w("")
    w("- **Cores:** the installer enters the cores it copied (section 3). Later cores you add yourself with **A** in the")
    w("  File Browser (F3), from any folder of the USB stick or the SD card; it does not search the media. One folder per core:")
    w("  `/cores/<name>.cor`; its folder `/cores/<name>/` (settings, `roms/`, `saves/`) appears beside it at the first start.")
    w("- **ROMs:** a core reads its ROM files from `roms/` in its own folder and nowhere else; no file is shared between cores.")
    w("  A file it finds missing is copied once, verified, from the old places (`roms/<folder>/` on `/flash`, `/usb0` ... `/usb3`,")
    w("  `/sd`, and a `roms/` folder beside the `.cor` for a file the core names). `/flash/roms` is only read, never written.")
    w("  A release copied over an older one leaves the folders as they are. F8, \"Cores and ROMs\", shows what is missing and what is double.")
    if u2as:
        w("- **Unzip (all cores):** the installer (section 3) writes `unzip.u2a` to `/cores/apps/unzip.u2a`; after the update in 3a, or by hand, copy it there yourself; every core finds it there, no copy per core folder. A `unzip.u2a`")
        w("  in a core's own `/cores/<name>/apps/` wins over that one. Then ENTER on a `.zip` in the File Browser (F3) offers **Unzip here**, **Unzip to folder**")
        w("  and **Extract one file**.")
        if "miniz.u2a" in u2as:
            w("  Unzip needs the zip library `miniz.u2a` in the same folder (`/cores/apps/` or the core's own `apps/`); copy both.")
    w("- **Media** (disks, games, images) can lie anywhere; you browse to them from the core's menu.")
    w("- **Settings:** the cores on the common application (Amiga, Amiga 1200, Atari 800 XL, NES, Apple IIe, ZX Spectrum, Amstrad CPC, ZX Spectrum Next, VIC-20, Commodore 264) keep")
    w("  them in `<name>.cfg` in the core's folder, `/cores/<name>/<name>.cfg`.")
    w("- **Full firmwares** (`.cfw`, section 8) lie beside the cores.")
    w("")
    w("## 5. ROMs per core")
    w("")
    w("This bundle contains **no ROM, Kickstart or disk image**. The C64 cores (TRX64 and C64U) use the ROMs already")
    w("in the board's flash (`/Flash/roms`), so nothing has to be downloaded for them; the TRX64 core copies them into its own folder at its first start.")
    w("A core that misses a required ROM stays **held** (no picture of the machine) and says so on the screen after")
    w("about 10 seconds. Put the file on the stick: the core keeps looking and starts by itself.")
    w("")
    w("| Core | Folder | File names and sizes | Needed |")
    w("|---|---|---|---|")
    for c in present:
        folder = c["folder"] if c["folder"].startswith("(") else "`cores/%s/roms/`" % c["id"]
        w("| %s | %s | %s | %s |" % (c["title"], folder, c["files"], c["needed"]))
    w("")
    for c in present:
        w("### %s" % c["title"])
        w("")
        for line in PARAS[c["id"]]:
            w(line)
        w("")
    for name in unknown:
        w("### %s" % name)
        w("")
        w("This guide has no ROM notes for this core. Start it: if it needs a file, it says which and where.")
        w("")
    w("These are the only sources named, because they are legal ones:")
    w("")
    w("- VICE (%s): its distribution holds the Commodore ROM images for the VIC-20 and the Plus/4." % VICE)
    w("- Amiga Forever (%s): Cloanto's licensed Kickstarts. C64 Forever (%s) is their C64 package." % (AMIGA_FOREVER, C64_FOREVER))
    w("- Altirra (%s): the free AltirraOS and AltirraBASIC for the Atari." % ALTIRRA)
    w("- Apple IIe: your own machine. There is no legal download.")
    w("")
    w("## 6. First start")
    w("")
    w("The host boots into its menu. Four pages, on the F keys of the C64 keyboard or a USB keyboard:")
    w("")
    w("| Key | Page | What it does |")
    w("|---|---|---|")
    w("| **F1** | Boot menu | UP/DOWN select, RETURN starts the core, **R** checks the selected entry again |")
    w("| **F3** | File Browser | the media and their files: RETURN opens a folder, **A** adds a `.cor` or `.cfw` to the menu (it reads the manifest and writes `/Flash/boot.cfg`), **S** starts it once, **P** packages a `.cfw` or `.ue2` as a `.cor` (section 8). On the list of media **F** formats the selected one as exFAT (images above 4 GiB need it): it asks for the medium's name and for RETURN twice |")
    w("| **F5** | Edit menu | **+/-** move, **D** make default, DEL twice removes, LEFT/RIGHT sets the countdown (0 to 60 s) |")
    w("| **F7** | Network | the address of the board |")
    w("")
    w("1. Plug in the stick. Press **F3**, browse to `cores`, press **A** on the `.cor` files you want.")
    w("2. **F5**: choose the auto core and the countdown. The auto entry starts when the countdown ends; any key")
    w("   stops it. If the auto core's stick is not there, the host waits up to 30 seconds for it.")
    w("3. **F1**, select a core, RETURN.")
    w("4. **No picture?** Press **V** in the menu. It steps through the resolutions one by one, applies each at")
    w("   once and saves the one that works (`video=<system>,<hdmi>` in `/Flash/boot.cfg`, which you can also edit over")
    w("   FTP). The menu asks the screen what it can do and uses 1080p, else 720p, else 576p/480p; a screen")
    w("   without a CEA block gets 1024 x 768.")
    w("5. **The network.** With an address the board serves a web page on port 80 (the boot menu, the media, the")
    w("   picture, start and add) and the same facts as JSON at `/v1/status`; the FTP server on port 21 is for files.")
    w("   Neither asks for a password, on purpose: use a network you trust. The top bar says whether the board has an")
    w("   address.")
    w("")
    w("Old menu entries that point at a `.cor` of an older host keep working and are not refused; replace or delete")
    w("them yourself. A `.cor` of this bundle only fits **this** host.")
    w("")
    w("## 7. In a core")
    w("")
    w("**Open the menu** with the Menu button on the board (the rocker). The cores on the common application")
    w("(Amiga, Amiga 1200, Atari 800 XL, NES, Apple IIe, ZX Spectrum, Amstrad CPC, ZX Spectrum Next, VIC-20, Commodore 264) also open it with **F10**, **Scroll Lock** or the **Menu** key of a")
    w("USB keyboard and close it with **Esc** or **F10**; the machine stands still while it is open. TRX64 and C64U")
    w("show the Ultimate menu.")
    w("")
    w("The common menu: **F1** quick actions (the core's resets and reloads on top, then one row per drive or slot: RETURN on it opens")
    w("the list for that drive, e.g. mount from the Library, create a blank image, eject, tape controls; a line with the machine's model and memory; save and load")
    w("the core's config), **F3** Library (the file browser: ENTER on a file offers what to do with it), **F5** the machine (its settings: video, audio, input),")
    w("**F7** app settings (storage, where the menu opens, hotkeys; a setting that also exists in the host starts with the host's value),")
    w("**F8** System (build version, ROMs, libraries, network, **Reset to host defaults** - every setting of the core back to the host's values -, return to the host menu).")
    w("")
    w("**Media per core:**")
    w("")
    for c in present:
        w("- **%s:** %s" % (c["title"], MEDIA[c["id"]]))
    w("")
    w("No core mounts a disk, an `.adf` or an `.hdf` by itself: mount what you want from the menu.")
    w("")
    w("**Back to the host menu:** in the common menu, **F8** System, \"Return to host menu\" - no power cycle needed.")
    w("TRX64 and C64U: switch the power off and on.")
    w("")
    w("## 8. Full firmwares (.cfw)")
    w("")
    w("A `.cfw` beside the cores shows up in the browser (F3) and can be added like a core. Tested: Gideon's original")
    w("3.15a and the TRX64 Ultimate rc1. The host keeps its bitstream as one file, `/Flash/legacy/fullcore.bit` (a new")
    w("file takes about 40 s the first time, then about 10 s), and hands over. Switch the power off and on to come back")
    w("to the host. Running Gideon's own updater from there replaces this host.")
    w("")
    w("Gideon's firmware is not part of this release and never will be: get it from the firmware page on")
    w("https://ultimate64.com")
    w("")
    w("A `.cfw` gets a small logo in the menu. Optional: a file `<name>.icon` beside it (`<name>` is the name of the `.cfw`,")
    w("32 x 32 pixels, RGB565 little endian, exactly 2048 bytes) replaces the built-in one; a file of any other size is ignored.")
    w("")
    w("A `.cfw` or a vendor's `.ue2` becomes a core of its own: in the File Browser (F3) select it and press **P** (Package). The host writes")
    w("`cores/<name>.cor` on the same medium (the folder is made when it is missing), with the logo, the name and the checksums of")
    w("both parts. The new file shows as `foreign` in the menu and starts like any large core. Your firmware file stays as it is,")
    w("and nothing is written to the flash. A `.cor` made this way is for your own board only: do not hand it on.")
    w("")
    w("## 9. Known problems")
    w("")
    w("- Every core except TRX64 is experimental: expect rough edges.")
    for c in present:
        if c["id"] in KNOWN:
            w("- " + KNOWN[c["id"]])
    if any(c["id"] == "nes" for c in present):
        w("- The NES reads the C64's own hardware: joystick 1 and 2 give the directions and A, **space** is B")
        w("  (right shift on port 2), **F3** is Select (F7), **F1** is Start (F5). No USB pad yet.")
    w("")
    w("## 10. Reporting")
    w("")
    w("Send us:")
    w("")
    w("- a **photo of the screen**;")
    w("- the **core and its version** (the boot menu shows them; F8 System shows the app build; F8's Bug report has core, core version")
    w("  and build) and this bundle's id: `%s`;" % a.id)
    w("- what you did, step by step, and what you expected;")
    w("- for a ROM problem: the message on the screen, and the file name and size on your stick.")
    w("")
    w("## 11. Checksums")
    w("")
    w("```")
    for f in sorted(os.listdir(a.cor_dir)):
        if f not in ("SHA256SUMS", "INSTALL.md") and not f.startswith("."):
            w("%s  %s" % (sha256(os.path.join(a.cor_dir, f)), f))
    w("```")
    w("")
    w("For developers: bundle T94, generic application T104, core folders T112, full firmwares T115.")
    w("")
    text = "\n".join(L)
    bad = bad_links(text)
    if bad:
        raise SystemExit("INSTALL.md would link outside the allow-list: %s" % ", ".join(bad))
    return text, unknown


def main():
    p = argparse.ArgumentParser()
    for o in ("--id", "--name", "--bit", "--app", "--bit-commit", "--app-commit", "--cor-dir", "-o"):
        p.add_argument(o, required=True)
    p.add_argument("--txu", default="", help="file name of the host's .txu in the bundle (T152), if it ships")
    a = p.parse_args()

    bit = open(a.bit, "rb").read()
    app = open(a.app, "rb").read()
    dest, alen, run = trx_pkg.check_app_record(app)
    bit_len = trx_pkg.bit_length_from_header(bit)
    cors = []
    for f in sorted(os.listdir(a.cor_dir)):
        if f.endswith(".cor"):
            cors.append((f, cor_meta(os.path.join(a.cor_dir, f))))
    text, unknown = render(a, cors, bit_len, alen, dest)
    for u in unknown:
        print("warning: no ROM notes for the core '%s' (tools/release_notes_mc.py CORES)" % u, file=sys.stderr)
    open(a.o, "w", encoding="utf-8").write(text)
    print("%s: %d lines, %d cores" % (a.o, text.count("\n") + 1, len(cors)))


if __name__ == "__main__":
    main()
