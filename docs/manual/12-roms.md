# 12. ROMs

## 12.1 General

12.1.1 This firmware contains **no ROM, Kickstart or disk image**. You bring your own.

> **NOTE.** We do not point to download or archive sites for ROM images. Only the legal sources in 12.3 are named.

12.1.2 The C64 cores (`trx64u.cor`, `trx64g.cor`) use the ROMs that are already in the board's flash. Nothing to bring for them. The TRX64 core copies
them from the flash into its own folder at its first start (12.2.3); the flash itself is never written.

## 12.2 Where the files go

12.2.1 Every core has **one folder of its own** beside its `.cor`: for `/SD/cores/apple2.cor` it is `/SD/cores/apple2/`. The core makes it at its
first start, with `roms/` and `saves/` inside, and keeps its settings there as `apple2.cfg`. The ROMs of a core go into `roms/` of this folder
and nowhere else:

```
/SD/cores/apple2.cor          the core (shipped; copy a new release over it)
/SD/cores/apple2/
    apple2.cfg                its settings
    roms/                     its ROM files (this chapter)
    saves/                    what it saves (battery RAM, snapshots)
```

12.2.2 **No file is shared between cores.** Two cores that need the same ROM (the 1541 ROM of the C64-family cores, a Kickstart for the Amiga and
the Amiga 1200) each have their own copy. A core never reads a file from another core's folder or from a shared `roms/` folder.

12.2.3 **Files you already have in the old places are copied once.** When a core finds a ROM missing in its folder, it looks in the places where
earlier versions read from: `roms/<folder>/` on `/flash`, `/usb0` to `/usb3` and `/sd` (for the Apple IIe on the SD card: `/SD/roms/apple2/`), and a `roms/` folder beside the `.cor` (`/SD/cores/roms/`; only for a file
whose name the core knows and whose size is right). It **copies** the first file it finds into its folder, reads the copy back and compares it
(size and SHA-256) with the original, and writes a line to the log. The original is not touched; from then on the core uses its own copy only.
`/flash/roms` belongs to the firmware of the board: it is read as a source of this copy and never written.

12.2.4 A core that misses a required file stays held. After about 10 seconds the screen says which file is missing and the one folder it belongs
in, for example `apple2e-video.rom is missing in /SD/cores/apple2/roms/`. Put the file there. The core looks again by itself.

12.2.5 **Update.** Copy the `.cor` files of a new release over the old ones. The folders (settings, ROMs, saves) are not part of a release and
stay as they are.

12.2.6 **Tidy up.** The page `Cores and ROMs` (F8 Settings, 8.7) shows for every core on the media whether its folder is there, which ROMs are
present or missing, and how many saves it has. It lists the files that belong to no core and the files that exist more than once, and it
deletes the files that belong to no core. It never deletes a `.cor`, saves or anything in the folder of a core that is there.

12.2.7 The files of each core:

| Core | Folder (beside `<name>.cor`) | Files | Needed |
|---|---|---|---|
| TRX64 (`trx64u.cor`, `trx64g.cor`) | the board's flash (TRX64: copied into `cores/trx64/roms/`) | Kernal 8,192, Basic 8,192, Char 4,096 bytes | Already on the board |
| VIC-20 | `cores/vc20/roms/` | `kernal.bin` 8,192, `basic.bin` 8,192, `char.bin` 4,096; the drive: `1541.rom` (16,384 or 32,768; also `1541.bin`, `1541-ii.bin`, `dos1541.rom`) | All three; the drive ROM is optional |
| Commodore 264 | `cores/c264/roms/` | `kernal.bin`, `basic.bin` 16,384 each; Plus/4: `3plus1lo.bin`, `3plus1hi.bin` 16,384 each; the drive: `1541.rom` as for the VIC-20 | Kernal and Basic; the function ROMs for the Plus/4; the drive ROM is optional |
| Amiga, Amiga 1200 | `cores/amiga/roms/`, `cores/amiga1200/roms/` | any `.rom` of 262,144 or 524,288 bytes (Kickstart) | One Kickstart |
| Atari 800 XL | `cores/atari800/roms/` | `ATARIXL.ROM` 16,384; `BASIC.ROM` or `ATARIBAS.ROM` 8,192 | Both |
| NES | - | none | Nothing |
| Apple IIe | `cores/apple2/roms/` | `apple2e.rom` 16,384; `apple2e-video.rom` 8,192; `diskii.rom` 256 | The first two; the third for any floppy |
| ZX Spectrum | `cores/zx/roms/` | `48.rom` 16,384; `128.rom` 32,768 | One of the two |
| ZX Spectrum Next | `cores/zxnext/roms/` (else `roms/zxnext/` of a medium) | `next.img`: a whole SD card image (FAT, MBR). The image is used where it lies, it is not copied | The image |
| SNK Neo Geo | `cores/neogeo/roms/` | `sp-s2.sp1` or `neo-epo.sp1` or `uni-bios.rom` (131,072 each); `000-lo.lo` 65,536 (or 131,072); `sfix.sfix` 131,072; `romsets.xml` (the file list of the game sets); the games as `.neo` files or folders | A BIOS, the zoom table and the fix ROM |
| Commodore 128 | `cores/c128/roms/` | `boot0.rom` (or `c128.rom`, `c128-system.rom`, `c128_system.rom`), 73,728 bytes; the drives: `1541.rom`, `1581.rom`; JiffyDOS (optional, files you select yourself, 11.14.4) | The system ROM; the drive ROMs and the JiffyDOS files are optional |
| Super Nintendo | - | none (the `.sfc` or `.smc` game is the media) | Nothing |
| Showcase demos, Port Diagnostics | - | none | Nothing |
| Amstrad CPC | `cores/cpc/roms/` | `os464.rom`, `basic464.rom`; `os664.rom`, `basic664.rom`, `amsdos.rom`; `os6128.rom`, `basic6128.rom`, `amsdos.rom` (16,384 each) | The files of one model |

## 12.3 Legal sources

12.3.1 **VICE** (https://vice-emu.sourceforge.io/). The distribution holds the Commodore ROM images in `data/VIC20/` and
`data/PLUS4/`.

- VIC-20: PAL Kernal `901486-07`, Basic `901486-01`, character ROM `901460-03`. Rename them to `kernal.bin`, `basic.bin`,
  `char.bin`.
- Plus/4: Kernal `318004-05`, Basic `318006-01`, function ROM `317053-01` (low) and `317054-01` (high). Rename them to
  `kernal.bin`, `basic.bin`, `3plus1lo.bin`, `3plus1hi.bin`.

12.3.2 **Amiga Forever** (https://www.amigaforever.com/). Cloanto's licensed package with Kickstart 1.3 and 3.1.
**C64 Forever** (https://www.c64forever.com/) is their C64 package. Use plain images (12.4.2).

12.3.3 **Altirra** (https://www.virtualdub.org/altirra.html). The free AltirraOS and AltirraBASIC. Use the 16 KB XL image of
AltirraOS as `ATARIXL.ROM` and the 8 KB AltirraBASIC as `BASIC.ROM`. Check the sizes. The file names in the Altirra
package are not ours.

12.3.4 **Apple IIe, ZX Spectrum, Amstrad CPC, ZX Spectrum Next.** There is no legal download that we can point to. Dump the
ROMs from your own machine (and your own Disk II card). For the ZX Spectrum Next, make the SD card image from your own
system files.

12.3.5 **How others source them.** The MiSTer project has core repositories at https://github.com/MiSTer-devel/ for
the same machines. The README of a core repository names the ROM files that the core expects. We name this as a place to
read, not as a source of files.

## 12.4 Notes per core

12.4.1 **Atari.** The core checks the size only, not the content. A file of the wrong size is reported at once with
its size.

12.4.2 **Amiga.** The core needs a plain image. A file that does not begin with `1111` or `1114` is not recognised as a
Kickstart. A file that needs Cloanto's `rom.key` must be converted by you first. Every `.rom` of the right size in the core's `roms/` folder is
offered; a `.rom` of the wrong size is refused with a message.

12.4.3 **Apple IIe.** `apple2e.rom` is refused if its reset vector is not `$FA62`.

12.4.4 **VIC-20.** Other names that the core accepts: `chargen.bin`, `chargen-901460-03.bin`, `basic-901486-01.bin`,
`kernal.901486-07.bin`.

12.4.5 **Wrong file.** If a core does not take a file, the screen shows a message. The message names the file, its size
and the size that is expected.

12.4.6 **Amstrad CPC.** The MiSTer Amstrad core keeps the CPC ROMs together in one `boot.rom`. This core reads the separate files of 11.13.1 only, not `boot.rom`. Cut the 16,384-byte pieces (the OS, BASIC
and AMSDOS of the model) out of that file and name them `os6128.rom`, `basic6128.rom` and `amsdos.rom` (and the same for the 464 and the 664). The core checks the size of each file, not its content.

12.4.7 **Commodore 128.** The system ROM is one image of 73,728 bytes in the layout of MiSTer's `boot0.rom`. JiffyDOS is optional and comes from your own kit (nothing is shipped). The core never searches for its files and does not look at their names: you select each file, on the F5 Settings tab or with **F3** (11.14.4), and "ROM set" chooses whether they are used. The roles and the sizes each takes:
the C128 KERNAL chip (U35), 16,384 bytes, replaces the second 16 KiB of the image, or the C128DCR kit's one file, 32,768 bytes, which holds the C64 part first and replaces both; the C64 part, 16,384 bytes (BASIC and KERNAL) replacing the first 16 KiB, or 8,192 bytes (the C64 KERNAL chip alone) replacing the upper half of that part; the 1541 drive ROM, 16,384 or 32,768 bytes (the 8,192-byte upper half alone is refused); the 1581 drive ROM, 32,768 bytes.
The core checks the size and the content (not empty, a reset vector at $E000 or above, the C64 part holds the C64 BASIC or, as the 8 KiB KERNAL, the C64 cold start $FCE2, and the C128 part does not hold the C64 BASIC). A file that fails is not taken and the reason is shown. This layout is not yet checked against real kit files.

12.4.8 **Neo Geo.** The BIOS files are 131,072 bytes. `romsets.xml` is your file: it names the files of each game set. A game as a `.neo` file needs the entry of the same name in it.
