# 0056. Each Amiga core picks its own Kickstart, and no core comes up with a disk in the drive

Date: 2026-10-16

Status: Accepted (answers the first open question of [T104](../specs/done/T104-generic-core-app.md) §5.7)

## Context

Both Amiga applications are one source built twice (`AMIGA1200_APP`). Its Auto rule for the Kickstart is
one line for both builds: the first file whose **name** contains "1.3" or "13", else the first by name
(`amiga.cc:386-398`). So the AGA core boots a 1.3 ROM whenever one lies in the ROM directory, while
`cores/amiga1200/core.toml:19` names Kickstart 3.1.

The name is not a usable key either. Measured on Alex's own set in
`~/Development/C64/Tools/amiga-roms` (2026-10-16, `xxd -l 16`):

| File | Size | Magic | Version word at `0x0C` | Is |
|---|---|---|---|---|
| `kick13.rom` | 524288 | `1111` | `0022 0005` | Kickstart 1.3 (34.5) |
| `AmigaVision.rom` | 524288 | `1114` | `0028 0044` | Kickstart 3.1 (40.68) |
| `MegaAGS-Kickstart.rom` | 524288 | `1114` | `0028 0044` | Kickstart 3.1 (40.68) |

Two of the three 3.1 images carry no version in their name, and `kick13.rom` carries one that the
current rule matches. The version is in the image, four bytes in, and it is unambiguous.

The same application also mounts `df0.adf` by itself at start-up if it finds one beside the Kickstart or
in a drive root (`auto_mount`, called at `amiga.cc:1024`).

## Decision

Alex, 2026-10-16: "Amiga 500 usw sollte 1.3 laden und KEINE disk fest eigelegt, nur das Kickstart /
Amiga AGA (1200/4000) sollte 3.1 laden und KEINE disk fest eingelegt" (translated: the Amiga 500 and its
kin load 1.3 and have **no** disk inserted, only the Kickstart; the AGA machines load 3.1 and have no
disk inserted).

- The Auto rule reads the version word at offset `0x0C` of each candidate: the OCS/ECS build wants major
  34 or below, the AGA build wants major 39 or above. A named file in the settings always wins over Auto.
- No application mounts a medium on its own. A drive is empty until a person mounts something.

## Consequences

- The Amiga application's Auto rule becomes content-based and gets an `AMIGA1200_APP` branch; the menu
  label says which version that build looks for. `auto_mount` goes.
- `cores/amiga/core.toml` (`roms = ["Kickstart 1.3"]`) and `cores/amiga1200/core.toml`
  (`roms = ["Kickstart 3.1"]`) were already right and stay as they are.
- Both applications are Gideon-base builds from another tree, uploaded as the generic package
  `trx64-coreapps` (`ci/package.sh`), so the change reaches a release only after that package is
  rebuilt and uploaded. Done the same day: fork commit `c116767e`, package version **2** with all seven
  applications, and `ci/package.sh` defaults to that version. The rule itself is `amiga_ks.c` with the
  host bench `target/pc/linux/amigakstest` (26 checks, control `amigakscontrol` red).
- For the generic application ([T104](../specs/done/T104-generic-core-app.md)) this is a property of the
  `rom_set` entry, not of a core module: a ROM set may be chosen by a **field inside the file**, not only
  by extension and size (§5.2), and the media slots of §5.2 have no auto-mount.
- The AmigaVision set keeps working: its Kickstart is 3.1, so the AGA core picks it. Which of two 3.1
  images Auto takes is the first by name.

## Links

- [T104](../specs/done/T104-generic-core-app.md) §5.7, [T85](../todo/T85.md), [T102](../todo/T102.md),
  [T97](../todo/T97.md)
